#pragma once

#include "core.hpp"
#include "vulkan.hpp"

/* all rendering objects (render passes, buffers, images, etc...) are accessed
   through 32 bit handles (with object respective names) however, if the handle is 
   not known in a certain scope, it can be accessed through a "[name]"_hash identifier 
   which will serve as a lookup into a hash table mapping string hashes to object handles
   a handle cache is maintained for extra speed (however if that extra speed is negligible, 
   in the future, all objects will simply be accessed through these strings) */

namespace Rendering
{
    
    struct Rendering_State
    {
	R_Mem<Vulkan_API::Render_Pass> test_render_pass;
	R_Mem<VkDescriptorSetLayout> descriptor_set_layout;
	R_Mem<Vulkan_API::Graphics_Pipeline> graphics_pipeline;
	R_Mem<VkCommandPool> graphics_command_pool;
	R_Mem<Vulkan_API::Image2D> depth_image;
	R_Mem<Vulkan_API::Model> test_model;
	R_Mem<Vulkan_API::Image2D> texture;

	R_Mem<Vulkan_API::Buffer> uniform_buffers;
	R_Mem<Vulkan_API::Descriptor_Set> descriptor_sets;

	R_Mem<VkCommandBuffer> command_buffers;

	R_Mem<VkSemaphore> image_ready_semaphores;
	R_Mem<VkSemaphore> render_finished_semaphores;
	R_Mem<VkFence> fences;
    };

    void
    init_rendering_state(Vulkan_API::State *vulkan_state
			 , Rendering_State *cache);

    void
    init_rendering_system(Vulkan_API::Swapchain *swapchain
			  , Vulkan_API::GPU *gpu
			  , R_Mem<Vulkan_API::Render_Pass> rndr_pass);

    // init rendering system actually has to be split in multiple stages
    // because of descriptor sets and layouts - need a JSON loader for sets and layouts to make this cleaner
    void
    init_render_passes_from_json(Vulkan_API::Swapchain *swapchain
				 , Vulkan_API::GPU *gpu);

    void
    init_framebuffers_from_json(Vulkan_API::Swapchain *swapchain
				, Vulkan_API::GPU *gpu);

    // not from JSON yet
    void
    init_descriptor_sets_and_layouts(Vulkan_API::Swapchain *swapchain
				     , Vulkan_API::GPU *gpu);
    
    void
    init_pipelines_from_json(Vulkan_API::Swapchain *swapchain
			     , Vulkan_API::GPU *gpu);

    struct Renderer_Init_Data
    {
	Constant_String rndr_id;
	
	s32 mtrl_max;
	Constant_String ppln_id;
	// images are stored in the descriptor sets
	Memory_Buffer_View<Constant_String> descriptor_sets;

	VkShaderStageFlags mtrl_unique_data_stage_dst;
    };
    
    void
    add_renderer(Renderer_Init_Data *init_data, VkCommandPool *cmdpool, Vulkan_API::GPU *gpu);

    void
    update_renderers(VkCommandBuffer *record_cmd
		     , VkExtent2D swapchain_extent
		     , u32 image_index
		     , const Memory_Buffer_View<VkDescriptorSet> &additional_sets
		     , R_Mem<Vulkan_API::Render_Pass> rndr_pass
		     , const glm::vec3 &player_position
		     , const glm::vec4 &light_position);

    struct Material_Data
    {
	void *data;
	u32 data_size = 0;

	R_Mem<Vulkan_API::Model> model;
	Vulkan_API::Draw_Indexed_Data draw_info;
   };

    struct Material_Access { s32 rndr_id, mtrl_id; };
    
    Material_Access
    init_material(const Constant_String &rndr_id
		  , const struct Material_Data *data);

}
