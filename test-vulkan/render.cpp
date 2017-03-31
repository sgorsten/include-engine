#include "render.h"
#include <stdexcept>
#include <iostream>

const char * to_string(VkResult result)
{
    switch(result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_OUT_OF_POOL_MEMORY_KHR: return "VK_ERROR_OUT_OF_POOL_MEMORY_KHR";
    default: fail_fast();
    }
}

struct vulkan_error : public std::error_category
{
    const char * name() const noexcept override { return "VkResult"; }
    std::string message(int _Errval) const override { return to_string(static_cast<VkResult>(_Errval)); }
    static const std::error_category & instance() { static vulkan_error inst; return inst; }
};

void check(VkResult result)
{
    if(result != VK_SUCCESS)
    {
        throw std::system_error(std::error_code(result, vulkan_error::instance()), "VkResult");
    }
}

static VkBool32 VKAPI_PTR debug_callback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT object_type, uint64_t object, size_t location, int32_t message_code, const char * layer_prefix, const char * message, void * user_data)
{
    std::cerr << "validation layer: " << message << std::endl;
    return VK_FALSE;
}

bool has_extension(const std::vector<VkExtensionProperties> & extensions, std::string_view name)
{
    return std::find_if(begin(extensions), end(extensions), [name](const VkExtensionProperties & p) { return p.extensionName == name; }) != end(extensions);
}

physical_device_selection select_physical_device(VkInstance instance, const std::vector<const char *> & required_extensions)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    GLFWwindow * example_window = glfwCreateWindow(256, 256, "", nullptr, nullptr);
    VkSurfaceKHR example_surface = 0;
    check(glfwCreateWindowSurface(instance, example_window, nullptr, &example_surface));

    uint32_t device_count = 0;
    check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    check(vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data()));
    for(auto & d : physical_devices)
    {
        // Skip physical devices which do not support our desired extensions
        uint32_t extension_count = 0;
        check(vkEnumerateDeviceExtensionProperties(d, 0, &extension_count, nullptr));
        std::vector<VkExtensionProperties> extensions(extension_count);
        check(vkEnumerateDeviceExtensionProperties(d, 0, &extension_count, extensions.data()));
        for(auto req : required_extensions) if(!has_extension(extensions, req)) continue;

        // Skip physical devices who do not support at least one format and present mode for our example surface
        VkSurfaceCapabilitiesKHR surface_caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d, example_surface, &surface_caps));
        uint32_t surface_format_count = 0, present_mode_count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(d, example_surface, &surface_format_count, nullptr));
        std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(d, example_surface, &surface_format_count, surface_formats.data()));
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(d, example_surface, &present_mode_count, nullptr));
        std::vector<VkPresentModeKHR> surface_present_modes(present_mode_count);
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(d, example_surface, &present_mode_count, surface_present_modes.data()));
        if(surface_formats.empty() || surface_present_modes.empty()) continue;
        
        // Select a format
        VkSurfaceFormatKHR surface_format = surface_formats[0];
        for(auto f : surface_formats) if(f.format==VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) surface_format = f;
        if(surface_format.format == VK_FORMAT_UNDEFINED) surface_format = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

        // Select a presentation mode
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for(auto mode : surface_present_modes) if(mode == VK_PRESENT_MODE_MAILBOX_KHR) present_mode = mode;

        // Look for a queue family that supports both graphics and presentation to our example surface
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_family_props(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &queue_family_count, queue_family_props.data());
        for(uint32_t i=0; i<queue_family_props.size(); ++i)
        {
            VkBool32 present = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(d, i, example_surface, &present));
            if((queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) 
            {
                vkDestroySurfaceKHR(instance, example_surface, nullptr);
                glfwDestroyWindow(example_window);
                return {d, i, surface_format, present_mode, std::min(surface_caps.minImageCount+1, surface_caps.maxImageCount), surface_caps.currentTransform};
            }
        }
    }
    throw std::runtime_error("no suitable Vulkan device present");
}

context::context()
{
    if(glfwInit() == GLFW_FALSE) throw std::runtime_error("glfwInit() failed");
    uint32_t extension_count = 0;
    auto ext = glfwGetRequiredInstanceExtensions(&extension_count);
    std::vector<const char *> extensions{ext, ext+extension_count};

    const VkApplicationInfo app_info {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "simple-scene", VK_MAKE_VERSION(1,0,0), "No Engine", VK_MAKE_VERSION(0,0,0), VK_API_VERSION_1_0};
    const char * layers[] {"VK_LAYER_LUNARG_standard_validation"};
    extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    const VkInstanceCreateInfo instance_info {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, {}, &app_info, countof(layers), layers, countof(extensions), extensions.data()};
    check(vkCreateInstance(&instance_info, nullptr, &instance));
    auto vkCreateDebugReportCallbackEXT = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
    vkDestroyDebugReportCallbackEXT = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));

    const VkDebugReportCallbackCreateInfoEXT callback_info {VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT, nullptr, VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT, debug_callback, nullptr};
    check(vkCreateDebugReportCallbackEXT(instance, &callback_info, nullptr, &callback));

    std::vector<const char *> device_extensions {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    selection = select_physical_device(instance, device_extensions);
    const float queue_priorities[] {1.0f};
    const VkDeviceQueueCreateInfo queue_infos[] {{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, {}, selection.queue_family, countof(queue_priorities), queue_priorities}};
    const VkDeviceCreateInfo device_info {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, {}, countof(queue_infos), queue_infos, countof(layers), layers, countof(device_extensions), device_extensions.data()};
    check(vkCreateDevice(selection.physical_device, &device_info, nullptr, &device));
    vkGetDeviceQueue(device, selection.queue_family, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties(selection.physical_device, &mem_props);
}

context::~context()
{
    vkDestroyDevice(device, nullptr);
    vkDestroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroyInstance(instance, nullptr);
}

uint32_t context::select_memory_type(const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props) const
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

VkDeviceMemory context::allocate(const VkMemoryRequirements & reqs, VkMemoryPropertyFlags props)
{
    VkMemoryAllocateInfo alloc_info {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = reqs.size;
    alloc_info.memoryTypeIndex = select_memory_type(reqs, props);

    VkDeviceMemory memory;
    check(vkAllocateMemory(device, &alloc_info, nullptr, &memory));
    return memory;
}

VkDescriptorSetLayout context::create_descriptor_set_layout(array_view<VkDescriptorSetLayoutBinding> bindings)
{
    VkDescriptorSetLayoutCreateInfo create_info {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    create_info.bindingCount = bindings.size;
    create_info.pBindings = bindings.data;

    VkDescriptorSetLayout descriptor_set_layout;
    check(vkCreateDescriptorSetLayout(device, &create_info, nullptr, &descriptor_set_layout));
    return descriptor_set_layout;
}

VkPipelineLayout context::create_pipeline_layout(array_view<VkDescriptorSetLayout> descriptor_sets)
{
    VkPipelineLayoutCreateInfo create_info {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    create_info.setLayoutCount = descriptor_sets.size;
    create_info.pSetLayouts = descriptor_sets.data;

    VkPipelineLayout pipeline_layout;
    check(vkCreatePipelineLayout(device, &create_info, nullptr, &pipeline_layout));
    return pipeline_layout;
}

window::window(context & ctx, uint32_t width, uint32_t height) : ctx{ctx}, width{width}, height{height}
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfw_window = glfwCreateWindow(width, height, "Vulkan Window", nullptr, nullptr);

    check(glfwCreateWindowSurface(ctx.instance, glfw_window, nullptr, &surface));

    VkBool32 present = VK_FALSE;
    check(vkGetPhysicalDeviceSurfaceSupportKHR(ctx.selection.physical_device, ctx.selection.queue_family, surface, &present));
    if(!present) throw std::runtime_error("vkGetPhysicalDeviceSurfaceSupportKHR(...) inconsistent");

    // Determine swap extent
    VkExtent2D swap_extent {width, height};
    VkSurfaceCapabilitiesKHR surface_caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.selection.physical_device, surface, &surface_caps));
    swap_extent.width = std::min(std::max(swap_extent.width, surface_caps.minImageExtent.width), surface_caps.maxImageExtent.width);
    swap_extent.height = std::min(std::max(swap_extent.height, surface_caps.minImageExtent.height), surface_caps.maxImageExtent.height);

    VkSwapchainCreateInfoKHR swapchain_info {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = ctx.selection.swap_image_count;
    swapchain_info.imageFormat = ctx.selection.surface_format.format;
    swapchain_info.imageColorSpace = ctx.selection.surface_format.colorSpace;
    swapchain_info.imageExtent = swap_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = ctx.selection.surface_transform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = ctx.selection.present_mode;
    swapchain_info.clipped = VK_TRUE;

    check(vkCreateSwapchainKHR(ctx.device, &swapchain_info, nullptr, &swapchain));    

    uint32_t swapchain_image_count;    
    check(vkGetSwapchainImagesKHR(ctx.device, swapchain, &swapchain_image_count, nullptr));
    std::vector<VkImage> swapchain_images(swapchain_image_count);
    check(vkGetSwapchainImagesKHR(ctx.device, swapchain, &swapchain_image_count, swapchain_images.data()));

    swapchain_image_views.resize(swapchain_image_count);
    for(uint32_t i=0; i<swapchain_image_count; ++i)
    {
        VkImageViewCreateInfo view_info {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = ctx.selection.surface_format.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        check(vkCreateImageView(ctx.device, &view_info, nullptr, &swapchain_image_views[i]));
    }

    VkSemaphoreCreateInfo semaphore_info {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(ctx.device, &semaphore_info, nullptr, &image_available));
    check(vkCreateSemaphore(ctx.device, &semaphore_info, nullptr, &render_finished));
}

window::~window()
{
    vkDestroySemaphore(ctx.device, render_finished, nullptr);
    vkDestroySemaphore(ctx.device, image_available, nullptr);
    for(auto image_view : swapchain_image_views) vkDestroyImageView(ctx.device, image_view, nullptr);
    vkDestroySwapchainKHR(ctx.device, swapchain, nullptr);
    vkDestroySurfaceKHR(ctx.instance, surface, nullptr);
    glfwDestroyWindow(glfw_window);
}

uint32_t window::begin()
{   
    uint32_t index;
    check(vkAcquireNextImageKHR(ctx.device, swapchain, std::numeric_limits<uint64_t>::max(), image_available, VK_NULL_HANDLE, &index));
    return index;
}

void window::end(std::initializer_list<VkCommandBuffer> commands, uint32_t index)
{
    VkPipelineStageFlags wait_stages[] {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit_info {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = commands.size();
    submit_info.pCommandBuffers = commands.begin();
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished;
    check(vkQueueSubmit(ctx.queue, 1, &submit_info, VK_NULL_HANDLE));

    VkPresentInfoKHR present_info {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &index;
    check(vkQueuePresentKHR(ctx.queue, &present_info));
}

////////////////////
// dynamic_buffer //
////////////////////

dynamic_buffer::dynamic_buffer(context & ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties) : ctx{ctx}
{
    VkBufferCreateInfo buffer_info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check(vkCreateBuffer(ctx.device, &buffer_info, nullptr, &buffer));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx.device, buffer, &mem_reqs);
    device_memory = ctx.allocate(mem_reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(ctx.device, buffer, device_memory, 0);

    void * mapped;
    check(vkMapMemory(ctx.device, device_memory, 0, size, 0, &mapped));
    mapped_memory = reinterpret_cast<char *>(mapped);
}

dynamic_buffer::~dynamic_buffer()
{
    vkDestroyBuffer(ctx.device, buffer, nullptr);
    vkUnmapMemory(ctx.device, device_memory);        
    vkFreeMemory(ctx.device, device_memory, nullptr);
}

void dynamic_buffer::reset() 
{ 
    offset = 0; 
}

VkDescriptorBufferInfo dynamic_buffer::write(size_t size, const void * data)
{
    VkDescriptorBufferInfo info {buffer, offset, size};
    memcpy(mapped_memory + offset, data, size);
    offset = (offset + size + 1023)/1024*1024; // TODO: Determine actual alignment
    return info;
}

////////////////////
// descriptor_set //
////////////////////

descriptor_set::descriptor_set(context & ctx, dynamic_buffer & uniform_buffer, VkDescriptorSet set) : ctx{ctx}, uniform_buffer{uniform_buffer}, set{set} 
{

}

void descriptor_set::write_uniform_buffer(uint32_t binding, uint32_t array_element, size_t size, const void * data)
{
    const auto info = uniform_buffer.write(size, data);
    VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, array_element, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &info, nullptr};
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
}

///////////////////////////
// command_buffer_helper //
///////////////////////////

command_buffer_helper::command_buffer_helper(context & ctx, array_view<VkDescriptorPoolSize> pool_sizes, uint32_t max_sets) : ctx{ctx}, uniform_buffer{ctx, 1024*1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT}
{
    VkDescriptorPoolCreateInfo desc_pool_info {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    desc_pool_info.poolSizeCount = pool_sizes.size;
    desc_pool_info.pPoolSizes = pool_sizes.data;
    desc_pool_info.maxSets = max_sets;
    check(vkCreateDescriptorPool(ctx.device, &desc_pool_info, nullptr, &desc_pool));   
}

command_buffer_helper::~command_buffer_helper()
{
    vkDestroyDescriptorPool(ctx.device, desc_pool, nullptr);
}

descriptor_set command_buffer_helper::allocate_descriptor_set(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo alloc_info {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet descriptor_set;
    check(vkAllocateDescriptorSets(ctx.device, &alloc_info, &descriptor_set));
    return {ctx, uniform_buffer, descriptor_set};
}

void command_buffer_helper::reset()
{
    check(vkResetDescriptorPool(ctx.device, desc_pool, 0));
    uniform_buffer.reset();
}

/////////////////
// fail_fast() //
/////////////////

#define NOMINMAX
#include <Windows.h>

void fail_fast()
{
    if(IsDebuggerPresent()) DebugBreak();
    std::cerr << "fail_fast() called." << std::endl;
    std::exit(EXIT_FAILURE);
}
