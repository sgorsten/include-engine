#include "rts-game.h"
#include "sprite.h"
#include "utility.h"
#include "load.h"

#include <iostream>
#include <chrono>

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

void draw_fullscreen_pass(VkCommandBuffer cmd, framebuffer & fb, const scene_descriptor_set & descriptors, const gfx_mesh & fullscreen_quad, const draw_list * additional_draws=nullptr)
{
    vkCmdBeginRenderPass(cmd, fb.get_render_pass().get_vk_handle(), fb.get_vk_handle(), fb.get_bounds(), {});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, descriptors.get_pipeline_for_render_pass(fb.get_render_pass()));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, descriptors.get_pipeline_layout(), descriptors.get_descriptor_set_offset(), {descriptors.get_descriptor_set()}, {});
    vkCmdBindVertexBuffers(cmd, 0, {*fullscreen_quad.vertex_buffer}, {0});
    vkCmdBindIndexBuffer(cmd, *fullscreen_quad.index_buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, fullscreen_quad.index_count, 1, 0, 0, 0);
    if(additional_draws) additional_draws->write_commands(cmd, fb.get_render_pass(), {});
    vkCmdEndRenderPass(cmd); 
}

int main() try
{
    constexpr coord_system vk_coords {coord_axis::right, coord_axis::down, coord_axis::forward};

    game::state g;

    sprite_sheet sprites;
    gui_sprites gs {sprites};
    const font_face font {sprites, "C:/windows/fonts/arial.ttf", 32.0f};
    sprites.prepare_sheet();

    renderer r {[](const char * message) { std::cerr << "validation layer: " << message << std::endl; }}; 
    sprites.texture = r.create_texture_2d(sprites.sheet);

    // Create our sampler
    VkSamplerCreateInfo image_sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    image_sampler_info.magFilter = VK_FILTER_LINEAR;
    image_sampler_info.minFilter = VK_FILTER_LINEAR;
    image_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    image_sampler_info.minLod = 0;
    image_sampler_info.maxLod = 0;
    image_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    image_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler image_sampler {r.ctx, image_sampler_info};

    // Create our sampler
    VkSamplerCreateInfo shadow_sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    shadow_sampler_info.magFilter = VK_FILTER_LINEAR;
    shadow_sampler_info.minFilter = VK_FILTER_LINEAR;
    shadow_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadow_sampler_info.minLod = 0;
    shadow_sampler_info.maxLod = 0;
    shadow_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow_sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    shadow_sampler_info.compareEnable = VK_TRUE;
    shadow_sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sampler shadow_sampler {r.ctx, shadow_sampler_info};

    // Set up scene contract
    auto fb_render_pass = r.create_render_pass({make_attachment_description(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)}, 
        make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED));
    auto shadowmap_render_pass = r.create_render_pass({}, make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL), true);
    auto post_render_pass = r.create_render_pass({make_attachment_description(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)}, std::nullopt);
    auto final_render_pass = r.create_render_pass({make_attachment_description(r.get_swapchain_surface_format(), VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)}, std::nullopt);

    auto contract = r.create_contract({fb_render_pass, shadowmap_render_pass}, {
        {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}, // PerScene uniform block
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT} //uniform sampler2D u_shadow_map;
        }, 
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
    auto sample_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/sample.frag");
    
    auto image_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, image_frag_shader}, false, false, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    auto hipass_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, hipass_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto hgauss_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, hgauss_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto vgauss_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, vgauss_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto add_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, add_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto sample_mtl = r.create_material(post_contract, image_vertex_format, {image_vert_shader, sample_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

    // Load our game resources
    const game::resources res {r, contract};

    // Set up a window with swapchain framebuffers
    window win {r.ctx, {1280, 720}, "Example RTS"};
    render_target color {r.ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    render_target color1 {r.ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    render_target color2 {r.ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    auto depth = make_depth_buffer(r.ctx, win.get_dims());
    render_target shadowmap {r.ctx, {2048,2048}, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT};

    // Create framebuffers
    auto main_framebuffer = r.create_framebuffer(fb_render_pass, {color.get_image_view(), depth.get_image_view()}, win.get_dims());
    auto shadow_framebuffer = r.create_framebuffer(shadowmap_render_pass, {shadowmap.get_image_view()}, {2048,2048});
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
        if(!win.get_key(GLFW_KEY_SPACE)) g.advance(timestep);

        // Determine matrices
        const auto proj_matrix = mul(linalg::perspective_matrix(1.0f, win.get_aspect(), 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one), make_transform_4x4(game::coords, vk_coords));        

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        // Generate a draw list for the scene
        fps_camera shadow_camera;        
        shadow_camera.position = {32,32,40};
        shadow_camera.yaw = 0;
        shadow_camera.pitch = -1.57f;

        //camera = shadow_camera;
        const float4x4 shadow_bias_matrix = {{0.5f,0,0,0},{0,0.5f,0,0},{0,0,1,0},{0.5f,0.5f,0,1}};
        const auto shadow_proj_matrix = mul(linalg::perspective_matrix(1.57f, 1.0f, 20.0f, 60.0f, linalg::pos_z, linalg::zero_to_one), make_transform_4x4(game::coords, vk_coords)); 
        game::per_view_uniforms pv_shadow;
        pv_shadow.view_proj_matrix = mul(shadow_proj_matrix, shadow_camera.get_view_matrix(game::coords));
        pv_shadow.eye_position = shadow_camera.position;
        pv_shadow.eye_x_axis = qrot(shadow_camera.get_orientation(game::coords), game::coords.get_right());
        pv_shadow.eye_y_axis = qrot(shadow_camera.get_orientation(game::coords), game::coords.get_down());

        game::per_scene_uniforms ps {};
        ps.shadow_map_matrix = mul(shadow_bias_matrix, pv_shadow.view_proj_matrix);
        ps.shadow_light_pos = pv_shadow.eye_position;
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-2,5});
        ps.light_color = {0.9f,0.9f,0.9f};
        draw_list list {pool, *contract};
        game::draw(list, ps, res, g);

        draw_list gui_list {pool, *post_contract};
        gui_context gui {gs, gui_list, win.get_dims()};
        auto r = rect{0,0,(int)win.get_dims().x,(int)win.get_dims().y};
        gui.begin_frame();
        //gui.draw_sprite_sheet({10,10});
        r = r.take_y1(250);
        auto r0 = r.take_x0(250); gui.draw_partial_rounded_rect(r0, 32, {0,0,0,0.5f}, false, true, false, false); gui.draw_partial_rounded_rect(r0.adjusted(0,4,-4,0), 28, {0,0,0,0.5f}, false, true, false, false);
        auto r1 = r.take_x1(350); gui.draw_partial_rounded_rect(r1, 32, {0,0,0,0.5f}, true, false, false, false); gui.draw_partial_rounded_rect(r1.adjusted(4,4,0,0), 28, {0,0,0,0.5f}, true, false, false, false);
        auto r2 = r.take_y1(200); gui.draw_rect(r2, {0,0,0,0.5f}); gui.draw_rect(r2.adjusted(-4,4,4,0), {0,0,0,0.5f});
        gui.draw_shadowed_text(font, {1,1,1,1}, r2.x0+10, r2.y0+40, "This is a test of font rendering");
        gui.end_frame(*image_mtl, image_sampler);

        // Set up per-scene and per-view descriptor sets
        game::per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, camera.get_view_matrix(game::coords));
        pv.eye_position = camera.position;
        pv.eye_x_axis = qrot(camera.get_orientation(game::coords), game::coords.get_right());
        pv.eye_y_axis = qrot(camera.get_orientation(game::coords), game::coords.get_down());

        auto per_scene = list.shared_descriptor_set(0);
        per_scene.write_uniform_buffer(0, 0, list.upload_uniforms(ps));      
        per_scene.write_combined_image_sampler(1, 0, shadow_sampler, shadowmap.get_image_view(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        auto per_view = list.shared_descriptor_set(1);
        per_view.write_uniform_buffer(0, 0, list.upload_uniforms(pv));

        auto per_view_shadow = list.shared_descriptor_set(1);
        per_view_shadow.write_uniform_buffer(0, 0, list.upload_uniforms(pv_shadow));

        VkCommandBuffer cmd = pool.allocate_command_buffer();

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cmd, &begin_info);

        vkCmdBeginRenderPass(cmd, shadowmap_render_pass->get_vk_handle(), shadow_framebuffer->get_vk_handle(), shadow_framebuffer->get_bounds(), {{1.0f, 0}});
        list.write_commands(cmd, *shadowmap_render_pass, {per_scene, per_view_shadow});
        vkCmdEndRenderPass(cmd); 

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
        draw_fullscreen_pass(cmd, *swapchain_framebuffers[index], add, quad_mesh, &gui_list);
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
