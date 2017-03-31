#include "render.h"
#include <array>
#include <string>
#include <vector>
#include <optional>
#include <iostream>

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

struct vertex { float position[2], color[3]; };

template<class Vertex, int N> VkVertexInputAttributeDescription make_attribute(uint32_t location, uint32_t binding, float (Vertex::*attribute)[N])
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

VkPipeline make_pipeline(VkDevice device, VkPipelineLayout layout, VkShaderModule vert_shader, VkShaderModule frag_shader, VkRenderPass render_pass, uint32_t width, uint32_t height)
{
    const VkPipelineShaderStageCreateInfo shader_stages[]
    {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_shader, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader, "main"}
    };

    const VkVertexInputBindingDescription bindings[] {{0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    const VkVertexInputAttributeDescription attributes[] {make_attribute(0, 0, &vertex::position), make_attribute(1, 0, &vertex::color)};
    VkPipelineVertexInputStateCreateInfo vertexInputInfo {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = countof(bindings);
    vertexInputInfo.pVertexBindingDescriptions = bindings;
    vertexInputInfo.vertexAttributeDescriptionCount = countof(attributes);
    vertexInputInfo.pVertexAttributeDescriptions = attributes;

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
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

    VkGraphicsPipelineCreateInfo pipelineInfo {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = countof(shader_stages);
    pipelineInfo.pStages = shader_stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr; // Optional
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = render_pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    return pipeline;
}

int main() try
{
    context ctx;

    const vertex vertices[]
    {
        {{ 0.0f, -0.5f}, {1,0,0}},
        {{+0.5f, +0.5f}, {0,1,0}},
        {{-0.5f, +0.5f}, {0,0,1}},
    };

    VkBufferCreateInfo buffer_info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = sizeof(vertices);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer vertex_buffer;
    check(vkCreateBuffer(ctx.device, &buffer_info, nullptr, &vertex_buffer));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx.device, vertex_buffer, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(ctx.selection.physical_device, &mem_props);
    auto select_memory_type = [mem_props](const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props)
    {
        for(uint32_t i=0; i<mem_props.memoryTypeCount; ++i)
        {
            if(reqs.memoryTypeBits & (1 << i) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
            {
                return i;
            }
        }
        throw std::runtime_error("no suitable memory type");
    };

    VkMemoryAllocateInfo alloc_info {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = select_memory_type(mem_reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory memory;
    check(vkAllocateMemory(ctx.device, &alloc_info, nullptr, &memory));
    vkBindBufferMemory(ctx.device, vertex_buffer, memory, 0);

    void * mapped_memory;
    check(vkMapMemory(ctx.device, memory, 0, sizeof(vertices), 0, &mapped_memory));
    memcpy(mapped_memory, vertices, sizeof(vertices));
    vkUnmapMemory(ctx.device, memory);

    window win {ctx, 640, 480};

    VkAttachmentDescription color_attachment_desc {};
    color_attachment_desc.format = ctx.selection.surface_format.format;
    color_attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment_desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass_desc {};
    subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_desc.colorAttachmentCount = 1;
    subpass_desc.pColorAttachments = &color_attachment_ref;
    
    VkRenderPassCreateInfo render_pass_info {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment_desc;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass_desc;

    VkRenderPass render_pass;
    check(vkCreateRenderPass(ctx.device, &render_pass_info, nullptr, &render_pass));

    std::vector<VkFramebuffer> swapchain_framebuffers(win.get_swapchain_image_views().size());
    for(uint32_t i=0; i<swapchain_framebuffers.size(); ++i)
    {
        VkImageView attachments[] {win.get_swapchain_image_views()[i]};

        VkFramebufferCreateInfo framebuffer_info {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = win.get_width();
        framebuffer_info.height = win.get_height();
        framebuffer_info.layers = 1;

        check(vkCreateFramebuffer(ctx.device, &framebuffer_info, nullptr, &swapchain_framebuffers[i]));
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 0; // Optional
    pipeline_layout_info.pSetLayouts = nullptr; // Optional
    pipeline_layout_info.pushConstantRangeCount = 0; // Optional
    pipeline_layout_info.pPushConstantRanges = 0; // Optional

    VkPipelineLayout pipeline_layout;
    check(vkCreatePipelineLayout(ctx.device, &pipeline_layout_info, nullptr, &pipeline_layout));
    
    VkShaderModule vert_shader = load_spirv_module(ctx.device, "assets/shader.vert.spv");
    VkShaderModule frag_shader = load_spirv_module(ctx.device, "assets/shader.frag.spv");

    VkPipeline pipeline = make_pipeline(ctx.device, pipeline_layout, vert_shader, frag_shader, render_pass, win.get_width(), win.get_height());

    VkCommandPoolCreateInfo command_pool_info {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = ctx.selection.queue_family;
    command_pool_info.flags = 0; // Optional

    VkCommandPool command_pool;
    check(vkCreateCommandPool(ctx.device, &command_pool_info, nullptr, &command_pool));

    while(!win.should_close())
    {
        uint32_t index = win.begin();

        VkCommandBufferAllocateInfo alloc_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer cmd;
        check(vkAllocateCommandBuffers(ctx.device, &alloc_info, &cmd));

        VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkRenderPassBeginInfo pass_begin_info {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass_begin_info.renderPass = render_pass;
        pass_begin_info.framebuffer = swapchain_framebuffers[index];
        pass_begin_info.renderArea.offset = {0, 0};
        pass_begin_info.renderArea.extent = {win.get_width(), win.get_height()};
        VkClearValue clearColor {0, 0, 0, 1};
        pass_begin_info.clearValueCount = 1;
        pass_begin_info.pClearValues = &clearColor;
        vkCmdBeginRenderPass(cmd, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const VkViewport viewport {0, 0, static_cast<float>(win.get_width()), static_cast<float>(win.get_height()), 0.0f, 1.0f};
        const VkRect2D scissor {0, 0, win.get_width(), win.get_height()};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
        check(vkEndCommandBuffer(cmd));

        win.end({cmd}, index);

        // TODO: Don't do this
        vkDeviceWaitIdle(ctx.device);
        vkFreeCommandBuffers(ctx.device, command_pool, 1, &cmd);

        glfwPollEvents();
    }

    vkDestroyBuffer(ctx.device, vertex_buffer, nullptr);
    vkFreeMemory(ctx.device, memory, nullptr);
    vkDestroyPipeline(ctx.device, pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
    vkDestroyShaderModule(ctx.device, vert_shader, nullptr);
    vkDestroyShaderModule(ctx.device, frag_shader, nullptr);
    vkDestroyCommandPool(ctx.device, command_pool, nullptr);
    for(auto framebuffer : swapchain_framebuffers) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    vkDestroyRenderPass(ctx.device, render_pass, nullptr);    
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}