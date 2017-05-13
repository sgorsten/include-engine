#include "renderer.h"
#include <stdexcept>
#include <iostream>

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

/////////////
// context //
/////////////

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

    // Set up staging buffer
    VkBufferCreateInfo buffer_info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = 16*1024*1024;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &buffer_info, nullptr, &staging_buffer));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, staging_buffer, &mem_reqs);
    staging_memory = allocate(mem_reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(device, staging_buffer, staging_memory, 0);

    check(vkMapMemory(device, staging_memory, 0, buffer_info.size, 0, &mapped_staging_memory));
        
    VkCommandPoolCreateInfo command_pool_info {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = selection.queue_family; // TODO: Could use an explicit transfer queue
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    check(vkCreateCommandPool(device, &command_pool_info, nullptr, &staging_pool));
}

context::~context()
{
    vkDestroyCommandPool(device, staging_pool, nullptr);
    vkDestroyBuffer(device, staging_buffer, nullptr);
    vkUnmapMemory(device, staging_memory);
    vkFreeMemory(device, staging_memory, nullptr);

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

VkCommandBuffer context::begin_transient() 
{
    VkCommandBufferAllocateInfo alloc_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = staging_pool;
    alloc_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer;
    check(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command_buffer, &begin_info));

    return command_buffer;
}

void context::end_transient(VkCommandBuffer command_buffer) 
{
    check(vkEndCommandBuffer(command_buffer));
    VkSubmitInfo submitInfo {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffer;
    check(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    check(vkQueueWaitIdle(queue)); // TODO: Do something with fences instead
    vkFreeCommandBuffers(device, staging_pool, 1, &command_buffer);
}

////////////
// window //
////////////

window::window(std::shared_ptr<context> ctx, uint2 dims, const char * title) : ctx{ctx}, dims{dims}
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfw_window = glfwCreateWindow(dims.x, dims.y, title, nullptr, nullptr);

    check(glfwCreateWindowSurface(ctx->instance, glfw_window, nullptr, &surface));

    VkBool32 present = VK_FALSE;
    check(vkGetPhysicalDeviceSurfaceSupportKHR(ctx->selection.physical_device, ctx->selection.queue_family, surface, &present));
    if(!present) throw std::runtime_error("vkGetPhysicalDeviceSurfaceSupportKHR(...) inconsistent");

    // Determine swap extent
    VkExtent2D swap_extent {dims.x, dims.y};
    VkSurfaceCapabilitiesKHR surface_caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->selection.physical_device, surface, &surface_caps));
    swap_extent.width = std::min(std::max(swap_extent.width, surface_caps.minImageExtent.width), surface_caps.maxImageExtent.width);
    swap_extent.height = std::min(std::max(swap_extent.height, surface_caps.minImageExtent.height), surface_caps.maxImageExtent.height);

    VkSwapchainCreateInfoKHR swapchain_info {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = ctx->selection.swap_image_count;
    swapchain_info.imageFormat = ctx->selection.surface_format.format;
    swapchain_info.imageColorSpace = ctx->selection.surface_format.colorSpace;
    swapchain_info.imageExtent = swap_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = ctx->selection.surface_transform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = ctx->selection.present_mode;
    swapchain_info.clipped = VK_TRUE;

    check(vkCreateSwapchainKHR(ctx->device, &swapchain_info, nullptr, &swapchain));    

    uint32_t swapchain_image_count;    
    check(vkGetSwapchainImagesKHR(ctx->device, swapchain, &swapchain_image_count, nullptr));
    swapchain_images.resize(swapchain_image_count);
    check(vkGetSwapchainImagesKHR(ctx->device, swapchain, &swapchain_image_count, swapchain_images.data()));

    swapchain_image_views.resize(swapchain_image_count);
    for(uint32_t i=0; i<swapchain_image_count; ++i)
    {
        VkImageViewCreateInfo view_info {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = ctx->selection.surface_format.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        check(vkCreateImageView(ctx->device, &view_info, nullptr, &swapchain_image_views[i]));
    }

    VkSemaphoreCreateInfo semaphore_info {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(ctx->device, &semaphore_info, nullptr, &image_available));
    check(vkCreateSemaphore(ctx->device, &semaphore_info, nullptr, &render_finished));
}

window::~window()
{
    vkDestroySemaphore(ctx->device, render_finished, nullptr);
    vkDestroySemaphore(ctx->device, image_available, nullptr);
    for(auto image_view : swapchain_image_views) vkDestroyImageView(ctx->device, image_view, nullptr);
    vkDestroySwapchainKHR(ctx->device, swapchain, nullptr);
    vkDestroySurfaceKHR(ctx->instance, surface, nullptr);
    glfwDestroyWindow(glfw_window);
}

uint32_t window::begin()
{   
    uint32_t index;
    check(vkAcquireNextImageKHR(ctx->device, swapchain, std::numeric_limits<uint64_t>::max(), image_available, VK_NULL_HANDLE, &index));
    return index;
}

void window::end(uint32_t index, array_view<VkCommandBuffer> commands, VkFence fence)
{
    VkPipelineStageFlags wait_stages[] {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit_info {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = commands.size;
    submit_info.pCommandBuffers = commands.data;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished;
    check(vkQueueSubmit(ctx->queue, 1, &submit_info, fence));

    VkPresentInfoKHR present_info {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &index;
    check(vkQueuePresentKHR(ctx->queue, &present_info));
}

///////////////////
// render_target //
///////////////////

render_target::render_target(std::shared_ptr<context> ctx, uint2 dims, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) : ctx{ctx}
{
    VkImageCreateInfo image_info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {dims.x, dims.y, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(ctx->device, &image_info, nullptr, &image));

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, image, &mem_reqs);
    device_memory = ctx->allocate(mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(ctx->device, image, device_memory, 0);
    
    VkImageViewCreateInfo image_view_info {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    image_view_info.image = image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = format;
    image_view_info.subresourceRange.aspectMask = aspect;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;
    check(vkCreateImageView(ctx->device, &image_view_info, nullptr, &image_view));
}

render_target::~render_target()
{
    vkDestroyImageView(ctx->device, image_view, nullptr);
    vkDestroyImage(ctx->device, image, nullptr);
    vkFreeMemory(ctx->device, device_memory, nullptr);
}

////////////////
// texture_2d //
////////////////

void transition_layout(VkCommandBuffer command_buffer, VkImage image, uint32_t mip_level, uint32_t array_layer, VkImageLayout old_layout, VkImageLayout new_layout);

texture_2d::texture_2d(std::shared_ptr<context> ctx, uint32_t width, uint32_t height, VkFormat format, const void * initial_data) : ctx{ctx}
{
    VkImageCreateInfo image_info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1+static_cast<uint32_t>(std::ceil(std::log2(std::max(width,height))));
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(ctx->device, &image_info, nullptr, &image));

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, image, &mem_reqs);
    device_memory = ctx->allocate(mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(ctx->device, image, device_memory, 0);
    
    VkImageViewCreateInfo image_view_info {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    image_view_info.image = image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = format;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = image_info.mipLevels;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;
    check(vkCreateImageView(ctx->device, &image_view_info, nullptr, &image_view));

    memcpy(ctx->mapped_staging_memory, initial_data, width*height*4);

    auto cmd = ctx->begin_transient();
    transition_layout(cmd, image, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy_region {};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = image_info.extent;
    vkCmdCopyBufferToImage(cmd, ctx->staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    // Generate mip levels using blits
    for(uint32_t i=1; i<image_info.mipLevels; ++i)
    {
        VkImageBlit blit {};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i-1, 0, 1};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {(int)width, (int)height, 1};

        width = std::max(width/2,1U);
        height = std::max(height/2,1U);
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {(int)width, (int)height, 1};

        transition_layout(cmd, image, i-1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition_layout(cmd, image, i, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        transition_layout(cmd, image, i-1, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    transition_layout(cmd, image, image_info.mipLevels-1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ctx->end_transient(cmd);
}

texture_2d::~texture_2d()
{
    vkDestroyImageView(ctx->device, image_view, nullptr);
    vkDestroyImage(ctx->device, image, nullptr);
    vkFreeMemory(ctx->device, device_memory, nullptr);
}

//////////////////
// texture_cube //
//////////////////

texture_cube::texture_cube(std::shared_ptr<context> ctx, VkFormat format, const image & posx, const image & negx, const image & posy, const image & negy, const image & posz, const image & negz) : ctx{ctx}
{
    uint32_t side_length = posx.get_width();
    VkImageCreateInfo image_info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {side_length, side_length, 1};
    image_info.mipLevels = 1; //1+std::ceil(std::log2(side_length));
    image_info.arrayLayers = 6;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(ctx->device, &image_info, nullptr, &img));

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, img, &mem_reqs);
    device_memory = ctx->allocate(mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(ctx->device, img, device_memory, 0);
    
    VkImageViewCreateInfo image_view_info {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    image_view_info.image = img;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    image_view_info.format = format;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = image_info.mipLevels;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 6;
    check(vkCreateImageView(ctx->device, &image_view_info, nullptr, &image_view));    

    const image * ims[] {&posx,&negx,&posy,&negy,&posz,&negz};
    for(uint32_t j=0; j<6; ++j)
    {
        memcpy(ctx->mapped_staging_memory, ims[j]->get_pixels(), side_length*side_length*4);

        VkImageSubresourceLayers layers {};
        layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        layers.baseArrayLayer = j;
        layers.layerCount = 1;

        auto cmd = ctx->begin_transient();
        transition_layout(cmd, img, 0, j, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy_region {};
        copy_region.imageSubresource = layers;
        copy_region.imageExtent = image_info.extent;
        vkCmdCopyBufferToImage(cmd, ctx->staging_buffer, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        // Generate mip levels using blits
        for(uint32_t i=1; i<image_info.mipLevels; ++i)
        {
            VkImageBlit blit {};
            blit.srcSubresource = layers;
            blit.srcSubresource.mipLevel = i-1;
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(int)side_length, (int)side_length, 1};

            side_length = std::max(side_length/2,1U);
            blit.dstSubresource = layers;
            blit.dstSubresource.mipLevel = i;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(int)side_length, (int)side_length, 1};

            transition_layout(cmd, img, i-1, j, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            transition_layout(cmd, img, i, j, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            transition_layout(cmd, img, i-1, j, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        transition_layout(cmd, img, image_info.mipLevels-1, j, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        ctx->end_transient(cmd);
    }
}

texture_cube::~texture_cube()
{
    vkDestroyImageView(ctx->device, image_view, nullptr);
    vkDestroyImage(ctx->device, img, nullptr);
    vkFreeMemory(ctx->device, device_memory, nullptr);
}

///////////////////
// static_buffer //
///////////////////

static_buffer::static_buffer(std::shared_ptr<context> ctx, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties, VkDeviceSize size, const void * initial_data) : ctx{ctx}
{
    memcpy(ctx->mapped_staging_memory, initial_data, size);

    VkBufferCreateInfo buffer_info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(ctx->device, &buffer_info, nullptr, &buffer));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, buffer, &mem_reqs);
    device_memory = ctx->allocate(mem_reqs, memory_properties);
    vkBindBufferMemory(ctx->device, buffer, device_memory, 0);

    auto cmd = ctx->begin_transient();
    const VkBufferCopy copy {0, 0, size};
    vkCmdCopyBuffer(cmd, ctx->staging_buffer, buffer, 1, &copy);
    ctx->end_transient(cmd);
}

static_buffer::~static_buffer()
{
    vkDestroyBuffer(ctx->device, buffer, nullptr);
    vkFreeMemory(ctx->device, device_memory, nullptr);
}

////////////////////
// dynamic_buffer //
////////////////////

dynamic_buffer::dynamic_buffer(std::shared_ptr<context> ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties) : ctx{ctx}
{
    VkBufferCreateInfo buffer_info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(ctx->device, &buffer_info, nullptr, &buffer));

    vkGetBufferMemoryRequirements(ctx->device, buffer, &mem_reqs);
    device_memory = ctx->allocate(mem_reqs, memory_properties);
    vkBindBufferMemory(ctx->device, buffer, device_memory, 0);
    check(vkMapMemory(ctx->device, device_memory, 0, size, 0, reinterpret_cast<void**>(&mapped_memory)));
}

dynamic_buffer::~dynamic_buffer()
{
    vkDestroyBuffer(ctx->device, buffer, nullptr);
    vkUnmapMemory(ctx->device, device_memory);        
    vkFreeMemory(ctx->device, device_memory, nullptr);
}

void dynamic_buffer::reset() 
{ 
    offset = range = 0; 
}

void dynamic_buffer::begin() 
{ 
    offset += (range + mem_reqs.alignment - 1) / mem_reqs.alignment * mem_reqs.alignment;
    range = 0;
}

void dynamic_buffer::write(size_t size, const void * data)
{
    memcpy(mapped_memory + offset + range, data, size); 
    range += size;
}

VkDescriptorBufferInfo dynamic_buffer::end()
{
    return {buffer, offset, range};
}

VkDescriptorBufferInfo dynamic_buffer::upload(size_t size, const void * data)
{
    begin();
    write(size, data);
    return end();
}

/////////////////////////////
// transient_resource_pool //
/////////////////////////////

transient_resource_pool::transient_resource_pool(std::shared_ptr<context> ctx, array_view<VkDescriptorPoolSize> descriptor_pool_sizes, uint32_t max_descriptor_sets)
    : ctx{ctx}, uniform_buffer{ctx, 1024*1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT}, 
        vertex_buffer{ctx, 1024*1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT}
{
    VkCommandPoolCreateInfo command_pool_info {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = ctx->selection.queue_family;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    check(vkCreateCommandPool(ctx->device, &command_pool_info, nullptr, &command_pool));

    VkDescriptorPoolCreateInfo descriptor_pool_info {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_pool_info.poolSizeCount = descriptor_pool_sizes.size;
    descriptor_pool_info.pPoolSizes = descriptor_pool_sizes.data;
    descriptor_pool_info.maxSets = max_descriptor_sets;
    descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    check(vkCreateDescriptorPool(ctx->device, &descriptor_pool_info, nullptr, &descriptor_pool));

    VkFenceCreateInfo fence_info {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(ctx->device, &fence_info, nullptr, &fence));
}

transient_resource_pool::~transient_resource_pool()
{
    vkDestroyFence(ctx->device, fence, nullptr);
    vkDestroyDescriptorPool(ctx->device, descriptor_pool, nullptr);
    vkDestroyCommandPool(ctx->device, command_pool, nullptr);
}

void transient_resource_pool::reset()
{
    check(vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX));
    check(vkResetFences(ctx->device, 1, &fence));
    if(!command_buffers.empty())
    {
        vkFreeCommandBuffers(ctx->device, command_pool, command_buffers.size(), command_buffers.data());
        command_buffers.clear();
    }
    check(vkResetCommandPool(ctx->device, command_pool, 0));
    if(!descriptor_sets.empty())
    {
        vkFreeDescriptorSets(ctx->device, descriptor_pool, descriptor_sets.size(), descriptor_sets.data());
        descriptor_sets.clear();
    }
    check(vkResetDescriptorPool(ctx->device, descriptor_pool, 0));
    uniform_buffer.reset();
    vertex_buffer.reset();
}

VkCommandBuffer transient_resource_pool::allocate_command_buffer()
{
    VkCommandBufferAllocateInfo alloc_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    check(vkAllocateCommandBuffers(ctx->device, &alloc_info, &command_buffer));
    command_buffers.push_back(command_buffer);
    return command_buffer;
}

VkDescriptorSet transient_resource_pool::allocate_descriptor_set(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo alloc_info {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet descriptor_set;
    check(vkAllocateDescriptorSets(ctx->device, &alloc_info, &descriptor_set));
    descriptor_sets.push_back(descriptor_set);
    return descriptor_set;
}

////////////////////////////
// transition_layout(...) //
////////////////////////////

void transition_layout(VkCommandBuffer command_buffer, VkImage image, uint32_t mip_level, uint32_t array_layer, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mip_level;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = array_layer;
    barrier.subresourceRange.layerCount = 1;
    switch(old_layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED: break; // No need to wait for anything, contents can be discarded
    case VK_IMAGE_LAYOUT_PREINITIALIZED: barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; break; // Wait for host writes to complete before changing layout    
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break; // Wait for transfer reads to complete before changing layout
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break; // Wait for transfer writes to complete before changing layout
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break; // Wait for color attachment writes to complete before changing layout
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break; // Wait for shader reads to complete before changing layout
    default: throw std::logic_error("unsupported layout transition");
    }
    switch(new_layout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break; // Transfer reads should wait for layout change to complete
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break; // Transfer writes should wait for layout change to complete
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break; // Writes to color attachments should wait for layout change to complete
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; break; // Shader reads should wait for layout change to complete
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT; break; // Memory reads should wait for layout change to complete
    default: throw std::logic_error("unsupported layout transition");
    }
    vkCmdPipelineBarrier(command_buffer, src_stage_mask, dst_stage_mask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}


VkPipeline make_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout, VkPipelineVertexInputStateCreateInfo vertex_input_state, array_view<VkPipelineShaderStageCreateInfo> stages, bool depth_write, bool depth_test, bool additive_blending)
{
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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
    if(additive_blending)
    {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    }

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
    depth_stencil_state.depthWriteEnable = depth_write;
    depth_stencil_state.depthTestEnable = depth_test;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS;

    VkGraphicsPipelineCreateInfo pipelineInfo {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = stages.size;
    pipelineInfo.pStages = stages.data;
    pipelineInfo.pVertexInputState = &vertex_input_state;
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

//////////////////////////
// Convenience wrappers //
//////////////////////////

void vkUpdateDescriptorSets(VkDevice device, array_view<VkWriteDescriptorSet> descriptorWrites, array_view<VkCopyDescriptorSet> descriptorCopies)
{
    vkUpdateDescriptorSets(device, descriptorWrites.size, descriptorWrites.data, descriptorCopies.size, descriptorCopies.data);
}

void vkWriteDescriptorBufferInfo(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info)
{
    vkUpdateDescriptorSets(device, {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, array_element, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &info, nullptr}}, {});
}

void vkWriteDescriptorCombinedImageSamplerInfo(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t array_element, VkDescriptorImageInfo info)
{
    vkUpdateDescriptorSets(device, {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, array_element, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &info, nullptr, nullptr}}, {});
}

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, array_view<VkDescriptorSet> descriptorSets, array_view<uint32_t> dynamicOffsets)
{
    vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSets.size, descriptorSets.data, dynamicOffsets.size, dynamicOffsets.data);
}

void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, array_view<VkBuffer> buffers, array_view<VkDeviceSize> offsets)
{
    if(buffers.size != offsets.size) fail_fast();
    vkCmdBindVertexBuffers(commandBuffer, firstBinding, buffers.size, buffers.data, offsets.data);
}

void vkCmdSetViewport(VkCommandBuffer commandBuffer, VkRect2D viewport)
{
    const VkViewport viewports[] {{static_cast<float>(viewport.offset.x), static_cast<float>(viewport.offset.y), 
        static_cast<float>(viewport.extent.width), static_cast<float>(viewport.extent.height), 0.0f, 1.0f}};
    vkCmdSetViewport(commandBuffer, 0, countof(viewports), viewports);
}

void vkCmdSetScissor(VkCommandBuffer commandBuffer, VkRect2D scissor)
{
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void vkCmdBeginRenderPass(VkCommandBuffer cmd, VkRenderPass renderPass, VkFramebuffer framebuffer, VkRect2D renderArea, array_view<VkClearValue> clearValues)
{
    VkRenderPassBeginInfo pass_begin_info {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass_begin_info.renderPass = renderPass;
    pass_begin_info.framebuffer = framebuffer;
    pass_begin_info.renderArea = renderArea;
    pass_begin_info.clearValueCount = clearValues.size;
    pass_begin_info.pClearValues = clearValues.data;
    vkCmdBeginRenderPass(cmd, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, renderArea);
    vkCmdSetScissor(cmd, renderArea);
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


vertex_format::vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes) :
    bindings{bindings.begin(), bindings.end()}, attributes{attributes.begin(), attributes.end()}
{

}

VkPipelineVertexInputStateCreateInfo vertex_format::get_vertex_input_state() const
{
    return {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(bindings.size()), bindings.data(), static_cast<uint32_t>(attributes.size()), attributes.data()};
}

////////////
// shader //
////////////

shader::shader(std::shared_ptr<context> ctx, VkShaderStageFlagBits stage, array_view<uint32_t> words)
    : ctx{ctx}, stage{stage}
{
    VkShaderModuleCreateInfo create_info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create_info.codeSize = words.size * sizeof(uint32_t);
    create_info.pCode = words.data;
    check(vkCreateShaderModule(ctx->device, &create_info, nullptr, &module));
}

shader::~shader()
{
    vkDestroyShaderModule(ctx->device, module, nullptr);
}

/////////////
// sampler //
/////////////

sampler::sampler(std::shared_ptr<context> ctx, const VkSamplerCreateInfo & create_info) : ctx{ctx}
{
    check(vkCreateSampler(ctx->device, &create_info, nullptr, &handle));
}

sampler::~sampler()
{
    vkDestroySampler(ctx->device, handle, nullptr);
}

/////////////////
// render_pass //
/////////////////

render_pass::render_pass(std::shared_ptr<context> ctx, array_view<VkAttachmentDescription> color_attachments, std::optional<VkAttachmentDescription> depth_attachment) : ctx{ctx}
{
    std::vector<VkAttachmentDescription> attachments(begin(color_attachments), end(color_attachments));
    std::vector<VkAttachmentReference> attachment_refs;
    for(uint32_t i=0; i<color_attachments.size; ++i) attachment_refs.push_back({i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    VkSubpassDescription subpass_desc {};
    subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if(depth_attachment)
    {
        attachments.push_back(*depth_attachment);
        attachment_refs.push_back({color_attachments.size, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL});
        subpass_desc.pDepthStencilAttachment = &attachment_refs[color_attachments.size];
    }
    subpass_desc.colorAttachmentCount = color_attachments.size;
    subpass_desc.pColorAttachments = attachment_refs.data();
    
    VkRenderPassCreateInfo render_pass_info {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = countof(attachments);
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass_desc;

    check(vkCreateRenderPass(ctx->device, &render_pass_info, nullptr, &handle));
}

render_pass::~render_pass()
{
    vkDestroyRenderPass(ctx->device, handle, nullptr);
}

/////////////////
// framebuffer //
/////////////////

framebuffer::framebuffer(std::shared_ptr<context> ctx, VkRenderPass render_pass, array_view<VkImageView> attachments, uint2 dims) : ctx{ctx}, dims{dims}
{
    VkFramebufferCreateInfo framebuffer_info {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = attachments.size;
    framebuffer_info.pAttachments = attachments.data;
    framebuffer_info.width = dims.x;
    framebuffer_info.height = dims.y;
    framebuffer_info.layers = 1;
    check(vkCreateFramebuffer(ctx->device, &framebuffer_info, nullptr, &handle));
}

framebuffer::~framebuffer()
{
    vkDestroyFramebuffer(ctx->device, handle, nullptr);
}

////////////////////
// scene_contract //
////////////////////

scene_contract::scene_contract(std::shared_ptr<context> ctx, array_view<VkRenderPass> render_passes, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings) : 
    ctx{ctx}, render_passes{render_passes.begin(), render_passes.end()}, per_scene_layout{ctx->create_descriptor_set_layout(per_scene_bindings)}, per_view_layout{ctx->create_descriptor_set_layout(per_view_bindings)},
    example_layout{ctx->create_pipeline_layout({per_scene_layout, per_view_layout})} {}

scene_contract::~scene_contract()
{
    vkDestroyPipelineLayout(ctx->device, example_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx->device, per_view_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx->device, per_scene_layout, nullptr);
}

scene_pipeline_layout::scene_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings) :
    contract{contract}, per_object_layout{contract->ctx->create_descriptor_set_layout(per_object_bindings)},
    pipeline_layout{contract->ctx->create_pipeline_layout({contract->per_scene_layout, contract->per_view_layout, per_object_layout})} {}

scene_pipeline_layout::~scene_pipeline_layout()
{
    vkDestroyPipelineLayout(contract->ctx->device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(contract->ctx->device, per_object_layout, nullptr);
}

VkDevice scene_pipeline_layout::get_device() const 
{ 
    return contract->ctx->device; 
}

scene_pipeline::scene_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending) : 
    layout{layout}
{
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    for(auto & s : stages) shader_stages.push_back(s->get_shader_stage());
    for(auto & p : layout->get_contract().get_render_passes()) pipelines.push_back(make_pipeline(layout->get_device(), p, layout->get_pipeline_layout(), format->get_vertex_input_state(), shader_stages, depth_write, depth_test, additive_blending));
}
    
scene_pipeline::~scene_pipeline()
{
    for(auto & p : pipelines) vkDestroyPipeline(layout->get_device(), p, nullptr);
}

void scene_descriptor_set::write_uniform_buffer(uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info)
{
    vkWriteDescriptorBufferInfo(layout->get_device(), set, binding, array_element, info);
}

void scene_descriptor_set::write_combined_image_sampler(uint32_t binding, uint32_t array_element, VkSampler sampler, VkImageView image_view, VkImageLayout image_layout)
{
    vkWriteDescriptorCombinedImageSamplerInfo(layout->get_device(), set, binding, array_element, {sampler, image_view, image_layout});
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls, VkDescriptorBufferInfo instances, size_t instance_stride)
{
    if(&pipeline.get_contract() != &contract) fail_fast();
    if(&pipeline.get_layout() != &descriptors.get_pipeline_layout()) fail_fast();

    draw_item item {&pipeline, descriptors.get_descriptor_set()};
    item.vertex_buffer_count = instance_stride ? 2 : 1;
    item.vertex_buffers[0] = *mesh.vertex_buffer;
    item.vertex_buffers[1] = instances.buffer;
    item.vertex_buffer_offsets[0] = 0;
    item.vertex_buffer_offsets[1] = instances.offset;
    item.index_buffer = *mesh.index_buffer;
    item.index_buffer_offset = 0;
    item.instance_count = instance_stride ? instances.range / instance_stride : 1;
    for(auto mtl : mtls)
    {
        item.first_index = mesh.m.materials[mtl].first_triangle*3;
        item.index_count = mesh.m.materials[mtl].num_triangles*3;
        items.push_back(item);
    }
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, VkDescriptorBufferInfo instances, size_t instance_stride)
{
    std::vector<size_t> mtls;
    for(size_t i=0; i<mesh.m.materials.size(); ++i) mtls.push_back(i);
    draw(pipeline, descriptors, mesh, mtls, instances, instance_stride);
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls)
{
    draw(pipeline, descriptors, mesh, mtls, {}, 0);
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh)
{
    draw(pipeline, descriptors, mesh, {}, 0);
}

void draw_list::write_commands(VkCommandBuffer cmd, VkRenderPass render_pass) const
{
    auto render_pass_index = contract.get_render_pass_index(render_pass);
    for(auto & item : items)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline(render_pass_index));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline_layout(), 2, {item.set}, {});
        vkCmdBindVertexBuffers(cmd, 0, item.vertex_buffer_count, item.vertex_buffers, item.vertex_buffer_offsets);
        vkCmdBindIndexBuffer(cmd, item.index_buffer, item.index_buffer_offset, VkIndexType::VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, item.index_count, item.instance_count, item.first_index, 0, 0);
    }
}

renderer::renderer() : ctx{std::make_shared<context>()} 
{

}

VkDevice renderer::get_device()
{
    return ctx->device;
}

VkFormat renderer::get_swapchain_surface_format() const 
{ 
    return ctx->selection.surface_format.format;
}

std::shared_ptr<framebuffer> renderer::create_framebuffer(VkRenderPass render_pass, array_view<VkImageView> attachments, uint2 dims)
{
    return std::make_shared<framebuffer>(ctx, render_pass, attachments, dims);
}

std::shared_ptr<shader> renderer::create_shader(VkShaderStageFlagBits stage, const char * filename)
{
    return std::make_shared<shader>(ctx, stage, compiler.compile_glsl(stage, filename));
}

std::shared_ptr<vertex_format> renderer::create_vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes)
{
    return std::make_shared<vertex_format>(bindings, attributes);
}

std::shared_ptr<scene_contract> renderer::create_contract(array_view<VkRenderPass> render_passes, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings)
{
    return std::make_shared<scene_contract>(ctx, render_passes, per_scene_bindings, per_view_bindings);
}

std::shared_ptr<scene_pipeline_layout> renderer::create_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings)
{
    return std::make_shared<scene_pipeline_layout>(contract, per_object_bindings);
}

std::shared_ptr<scene_pipeline> renderer::create_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending)
{
    return std::make_shared<scene_pipeline>(layout, format, stages, depth_write, depth_test, additive_blending);
}