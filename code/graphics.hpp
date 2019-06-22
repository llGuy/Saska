#pragma once

#include "core.hpp"
#include "vulkan.hpp"

using Handle = u32;
using GPU_Buffer_Handle = Handle;
using Image_Handle = Handle;
using Framebuffer_Handle = Handle;
using Render_Pass_Handle = Handle;
using Pipeline_Handle = Handle;
using Model_Handle = Handle;

template <typename T, u32 Max = 40> struct Object_Manager
{
    using Type = T;
    
    u32 count {0};
    T   objects[ Max ];

    // To be used only in initialization of program.
    Hash_Table_Inline<u32, Max, 4, 4> object_name_map {""};

    Handle
    add(const Constant_String &name, u32 allocation_count = 1)
    {
	object_name_map.insert(name.hash, count);

	u32 prev = count;
	count += allocation_count;
	
	return(prev);
    }

    Handle
    get_handle(const Constant_String &name)
    {
	return(*object_name_map.get(name.hash));
    }
    
    T *
    get(Handle handle)
    {
	return(&objects[handle]);
    }

    // To use for convenience, not for performance
    T *
    get(const Constant_String &name)
    {
	return(&objects[ *object_name_map.get(name.hash) ]);
    }

    void
    clean_up(Vulkan::GPU *gpu)
    {
	for (u32 i = 0; i < count; ++i)
	{
	    objects[i].destroy(gpu);
	}
    }
};

using GPU_Buffer_Manager = Object_Manager<Vulkan::Buffer>;
using Image_Manager = Object_Manager<Vulkan::Image2D>;
using Framebuffer_Manager = Object_Manager<Vulkan::Framebuffer>;
using Render_Pass_Manager = Object_Manager<Vulkan::Render_Pass>;
using Pipeline_Manager = Object_Manager<Vulkan::Graphics_Pipeline>;
using Model_Manager = Object_Manager<Vulkan::Model>;
// For now, defined descriptor manager structs here:

// Uniform Group is the struct going to be used to alias VkDescriptorSet, and in other APIs, simply groups of uniforms
using Uniform_Group = VkDescriptorSet;
using Uniform_Layout = VkDescriptorSetLayout;

using Uniform_Layout_Manager = Object_Manager<Uniform_Layout>;
using Uniform_Group_Manager = Object_Manager<Uniform_Group>;

extern GPU_Buffer_Manager g_gpu_buffer_manager;
extern Image_Manager g_image_manager;
extern Framebuffer_Manager g_framebuffer_manager;
extern Render_Pass_Manager g_render_pass_manager;
extern Pipeline_Manager g_pipeline_manager;
extern Uniform_Layout_Manager g_uniform_layout_manager;
extern Uniform_Group_Manager g_uniform_group_manager;
extern Model_Manager g_model_manager;



// Later when maybe introducing new APIs, might be something different
// Clearer name for people reading code
using GPU_Command_Queue = VkCommandBuffer;
using GPU_Command_Queue_Pool = VkCommandPool;
// Submit level of a Material Submission Queue Manager which will either submit to a secondary queue or directly into the main queue
using Submit_Level = VkCommandBufferLevel;

GPU_Command_Queue
make_command_queue(VkCommandPool *pool, Submit_Level level, Vulkan::GPU *gpu);

void
begin_command_queue(GPU_Command_Queue *queue, Vulkan::GPU *gpu);
    
void
end_command_queue(GPU_Command_Queue *queue, Vulkan::GPU *gpu);

// --------------------- Uniform stuff ---------------------
// Naming is better than Descriptor in case of people familiar with different APIs / also will be useful when introducing other APIs
using Uniform_Binding = VkDescriptorSetLayoutBinding;

Uniform_Binding
make_uniform_binding_s(u32 count
		       , u32 binding
		       , VkDescriptorType uniform_type
		       , VkShaderStageFlags shader_flags);

// Layout depends on uniform bindings --> almost like a prototype for making uniform groups
// Separate Uniform_Layout_Info (list of binding structs) from Uniform_Layout (API struct) for optimisation reasons
struct Uniform_Layout_Info // --> VkDescriptorSetLayout
{
    Memory_Buffer_View<Uniform_Binding> bindings;
    u32 stack_ptr {0};

    void
    allocate(u32 binding_count);

    void
    free(void);
    
    void
    push(const Uniform_Binding &binding_info);

    void
    push(u32 count
	 , u32 binding
	 , VkDescriptorType uniform_type
	 , VkShaderStageFlags shader_flags);
};



Uniform_Layout
make_uniform_layout(Uniform_Layout_Info *blueprint, Vulkan::GPU *gpu);



Uniform_Group
make_uniform_group(Uniform_Layout *layout, VkDescriptorPool *pool, Vulkan::GPU *gpu);

VkWriteDescriptorSet
update_texture(Uniform_Group *group, Vulkan::Image2D &img, u32 binding, u32 dst_element, u32 count, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

VkWriteDescriptorSet
update_buffer(Uniform_Group *group, Vulkan::Buffer &ubo, u32 binding, u32 dst_element, u32 count, u32 offset_into_buffer = 0);

// Use : update_binding_from_group( { update_texture(...), update_texture(...), update_buffer(...)... } ...)
void
update_binding_from_group(const Memory_Buffer_View<VkWriteDescriptorSet> &writes, Vulkan::GPU *gpu);

// Update_Struct should always be VkWriteDescriptorSet
// Function for compile time stuff (however, most of it will be runtime from JSON - or whatever file format is currently being used)
template <typename ...Update_Struct> void
update_binding_from_group(Vulkan::GPU *gpu, Update_Struct &&...updates)
{
    constexpr u32 UPDATES_COUNT = sizeof...(Update_Struct);
    VkWriteDescriptorSet tmp_updates[UPDATES_COUNT] = { updates... };
    Vulkan::update_descriptor_sets({UPDATES_COUNT, tmp_updates}, gpu);
}

using Uniform_Layout_Handle = Handle;
using Uniform_Group_Handle = Handle;





// --------------------- Rendering stuff ---------------------
// Material is submittable to a GPU_Material_Submission_Queue to be eventually submitted to the GPU for render
struct Material 
{
    // ---- push constant information
    void *push_k_ptr = nullptr;
    u32 push_k_size = 0;
    // ---- vbo information
    Memory_Buffer_View<VkBuffer> vbo_bindings;
    // ---- for sorting
    u32 model_id;
    // ---- ibo information
    Vulkan::Model_Index_Data index_data;
    Vulkan::Draw_Indexed_Data draw_index_data;
};

// Queue of materials to be submitted
struct GPU_Material_Submission_Queue
{
    u32 mtrl_count;
    Memory_Buffer_View<Material> mtrls;
    
    VkShaderStageFlags push_k_dst;

    // for multi-threaded rendering in the future when needed
    s32 cmdbuf_index{-1};

    u32
    push_material(void *push_k_ptr, u32 push_k_size
		  , const Memory_Buffer_View<VkBuffer> &vbo_bindings
		  , const Vulkan::Model_Index_Data &index_data
		  , const Vulkan::Draw_Indexed_Data &draw_index_data);

    GPU_Command_Queue *
    get_command_buffer(GPU_Command_Queue *queue = nullptr);
    
    void
    submit_queued_materials(const Memory_Buffer_View<Uniform_Group> &uniform_groups
			    , Vulkan::Graphics_Pipeline *graphics_pipeline
			    , VkViewport *viewport
			    , Vulkan::Render_Pass *render_pass
			    , Vulkan::Framebuffer *fbo
			    , u32 subpass
			    , GPU_Command_Queue *main_queue
			    , Submit_Level level);
	
    void
    flush_queue(void);

    void
    submit_to_cmdbuf(GPU_Command_Queue *queue);
};

GPU_Material_Submission_Queue
make_gpu_material_submission_queue(u32 max_materials, VkShaderStageFlags push_k_dst // for rendering purposes (quite Vulkan specific)
				   , Submit_Level level, GPU_Command_Queue_Pool *pool, Vulkan::GPU *gpu);

void
submit_queued_materials_from_secondary_queues(GPU_Command_Queue *queue);
