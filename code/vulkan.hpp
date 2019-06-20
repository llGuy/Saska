#pragma once

#include <limits.h>
#include <memory.h>
#include "core.hpp"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#define MAKE_VK_VIEW_TYPES(VkName) using View_##VkName = VkName; using Real_##VkName = VkName;

namespace Vulkan
{
    
    struct Queue_Families
    {
	s32 graphics_family = -1;
	s32 present_family = 1;

	inline bool
	complete(void)
	{
	    return(graphics_family >= 0 && present_family >= 0);
	}
    };

    struct Swapchain_Details
    {
	VkSurfaceCapabilitiesKHR capabilities;

	u32 available_formats_count;
	VkSurfaceFormatKHR *available_formats;
	
	u32 available_present_modes_count;
	VkPresentModeKHR *available_present_modes;
    };
    
    struct GPU
    {
	VkPhysicalDevice hardware;
	VkDevice logical_device;

	VkPhysicalDeviceMemoryProperties memory_properties;
	VkPhysicalDeviceProperties properties;

	Queue_Families queue_families;
	VkQueue graphics_queue;
	VkQueue present_queue;

	Swapchain_Details swapchain_support;
	VkFormat supported_depth_format;

	void
	find_queue_families(VkSurfaceKHR *surface);
    };

    namespace Memory
    {

	// allocates memory for stuff like buffers and images
	void
	allocate_gpu_memory(VkMemoryPropertyFlags properties
			    , VkMemoryRequirements memory_requirements
			    , GPU *gpu
			    , VkDeviceMemory *dest_memory);
    }
    
    struct Mapped_GPU_Memory
    {
	u32 offset;
	VkDeviceSize size;
	VkDeviceMemory *memory;
	void *data;

	FORCEINLINE void
	begin(GPU *gpu)
	{
	    vkMapMemory(gpu->logical_device, *memory, offset, size, 0, &data);
	}

	FORCEINLINE void
	fill(Memory_Byte_Buffer byte_buffer)
	{
	    memcpy(data, byte_buffer.ptr, size);
	}

	FORCEINLINE void
	flush(u32 offset, u32 size, Vulkan::GPU *gpu)
	{
	    VkMappedMemoryRange range = {};
	    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	    range.memory = *memory;
	    range.offset = offset;
	    range.size = size;
	    vkFlushMappedMemoryRanges(gpu->logical_device, 1, &range);
	}
	
	FORCEINLINE void
	end(GPU *gpu)
	{
	    vkUnmapMemory(gpu->logical_device, *memory);
	}
    };

    struct Buffer
    {
	VkBuffer buffer;
	
	VkDeviceMemory memory;
	VkDeviceSize size;

	FORCEINLINE Mapped_GPU_Memory
	construct_map(void)
	{
	    return(Mapped_GPU_Memory{0, size, &memory});
	}

	FORCEINLINE VkDescriptorBufferInfo
	make_descriptor_info(u32 offset_into_buffer)
	{
	    VkDescriptorBufferInfo info = {};
	    info.buffer = buffer;
	    info.offset = offset_into_buffer; // for the moment is 0
	    info.range = size;
	    return(info);
	}
    };

    struct Draw_Indexed_Data
    {
	u32 index_count;
	u32 instance_count;
	u32 first_index;
	u32 vertex_offset;
	u32 first_instance;
    };

    internal FORCEINLINE Draw_Indexed_Data
    init_draw_indexed_data_default(u32 instance_count, u32 index_count)
    {
	Draw_Indexed_Data index_data = {};
	index_data.index_count = index_count;
	index_data.instance_count = instance_count;
	index_data.first_index = 0;
	index_data.vertex_offset = 0;
	index_data.first_instance = 0;
	return(index_data);
    }
    
    struct Model_Index_Data
    {
	VkBuffer index_buffer;
	
	u32 index_count;
	u32 index_offset;
	VkIndexType index_type;

	Draw_Indexed_Data
	init_draw_indexed_data(u32 first_index
			       , u32 offset)
	{
	    Draw_Indexed_Data data;
	    data.index_count = index_count;
	    // default the instance_count to 0
	    data.instance_count = 1;
	    data.first_index = 0;
	    data.vertex_offset = offset;
	    data.first_instance = 0;

	    return(data);
	}
    };
    
    internal FORCEINLINE void
    command_buffer_bind_ibo(const Model_Index_Data &index_data
			    , VkCommandBuffer *command_buffer)
    {
	vkCmdBindIndexBuffer(*command_buffer
			     , index_data.index_buffer
			     , index_data.index_offset
			     , index_data.index_type);
    }
    
    internal FORCEINLINE void
    command_buffer_bind_vbos(const Memory_Buffer_View<VkBuffer> &buffers
			     , const Memory_Buffer_View<VkDeviceSize> &offsets
			     , u32 first_binding, u32 binding_count
			     , VkCommandBuffer *command_buffer)
    {
	vkCmdBindVertexBuffers(*command_buffer
			       , first_binding
			       , binding_count
			       , buffers.buffer
			       , offsets.buffer);
    }

    internal FORCEINLINE void
    command_buffer_execute_commands(VkCommandBuffer *cmdbuf
				    , const Memory_Buffer_View<VkCommandBuffer> &cmds)
    {
	vkCmdExecuteCommands(*cmdbuf
			     , cmds.count
			     , cmds.buffer);
    }

    
    struct Graphics_Pipeline
    {
	enum Shader_Stages_Bits : s32 { VERTEX_SHADER_BIT = 1 << 0
					  , GEOMETRY_SHADER_BIT = 1 << 1
					  , TESSELATION_SHADER_BIT = 1 << 2
					  , FRAGMENT_SHADER_BIT = 1 << 3};

	s32 stages;

	const char *base_dir_and_name;

	VkPipelineLayout layout;

	VkPipeline pipeline;

	FORCEINLINE void
	destroy(Vulkan::GPU *gpu)
	{
	    vkDestroyPipeline(gpu->logical_device, pipeline, nullptr);
	}
    };
    
    internal FORCEINLINE void
    command_buffer_push_constant(void *data
				 , u32 size
				 , u32 offset
				 , VkShaderStageFlags stage
				 , Graphics_Pipeline *ppln
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
		  , GPU *gpu
		  , Buffer *dest_buffer);

    
    struct Image2D
    {
	VkImage image			= VK_NULL_HANDLE;
	VkImageView image_view		= VK_NULL_HANDLE;
	VkSampler image_sampler		= VK_NULL_HANDLE;
	VkDeviceMemory device_memory	= VK_NULL_HANDLE;

	VkFormat format;
	
	FORCEINLINE VkMemoryRequirements
	get_memory_requirements(GPU *gpu)
	{
	    VkMemoryRequirements requirements = {};
	    vkGetImageMemoryRequirements(gpu->logical_device, image, &requirements);

	    return(requirements);
	}

	FORCEINLINE VkDescriptorImageInfo
	make_descriptor_info(VkImageLayout expected_layout)
	{
	    VkDescriptorImageInfo info = {};
	    info.imageLayout = expected_layout;
	    info.imageView = image_view;
	    info.sampler = image_sampler;
	    return(info);
	}

	FORCEINLINE void
	destroy(Vulkan::GPU *gpu)
	{
	    if (image != VK_NULL_HANDLE) vkDestroyImage(gpu->logical_device, image, nullptr);
	    if (image_view != VK_NULL_HANDLE) vkDestroyImageView(gpu->logical_device, image_view, nullptr);
	    if (image_sampler != VK_NULL_HANDLE) vkDestroySampler(gpu->logical_device, image_sampler, nullptr);
	    if (device_memory != VK_NULL_HANDLE) vkFreeMemory(gpu->logical_device, device_memory, nullptr);
	}
    }; 
    
    void
    init_image(u32 width
	       , u32 height
	       , VkFormat format
	       , VkImageTiling tiling
	       , VkImageUsageFlags usage
	       , VkMemoryPropertyFlags properties
	       , u32 layers
	       , GPU *gpu
	       , Image2D *dest_image
	       , VkImageCreateFlags flags = 0);

    void
    init_image_view(VkImage *image
		    , VkFormat format
		    , VkImageAspectFlags aspect_flags
		    , GPU *gpu
		    , VkImageView *dest_image_view
		    , VkImageViewType type
		    , u32 layers);

    void
    init_image_sampler(VkFilter mag_filter
		       , VkFilter min_filter
		       , VkSamplerAddressMode u_sampler_address_mode
		       , VkSamplerAddressMode v_sampler_address_mode
		       , VkSamplerAddressMode w_sampler_address_mode
		       , VkBool32 anisotropy_enable
		       , u32 max_anisotropy
		       , VkBorderColor clamp_border_color
		       , VkBool32 compare_enable
		       , VkCompareOp compare_op
		       , VkSamplerMipmapMode mipmap_mode
		       , f32 mip_lod_bias
		       , f32 min_lod
		       , f32 max_lod
		       , GPU *gpu
		       , VkSampler *dest_sampler);

    struct Swapchain
    {
	VkFormat format;
	VkPresentModeKHR present_mode;
	VkSwapchainKHR swapchain;
	VkExtent2D extent;
	
	Memory_Buffer_View<VkImage> imgs;
	Memory_Buffer_View<VkImageView> views;
    };
    
    struct Render_Pass
    {
	VkRenderPass render_pass;
	u32 subpass_count;

	FORCEINLINE void
	destroy(Vulkan::GPU *gpu)
	{
	    vkDestroyRenderPass(gpu->logical_device, render_pass, nullptr);
	}
    };

    internal FORCEINLINE VkAttachmentDescription
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

    internal FORCEINLINE VkAttachmentReference
    init_attachment_reference(u32 attachment
			      , VkImageLayout layout)
    {
	VkAttachmentReference ref = {};
	ref.attachment = attachment;
	ref.layout = layout;
	return(ref);
    }

    internal FORCEINLINE VkSubpassDescription
    init_subpass_description(const Memory_Buffer_View<VkAttachmentReference> &color_refs
			     , VkAttachmentReference *depth
			     , const Memory_Buffer_View<VkAttachmentReference> &input_refs)
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

    internal FORCEINLINE VkSubpassDependency
    init_subpass_dependency(u32 src_subpass
			    , u32 dst_subpass
			    , VkPipelineStageFlags src_stage
			    , u32 src_access_mask
			    , VkPipelineStageFlags dst_stage
			    , u32 dst_access_mask
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

    internal FORCEINLINE VkClearValue
    init_clear_color_color(f32 r, f32 g, f32 b, f32 a)
    {
	VkClearValue value {};
	value.color = {r, g, b, a};
	return(value);
    }

    internal FORCEINLINE VkClearValue
    init_clear_color_depth(f32 d, u32 s)
    {
	VkClearValue value {};
	value.depthStencil = {d, s};
	return(value);
    }

    internal FORCEINLINE VkRect2D
    init_render_area(const VkOffset2D &offset, const VkExtent2D &extent)
    {
	VkRect2D render_area {};
	render_area.offset = offset;
	render_area.extent = extent;
	return(render_area);
    }
    
    void
    command_buffer_begin_render_pass(Render_Pass *render_pass
				     , struct Framebuffer *fbo
				     , VkRect2D render_area
				     , const Memory_Buffer_View<VkClearValue> &clear_colors
				     , VkSubpassContents subpass_contents
				     , VkCommandBuffer *command_buffer);

    void
    command_buffer_next_subpass(VkCommandBuffer *cmdbuf
				, VkSubpassContents contents);

    internal FORCEINLINE void
    command_buffer_end_render_pass(VkCommandBuffer *command_buffer)
    {
	vkCmdEndRenderPass(*command_buffer);
    }
    
    void
    init_render_pass(const Memory_Buffer_View<VkAttachmentDescription> &attachment_descriptions
		     , const Memory_Buffer_View<VkSubpassDescription> &subpass_descriptions
		     , const Memory_Buffer_View<VkSubpassDependency> &subpass_dependencies
		     , GPU *gpu
		     , Render_Pass *dest_render_pass);
    void
    init_shader(VkShaderStageFlagBits stage_bits
		, u32 content_size
		, byte *file_contents
		, GPU *gpu
		, VkShaderModule *dest_shader_module);

    // describes the binding of a buffer to a model VAO
    struct Model_Binding
    {
	// buffer that stores all the attributes
	VkBuffer buffer;
	u32 binding;
	VkVertexInputRate input_rate;

	VkVertexInputAttributeDescription *attribute_list = nullptr;
	u32 stride = 0;
	
	void
	begin_attributes_creation(VkVertexInputAttributeDescription *attribute_list)
	{
	    this->attribute_list = attribute_list;
	}
	
	void
	push_attribute(u32 location, VkFormat format, u32 size)
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
    struct Model
    {
	// model bindings
	u32 binding_count;
	// allocated on free list allocator
	Model_Binding *bindings;
	// model attriutes
	u32 attribute_count;
	// allocated on free list also | multiple bindings can push to this buffer
	VkVertexInputAttributeDescription *attributes_buffer;

	Model_Index_Data index_data;

	Memory_Buffer_View<VkBuffer> raw_cache_for_rendering;

	void
	prepare_bindings(u32 binding_count)
	{
	    
	}
	
	VkVertexInputBindingDescription *
	create_binding_descriptions(void)
	{
	    VkVertexInputBindingDescription *descriptions = (VkVertexInputBindingDescription *)allocate_stack(sizeof(VkVertexInputBindingDescription) * binding_count
													      , 1
													      , "binding_total_list_allocation");
	    for (u32 i = 0; i < binding_count; ++i)
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
	    
	    for (u32 i = 0; i < binding_count; ++i)
	    {
		raw_cache_for_rendering[i] = bindings[i].buffer;
	    }
	}

	Model
	copy(void)
	{
	    Model ret = {};
	    ret.binding_count = this->binding_count;
	    ret.attribute_count = this->attribute_count;
	    ret.index_data = this->index_data;
	    ret.bindings = (Model_Binding *)allocate_free_list(sizeof(Model_Binding) * ret.binding_count);
	    memcpy(ret.bindings, this->bindings, sizeof(Model_Binding) * ret.binding_count);
	    ret.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * ret.attribute_count);
	    memcpy(ret.attributes_buffer, this->attributes_buffer, ret.attribute_count * sizeof(VkVertexInputAttributeDescription));
	    return(ret);
	}
    };
    
    internal FORCEINLINE void
    command_buffer_draw_indexed(VkCommandBuffer *command_buffer
				, const Draw_Indexed_Data &data)
    {
	vkCmdDrawIndexed(*command_buffer
			 , data.index_count
			 , data.instance_count
			 , data.first_index
			 , (s32)data.vertex_offset
			 , data.first_instance);
    }

    internal FORCEINLINE void
    command_buffer_draw(VkCommandBuffer *cmdbuf
			, u32 v_count
			, u32 i_count
			, u32 first_v
			, u32 first_i)
    {
	vkCmdDraw(*cmdbuf
		  , v_count
		  , i_count
		  , first_v
		  , first_i);
    }

    internal FORCEINLINE void
    command_buffer_bind_pipeline(Graphics_Pipeline *pipeline
				 , VkCommandBuffer *command_buffer)
    {
	vkCmdBindPipeline(*command_buffer
			  , VK_PIPELINE_BIND_POINT_GRAPHICS
			  , pipeline->pipeline);
    }

    // creating pipelines takes a LOT of params
    internal FORCEINLINE void
    init_shader_pipeline_info(VkShaderModule *module
			      , VkShaderStageFlagBits stage_bits
			      , VkPipelineShaderStageCreateInfo *dest_info)
    {
	dest_info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	dest_info->stage = stage_bits;
	dest_info->module = *module;
	dest_info->pName = "main";	
    }
    
    internal FORCEINLINE void
    init_pipeline_vertex_input_info(Model *model /* model contains input information required */
				    , VkPipelineVertexInputStateCreateInfo *info)
    {
	model->create_vertex_input_state_info(info);
    }

    internal FORCEINLINE VkVertexInputAttributeDescription
    init_attribute_description(u32 binding
			       , u32 location
			       , VkFormat format
			       , u32 offset)
    {
	VkVertexInputAttributeDescription info = {};

	info.binding = binding;
	info.location = location;
	info.format = format;
	info.offset = offset;

	return(info);
    }
    
    internal FORCEINLINE VkVertexInputBindingDescription
    init_binding_description(u32 binding
			     , u32 stride
			     , VkVertexInputRate input_rate)
    {
	VkVertexInputBindingDescription info = {};

	info.binding = binding;
	info.stride = stride;
	info.inputRate = input_rate;

	return(info);
    }
    
    internal FORCEINLINE VkPipelineVertexInputStateCreateInfo
    init_pipeline_vertex_input_info(const Memory_Buffer_View<VkVertexInputBindingDescription> &bindings
				    , const Memory_Buffer_View<VkVertexInputAttributeDescription> & attributes)
    {
	VkPipelineVertexInputStateCreateInfo info = {};

	info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	
	info.vertexBindingDescriptionCount = bindings.count;
	info.pVertexBindingDescriptions = bindings.buffer;
	info.vertexAttributeDescriptionCount = attributes.count;
	info.pVertexAttributeDescriptions = attributes.buffer;

	return(info);
    }
    
    internal FORCEINLINE void
    init_pipeline_input_assembly_info(VkPipelineInputAssemblyStateCreateFlags flags
				      , VkPrimitiveTopology topology
				      , VkBool32 primitive_restart
				      , VkPipelineInputAssemblyStateCreateInfo *info)
    {
	info->sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	info->topology = topology;
	info->primitiveRestartEnable = primitive_restart;
    }

    internal FORCEINLINE void
    init_viewport(u32 width
		  , u32 height
		  , f32 min_depth
		  , f32 max_depth
		  , VkViewport *viewport)
    {
	viewport->x = 0.0f;
	viewport->y = 0.0f;
	viewport->width = (f32)width;
	viewport->height = (f32)height;
	viewport->minDepth = min_depth;
	viewport->maxDepth = max_depth;
    }

    internal FORCEINLINE void
    init_rect_2D(VkOffset2D offset
		 , VkExtent2D extent
		 , VkRect2D *rect)
    {
	rect->offset = offset;
	rect->extent = extent;
    }
    
    internal FORCEINLINE void
    init_pipeline_viewport_info(Memory_Buffer_View<VkViewport> *viewports
				, Memory_Buffer_View<VkRect2D> *scissors
				, VkPipelineViewportStateCreateInfo *info)
    {
	info->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	info->viewportCount = viewports->count;
	info->pViewports = viewports->buffer;
	info->scissorCount = scissors->count;
	info->pScissors = scissors->buffer;;
    }
				
    
    internal FORCEINLINE void
    init_pipeline_rasterization_info(VkPolygonMode polygon_mode
				     , VkCullModeFlags cull_flags
				     , f32 line_width
				     , VkPipelineRasterizationStateCreateFlags flags
				     , VkPipelineRasterizationStateCreateInfo *info)
    {
	info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	info->depthClampEnable = VK_FALSE;
	info->rasterizerDiscardEnable = VK_FALSE;
	info->polygonMode = polygon_mode;
	info->lineWidth = line_width;
	info->cullMode = cull_flags;
	info->frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	info->depthBiasEnable = VK_FALSE;
	info->depthBiasConstantFactor = 0.0f;
	info->depthBiasClamp = 0.0f;
	info->depthBiasSlopeFactor = 0.0f;
	info->flags = flags;
    }
    
    internal FORCEINLINE void
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

    internal FORCEINLINE void
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
    
    internal FORCEINLINE void
    init_pipeline_blending_info(VkBool32 enable_logic_op
				, VkLogicOp logic_op
				, Memory_Buffer_View<VkPipelineColorBlendAttachmentState> *states
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

    internal FORCEINLINE void
    init_pipeline_dynamic_states_info(Memory_Buffer_View<VkDynamicState> *dynamic_states
				      , VkPipelineDynamicStateCreateInfo *info)
    {
	info->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	info->dynamicStateCount = dynamic_states->count;
	info->pDynamicStates = dynamic_states->buffer;
    }

    void
    init_pipeline_layout(Memory_Buffer_View<VkDescriptorSetLayout> *layouts
			 , Memory_Buffer_View<VkPushConstantRange> *ranges
			 , GPU *gpu
			 , VkPipelineLayout *pipeline_layout);

    internal FORCEINLINE void
    init_push_constant_range(VkShaderStageFlags stage_flags
			     , u32 size
			     , u32 offset
			     , VkPushConstantRange *rng)
    {
	rng->stageFlags = stage_flags;
	rng->offset = offset;
	rng->size = size;
    }
    
    internal FORCEINLINE void
    init_pipeline_depth_stencil_info(VkBool32 depth_test_enable
				     , VkBool32 depth_write_enable
				     , f32 min_depth_bounds
				     , f32 max_depth_bounds
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
    init_graphics_pipeline(Memory_Buffer_View<VkPipelineShaderStageCreateInfo> *shaders
			   , VkPipelineVertexInputStateCreateInfo *vertex_input_info
			   , VkPipelineInputAssemblyStateCreateInfo *input_assembly_info
			   , VkPipelineViewportStateCreateInfo *viewport_info
			   , VkPipelineRasterizationStateCreateInfo *rasterization_info
			   , VkPipelineMultisampleStateCreateInfo *multisample_info
			   , VkPipelineColorBlendStateCreateInfo *blend_info
			   , VkPipelineDynamicStateCreateInfo *dynamic_state_info
			   , VkPipelineDepthStencilStateCreateInfo *depth_stencil_info
			   , VkPipelineLayout *layout
			   , Render_Pass *render_pass
			   , u32 subpass
			   , GPU *gpu
			   , VkPipeline *pipeline);

    void
    allocate_command_pool(u32 queue_family_index
			  , GPU *gpu
			  , VkCommandPool *command_pool);

    void
    allocate_command_buffers(VkCommandPool *command_pool_source
			     , VkCommandBufferLevel level
			     , GPU *gpu
			     , const Memory_Buffer_View<VkCommandBuffer> &command_buffers);

    internal FORCEINLINE void
    free_command_buffer(const Memory_Buffer_View<VkCommandBuffer> &command_buffers
			, VkCommandPool *pool
			, GPU *gpu)
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

    internal FORCEINLINE void
    end_command_buffer(VkCommandBuffer *command_buffer)
    {
	vkEndCommandBuffer(*command_buffer);
    }

    void
    submit(const Memory_Buffer_View<VkCommandBuffer> &command_buffers
	   , const Memory_Buffer_View<VkSemaphore> &wait_semaphores
	   , const Memory_Buffer_View<VkSemaphore> &signal_semaphores
	   , const Memory_Buffer_View<VkPipelineStageFlags> &wait_stages
	   , VkFence *fence
	   , VkQueue *queue);

    void
    init_single_use_command_buffer(VkCommandPool *command_pool
				   , GPU *gpu
				   , VkCommandBuffer *dst);

    void
    destroy_single_use_command_buffer(VkCommandBuffer *command_buffer
				      , VkCommandPool *command_pool
				      , GPU *gpu);
    
    void
    transition_image_layout(VkImage *image
			    , VkFormat format
			    , VkImageLayout old_layout
			    , VkImageLayout new_layout
			    , VkCommandPool *graphics_command_pool
			    , GPU *gpu);

    void
    copy_buffer_into_image(Buffer *src_buffer
			   , Image2D *dst_image
			   , u32 width
			   , u32 height
			   , VkCommandPool *command_pool
			   , GPU *gpu);

    void
    copy_buffer(Buffer *src_buffer
		, Buffer *dst_buffer
		, VkCommandPool *command_pool
		, GPU *gpu);

    void
    invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer items
						  , VkBufferUsageFlags usage
						  , VkCommandPool *transfer_command_pool
						  , Buffer *dst_buffer
						  , GPU *gpu);

    void
    invoke_staging_buffer_for_device_local_image();
    
    struct Framebuffer
    {
	VkFramebuffer framebuffer;

	VkExtent2D extent;

	// for color attachments only
	Memory_Buffer_View<VkImageView> color_attachments;
	VkImageView depth_attachment;
    };

    void
    init_framebuffer_attachment(u32 width
				, u32 height
				, VkFormat format
				, VkImageUsageFlags usage
				, GPU *gpu
				, Image2D *attachment);
    
    void
    init_framebuffer(Render_Pass *compatible_render_pass
		     , u32 width
		     , u32 height
		     , u32 layers
		     , GPU *gpu
		     , Framebuffer *framebuffer); // need to initialize the attachment handles

    internal FORCEINLINE VkDescriptorSetLayoutBinding
    init_descriptor_set_layout_binding(VkDescriptorType type
				       , u32 binding
				       , u32 count
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
    init_descriptor_set_layout(const Memory_Buffer_View<VkDescriptorSetLayoutBinding> &bindings
			       , GPU *gpu
			       , VkDescriptorSetLayout *dst);

    internal FORCEINLINE void
    command_buffer_bind_descriptor_sets(Graphics_Pipeline *pipeline
					, const Memory_Buffer_View<VkDescriptorSet> &sets
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
    allocate_descriptor_sets(Memory_Buffer_View<VkDescriptorSet> &descriptor_sets
			     , const Memory_Buffer_View<VkDescriptorSetLayout> &layouts
			     , GPU *gpu
			     , VkDescriptorPool *descriptor_pool);

    VkDescriptorSet
    allocate_descriptor_set(VkDescriptorSetLayout *layout
			    , GPU *gpu
			    , VkDescriptorPool *descriptor_pool);
    
    internal void
    init_descriptor_set_buffer_info(Buffer *buffer
				    , u32 offset_in_ubo
				    , VkDescriptorBufferInfo *buffer_info)
    {
	buffer_info->buffer = buffer->buffer;
	buffer_info->offset = offset_in_ubo;
	buffer_info->range = buffer->size;
    }

    internal void
    init_buffer_descriptor_set_write(VkDescriptorSet *set
				     , u32 binding
				     , u32 dst_array_element
				     , u32 count
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
				    , u32 binding
				    , u32 dst_array_element
				    , u32 count
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
				    , u32 binding
				    , u32 dst_array_element
				    , u32 count
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
			      , u32 count
			      , VkDescriptorPoolSize *size)
    {
	size->type = type;
	size->descriptorCount = count;
    }

    struct Descriptor_Pool
    {
	VkDescriptorPool pool;
    };
    
    void
    init_descriptor_pool(const Memory_Buffer_View<VkDescriptorPoolSize> &sizes
			 , u32 max_sets
			 , GPU *gpu
			 , Descriptor_Pool *pool);
    
    
    void
    update_descriptor_sets(const Memory_Buffer_View<VkWriteDescriptorSet> &writes
			   , GPU *gpu);

    internal FORCEINLINE void
    init_semaphore(GPU *gpu
		   , VkSemaphore *semaphore)
    {
	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	
	VK_CHECK(vkCreateSemaphore(gpu->logical_device
				   , &semaphore_info
				   , nullptr
				   , semaphore));
    }

    internal FORCEINLINE void
    init_fence(GPU *gpu
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

    internal FORCEINLINE void
    wait_fences(GPU *gpu
	       , const Memory_Buffer_View<VkFence> &fences)
    {
	vkWaitForFences(gpu->logical_device
			, fences.count
			, fences.buffer
			, VK_TRUE
			, UINT_MAX);
    }

    struct Next_Image_Return {VkResult result; u32 image_index;};
    internal FORCEINLINE Next_Image_Return
    acquire_next_image(Swapchain *swapchain
		       , GPU *gpu
		       , VkSemaphore *semaphore
		       , VkFence *fence)
    {
	u32 image_index = 0;
	VkResult result = vkAcquireNextImageKHR(gpu->logical_device
						, swapchain->swapchain
						, UINT_MAX
						, *semaphore
						, *fence
						, &image_index);
	return(Next_Image_Return{result, image_index});
    }

    VkResult
    present(const Memory_Buffer_View<VkSemaphore> &signal_semaphores
	    , const Memory_Buffer_View<VkSwapchainKHR> &swapchains
	    , u32 *image_index
	    , VkQueue *present_queue);

    internal FORCEINLINE void
    reset_fences(GPU *gpu
		 , const Memory_Buffer_View<VkFence> &fences)
    {
	vkResetFences(gpu->logical_device, fences.count, fences.buffer);
    }

    internal FORCEINLINE u32
    adjust_memory_size_for_gpu_alignment(u32 size
					 , Vulkan::GPU *gpu)
    {
	u32 alignment = gpu->properties.limits.nonCoherentAtomSize;

	u32 mod = size % alignment;
	
	if (mod == 0) return size;
	else return size + alignment - mod;
    }
    
    struct State
    {
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;
	GPU gpu;
	VkSurfaceKHR surface;
	Swapchain swapchain;
    };

    /* entry point for vulkan stuff */
    void
    init_state(State *state
	       , GLFWwindow *window);

    void
    destroy_state(State *state);

}
