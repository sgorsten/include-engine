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
    std::vector<VkImageView> swapchain_image_views;
    VkSemaphore image_available {}, render_finished {};
    uint2 dims;
public:
    window(context & ctx, uint2 dims, const char * title);
    ~window();

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

class depth_buffer
{
    context & ctx;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    depth_buffer(context & ctx, uint2 dims);
    ~depth_buffer();

    operator VkImageView () const { return image_view; }
};

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
};

class descriptor_set
{
    context & ctx;
    dynamic_buffer & uniform_buffer;
    VkDescriptorSet set;
public:
    descriptor_set(context & ctx, dynamic_buffer & uniform_buffer, VkDescriptorSet set);

    operator VkDescriptorSet () { return set; }

    void write_uniform_buffer(uint32_t binding, uint32_t array_element, size_t size, const void * data);
    void write_combined_image_sampler(uint32_t binding, uint32_t array_element, VkSampler sampler, VkImageView image_view, VkImageLayout image_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    template<class T> void write_uniform_buffer(uint32_t binding, uint32_t array_element, const T & data) { write_uniform_buffer(binding, array_element, sizeof(data), &data); }
};

class command_buffer
{
    VkCommandBuffer cmd;
public:
    command_buffer(VkCommandBuffer cmd) : cmd{cmd} {}

    operator VkCommandBuffer () { return cmd; }

    void begin(VkCommandBufferUsageFlags flags);

    void bind_descriptor_set(VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t set_index, VkDescriptorSet set, array_view<uint32_t> dynamic_offsets);
    void bind_pipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline);
    void bind_vertex_buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset);
    void bind_index_buffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType index_type);

    void begin_render_pass(VkRenderPass render_pass, VkFramebuffer framebuffer, uint2 dims, array_view<VkClearValue> clear_values);
    void draw_indexed(uint32_t index_count, uint32_t instance_count=1, uint32_t first_index=0, uint32_t vertex_offset=0, uint32_t first_instance=0);
    void end_render_pass();

    void end();
};

// Manages the allocation of short-lived resources which can be recycled in a single call, protected by a fence
class transient_resource_pool
{
    context & ctx;
    dynamic_buffer uniform_buffer;
    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;
    VkDescriptorPool descriptor_pool;
    VkFence fence;
public:
    transient_resource_pool(context & ctx, array_view<VkDescriptorPoolSize> descriptor_pool_sizes, uint32_t max_descriptor_sets);
    ~transient_resource_pool();

    void reset();
    VkCommandBuffer allocate_command_buffer();
    descriptor_set allocate_descriptor_set(VkDescriptorSetLayout layout);
    VkFence get_fence() { return fence; }
};

// Other utility functions
void transition_layout(VkCommandBuffer command_buffer, VkImage image, uint32_t mip_level, uint32_t array_layer, VkImageLayout old_layout, VkImageLayout new_layout);

VkPipeline make_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout, 
    array_view<VkVertexInputBindingDescription> vertex_bindings, array_view<VkVertexInputAttributeDescription> vertex_attributes, 
    VkShaderModule vert_shader, VkShaderModule frag_shader, bool depth_write, bool depth_test);

#endif
