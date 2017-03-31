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
    uint32_t width {}, height {};
public:
    window(context & ctx, uint32_t width, uint32_t height);
    ~window();

    const std::vector<VkImageView> & get_swapchain_image_views() const { return swapchain_image_views; }
    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }
    bool should_close() const { return !!glfwWindowShouldClose(glfw_window); }

    float2 get_cursor_pos() const { double2 c; glfwGetCursorPos(glfw_window, &c.x, &c.y); return float2{c}; }
    bool get_mouse_button(int button) const { return glfwGetMouseButton(glfw_window, button) == GLFW_PRESS; }
    bool get_key(int key) const { return glfwGetKey(glfw_window, key) == GLFW_PRESS; }

    uint32_t begin();
    void end(uint32_t index, std::initializer_list<VkCommandBuffer> commands, VkFence fence);
};

class depth_buffer
{
    context & ctx;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    depth_buffer(context & ctx, uint32_t width, uint32_t height);
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

    const VkDescriptorSet * operator & () const { return &set; }

    void write_uniform_buffer(uint32_t binding, uint32_t array_element, size_t size, const void * data);
    void write_combined_image_sampler(uint32_t binding, uint32_t array_element, VkSampler sampler, VkImageView image_view, VkImageLayout image_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

#endif
