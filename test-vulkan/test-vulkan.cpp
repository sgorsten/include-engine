#include "render.h"
#include "fbx.h"
#include "data-types.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <memory>

std::vector<uint32_t> load_spirv_binary(const char * path)
{
    FILE * f = fopen(path, "rb");
    if(!f) throw std::runtime_error(std::string("failed to open ") + path);
    fseek(f, 0, SEEK_END);
    auto len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> words(len/4);
    if(fread(words.data(), sizeof(uint32_t), words.size(), f) != words.size()) throw std::runtime_error(std::string("failed to read ") + path);
    fclose(f);
    return words;
}

VkShaderModule load_spirv_module(VkDevice device, const char * path)
{
    auto words = load_spirv_binary(path);

    VkShaderModuleCreateInfo create_info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create_info.codeSize = words.size() * sizeof(uint32_t);
    create_info.pCode = words.data();
    VkShaderModule module;
    check(vkCreateShaderModule(device, &create_info, nullptr, &module));
    return module;
}

template<class Vertex, int N> VkVertexInputAttributeDescription make_attribute(uint32_t location, uint32_t binding, linalg::vec<float,N> (Vertex::*attribute))
{
    const Vertex vertex {};
    switch(N)
    {
    case 1: return {location, binding, VK_FORMAT_R32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, *attribute))};
    case 2: return {location, binding, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, *attribute))};
    case 3: return {location, binding, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, *attribute))};
    case 4: return {location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, *attribute))};
    default: throw std::logic_error("unsupported attribute type");
    }
}

VkPipeline make_pipeline(VkDevice device, array_view<VkVertexInputBindingDescription> vertex_bindings, array_view<VkVertexInputAttributeDescription> vertex_attributes,    
    VkPipelineLayout layout, VkShaderModule vert_shader, VkShaderModule frag_shader, VkRenderPass render_pass);

struct per_view_uniforms
{
	alignas(16) float4x4 view_proj_matrix;
	alignas(16) float3 eye_position;
	alignas(16) float3 ambient_light;
	alignas(16) float3 light_direction;
	alignas(16) float3 light_color;
};

int main() try
{
    std::ifstream in("../example-game/assets/helmet-mesh.fbx", std::ifstream::binary);
    const auto doc = fbx::load(in);
    std::cout << "FBX Version " << doc.version << std::endl;
    for(auto & node : doc.nodes) std::cout << node << std::endl;
    auto models = load_models(doc);

    // Adjust coordinates and compute tangent space basis
    for(auto & m : models) for(auto & g : m.geoms)
    {
        for(auto & v : g.vertices)
        {
            v.position *= float3{1,-1,-1};
            v.normal *= float3{1,-1,-1};
        }
        for(auto t : g.triangles)
        {
            auto & v0 = g.vertices[t.x], & v1 = g.vertices[t.y], & v2 = g.vertices[t.z];
            const float3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
            const float2 d1 = v1.texcoord - v0.texcoord, d2 = v2.texcoord - v0.texcoord;
            const float3 dpds = float3(d2.y * e1.x - d1.y * e2.x, d2.y * e1.y - d1.y * e2.y, d2.y * e1.z - d1.y * e2.z) / cross(d1, d2);
            const float3 dpdt = float3(d1.x * e2.x - d2.x * e1.x, d1.x * e2.y - d2.x * e1.y, d1.x * e2.z - d2.x * e1.z) / cross(d1, d2);
            v0.tangent += dpds; v1.tangent += dpds; v2.tangent += dpds;
            v0.bitangent += dpdt; v1.bitangent += dpdt; v2.bitangent += dpdt;
        }
        for(auto & v : g.vertices)
        {
            v.tangent = normalize(v.tangent);
            v.bitangent = normalize(v.bitangent);
        }
    }

    image albedo("../example-game/assets/helmet-albedo.jpg");
    image normal("../example-game/assets/helmet-normal.jpg");
    image metallic("../example-game/assets/helmet-metallic.jpg");
    std::cout << albedo.get_channels() << std::endl;

    context ctx;

    // Create our texture
    texture_2d albedo_tex(ctx, albedo.get_width(), albedo.get_height(), VK_FORMAT_R8G8B8A8_UNORM, albedo.get_pixels());
    texture_2d normal_tex(ctx, normal.get_width(), normal.get_height(), VK_FORMAT_R8G8B8A8_UNORM, normal.get_pixels());
    texture_2d metallic_tex(ctx, metallic.get_width(), metallic.get_height(), VK_FORMAT_R8G8B8A8_UNORM, metallic.get_pixels());
    texture_cube env_tex(ctx, VK_FORMAT_R8G8B8A8_UNORM, "../example-game/assets/posx.jpg", "../example-game/assets/negx.jpg", "../example-game/assets/posy.jpg", 
        "../example-game/assets/negy.jpg", "../example-game/assets/posz.jpg", "../example-game/assets/negz.jpg");

    // Create our sampler
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    VkSampler sampler;
    check(vkCreateSampler(ctx.device, &sampler_info, nullptr, &sampler));

    // Create our meshes
    struct mesh
    {
        float4x4 model_matrix;
        std::unique_ptr<dynamic_buffer> vertex_buffer;
        std::unique_ptr<dynamic_buffer> index_buffer;
        VkDescriptorBufferInfo vertices;
        VkDescriptorBufferInfo indices;
        size_t index_count;
    };
    std::vector<mesh> meshes;
    for(auto & m : models)
    {
        for(auto & g : m.geoms)
        {
            mesh mesh;
            mesh.model_matrix = m.get_model_matrix();
            mesh.vertex_buffer = std::make_unique<dynamic_buffer>(ctx, g.vertices.size() * sizeof(fbx::geometry::vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            mesh.index_buffer = std::make_unique<dynamic_buffer>(ctx, g.triangles.size() * sizeof(uint3), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            mesh.index_count = g.triangles.size() * 3;
            mesh.vertices = mesh.vertex_buffer->write(g.vertices.size() * sizeof(fbx::geometry::vertex), g.vertices.data());
            mesh.indices = mesh.index_buffer->write(g.triangles.size() * sizeof(uint3), g.triangles.data());
            meshes.push_back(std::move(mesh));
        }
    }

    // Set up our layouts
    auto per_view_layout = ctx.create_descriptor_set_layout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}});
    auto per_object_layout = ctx.create_descriptor_set_layout({
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    });
    auto pipeline_layout = ctx.create_pipeline_layout({per_view_layout, per_object_layout});

    // Set up a render pass
    VkAttachmentDescription attachments[2] {};
    attachments[0].format = ctx.selection.surface_format.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAttachmentReference attachment_refs[] 
    {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    VkSubpassDescription subpass_desc {};
    subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_desc.colorAttachmentCount = 1;
    subpass_desc.pColorAttachments = &attachment_refs[0];
    subpass_desc.pDepthStencilAttachment = &attachment_refs[1];
    
    VkRenderPassCreateInfo render_pass_info {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = countof(attachments);
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass_desc;

    VkRenderPass render_pass;
    check(vkCreateRenderPass(ctx.device, &render_pass_info, nullptr, &render_pass));

    // Set up our shader pipeline
    VkShaderModule vert_shader = load_spirv_module(ctx.device, "assets/shader.vert.spv");
    VkShaderModule frag_shader = load_spirv_module(ctx.device, "assets/shader.frag.spv");

    const VkVertexInputBindingDescription bindings[] {{0, sizeof(fbx::geometry::vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    const VkVertexInputAttributeDescription attributes[] 
    {
        make_attribute(0, 0, &fbx::geometry::vertex::position), 
        make_attribute(1, 0, &fbx::geometry::vertex::normal),
        make_attribute(2, 0, &fbx::geometry::vertex::texcoord),
        make_attribute(3, 0, &fbx::geometry::vertex::tangent),
        make_attribute(4, 0, &fbx::geometry::vertex::bitangent)
    };
    VkPipeline pipeline = make_pipeline(ctx.device, bindings, attributes, pipeline_layout, vert_shader, frag_shader, render_pass);

    // Set up a window with swapchain framebuffers
    window win {ctx, 640, 480};
    depth_buffer depth {ctx, win.get_width(), win.get_height()};

    // Create framebuffers
    std::vector<VkFramebuffer> swapchain_framebuffers(win.get_swapchain_image_views().size());
    for(uint32_t i=0; i<swapchain_framebuffers.size(); ++i)
    {
        VkImageView attachments[] {win.get_swapchain_image_views()[i], depth};
        VkFramebufferCreateInfo framebuffer_info {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = countof(attachments);
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = win.get_width();
        framebuffer_info.height = win.get_height();
        framebuffer_info.layers = 1;

        check(vkCreateFramebuffer(ctx.device, &framebuffer_info, nullptr, &swapchain_framebuffers[i]));
    }

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

        VkCommandBuffer cmd = pool.allocate_command_buffer();

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        // Bind per-view uniforms
        auto per_view = pool.allocate_descriptor_set(per_view_layout);
        per_view_uniforms pv;
        pv.view_proj_matrix = mul(proj_matrix, view_matrix);
        pv.eye_position = camera_position;
        pv.ambient_light = {0.01f,0.01f,0.01f};
        pv.light_direction = normalize(float3{1,-5,-2});
        pv.light_color = {0.8f,0.7f,0.5f};
        per_view.write_uniform_buffer(0, 0, sizeof(pv), &pv);      
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &per_view, 0, nullptr);

        const uint32_t index = win.begin();
        VkRenderPassBeginInfo pass_begin_info {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass_begin_info.renderPass = render_pass;
        pass_begin_info.framebuffer = swapchain_framebuffers[index];
        pass_begin_info.renderArea.offset = {0, 0};
        pass_begin_info.renderArea.extent = {win.get_width(), win.get_height()};
        VkClearValue clear_values[2];
        clear_values[0].color = {0, 0, 0, 1};
        clear_values[1].depthStencil = {1.0f, 0};
        pass_begin_info.clearValueCount = countof(clear_values);
        pass_begin_info.pClearValues = clear_values;
        vkCmdBeginRenderPass(cmd, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const VkViewport viewport {0, 0, static_cast<float>(win.get_width()), static_cast<float>(win.get_height()), 0.0f, 1.0f};
        const VkRect2D scissor {0, 0, win.get_width(), win.get_height()};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        for(auto & m : meshes)
        {
            auto per_object = pool.allocate_descriptor_set(per_object_layout);
            per_object.write_uniform_buffer(0, 0, sizeof(m.model_matrix), &m.model_matrix);
            per_object.write_combined_image_sampler(1, 0, sampler, albedo_tex);
            per_object.write_combined_image_sampler(2, 0, sampler, normal_tex);
            per_object.write_combined_image_sampler(3, 0, sampler, metallic_tex);
            per_object.write_combined_image_sampler(4, 0, sampler, env_tex);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &per_object, 0, nullptr);

            vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertices.buffer, &m.vertices.offset);
            vkCmdBindIndexBuffer(cmd, m.indices.buffer, m.indices.offset, VkIndexType::VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, m.index_count, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        check(vkEndCommandBuffer(cmd));

        win.end(index, {cmd}, pool.get_fence());
        
        glfwPollEvents();
    }

    vkDeviceWaitIdle(ctx.device);
    vkDestroySampler(ctx.device, sampler, nullptr);
    vkDestroyPipeline(ctx.device, pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_object_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_view_layout, nullptr);
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