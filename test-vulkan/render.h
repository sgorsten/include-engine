#ifndef RENDER_H
#define RENDER_H

#include "linalg.h"
using namespace linalg::aliases;

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <array>
#include <vector>

[[noreturn]] void fail_fast();
const char * to_string(VkResult result);
void check(VkResult result);

// Abstract over determining the number of elements in a collection
template<class T, uint32_t N> uint32_t countof(const T (&)[N]) { return N; }
template<class T, uint32_t N> uint32_t countof(const std::array<T,N> &) { return N; }
template<class T> uint32_t countof(const std::vector<T> & v) { return static_cast<uint32_t>(v.size()); }

template<class T> struct array_view
{
    const T * data;
    size_t size;

    template<size_t N> array_view(const T (& array)[N]) : data{array}, size{N} {}
    template<size_t N> array_view(const std::array<T,N> & array) : data{array.data()}, size{N} {}
    array_view(std::initializer_list<T> ilist) : data{ilist.begin()}, size{ilist.size()} {}
    array_view(const std::vector<T> & vec) : data{vec.data()}, size{vec.size()} {}    
};

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
    VkPhysicalDeviceMemoryProperties mem_props;

    context();
    ~context();

    uint32_t select_memory_type(const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props) const;
    VkDeviceMemory allocate(const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props);

    VkDescriptorSetLayout create_descriptor_set_layout(array_view<VkDescriptorSetLayoutBinding> bindings);
    VkPipelineLayout create_pipeline_layout(array_view<VkDescriptorSetLayout> descriptor_sets);
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
    void end(std::initializer_list<VkCommandBuffer> commands, uint32_t index);
};

#endif
