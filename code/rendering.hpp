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
	Vulkan_API::Registered_Render_Pass test_render_pass;
	Vulkan_API::Registered_Descriptor_Set_Layout descriptor_set_layout;
	Vulkan_API::Registered_Graphics_Pipeline graphics_pipeline;
	Vulkan_API::Registered_Command_Pool graphics_command_pool;
	Vulkan_API::Registered_Image2D depth_image;
	Vulkan_API::Registered_Model test_model;
	Vulkan_API::Registered_Image2D texture;

	Vulkan_API::Registered_Buffer uniform_buffers;
	Vulkan_API::Registered_Descriptor_Set descriptor_sets;

	Vulkan_API::Registered_Command_Buffer command_buffers;

	Vulkan_API::Registered_Semaphore image_ready_semaphores;
	Vulkan_API::Registered_Semaphore render_finished_semaphores;
	Vulkan_API::Registered_Fence fences;
    };

    void
    init_rendering_state(Vulkan_API::State *vulkan_state
			 , Rendering_State *cache);

    void
    init_rendering_system(Vulkan_API::Swapchain *swapchain
			  , Vulkan_API::GPU *gpu
			  , Vulkan_API::Registered_Render_Pass rndr_pass);

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
		     , Vulkan_API::Registered_Render_Pass rndr_pass);

    struct Material_Data
    {
	void *data;
	u32 data_size = 0;

	Vulkan_API::Registered_Model model;
	Vulkan_API::Draw_Indexed_Data draw_info;
   };

    struct Material_Access { s32 rndr_id, mtrl_id; };
    
    Material_Access
    init_material(const Constant_String &rndr_id
		  , const struct Material_Data *data);

}
