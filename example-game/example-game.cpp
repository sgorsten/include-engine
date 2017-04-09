#include "vulkan.h"
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
    alignas(16) float4x4 rotation_only_view_proj_matrix;
	alignas(16) float3 eye_position;
};

struct per_skinned_object
{
    alignas(16) float4x4 bone_matrices[64];
};

VkAttachmentDescription make_attachment_description(VkFormat format, VkSampleCountFlagBits samples, VkAttachmentLoadOp load_op, VkImageLayout initial_layout=VK_IMAGE_LAYOUT_UNDEFINED, VkAttachmentStoreOp store_op=VK_ATTACHMENT_STORE_OP_DONT_CARE, VkImageLayout final_layout=VK_IMAGE_LAYOUT_UNDEFINED)
{
    return {0, format, samples, load_op, store_op, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, initial_layout, final_layout};
}

int main() try
{
    context ctx;

    // Create our textures
    texture_2d black_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({0,0,0,255}));
    texture_2d gray_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({128,128,128,255}));
    texture_2d flat_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({128,128,255,255}));
    texture_2d mutant_albedo(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/mutant-albedo.jpg")); //"assets/helmet-albedo.jpg"));
    texture_2d mutant_normal(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/mutant-normal.jpg")); //helmet-normal.jpg"));
    texture_2d akai_albedo(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/akai-albedo.jpg")); //"assets/helmet-albedo.jpg"));
    texture_2d akai_normal(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/akai-normal.jpg")); //helmet-normal.jpg"));
    texture_2d metallic_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/helmet-metallic.jpg"));
    texture_cube env_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, 
        load_image("assets/posx.jpg"), load_image("assets/negx.jpg"), 
        load_image("assets/posy.jpg"), load_image("assets/negy.jpg"),
        load_image("assets/posz.jpg"), load_image("assets/negz.jpg"));

    // Create our sampler
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.maxLod = 11;
    sampler_info.minLod = 0;
    VkSampler sampler;
    check(vkCreateSampler(ctx.device, &sampler_info, nullptr, &sampler));

    // Create our meshes
    struct gfx_mesh
    {
        std::unique_ptr<static_buffer> vertex_buffer;
        std::unique_ptr<static_buffer> index_buffer;
        uint32_t index_count;
        mesh m;

        gfx_mesh(context & ctx, const mesh & m) :
            vertex_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.vertices.size() * sizeof(mesh::vertex), m.vertices.data())},
            index_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.triangles.size() * sizeof(uint3), m.triangles.data())},
            index_count{static_cast<uint32_t>(m.triangles.size() * 3)}, m{m}
        {
        
        }

        void draw(command_buffer & cmd) const
        {
            cmd.bind_vertex_buffer(0, *vertex_buffer, 0);
            cmd.bind_index_buffer(*index_buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
            cmd.draw_indexed(index_count);
        }

        void draw(command_buffer & cmd, size_t mtl) const
        {
            cmd.bind_vertex_buffer(0, *vertex_buffer, 0);
            cmd.bind_index_buffer(*index_buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, m.materials[mtl].num_triangles*3, 1, m.materials[mtl].first_triangle*3, 0, 0);
        }
    };
    std::vector<gfx_mesh> meshes;
    for(auto & m : load_meshes_from_fbx("assets/mutant-mesh.fbx"))
    {
        meshes.push_back({ctx, m});
    }
    gfx_mesh skybox_mesh {ctx, generate_box_mesh({-10,-10,-10}, {10,10,10})};
    gfx_mesh ground_mesh {ctx, generate_box_mesh({-80,8,-80}, {80,10,80})};
    gfx_mesh box_mesh {ctx, generate_box_mesh({-1,-1,-1}, {1,1,1})};

    // Set up our layouts
    auto per_scene_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto per_view_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto per_object_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto pipeline_layout = ctx.create_pipeline_layout({per_scene_layout, per_view_layout, per_object_layout});
    auto skybox_pipeline_layout = ctx.create_pipeline_layout({per_scene_layout, per_view_layout});

    // Set up a render pass
    auto render_pass = ctx.create_render_pass(
        {make_attachment_description(ctx.selection.surface_format.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
        make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED)
    );

    // Set up our shader pipeline
    shader_compiler compiler;
    VkShaderModule static_vert_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_VERTEX_BIT, "assets/static.vert"));
    VkShaderModule skinned_vert_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_VERTEX_BIT, "assets/skinned.vert"));
    VkShaderModule frag_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shader.frag"));
    VkShaderModule skybox_vert_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_VERTEX_BIT, "assets/skybox.vert"));
    VkShaderModule skybox_frag_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/skybox.frag"));

    const VkVertexInputBindingDescription bindings[] {{0, sizeof(mesh::vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    const VkVertexInputAttributeDescription attributes[] 
    {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}, 
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(mesh::vertex, texcoord)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, tangent)},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, bitangent)},
        {5, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(mesh::vertex, bone_indices)},
        {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(mesh::vertex, bone_weights)}
    };
    const VkVertexInputAttributeDescription skybox_attributes[] 
    {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}
    };
    VkPipeline static_pipeline = make_pipeline(ctx.device, render_pass, pipeline_layout, bindings, attributes, static_vert_shader, frag_shader, true, true);
    VkPipeline skinned_pipeline = make_pipeline(ctx.device, render_pass, pipeline_layout, bindings, attributes, skinned_vert_shader, frag_shader, true, true);
    VkPipeline skybox_pipeline = make_pipeline(ctx.device, render_pass, skybox_pipeline_layout, bindings, skybox_attributes, skybox_vert_shader, skybox_frag_shader, false, false);

    // Set up a window with swapchain framebuffers
    window win {ctx, {640, 480}, "Example Game"};
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

    float3 camera_position {0,-20,-20};
    float camera_yaw {0}, camera_pitch {0};
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
            camera_yaw += move.x * 0.01f;
            camera_pitch = std::max(-1.5f, std::min(1.5f, camera_pitch - move.y * 0.01f));
        }
        last_cursor = cursor;

        // Handle WASD
        const float4 camera_orientation = qmul(rotation_quat(float3{0,1,0}, camera_yaw), rotation_quat(float3{1,0,0}, camera_pitch));
        if(win.get_key(GLFW_KEY_W)) camera_position += qzdir(camera_orientation) * (timestep * 50);
        if(win.get_key(GLFW_KEY_A)) camera_position -= qxdir(camera_orientation) * (timestep * 50);
        if(win.get_key(GLFW_KEY_S)) camera_position -= qzdir(camera_orientation) * (timestep * 50);
        if(win.get_key(GLFW_KEY_D)) camera_position += qxdir(camera_orientation) * (timestep * 50);
        
        // Determine matrices
        const auto proj_matrix = linalg::perspective_matrix(1.0f, win.get_aspect(), 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one);

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        command_buffer cmd = pool.allocate_command_buffer();

        cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        // Bind per-scene uniforms
        per_scene_uniforms ps;
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-5,-2});
        ps.light_color = {0.8f,0.7f,0.5f};

        auto per_scene = pool.allocate_descriptor_set(per_scene_layout);
        per_scene.write_uniform_buffer(0, 0, ps);      
        per_scene.write_combined_image_sampler(1, 0, sampler, env_tex);
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, per_scene, {});

        // Bind per-view uniforms
        per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, inverse(pose_matrix(camera_orientation, camera_position)));
        pv.rotation_only_view_proj_matrix = mul(proj_matrix, inverse(pose_matrix(camera_orientation, float3{0,0,0})));
        pv.eye_position = camera_position;

        auto per_view = pool.allocate_descriptor_set(per_view_layout);
        per_view.write_uniform_buffer(0, 0, pv);      
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, per_view, {});

        // Begin render pass
        const uint32_t index = win.begin();
        cmd.begin_render_pass(render_pass, swapchain_framebuffers[index], win.get_dims(), {{0, 0, 0, 1}, {1.0f, 0}});

        // Draw skybox
        cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_pipeline);
        skybox_mesh.draw(cmd);

        // Draw meshes
        cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_pipeline);
        for(auto & m : meshes)
        {
            if(++anim_frame >= m.m.animations[0].keyframes.size()) anim_frame = 0;
            auto & kf = m.m.animations[0].keyframes[anim_frame];

            per_skinned_object po {};
            for(size_t i=0; i<m.m.bones.size(); ++i)
            {   
                po.bone_matrices[i] = mul(scaling_matrix(float3{1,-1,-1}), m.m.get_bone_pose(kf.local_transforms, i), m.m.bones[i].model_to_bone_matrix);
            }

            auto per_object = pool.allocate_descriptor_set(per_object_layout);
            per_object.write_uniform_buffer(0, 0, po);
            per_object.write_combined_image_sampler(1, 0, sampler, mutant_albedo);
            per_object.write_combined_image_sampler(2, 0, sampler, mutant_normal);
            per_object.write_combined_image_sampler(3, 0, sampler, metallic_tex);
            cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 2, per_object, {});
            m.draw(cmd, 0);
            m.draw(cmd, 1);
            m.draw(cmd, 3);

            auto per_object_2 = pool.allocate_descriptor_set(per_object_layout);
            per_object_2.write_uniform_buffer(0, 0, po);
            per_object_2.write_combined_image_sampler(1, 0, sampler, akai_albedo);
            per_object_2.write_combined_image_sampler(2, 0, sampler, akai_normal);
            per_object_2.write_combined_image_sampler(3, 0, sampler, metallic_tex);   
            cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 2, per_object_2, {});
            m.draw(cmd, 2);
        }

        cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, static_pipeline);
        const float4x4 identity_matrix = translation_matrix(float3{0,0,0});
        auto per_object = pool.allocate_descriptor_set(per_object_layout);
        per_object.write_uniform_buffer(0, 0, identity_matrix);
        per_object.write_combined_image_sampler(1, 0, sampler, gray_tex);
        per_object.write_combined_image_sampler(2, 0, sampler, flat_tex);
        per_object.write_combined_image_sampler(3, 0, sampler, black_tex);
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 2, per_object, {});
        ground_mesh.draw(cmd);

        cmd.end_render_pass();
        cmd.end();

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroySampler(ctx.device, sampler, nullptr);
    vkDestroyPipeline(ctx.device, static_pipeline, nullptr);
    vkDestroyPipeline(ctx.device, skinned_pipeline, nullptr);
    vkDestroyPipeline(ctx.device, skybox_pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
    vkDestroyPipelineLayout(ctx.device, skybox_pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_object_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_view_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_scene_layout, nullptr);
    vkDestroyShaderModule(ctx.device, static_vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, skinned_vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, frag_shader, nullptr);
    vkDestroyShaderModule(ctx.device, skybox_vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, skybox_frag_shader, nullptr);
    for(auto framebuffer : swapchain_framebuffers) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    vkDestroyRenderPass(ctx.device, render_pass, nullptr);    
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}