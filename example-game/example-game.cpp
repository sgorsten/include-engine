#include "vulkan.h"
#include "load.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <memory>

VkPipeline make_pipeline(VkDevice device, array_view<VkVertexInputBindingDescription> vertex_bindings, array_view<VkVertexInputAttributeDescription> vertex_attributes,    
    VkPipelineLayout layout, VkShaderModule vert_shader, VkShaderModule frag_shader, VkRenderPass render_pass);

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

VkAttachmentDescription make_attachment_description(VkFormat format, VkSampleCountFlagBits samples, VkAttachmentLoadOp load_op, VkImageLayout initial_layout=VK_IMAGE_LAYOUT_UNDEFINED, VkAttachmentStoreOp store_op=VK_ATTACHMENT_STORE_OP_DONT_CARE, VkImageLayout final_layout=VK_IMAGE_LAYOUT_UNDEFINED)
{
    VkAttachmentDescription desc {};
    desc.format = format;
    desc.samples = samples;
    desc.loadOp = load_op;
    desc.storeOp = store_op;
    desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    desc.initialLayout = initial_layout;
    desc.finalLayout = final_layout;
    return desc;
}

int main() try
{
    context ctx;

    // Create our textures
    texture_2d albedo_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/helmet-albedo.jpg"));
    texture_2d normal_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/helmet-normal.jpg"));
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
        std::unique_ptr<dynamic_buffer> vertex_buffer;
        std::unique_ptr<dynamic_buffer> index_buffer;
        VkDescriptorBufferInfo vertices;
        VkDescriptorBufferInfo indices;
        size_t index_count;
    };
    std::vector<gfx_mesh> meshes;
    for(auto & m : load_meshes_from_fbx("assets/helmet-mesh.fbx"))
    {
        gfx_mesh mesh;
        mesh.vertex_buffer = std::make_unique<dynamic_buffer>(ctx, m.vertices.size() * sizeof(mesh::vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        mesh.index_buffer = std::make_unique<dynamic_buffer>(ctx, m.triangles.size() * sizeof(uint3), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        mesh.index_count = m.triangles.size() * 3;
        mesh.vertices = mesh.vertex_buffer->write(m.vertices.size() * sizeof(mesh::vertex), m.vertices.data());
        mesh.indices = mesh.index_buffer->write(m.triangles.size() * sizeof(uint3), m.triangles.data());
        meshes.push_back(std::move(mesh));
    }

    // Set up our layouts
    auto per_scene_layout = ctx.create_descriptor_set_layout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto per_view_layout = ctx.create_descriptor_set_layout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto per_object_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    });
    auto pipeline_layout = ctx.create_pipeline_layout({per_scene_layout, per_view_layout, per_object_layout});

    // Set up a render pass
    auto render_pass = ctx.create_render_pass(
        {make_attachment_description(ctx.selection.surface_format.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
        make_attachment_description(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED)
    );

    // Set up our shader pipeline
    VkShaderModule vert_shader = ctx.create_shader_module(load_spirv_binary("assets/shader.vert.spv"));
    VkShaderModule frag_shader = ctx.create_shader_module(load_spirv_binary("assets/shader.frag.spv"));

    const VkVertexInputBindingDescription bindings[] {{0, sizeof(mesh::vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    const VkVertexInputAttributeDescription attributes[] 
    {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}, 
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(mesh::vertex, texcoord)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, tangent)},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, bitangent)}
    };
    VkPipeline pipeline = make_pipeline(ctx.device, bindings, attributes, pipeline_layout, vert_shader, frag_shader, render_pass);

    // Set up a window with swapchain framebuffers
    window win {ctx, 640, 480};
    depth_buffer depth {ctx, win.get_width(), win.get_height()};

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

    float3 camera_position {0,0,-20};
    float camera_yaw {0}, camera_pitch {0};
    float2 last_cursor;
    auto t0 = std::chrono::high_resolution_clock::now();
    while(!win.should_close())
    {
        glfwPollEvents();

        // Compute timestep
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto timestep = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;

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
        int width=win.get_width(), height=win.get_height();
        const auto proj_matrix = linalg::perspective_matrix(1.0f, (float)width/height, 1.0f, 1000.0f, linalg::pos_z, linalg::zero_to_one);
        const auto view_matrix = inverse(pose_matrix(camera_orientation, camera_position));

        // Render a frame
        auto & pool = pools[frame_index];
        frame_index = (frame_index+1)%3;
        pool.reset();

        command_buffer cmd = pool.allocate_command_buffer();

        cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        // Bind per-view uniforms
        auto per_scene = pool.allocate_descriptor_set(per_scene_layout);
        per_scene_uniforms ps;
        ps.ambient_light = {0.01f,0.01f,0.01f};
        ps.light_direction = normalize(float3{1,-5,-2});
        ps.light_color = {0.8f,0.7f,0.5f};
        per_scene.write_uniform_buffer(0, 0, sizeof(ps), &ps);      
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, per_scene, {});

        auto per_view = pool.allocate_descriptor_set(per_view_layout);
        per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, view_matrix);
        pv.eye_position = camera_position;
        per_view.write_uniform_buffer(0, 0, sizeof(pv), &pv);      
        cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, per_view, {});

        const uint32_t index = win.begin();
        cmd.begin_render_pass(render_pass, swapchain_framebuffers[index], win.get_dims(), {{0, 0, 0, 1}, {1.0f, 0}});

        cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for(auto & m : meshes)
        {
            for(int i=0; i<9; ++i)
            {
                float4x4 model_matrix = linalg::translation_matrix(float3{i*10.0f-40,0,0});

                auto per_object = pool.allocate_descriptor_set(per_object_layout);
                per_object.write_uniform_buffer(0, 0, sizeof(model_matrix), &model_matrix);
                per_object.write_combined_image_sampler(1, 0, sampler, albedo_tex);
                per_object.write_combined_image_sampler(2, 0, sampler, normal_tex);
                per_object.write_combined_image_sampler(3, 0, sampler, metallic_tex);
                per_object.write_combined_image_sampler(4, 0, sampler, env_tex);
                cmd.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 2, per_object, {});

                cmd.bind_vertex_buffer(0, m.vertices.buffer, m.vertices.offset);
                cmd.bind_index_buffer(m.indices.buffer, m.indices.offset, VkIndexType::VK_INDEX_TYPE_UINT32);
                cmd.draw_indexed(m.index_count);
            }
        }

        cmd.end_render_pass();
        cmd.end();

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroySampler(ctx.device, sampler, nullptr);
    vkDestroyPipeline(ctx.device, pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_object_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_view_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_scene_layout, nullptr);
    vkDestroyShaderModule(ctx.device, vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, frag_shader, nullptr);
    for(auto framebuffer : swapchain_framebuffers) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    vkDestroyRenderPass(ctx.device, render_pass, nullptr);    
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

VkPipeline make_pipeline(VkDevice device, array_view<VkVertexInputBindingDescription> vertex_bindings, array_view<VkVertexInputAttributeDescription> vertex_attributes,    
    VkPipelineLayout layout, VkShaderModule vert_shader, VkShaderModule frag_shader, VkRenderPass render_pass)
{
    const VkPipelineShaderStageCreateInfo shader_stages[]
    {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_shader, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader, "main"}
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = vertex_bindings.size;
    vertexInputInfo.pVertexBindingDescriptions = vertex_bindings.data;
    vertexInputInfo.vertexAttributeDescriptionCount = vertex_attributes.size;
    vertexInputInfo.pVertexAttributeDescriptions = vertex_attributes.data;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    const VkViewport viewport {};
    const VkRect2D scissor {};
    VkPipelineViewportStateCreateInfo viewportState {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; //VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; /// Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

    VkPipelineColorBlendStateCreateInfo colorBlending {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    const VkDynamicState dynamic_states[] {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = countof(dynamic_states);
    dynamicState.pDynamicStates = dynamic_states;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS;

    VkGraphicsPipelineCreateInfo pipelineInfo {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = countof(shader_stages);
    pipelineInfo.pStages = shader_stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depth_stencil_state;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = render_pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    return pipeline;
}