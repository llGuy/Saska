#pragma once

#include "core.hpp"
#include "vulkan.hpp"

struct Material
{
    // ---- push constant information
    void *push_k_ptr = nullptr;
    u32 push_k_size = 0;
    // ---- vbo information
    Memory_Buffer_View<VkBuffer> vbo_bindings;
    // ---- for sorting (later)
    u32 model_id;
    // ---- ibo information
    Vulkan::Model_Index_Data index_data;
    Vulkan::Draw_Indexed_Data draw_index_data;
};

struct Cached_Render_Submissions_Manager
{
    static constexpr u32 MAX_COMMAND_BUFFERS = 10;

    u32 active_cmdbuf_count;
    VkCommandBuffer cache_cmdbufs[MAX_COMMAND_BUFFERS];
};

struct Render_Submission_Queue
{
    R_Mem<Vulkan::Graphics_Pipeline> ppln;
    VkShaderStageFlags push_k_dst;
	
    Memory_Buffer_View<Material> mtrls;
    u32 mtrl_count;

    // optional
    s32 cmdbuf_index {-1};

    // ---- methods ----

    Render_Submission_Queue(const R_Mem<Vulkan::Graphics_Pipeline> &p_ppln
			    , VkShaderStageFlags pk_dst
			    , u32 max_mtrls);

    u32 // <---- index of the mtrl in the array
    push_material(void *push_k_ptr, u32 push_k_size
	 , const Memory_Buffer_View<VkBuffer> &vbo_bindings
	 , const Vulkan::Model_Index_Data &index_data
	 , const Vulkan::Draw_Indexed_Data &draw_index_data) 
    {
	Material new_mtrl		= {};
	new_mtrl.push_k_ptr		= push_k_ptr;
	new_mtrl.push_k_size	= push_k_size;
	new_mtrl.vbo_bindings	= vbo_bindings;
	new_mtrl.index_data		= index_data;
	new_mtrl.draw_index_data	= draw_index_data;

	mtrls[mtrl_count] = new_mtrl;

	return(mtrl_count++);
    }

    VkCommandBuffer *
    get_cache_command_buffer(Cached_Render_Submissions_Manager *manager)
    {
	if (cmdbuf_index == -1)
	{
	    return(nullptr);
	}
	else
	{
	    return(&pool[cmdbuf_index]);
	}
    }

    void
    execute_cached_cmdbuf(VkCommandBuffer *cmdbufs_pool
			  , VkCommandBuffer *dst)
    {
	if (cmdbuf_index != -1)
	{
	    Vulkan::command_buffer_execute_commands(dst
						    , {1, get_cache_command_buffer(cmdbufs_pool)});
	}
    }

    enum Send_Commands { CACHE_QUEUE, FLUSH_QUEUE };
    
    void
    send_commands(Send_Commands after_send
		  , VkCommandBuffer *primary_cmdbuf
		  , Cached_Render_Submissions_Manager *cached_manager
		  , const Memory_Buffer_View<VkDescriptorSet> &sets
		  , Vulkan::Graphics_Pipeline *in_ppln = nullptr)
    {
	VkCommandBuffer *dst = [primary_cmdbuf, cached_manager](u32 cmdbuf_index) -> VkCommandBuffer *
	{
	    if (cmdbuf_index == -1)
	    {
		return(primary_cmdbuf);
	    }
	    else
	    {
		Vulkan::begin_command_buffer(&cached_manager->cache_cmdbufs[cmdbuf_index], 0, nullptr);
		return(&cached_manager->cache_cmdbufs[cmdbuf_index]);
	    }
	}(cmdbuf_index);

	Vulkan::Graphics_Pipeline *used_ppln = [this, in_ppln]() -> Vulkan::Graphics_Pipeline *
	{
	    if (!in_ppln)
	    {
		return(ppln.p);
	    }
	    else
	    {
		return(in_ppln);
	    }		
	}();
	
	update();
    }
    
    void
    update(VkCommandBuffer *cmdbuf
	   , const Memory_Buffer_View<VkDescriptorSet> &sets
	   , Vulkan::Graphics_Pipeline *in_ppln = nullptr)
    {
	if (!in_ppln)
	{
	    in_ppln = ppln.p;
	}

	Vulkan::begin_command_buffer(cmdbuf, 0, nullptr);
	    
	Vulkan::command_buffer_bind_pipeline(in_ppln, cmdbuf);

	Vulkan::command_buffer_bind_descriptor_sets(in_ppln
						    , sets
						    , cmdbuf);

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
					     , cmdbuf);
			
	    Vulkan::command_buffer_bind_ibo(mtrl->index_data
					    , cmdbuf);

	    Vulkan::command_buffer_push_constant(mtrl->push_k_ptr
						 , mtrl->push_k_size
						 , 0
						 , push_k_dst
						 , in_ppln
						 , cmdbuf);

	    Vulkan::command_buffer_draw_indexed(cmdbuf
						, mtrl->draw_index_data);
	}
	    
	Vulkan::end_command_buffer(cmdbuf);
    }
};
