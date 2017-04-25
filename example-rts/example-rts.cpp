#include "renderer.h"
#include "load.h"
#include "fbx.h"
#include "rts-game.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <memory>

struct point_light
{
	alignas(16) float3 u_position;
	alignas(16) float3 u_color;
};

struct per_scene_uniforms
{
	alignas(16) float3 ambient_light;
	alignas(16) float3 light_direction;
	alignas(16) float3 light_color;
	alignas(16) point_light u_point_lights[64];
	alignas(16) int u_num_point_lights;
};

struct per_view_uniforms
{
	alignas(16) float4x4 view_proj_matrix;
	alignas(16) float3 eye_position;
};

struct per_static_object
{
    alignas(16) float4x4 model_matrix;
};

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

void draw_fullscreen_pass(VkCommandBuffer cmd, const uint2 & dims, VkRenderPass render_pass, VkFramebuffer framebuffer, VkPipeline pipeline, VkPipelineLayout pipeline_layout, VkDescriptorSet descriptors, const gfx_mesh & fullscreen_quad)
{
    VkRenderPassBeginInfo pass_begin_info {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass_begin_info.renderPass = render_pass;
    pass_begin_info.framebuffer = framebuffer;
    pass_begin_info.renderArea.extent = {dims.x, dims.y};
    vkCmdBeginRenderPass(cmd, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport {0, 0, static_cast<float>(dims.x), static_cast<float>(dims.y), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor {0, 0, dims.x, dims.y};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
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

    std::mt19937 rng;
    std::vector<unit> units;
    std::vector<bullet> bullets;
    init_game(rng, units);

    context ctx;
    gfx_mesh quad_mesh {ctx, generate_fullscreen_quad()};
    gfx_mesh terrain_mesh {ctx, generate_box_mesh({0,0,-1}, {64,64,0})};
    gfx_mesh unit0_mesh {ctx, transform(scaling_matrix(float3{0.1f}), load_mesh_from_obj(game_coords, "assets/f44a.obj"))};
    gfx_mesh unit1_mesh {ctx, transform(scaling_matrix(float3{0.1f}), load_mesh_from_obj(game_coords, "assets/cf105.obj"))};
    gfx_mesh bullet_mesh {ctx, apply_vertex_color(generate_box_mesh({-0.05f,-0.1f,-0.05f},{+0.05f,+0.1f,0.05f}), {2,2,2})};
    texture_2d terrain_tex {ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({127,85,60,255})};
    texture_2d unit0_tex {ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/f44a.jpg")};
    texture_2d unit1_tex {ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/cf105.jpg")};
    texture_2d bullet_tex {ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({255,255,255,255})};

    // Create our sampler
    VkSamplerCreateInfo sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.maxLod = 11;
    sampler_info.minLod = 0;
    VkSamplerCreateInfo image_sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    image_sampler_info.magFilter = VK_FILTER_LINEAR;
    image_sampler_info.minFilter = VK_FILTER_LINEAR;
    image_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    image_sampler_info.maxLod = 0;
    image_sampler_info.minLod = 0;
    image_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    image_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler, image_sampler;
    check(vkCreateSampler(ctx.device, &sampler_info, nullptr, &sampler));
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

    renderer r {ctx};
    auto contract = r.create_contract(fb_render_pass, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}}, 
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto layout = r.create_pipeline_layout(contract, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT},    
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto glow_layout = r.create_pipeline_layout(contract, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT}
    });
    
    // Set up our shader pipeline
    auto vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/static.vert");
    auto frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shader.frag");
    auto glow_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/glow.frag");
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
    auto pipeline = r.create_pipeline(layout, mesh_vertex_format, {vert_shader, frag_shader}, true, true);
    auto glow_pipeline = r.create_pipeline(layout, mesh_vertex_format, {vert_shader, glow_shader}, true, true);

    auto gauss_desc_layout = ctx.create_descriptor_set_layout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto gauss_pipe_layout = ctx.create_pipeline_layout({gauss_desc_layout});
    auto add_desc_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto add_pipe_layout = ctx.create_pipeline_layout({add_desc_layout});
    auto hipass_pipe = make_pipeline(ctx.device, post_render_pass, gauss_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), hipass_frag_shader->get_shader_stage()}, false, false);
    auto hgauss_pipe = make_pipeline(ctx.device, post_render_pass, gauss_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), hgauss_frag_shader->get_shader_stage()}, false, false);
    auto vgauss_pipe = make_pipeline(ctx.device, post_render_pass, gauss_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), vgauss_frag_shader->get_shader_stage()}, false, false);
    auto add_pipe = make_pipeline(ctx.device, final_render_pass, add_pipe_layout, mesh_vertex_format->get_vertex_input_state(), {image_vert_shader->get_shader_stage(), add_frag_shader->get_shader_stage()}, false, false);

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
        if(win.get_key(GLFW_KEY_W)) camera.position += game_coords.get_axis(coord_axis::north) * (timestep * 50);
        if(win.get_key(GLFW_KEY_A)) camera.position += game_coords.get_axis(coord_axis::west ) * (timestep * 50);
        if(win.get_key(GLFW_KEY_S)) camera.position += game_coords.get_axis(coord_axis::south) * (timestep * 50);
        if(win.get_key(GLFW_KEY_D)) camera.position += game_coords.get_axis(coord_axis::east ) * (timestep * 50);
        
        advance_game(rng, units, bullets, timestep);

        // Determine matrices
        const auto proj_matrix = mul(linalg::perspective_matrix(1.0f, win.get_aspect(), 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one), make_transform_4x4(game_coords, vk_coords));        

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        // Generate a draw list for the scene
        per_scene_uniforms ps {};
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-2,5});
        ps.light_color = {0.9f,0.9f,0.9f};

        draw_list list {pool, *contract};
        {
            scene_descriptor_set terrain_descriptors {pool, *pipeline};
            terrain_descriptors.write_uniform_buffer(0, 0, pool.write_data(per_static_object{translation_matrix(float3{0,0,0})}));
            terrain_descriptors.write_combined_image_sampler(1, 0, sampler, terrain_tex);
            list.draw(*pipeline, terrain_descriptors, terrain_mesh);

            for(auto & u : units)
            {
                scene_descriptor_set unit_descriptors {pool, *pipeline};
                unit_descriptors.write_uniform_buffer(0, 0, pool.write_data(per_static_object{u.get_model_matrix()}));
                unit_descriptors.write_combined_image_sampler(1, 0, sampler, u.owner ? unit1_tex : unit0_tex);
                list.draw(*pipeline, unit_descriptors, u.owner ? unit1_mesh : unit0_mesh);
            }

            for(auto & b : bullets)
            {
                scene_descriptor_set bullet_descriptors {pool, *glow_pipeline};
                bullet_descriptors.write_uniform_buffer(0, 0, pool.write_data(per_static_object{b.get_model_matrix()}));
                list.draw(*glow_pipeline, bullet_descriptors, bullet_mesh);
                if(ps.u_num_point_lights < 64) ps.u_point_lights[ps.u_num_point_lights++] = {b.get_position(), b.owner ? float3{0.2f,0.2f,1.0f} : float3{0.5f,0.5f,0.0f}};
            }
        }

        // Set up per-scene and per-view descriptor sets
        per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, camera.get_view_matrix(game_coords));
        pv.eye_position = camera.position;

        VkDescriptorSet per_scene = pool.allocate_descriptor_set(list.contract.get_per_scene_layout());
        vkWriteDescriptorBufferInfo(ctx.device, per_scene, 0, 0, pool.write_data(ps));      

        VkDescriptorSet per_view = pool.allocate_descriptor_set(list.contract.get_per_view_layout());
        vkWriteDescriptorBufferInfo(ctx.device, per_view, 0, 0, pool.write_data(pv));

        VkCommandBuffer cmd = pool.allocate_command_buffer();

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cmd, &begin_info);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, list.contract.get_example_layout(), 0, {per_scene, per_view}, {});

        // Begin render pass
        const uint32_t index = win.begin();

        VkClearValue clear_values[] {{0, 0, 0, 1}, {1.0f, 0}};
        const uint2 dims = win.get_dims();
        VkRenderPassBeginInfo pass_begin_info {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass_begin_info.renderPass = fb_render_pass;
        pass_begin_info.framebuffer = main_framebuffer; //swapchain_framebuffers[index];
        pass_begin_info.renderArea.offset = {0, 0};
        pass_begin_info.renderArea.extent = {dims.x, dims.y};
        pass_begin_info.clearValueCount = countof(clear_values);
        pass_begin_info.pClearValues = clear_values;
        vkCmdBeginRenderPass(cmd, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport {0, 0, static_cast<float>(dims.x), static_cast<float>(dims.y), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        const VkRect2D scissor {0, 0, dims.x, dims.y};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        list.write_commands(cmd);

        vkCmdEndRenderPass(cmd); 

        auto hipass_descriptors = pool.allocate_descriptor_set(gauss_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, hipass_descriptors, 0, 0, {image_sampler, color.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, dims, post_render_pass, aux_framebuffer1, hipass_pipe, gauss_pipe_layout, hipass_descriptors, quad_mesh);

        auto hgauss_descriptors = pool.allocate_descriptor_set(gauss_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, hgauss_descriptors, 0, 0, {image_sampler, color1.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, dims, post_render_pass, aux_framebuffer2, hgauss_pipe, gauss_pipe_layout, hgauss_descriptors, quad_mesh);

        auto vgauss_descriptors = pool.allocate_descriptor_set(gauss_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, vgauss_descriptors, 0, 0, {image_sampler, color2.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, dims, post_render_pass, aux_framebuffer1, vgauss_pipe, gauss_pipe_layout, vgauss_descriptors, quad_mesh);

        auto add_descriptors = pool.allocate_descriptor_set(add_desc_layout);
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, add_descriptors, 0, 0, {image_sampler, color.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        vkWriteDescriptorCombinedImageSamplerInfo(ctx.device, add_descriptors, 1, 0, {image_sampler, color1.get_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        draw_fullscreen_pass(cmd, dims, final_render_pass, swapchain_framebuffers[index], add_pipe, add_pipe_layout, add_descriptors, quad_mesh);
        
        check(vkEndCommandBuffer(cmd)); 

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroySampler(ctx.device, sampler, nullptr);
    for(auto framebuffer : swapchain_framebuffers) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    vkDestroyRenderPass(ctx.device, fb_render_pass, nullptr);    
    vkDestroyRenderPass(ctx.device, post_render_pass, nullptr);    
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
