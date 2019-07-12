#pragma once

#include <limits.h>
#include <memory.h>
#include "core.hpp"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

struct queue_families_t
{
    int32_t graphics_family = -1;
    int32_t present_family = 1;

    inline bool
    complete(void)
    {
        return(graphics_family >= 0 && present_family >= 0);
    }
};

struct swapchain_details_t
{
    VkSurfaceCapabilitiesKHR capabilities;

    uint32_t available_formats_count;
    VkSurfaceFormatKHR *available_formats;
	
    uint32_t available_present_modes_count;
    VkPresentModeKHR *available_present_modes;
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

    void
    find_queue_families(VkSurfaceKHR *surface);
};

// allocates memory for stuff like buffers and images
void
allocate_gpu_memory(VkMemoryPropertyFlags properties
                    , VkMemoryRequirements memory_requirements
                    , gpu_t *gpu
                    , VkDeviceMemory *dest_memory);

struct mapped_gpu_memory_t
{
    uint32_t offset;
    VkDeviceSize size;
    VkDeviceMemory *memory;
    void *data;

    inline void
    begin(gpu_t *gpu)
    {
        vkMapMemory(gpu->logical_device, *memory, offset, size, 0, &data);
    }

    inline void
    fill(memory_byte_buffer_t byte_buffer)
    {
        memcpy(data, byte_buffer.ptr, size);
    }

    inline void
    flush(uint32_t offset, uint32_t size, gpu_t *gpu)
    {
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = *memory;
        range.offset = offset;
        range.size = size;
        vkFlushMappedMemoryRanges(gpu->logical_device, 1, &range);
    }
	
    inline void
    end(gpu_t *gpu)
    {
        vkUnmapMemory(gpu->logical_device, *memory);
    }
};

struct gpu_buffer_t
{
    VkBuffer buffer;
	
    VkDeviceMemory memory;
    VkDeviceSize size;

    inline mapped_gpu_memory_t
    construct_map(void)
    {
        return(mapped_gpu_memory_t{0, size, &memory});
    }

    inline VkDescriptorBufferInfo
    make_descriptor_info(uint32_t offset_into_buffer)
    {
        VkDescriptorBufferInfo info = {};
        info.buffer = buffer;
        info.offset = offset_into_buffer; // for the moment is 0
        info.range = size;
        return(info);
    }

    void
    destroy(gpu_t *gpu)
    {
        vkDestroyBuffer(gpu->logical_device, buffer, nullptr);
        vkFreeMemory(gpu->logical_device, memory, nullptr);
    }
};

void
update_gpu_buffer(gpu_buffer_t *dst, void *data, uint32_t size, uint32_t offset, VkPipelineStageFlags stage, VkAccessFlags access, VkCommandBuffer *queue);

struct draw_indexed_data_t
{
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t vertex_offset;
    uint32_t first_instance;
};

internal inline draw_indexed_data_t
init_draw_indexed_data_default(uint32_t instance_count, uint32_t index_count)
{
    draw_indexed_data_t index_data = {};
    index_data.index_count = index_count;
    index_data.instance_count = instance_count;
    index_data.first_index = 0;
    index_data.vertex_offset = 0;
    index_data.first_instance = 0;
    return(index_data);
}
    
struct model_index_data_t
{
    VkBuffer index_buffer;
	
    uint32_t index_count;
    uint32_t index_offset;
    VkIndexType index_type;

    draw_indexed_data_t
    init_draw_indexed_data(uint32_t first_index
                           , uint32_t offset)
    {
        draw_indexed_data_t data;
        data.index_count = index_count;
        // default the instance_count to 0
        data.instance_count = 1;
        data.first_index = 0;
        data.vertex_offset = offset;
        data.first_instance = 0;

        return(data);
    }
};
    
internal inline void
command_buffer_bind_ibo(const model_index_data_t &index_data
                        , VkCommandBuffer *command_buffer)
{
    vkCmdBindIndexBuffer(*command_buffer
                         , index_data.index_buffer
                         , index_data.index_offset
                         , index_data.index_type);
}
    
internal inline void
command_buffer_bind_vbos(const memory_buffer_view_t<VkBuffer> &buffers
                         , const memory_buffer_view_t<VkDeviceSize> &offsets
                         , uint32_t first_binding, uint32_t binding_count
                         , VkCommandBuffer *command_buffer)
{
    vkCmdBindVertexBuffers(*command_buffer
                           , first_binding
                           , binding_count
                           , buffers.buffer
                           , offsets.buffer);
}

internal inline void
command_buffer_execute_commands(VkCommandBuffer *cmdbuf
                                , const memory_buffer_view_t<VkCommandBuffer> &cmds)
{
    vkCmdExecuteCommands(*cmdbuf
                         , cmds.count
                         , cmds.buffer);
}

    
struct graphics_pipeline_t
{
    enum shader_stages_bits_t : int32_t { VERTEX_SHADER_BIT = 1 << 0
                                    , GEOMETRY_SHADER_BIT = 1 << 1
                                    , TESSELATION_SHADER_BIT = 1 << 2
                                    , FRAGMENT_SHADER_BIT = 1 << 3};

    int32_t stages;

    const char *base_dir_and_name;

    VkPipelineLayout layout;

    VkPipeline pipeline;

    inline void
    destroy(gpu_t *gpu)
    {
        vkDestroyPipelineLayout(gpu->logical_device, layout, nullptr);
        vkDestroyPipeline(gpu->logical_device, pipeline, nullptr);
    }
};
    
internal inline void
command_buffer_push_constant(void *data
                             , uint32_t size
                             , uint32_t offset
                             , VkShaderStageFlags stage
                             , graphics_pipeline_t *ppln
                             , VkCommandBuffer *command_buffer)
{
    vkCmdPushConstants(*command_buffer
                       , ppln->layout
                       , stage
                       , offset
                       , size
                       , data);
}

void
init_buffer(VkDeviceSize buffer_size
            , VkBufferUsageFlags usage
            , VkSharingMode sharing_mode
            , VkMemoryPropertyFlags memory_properties
            , gpu_t *gpu
            , gpu_buffer_t *dest_buffer);

    
struct image2d_t
{
    VkImage image			= VK_NULL_HANDLE;
    VkImageView image_view		= VK_NULL_HANDLE;
    VkSampler image_sampler		= VK_NULL_HANDLE;
    VkDeviceMemory device_memory	= VK_NULL_HANDLE;

    VkFormat format;
	
    inline VkMemoryRequirements
    get_memory_requirements(gpu_t *gpu)
    {
        VkMemoryRequirements requirements = {};
        vkGetImageMemoryRequirements(gpu->logical_device, image, &requirements);

        return(requirements);
    }

    inline VkDescriptorImageInfo
    make_descriptor_info(VkImageLayout expected_layout)
    {
        VkDescriptorImageInfo info = {};
        info.imageLayout = expected_layout;
        info.imageView = image_view;
        info.sampler = image_sampler;
        return(info);
    }

    inline void
    destroy(gpu_t *gpu)
    {
        if (image != VK_NULL_HANDLE) vkDestroyImage(gpu->logical_device, image, nullptr);
        if (image_view != VK_NULL_HANDLE) vkDestroyImageView(gpu->logical_device, image_view, nullptr);
        if (image_sampler != VK_NULL_HANDLE) vkDestroySampler(gpu->logical_device, image_sampler, nullptr);
        if (device_memory != VK_NULL_HANDLE) vkFreeMemory(gpu->logical_device, device_memory, nullptr);
    }
}; 
    
void
init_image(uint32_t width
           , uint32_t height
           , VkFormat format
           , VkImageTiling tiling
           , VkImageUsageFlags usage
           , VkMemoryPropertyFlags properties
           , uint32_t layers
           , gpu_t *gpu
           , image2d_t *dest_image
           , VkImageCreateFlags flags = 0);

void
init_image_view(VkImage *image
                , VkFormat format
                , VkImageAspectFlags aspect_flags
                , gpu_t *gpu
                , VkImageView *dest_image_view
                , VkImageViewType type
                , uint32_t layers);

void
init_image_sampler(VkFilter mag_filter
                   , VkFilter min_filter
                   , VkSamplerAddressMode u_sampler_address_mode
                   , VkSamplerAddressMode v_sampler_address_mode
                   , VkSamplerAddressMode w_sampler_address_mode
                   , VkBool32 anisotropy_enable
                   , uint32_t max_anisotropy
                   , VkBorderColor clamp_border_color
                   , VkBool32 compare_enable
                   , VkCompareOp compare_op
                   , VkSamplerMipmapMode mipmap_mode
                   , float32_t mip_lod_bias
                   , float32_t min_lod
                   , float32_t max_lod
                   , gpu_t *gpu
                   , VkSampler *dest_sampler);

struct swapchain_t
{
    VkFormat format;
    VkPresentModeKHR present_mode;
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
	
    memory_buffer_view_t<VkImage> imgs;
    memory_buffer_view_t<VkImageView> views;
};
    
struct render_pass_t
{
    VkRenderPass render_pass;
    uint32_t subpass_count;

    inline void
    destroy(gpu_t *gpu)
    {
        vkDestroyRenderPass(gpu->logical_device, render_pass, nullptr);
    }
};

internal inline VkAttachmentDescription
init_attachment_description(VkFormat format
                            , VkSampleCountFlagBits samples
                            , VkAttachmentLoadOp load, VkAttachmentStoreOp store
                            , VkAttachmentLoadOp stencil_load, VkAttachmentStoreOp stencil_store
                            , VkImageLayout initial_layout, VkImageLayout final_layout)
{
    VkAttachmentDescription desc = {};
    desc.format		= format;
    desc.samples		= samples;
    desc.loadOp		= load;
    desc.storeOp		= store;
    desc.stencilLoadOp	= stencil_load;
    desc.stencilStoreOp	= stencil_store;
    desc.initialLayout	= initial_layout;
    desc.finalLayout	= final_layout;
    return(desc);
}

internal inline VkAttachmentReference
init_attachment_reference(uint32_t attachment
                          , VkImageLayout layout)
{
    VkAttachmentReference ref = {};
    ref.attachment = attachment;
    ref.layout = layout;
    return(ref);
}

internal inline VkSubpassDescription
init_subpass_description(const memory_buffer_view_t<VkAttachmentReference> &color_refs
                         , VkAttachmentReference *depth
                         , const memory_buffer_view_t<VkAttachmentReference> &input_refs)
{
    VkSubpassDescription subpass	= {};
    subpass.pipelineBindPoint	= VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount	= color_refs.count;
    subpass.pColorAttachments	= color_refs.buffer;
    subpass.pDepthStencilAttachment	= depth;
    subpass.inputAttachmentCount	= input_refs.count;
    subpass.pInputAttachments	= input_refs.buffer;
    return(subpass);
}

internal inline VkSubpassDependency
init_subpass_dependency(uint32_t src_subpass
                        , uint32_t dst_subpass
                        , VkPipelineStageFlags src_stage
                        , uint32_t src_access_mask
                        , VkPipelineStageFlags dst_stage
                        , uint32_t dst_access_mask
                        , VkDependencyFlagBits flags = (VkDependencyFlagBits)0)
{
    VkSubpassDependency dependency	= {};
    dependency.srcSubpass		= src_subpass;
    dependency.dstSubpass		= dst_subpass;
	
    dependency.srcStageMask		= src_stage;
    dependency.srcAccessMask	= src_access_mask;
	
    dependency.dstStageMask		= dst_stage;
    dependency.dstAccessMask	= dst_access_mask;

    dependency.dependencyFlags = flags;
	
    return(dependency);
}

internal inline VkClearValue
init_clear_color_color(float32_t r, float32_t g, float32_t b, float32_t a)
{
    VkClearValue value {};
    value.color = {r, g, b, a};
    return(value);
}

internal inline VkClearValue
init_clear_color_depth(float32_t d, uint32_t s)
{
    VkClearValue value {};
    value.depthStencil = {d, s};
    return(value);
}

internal inline VkRect2D
init_render_area(const VkOffset2D &offset, const VkExtent2D &extent)
{
    VkRect2D render_area {};
    render_area.offset = offset;
    render_area.extent = extent;
    return(render_area);
}
    
void
command_buffer_begin_render_pass(render_pass_t *render_pass
                                 , struct framebuffer_t *fbo
                                 , VkRect2D render_area
                                 , const memory_buffer_view_t<VkClearValue> &clear_colors
                                 , VkSubpassContents subpass_contents
                                 , VkCommandBuffer *command_buffer);

void
command_buffer_next_subpass(VkCommandBuffer *cmdbuf
                            , VkSubpassContents contents);

internal inline void
command_buffer_end_render_pass(VkCommandBuffer *command_buffer)
{
    vkCmdEndRenderPass(*command_buffer);
}
    
void
init_render_pass(const memory_buffer_view_t<VkAttachmentDescription> &attachment_descriptions
                 , const memory_buffer_view_t<VkSubpassDescription> &subpass_descriptions
                 , const memory_buffer_view_t<VkSubpassDependency> &subpass_dependencies
                 , gpu_t *gpu
                 , render_pass_t *dest_render_pass);
void
init_shader(VkShaderStageFlagBits stage_bits
            , uint32_t content_size
            , byte_t *file_contents
            , gpu_t *gpu
            , VkShaderModule *dest_shader_module);

// describes the binding of a buffer to a model VAO
struct model_binding_t
{
    // buffer that stores all the attributes
    VkBuffer buffer;
    uint32_t binding;
    VkVertexInputRate input_rate;

    VkVertexInputAttributeDescription *attribute_list = nullptr;
    uint32_t stride = 0;
	
    void
    begin_attributes_creation(VkVertexInputAttributeDescription *attribute_list)
    {
        this->attribute_list = attribute_list;
    }
	
    void
    push_attribute(uint32_t location, VkFormat format, uint32_t size)
    {
        VkVertexInputAttributeDescription *attribute = &attribute_list[location];
	    
        attribute->binding = binding;
        attribute->location = location;
        attribute->format = format;
        attribute->offset = stride;

        stride += size;
    }

    void
    end_attributes_creation(void)
    {
        attribute_list = nullptr;
    }
};
    
// describes the attributes and bindings of the model
struct model_t
{
    // model bindings
    uint32_t binding_count;
    // allocated on free list allocator
    model_binding_t *bindings;
    // model attriutes
    uint32_t attribute_count;
    // allocated on free list also | multiple bindings can push to this buffer
    VkVertexInputAttributeDescription *attributes_buffer;

    model_index_data_t index_data;

    memory_buffer_view_t<VkBuffer> raw_cache_for_rendering;

    void
    prepare_bindings(uint32_t binding_count)
    {
	    
    }
	
    VkVertexInputBindingDescription *
    create_binding_descriptions(void)
    {
        VkVertexInputBindingDescription *descriptions = (VkVertexInputBindingDescription *)allocate_stack(sizeof(VkVertexInputBindingDescription) * binding_count
                                                                                                          , 1
                                                                                                          , "binding_total_list_allocation");
        for (uint32_t i = 0; i < binding_count; ++i)
        {
            descriptions[i].binding = bindings[i].binding;
            descriptions[i].stride = bindings[i].stride;
            descriptions[i].inputRate = bindings[i].input_rate;
        }
        return(descriptions);
    }

    void
    create_vertex_input_state_info(VkPipelineVertexInputStateCreateInfo *info)
    {
        VkVertexInputBindingDescription *binding_descriptions = create_binding_descriptions();

        info->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	
        info->vertexBindingDescriptionCount = binding_count;
        info->pVertexBindingDescriptions = binding_descriptions;
        info->vertexAttributeDescriptionCount = attribute_count;
        info->pVertexAttributeDescriptions = attributes_buffer;
    }

    void
    create_vbo_list(void)
    {
        allocate_memory_buffer(raw_cache_for_rendering, binding_count);
	    
        for (uint32_t i = 0; i < binding_count; ++i)
        {
            raw_cache_for_rendering[i] = bindings[i].buffer;
        }
    }

    model_t
    copy(void)
    {
        model_t ret = {};
        ret.binding_count = this->binding_count;
        ret.attribute_count = this->attribute_count;
        ret.index_data = this->index_data;
        ret.bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t) * ret.binding_count);
        memcpy(ret.bindings, this->bindings, sizeof(model_binding_t) * ret.binding_count);
        ret.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * ret.attribute_count);
        memcpy(ret.attributes_buffer, this->attributes_buffer, ret.attribute_count * sizeof(VkVertexInputAttributeDescription));
        return(ret);
    }
};
    
internal inline void
command_buffer_draw_indexed(VkCommandBuffer *command_buffer
                            , const draw_indexed_data_t &data)
{
    vkCmdDrawIndexed(*command_buffer
                     , data.index_count
                     , data.instance_count
                     , data.first_index
                     , (int32_t)data.vertex_offset
                     , data.first_instance);
}

internal inline void
command_buffer_draw(VkCommandBuffer *cmdbuf
                    , uint32_t v_count
                    , uint32_t i_count
                    , uint32_t first_v
                    , uint32_t first_i)
{
    vkCmdDraw(*cmdbuf
              , v_count
              , i_count
              , first_v
              , first_i);
}

internal inline void
command_buffer_bind_pipeline(graphics_pipeline_t *pipeline
                             , VkCommandBuffer *command_buffer)
{
    vkCmdBindPipeline(*command_buffer
                      , VK_PIPELINE_BIND_POINT_GRAPHICS
                      , pipeline->pipeline);
}

// creating pipelines takes a LOT of params
internal inline void
init_shader_pipeline_info(VkShaderModule *module
                          , VkShaderStageFlagBits stage_bits
                          , VkPipelineShaderStageCreateInfo *dest_info)
{
    dest_info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    dest_info->stage = stage_bits;
    dest_info->module = *module;
    dest_info->pName = "main";	
}
    
internal inline void
init_pipeline_vertex_input_info(model_t *model /* model contains input information required */
                                , VkPipelineVertexInputStateCreateInfo *info)
{
    if (model)
    {
        model->create_vertex_input_state_info(info);
    }
    else
    {
        info->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    }
}

internal inline VkVertexInputAttributeDescription
init_attribute_description(uint32_t binding
                           , uint32_t location
                           , VkFormat format
                           , uint32_t offset)
{
    VkVertexInputAttributeDescription info = {};

    info.binding = binding;
    info.location = location;
    info.format = format;
    info.offset = offset;

    return(info);
}
    
internal inline VkVertexInputBindingDescription
init_binding_description(uint32_t binding
                         , uint32_t stride
                         , VkVertexInputRate input_rate)
{
    VkVertexInputBindingDescription info = {};

    info.binding = binding;
    info.stride = stride;
    info.inputRate = input_rate;

    return(info);
}
    
internal inline VkPipelineVertexInputStateCreateInfo
init_pipeline_vertex_input_info(const memory_buffer_view_t<VkVertexInputBindingDescription> &bindings
                                , const memory_buffer_view_t<VkVertexInputAttributeDescription> & attributes)
{
    VkPipelineVertexInputStateCreateInfo info = {};

    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	
    info.vertexBindingDescriptionCount = bindings.count;
    info.pVertexBindingDescriptions = bindings.buffer;
    info.vertexAttributeDescriptionCount = attributes.count;
    info.pVertexAttributeDescriptions = attributes.buffer;

    return(info);
}
    
internal inline void
init_pipeline_input_assembly_info(VkPipelineInputAssemblyStateCreateFlags flags
                                  , VkPrimitiveTopology topology
                                  , VkBool32 primitive_restart
                                  , VkPipelineInputAssemblyStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    info->topology = topology;
    info->primitiveRestartEnable = primitive_restart;
}

internal inline VkRect2D
make_rect2D(int32_t startx,
            int32_t starty,
            uint32_t width,
            uint32_t height)
{
    VkRect2D dst = {};
    dst.offset = VkOffset2D{startx, starty};
    dst.extent = VkExtent2D{width, height};
    return(dst);
}
    
internal inline void
init_viewport(uint32_t x, uint32_t y
              , uint32_t width
              , uint32_t height
              , float32_t min_depth
              , float32_t max_depth
              , VkViewport *viewport)
{
    viewport->x = x;
    viewport->y = y;
    viewport->width = (float32_t)width;
    viewport->height = (float32_t)height;
    viewport->minDepth = min_depth;
    viewport->maxDepth = max_depth;
}

inline void
command_buffer_set_rect2D(VkRect2D *rect, VkCommandBuffer *cmdbuf)
{
    vkCmdSetScissor(*cmdbuf, 0, 1, rect);
}

inline void
command_buffer_set_viewport(VkViewport *viewport, VkCommandBuffer *cmdbuf)
{
    vkCmdSetViewport(*cmdbuf, 0, 1, viewport);
}
    
inline void
command_buffer_set_viewport(uint32_t width, uint32_t height, float32_t depth_min, float32_t depth_max, VkCommandBuffer *cmdbuf)
{
    VkViewport viewport = {};
    init_viewport(0, 0, width, height, depth_min, depth_max, &viewport);
   
    vkCmdSetViewport(*cmdbuf, 0, 1, &viewport);
}

inline void
command_buffer_set_line_width(float32_t width, VkCommandBuffer *cmdbuf)
{
    vkCmdSetLineWidth(*cmdbuf, width);
}

inline void
init_rect2D(VkOffset2D offset
            , VkExtent2D extent
            , VkRect2D *rect)
{
    rect->offset = offset;
    rect->extent = extent;
}

inline void
init_pipeline_viewport_info(const memory_buffer_view_t<VkViewport> &viewports
                            , const memory_buffer_view_t<VkRect2D> &scissors
                            , VkPipelineViewportStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    info->viewportCount = viewports.count;
    info->pViewports = viewports.buffer;
    info->scissorCount = scissors.count;
    info->pScissors = scissors.buffer;;
}
    
inline void
init_pipeline_viewport_info(memory_buffer_view_t<VkViewport> *viewports
                            , memory_buffer_view_t<VkRect2D> *scissors
                            , VkPipelineViewportStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    info->viewportCount = viewports->count;
    info->pViewports = viewports->buffer;
    info->scissorCount = scissors->count;
    info->pScissors = scissors->buffer;;
}
    
internal inline void
init_pipeline_rasterization_info(VkPolygonMode polygon_mode
                                 , VkCullModeFlags cull_flags
                                 , float32_t line_width
                                 , VkPipelineRasterizationStateCreateFlags flags
                                 , VkPipelineRasterizationStateCreateInfo *info
                                 , VkBool32 enable_depth_bias = VK_FALSE
                                 , float32_t bias_constant = 0.0f
                                 , float32_t bias_slope = 0.0f)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info->depthClampEnable = VK_FALSE;
    info->rasterizerDiscardEnable = VK_FALSE;
    info->polygonMode = polygon_mode;
    info->lineWidth = line_width;
    info->cullMode = cull_flags;
    info->frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    info->depthBiasEnable = enable_depth_bias;
    info->depthBiasConstantFactor = bias_constant;
    info->depthBiasClamp = 0.0f;
    info->depthBiasSlopeFactor = bias_slope;
    info->flags = flags;
}
    
internal inline void
init_pipeline_multisampling_info(VkSampleCountFlagBits rasterization_samples
                                 , VkPipelineMultisampleStateCreateFlags flags
                                 , VkPipelineMultisampleStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    info->sampleShadingEnable = VK_FALSE;
    info->rasterizationSamples = rasterization_samples;
    info->minSampleShading = 1.0f;
    info->pSampleMask = nullptr;
    info->alphaToCoverageEnable = VK_FALSE;
    info->alphaToOneEnable = VK_FALSE;
    info->flags = flags;
}

internal inline void
init_blend_state_attachment(VkColorComponentFlags color_write_flags
                            , VkBool32 enable_blend
                            , VkBlendFactor src_color
                            , VkBlendFactor dst_color
                            , VkBlendOp color_op
                            , VkBlendFactor src_alpha
                            , VkBlendFactor dst_alpha
                            , VkBlendOp alpha_op
                            , VkPipelineColorBlendAttachmentState *attachment)
{

    attachment->colorWriteMask = color_write_flags;
    attachment->blendEnable = enable_blend;
    attachment->srcColorBlendFactor = src_color;
    attachment->dstColorBlendFactor = dst_color;
    attachment->colorBlendOp = color_op;
    attachment->srcAlphaBlendFactor = src_alpha;
    attachment->dstAlphaBlendFactor = dst_alpha;
    attachment->alphaBlendOp = alpha_op;
}

internal inline void
init_pipeline_blending_info(VkBool32 enable_logic_op
                            , VkLogicOp logic_op
                            , const memory_buffer_view_t<VkPipelineColorBlendAttachmentState> &states
                            , VkPipelineColorBlendStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    info->logicOpEnable = enable_logic_op;
    info->logicOp = logic_op;
    info->attachmentCount = states.count;
    info->pAttachments = states.buffer;
    info->blendConstants[0] = 0.0f;
    info->blendConstants[1] = 0.0f;
    info->blendConstants[2] = 0.0f;
    info->blendConstants[3] = 0.0f;
}
    
internal inline void
init_pipeline_blending_info(VkBool32 enable_logic_op
                            , VkLogicOp logic_op
                            , memory_buffer_view_t<VkPipelineColorBlendAttachmentState> *states
                            , VkPipelineColorBlendStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    info->logicOpEnable = enable_logic_op;
    info->logicOp = logic_op;
    info->attachmentCount = states->count;
    info->pAttachments = states->buffer;
    info->blendConstants[0] = 0.0f;
    info->blendConstants[1] = 0.0f;
    info->blendConstants[2] = 0.0f;
    info->blendConstants[3] = 0.0f;
}

internal inline void
init_pipeline_dynamic_states_info(memory_buffer_view_t<VkDynamicState> *dynamic_states
                                  , VkPipelineDynamicStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    info->dynamicStateCount = dynamic_states->count;
    info->pDynamicStates = dynamic_states->buffer;
}

void
init_pipeline_layout(memory_buffer_view_t<VkDescriptorSetLayout> *layouts
                     , memory_buffer_view_t<VkPushConstantRange> *ranges
                     , gpu_t *gpu
                     , VkPipelineLayout *pipeline_layout);

void
init_pipeline_layout(const memory_buffer_view_t<VkDescriptorSetLayout> &layouts
                     , const memory_buffer_view_t<VkPushConstantRange> &ranges
                     , gpu_t *gpu
                     , VkPipelineLayout *pipeline_layout);

internal inline void
init_push_constant_range(VkShaderStageFlags stage_flags
                         , uint32_t size
                         , uint32_t offset
                         , VkPushConstantRange *rng)
{
    rng->stageFlags = stage_flags;
    rng->offset = offset;
    rng->size = size;
}
    
internal inline void
init_pipeline_depth_stencil_info(VkBool32 depth_test_enable
                                 , VkBool32 depth_write_enable
                                 , float32_t min_depth_bounds
                                 , float32_t max_depth_bounds
                                 , VkBool32 stencil_enable
                                 , VkPipelineDepthStencilStateCreateInfo *info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    info->depthTestEnable = depth_test_enable;
    info->depthWriteEnable = depth_write_enable;
    info->depthCompareOp = VK_COMPARE_OP_LESS;
    info->depthBoundsTestEnable = VK_FALSE;
    info->minDepthBounds = min_depth_bounds;
    info->maxDepthBounds = max_depth_bounds;
    info->stencilTestEnable = VK_FALSE;
    info->front = {};
    info->back = {};
}

void
init_graphics_pipeline(memory_buffer_view_t<VkPipelineShaderStageCreateInfo> *shaders
                       , VkPipelineVertexInputStateCreateInfo *vertex_input_info
                       , VkPipelineInputAssemblyStateCreateInfo *input_assembly_info
                       , VkPipelineViewportStateCreateInfo *viewport_info
                       , VkPipelineRasterizationStateCreateInfo *rasterization_info
                       , VkPipelineMultisampleStateCreateInfo *multisample_info
                       , VkPipelineColorBlendStateCreateInfo *blend_info
                       , VkPipelineDynamicStateCreateInfo *dynamic_state_info
                       , VkPipelineDepthStencilStateCreateInfo *depth_stencil_info
                       , VkPipelineLayout *layout
                       , render_pass_t *render_pass
                       , uint32_t subpass
                       , gpu_t *gpu
                       , VkPipeline *pipeline);

void
allocate_command_pool(uint32_t queue_family_index
                      , gpu_t *gpu
                      , VkCommandPool *command_pool);

void
allocate_command_buffers(VkCommandPool *command_pool_source
                         , VkCommandBufferLevel level
                         , gpu_t *gpu
                         , const memory_buffer_view_t<VkCommandBuffer> &command_buffers);

internal inline void
free_command_buffer(const memory_buffer_view_t<VkCommandBuffer> &command_buffers
                    , VkCommandPool *pool
                    , gpu_t *gpu)
{
    vkFreeCommandBuffers(gpu->logical_device
                         , *pool
                         , command_buffers.count
                         , command_buffers.buffer);
}
    
void
begin_command_buffer(VkCommandBuffer *command_buffer
                     , VkCommandBufferUsageFlags usage_flags
                     , VkCommandBufferInheritanceInfo *inheritance);

internal inline void
end_command_buffer(VkCommandBuffer *command_buffer)
{
    vkEndCommandBuffer(*command_buffer);
}

void
submit(const memory_buffer_view_t<VkCommandBuffer> &command_buffers
       , const memory_buffer_view_t<VkSemaphore> &wait_semaphores
       , const memory_buffer_view_t<VkSemaphore> &signal_semaphores
       , const memory_buffer_view_t<VkPipelineStageFlags> &wait_stages
       , VkFence *fence
       , VkQueue *queue);

void
init_single_use_command_buffer(VkCommandPool *command_pool
                               , gpu_t *gpu
                               , VkCommandBuffer *dst);

void
destroy_single_use_command_buffer(VkCommandBuffer *command_buffer
                                  , VkCommandPool *command_pool
                                  , gpu_t *gpu);
    
void
transition_image_layout(VkImage *image
                        , VkFormat format
                        , VkImageLayout old_layout
                        , VkImageLayout new_layout
                        , VkCommandPool *graphics_command_pool
                        , gpu_t *gpu);

void
copy_buffer_into_image(gpu_buffer_t *src_buffer
                       , image2d_t *dst_image
                       , uint32_t width
                       , uint32_t height
                       , VkCommandPool *command_pool
                       , gpu_t *gpu);

void
copy_buffer(gpu_buffer_t *src_buffer
            , gpu_buffer_t *dst_buffer
            , VkCommandPool *command_pool
            , gpu_t *gpu);

void
invoke_staging_buffer_for_device_local_buffer(memory_byte_buffer_t items
                                              , VkBufferUsageFlags usage
                                              , VkCommandPool *transfer_command_pool
                                              , gpu_buffer_t *dst_buffer
                                              , gpu_t *gpu);

void
invoke_staging_buffer_for_device_local_image(memory_byte_buffer_t items,
                                             VkCommandPool *transfer_command_pool,
                                             image2d_t *dst_image,
                                             int32_t width, int32_t height,
                                             gpu_t *gpu);
    
struct framebuffer_t
{
    VkFramebuffer framebuffer;

    VkExtent2D extent;

    // for color attachments only
    memory_buffer_view_t<VkImageView> color_attachments;
    VkImageView depth_attachment;

    void
    destroy(gpu_t *gpu)
    {
        vkDestroyFramebuffer(gpu->logical_device, framebuffer, nullptr);
    }
};

void
init_framebuffer_attachment(uint32_t width
                            , uint32_t height
                            , VkFormat format
                            , VkImageUsageFlags usage
                            , gpu_t *gpu
                            , image2d_t *attachment
                            // for_t cubemap targets
                            , uint32_t layers = 1
                            , VkImageCreateFlags create_flags = 0
                            , VkImageViewType image_view_type = VK_IMAGE_VIEW_TYPE_2D);
    
void
init_framebuffer(render_pass_t *compatible_render_pass
                 , uint32_t width
                 , uint32_t height
                 , uint32_t layers
                 , gpu_t *gpu
                 , framebuffer_t *framebuffer); // need to initialize the attachment handles

internal inline VkDescriptorSetLayoutBinding
init_descriptor_set_layout_binding(VkDescriptorType type
                                   , uint32_t binding
                                   , uint32_t count
                                   , VkShaderStageFlags stage)
{
    VkDescriptorSetLayoutBinding ubo_binding	= {};
    ubo_binding.binding				= binding;
    ubo_binding.descriptorType			= type;
    ubo_binding.descriptorCount			= count;
    ubo_binding.stageFlags				= stage;
    ubo_binding.pImmutableSamplers			= nullptr;
    return(ubo_binding);
}

void
init_descriptor_set_layout(const memory_buffer_view_t<VkDescriptorSetLayoutBinding> &bindings
                           , gpu_t *gpu
                           , VkDescriptorSetLayout *dst);

internal inline void
command_buffer_bind_descriptor_sets(graphics_pipeline_t *pipeline
                                    , const memory_buffer_view_t<VkDescriptorSet> &sets
                                    , VkCommandBuffer *command_buffer)
{
    vkCmdBindDescriptorSets(*command_buffer
                            , VK_PIPELINE_BIND_POINT_GRAPHICS
                            , pipeline->layout
                            , 0
                            , sets.count
                            , sets.buffer
                            , 0
                            , nullptr);
}
    
void
allocate_descriptor_sets(memory_buffer_view_t<VkDescriptorSet> &descriptor_sets
                         , const memory_buffer_view_t<VkDescriptorSetLayout> &layouts
                         , gpu_t *gpu
                         , VkDescriptorPool *descriptor_pool);

VkDescriptorSet
allocate_descriptor_set(VkDescriptorSetLayout *layout
                        , gpu_t *gpu
                        , VkDescriptorPool *descriptor_pool);
    
internal void
init_descriptor_set_buffer_info(gpu_buffer_t *buffer
                                , uint32_t offset_in_ubo
                                , VkDescriptorBufferInfo *buffer_info)
{
    buffer_info->buffer = buffer->buffer;
    buffer_info->offset = offset_in_ubo;
    buffer_info->range = buffer->size;
}

internal void
init_buffer_descriptor_set_write(VkDescriptorSet *set
                                 , uint32_t binding
                                 , uint32_t dst_array_element
                                 , uint32_t count
                                 , VkDescriptorBufferInfo *infos
                                 , VkWriteDescriptorSet *write)
{
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->dstSet = *set;
    write->dstBinding = binding;
    write->dstArrayElement = dst_array_element;
    write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write->descriptorCount = count;
    write->pBufferInfo = infos;
}

internal void
init_input_attachment_descriptor_set_write(VkDescriptorSet *set
                                           , uint32_t binding
                                           , uint32_t dst_array_element
                                           , uint32_t count
                                           , VkDescriptorImageInfo *infos
                                           , VkWriteDescriptorSet *write)
{
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->dstSet = *set;
    write->dstBinding = binding;
    write->dstArrayElement = dst_array_element;
    write->descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    write->descriptorCount = count;
    write->pImageInfo = infos;
}
    
internal void
init_image_descriptor_set_write(VkDescriptorSet *set
                                , uint32_t binding
                                , uint32_t dst_array_element
                                , uint32_t count
                                , VkDescriptorImageInfo *infos
                                , VkWriteDescriptorSet *write)
{
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->dstSet = *set;
    write->dstBinding = binding;
    write->dstArrayElement = dst_array_element;
    write->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write->descriptorCount = count;
    write->pImageInfo = infos;
}
    
internal void
init_descriptor_set_image_info(VkSampler sampler
                               , VkImageView image_view
                               , VkImageLayout expected_layout
                               , VkDescriptorImageInfo *image_info)
{
    image_info->imageLayout = expected_layout;
    image_info->imageView = image_view;
    image_info->sampler = sampler;
}

internal void
init_descriptor_pool_size(VkDescriptorType type
                          , uint32_t count
                          , VkDescriptorPoolSize *size)
{
    size->type = type;
    size->descriptorCount = count;
}

struct descriptor_pool_t
{
    VkDescriptorPool pool;
};

void
init_descriptor_pool(const memory_buffer_view_t<VkDescriptorPoolSize> &sizes
                     , uint32_t max_sets
                     , gpu_t *gpu
                     , VkDescriptorPool *pool);
    
void
init_descriptor_pool(const memory_buffer_view_t<VkDescriptorPoolSize> &sizes
                     , uint32_t max_sets
                     , gpu_t *gpu
                     , descriptor_pool_t *pool);
    
    
void
update_descriptor_sets(const memory_buffer_view_t<VkWriteDescriptorSet> &writes
                       , gpu_t *gpu);

internal inline void
init_semaphore(gpu_t *gpu
               , VkSemaphore *semaphore)
{
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	
    VK_CHECK(vkCreateSemaphore(gpu->logical_device
                               , &semaphore_info
                               , nullptr
                               , semaphore));
}

internal inline void
init_fence(gpu_t *gpu
           , VkFenceCreateFlags flags
           , VkFence *fence)
{
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = flags;

    VK_CHECK(vkCreateFence(gpu->logical_device
                           , &fence_info
                           , nullptr
                           , fence));
}

internal inline void
wait_fences(gpu_t *gpu
            , const memory_buffer_view_t<VkFence> &fences)
{
    vkWaitForFences(gpu->logical_device
                    , fences.count
                    , fences.buffer
                    , VK_TRUE
                    , std::numeric_limits<uint64_t>::max());
}

struct next_image_return_t {VkResult result; uint32_t image_index;};
internal inline next_image_return_t
acquire_next_image(swapchain_t *swapchain
                   , gpu_t *gpu
                   , VkSemaphore *semaphore
                   , VkFence *fence)
{
    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(gpu->logical_device
                                            , swapchain->swapchain
                                            , std::numeric_limits<uint64_t>::max()
                                            , *semaphore
                                            , *fence
                                            , &image_index);
    return(next_image_return_t{result, image_index});
}

VkResult
present(const memory_buffer_view_t<VkSemaphore> &signal_semaphores
        , const memory_buffer_view_t<VkSwapchainKHR> &swapchains
        , uint32_t *image_index
        , VkQueue *present_queue);

internal inline void
reset_fences(gpu_t *gpu
             , const memory_buffer_view_t<VkFence> &fences)
{
    vkResetFences(gpu->logical_device, fences.count, fences.buffer);
}

internal inline uint32_t
adjust_memory_size_for_gpu_alignment(uint32_t size
                                     , gpu_t *gpu)
{
    uint32_t alignment = gpu->properties.limits.nonCoherentAtomSize;

    uint32_t mod = size % alignment;
	
    if (mod == 0) return size;
    else return size + alignment - mod;
}
    
struct vulkan_state_t
{
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    gpu_t gpu;
    VkSurfaceKHR surface;
    swapchain_t swapchain;
};

/* entry point for vulkan stuff */
void
init_vulkan_state(vulkan_state_t *state
                  , GLFWwindow *window);

void
destroy_swapchain(vulkan_state_t *state);
    
void
destroy_vulkan_state(vulkan_state_t *state);
