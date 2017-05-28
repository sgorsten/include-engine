#include "rts-game.h"
#include "utility.h"
#include "load.h"

#include <iostream>
#include <chrono>

#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "../3rdparty/stb/stb_truetype.h"

typedef struct
{
   int x0,y0,x1,y1; // coordinates of bbox in bitmap
   int xoff,yoff;
   int xadvance;
} glyph_info;
glyph_info cdata[96]; // ASCII 32..126 is 95 glyphs

VkAttachmentDescription make_attachment_description(VkFormat format, VkSampleCountFlagBits samples, VkAttachmentLoadOp load_op, VkImageLayout initial_layout=VK_IMAGE_LAYOUT_UNDEFINED, VkAttachmentStoreOp store_op=VK_ATTACHMENT_STORE_OP_DONT_CARE, VkImageLayout final_layout=VK_IMAGE_LAYOUT_UNDEFINED)
{
    return {0, format, samples, load_op, store_op, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, initial_layout, final_layout};
}

struct fps_camera
{
    float3 position;
    float yaw {}, pitch {};

    float4 get_orientation(const coord_system & c) const { return qmul(rotation_quat(c.get_up(), yaw), rotation_quat(c.get_right(), pitch)); }
    float_pose get_pose(const coord_system & c) const { return {get_orientation(c), position}; }
    float4x4 get_view_matrix(const coord_system & c) const { return pose_matrix(inverse(get_pose(c))); }
};

void begin_render_pass(VkCommandBuffer cmd, framebuffer & fb)
{
    vkCmdBeginRenderPass(cmd, fb.get_render_pass().get_vk_handle(), fb.get_vk_handle(), fb.get_bounds(), {});
}

void draw_fullscreen_quad(VkCommandBuffer cmd, const render_pass & pass, const scene_descriptor_set & descriptors, const gfx_mesh & fullscreen_quad)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, descriptors.get_pipeline_for_render_pass(pass));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, descriptors.get_pipeline_layout(), descriptors.get_descriptor_set_offset(), {descriptors.get_descriptor_set()}, {});
    vkCmdBindVertexBuffers(cmd, 0, {*fullscreen_quad.vertex_buffer}, {0});
    vkCmdBindIndexBuffer(cmd, *fullscreen_quad.index_buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, fullscreen_quad.index_count, 1, 0, 0, 0);
}

void draw_fullscreen_pass(VkCommandBuffer cmd, framebuffer & fb, const scene_descriptor_set & descriptors, const gfx_mesh & fullscreen_quad)
{
    begin_render_pass(cmd, fb);
    draw_fullscreen_quad(cmd, fb.get_render_pass(), descriptors, fullscreen_quad);
    vkCmdEndRenderPass(cmd); 
}

struct image_vertex { float2 position, texcoord; float4 color; };
struct gui_context
{
    draw_list & list;
    uint2 dims;
    uint32_t num_quads;

    gui_context(draw_list & list, const uint2 & dims) : list{list}, dims{dims} {}

    void begin_frame()
    {
        list.begin_vertices();
        list.begin_indices();
        num_quads = 0;
    }

    void draw_rect(const float4 & color, int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1)
    {
        const float fx0 = x0*2.0f/dims.x-1;
        const float fy0 = y0*2.0f/dims.y-1;
        const float fx1 = x1*2.0f/dims.x-1;
        const float fy1 = y1*2.0f/dims.y-1;
        list.write_vertex(image_vertex{{fx0,fy0},{s0,t0},color});
        list.write_vertex(image_vertex{{fx0,fy1},{s0,t1},color});
        list.write_vertex(image_vertex{{fx1,fy1},{s1,t1},color});
        list.write_vertex(image_vertex{{fx1,fy0},{s1,t0},color});
        list.write_indices(num_quads*4+uint3{0,1,2});
        list.write_indices(num_quads*4+uint3{0,2,3});
        ++num_quads;
    }

    void draw_text(const float4 & color, int x, int y, std::string_view text)
    {
        for(auto ch : text)
        {
            if(ch < 32 || ch > 126) continue;
            const auto & b = cdata[ch-32];
            const int x0 = x + b.xoff, y0 = y + b.yoff, x1 = x0 + b.x1 - b.x0, y1 = y0 + b.y1 - b.y0;
            const float s0 = (float)b.x0/512, t0 = (float)b.y0/512, s1 = (float)b.x1/512, t1 = (float)b.y1/512;
            draw_rect(color, x0, y0, x1, y1, s0, t0, s1, t1);
            x += b.xadvance;
        }
    }

    void draw_shadowed_text(const float4 & color, int x, int y, std::string_view text)
    {
        draw_text({0,0,0,color.w},x+1,y+1,text);
        draw_text(color,x,y,text);
    }

    void end_frame(const scene_material & mtl, const sampler & samp, const texture_2d & font_tex)
    {
        auto vertex_info = list.end_vertices();
        auto index_info = list.end_indices();
        auto desc = list.descriptor_set(mtl);
        desc.write_combined_image_sampler(0, 0, samp, font_tex);
        list.draw(desc, {vertex_info}, index_info, num_quads*6, 1);
    }
};

static image bake_font_bitmap(float pixel_height, int first_char, int num_chars)
{
    image im {{512,512}, VK_FORMAT_R8_UNORM};
    auto data = load_binary_file("C:/windows/fonts/arial.ttf");
    stbtt_fontinfo f {};
    if(!stbtt_InitFont(&f, data.data(), 0)) throw std::runtime_error("stbtt_InitFont(...) failed");
    memset(im.get_pixels(), 0, im.get_width()*im.get_height());
    const float scale = stbtt_ScaleForPixelHeight(&f, pixel_height);
    int x=1, y=1, bottom_y=1;
    for(int i=0; i<num_chars; ++i)
    {
        const int g = stbtt_FindGlyphIndex(&f, first_char + i);
        int advance, lsb, x0,y0,x1,y1;
        stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
        stbtt_GetGlyphBitmapBox(&f, g, scale,scale, &x0,&y0,&x1,&y1);

        const int gw = x1-x0, gh = y1-y0;
        if(x + gw + 1 >= im.get_width()) y = bottom_y, x = 1; // advance to next row
        if(y + gh + 1 >= im.get_height()) throw std::runtime_error("out of space in image");
        STBTT_assert(x+gw < pw);
        STBTT_assert(y+gh < ph);

        stbtt_MakeGlyphBitmap(&f, reinterpret_cast<uint8_t *>(im.get_pixels()+x+y*im.get_width()), gw,gh, im.get_width(), scale,scale, g);
        cdata[i].x0 = x;
        cdata[i].y0 = y;
        cdata[i].x1 = x + gw;
        cdata[i].y1 = y + gh;
        cdata[i].xoff = x0;
        cdata[i].yoff = y0;
        cdata[i].xadvance = static_cast<int>(std::round(scale * advance));
        x += gw + 1;
        if(y+gh+1 > bottom_y) bottom_y = y+gh+1;
    }
    return im;
}

int main() try
{
    constexpr coord_system vk_coords {coord_axis::right, coord_axis::down, coord_axis::forward};

    game::state g;

    renderer r {[](const char * message) { std::cerr << "validation layer: " << message << std::endl; }};
  
    texture_2d font_tex(r.ctx, bake_font_bitmap(32.0, 32, 96));

    // Create our sampler
    VkSamplerCreateInfo image_sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    image_sampler_info.magFilter = VK_FILTER_LINEAR;
    image_sampler_info.minFilter = VK_FILTER_LINEAR;
    image_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    image_sampler_info.maxLod = 0;
    image_sampler_info.minLod = 0;
    image_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    image_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler image_sampler {r.ctx, image_sampler_info};

    // Set up scene contract
    auto fb_render_pass = r.create_render_pass({make_attachment_description(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)}, make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED));
    auto shadowmap_render_pass = r.create_render_pass({}, make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto post_render_pass = r.create_render_pass({make_attachment_description(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)}, std::nullopt);
    auto final_render_pass = r.create_render_pass({make_attachment_description(r.get_swapchain_surface_format(), VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)}, std::nullopt);

    auto contract = r.create_contract({fb_render_pass, shadowmap_render_pass}, {
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}}, 
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}}
    });
    auto post_contract = r.create_contract({post_render_pass, final_render_pass}, {});

    auto image_vertex_format = r.create_vertex_format({{0, sizeof(image_vertex), VK_VERTEX_INPUT_RATE_VERTEX}}, {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(image_vertex, position)}, 
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(image_vertex, texcoord)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(image_vertex, color)},
    });
    std::vector<image_vertex> quad_verts {{{-1,-1},{0,0},{1,1,1,1}}, {{-1,+1},{0,1},{1,1,1,1}}, {{+1,+1},{1,1},{1,1,1,1}}, {{+1,-1},{1,0},{1,1,1,1}}};
    std::vector<uint3> quad_tris {{0,1,2},{0,2,3}};
    gfx_mesh quad_mesh {r.ctx, quad_verts, quad_tris};

    auto image_vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/image.vert");
    auto image_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/image.frag");
    auto hipass_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/hipass.frag");
    auto hgauss_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/hgauss.frag");
    auto vgauss_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/vgauss.frag");
    auto add_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/add.frag");
    
    auto image_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, image_frag_shader}, false, false, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    auto hipass_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, hipass_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto hgauss_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, hgauss_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto vgauss_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, vgauss_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto add_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, add_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    
    // Load our game resources
    const game::resources res {r, contract};

    // Set up a window with swapchain framebuffers
    window win {r.ctx, {1280, 720}, "Example RTS"};
    render_target color {r.ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    render_target color1 {r.ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    render_target color2 {r.ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    auto depth = make_depth_buffer(r.ctx, win.get_dims());

    // Create framebuffers
    auto main_framebuffer = r.create_framebuffer(fb_render_pass, {color.get_image_view(), depth.get_image_view()}, win.get_dims());
    auto aux_framebuffer1 = r.create_framebuffer(post_render_pass, {color1.get_image_view()}, win.get_dims());
    auto aux_framebuffer2 = r.create_framebuffer(post_render_pass, {color2.get_image_view()}, win.get_dims());
    std::vector<std::shared_ptr<framebuffer>> swapchain_framebuffers;
    for(auto & view : win.get_swapchain_image_views()) swapchain_framebuffers.push_back(r.create_framebuffer(final_render_pass, {view}, win.get_dims()));

    // Set up our transient resource pools
    const VkDescriptorPoolSize pool_sizes[]
    {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1024},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1024},
    };
    transient_resource_pool pools[3]
    {
        {r.ctx, pool_sizes, 1024},
        {r.ctx, pool_sizes, 1024},
        {r.ctx, pool_sizes, 1024},
    };
    int frame_index = 0;

    fps_camera camera {{32,32,10}};
    camera.pitch = -1.0f;
    float2 last_cursor;
    float total_time = 0;
    auto t0 = std::chrono::high_resolution_clock::now();

    size_t anim_frame = 0;
    while(!win.should_close())
    {
        glfwPollEvents();

        // Compute timestep
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto timestep = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;
        total_time += timestep;

        // Handle mouselook
        auto cursor = win.get_cursor_pos();
        if(win.get_mouse_button(GLFW_MOUSE_BUTTON_LEFT))
        {
            const auto move = float2(cursor - last_cursor);
            camera.yaw -= move.x * 0.01f;
            camera.pitch = std::max(-1.5f, std::min(1.5f, camera.pitch - move.y * 0.01f));
        }
        last_cursor = cursor;

        // Handle WASD
        if(win.get_key(GLFW_KEY_W)) camera.position += qrot(camera.get_orientation(game::coords), game::coords.get_axis(coord_axis::north) * (timestep * 50));
        if(win.get_key(GLFW_KEY_A)) camera.position += qrot(camera.get_orientation(game::coords), game::coords.get_axis(coord_axis::west ) * (timestep * 50));
        if(win.get_key(GLFW_KEY_S)) camera.position += qrot(camera.get_orientation(game::coords), game::coords.get_axis(coord_axis::south) * (timestep * 50));
        if(win.get_key(GLFW_KEY_D)) camera.position += qrot(camera.get_orientation(game::coords), game::coords.get_axis(coord_axis::east ) * (timestep * 50));
        
        g.advance(timestep);

        // Determine matrices
        const auto proj_matrix = mul(linalg::perspective_matrix(1.0f, win.get_aspect(), 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one), make_transform_4x4(game::coords, vk_coords));        

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        // Generate a draw list for the scene
        game::per_scene_uniforms ps {};
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-2,5});
        ps.light_color = {0.9f,0.9f,0.9f};
        draw_list list {pool, *contract};
        game::draw(list, ps, res, g);

        draw_list gui_list {pool, *post_contract};
        gui_context gui {gui_list, win.get_dims()};
        gui.begin_frame();
        gui.draw_shadowed_text({1,1,1,1}, 50, 50, "This is a test of font rendering");
        gui.end_frame(*image_mtl, image_sampler, font_tex);

        // Set up per-scene and per-view descriptor sets
        game::per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, camera.get_view_matrix(game::coords));
        pv.eye_position = camera.position;
        pv.eye_x_axis = qrot(camera.get_orientation(game::coords), game::coords.get_right());
        pv.eye_y_axis = qrot(camera.get_orientation(game::coords), game::coords.get_down());

        auto per_scene = list.shared_descriptor_set(0);
        per_scene.write_uniform_buffer(0, 0, list.upload_uniforms(ps));      

        auto per_view = list.shared_descriptor_set(1);
        per_view.write_uniform_buffer(0, 0, list.upload_uniforms(pv));

        VkCommandBuffer cmd = pool.allocate_command_buffer();

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cmd, &begin_info);

        vkCmdBeginRenderPass(cmd, fb_render_pass->get_vk_handle(), main_framebuffer->get_vk_handle(), main_framebuffer->get_bounds(), {{0, 0, 0, 1}, {1.0f, 0}});
        list.write_commands(cmd, *fb_render_pass, {per_scene, per_view});
        vkCmdEndRenderPass(cmd); 

        scene_descriptor_set hipass {pool, *hipass_mtl};
        hipass.write_combined_image_sampler(0, 0, image_sampler, color.get_image_view());
        draw_fullscreen_pass(cmd, *aux_framebuffer1, hipass, quad_mesh);

        scene_descriptor_set hgauss {pool, *hgauss_mtl};
        hgauss.write_combined_image_sampler(0, 0, image_sampler, color1.get_image_view());
        draw_fullscreen_pass(cmd, *aux_framebuffer2, hgauss, quad_mesh);

        scene_descriptor_set vgauss {pool, *vgauss_mtl};
        vgauss.write_combined_image_sampler(0, 0, image_sampler, color2.get_image_view());
        draw_fullscreen_pass(cmd, *aux_framebuffer1, vgauss, quad_mesh);

        scene_descriptor_set add {pool, *add_mtl};
        add.write_combined_image_sampler(0, 0, image_sampler, color.get_image_view());
        add.write_combined_image_sampler(1, 0, image_sampler, color1.get_image_view());

        const uint32_t index = win.begin();
        begin_render_pass(cmd, *swapchain_framebuffers[index]);
        draw_fullscreen_quad(cmd, swapchain_framebuffers[index]->get_render_pass(), add, quad_mesh);
        gui_list.write_commands(cmd, swapchain_framebuffers[index]->get_render_pass(), {});
        vkCmdEndRenderPass(cmd); 
        check(vkEndCommandBuffer(cmd));
        win.end(index, {cmd}, pool.get_fence());
    }

    r.wait_until_device_idle();
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
