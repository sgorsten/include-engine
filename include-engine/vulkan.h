#ifndef RENDER_H
#define RENDER_H

#include "data-types.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

[[noreturn]] void fail_fast();
const char * to_string(VkResult result);
void check(VkResult result);

struct physical_device_selection
{
    VkPhysicalDevice physical_device;
    uint32_t queue_family;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    uint32_t swap_image_count;
    VkSurfaceTransformFlagBitsKHR surface_transform;
};

struct context
{
    VkInstance instance {};
    PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT {};
    VkDebugReportCallbackEXT callback {};
    physical_device_selection selection {};
    VkDevice device {};
    VkQueue queue {};
    VkPhysicalDeviceMemoryProperties mem_props {};

    VkBuffer staging_buffer {};
    VkDeviceMemory staging_memory {};
    void * mapped_staging_memory {};
    VkCommandPool staging_pool {};

    context();
    ~context();

    uint32_t select_memory_type(const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props) const;
    VkDeviceMemory allocate(const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props);

    VkDescriptorSetLayout create_descriptor_set_layout(array_view<VkDescriptorSetLayoutBinding> bindings);
    VkPipelineLayout create_pipeline_layout(array_view<VkDescriptorSetLayout> descriptor_sets);
    VkShaderModule create_shader_module(array_view<uint32_t> spirv_words);
    VkFramebuffer create_framebuffer(VkRenderPass render_pass, array_view<VkImageView> attachments, uint2 dims);
    VkRenderPass create_render_pass(array_view<VkAttachmentDescription> color_attachments, std::optional<VkAttachmentDescription> depth_attachment);

    VkCommandBuffer begin_transient();
    void end_transient(VkCommandBuffer commandBuffer);
};

class window
{
    context & ctx;

    GLFWwindow * glfw_window {};
    VkSurfaceKHR surface {};
    VkSwapchainKHR swapchain {};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkSemaphore image_available {}, render_finished {};
    uint2 dims;
public:
    window(context & ctx, uint2 dims, const char * title);
    ~window();

    const std::vector<VkImage> & get_swapchain_images() const { return swapchain_images; }
    const std::vector<VkImageView> & get_swapchain_image_views() const { return swapchain_image_views; }
    uint2 get_dims() const { return dims; }
    float get_aspect() const { return (float)dims.x/dims.y; }
    bool should_close() const { return !!glfwWindowShouldClose(glfw_window); }

    float2 get_cursor_pos() const { double2 c; glfwGetCursorPos(glfw_window, &c.x, &c.y); return float2{c}; }
    bool get_mouse_button(int button) const { return glfwGetMouseButton(glfw_window, button) == GLFW_PRESS; }
    bool get_key(int key) const { return glfwGetKey(glfw_window, key) == GLFW_PRESS; }

    uint32_t begin();
    void end(uint32_t index, array_view<VkCommandBuffer> commands, VkFence fence);
};

class render_target
{
    context & ctx;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    render_target(context & ctx, uint2 dims, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    ~render_target();

    VkImage get_image() const { return image; }
    VkImageView get_image_view() const { return image_view; }
};

inline render_target make_depth_buffer(context & ctx, uint2 dims) { return {ctx, dims, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT}; }

class texture_2d
{
    context & ctx;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    texture_2d(context & ctx, uint32_t width, uint32_t height, VkFormat format, const void * initial_data);
    texture_2d(context & ctx, VkFormat format, const ::image & image) : texture_2d(ctx, image.get_width(), image.get_height(), format, image.get_pixels()) {}
    ~texture_2d();

    VkImage get_image() { return image; }
    operator VkImageView () const { return image_view; }
};

class texture_cube
{
    context & ctx;
    VkImage img;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    texture_cube(context & ctx, VkFormat format, const image & posx, const image & negx, const image & posy, const image & negy, const image & posz, const image & negz);
    ~texture_cube();

    VkImage get_image() { return img; }
    operator VkImageView () const { return image_view; }
};

class static_buffer
{
    context & ctx;
    VkBuffer buffer;
    VkDeviceMemory device_memory;
public:
    static_buffer(context & ctx, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties, VkDeviceSize size, const void * initial_data);
    ~static_buffer();

    operator VkBuffer () { return buffer; }
};

template<class T> struct reserved_range
{
    VkDescriptorBufferInfo info;
    T * mapped_memory;
    T & operator[] (size_t i) { return mapped_memory[i]; }
};

class dynamic_buffer
{
    context & ctx;
    VkBuffer buffer;
    VkDeviceMemory device_memory;
    char * mapped_memory;
    VkDeviceSize offset {};
public:
    dynamic_buffer(context & ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties);
    ~dynamic_buffer();

    void reset();
    VkDescriptorBufferInfo write(size_t size, const void * data);

    template<class T> reserved_range<T> reserve_range(size_t count)
    {
        size_t size = sizeof(T)*count;
        VkDescriptorBufferInfo info {buffer, offset, size};
        offset = (offset + size + 1023)/1024*1024; // TODO: Determine actual alignment
        return {info, reinterpret_cast<T *>(mapped_memory + info.offset)};
    }
};

// Manages the allocation of short-lived resources which can be recycled in a single call, protected by a fence
class transient_resource_pool
{
    context & ctx;
    dynamic_buffer uniform_buffer;
    dynamic_buffer vertex_buffer;
    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;
    VkDescriptorPool descriptor_pool;
    std::vector<VkDescriptorSet> descriptor_sets;
    VkFence fence;
public:
    transient_resource_pool(context & ctx, array_view<VkDescriptorPoolSize> descriptor_pool_sizes, uint32_t max_descriptor_sets);
    ~transient_resource_pool();

    void reset();
    VkCommandBuffer allocate_command_buffer();
    VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout layout);
    VkDescriptorBufferInfo write_data(size_t size, const void * data) { return uniform_buffer.write(size, data); }
    VkDescriptorBufferInfo write_vertex_data(size_t size, const void * data) { return vertex_buffer.write(size, data); }
    template<class T> reserved_range<T> reserve_instances(size_t count) { return vertex_buffer.reserve_range<T>(count); }

    VkFence get_fence() { return fence; }

    template<class T> VkDescriptorBufferInfo write_data(const T & data) { return write_data(sizeof(data), &data); }
};

// Other utility functions
void transition_layout(VkCommandBuffer command_buffer, VkImage image, uint32_t mip_level, uint32_t array_layer, VkImageLayout old_layout, VkImageLayout new_layout);

VkPipeline make_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout, VkPipelineVertexInputStateCreateInfo vertex_input_state, 
    array_view<VkPipelineShaderStageCreateInfo> stages, bool depth_write, bool depth_test, bool additive_blending);

// Convenience wrappers around Vulkan calls
void vkUpdateDescriptorSets(VkDevice device, array_view<VkWriteDescriptorSet> descriptorWrites, array_view<VkCopyDescriptorSet> descriptorCopies);
void vkWriteDescriptorCombinedImageSamplerInfo(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t array_element, VkDescriptorImageInfo info);
void vkWriteDescriptorBufferInfo(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info);

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, array_view<VkDescriptorSet> descriptorSets, array_view<uint32_t> dynamicOffsets);
void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, array_view<VkBuffer> buffers, array_view<VkDeviceSize> offsets);

#endif
