#include "renderer.h"
#include "load.h"
#include "fbx.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <memory>

struct per_scene_uniforms
{
	alignas(16) float3 ambient_light;
	alignas(16) float3 light_direction;
	alignas(16) float3 light_color;
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

    float3 get_axis(const coord_system & c, coord_axis axis) const { return qrot(get_orientation(c), c.get_axis(axis)); }
    float3 get_left(const coord_system & c) const { return get_axis(c, coord_axis::left); }
    float3 get_right(const coord_system & c) const { return get_axis(c, coord_axis::right); }
    float3 get_up(const coord_system & c) const { return get_axis(c, coord_axis::up); }
    float3 get_down(const coord_system & c) const { return get_axis(c, coord_axis::down); }
    float3 get_forward(const coord_system & c) const { return get_axis(c, coord_axis::forward); }
    float3 get_back(const coord_system & c) const { return get_axis(c, coord_axis::back); }
};

#include <random>

constexpr coord_system game_coords {coord_axis::east, coord_axis::north, coord_axis::up};

struct unit
{
    float2 position;
    float2 target;

    float3 get_position() const { return {position,0}; }
    float3 get_direction() const { return {normalize(target-position),0}; }
    float4 get_orientation() const { return rotation_quat(game_coords.get_axis(coord_axis::north), get_direction()); }
    float_pose get_pose() const { return {get_orientation(), get_position()}; }
    float4x4 get_model_matrix() const { return pose_matrix(get_pose()); }
};

int main() try
{
    constexpr coord_system vk_coords {coord_axis::right, coord_axis::down, coord_axis::forward};

    std::mt19937 rng; 
    std::uniform_real_distribution<float> dist{0, 64};
    std::vector<unit> units;
    for(size_t i=0; i<100; ++i) units.push_back({{dist(rng), dist(rng)}, {dist(rng), dist(rng)}});

    context ctx;
    gfx_mesh terrain_mesh {ctx, generate_box_mesh({0,0,-1}, {64,64,0})};
    gfx_mesh unit_mesh {ctx, transform(scaling_matrix(float3{0.1f}), load_mesh_from_obj(game_coords, "assets/f44a.obj"))};
    texture_2d terrain_tex {ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({127,85,25,255})};
    texture_2d unit_tex {ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/f44a.jpg")};

    // Create our sampler
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.maxLod = 11;
    sampler_info.minLod = 0;
    VkSampler sampler;
    check(vkCreateSampler(ctx.device, &sampler_info, nullptr, &sampler));

    // Set up scene contract
    auto render_pass = ctx.create_render_pass(
        {make_attachment_description(ctx.selection.surface_format.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
        make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED)
    );

    renderer r {ctx};
    auto contract = r.create_contract(render_pass, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}}, 
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto layout = r.create_pipeline_layout(contract, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},    
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    });
    
    // Set up our shader pipeline
    auto vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/static.vert");
    auto frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shader.frag");

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

    // Set up a window with swapchain framebuffers
    window win {ctx, {1280, 720}, "Example RTS"};
    depth_buffer depth {ctx, win.get_dims()};

    // Create framebuffers
    std::vector<VkFramebuffer> swapchain_framebuffers;
    for(auto & view : win.get_swapchain_image_views()) swapchain_framebuffers.push_back(ctx.create_framebuffer(render_pass, {view, depth}, win.get_dims()));

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
        if(win.get_key(GLFW_KEY_A)) camera.position += game_coords.get_axis(coord_axis::west) * (timestep * 50);
        if(win.get_key(GLFW_KEY_S)) camera.position += game_coords.get_axis(coord_axis::south) * (timestep * 50);
        if(win.get_key(GLFW_KEY_D)) camera.position += game_coords.get_axis(coord_axis::east) * (timestep * 50);
        
        // Update game logic
        float max_step = timestep * 2.0f;
        for(auto & u : units)
        {
            const auto delta = u.target - u.position;
            const auto delta_len = length(delta);
            if(delta_len < max_step) 
            {
                u.position = u.target;
                u.target = {dist(rng), dist(rng)};
            }
            else u.position += delta*(max_step/delta_len);
        }

        // Determine matrices
        const auto proj_matrix = mul(linalg::perspective_matrix(1.0f, win.get_aspect(), 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one), make_transform_4x4(game_coords, vk_coords));        

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        // Generate a draw list for the scene
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
                unit_descriptors.write_combined_image_sampler(1, 0, sampler, unit_tex);
                list.draw(*pipeline, unit_descriptors, unit_mesh);
            }
        }

        // Set up per-scene and per-view descriptor sets
        per_scene_uniforms ps;
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-2,5});
        ps.light_color = {0.8f,0.7f,0.5f};

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
        pass_begin_info.renderPass = render_pass;
        pass_begin_info.framebuffer = swapchain_framebuffers[index];
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
        check(vkEndCommandBuffer(cmd)); 

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroySampler(ctx.device, sampler, nullptr);
    for(auto framebuffer : swapchain_framebuffers) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    vkDestroyRenderPass(ctx.device, render_pass, nullptr);    
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
