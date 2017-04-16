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

class scene_contract
{
    friend class scene_pipeline_layout;
    context & ctx;
    VkDescriptorSetLayout per_scene_layout; // Descriptors which are shared by the entire scene
    VkDescriptorSetLayout per_view_layout; // Descriptors which vary per unique view into the scene
public:
    scene_contract(context & ctx, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings) : 
        ctx{ctx}, per_scene_layout{ctx.create_descriptor_set_layout(per_scene_bindings)}, per_view_layout{ctx.create_descriptor_set_layout(per_view_bindings)} {}

    ~scene_contract()
    {
        vkDestroyDescriptorSetLayout(ctx.device, per_view_layout, nullptr);
        vkDestroyDescriptorSetLayout(ctx.device, per_scene_layout, nullptr);
    }

    VkDescriptorSetLayout get_per_scene_layout() const { return per_scene_layout; }
    VkDescriptorSetLayout get_per_view_layout() const { return per_view_layout; }
};

class scene_pipeline_layout
{
    scene_contract & contract;
    VkDescriptorSetLayout per_object_layout;
    VkPipelineLayout pipeline_layout;
public:
    scene_pipeline_layout(scene_contract & contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings) :
        contract{contract}, per_object_layout{contract.ctx.create_descriptor_set_layout(per_object_bindings)},
        pipeline_layout{contract.ctx.create_pipeline_layout({contract.per_scene_layout, contract.per_view_layout, per_object_layout})} {}

    ~scene_pipeline_layout()
    {
        vkDestroyPipelineLayout(contract.ctx.device, pipeline_layout, nullptr);
        vkDestroyDescriptorSetLayout(contract.ctx.device, per_object_layout, nullptr);
    }

    VkDevice get_device() const { return contract.ctx.device; }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return per_object_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }
};

class scene_pipeline
{
    scene_pipeline_layout & layout;
    VkPipeline pipeline;
public:
    scene_pipeline(scene_pipeline_layout & layout, VkRenderPass render_pass,  array_view<VkVertexInputBindingDescription> vertex_bindings, array_view<VkVertexInputAttributeDescription> vertex_attributes, VkShaderModule vert_shader, VkShaderModule frag_shader, bool depth_write, bool depth_test) : 
        layout{layout}, pipeline{make_pipeline(layout.get_device(), render_pass, layout.get_pipeline_layout(), vertex_bindings, vertex_attributes, vert_shader, frag_shader, depth_write, depth_test)} {}
    
    ~scene_pipeline()
    {
        vkDestroyPipeline(layout.get_device(), pipeline, nullptr);
    }

    VkPipeline get_pipeline() const { return pipeline; }
    VkPipelineLayout get_pipeline_layout() const { return layout.get_pipeline_layout(); }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return layout.get_per_object_descriptor_set_layout(); }
};

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

struct draw_item 
{
    const scene_pipeline * pipeline;
    VkDescriptorSet set;
    const gfx_mesh * mesh;
    std::vector<size_t> mtls;
};
struct draw_list
{
    transient_resource_pool & pool;
    std::vector<draw_item> items;
    
    draw_list(transient_resource_pool & pool) : pool{pool} {}

    descriptor_set draw(const scene_pipeline & pipeline, const gfx_mesh & mesh, std::vector<size_t> mtls)
    {
        descriptor_set set = pool.allocate_descriptor_set(pipeline.get_per_object_descriptor_set_layout());
        items.push_back({&pipeline, set, &mesh, mtls});
        return set;
    }

    descriptor_set draw(const scene_pipeline & pipeline, const gfx_mesh & mesh)
    {
        std::vector<size_t> mtls;
        for(size_t i=0; i<mesh.m.materials.size(); ++i) mtls.push_back(i);
        return draw(pipeline, mesh, mtls);
    }

    void write_commands(command_buffer & cmd)
    {
        for(auto & item : items)
        {
            cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline());
            cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline_layout(), 2, item.set, {});
            for(auto mtl : item.mtls) item.mesh->draw(cmd, mtl);
        }
    }
};

int main() try
{
    context ctx;

    // Create our textures
    texture_2d black_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({0,0,0,255}));
    texture_2d gray_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({128,128,128,255}));
    texture_2d flat_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({128,128,255,255}));
    texture_2d helmet_albedo(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/helmet-albedo.jpg"));
    texture_2d helmet_normal(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/helmet-normal.jpg"));
    texture_2d helmet_metallic(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/helmet-metallic.jpg"));
    texture_2d mutant_albedo(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/mutant-albedo.jpg"));
    texture_2d mutant_normal(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/mutant-normal.jpg"));
    texture_2d akai_albedo(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/akai-albedo.jpg"));
    texture_2d akai_normal(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/akai-normal.jpg"));
    texture_2d map_2_island(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/map_2_island.jpg"));
    texture_2d map_2_objects(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/map_2_objects.jpg"));
    texture_2d map_2_terrain(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/map_2_terrain.jpg"));
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
    gfx_mesh helmet_mesh {ctx, load_meshes_from_fbx("assets/helmet-mesh.fbx")[0]};
    gfx_mesh mutant_mesh {ctx, load_meshes_from_fbx("assets/mutant-mesh.fbx")[0]};
    gfx_mesh skybox_mesh {ctx, generate_box_mesh({-10,-10,-10}, {10,10,10})};
    gfx_mesh ground_mesh {ctx, generate_box_mesh({-80,8,-80}, {80,10,80})};
    gfx_mesh box_mesh {ctx, load_meshes_from_fbx("assets/cube-mesh.fbx")[0]};
    gfx_mesh sands_mesh {ctx, load_mesh_from_obj("assets/sands location.obj")};

    // Set up our layouts

    scene_contract contract {ctx, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    }, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}
    }};

    scene_pipeline_layout metallic_layout {contract, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    }};

    scene_pipeline_layout skybox_layout {contract, {}};

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
    VkShaderModule metal_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/metal.frag"));
    VkShaderModule skybox_vert_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_VERTEX_BIT, "assets/skybox.vert"));
    VkShaderModule skybox_frag_shader = ctx.create_shader_module(compiler.compile_glsl(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/skybox.frag"));

    const VkVertexInputBindingDescription bindings[] {{0, sizeof(mesh::vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    const VkVertexInputAttributeDescription attributes[] 
    {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}, 
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, color)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, normal)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(mesh::vertex, texcoord)},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, tangent)},
        {5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, bitangent)},
        {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(mesh::vertex, bone_indices)},
        {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(mesh::vertex, bone_weights)}
    };
    const VkVertexInputAttributeDescription skybox_attributes[] 
    {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}
    };
    scene_pipeline helmet_pipeline  {metallic_layout, render_pass, bindings, attributes, static_vert_shader, metal_shader, true, true};
    scene_pipeline static_pipeline  {metallic_layout, render_pass, bindings, attributes, static_vert_shader, frag_shader, true, true};
    scene_pipeline skinned_pipeline {metallic_layout, render_pass, bindings, attributes, skinned_vert_shader, frag_shader, true, true};
    scene_pipeline skybox_pipeline  {skybox_layout,   render_pass, bindings, skybox_attributes, skybox_vert_shader, skybox_frag_shader, false, false};

    // Set up a window with swapchain framebuffers
    window win {ctx, {1280, 720}, "Example Game"};
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

        // Generate a draw list for the scene
        draw_list list {pool};
        {
            list.draw(skybox_pipeline, skybox_mesh);

            auto helmet = list.draw(helmet_pipeline, helmet_mesh);
            helmet.write_uniform_buffer(0, 0, mul(translation_matrix(float3{30, -20, 0}), scaling_matrix(float3{1,-1,-1}), helmet_mesh.m.bones[0].initial_pose.get_local_transform(), helmet_mesh.m.bones[0].model_to_bone_matrix));
            helmet.write_combined_image_sampler(1, 0, sampler, helmet_albedo);
            helmet.write_combined_image_sampler(2, 0, sampler, helmet_normal);
            helmet.write_combined_image_sampler(3, 0, sampler, helmet_metallic);

            if(++anim_frame >= mutant_mesh.m.animations[0].keyframes.size()) anim_frame = 0;
            auto & kf = mutant_mesh.m.animations[0].keyframes[anim_frame];

            per_skinned_object po {};
            for(size_t i=0; i<mutant_mesh.m.bones.size(); ++i)
            {   
                po.bone_matrices[i] = mul(scaling_matrix(float3{1,-1,-1}), mutant_mesh.m.get_bone_pose(kf.local_transforms, i), mutant_mesh.m.bones[i].model_to_bone_matrix);
            }

            auto mutant = list.draw(skinned_pipeline, mutant_mesh, {0,1,3});
            mutant.write_uniform_buffer(0, 0, po);
            mutant.write_combined_image_sampler(1, 0, sampler, mutant_albedo);
            mutant.write_combined_image_sampler(2, 0, sampler, mutant_normal);
            mutant.write_combined_image_sampler(3, 0, sampler, black_tex);

            auto akai = list.draw(skinned_pipeline, mutant_mesh, {2});
            akai.write_uniform_buffer(0, 0, po);
            akai.write_combined_image_sampler(1, 0, sampler, akai_albedo);
            akai.write_combined_image_sampler(2, 0, sampler, akai_normal);
            akai.write_combined_image_sampler(3, 0, sampler, black_tex);
       
            auto box = list.draw(static_pipeline, box_mesh);
            box.write_uniform_buffer(0, 0, mul(translation_matrix(float3{-30,-20,0}), scaling_matrix(float3{4,4,4})));
            box.write_combined_image_sampler(1, 0, sampler, gray_tex);
            box.write_combined_image_sampler(2, 0, sampler, flat_tex);
            box.write_combined_image_sampler(3, 0, sampler, black_tex);
        
            for(size_t i=0; i<sands_mesh.m.materials.size(); ++i)
            {
                auto sands = list.draw(static_pipeline, sands_mesh, {i});

                sands.write_uniform_buffer(0, 0, mul(translation_matrix(float3{0,64,27}), scaling_matrix(float3{10,-10,-10})));
                if(sands_mesh.m.materials[i].name == "map_2_island1") sands.write_combined_image_sampler(1, 0, sampler, map_2_island);
                else if(sands_mesh.m.materials[i].name == "map_2_object1") sands.write_combined_image_sampler(1, 0, sampler, map_2_objects);
                else if(sands_mesh.m.materials[i].name == "map_2_terrain1") sands.write_combined_image_sampler(1, 0, sampler, map_2_terrain);
                else sands.write_combined_image_sampler(1, 0, sampler, gray_tex);
                sands.write_combined_image_sampler(2, 0, sampler, flat_tex);
                sands.write_combined_image_sampler(3, 0, sampler, black_tex);
            }
        }

        command_buffer cmd = pool.allocate_command_buffer();

        cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        // Bind per-scene uniforms
        per_scene_uniforms ps;
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-5,-2});
        ps.light_color = {0.8f,0.7f,0.5f};

        auto per_scene = pool.allocate_descriptor_set(contract.get_per_scene_layout());
        per_scene.write_uniform_buffer(0, 0, ps);      
        per_scene.write_combined_image_sampler(1, 0, sampler, env_tex);
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_layout.get_pipeline_layout(), 0, per_scene, {});

        // Bind per-view uniforms
        per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, inverse(pose_matrix(camera_orientation, camera_position)));
        pv.rotation_only_view_proj_matrix = mul(proj_matrix, inverse(pose_matrix(camera_orientation, float3{0,0,0})));
        pv.eye_position = camera_position;

        auto per_view = pool.allocate_descriptor_set(contract.get_per_view_layout());
        per_view.write_uniform_buffer(0, 0, pv);      
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_layout.get_pipeline_layout(), 1, per_view, {});

        // Begin render pass
        const uint32_t index = win.begin();
        cmd.begin_render_pass(render_pass, swapchain_framebuffers[index], win.get_dims(), {{0, 0, 0, 1}, {1.0f, 0}});

        list.write_commands(cmd);

        cmd.end_render_pass();
        cmd.end();

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroySampler(ctx.device, sampler, nullptr);
    vkDestroyShaderModule(ctx.device, static_vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, skinned_vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, frag_shader, nullptr);
    vkDestroyShaderModule(ctx.device, metal_shader, nullptr);
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
