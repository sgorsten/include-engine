#include "renderer.h"
#include "load.h"
#include "fbx.h"
#include <iostream>
#include <chrono>
#include <algorithm>

struct per_scene_uniforms
{
    alignas(16) float4x4 cubemap_xform;
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

struct per_static_object
{
    alignas(16) float4x4 model_matrix;
};

struct per_skinned_object
{
    alignas(16) float4x4 bone_matrices[64];
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

    quatf get_orientation(const coord_system & c) const { return rotation_quat(c.get_up(), yaw) * rotation_quat(c.get_right(), pitch); }
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

int main() try
{
    constexpr coord_system game_coords {coord_axis::right, coord_axis::forward, coord_axis::up};
    constexpr coord_system vk_coords {coord_axis::right, coord_axis::down, coord_axis::forward};
    constexpr coord_system cubemap_coords {coord_axis::right, coord_axis::up, coord_axis::back};

    renderer r {[](const char * message) { std::cerr << "validation layer: " << message << std::endl; }};

    // Create our textures
    auto black_tex = r.create_texture_2d(generate_single_color_image({0,0,0,255}));
    auto gray_tex = r.create_texture_2d(generate_single_color_image({128,128,128,255}));
    auto flat_tex = r.create_texture_2d(generate_single_color_image({128,128,255,255}));
    auto helmet_albedo = r.create_texture_2d(load_image("assets/helmet-albedo.jpg", true));
    auto helmet_normal = r.create_texture_2d(load_image("assets/helmet-normal.jpg", true));
    auto helmet_metallic = r.create_texture_2d(load_image("assets/helmet-metallic.jpg", true));
    auto mutant_albedo = r.create_texture_2d(load_image("assets/mutant-albedo.jpg", true));
    auto mutant_normal = r.create_texture_2d(load_image("assets/mutant-normal.jpg", true));
    auto akai_albedo = r.create_texture_2d(load_image("assets/akai-albedo.jpg", true));
    auto akai_normal = r.create_texture_2d(load_image("assets/akai-normal.jpg", true));
    auto map_2_island = r.create_texture_2d(load_image("assets/map_2_island.jpg", true));
    auto map_2_objects = r.create_texture_2d(load_image("assets/map_2_objects.jpg", true));
    auto map_2_terrain = r.create_texture_2d(load_image("assets/map_2_terrain.jpg", true));
    auto env_tex = r.create_texture_cube(
        load_image("assets/posx.jpg", true), load_image("assets/negx.jpg", true), 
        load_image("assets/posy.jpg", true), load_image("assets/negy.jpg", true),
        load_image("assets/posz.jpg", true), load_image("assets/negz.jpg", true));

    // Create our sampler
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.maxLod = 11;
    sampler_info.minLod = 0;
    sampler sampler {r.ctx, sampler_info};

    // Create our meshes
    gfx_mesh helmet_mesh {r.ctx, load_meshes_from_fbx(game_coords, "assets/helmet-mesh.fbx")[0]};
    gfx_mesh mutant_mesh {r.ctx, load_meshes_from_fbx(game_coords, "assets/mutant-mesh.fbx")[0]};
    gfx_mesh skybox_mesh {r.ctx, invert_faces(generate_box_mesh({-10,-10,-10}, {10,10,10}))};
    gfx_mesh box_mesh {r.ctx, load_meshes_from_fbx(game_coords, "assets/cube-mesh.fbx")[0]};
    gfx_mesh sands_mesh {r.ctx, load_mesh_from_obj(game_coords, "assets/sands location.obj")};

    // Set up scene contract
    auto render_pass = r.create_render_pass(
        {make_attachment_description(r.get_swapchain_surface_format(), VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
        make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED)
    );

    auto contract = r.create_contract({render_pass}, {{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    }, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}
    }});
    
    // Set up our shader pipeline
    auto static_vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/static.vert");
    auto skinned_vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/skinned.vert");
    auto frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shader.frag");
    auto metal_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/metal.frag");
    auto skybox_vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/skybox.vert");
    auto skybox_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/skybox.frag");

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

    auto helmet_pipeline  = r.create_material(contract, mesh_vertex_format, {static_vert_shader, metal_shader}, true, true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto static_pipeline  = r.create_material(contract, mesh_vertex_format, {static_vert_shader, frag_shader}, true, true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto skinned_pipeline = r.create_material(contract, mesh_vertex_format, {skinned_vert_shader, frag_shader}, true, true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    auto skybox_pipeline  = r.create_material(contract, mesh_vertex_format, {skybox_vert_shader, skybox_frag_shader}, false, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

    // Set up a window with swapchain framebuffers
    window win {r.ctx, {1280, 720}, "Example Game"};
    auto depth = make_depth_buffer(r.ctx, win.get_dims());

    // Create framebuffers
    std::vector<std::shared_ptr<framebuffer>> swapchain_framebuffers;
    for(auto & view : win.get_swapchain_image_views()) swapchain_framebuffers.push_back(r.create_framebuffer(render_pass, {view, depth.get_image_view()}, win.get_dims()));

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

    fps_camera camera {{0,-20,20}};
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
        if(win.get_key(GLFW_KEY_W)) camera.position += camera.get_forward(game_coords) * (timestep * 50);
        if(win.get_key(GLFW_KEY_A)) camera.position += camera.get_left(game_coords) * (timestep * 50);
        if(win.get_key(GLFW_KEY_S)) camera.position += camera.get_back(game_coords) * (timestep * 50);
        if(win.get_key(GLFW_KEY_D)) camera.position += camera.get_right(game_coords) * (timestep * 50);
        
        // Determine matrices
        const auto proj_matrix = linalg::perspective_matrix(1.0f, win.get_aspect(), 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one) * make_transform_4x4(game_coords, vk_coords);        

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        // Generate a draw list for the scene
        draw_list list {pool, *contract};
        {
            scene_descriptor_set skybox_descriptors {pool, *skybox_pipeline};
            list.draw(skybox_descriptors, skybox_mesh);

            scene_descriptor_set helmet_descriptors {pool, *helmet_pipeline};
            helmet_descriptors.write_uniform_buffer(0, 0, pool.write_data(per_static_object{translation_matrix(float3{30, 0, 20}) * helmet_mesh.m.bones[0].initial_pose.get_local_transform() * helmet_mesh.m.bones[0].model_to_bone_matrix}));
            helmet_descriptors.write_combined_image_sampler(1, 0, sampler, *helmet_albedo);
            helmet_descriptors.write_combined_image_sampler(2, 0, sampler, *helmet_normal);
            helmet_descriptors.write_combined_image_sampler(3, 0, sampler, *helmet_metallic);
            list.draw(helmet_descriptors, helmet_mesh);

            if(++anim_frame >= mutant_mesh.m.animations[0].keyframes.size()) anim_frame = 0;
            auto & kf = mutant_mesh.m.animations[0].keyframes[anim_frame];

            per_skinned_object po {};
            for(size_t i=0; i<mutant_mesh.m.bones.size(); ++i) po.bone_matrices[i] = mutant_mesh.m.get_bone_pose(kf.local_transforms, i) * mutant_mesh.m.bones[i].model_to_bone_matrix;
            auto podata = pool.write_data(po);

            auto mutant = list.descriptor_set(*skinned_pipeline);
            mutant.write_uniform_buffer(0, 0, podata);
            mutant.write_combined_image_sampler(1, 0, sampler, *mutant_albedo);
            mutant.write_combined_image_sampler(2, 0, sampler, *mutant_normal);
            mutant.write_combined_image_sampler(3, 0, sampler, *black_tex);
            list.draw(mutant, mutant_mesh, {0,1,3});

            auto akai = list.descriptor_set(*skinned_pipeline);
            akai.write_uniform_buffer(0, 0, podata);
            akai.write_combined_image_sampler(1, 0, sampler, *akai_albedo);
            akai.write_combined_image_sampler(2, 0, sampler, *akai_normal);
            akai.write_combined_image_sampler(3, 0, sampler, *black_tex);
            list.draw(akai, mutant_mesh, {2});
       
            auto box = list.descriptor_set(*static_pipeline);
            box.write_uniform_buffer(0, 0, pool.write_data(per_static_object{translation_matrix(float3{-30,0,20}) * scaling_matrix(float3{4,4,4})}));
            box.write_combined_image_sampler(1, 0, sampler, *gray_tex);
            box.write_combined_image_sampler(2, 0, sampler, *flat_tex);
            box.write_combined_image_sampler(3, 0, sampler, *black_tex);
            list.draw(box, box_mesh);
        
            for(size_t i=0; i<sands_mesh.m.materials.size(); ++i)
            {
                auto sands = list.descriptor_set(*static_pipeline);
                sands.write_uniform_buffer(0, 0, pool.write_data(per_static_object{translation_matrix(float3{0,27,-64}) * scaling_matrix(float3{10,10,10})}));
                if(sands_mesh.m.materials[i].name == "map_2_island1") sands.write_combined_image_sampler(1, 0, sampler, *map_2_island);
                else if(sands_mesh.m.materials[i].name == "map_2_object1") sands.write_combined_image_sampler(1, 0, sampler, *map_2_objects);
                else if(sands_mesh.m.materials[i].name == "map_2_terrain1") sands.write_combined_image_sampler(1, 0, sampler, *map_2_terrain);
                else sands.write_combined_image_sampler(1, 0, sampler, *gray_tex);
                sands.write_combined_image_sampler(2, 0, sampler, *flat_tex);
                sands.write_combined_image_sampler(3, 0, sampler, *black_tex);
                list.draw(sands, sands_mesh, {i});
            }
        }

        // Set up per-scene and per-view descriptor sets
        per_scene_uniforms ps;
        ps.cubemap_xform = make_transform_4x4(game_coords, cubemap_coords);
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-2,5});
        ps.light_color = {0.8f,0.7f,0.5f};

        per_view_uniforms pv;
        pv.view_proj_matrix = proj_matrix * camera.get_view_matrix(game_coords);
        pv.rotation_only_view_proj_matrix = proj_matrix * inverse(pose_matrix(camera.get_orientation(game_coords), float3{0,0,0}));
        pv.eye_position = camera.position;

        auto per_scene = list.shared_descriptor_set(0);
        per_scene.write_uniform_buffer(0, 0, list.upload_uniforms(ps));      
        per_scene.write_combined_image_sampler(1, 0, sampler, *env_tex);

        auto per_view = list.shared_descriptor_set(1);
        per_view.write_uniform_buffer(0, 0, list.upload_uniforms(pv));

        VkCommandBuffer cmd = pool.allocate_command_buffer();

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cmd, &begin_info);

        // Begin render pass
        const uint32_t index = win.begin();
        const uint2 dims = win.get_dims();
        vkCmdBeginRenderPass(cmd, render_pass->get_vk_handle(), swapchain_framebuffers[index]->get_vk_handle(), {{0,0},{dims.x,dims.y}}, {{0, 0, 0, 1}, {1.0f, 0}});
        list.write_commands(cmd, *render_pass, {per_scene, per_view});
        vkCmdEndRenderPass(cmd); 
        check(vkEndCommandBuffer(cmd)); 

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    r.wait_until_device_idle();
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
