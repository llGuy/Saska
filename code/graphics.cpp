#include "core.hpp"
#include "vulkan.hpp"

#include "graphics.hpp"

// Declared in header
GPU_Buffer_Manager g_gpu_buffer_manager;
Image_Manager g_image_manager;
Framebuffer_Manager g_framebuffer_manager;
Render_Pass_Manager g_render_pass_manager;
Pipeline_Manager g_pipeline_manager;
Uniform_Layout_Manager g_uniform_layout_manager;
Uniform_Group_Manager g_uniform_group_manager;
Model_Manager g_model_manager;

// Later when maybe introducing new APIs, might be something different
// Clearer name for people reading code
using GPU_Command_Queue = VkCommandBuffer;
using GPU_Command_Queue_Pool = VkCommandPool;
// Submit level of a Material Submission Queue Manager which will either submit to a secondary queue or directly into the main queue
using Submit_Level = VkCommandBufferLevel;

GPU_Command_Queue
make_command_queue(VkCommandPool *pool, Submit_Level level, Vulkan::GPU *gpu)
{
    GPU_Command_Queue result;
    Vulkan::allocate_command_buffers(pool, level, gpu, {1, &result});
    return(result);
}

void
begin_command_queue(GPU_Command_Queue *queue, Vulkan::GPU *gpu)
{
    Vulkan::begin_command_buffer(queue, 0, nullptr);
}
    
void
end_command_queue(GPU_Command_Queue *queue, Vulkan::GPU *gpu)
{
    Vulkan::end_command_buffer(queue);
}



// --------------------- Uniform stuff ---------------------
// Naming is better than Descriptor in case of people familiar with different APIs / also will be useful when introducing other APIs
using Uniform_Binding = VkDescriptorSetLayoutBinding;

Uniform_Binding
make_uniform_binding_s(u32 count
		       , u32 binding
		       , VkDescriptorType uniform_type
		       , VkShaderStageFlags shader_flags)
{
    return Vulkan::init_descriptor_set_layout_binding(uniform_type, binding, count, shader_flags);
}



void
Uniform_Layout_Info::allocate(u32 binding_count)
{
    allocate_memory_buffer(bindings, binding_count);
}

void
Uniform_Layout_Info::free(void)
{
    deallocate_free_list(bindings.buffer);
}
    
void
Uniform_Layout_Info::push(const Uniform_Binding &binding_info)
{
    bindings[stack_ptr++] = binding_info;
}

void
Uniform_Layout_Info::push(u32 count
			  , u32 binding
			  , VkDescriptorType uniform_type
			  , VkShaderStageFlags shader_flags)
{
    bindings[stack_ptr++] = Vulkan::init_descriptor_set_layout_binding(uniform_type, binding, count, shader_flags);
}

using Uniform_Layout = VkDescriptorSetLayout;

Uniform_Layout
make_uniform_layout(Uniform_Layout_Info *blueprint, Vulkan::GPU *gpu)
{
    VkDescriptorSetLayout layout;
    Vulkan::init_descriptor_set_layout(blueprint->bindings, gpu, &layout);
    return(layout);
}



// Uniform Group is the struct going to be used to alias VkDescriptorSet, and in other APIs, simply groups of uniforms
using Uniform_Group = VkDescriptorSet;

Uniform_Group
make_uniform_group(Uniform_Layout *layout, VkDescriptorPool *pool, Vulkan::GPU *gpu)
{
    Uniform_Group uniform_group = Vulkan::allocate_descriptor_set(layout, gpu, pool);

    return(uniform_group);
}

VkWriteDescriptorSet
update_texture(Uniform_Group *group, Vulkan::Image2D &img, u32 binding, u32 dst_element, u32 count, VkImageLayout layout)
{
    // textures will kind of always be VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    VkDescriptorImageInfo image_info = img.make_descriptor_info(layout);
    VkWriteDescriptorSet write = {};
    Vulkan::init_image_descriptor_set_write(group, binding, dst_element, count, &image_info, &write);
    
    return(write);
}

VkWriteDescriptorSet
update_buffer(Uniform_Group *group, Vulkan::Buffer &ubo, u32 binding, u32 dst_element, u32 count, u32 offset_into_buffer)
{
    VkDescriptorBufferInfo buffer_info = ubo.make_descriptor_info(offset_into_buffer);
    VkWriteDescriptorSet write = {};
    Vulkan::init_buffer_descriptor_set_write(group, binding, dst_element, count, &buffer_info, &write);

    return(write);
}

// Use : update_binding_from_group( { update_texture(...), update_texture(...), update_buffer(...)... } ...)
void
update_binding_from_group(const Memory_Buffer_View<VkWriteDescriptorSet> &writes, Vulkan::GPU *gpu)
{
    Vulkan::update_descriptor_sets(writes, gpu);
};



// --------------------- Rendering stuff ---------------------

// will be used for multi-threading the rendering process for extra extra performance !
global_var struct GPU_Material_Submission_Queue_Manager // maybe in the future this will be called multi-threaded rendering manager
{
    persist constexpr u32 MAX_ACTIVE_QUEUES = 10;

    u32 active_queue_ptr {0};
    GPU_Command_Queue active_queues[MAX_ACTIVE_QUEUES];
} material_queue_manager;

u32
GPU_Material_Submission_Queue::push_material(void *push_k_ptr, u32 push_k_size
					     , const Memory_Buffer_View<VkBuffer> &vbo_bindings
					     , const Vulkan::Model_Index_Data &index_data
					     , const Vulkan::Draw_Indexed_Data &draw_index_data)
{
    Material new_mtrl = {};
    new_mtrl.push_k_ptr = push_k_ptr;
    new_mtrl.push_k_size = push_k_size;
    new_mtrl.vbo_bindings = vbo_bindings;
    new_mtrl.index_data = index_data;
    new_mtrl.draw_index_data = draw_index_data;

    mtrls[mtrl_count] = new_mtrl;

    return(mtrl_count++);
}

GPU_Command_Queue *
GPU_Material_Submission_Queue::get_command_buffer(GPU_Command_Queue *queue)
{
    if (cmdbuf_index >= 0)
    {
	return(&material_queue_manager.active_queues[cmdbuf_index]);
    }
    else return(queue);
}
    
void
GPU_Material_Submission_Queue::submit_queued_materials(const Memory_Buffer_View<Uniform_Group> &uniform_groups
						       , Vulkan::Graphics_Pipeline *graphics_pipeline
						       , VkViewport *viewport
						       , Vulkan::Render_Pass *render_pass
						       , Vulkan::Framebuffer *fbo
						       , u32 subpass
						       , GPU_Command_Queue *main_queue
						       , Submit_Level level)
{
    // Depends on the cmdbuf_index var, not on the submit level that is given
    GPU_Command_Queue *dst_command_queue = get_command_buffer(main_queue);
    // Now depends also on the submit level
    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) dst_command_queue = main_queue;

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	VkCommandBufferInheritanceInfo inheritance_info = {};
	inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritance_info.renderPass = render_pass->render_pass;
	inheritance_info.subpass = subpass;
	inheritance_info.framebuffer = fbo->framebuffer;
	
	Vulkan::begin_command_buffer(dst_command_queue
				     , VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
				     , &inheritance_info);
    }
	    
    Vulkan::command_buffer_bind_pipeline(graphics_pipeline, dst_command_queue);

    vkCmdSetViewport(*dst_command_queue, 0, 1, viewport);    

    Vulkan::command_buffer_bind_descriptor_sets(graphics_pipeline
						, uniform_groups
						, dst_command_queue);

    for (u32 i = 0; i < mtrl_count; ++i)
    {
	Material *mtrl = &mtrls[i];

	Memory_Buffer_View<VkDeviceSize> offsets;
	allocate_memory_buffer_tmp(offsets, mtrl->vbo_bindings.count);
	offsets.zero();
			
	Vulkan::command_buffer_bind_vbos(mtrl->vbo_bindings
					 , offsets
					 , 0
					 , mtrl->vbo_bindings.count
					 , dst_command_queue);
			
	Vulkan::command_buffer_bind_ibo(mtrl->index_data
					, dst_command_queue);

	Vulkan::command_buffer_push_constant(mtrl->push_k_ptr
					     , mtrl->push_k_size
					     , 0
					     , push_k_dst
					     , graphics_pipeline
					     , dst_command_queue);

	Vulkan::command_buffer_draw_indexed(dst_command_queue
					    , mtrl->draw_index_data);
    }

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	Vulkan::end_command_buffer(dst_command_queue);
    }
}

void
GPU_Material_Submission_Queue::flush_queue(void)
{
    mtrl_count = 0;
}

void
GPU_Material_Submission_Queue::submit_to_cmdbuf(GPU_Command_Queue *queue)
{
    Vulkan::command_buffer_execute_commands(queue, {1, get_command_buffer(nullptr)});
}

GPU_Material_Submission_Queue
make_gpu_material_submission_queue(u32 max_materials, VkShaderStageFlags push_k_dst // for rendering purposes (quite Vulkan specific)
				   , Submit_Level level, GPU_Command_Queue_Pool *pool, Vulkan::GPU *gpu)
{
    GPU_Material_Submission_Queue material_queue;

    material_queue.push_k_dst = push_k_dst;
    
    // not used for multi threading - directly gets inlined into the main command queue
    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
	material_queue.mtrl_count = 0;
	allocate_memory_buffer(material_queue.mtrls, max_materials);
    }

    // used for multi threading - gets submitted to secondary buffer then all the secondary buffers join into the main queue
    else if (level = VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	GPU_Command_Queue command_queue = make_command_queue(pool, level, gpu);

	material_queue.mtrl_count = 0;
	allocate_memory_buffer(material_queue.mtrls, max_materials);

	material_queue.cmdbuf_index = material_queue_manager.active_queue_ptr;
	material_queue_manager.active_queues[material_queue_manager.active_queue_ptr++] = command_queue;
    }

    return(material_queue);
}

void
submit_queued_materials_from_secondary_queues(GPU_Command_Queue *queue)
{
    Vulkan::command_buffer_execute_commands(queue, {material_queue_manager.active_queue_ptr, material_queue_manager.active_queues});
}
