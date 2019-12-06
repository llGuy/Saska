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

using byte_t = uint8_t;

#include "../memory.cpp"

#define VK_CHECK(f) f
#define ALLOCA_T(t, c) (t *)alloca(sizeof(t) * c)

template <typename T> struct memory_buffer_view_t
{
    uint32_t count;
    T *buffer;
    inline void zero(void) { memset(buffer, 0, count * sizeof(T)); }
    inline T &operator[](uint32_t i) { return(buffer[i]); }
    inline const T &operator[](uint32_t i) const { return(buffer[i]); }
};
template <typename T> void allocate_memory_buffer(memory_buffer_view_t<T> &view, uint32_t count)
{
    view.count = count;
    view.buffer = (T *)allocate_free_list(count * sizeof(T));
}

#ifndef __GNUC__
#include <intrin.h>
#endif

inline constexpr uint32_t left_shift(uint32_t n) { return 1 << n; }
struct bitset32_t
{
    uint32_t bitset = 0;

    inline uint32_t pop_count(void)
    {
#ifndef __GNUC__
	return __popcnt(bitset);
#else
	return __builtin_popcount(bitset);  
#endif
    }
    inline void set1(uint32_t bit) { bitset |= left_shift(bit); }
    inline void set0(uint32_t bit) { bitset &= ~(left_shift(bit)); }
    inline bool get(uint32_t bit) { return bitset & left_shift(bit); }
};

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

    void find_queue_families(VkSurfaceKHR *surface)
    {
        // TODO: Implement
    }
};

struct vulkan_context_t
{
    VkInstance instance;
    debug_callback_t callback;
    VkDebugUtilsMessengerEXT debug_messenger;
    gpu_t gpu;
    VkSurfaceKHR surface;
    swapchain_t swapchain;
    VkSemaphore img_ready [2];
    VkSemaphore render_finish [2];
    VkFence cpu_wait [2];
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer[3];
    uint32_t image_index;
    uint32_t current_frame;



    // Pointers to allocators
    linear_allocator_t *linear_allocator;
    stack_allocator_t *stack_allocator;
    free_list_allocator_t *free_list_allocator;
};




// Global
static vulkan_context_t graphics_context;





// Memory operations
static inline void *allocate_linear(uint32_t alloc_size, alignment_t alignment = 1, const char *name = "", linear_allocator_t *allocator = graphics_context.linear_allocator)
{
    return(allocate_linear_impl(alloc_size, alignment, name, allocator));
}

static inline void clear_linear(linear_allocator_t *allocator = graphics_context.linear_allocator)
{
    clear_linear_impl(allocator);
}

static inline void *allocate_stack(uint32_t allocation_size, alignment_t alignment = 1, const char *name = "", stack_allocator_t *allocator = graphics_context.stack_allocator)
{
    return(allocate_stack_impl(allocation_size, alignment, name, allocator));
}

// only applies to the allocation at the top of the stack
static inline void extend_stack_top(uint32_t extension_size, stack_allocator_t *allocator = graphics_context.stack_allocator)
{
    extend_stack_top_impl(extension_size, allocator);
}

// contents in the allocation must be destroyed by the user
static inline void pop_stack(stack_allocator_t *allocator = graphics_context.stack_allocator)
{
    pop_stack_impl(allocator);
}

static inline void *allocate_free_list(uint32_t allocation_size, alignment_t alignment = 1, const char *name = "", free_list_allocator_t *allocator = graphics_context.free_list_allocator)
{
    return(allocate_free_list_impl(allocation_size, alignment, name, allocator));
}

static inline void deallocate_free_list(void *pointer, free_list_allocator_t *allocator = graphics_context.free_list_allocator)
{
    deallocate_free_list_impl(pointer, allocator);
}



// Renderer methods
#define IMPLEMENT_RENDERER_METHOD(return_type, name, ...) return_type name##__dllimpl(__VA_ARGS__)

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


static void initialize_debug_messenger(debug_callback_t callback)
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

        graphics_context.gpu.find_queue_families(&graphics_context.surface);

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


static VkExtent2D choose_swapchain_extent(display_window_info_t window_info, const VkSurfaceCapabilitiesKHR *capabilities)
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


static void initialize_swapchain(display_window_info_t window_info)
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


IMPLEMENT_RENDERER_METHOD(void, initialize_renderer, void *platform_specific, debug_callback_t callback, allocators_t allocators, display_window_info_t window_info)
{
    // Initialize allocator pointers
    graphics_context.linear_allocator = allocators.linear_allocator;
    graphics_context.stack_allocator = allocators.stack_allocator;
    graphics_context.free_list_allocator = allocators.free_list_allocator;

    const uint32_t requested_layer_count = 1;
    const char *requested_layer_names[requested_layer_count] = { "VK_LAYER_LUNARG_standard_validation" };
    
    initialize_vulkan_instance(requested_layer_count, requested_layer_names);
    initialize_debug_messenger(callback);
    initialize_surface(platform_specific);
    initialize_gpu(requested_layer_count, requested_layer_names);
    initialize_swapchain(window_info);
}
