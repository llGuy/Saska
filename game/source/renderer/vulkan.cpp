// VULKAN

#include "renderer.hpp"

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#undef max
#endif

#include <cassert>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <limits>
#include <vulkan/vulkan.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define SOURCE_RENDERER
#define MEMORY_API static


// Just for testing, making sure this all compiles
#include "../core.hpp"
#include "../utils.hpp"
#include "../allocators.hpp"
#include "../allocators.cpp"
#include "../memory.cpp"


struct queue_families_t
{
    int32_t graphics_family = -1;
    int32_t present_family = 1;
    inline bool complete(void) { return(graphics_family >= 0 && present_family >= 0); }
};

struct swapchain_details_t
{
    VkSurfaceCapabilitiesKHR capabilities;

    uint32_t available_formats_count;
    VkSurfaceFormatKHR *available_formats;
	
    uint32_t available_present_modes_count;
    VkPresentModeKHR *available_present_modes;
};

struct swapchain_t
{
    VkFormat format;
    VkPresentModeKHR present_mode;
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
	
    memory_buffer_view_t<VkImage> imgs;
    memory_buffer_view_t<VkImageView> views;
};

struct gpu_t
{
    VkPhysicalDevice hardware;
    VkDevice logical_device;

    VkPhysicalDeviceMemoryProperties memory_properties;
    VkPhysicalDeviceProperties properties;

    queue_families_t queue_families;
    VkQueue graphics_queue;
    VkQueue present_queue;

    swapchain_details_t swapchain_support;
    VkFormat supported_depth_format;
};

#define PRIMARY_COMMAND_BUFFER_COUNT 3
#define SYNCHRONIZATION_PRIMITIVE_COUNT 2

struct vulkan_context_t
{
    VkInstance instance;
    g3d_debug_callback_t callback;
    VkDebugUtilsMessengerEXT debug_messenger;
    gpu_t gpu;
    VkSurfaceKHR surface;
    swapchain_t swapchain;
    VkSemaphore img_ready[SYNCHRONIZATION_PRIMITIVE_COUNT];
    VkSemaphore render_finish[SYNCHRONIZATION_PRIMITIVE_COUNT];
    VkFence cpu_wait[SYNCHRONIZATION_PRIMITIVE_COUNT];
    VkCommandPool command_pool;
    g3d_command_buffer_t command_buffers[PRIMARY_COMMAND_BUFFER_COUNT];
    uint32_t image_index;
    uint32_t current_frame;
};


// Globals
static vulkan_context_t graphics_context;
static stack_dynamic_container_t<VkCommandBuffer, 20, g3d_command_buffer_t> command_buffers;

// Declarations: Vulkan specific code
static void vk_allocate_command_buffers(VkCommandBuffer *command_buffers, uint32_t count, VkCommandBufferLevel level);
static void vk_begin_command_buffer(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags usage_flags, VkCommandBufferInheritanceInfo *inheritance);
static void vk_submit_command_buffer(VkCommandBuffer command_buffer, VkSemaphore *wait_semaphores, uint32_t wait_semaphore_count, VkSemaphore *signal_semaphores, uint32_t signal_semaphore_count, VkFence fence, VkPipelineStageFlags *pipeline_wait_stages, VkQueue queue);
static void vk_end_command_buffer(VkCommandBuffer command_buffer);
static VkSemaphore vk_create_semaphore(void);
static VkFence vk_create_fence(VkFenceCreateFlags fence_flags);



static void initialize_vulkan_instance(uint32_t requested_layer_count, const char **requested_layer_names)
{
    uint32_t extension_count;
    const char **total_extension_buffer;
    
#if defined (_WIN32)
    total_extension_buffer = ALLOCA_T(const char *, 4);
    total_extension_buffer[0] = "VK_KHR_win32_surface";
    total_extension_buffer[1] = "VK_KHR_surface";
    total_extension_buffer[2] = "VK_EXT_debug_utils";
    total_extension_buffer[3] = "VK_EXT_debug_report";
    extension_count = 4;
#else
    // TODO: Implement way for non-windows systems
#endif
	
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Saska";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    uint32_t available_layer_count;
    vkEnumerateInstanceLayerProperties(&available_layer_count, nullptr);

    VkLayerProperties *properties = ALLOCA_T(VkLayerProperties, available_layer_count);
    vkEnumerateInstanceLayerProperties(&available_layer_count, properties);

    for (uint32_t r = 0; r < requested_layer_count; ++r)
    {
        bool found_layer = false;
        for (uint32_t l = 0; l < available_layer_count; ++l)
        {
            if (!strcmp(properties[l].layerName, requested_layer_names[r])) found_layer = true;
        }

        if (!found_layer) assert(false);
    }

    instance_info.enabledLayerCount = requested_layer_count;
    instance_info.ppEnabledLayerNames = requested_layer_names;

    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = total_extension_buffer;

    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &graphics_context.instance));
}


static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_proc(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT *message_data, void *user_data)
{
    graphics_context.callback(message_data->pMessage);

    return(VK_FALSE);
}


static void initialize_debug_messenger(g3d_debug_callback_t callback)
{
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {};
    debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_info.pfnUserCallback = vulkan_debug_proc;
    debug_info.pUserData = nullptr;

    PFN_vkCreateDebugUtilsMessengerEXT vk_create_debug_utils_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(graphics_context.instance, "vkCreateDebugUtilsMessengerEXT");
    assert(vk_create_debug_utils_messenger != nullptr);

    graphics_context.callback = callback;
    
    VK_CHECK(vk_create_debug_utils_messenger(graphics_context.instance, &debug_info, nullptr, &graphics_context.debug_messenger));
}


static void initialize_surface(void *platform)
{
#ifdef _WIN32
    HWND *window_handle = (HWND *)platform;

    VkWin32SurfaceCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hwnd = *window_handle;
    create_info.hinstance = GetModuleHandle(nullptr);
    VK_CHECK(vkCreateWin32SurfaceKHR(graphics_context.instance, &create_info, nullptr, &graphics_context.surface));
#else
#endif
}


static void get_swapchain_support(VkSurfaceKHR *surface)
{
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(graphics_context.gpu.hardware, *surface, &graphics_context.gpu.swapchain_support.capabilities);
    vkGetPhysicalDeviceSurfaceFormatsKHR(graphics_context.gpu.hardware, *surface, &graphics_context.gpu.swapchain_support.available_formats_count, nullptr);

    if (graphics_context.gpu.swapchain_support.available_formats_count != 0)
    {
        graphics_context.gpu.swapchain_support.available_formats = (VkSurfaceFormatKHR *)allocate_stack(sizeof(VkSurfaceFormatKHR) * graphics_context.gpu.swapchain_support.available_formats_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(graphics_context.gpu.hardware, *surface, &graphics_context.gpu.swapchain_support.available_formats_count, graphics_context.gpu.swapchain_support.available_formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(graphics_context.gpu.hardware, *surface, &graphics_context.gpu.swapchain_support.available_present_modes_count, nullptr);
    if (graphics_context.gpu.swapchain_support.available_present_modes_count != 0)
    {
        graphics_context.gpu.swapchain_support.available_present_modes = (VkPresentModeKHR *)allocate_stack(sizeof(VkPresentModeKHR) * graphics_context.gpu.swapchain_support.available_present_modes_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(graphics_context.gpu.hardware, *surface, &graphics_context.gpu.swapchain_support.available_present_modes_count, graphics_context.gpu.swapchain_support.available_present_modes);
    }
}


static VkFormat find_supported_format(const VkFormat *candidates, uint32_t candidate_size, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (uint32_t i = 0; i < candidate_size; ++i)
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(graphics_context.gpu.hardware, candidates[i], &properties);
        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
        {
            return(candidates[i]);
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
        {
            return(candidates[i]);
        }
    }

    assert(false);

    return VkFormat{};
}


static void find_depth_format(void)
{
    VkFormat formats[] =  { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
    
    graphics_context.gpu.supported_depth_format = find_supported_format(formats, 3, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}


static void find_queue_families(queue_families_t *families, VkPhysicalDevice hardware, VkSurfaceKHR *surface)
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(hardware, &queue_family_count, nullptr);

    VkQueueFamilyProperties *queue_properties = (VkQueueFamilyProperties *)allocate_stack(sizeof(VkQueueFamilyProperties) * queue_family_count, alignment_t(1), "queue_family_list_allocation");
    vkGetPhysicalDeviceQueueFamilyProperties(hardware, &queue_family_count, queue_properties);

    for (uint32_t i = 0; i < queue_family_count; ++i)
    {
        if (queue_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && queue_properties[i].queueCount > 0)
        {
            families->graphics_family = i;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(hardware, i, *surface, &present_support);
	
        if (queue_properties[i].queueCount > 0 && present_support)
        {
            families->present_family = i;
        }
	
        if (families->complete())
        {
            break;
        }
    }

    pop_stack();
}


static void initialize_gpu(uint32_t requested_layer_count, const char **requested_layer_names)
{
    // Find GPU
    static constexpr uint32_t gpu_requested_extension_count = 1;
    const char *gpu_requested_extension_names[gpu_requested_extension_count] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(graphics_context.instance, &device_count, nullptr);
    
    VkPhysicalDevice *devices = (VkPhysicalDevice *)allocate_linear(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(graphics_context.instance, &device_count, devices);

    for (uint32_t i = 0; i < device_count; ++i)
    {
        graphics_context.gpu.hardware = devices[i];
        
        find_queue_families(&graphics_context.gpu.queue_families, graphics_context.gpu.hardware, &graphics_context.surface);

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(graphics_context.gpu.hardware, &device_properties);
    
        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(graphics_context.gpu.hardware, &device_features);

        uint32_t available_extension_count;
        vkEnumerateDeviceExtensionProperties(graphics_context.gpu.hardware, nullptr, &available_extension_count, nullptr);

        VkExtensionProperties *available_extension_properties = (VkExtensionProperties *)allocate_linear(sizeof(VkExtensionProperties) * available_extension_count);
        vkEnumerateDeviceExtensionProperties(graphics_context.gpu.hardware, nullptr, &available_extension_count, available_extension_properties);
    
        uint32_t required_extensions_left = gpu_requested_extension_count;
        for (uint32_t i = 0; i < available_extension_count && required_extensions_left > 0; ++i)
        {
            for (uint32_t j = 0; j < gpu_requested_extension_count; ++j)
            {
                if (!strcmp(available_extension_properties[i].extensionName, gpu_requested_extension_names[j]))
                {
                    --required_extensions_left;
                }
            }
        }

        bool swapchain_supported = (!required_extensions_left);
        

        bool swapchain_usable = false;
        if (swapchain_supported)
        {
            get_swapchain_support(&graphics_context.surface);
            
            swapchain_usable = graphics_context.gpu.swapchain_support.available_formats_count && graphics_context.gpu.swapchain_support.available_present_modes_count;
        }

        bool is_gpu_suitable = (swapchain_supported && swapchain_usable
                                && (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                                && graphics_context.gpu.queue_families.complete()
                                && device_features.geometryShader
                                && device_features.wideLines
                                && device_features.textureCompressionBC
                                && device_features.samplerAnisotropy
                                && device_features.fillModeNonSolid);


        
        if (is_gpu_suitable)
        {
            vkGetPhysicalDeviceProperties(graphics_context.gpu.hardware, &graphics_context.gpu.properties);
		
            break;
        }
    }

    assert(graphics_context.gpu.hardware != VK_NULL_HANDLE);
    
    vkGetPhysicalDeviceMemoryProperties(graphics_context.gpu.hardware, &graphics_context.gpu.memory_properties);
    
    find_depth_format();
    
    // Initialize logical device
    queue_families_t *indices = &graphics_context.gpu.queue_families;

    bitset32_t bitset;
    bitset.set1(indices->graphics_family);
    bitset.set1(indices->present_family);

    uint32_t unique_sets = bitset.pop_count();

    uint32_t *unique_family_indices = (uint32_t *)allocate_linear(sizeof(uint32_t) * unique_sets);
    VkDeviceQueueCreateInfo *unique_queue_infos = (VkDeviceQueueCreateInfo *)allocate_linear(sizeof(VkDeviceCreateInfo) * unique_sets);

    // fill the unique_family_indices with the indices
    for (uint32_t b = 0, set_bit = 0; b < 32 && set_bit < unique_sets; ++b)
    {
        if (bitset.get(b))
        {
            unique_family_indices[set_bit++] = b;
        }
    }
    
    float priority1 = 1.0f;
    for (uint32_t i = 0; i < unique_sets; ++i)
    {
        VkDeviceQueueCreateInfo queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = unique_family_indices[i];
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority1;
        unique_queue_infos[i] = queue_info;
    }

    VkPhysicalDeviceFeatures device_features = {};
    device_features.samplerAnisotropy = VK_TRUE;
    device_features.wideLines = VK_TRUE;
    device_features.geometryShader = VK_TRUE;
    device_features.fillModeNonSolid = VK_TRUE;
	
    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pQueueCreateInfos = unique_queue_infos;
    device_info.queueCreateInfoCount = unique_sets;
    device_info.pEnabledFeatures = &device_features;
    device_info.enabledExtensionCount = gpu_requested_extension_count;
    device_info.ppEnabledExtensionNames = gpu_requested_extension_names;
    device_info.ppEnabledLayerNames = requested_layer_names;
    device_info.enabledLayerCount = requested_layer_count;
    
    VK_CHECK(vkCreateDevice(graphics_context.gpu.hardware, &device_info, nullptr, &graphics_context.gpu.logical_device));

    vkGetDeviceQueue(graphics_context.gpu.logical_device, graphics_context.gpu.queue_families.graphics_family, 0, &graphics_context.gpu.graphics_queue);
    vkGetDeviceQueue(graphics_context.gpu.logical_device, graphics_context.gpu.queue_families.present_family, 0, &graphics_context.gpu.present_queue);
}


static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR *available_formats, uint32_t format_count)
{
    if (format_count == 1 && available_formats[0].format == VK_FORMAT_UNDEFINED)
    {
        VkSurfaceFormatKHR format;
        format.format = VK_FORMAT_B8G8R8A8_UNORM;
        format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
    for (uint32_t i = 0; i < format_count; ++i)
    {
        if (available_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && available_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return(available_formats[i]);
        }
    }

    return(available_formats[0]);
}


static VkPresentModeKHR choose_surface_present_mode(const VkPresentModeKHR *available_present_modes, uint32_t present_modes_count)
{
    // supported by most hardware
    VkPresentModeKHR best_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < present_modes_count; ++i)
    {
        if (available_present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return(available_present_modes[i]);
        }
        else if (available_present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            best_mode = available_present_modes[i];
        }
    }
    return(best_mode);
}


static VkExtent2D choose_swapchain_extent(g3d_display_window_info_t window_info, const VkSurfaceCapabilitiesKHR *capabilities)
{
    if (capabilities->currentExtent.width != std::numeric_limits<uint64_t>::max())
    {
        return(capabilities->currentExtent);
    }
    else
    {
        int32_t width = window_info.width, height = window_info.height;

        VkExtent2D actual_extent = { (uint32_t)width, (uint32_t)height };
        actual_extent.width = MAX(capabilities->minImageExtent.width, MIN(capabilities->maxImageExtent.width, actual_extent.width));
        actual_extent.height = MAX(capabilities->minImageExtent.height, MIN(capabilities->maxImageExtent.height, actual_extent.height));

        return(actual_extent);
    }
}


static void initialize_swapchain(g3d_display_window_info_t window_info)
{
    swapchain_details_t *swapchain_details = &graphics_context.gpu.swapchain_support;
    
    VkSurfaceFormatKHR surface_format = choose_surface_format(swapchain_details->available_formats, swapchain_details->available_formats_count);
    
    VkExtent2D surface_extent = choose_swapchain_extent(window_info, &swapchain_details->capabilities);
    
    VkPresentModeKHR present_mode = choose_surface_present_mode(swapchain_details->available_present_modes, swapchain_details->available_present_modes_count);

    // add 1 to the minimum images supported in the swapchain
    uint32_t image_count = swapchain_details->capabilities.minImageCount + 1;
    if (image_count > swapchain_details->capabilities.maxImageCount)
    {
        image_count = swapchain_details->capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = graphics_context.surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = surface_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queue_family_indices[] = { (uint32_t)graphics_context.gpu.queue_families.graphics_family, (uint32_t)graphics_context.gpu.queue_families.present_family };

    if (graphics_context.gpu.queue_families.graphics_family != graphics_context.gpu.queue_families.present_family)
    {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_info.queueFamilyIndexCount = 2;
        swapchain_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.queueFamilyIndexCount = 0;
        swapchain_info.pQueueFamilyIndices = nullptr;
    }

    swapchain_info.preTransform = swapchain_details->capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(graphics_context.gpu.logical_device, &swapchain_info, nullptr, &graphics_context.swapchain.swapchain));

    vkGetSwapchainImagesKHR(graphics_context.gpu.logical_device, graphics_context.swapchain.swapchain, &image_count, nullptr);

    allocate_memory_buffer(graphics_context.swapchain.imgs, image_count);
	
    vkGetSwapchainImagesKHR(graphics_context.gpu.logical_device, graphics_context.swapchain.swapchain, &image_count, graphics_context.swapchain.imgs.buffer);
	
    graphics_context.swapchain.extent = surface_extent;
    graphics_context.swapchain.format = surface_format.format;
    graphics_context.swapchain.present_mode = present_mode;

    allocate_memory_buffer(graphics_context.swapchain.views, image_count);
	
    for (uint32_t i = 0
             ; i < image_count
             ; ++i)
    {
        VkImage *image = &graphics_context.swapchain.imgs[i];

        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = *image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = graphics_context.swapchain.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(graphics_context.gpu.logical_device, &view_info, nullptr, &graphics_context.swapchain.views[i]));
    }
}


static void initialize_command_pool(uint32_t queue_family_index)
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = queue_family_index;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(graphics_context.gpu.logical_device, &pool_info, nullptr, &graphics_context.command_pool));
}


static void initialize_command_buffers(void)
{
    for (uint32_t i = 0; i < PRIMARY_COMMAND_BUFFER_COUNT; ++i)
    {
        graphics_context.command_buffers[i] = command_buffers.add();
        vk_allocate_command_buffers(command_buffers.get(graphics_context.command_buffers[i]), 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
}


static void initialize_synchronization_primitives(void)
{
    for (uint32_t i = 0; i < SYNCHRONIZATION_PRIMITIVE_COUNT; ++i)
    {
        graphics_context.cpu_wait[i] = vk_create_fence(VK_FENCE_CREATE_SIGNALED_BIT);
        graphics_context.img_ready[i] = vk_create_semaphore();
        graphics_context.render_finish[i] = vk_create_semaphore();
    }
}


void g3d_initialize_renderer(void *platform_specific, g3d_debug_callback_t callback, g3d_display_window_info_t window_info)
{
    const uint32_t requested_layer_count = 1;
    const char *requested_layer_names[requested_layer_count] = { "VK_LAYER_LUNARG_standard_validation" };
    
    initialize_vulkan_instance(requested_layer_count, requested_layer_names);
    initialize_debug_messenger(callback);
    initialize_surface(platform_specific);
    initialize_gpu(requested_layer_count, requested_layer_names);
    initialize_swapchain(window_info);
    initialize_command_pool(graphics_context.gpu.queue_families.graphics_family);
    initialize_command_buffers();
    initialize_synchronization_primitives();
}


struct next_image_return_t {VkResult result; uint32_t image_index;};
next_image_return_t acquire_next_image(VkSemaphore semaphore, VkFence fence)
{
    uint32_t image_index = 0;
    
    VkResult result = vkAcquireNextImageKHR(graphics_context.gpu.logical_device, graphics_context.swapchain.swapchain, std::numeric_limits<uint64_t>::max(), semaphore, fence, &image_index);
    
    return(next_image_return_t{result, image_index});
}


void g3d_begin_frame_rendering(void)
{
    static uint32_t current_frame = 0;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    graphics_context.current_frame = 0;
    
    VkFence null_fence = VK_NULL_HANDLE;
    
    next_image_return_t next_image_data = acquire_next_image(graphics_context.img_ready[graphics_context.current_frame], null_fence);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        //recreate_swapchain(input_state);
	return {};
    }
    else if (next_image_data.result != VK_SUCCESS && next_image_data.result != VK_SUBOPTIMAL_KHR)
    {
    }

    vkWaitForFences(graphics_context.gpu.logical_device, 1, &graphics_context.cpu_wait[graphics_context.current_frame], VK_TRUE, std::numeric_limits<uint64_t>::max());

    vkResetFences(graphics_context.gpu.logical_device, 1, &graphics_context.cpu_wait[graphics_context.current_frame]);

    graphics_context.image_index = next_image_data.image_index;

    // Begin command buffer for any kind of rendering operations
    vk_begin_command_buffer(*command_buffers.get(graphics_context.command_buffer[graphics_context.current_frame]), 0, nullptr);
}


static void g3d_end_frame_rendering_and_refresh(void)
{
    end_command_buffer(*command_buffers.get(g_context->command_buffer[g_context->current_frame]));

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;;

    submit(memory_buffer_view_t<VkCommandBuffer>{1, &g_context->command_buffer[g_context->current_frame]},
           memory_buffer_view_t<VkSemaphore>{1, &g_context->img_ready[g_context->current_frame]},
           memory_buffer_view_t<VkSemaphore>{1, &g_context->render_finish[g_context->current_frame]},
           memory_buffer_view_t<VkPipelineStageFlags>{1, &wait_stages},
           &g_context->cpu_wait[g_context->current_frame],
           &g_context->gpu.graphics_queue);
    
    VkSemaphore signal_semaphores[] = {g_context->render_finish[g_context->current_frame]};

    VkResult result = present(memory_buffer_view_t<VkSemaphore>{1, &g_context->render_finish[g_context->current_frame]},
                              &g_context->image_index,
                              &g_context->gpu.present_queue);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        //recreate_swapchain(input_state);
    }
    else if (result != VK_SUCCESS)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to present swapchain image");
    }
}


// Definitions: Vulkan specific code
static void vk_allocate_command_buffers(VkCommandBuffer *command_buffers, uint32_t count, VkCommandBufferLevel level)
{
    VkCommandBufferAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.level = level;
    allocate_info.commandPool = graphics_context.command_pool;
    allocate_info.commandBufferCount = count;

    vkAllocateCommandBuffers(graphics_context.gpu.logical_device, &allocate_info, command_buffers);
}


static VkSemaphore vk_create_semaphore(void)
{
    VkSempahore semaphore;
    
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	
    VK_CHECK(vkCreateSemaphore(graphics_context.gpu.logical_device, &semaphore_info, nullptr, &semaphore));

    return(semaphore);
}


static VkFence vk_create_fence(VkFenceCreateFlags fence_flags)
{
    VkFence fence;
    
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = fence_flags;

    VK_CHECK(vkCreateFence(graphics_context.gpu.logical_device, &fence_info, nullptr, &fence));

    return(fence);
}


static void vk_begin_command_buffer(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags usage_flags, VkCommandBufferInheritanceInfo *inheritance)
{
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = usage_flags;
    begin_info.pInheritanceInfo = inheritance;

    vkBeginCommandBuffer(command_buffer, &begin_info);
}


static void vk_end_command_buffer(VkCommandBuffer command_buffer)
{
    vkEndCommandBuffer(command_buffer);
}


static void vk_submit_command_buffer(VkCommandBuffer command_buffer, VkSemaphore *wait_semaphores, uint32_t wait_semaphore_count, VkSemaphore *signal_semaphores, uint32_t signal_semaphore_count, VkFence fence, VkPipelineStageFlags *pipeline_wait_stages, VkQueue queue)
{
    
}
