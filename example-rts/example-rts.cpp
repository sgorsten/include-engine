#include "renderer.h"
#include "load.h"
#include "fbx.h"
#include "rts-game.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <memory>

VkAttachmentDescription make_attachment_description(VkFormat format, VkSampleCountFlagBits samples, VkAttachmentLoadOp load_op, VkImageLayout initial_layout=VK_IMAGE_LAYOUT_UNDEFINED, VkAttachmentStoreOp store_op=VK_ATTACHMENT_STORE_OP_DONT_CARE, VkImageLayout final_layout=VK_IMAGE_LAYOUT_UNDEFINED)
{
    return {0, format, samples, load_op, store_op, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, initial_layout, final_layout};
}

std::ostream & operator << (std::ostream & out, const float3 & v) { return out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }

struct fps_camera
{
    float3 position;
    float yaw {}, pitch {};

    float4 get_orientation(const coord_system & c) const { return qmul(rotation_quat(c.get_up(), yaw), rotation_quat(c.get_right(), pitch)); }
    float_pose get_pose(const coord_system & c) const { return {get_orientation(c), position}; }
    float4x4 get_view_matrix(const coord_system & c) const { return pose_matrix(inverse(get_pose(c))); }
};

void draw_fullscreen_pass(VkCommandBuffer cmd, VkRenderPass render_pass, VkFramebuffer framebuffer, const VkRect2D & rect, VkPipeline pipeline, VkPipelineLayout pipeline_layout, VkDescriptorSet descriptors, const gfx_mesh & fullscreen_quad)
{
    vkCmdBeginRenderPass(cmd, render_pass, framebuffer, rect, {});  
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, {descriptors}, {});
    vkCmdBindVertexBuffers(cmd, 0, {*fullscreen_quad.vertex_buffer}, {0});
    vkCmdBindIndexBuffer(cmd, *fullscreen_quad.index_buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, fullscreen_quad.index_count, 1, 0, 0, 0);
    vkCmdEndRenderPass(cmd); 
}

int main() try
{
    constexpr coord_system vk_coords {coord_axis::right, coord_axis::down, coord_axis::forward};

    game::state g;

    context ctx;
    renderer r {ctx};

    gfx_mesh quad_mesh {ctx, generate_fullscreen_quad()};
   

    // Create our sampler
    VkSamplerCreateInfo image_sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    image_sampler_info.magFilter = VK_FILTER_LINEAR;
    image_sampler_info.minFilter = VK_FILTER_LINEAR;
    image_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    image_sampler_info.maxLod = 0;
    image_sampler_info.minLod = 0;
    image_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    image_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler image_sampler;
    check(vkCreateSampler(ctx.device, &image_sampler_info, nullptr, &image_sampler));

    // Set up scene contract
    auto fb_render_pass = ctx.create_render_pass(
        {make_attachment_description(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)},
        make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED)
    );
    auto post_render_pass = ctx.create_render_pass(
        {make_attachment_description(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)},
        std::nullopt
    );
    auto final_render_pass = ctx.create_render_pass(
        {make_attachment_description(ctx.selection.surface_format.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
        std::nullopt
    );

    auto gauss_desc_layout = ctx.create_descriptor_set_layout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto gauss_pipe_layout = ctx.create_pipeline_layout({gauss_desc_layout});
    auto add_desc_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto add_pipe_layout = ctx.create_pipeline_layout({add_desc_layout});

    auto image_vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/image.vert");
    auto hipass_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/hipass.frag");
    auto hgauss_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/hgauss.frag");
    auto vgauss_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/vgauss.frag");
    auto add_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/add.frag");

    auto mesh_vertex_format = r.create_vertex_format({{0, sizeof(mesh::vertex), VK_VERTEX_INPUT_RATE_VERTEX}}, {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}, 
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, color)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, normal)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(mesh::vertex, texcoord)},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, tangent)},
        {5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, bitangent)},
        {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(mesh::vertex, bone_indices)},
        {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(mesh::vertex, bone_weights)}
    });

    auto hipass_pipe = make_pipeline(ctx.device, post_render_pass, gauss_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), hipass_frag_shader->get_shader_stage()}, false, false, false);
    auto hgauss_pipe = make_pipeline(ctx.device, post_render_pass, gauss_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), hgauss_frag_shader->get_shader_stage()}, false, false, false);
    auto vgauss_pipe = make_pipeline(ctx.device, post_render_pass, gauss_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), vgauss_frag_shader->get_shader_stage()}, false, false, false);
    auto add_pipe = make_pipeline(ctx.device, final_render_pass, add_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), add_frag_shader->get_shader_stage()}, false, false, false);
    
    // Load our game resources
    auto contract = r.create_contract(fb_render_pass, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}}, 
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}});
    const game::resources res {r, contract};

    // Set up a window with swapchain framebuffers
    window win {ctx, {1280, 720}, "Example RTS"};
    render_target color {ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    render_target color1 {ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    render_target color2 {ctx, win.get_dims(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    auto depth = make_depth_buffer(ctx, win.get_dims());

    // Create framebuffers
    VkFramebuffer main_framebuffer = ctx.create_framebuffer(fb_render_pass, {color.get_image_view(), depth.get_image_view()}, win.get_dims());
    VkFramebuffer aux_framebuffer1 = ctx.create_framebuffer(post_render_pass, {color1.get_image_view()}, win.get_dims());
    VkFramebuffer aux_framebuffer2 = ctx.create_framebuffer(post_render_pass, {color2.get_image_view()}, win.get_dims());
    std::vector<VkFramebuffer> swapchain_framebuffers;
    for(auto & view : win.get_swapchain_image_views()) swapchain_framebuffers.push_back(ctx.create_framebuffer(final_render_pass, {view}, win.get_dims()));

    // Set up our transient resource pools
    const VkDescriptorPoolSize pool_sizes[]
    {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1024},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1024},
    };
    transient_resource_pool pools[3]
    {
        {ctx, pool_sizes, 1024},
        {ctx, pool_sizes, 1024},
        {ctx, pool_sizes, 1024},
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

        // Set up per-scene and per-view descriptor sets
        game::per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, camera.get_view_matrix(game::coords));
        pv.eye_position = camera.position;
        pv.eye_x_axis = qrot(camera.get_orientation(game::coords), game::coords.get_right());
        pv.eye_y_axis = qrot(camera.get_orientation(game::coords), game::coords.get_down());

        VkDescriptorSet per_scene = pool.allocate_descriptor_set(list.contract.get_per_scene_layout());
        vkWriteDescriptorBufferInfo(ctx.device, per_scene, 0, 0, pool.write_data(ps));      

        VkDescriptorSet per_view = pool.allocate_descriptor_set(list.contract.get_per_view_layout());
        vkWriteDescriptorBufferInfo(ctx.device, per_view, 0, 0, pool.write_data(pv));

        VkCommandBuffer cmd = pool.allocate_command_buffer();

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cmd, &begin_info);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, list.contract.get_example_layout(), 0, {per_scene, per_view}, {});

        // Begin render pass
        VkRect2D render_area {{0,0},{win.get_dims().x,win.get_dims().y}};
        vkCmdBeginRenderPass(cmd, fb_render_pass, main_framebuffer, render_area, {{0, 0, 0, 1}, {1.0f, 0}});
        list.write_commands(cmd);
        vkCmdEndRenderPass(cmd); 

        auto hipass_descriptors = pool.allocate_descriptor_set(gauss_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, hipass_descriptors, 0, 0, {image_sampler, color.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, post_render_pass, aux_framebuffer1, render_area, hipass_pipe, gauss_pipe_layout, hipass_descriptors, quad_mesh);

        auto hgauss_descriptors = pool.allocate_descriptor_set(gauss_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, hgauss_descriptors, 0, 0, {image_sampler, color1.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, post_render_pass, aux_framebuffer2, render_area, hgauss_pipe, gauss_pipe_layout, hgauss_descriptors, quad_mesh);

        auto vgauss_descriptors = pool.allocate_descriptor_set(gauss_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, vgauss_descriptors, 0, 0, {image_sampler, color2.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, post_render_pass, aux_framebuffer1, render_area, vgauss_pipe, gauss_pipe_layout, vgauss_descriptors, quad_mesh);

        auto add_descriptors = pool.allocate_descriptor_set(add_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, add_descriptors, 0, 0, {image_sampler, color.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, add_descriptors, 1, 0, {image_sampler, color1.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        const uint32_t index = win.begin();
        draw_fullscreen_pass(cmd, final_render_pass, swapchain_framebuffers[index], render_area, add_pipe, add_pipe_layout, add_descriptors, quad_mesh);
        check(vkEndCommandBuffer(cmd));
        win.end(index, {cmd}, pool.get_fence());
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroyPipeline(ctx.device, hipass_pipe, nullptr);
    vkDestroyPipeline(ctx.device, hgauss_pipe, nullptr);
    vkDestroyPipeline(ctx.device, vgauss_pipe, nullptr);
    vkDestroyPipeline(ctx.device, add_pipe, nullptr);
    vkDestroyPipelineLayout(ctx.device, gauss_pipe_layout, nullptr);
    vkDestroyPipelineLayout(ctx.device, add_pipe_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, gauss_desc_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, add_desc_layout, nullptr);
    vkDestroySampler(ctx.device, res.linear_sampler, nullptr);
    vkDestroySampler(ctx.device, image_sampler, nullptr);
    vkDestroyFramebuffer(ctx.device, main_framebuffer, nullptr);
    vkDestroyFramebuffer(ctx.device, aux_framebuffer1, nullptr);
    vkDestroyFramebuffer(ctx.device, aux_framebuffer2, nullptr);
    for(auto framebuffer : swapchain_framebuffers) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    vkDestroyRenderPass(ctx.device, fb_render_pass, nullptr);    
    vkDestroyRenderPass(ctx.device, post_render_pass, nullptr);
    vkDestroyRenderPass(ctx.device, final_render_pass, nullptr);   
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
