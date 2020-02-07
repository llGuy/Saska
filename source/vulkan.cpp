#include "game.hpp"
/* vulkan.cpp */

// TODO: Big refactor after animations are loaded

#define GLFW_INCLUDE_VULKAN
#include <cstring>

#include "vulkan.hpp"
#include <limits.h>
//#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

static vulkan_context_t *g_context;

void mapped_gpu_memory_t::begin(void)
{
    vkMapMemory(g_context->gpu.logical_device, *memory, offset, size, 0, &data);
}

void mapped_gpu_memory_t::fill(memory_byte_buffer_t byte_buffer)
{
    memcpy(data, byte_buffer.ptr, size);
}

void mapped_gpu_memory_t::flush(uint32_t offset, uint32_t size)
{
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = *memory;
    range.offset = offset;
    range.size = size;
    vkFlushMappedMemoryRanges(g_context->gpu.logical_device, 1, &range);
}

void mapped_gpu_memory_t::end(void)
{
    vkUnmapMemory(g_context->gpu.logical_device, *memory);
}

void gpu_buffer_t::destroy(void)
{
    vkDestroyBuffer(g_context->gpu.logical_device, buffer, nullptr);
    vkFreeMemory(g_context->gpu.logical_device, memory, nullptr);
}

void destroy_shader_module(VkShaderModule *module)
{
    vkDestroyShaderModule(g_context->gpu.logical_device, *module, nullptr);
}

VkMemoryRequirements image2d_t::get_memory_requirements(void)
{
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(g_context->gpu.logical_device, image, &requirements);

    return(requirements);
}

void image2d_t::destroy(void)
{
    if (image != VK_NULL_HANDLE) vkDestroyImage(g_context->gpu.logical_device, image, nullptr);
    if (image_view != VK_NULL_HANDLE) vkDestroyImageView(g_context->gpu.logical_device, image_view, nullptr);
    if (image_sampler != VK_NULL_HANDLE) vkDestroySampler(g_context->gpu.logical_device, image_sampler, nullptr);
    if (device_memory != VK_NULL_HANDLE) vkFreeMemory(g_context->gpu.logical_device, device_memory, nullptr);
}

mapped_gpu_memory_t image2d_t::construct_map(void)
{
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(g_context->gpu.logical_device, image, &requirements);
    return(mapped_gpu_memory_t{0, requirements.size, &device_memory});
}

void render_pass_t::destroy(void)
{
    vkDestroyRenderPass(g_context->gpu.logical_device, render_pass, nullptr);
}

void framebuffer_t::destroy(void)
{
    vkDestroyFramebuffer(g_context->gpu.logical_device, framebuffer, nullptr);
}

void init_semaphore(VkSemaphore *semaphore)
{
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	
    VK_CHECK(vkCreateSemaphore(g_context->gpu.logical_device, &semaphore_info, nullptr, semaphore));
}

void init_fence(VkFenceCreateFlags flags, VkFence *fence)
{
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = flags;

    VK_CHECK(vkCreateFence(g_context->gpu.logical_device, &fence_info, nullptr, fence));
}

#undef max

void wait_fences(const memory_buffer_view_t<VkFence> &fences)
{
    vkWaitForFences(g_context->gpu.logical_device, fences.count, fences.buffer, VK_TRUE, std::numeric_limits<uint64_t>::max());
}

VkFormat get_device_supported_depth_format(void)
{
    return g_context->gpu.supported_depth_format;
}

next_image_return_t acquire_next_image(VkSemaphore *semaphore, VkFence *fence)
{
    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(g_context->gpu.logical_device, g_context->swapchain.swapchain, std::numeric_limits<uint64_t>::max(), *semaphore, *fence, &image_index);
    return(next_image_return_t{result, image_index});
}

void reset_fences(const memory_buffer_view_t<VkFence> &fences)
{
    vkResetFences(g_context->gpu.logical_device, fences.count, fences.buffer);
}

uint32_t adjust_memory_size_for_gpu_alignment(uint32_t size)
{
    uint32_t alignment = (uint32_t)(g_context->gpu.properties.limits.nonCoherentAtomSize);

    uint32_t mod = size % alignment;
	
    if (mod == 0) return size;
    else return size + alignment - mod;
}

void free_command_buffer(const memory_buffer_view_t<VkCommandBuffer> &command_buffers, VkCommandPool *pool)
{
    vkFreeCommandBuffers(g_context->gpu.logical_device, *pool, command_buffers.count, command_buffers.buffer);
}

static uint32_t find_memory_type_according_to_requirements(VkMemoryPropertyFlags properties, VkMemoryRequirements memory_requirements)
{
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_context->gpu.hardware, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i)
    {
        if (memory_requirements.memoryTypeBits & (1 << i)
            && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return(i);
        }
    }
	    
    //OUTPUT_DEBUG_LOG("%s\n", "failed to find suitable memory type");
    assert(false);
    return(0);
}
	
void allocate_gpu_memory(VkMemoryPropertyFlags properties, VkMemoryRequirements memory_requirements, VkDeviceMemory *dest_memory)
{
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type_according_to_requirements(properties, memory_requirements);

    VK_CHECK(vkAllocateMemory(g_context->gpu.logical_device, &alloc_info, nullptr, dest_memory));
}

queue_families_t *get_queue_families(void)
{
    return &g_context->gpu.queue_families;
}

VkFormat get_swapchain_format(void)
{
    return g_context->swapchain.format;
}

VkExtent2D get_swapchain_extent(void)
{
    return g_context->swapchain.extent;
}

VkImageView *get_swapchain_image_views(void)
{
    return g_context->swapchain.views.buffer;
}

uint32_t get_swapchain_image_count(void)
{
    return g_context->swapchain.imgs.count;
}

VkQueue *get_present_queue(void)
{
    return &g_context->gpu.present_queue;
}

VkQueue *get_graphics_queue(void)
{
    return &g_context->gpu.graphics_queue;
}

VkCommandPool *get_global_command_pool(void)
{
    return(&g_context->command_pool);
}

void gpu_t::find_queue_families(VkSurfaceKHR *surface)
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(hardware, &queue_family_count, nullptr);

    VkQueueFamilyProperties *queue_properties = (VkQueueFamilyProperties *)allocate_stack(sizeof(VkQueueFamilyProperties) * queue_family_count, alignment_t(1), "queue_family_list_allocation");
    vkGetPhysicalDeviceQueueFamilyProperties(hardware, &queue_family_count, queue_properties);

    for (uint32_t i = 0; i < queue_family_count; ++i)
    {
        if (queue_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && queue_properties[i].queueCount > 0)
        {
            queue_families.graphics_family = i;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(hardware, i, *surface, &present_support);
	
        if (queue_properties[i].queueCount > 0 && present_support)
        {
            queue_families.present_family = i;
        }
	
        if (queue_families.complete())
        {
            break;
        }
    }

    pop_stack();
}

struct instance_create_validation_layer_params_t
{
    bool r_enable;
    uint32_t o_layer_count;
    const char **o_layer_names;
};

struct instance_create_extension_params_t
{
    uint32_t r_extension_count;
    const char **r_extension_names;
};

static void init_instance(VkApplicationInfo *app_info, instance_create_validation_layer_params_t *validation_params, instance_create_extension_params_t *extension_params)
{
    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = app_info;

    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    VkLayerProperties *properties = (VkLayerProperties *)allocate_stack(sizeof(VkLayerProperties) * layer_count, alignment_t(1), "validation_layer_list_allocation");
    vkEnumerateInstanceLayerProperties(&layer_count, properties);

    for (uint32_t r = 0; r < validation_params->o_layer_count; ++r)
    {
        bool found_layer = false;
        for (uint32_t l = 0; l < layer_count; ++l)
        {
            if (!strcmp(properties[l].layerName, validation_params->o_layer_names[r])) found_layer = true;
        }

        if (!found_layer) assert(false);
    }

    // if found then add to the instance information
    instance_info.enabledLayerCount = validation_params->o_layer_count;
    instance_info.ppEnabledLayerNames = validation_params->o_layer_names;

    // get extensions needed

    instance_info.enabledExtensionCount = extension_params->r_extension_count;
    instance_info.ppEnabledExtensionNames = extension_params->r_extension_names;

    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &g_context->instance), "failed to create instance\n");

    pop_stack();
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_proc(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT *message_data, void *user_data)
{
    //output_to_debug_console("Vulkan> ", message_data->pMessage, "\n");

    return(VK_FALSE);
}

static void init_debug_messenger(void)
{
    // setup debugger
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

    PFN_vkCreateDebugUtilsMessengerEXT vk_create_debug_utils_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_context->instance, "vkCreateDebugUtilsMessengerEXT");
    assert(vk_create_debug_utils_messenger != nullptr);
    VK_CHECK(vk_create_debug_utils_messenger(g_context->instance, &debug_info, nullptr, &g_context->debug_messenger));
}

static void get_swapchain_support(VkSurfaceKHR *surface)
{
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_context->gpu.hardware, *surface, &g_context->gpu.swapchain_support.capabilities);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_context->gpu.hardware, *surface, &g_context->gpu.swapchain_support.available_formats_count, nullptr);

    if (g_context->gpu.swapchain_support.available_formats_count != 0)
    {
        g_context->gpu.swapchain_support.available_formats = (VkSurfaceFormatKHR *)allocate_stack(sizeof(VkSurfaceFormatKHR) * g_context->gpu.swapchain_support.available_formats_count, alignment_t(1), "surface_format_list_allocation");
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_context->gpu.hardware, *surface, &g_context->gpu.swapchain_support.available_formats_count, g_context->gpu.swapchain_support.available_formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(g_context->gpu.hardware, *surface, &g_context->gpu.swapchain_support.available_present_modes_count, nullptr);
    if (g_context->gpu.swapchain_support.available_present_modes_count != 0)
    {
        g_context->gpu.swapchain_support.available_present_modes = (VkPresentModeKHR *)allocate_stack(sizeof(VkPresentModeKHR) * g_context->gpu.swapchain_support.available_present_modes_count, alignment_t(1), "surface_present_mode_list_allocation");
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_context->gpu.hardware, *surface, &g_context->gpu.swapchain_support.available_present_modes_count, g_context->gpu.swapchain_support.available_present_modes);
    }
}
    
struct physical_device_extensions_params_t
{
    uint32_t r_extension_count;
    const char **r_extension_names;
};

static bool check_if_physical_device_supports_extensions(physical_device_extensions_params_t *extension_params, VkPhysicalDevice gpu)
{
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extension_count, nullptr);

    VkExtensionProperties *extension_properties = (VkExtensionProperties *)allocate_stack(sizeof(VkExtensionProperties) * extension_count, alignment_t(1), "gpu_extension_properties_list_allocation");
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extension_count, extension_properties);
    
    uint32_t required_extensions_left = extension_params->r_extension_count;
    for (uint32_t i = 0; i < extension_count && required_extensions_left > 0; ++i)
    {
        for (uint32_t j = 0; j < extension_params->r_extension_count; ++j)
        {
            if (!strcmp(extension_properties[i].extensionName, extension_params->r_extension_names[j]))
            {
                --required_extensions_left;
            }
        }
    }
    pop_stack();

    return(!required_extensions_left);
}
    
static bool check_if_physical_device_is_suitable(physical_device_extensions_params_t *extension_params, VkSurfaceKHR *surface)
{
    g_context->gpu.find_queue_families(surface);

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(g_context->gpu.hardware, &device_properties);
    
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(g_context->gpu.hardware, &device_features);

    bool swapchain_supported = check_if_physical_device_supports_extensions(extension_params, g_context->gpu.hardware);

    bool swapchain_usable = false;
    if (swapchain_supported)
    {
        get_swapchain_support(surface);
        swapchain_usable = g_context->gpu.swapchain_support.available_formats_count && g_context->gpu.swapchain_support.available_present_modes_count;
    }

    return(swapchain_supported && swapchain_usable
           && (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
           && g_context->gpu.queue_families.complete()
           && device_features.geometryShader
           && device_features.wideLines
           && device_features.textureCompressionBC
           && device_features.samplerAnisotropy
           && device_features.fillModeNonSolid);
}

static void choose_gpu(physical_device_extensions_params_t *extension_params)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(g_context->instance, &device_count, nullptr);
    
    VkPhysicalDevice *devices = (VkPhysicalDevice *)allocate_stack(sizeof(VkPhysicalDevice) * device_count, alignment_t(1), "physical_device_list_allocation");
    vkEnumeratePhysicalDevices(g_context->instance, &device_count, devices);

    //OUTPUT_DEBUG_LOG("available physical hardware devices count : %d\n", device_count);

    for (uint32_t i = 0
             ; i < device_count
             ; ++i)
    {
        g_context->gpu.hardware = devices[i];
	
        // check if device is suitable
        if (check_if_physical_device_is_suitable(extension_params, &g_context->surface))
        {
            vkGetPhysicalDeviceProperties(g_context->gpu.hardware, &g_context->gpu.properties);
		
            break;
        }
    }

    assert(g_context->gpu.hardware != VK_NULL_HANDLE);
    //OUTPUT_DEBUG_LOG("%s\n", "found gpu compatible with application");
}

static void init_device(physical_device_extensions_params_t *gpu_extensions, instance_create_validation_layer_params_t *validation_layers)
{
    // create the logical device
    queue_families_t *indices = &g_context->gpu.queue_families;

    bitset32_t bitset;
    bitset.set1(indices->graphics_family);
    bitset.set1(indices->present_family);

    uint32_t unique_sets = bitset.pop_count();

    uint32_t *unique_family_indices = (uint32_t *)allocate_stack(sizeof(uint32_t) * unique_sets
                                                       , alignment_t(1)
                                                       , "unique_queue_family_indices_allocation");
    VkDeviceQueueCreateInfo *unique_queue_infos = (VkDeviceQueueCreateInfo *)allocate_stack(sizeof(VkDeviceCreateInfo) * unique_sets, alignment_t(1), "unique_queue_list_allocation");

    // fill the unique_family_indices with the indices
    for (uint32_t b = 0, set_bit = 0
             ; b < 32 && set_bit < unique_sets
             ; ++b)
    {
        if (bitset.get(b))
        {
            unique_family_indices[set_bit++] = b;
        }
    }
    
    float32_t priority1 = 1.0f;
    for (uint32_t i = 0
             ; i < unique_sets
             ; ++i)
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
    device_info.enabledExtensionCount = gpu_extensions->r_extension_count;
    device_info.ppEnabledExtensionNames = gpu_extensions->r_extension_names;
    device_info.ppEnabledLayerNames = validation_layers->o_layer_names;
    device_info.enabledLayerCount = validation_layers->o_layer_count;
    
    VK_CHECK(vkCreateDevice(g_context->gpu.hardware, &device_info, nullptr, &g_context->gpu.logical_device));
    pop_stack();
    pop_stack();

    vkGetDeviceQueue(g_context->gpu.logical_device, g_context->gpu.queue_families.graphics_family, 0, &g_context->gpu.graphics_queue);
    vkGetDeviceQueue(g_context->gpu.logical_device, g_context->gpu.queue_families.present_family, 0, &g_context->gpu.present_queue);
}

static VkSurfaceFormatKHR choose_surface_format(VkSurfaceFormatKHR *available_formats, uint32_t format_count)
{
    if (format_count == 1 && available_formats[0].format == VK_FORMAT_UNDEFINED)
    {
        VkSurfaceFormatKHR format;
        format.format		= VK_FORMAT_B8G8R8A8_UNORM;
        format.colorSpace	= VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
    for (uint32_t i = 0
             ; i < format_count
             ; ++i)
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
    for (uint32_t i = 0
             ; i < present_modes_count
             ; ++i)
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

static VkExtent2D choose_swapchain_extent(raw_input_t *raw_input, const VkSurfaceCapabilitiesKHR *capabilities)
{
    if (capabilities->currentExtent.width != std::numeric_limits<uint64_t>::max())
    {
        return(capabilities->currentExtent);
    }
    else
    {
        int32_t width = raw_input->window_width, height = raw_input->window_height;

        VkExtent2D actual_extent	= { (uint32_t)width, (uint32_t)height };
        actual_extent.width		= MAX(capabilities->minImageExtent.width, MIN(capabilities->maxImageExtent.width, actual_extent.width));
        actual_extent.height	= MAX(capabilities->minImageExtent.height, MIN(capabilities->maxImageExtent.height, actual_extent.height));

        return(actual_extent);
    }
}

VkSubresourceLayout get_image_subresource_layout(VkImage *image,VkImageSubresource *subresource)
{
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(g_context->gpu.logical_device, *image, subresource, &layout);
    return(layout);
}

void init_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t layers, image2d_t *dest_image, uint32_t mips, VkImageCreateFlags flags)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = mips;
    image_info.arrayLayers = layers;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.flags = flags;

    VK_CHECK(vkCreateImage(g_context->gpu.logical_device, &image_info, nullptr, &dest_image->image));

    VkMemoryRequirements mem_requirements = {};
    vkGetImageMemoryRequirements(g_context->gpu.logical_device, dest_image->image, &mem_requirements);

    allocate_gpu_memory(properties, mem_requirements, &dest_image->device_memory);

    vkBindImageMemory(g_context->gpu.logical_device, dest_image->image, dest_image->device_memory, 0);

    dest_image->format;
    dest_image->mip_level_count = mips;
    dest_image->layer_count = layers;
}
    
void init_image_view(VkImage *image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *dest_image_view, VkImageViewType type, uint32_t layers, uint32_t mips)
{
    VkImageViewCreateInfo view_info			= {};
    view_info.sType					= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image					= *image;
    view_info.viewType				= type;
    view_info.format				= format;
    view_info.subresourceRange.aspectMask		= aspect_flags;
    view_info.subresourceRange.baseMipLevel		= 0;
    view_info.subresourceRange.levelCount		= mips;
    view_info.subresourceRange.baseArrayLayer	= 0;
    view_info.subresourceRange.layerCount		= layers;

    VK_CHECK(vkCreateImageView(g_context->gpu.logical_device, &view_info, nullptr, dest_image_view));
}

void init_image_sampler(VkFilter mag_filter, VkFilter min_filter, VkSamplerAddressMode u_sampler_address_mode, VkSamplerAddressMode v_sampler_address_mode, VkSamplerAddressMode w_sampler_address_mode,
                        VkBool32 anisotropy_enable, uint32_t max_anisotropy, VkBorderColor clamp_border_color, VkBool32 compare_enable, VkCompareOp compare_op,
                        VkSamplerMipmapMode mipmap_mode, float32_t mip_lod_bias, float32_t min_lod, float32_t max_lod, VkSampler *dest_sampler)
{
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = mag_filter;
    sampler_info.minFilter = min_filter;
    sampler_info.addressModeU = u_sampler_address_mode;
    sampler_info.addressModeV = v_sampler_address_mode;
    sampler_info.addressModeW = w_sampler_address_mode;
    sampler_info.anisotropyEnable = anisotropy_enable;
    sampler_info.maxAnisotropy = (float32_t)(max_anisotropy);
    sampler_info.borderColor = clamp_border_color; // when clamping
    sampler_info.compareEnable = compare_enable;
    sampler_info.compareOp = compare_op;
    sampler_info.mipmapMode = mipmap_mode;
    sampler_info.mipLodBias = mip_lod_bias;
    sampler_info.minLod = min_lod;
    sampler_info.maxLod = max_lod;

    VK_CHECK(vkCreateSampler(g_context->gpu.logical_device, &sampler_info, nullptr, dest_sampler));
}

void begin_command_buffer(VkCommandBuffer *command_buffer, VkCommandBufferUsageFlags usage_flags, VkCommandBufferInheritanceInfo *inheritance)
{
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = usage_flags;
    begin_info.pInheritanceInfo = inheritance;

    vkBeginCommandBuffer(*command_buffer, &begin_info);
}

void allocate_command_buffers(VkCommandPool *command_pool_source, VkCommandBufferLevel level, const memory_buffer_view_t<VkCommandBuffer> &command_buffers)
{
    VkCommandBufferAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.level = level;
    allocate_info.commandPool = *command_pool_source;
    allocate_info.commandBufferCount = command_buffers.count;

    vkAllocateCommandBuffers(g_context->gpu.logical_device, &allocate_info, command_buffers.buffer);
}
    
void submit(const memory_buffer_view_t<VkCommandBuffer> &command_buffers, const memory_buffer_view_t<VkSemaphore> &wait_semaphores, const memory_buffer_view_t<VkSemaphore> &signal_semaphores, const memory_buffer_view_t<VkPipelineStageFlags> &wait_stages, VkFence *fence, VkQueue *queue)
{
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = command_buffers.count;
    submit_info.pCommandBuffers = command_buffers.buffer;

    submit_info.waitSemaphoreCount = wait_semaphores.count;
    submit_info.pWaitSemaphores = wait_semaphores.buffer;
    submit_info.pWaitDstStageMask = wait_stages.buffer;

    submit_info.signalSemaphoreCount = signal_semaphores.count;
    submit_info.pSignalSemaphores = signal_semaphores.buffer;

    vkQueueSubmit(*queue, 1, &submit_info, *fence);
}

static bool has_stencil_component(VkFormat format)
{
    return(format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT);
}

void init_single_use_command_buffer(VkCommandPool *command_pool, VkCommandBuffer *dst)
{
    allocate_command_buffers(command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, memory_buffer_view_t<VkCommandBuffer>{1, dst});

    begin_command_buffer(dst, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr);
}

void destroy_single_use_command_buffer(VkCommandBuffer *command_buffer, VkCommandPool *command_pool)
{
    end_command_buffer(command_buffer);

    VkFence null_fence = VK_NULL_HANDLE;
    submit(memory_buffer_view_t<VkCommandBuffer>{1, command_buffer}, null_buffer<VkSemaphore>(), null_buffer<VkSemaphore>(), null_buffer<VkPipelineStageFlags>(), &null_fence, &g_context->gpu.graphics_queue);

    vkQueueWaitIdle(g_context->gpu.graphics_queue);

    free_command_buffer(memory_buffer_view_t<VkCommandBuffer>{1, command_buffer}, command_pool);
}

void initialize_image_memory_barrier(VkImageLayout layout_before, VkImageLayout layout_after, image2d_t *image, VkImageAspectFlags image_aspect, uint32_t layer_count, image_memory_barrier_t *dst)
{
    dst->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst->oldLayout = layout_before;
    dst->newLayout = layout_after;
    dst->image = image->image;
    dst->subresourceRange.aspectMask = image_aspect;
    dst->subresourceRange.baseMipLevel = 0;
    dst->subresourceRange.levelCount = 1;
    dst->subresourceRange.baseArrayLayer = 0;
    dst->subresourceRange.layerCount = layer_count;
}

void initialize_buffer_memory_barrier(gpu_buffer_t *buffer, uint32_t offset, uint32_t size, VkAccessFlagBits access_before, VkAccessFlags access_after, buffer_memory_barrier_t *dst)
{
    dst->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    dst->srcAccessMask = access_before;
    dst->dstAccessMask = access_after;
    dst->buffer = buffer->buffer;
    dst->offset = offset;
    dst->size = size;
}

void issue_pipeline_barrier(VkPipelineStageFlags stage_before, VkPipelineStageFlags stage_after, const memory_buffer_view_t<buffer_memory_barrier_t> &buffer_barriers, const memory_buffer_view_t<image_memory_barrier_t> &image_barriers, VkCommandBuffer *cmdbuf)
{
    vkCmdPipelineBarrier(*cmdbuf, stage_after, stage_before, 0, 0, 0, buffer_barriers.count, buffer_barriers.buffer, image_barriers.count, image_barriers.buffer);
}

void transition_image_layout(VkImage *image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, VkCommandPool *graphics_command_pool, uint32_t layer_count, uint32_t mip_count)
{
    VkCommandBuffer single_use;
    init_single_use_command_buffer(graphics_command_pool, &single_use);
	
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   
    barrier.image				= *image;
    barrier.subresourceRange.aspectMask	= VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel	= 0;
    barrier.subresourceRange.levelCount	= mip_count;
    barrier.subresourceRange.baseArrayLayer	= 0;
    barrier.subresourceRange.layerCount	= layer_count;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else
    {
        //OUTPUT_DEBUG_LOG("%s\n", "unsupported layout transition");
        assert(false);
    }

    if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (has_stencil_component(format))
        {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    else
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    vkCmdPipelineBarrier(single_use, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    destroy_single_use_command_buffer(&single_use, graphics_command_pool);
}

void copy_buffer_into_image(gpu_buffer_t *src_buffer, image2d_t *dst_image, uint32_t width, uint32_t height, VkCommandPool *command_pool)
{
    VkCommandBuffer command_buffer;
    init_single_use_command_buffer(command_pool, &command_buffer);

    VkBufferImageCopy region	= {};
    region.bufferOffset		= 0;
    region.bufferRowLength		= 0;
    region.bufferImageHeight	= 0;

    region.imageSubresource.aspectMask	= VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel	= 0;
    region.imageSubresource.baseArrayLayer	= 0;
    region.imageSubresource.layerCount	= 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(command_buffer, src_buffer->buffer, dst_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    destroy_single_use_command_buffer(&command_buffer, command_pool);
}

void copy_buffer(gpu_buffer_t *src_buffer, gpu_buffer_t *dst_buffer, VkCommandPool *command_pool)
{
    VkCommandBuffer command_buffer;
    init_single_use_command_buffer(command_pool, &command_buffer);

    VkBufferCopy region = {};
    region.size = src_buffer->size;
    vkCmdCopyBuffer(command_buffer, src_buffer->buffer, dst_buffer->buffer, 1, &region);

    destroy_single_use_command_buffer(&command_buffer, command_pool);
}

void copy_image(image2d_t *src_image,image2d_t *dst_image,uint32_t width, uint32_t height, VkPipelineStageFlags flags_before, VkPipelineStageFlags flags_after, VkImageLayout layout_before_dst, VkImageLayout layout_before_src, VkCommandBuffer *cmdbuf, uint32_t dest_layer, uint32_t dest_mip)
{
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = layout_before_src;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.image = src_image->image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = src_image->mip_level_count;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(*cmdbuf, flags_before, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    image_barrier.image = dst_image->image;
    image_barrier.oldLayout = layout_before_dst;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.subresourceRange.levelCount = dst_image->mip_level_count;
    image_barrier.subresourceRange.layerCount = dst_image->layer_count;

    // Just perform layout transition
    vkCmdPipelineBarrier(*cmdbuf, flags_before, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    VkImageCopy image_copy = {};
    image_copy.srcSubresource.mipLevel = 0;
    image_copy.srcSubresource.layerCount = 1;
    image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.srcSubresource.baseArrayLayer = 0;
    image_copy.srcOffset.x = 0;
    image_copy.srcOffset.y = 0;
    image_copy.srcOffset.z = 0;
    
    image_copy.dstSubresource.mipLevel = dest_mip;
    image_copy.dstSubresource.layerCount = 1;
    image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.dstSubresource.baseArrayLayer = dest_layer;
    image_copy.dstOffset.x = 0;
    image_copy.dstOffset.y = 0;
    image_copy.dstOffset.z = 0;

    image_copy.extent.width = width;
    image_copy.extent.height = height;
    image_copy.extent.depth = 1;
    
    vkCmdCopyImage(*cmdbuf, src_image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);

    image_barrier.image = src_image->image;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.newLayout = layout_before_src;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(*cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, flags_after, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    image_barrier.image = dst_image->image;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.newLayout = layout_before_dst;
    image_barrier.subresourceRange.levelCount = dst_image->mip_level_count;
    image_barrier.subresourceRange.layerCount = dst_image->layer_count;
    vkCmdPipelineBarrier(*cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, flags_after, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);
}

void blit_image(image2d_t *src_image, image2d_t *dst_image, uint32_t width, uint32_t height, VkPipelineStageFlags flags_before, VkPipelineStageFlags flags_after, VkImageLayout layout_before, VkCommandBuffer *cmdbuf)
{
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = layout_before;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.image = src_image->image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(*cmdbuf, flags_before, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    image_barrier.image = dst_image->image;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    // Just perform layout transition
    vkCmdPipelineBarrier(*cmdbuf, flags_before, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    VkImageBlit image_blit = {};
    image_blit.srcSubresource.mipLevel = 0;
    image_blit.srcSubresource.layerCount = 1;
    image_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_blit.srcSubresource.baseArrayLayer = 0;
    image_blit.srcOffsets[0].x = 0;
    image_blit.srcOffsets[0].y = 0;
    image_blit.srcOffsets[0].z = 0;
    image_blit.srcOffsets[1].x = width;
    image_blit.srcOffsets[1].y = height;
    image_blit.srcOffsets[1].z = 1;
    
    image_blit.dstSubresource.mipLevel = 0;
    image_blit.dstSubresource.layerCount = 1;
    image_blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_blit.dstSubresource.baseArrayLayer = 0;
    image_blit.dstOffsets[0].x = 0;
    image_blit.dstOffsets[0].y = 0;
    image_blit.dstOffsets[0].z = 0;
    image_blit.dstOffsets[1].x = width;
    image_blit.dstOffsets[1].y = height;
    image_blit.dstOffsets[1].z = 1;
    
    vkCmdBlitImage(*cmdbuf, src_image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_blit, VK_FILTER_NEAREST);

    image_barrier.image = src_image->image;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.newLayout = layout_before;
    image_barrier.subresourceRange.levelCount = 1;
    vkCmdPipelineBarrier(*cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, flags_after, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

    image_barrier.image = dst_image->image;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.newLayout = layout_before;
    vkCmdPipelineBarrier(*cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, flags_after, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);
}

void invoke_staging_buffer_for_device_local_image(memory_byte_buffer_t items, VkCommandPool *transfer_command_pool, image2d_t *dst_image, int32_t width, int32_t height)
{
    VkDeviceSize staging_buffer_size = items.size;
	
    gpu_buffer_t staging_buffer;
    staging_buffer.size = staging_buffer_size;

    init_buffer(staging_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging_buffer);

    mapped_gpu_memory_t mapped_memory = staging_buffer.construct_map();
    mapped_memory.begin();
    mapped_memory.fill(items);
    mapped_memory.end();

    copy_buffer_into_image(&staging_buffer, dst_image, width, height, transfer_command_pool);

    vkDestroyBuffer(g_context->gpu.logical_device, staging_buffer.buffer, nullptr);
    vkFreeMemory(g_context->gpu.logical_device, staging_buffer.memory, nullptr);    
}

void invoke_staging_buffer_for_device_local_buffer(memory_byte_buffer_t items, VkBufferUsageFlags usage, VkCommandPool *transfer_command_pool, gpu_buffer_t *dst_buffer)
{
    VkDeviceSize buffer_size = items.size;
	
    gpu_buffer_t staging_buffer;
    staging_buffer.size = buffer_size;

    init_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging_buffer);

    mapped_gpu_memory_t mapped_memory = staging_buffer.construct_map();
    mapped_memory.begin();
    mapped_memory.fill(items);
    mapped_memory.end();

    init_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | usage, VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dst_buffer);

    copy_buffer(&staging_buffer, dst_buffer, transfer_command_pool);

    vkDestroyBuffer(g_context->gpu.logical_device, staging_buffer.buffer, nullptr);
    vkFreeMemory(g_context->gpu.logical_device, staging_buffer.memory, nullptr);
}
    
static void init_swapchain(raw_input_t *raw_input)
{
    swapchain_details_t *swapchain_details = &g_context->gpu.swapchain_support;
    VkSurfaceFormatKHR surface_format = choose_surface_format(swapchain_details->available_formats, swapchain_details->available_formats_count);
    VkExtent2D surface_extent = choose_swapchain_extent(raw_input, &swapchain_details->capabilities);
    VkPresentModeKHR present_mode = choose_surface_present_mode(swapchain_details->available_present_modes, swapchain_details->available_present_modes_count);

    // add 1 to the minimum images supported in the swapchain
    uint32_t image_count = swapchain_details->capabilities.minImageCount + 1;
    if (image_count > swapchain_details->capabilities.maxImageCount)
    {
        image_count = swapchain_details->capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = g_context->surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = surface_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queue_family_indices[] = { (uint32_t)g_context->gpu.queue_families.graphics_family, (uint32_t)g_context->gpu.queue_families.present_family };

    if (g_context->gpu.queue_families.graphics_family != g_context->gpu.queue_families.present_family)
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

    VK_CHECK(vkCreateSwapchainKHR(g_context->gpu.logical_device, &swapchain_info, nullptr, &g_context->swapchain.swapchain));

    vkGetSwapchainImagesKHR(g_context->gpu.logical_device, g_context->swapchain.swapchain, &image_count, nullptr);

    allocate_memory_buffer(g_context->swapchain.imgs, image_count);
	
    vkGetSwapchainImagesKHR(g_context->gpu.logical_device, g_context->swapchain.swapchain, &image_count, g_context->swapchain.imgs.buffer);
	
    g_context->swapchain.extent = surface_extent;
    g_context->swapchain.format = surface_format.format;
    g_context->swapchain.present_mode = present_mode;

    allocate_memory_buffer(g_context->swapchain.views, image_count);
	
    for (uint32_t i = 0
             ; i < image_count
             ; ++i)
    {
        VkImage *image = &g_context->swapchain.imgs[i];

        init_image_view(image, g_context->swapchain.format, VK_IMAGE_ASPECT_COLOR_BIT, &g_context->swapchain.views[i], VK_IMAGE_VIEW_TYPE_2D, 1, 1);
    }
}
    
void init_render_pass(const memory_buffer_view_t<VkAttachmentDescription> &attachment_descriptions, const memory_buffer_view_t<VkSubpassDescription> &subpass_descriptions, const memory_buffer_view_t<VkSubpassDependency> &subpass_dependencies, render_pass_t *dest_render_pass)
{
    VkRenderPassCreateInfo render_pass_info	= {};
    render_pass_info.sType			= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount	= attachment_descriptions.count;
    render_pass_info.pAttachments		= attachment_descriptions.buffer;
    render_pass_info.subpassCount		= subpass_descriptions.count;
    render_pass_info.pSubpasses		= subpass_descriptions.buffer;
    render_pass_info.dependencyCount	= subpass_dependencies.count;
    render_pass_info.pDependencies		= subpass_dependencies.buffer;

    VK_CHECK(vkCreateRenderPass(g_context->gpu.logical_device, &render_pass_info, nullptr, &dest_render_pass->render_pass));
    dest_render_pass->subpass_count = subpass_descriptions.count;
}

// find gpu supported depth format
static VkFormat find_supported_format(const VkFormat *candidates, uint32_t candidate_size, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (uint32_t i = 0
             ; i < candidate_size
             ; ++i)
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(g_context->gpu.hardware, candidates[i], &properties);
        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
        {
            return(candidates[i]);
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
        {
            return(candidates[i]);
        }
    }
    //OUTPUT_DEBUG_LOG("%s\n", "failed to find supported format");
    assert(false);

    return VkFormat{};
}

static void find_depth_format(void)
{
    VkFormat formats[] = 
        {
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT
        };
    
    g_context->gpu.supported_depth_format = find_supported_format(formats, 3, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
    
void init_shader(VkShaderStageFlagBits stage_bits, uint32_t content_size, byte_t *file_contents, VkShaderModule *dest_shader_module)
{
    VkShaderModuleCreateInfo shader_info = {};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = content_size;
    shader_info.pCode = reinterpret_cast<const uint32_t *>(file_contents);

    VK_CHECK(vkCreateShaderModule(g_context->gpu.logical_device, &shader_info, nullptr, dest_shader_module));
}

void init_pipeline_layout(const memory_buffer_view_t<VkDescriptorSetLayout> &layouts, const memory_buffer_view_t<VkPushConstantRange> &ranges, VkPipelineLayout *pipeline_layout)
{
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = layouts.count;
    layout_info.pSetLayouts = layouts.buffer;
    layout_info.pushConstantRangeCount = ranges.count;
    layout_info.pPushConstantRanges = ranges.buffer;

    VK_CHECK(vkCreatePipelineLayout(g_context->gpu.logical_device, &layout_info, nullptr, pipeline_layout));        
}
    
void init_pipeline_layout(memory_buffer_view_t<VkDescriptorSetLayout> *layouts, memory_buffer_view_t<VkPushConstantRange> *ranges, VkPipelineLayout *pipeline_layout)
{
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = layouts->count;
    layout_info.pSetLayouts = layouts->buffer;
    layout_info.pushConstantRangeCount = ranges->count;
    layout_info.pPushConstantRanges = ranges->buffer;

    VK_CHECK(vkCreatePipelineLayout(g_context->gpu.logical_device, &layout_info, nullptr, pipeline_layout));
}

void
init_graphics_pipeline(memory_buffer_view_t<VkPipelineShaderStageCreateInfo> *shaders,
                       VkPipelineVertexInputStateCreateInfo *vertex_input_info,
                       VkPipelineInputAssemblyStateCreateInfo *input_assembly_info,
                       VkPipelineViewportStateCreateInfo *viewport_info,
                       VkPipelineRasterizationStateCreateInfo *rasterization_info,
                       VkPipelineMultisampleStateCreateInfo *multisample_info,
                       VkPipelineColorBlendStateCreateInfo *blend_info,
                       VkPipelineDynamicStateCreateInfo *dynamic_state_info,
                       VkPipelineDepthStencilStateCreateInfo *depth_stencil_info,
                       VkPipelineLayout *pipeline_layout,
                       render_pass_t *render_pass,
                       uint32_t subpass,
                       VkPipeline *pipeline)
{
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = shaders->count;
    pipeline_info.pStages = shaders->buffer;
    pipeline_info.pVertexInputState = vertex_input_info;
    pipeline_info.pInputAssemblyState = input_assembly_info;
    pipeline_info.pViewportState = viewport_info;
    pipeline_info.pRasterizationState = rasterization_info;
    pipeline_info.pMultisampleState = multisample_info;
    pipeline_info.pDepthStencilState = depth_stencil_info;
    pipeline_info.pColorBlendState = blend_info;
    pipeline_info.pDynamicState = dynamic_state_info;

    pipeline_info.layout = *pipeline_layout;
    pipeline_info.renderPass = render_pass->render_pass;
    pipeline_info.subpass = subpass;

    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    VK_CHECK (vkCreateGraphicsPipelines(g_context->gpu.logical_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, pipeline) );
}

void allocate_command_pool(uint32_t queue_family_index, VkCommandPool *command_pool)
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType			= VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex	= queue_family_index;
    pool_info.flags			= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(g_context->gpu.logical_device, &pool_info, nullptr, command_pool));
}

void init_framebuffer_attachment(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, image2d_t *attachment, uint32_t layers, VkImageCreateFlags create_flags, VkImageViewType image_view_type)
{
    init_image(width, height, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, layers, attachment, create_flags);

    VkImageAspectFlags aspect_flags;
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;
	
    init_image_view(&attachment->image, format, aspect_flags, &attachment->image_view, image_view_type, layers, 1);
}
    
void init_framebuffer(render_pass_t *compatible_render_pass, uint32_t width, uint32_t height, uint32_t layer_count, framebuffer_t *framebuffer)
{
    memory_buffer_view_t<VkImageView> image_view_attachments;
	
    image_view_attachments.count = framebuffer->color_attachments.count;
    image_view_attachments.buffer = (VkImageView *)allocate_stack(sizeof(VkImageView) * image_view_attachments.count);
	
    for (uint32_t i = 0
             ; i < image_view_attachments.count
             ; ++i)
    {
        VkImageView *image = &framebuffer->color_attachments.buffer[i];
        image_view_attachments.buffer[i] = *image;
    }

    if (framebuffer->depth_attachment != VK_NULL_HANDLE)
    {
        VkImageView *depth_image = &framebuffer->depth_attachment;
        extend_stack_top(sizeof(VkImageView));
        image_view_attachments.buffer[image_view_attachments.count++] = *depth_image;
    }
	
    VkFramebufferCreateInfo fbo_info	= {};
    fbo_info.sType				= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbo_info.renderPass			= compatible_render_pass->render_pass;
    fbo_info.attachmentCount		= image_view_attachments.count;
    fbo_info.pAttachments			= image_view_attachments.buffer;
    fbo_info.width				= width;
    fbo_info.height				= height;
    fbo_info.layers				= layer_count;

    VK_CHECK(vkCreateFramebuffer(g_context->gpu.logical_device, &fbo_info, nullptr, &framebuffer->framebuffer));
}

VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout *layout, VkDescriptorPool *descriptor_pool)
{
    VkDescriptorSet result;
	
    VkDescriptorSetAllocateInfo alloc_info	= {};
    alloc_info.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool	= *descriptor_pool;
    alloc_info.descriptorSetCount	= 1;
    alloc_info.pSetLayouts		= layout;

    VK_CHECK(vkAllocateDescriptorSets(g_context->gpu.logical_device, &alloc_info, &result));

    return(result);
}
    
void allocate_descriptor_sets(memory_buffer_view_t<VkDescriptorSet> &descriptor_sets, const memory_buffer_view_t<VkDescriptorSetLayout> &layouts, VkDescriptorPool *descriptor_pool)
{
    VkDescriptorSetAllocateInfo alloc_info	= {};
    alloc_info.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool	= *descriptor_pool;
    alloc_info.descriptorSetCount	= layouts.count;
    alloc_info.pSetLayouts		= layouts.buffer;

    VK_CHECK(vkAllocateDescriptorSets(g_context->gpu.logical_device, &alloc_info, descriptor_sets.buffer));
}

void init_descriptor_pool(const memory_buffer_view_t<VkDescriptorPoolSize> &sizes, uint32_t max_sets, VkDescriptorPool *pool)
{
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = sizes.count;
    pool_info.pPoolSizes = sizes.buffer;

    pool_info.maxSets = max_sets;

    VK_CHECK(vkCreateDescriptorPool(g_context->gpu.logical_device, &pool_info, nullptr, pool));
}
    
void init_descriptor_pool(const memory_buffer_view_t<VkDescriptorPoolSize> &sizes, uint32_t max_sets, descriptor_pool_t *pool)
{
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = sizes.count;
    pool_info.pPoolSizes = sizes.buffer;

    pool_info.maxSets = max_sets;

    VK_CHECK(vkCreateDescriptorPool(g_context->gpu.logical_device, &pool_info, nullptr, &pool->pool));
}

void init_descriptor_set_layout(const memory_buffer_view_t<VkDescriptorSetLayoutBinding> &bindings, VkDescriptorSetLayout *dst)
{
    VkDescriptorSetLayoutCreateInfo layout_info	= {};
    layout_info.sType				= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount			= bindings.count;
    layout_info.pBindings				= bindings.buffer;
	
    VK_CHECK(vkCreateDescriptorSetLayout(g_context->gpu.logical_device, &layout_info, nullptr, dst));
}
    
void update_descriptor_sets(const memory_buffer_view_t<VkWriteDescriptorSet> &writes)
{
    vkUpdateDescriptorSets(g_context->gpu.logical_device, writes.count, writes.buffer, 0, nullptr);
}
    
void init_buffer(VkDeviceSize buffer_size, VkBufferUsageFlags usage, VkSharingMode sharing_mode, VkMemoryPropertyFlags memory_properties, gpu_buffer_t *dest_buffer)
{
    dest_buffer->size = buffer_size;
	
    VkBufferCreateInfo buffer_info	= {};
    buffer_info.sType	= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size	= buffer_size;
    buffer_info.usage	= usage;
    buffer_info.sharingMode	= sharing_mode;
    buffer_info.flags	= 0;

    VK_CHECK(vkCreateBuffer(g_context->gpu.logical_device, &buffer_info, nullptr, &dest_buffer->buffer));

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(g_context->gpu.logical_device, dest_buffer->buffer, &mem_requirements);

    allocate_gpu_memory(memory_properties, mem_requirements, &dest_buffer->memory);
	
    vkBindBufferMemory(g_context->gpu.logical_device, dest_buffer->buffer, dest_buffer->memory, 0);
}

void update_gpu_buffer(gpu_buffer_t *dst, void *data, uint32_t size, uint32_t offset, VkPipelineStageFlags stage, VkAccessFlags access, VkCommandBuffer *queue)
{
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = access;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.buffer = dst->buffer;
    barrier.offset = offset;
    barrier.size = size;
    vkCmdPipelineBarrier(*queue,stage,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);

    vkCmdUpdateBuffer(*queue, dst->buffer, offset, size, data);
    
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = access;
    vkCmdPipelineBarrier(*queue, VK_PIPELINE_STAGE_TRANSFER_BIT, stage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void command_buffer_begin_render_pass(render_pass_t *render_pass, framebuffer_t *fbo, VkRect2D render_area, const memory_buffer_view_t<VkClearValue> &clear_colors, VkSubpassContents subpass_contents, VkCommandBuffer *command_buffer)
{
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.pNext = nullptr;
    render_pass_info.renderPass = render_pass->render_pass;

    render_pass_info.framebuffer = fbo->framebuffer;
    render_pass_info.renderArea = render_area;

    render_pass_info.clearValueCount = clear_colors.count;
    render_pass_info.pClearValues = clear_colors.buffer;

    vkCmdBeginRenderPass(*command_buffer, &render_pass_info, subpass_contents);
}

void command_buffer_next_subpass(VkCommandBuffer *cmdbuf, VkSubpassContents contents)
{
    vkCmdNextSubpass(*cmdbuf, contents);
}

VkResult present(const memory_buffer_view_t<VkSemaphore> &signal_semaphores, uint32_t *image_index, VkQueue *present_queue)
{
    memory_buffer_view_t<VkSwapchainKHR> swapchains{1, &g_context->swapchain.swapchain};
    
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = signal_semaphores.count;
    present_info.pWaitSemaphores = signal_semaphores.buffer;

    present_info.swapchainCount = swapchains.count;
    present_info.pSwapchains = swapchains.buffer;
    present_info.pImageIndices = image_index;

    return(vkQueuePresentKHR(*present_queue, &present_info));
}
    
graphics_api_initialize_ret_t initialize_graphics_api(create_vulkan_surface *create_surface_proc, raw_input_t *raw_input)
{
    // initialize instance
    static constexpr uint32_t layer_count = 1;
    const char *layer_names[layer_count] = { "VK_LAYER_LUNARG_standard_validation" };

    instance_create_validation_layer_params_t validation_params = {};
    validation_params.r_enable = true;
    validation_params.o_layer_count = layer_count;
    validation_params.o_layer_names = layer_names;

    uint32_t extension_count;
    const char **total_extension_buffer;
#if defined (_WIN32)
    // TODO: TODO: TODO: MAKE A BETTER WAY TO SELECT FRICKING EXTENSIONS
    total_extension_buffer = (const char **)allocate_stack(sizeof(const char *) * 4);
    total_extension_buffer[0] = "VK_KHR_win32_surface";
    total_extension_buffer[1] = "VK_KHR_surface";
    total_extension_buffer[2] = "VK_EXT_debug_utils";
    total_extension_buffer[3] = "VK_EXT_debug_report";
    extension_count = 4;
#else
    const char **extension_names = glfwGetRequiredInstanceExtensions(&extension_count);
    total_extension_buffer = (const char **)allocate_stack(sizeof(const char *) * (extension_count + 4), alignment_t(1), "vulkan_instanc_extension_names_list_allocation");
    memcpy(total_extension_buffer, extension_names, sizeof(const char *) * extension_count);
    total_extension_buffer[extension_count++] = "VK_EXT_debug_utils";
    total_extension_buffer[extension_count++] = "VK_EXT_debug_report";
#endif
	
    instance_create_extension_params_t extension_params = {};
    extension_params.r_extension_count = extension_count;
    extension_params.r_extension_names = total_extension_buffer;

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "vulkan engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
	
    init_instance(&app_info, &validation_params, &extension_params);
	
    pop_stack();

    init_debug_messenger();

    // create the surface
    create_surface_proc->instance = &g_context->instance;
    create_surface_proc->surface = &g_context->surface;
    VK_CHECK(create_surface_proc->create_proc());

    // choose hardware and create device
    static constexpr uint32_t gpu_extension_count = 1;
    const char *gpu_extension_names[gpu_extension_count] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    physical_device_extensions_params_t gpu_extensions = {};
    gpu_extensions.r_extension_count = gpu_extension_count;
    gpu_extensions.r_extension_names = gpu_extension_names;
    choose_gpu(&gpu_extensions);
    vkGetPhysicalDeviceMemoryProperties(g_context->gpu.hardware, &g_context->gpu.memory_properties);
    find_depth_format();
    init_device(&gpu_extensions, &validation_params);

    // create swapchain
    init_swapchain(raw_input);

    allocate_command_pool(g_context->gpu.queue_families.graphics_family, &g_context->command_pool);

    allocate_command_buffers(&g_context->command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, memory_buffer_view_t<VkCommandBuffer>{3, g_context->command_buffer});

    for (uint32_t i = 0; i < 2; ++i)
    {
        init_semaphore(&g_context->img_ready[i]);
        init_semaphore(&g_context->render_finish[i]);   
        init_fence(VK_FENCE_CREATE_SIGNALED_BIT, &g_context->cpu_wait[i]);
    }
    
    return graphics_api_initialize_ret_t{ &g_context->command_pool };
}

void recreate_swapchain(raw_input_t *raw_input)
{
    destroy_swapchain();

    get_swapchain_support(&g_context->surface);
    
    init_swapchain(raw_input);
}

frame_rendering_data_t begin_frame_rendering(raw_input_t *raw_input)
{
    static uint32_t current_frame = 0;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    g_context->current_frame = 0;
    
    VkFence null_fence = VK_NULL_HANDLE;
    
    auto next_image_data = acquire_next_image(&g_context->img_ready[g_context->current_frame], &null_fence);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        //recreate_swapchain(raw_input);
	return {};
    }
    else if (next_image_data.result != VK_SUCCESS && next_image_data.result != VK_SUBOPTIMAL_KHR)
    {
	//OUTPUT_DEBUG_LOG("Failed to acquire swapchain image");
    }
    
    wait_fences(memory_buffer_view_t<VkFence>{1, &g_context->cpu_wait[g_context->current_frame]});
    reset_fences({1, &g_context->cpu_wait[g_context->current_frame]});

    g_context->image_index = next_image_data.image_index;
    
    begin_command_buffer(&g_context->command_buffer[g_context->current_frame], 0, nullptr);

    return {g_context->image_index, g_context->command_buffer[g_context->current_frame]};
}

void end_frame_rendering_and_refresh(raw_input_t *raw_input)
{
    end_command_buffer(&g_context->command_buffer[g_context->current_frame]);

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
        //recreate_swapchain(raw_input);
    }
    else if (result != VK_SUCCESS)
    {
	//OUTPUT_DEBUG_LOG("%s\n", "failed to present swapchain image");
    }
}

void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *allocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, messenger, allocator);
    }
}
    
void destroy_pipeline(VkPipeline *pipeline)
{
    vkDestroyPipeline(g_context->gpu.logical_device, *pipeline, nullptr);
}

void
destroy_vulkan_state(void)
{
    free_command_buffer(memory_buffer_view_t<VkCommandBuffer>{3, g_context->command_buffer}, &g_context->command_pool);

    /*    vkDestroyCommandPool(gpu->logical_device, window_rendering.command_pool, nullptr);
          vkDestroyFence(gpu->logical_device, window_rendering.cpu_wait[0], nullptr);
          vkDestroyFence(gpu->logical_device, window_rendering.cpu_wait[1], nullptr);
    
          vkDestroySemaphore(gpu->logical_device, window_rendering.render_finish[0], nullptr);
          vkDestroySemaphore(gpu->logical_device, window_rendering.img_ready[0], nullptr);

          vkDestroySemaphore(gpu->logical_device, window_rendering.render_finish[1], nullptr);
          vkDestroySemaphore(gpu->logical_device, window_rendering.img_ready[1], nullptr);*/
    
    vkDestroyDevice(g_context->gpu.logical_device, nullptr);
	
    vkDestroySurfaceKHR(g_context->instance, g_context->surface, nullptr);
	
    destroy_debug_utils_messenger_ext(g_context->instance, g_context->debug_messenger, nullptr);
    vkDestroyInstance(g_context->instance, nullptr);
}

void idle_gpu(void)
{
    vkDeviceWaitIdle(g_context->gpu.logical_device);
}

void destroy_framebuffer(VkFramebuffer *fbo)
{
    vkDestroyFramebuffer(g_context->gpu.logical_device, *fbo, nullptr);
}

void destroy_render_pass(VkRenderPass *render_pass)
{
    vkDestroyRenderPass(g_context->gpu.logical_device, *render_pass, nullptr);
}

void destroy_image_view(VkImageView *image_view)
{
    vkDestroyImageView(g_context->gpu.logical_device, *image_view, nullptr);
}

void destroy_swapchain(void)
{
    for (uint32_t i = 0; i < g_context->swapchain.views.count; ++i)
    {
        vkDestroyImageView(g_context->gpu.logical_device, g_context->swapchain.views[i], nullptr);
    }
	
    vkDestroySwapchainKHR(g_context->gpu.logical_device, g_context->swapchain.swapchain, nullptr);
}

void initialize_vulkan_translation_unit(game_memory_t *memory)
{
    g_context = &memory->graphics_context.context;
}

bool is_graphics_api_initialized(void)
{
    return g_context != nullptr;
}
