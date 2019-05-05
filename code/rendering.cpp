#include <chrono>
#include <nlohmann/json.hpp>
#include "vulkan.hpp"
#include <glm/glm.hpp>
#include "rendering.hpp"
#include "file_system.hpp"
#include <glm/gtx/transform.hpp>

#include "load.hpp"

namespace Rendering
{
    
    // almost acts like the actual render component object itself
    struct Material
    {
	// possibly uniform data or data to be submitted to a instance buffer of something
	void *data = nullptr;
	u32 data_size = 0;

	R_Mem<Vulkan::Model> model;
	Vulkan::Draw_Indexed_Data draw_info;
    };

    /*
      NEED A WAY TO SORT : 
      - MATERIAL WITH THE SAME MODELS (organizing the bindings of the ibos and vbos!!!)
      - THEN NEED TO MAKE SURE THAT EACH MATERIAL RENDERER HANDLES AN ARRAY OF TEXTURES (in which the user can push textures that various objects will have to bind) - so that binding textures is just a push constant
     */
    struct Renderer // renders a material type
    {
	// shader, pipieline states...
	R_Mem<Vulkan::Graphics_Pipeline> ppln;
	Memory_Buffer_View<VkDescriptorSet> sets;
	VkShaderStageFlags mtrl_unique_data_stage_dst;
	
	Memory_Buffer_View<Material> mtrls;
	
	u32 mtrl_count;

	enum Render_Method { INLINE, INSTANCED } mthd;

	// reuse for different render passes (e.g. for example for rendering shadow maps, reflection / refraction...)
	VkCommandBuffer rndr_cmdbuf;
	
	void
	init(Renderer_Init_Data *data
	     , VkCommandPool *cmdpool
	     , Vulkan::GPU *gpu)
	{
	    mtrl_count = 0;
	    allocate_memory_buffer(mtrls, data->mtrl_max);

	    u32 set_count = 0;
	    for (u32 i = 0; i < data->descriptor_sets.count; ++i)
	    {
		R_Mem<Vulkan::Descriptor_Set> tmp_set = get_memory(data->descriptor_sets[i]);
		set_count += tmp_set.size;
	    }
	    
	    allocate_memory_buffer(sets, set_count);
	    for (u32 i = 0; i < sets.count; ++i)
	    {
		u32 p = i;
		R_Mem<Vulkan::Descriptor_Set> tmp_set = get_memory(data->descriptor_sets[i]);
		for (; i < p + tmp_set.size; ++i)
		{
		    sets[i] = tmp_set.p[i].set;
		}
	    }

	    ppln = get_memory(data->ppln_id);
	    mtrl_unique_data_stage_dst = data->mtrl_unique_data_stage_dst;

	    Vulkan::allocate_command_buffers(cmdpool
						 , VK_COMMAND_BUFFER_LEVEL_SECONDARY
						 , gpu
						 , Memory_Buffer_View<VkCommandBuffer>{1, &rndr_cmdbuf});
	}
	
	void
	update(const Memory_Buffer_View<VkDescriptorSet> &additional_sets
	       , VkDescriptorSet cubemap_test)
	{
	    Vulkan::begin_command_buffer(&rndr_cmdbuf, 0, nullptr);
	    
	    switch(mthd)
	    {
		// inline means NOT instanced
	    case Render_Method::INLINE:
		{
		    Vulkan::command_buffer_bind_pipeline(ppln.p, &rndr_cmdbuf);

		    // testing stuff
		    VkDescriptorSet sets[] {additional_sets[0], cubemap_test};
		    
		    Vulkan::command_buffer_bind_descriptor_sets(ppln.p
								    , Memory_Buffer_View<VkDescriptorSet>{2, sets}
								    , &rndr_cmdbuf);

		    for (u32 i = 0; i < mtrl_count; ++i)
		    {
			Material *mtrl = &mtrls[i];

			Memory_Buffer_View<VkDeviceSize> offsets;
			allocate_memory_buffer_tmp(offsets, mtrl->model.p->binding_count);
			offsets.zero();
			
			Vulkan::command_buffer_bind_vbos(mtrl->model.p->raw_cache_for_rendering
							     , offsets
							     , 0
							     , mtrl->model.p->binding_count
							     , &rndr_cmdbuf);
			
			Vulkan::command_buffer_bind_ibo(mtrl->model.p->index_data
							    , &rndr_cmdbuf);

			Vulkan::command_buffer_push_constant(mtrl->data
								 , mtrl->data_size
								 , 0
								 , mtrl_unique_data_stage_dst
								 , ppln.p
								 , &rndr_cmdbuf);

			Vulkan::command_buffer_draw_indexed(&rndr_cmdbuf
								, mtrl->draw_info);
		    }
		}break;
	    case Render_Method::INSTANCED:
		{
		    
		}break;
	    };

	    Vulkan::end_command_buffer(&rndr_cmdbuf);
	}
    };

    internal void
    render_skybox(VkCommandBuffer *cmdbuf
		  , VkDescriptorSet ubuffer
		  , const glm::vec3 &player_position)
    {
	R_Mem<Vulkan::Model> cube					= get_memory("vulkan_model.test_model"_hash);
	R_Mem<Vulkan::Graphics_Pipeline> skybox_pipeline		= get_memory("pipeline.render_atmosphere"_hash);
	R_Mem<Vulkan::Descriptor_Set> atmosphere_descriptor_set	= get_memory("descriptor_set.cubemap"_hash);

	Vulkan::command_buffer_bind_pipeline(skybox_pipeline.p
						 , cmdbuf);

	VkDescriptorSet sets[] = {ubuffer, atmosphere_descriptor_set.p->set};
	
	Vulkan::command_buffer_bind_descriptor_sets(skybox_pipeline.p
							, Memory_Buffer_View<VkDescriptorSet>{2, sets}
							, cmdbuf);

	Memory_Buffer_View<VkDeviceSize> offsets;
	allocate_memory_buffer_tmp(offsets, 1);
	offsets.zero();
	Vulkan::command_buffer_bind_vbos(cube.p->raw_cache_for_rendering
					     , offsets
					     , 0
					     , cube.p->binding_count
					     , cmdbuf);

	Vulkan::command_buffer_bind_ibo(cube.p->index_data
					    , cmdbuf);

	struct Skybox_Push_Constant
	{
	    glm::mat4 model_matrix;
	} push_k;

	push_k.model_matrix = glm::translate(player_position);

	Vulkan::command_buffer_push_constant(&push_k
						 , sizeof(push_k)
						 , 0
						 , VK_SHADER_STAGE_VERTEX_BIT
						 , skybox_pipeline.p
						 , cmdbuf);

	Vulkan::command_buffer_draw_indexed(cmdbuf
						, cube.p->index_data.init_draw_indexed_data(0, 0));
    }
    
    global_var struct Render_System
    {
	static constexpr u32 MAX_RNDRS = 20;
	
	// 1 renderer per material type
	Memory_Buffer_View<Renderer> rndrs;
	u32 rndr_count;
	Hash_Table_Inline<s32, 30, 4, 3> rndr_id_map;


	R_Mem<Vulkan::Render_Pass> deferred_rndr_pass;
	R_Mem<Vulkan::Framebuffer> fbos;
	R_Mem<Vulkan::Graphics_Pipeline> deferred_pipeline;
	R_Mem<VkDescriptorSetLayout> deferred_descriptor_set_layout;
	R_Mem<Vulkan::Descriptor_Set> deferred_descriptor_set;
	R_Mem<Vulkan::Buffer> ppfx_quad;
	
	
	Render_System(void) :rndr_id_map("") {}

	void
	init_system(Vulkan::Swapchain *swapchain
		    , Vulkan::GPU *gpu)
	{
	    allocate_memory_buffer(rndrs, MAX_RNDRS);
	    rndr_count = 0;
	}
	
	void
	add_renderer(Renderer_Init_Data *init_data, VkCommandPool *cmdpool, Vulkan::GPU *gpu)
	{
	    rndrs[rndr_count].init(init_data, cmdpool, gpu);
	    rndr_id_map.insert(init_data->rndr_id.hash, rndr_count++);
	}
	
	void
	update(VkCommandBuffer *cmdbuf
	       , VkExtent2D swapchain_extent
	       , u32 image_index
	       , const Memory_Buffer_View<VkDescriptorSet> &additional_sets
	       , R_Mem<Vulkan::Render_Pass> rndr_pass
	       , const glm::vec3 &player_position
	       , const glm::vec4 &light_position)
	{
	    VkClearValue clears[] {Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
		    , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
		    , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
		    , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
		    , Vulkan::init_clear_color_depth(1.0f, 0)};
	    
	    Vulkan::command_buffer_begin_render_pass(deferred_rndr_pass.p
							 , &fbos.p[image_index]
							 , Vulkan::init_render_area({0, 0}, swapchain_extent)
							 , Memory_Buffer_View<VkClearValue> {sizeof(clears) / sizeof(clears[0]), clears}
							 , VK_SUBPASS_CONTENTS_INLINE
							 , cmdbuf);

	    Memory_Buffer_View<VkCommandBuffer> cmds;
	    allocate_memory_buffer_tmp(cmds, rndr_count);
	    cmds.zero();

	    R_Mem<Vulkan::Descriptor_Set> atmosphere_descriptor_set = get_memory("descriptor_set.cubemap"_hash);
	    
	    for (u32 i = 0; i < rndr_count; ++i)
	    {
		rndrs.buffer[i].update(additional_sets, atmosphere_descriptor_set.p->set);
		cmds.buffer[i] = rndrs.buffer[i].rndr_cmdbuf;
	    }

	    Vulkan::command_buffer_execute_commands(cmdbuf
							, cmds);



	    // render skybox
	    glm::vec3 p = {};
	    render_skybox(cmdbuf, additional_sets[0], p);


	    
	    Vulkan::command_buffer_next_subpass(cmdbuf
						    , VK_SUBPASS_CONTENTS_INLINE);
	    
	    Vulkan::command_buffer_bind_pipeline(deferred_pipeline.p
						     , cmdbuf);

	    VkDescriptorSet deferred_sets[] = {deferred_descriptor_set.p->set, atmosphere_descriptor_set.p->set};
	    
	    Vulkan::command_buffer_bind_descriptor_sets(deferred_pipeline.p
							    , Memory_Buffer_View<VkDescriptorSet>{2, deferred_sets}
							    , cmdbuf);

	    struct Deferred_Lighting_Push_K
	    {
		glm::vec4 light_position;
	    } deferred_push_k;

	    deferred_push_k.light_position = light_position;
	    
	    Vulkan::command_buffer_push_constant(&deferred_push_k
						     , sizeof(deferred_push_k)
						     , 0
						     , VK_SHADER_STAGE_FRAGMENT_BIT
						     , deferred_pipeline.p
						     , cmdbuf);
	    
	    Vulkan::command_buffer_draw(cmdbuf
					    , 3, 1, 0, 0);
	    
	    Vulkan::command_buffer_end_render_pass(cmdbuf);
	}

	Material_Access
	init_material(const Constant_String &rndr_id
		      , const Material_Data *i)
	{
	    s32 rndr_index = *rndr_id_map.get(rndr_id.hash);
	    Renderer *rndr = &rndrs[rndr_index];

	    s32 mtrl_index = rndr->mtrl_count;
	    Material *mtrl = &rndr->mtrls[mtrl_index];
	    mtrl->data = i->data;
	    mtrl->data_size = i->data_size;

	    mtrl->model = i->model;
	    mtrl->draw_info = i->draw_info;

	    ++rndr->mtrl_count;

	    return(Material_Access{rndr_index, mtrl_index});
	}
    } rndr_sys;
    
    void
    init_deferred_render_pass(Vulkan::GPU *gpu
			      , Vulkan::Swapchain *swapchain)
    {
	load_render_passes_from_json(gpu, swapchain);
	rndr_sys.deferred_rndr_pass = get_memory("render_pass.deferred_render_pass"_hash);
    }

    void
    init_deferred_fbos(Vulkan::GPU *gpu
		       , Vulkan::Swapchain *swapchain
		       , R_Mem<Vulkan::Render_Pass> rndr_pass)
    {
	// fbos dont work yet
	
	load_framebuffers_from_json(gpu, swapchain);
	rndr_sys.fbos = get_memory("framebuffer.main_fbo"_hash);
    }
            
    void
    init_deferred_descriptor_sets(Vulkan::GPU *gpu
				  , Vulkan::Swapchain *swapchain)
    {
	// create descriptor set
	rndr_sys.deferred_descriptor_set_layout = register_memory("descriptor_set_layout.deferred_layout"_hash, sizeof(VkDescriptorSetLayout));
	    
	VkDescriptorSetLayoutBinding bindings[] = 
	{
	    Vulkan::init_descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
	    , Vulkan::init_descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
	    , Vulkan::init_descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 2, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
	};
	
	Vulkan::init_descriptor_set_layout(Memory_Buffer_View<VkDescriptorSetLayoutBinding>{3, bindings}
						   , gpu
						   , rndr_sys.deferred_descriptor_set_layout.p);

	R_Mem<Vulkan::Descriptor_Pool> descriptor_pool = register_memory("descriptor_pool.deferred_sets_pool"_hash, sizeof(Vulkan::Descriptor_Pool));
	rndr_sys.deferred_descriptor_set = register_memory("descriptor_set.deferred_descriptor_sets"_hash, sizeof(Vulkan::Descriptor_Set));

	VkDescriptorPoolSize pool_sizes[1] = {};

	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 4, &pool_sizes[0]);
	Vulkan::init_descriptor_pool(Memory_Buffer_View<VkDescriptorPoolSize>{1, pool_sizes}, 1, gpu, descriptor_pool.p);

	Vulkan::Descriptor_Set *single_set = rndr_sys.deferred_descriptor_set.p;
	auto sets_sb = single_buffer(&single_set);
	
	Vulkan::allocate_descriptor_sets(sets_sb
					     , Memory_Buffer_View<VkDescriptorSetLayout>{1, rndr_sys.deferred_descriptor_set_layout.p}
					     , gpu
					     , &descriptor_pool.p->pool);

	VkDescriptorImageInfo image_infos [3] = {};
	R_Mem<Vulkan::Image2D> albedo_image = get_memory("image2D.fbo_albedo"_hash);
	R_Mem<Vulkan::Image2D> position_image = get_memory("image2D.fbo_position"_hash);
	R_Mem<Vulkan::Image2D> normal_image = get_memory("image2D.fbo_normal"_hash);

	Vulkan::init_descriptor_set_image_info(albedo_image.p->image_sampler, albedo_image.p->image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &image_infos[0]);
	Vulkan::init_descriptor_set_image_info(position_image.p->image_sampler, position_image.p->image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &image_infos[1]);
	Vulkan::init_descriptor_set_image_info(normal_image.p->image_sampler, normal_image.p->image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &image_infos[2]);

	VkWriteDescriptorSet descriptor_writes[3] = {};
	Vulkan::init_input_attachment_descriptor_set_write(rndr_sys.deferred_descriptor_set.p, 0, 0, 1, &image_infos[0], &descriptor_writes[0]);
	Vulkan::init_input_attachment_descriptor_set_write(rndr_sys.deferred_descriptor_set.p, 1, 0, 1, &image_infos[1], &descriptor_writes[1]);
	Vulkan::init_input_attachment_descriptor_set_write(rndr_sys.deferred_descriptor_set.p, 2, 0, 1, &image_infos[2], &descriptor_writes[2]);

	Vulkan::update_descriptor_sets(Memory_Buffer_View<VkWriteDescriptorSet>{3, descriptor_writes}, gpu);
	//	rndr_sys.deferred_pipeline = Vulkan::get_object("pipeline.deferred_pipeline"_hash);
    }

    void
    init_render_passes_from_json(Vulkan::Swapchain *swapchain
				 , Vulkan::GPU *gpu)
    {
	load_render_passes_from_json(gpu, swapchain);
	
	rndr_sys.deferred_rndr_pass = get_memory("render_pass.deferred_render_pass"_hash);	
    }

    void
    init_framebuffers_from_json(Vulkan::Swapchain *swapchain
				, Vulkan::GPU *gpu)
    {
	load_framebuffers_from_json(gpu, swapchain);
	
	rndr_sys.fbos = get_memory("framebuffer.main_fbo"_hash);
    }

    // not from JSON yet
    void
    init_descriptor_sets_and_layouts(Vulkan::Swapchain *swapchain
				     , Vulkan::GPU *gpu)
    {
	init_deferred_descriptor_sets(gpu, swapchain);
    }
    
    void
    init_pipelines_from_json(Vulkan::Swapchain *swapchain
			     , Vulkan::GPU *gpu)
    {
	load_pipelines_from_json(gpu, swapchain);

	rndr_sys.deferred_pipeline = get_memory("pipeline.deferred_pipeline"_hash);
    }
    
    void
    init_rendering_system(Vulkan::Swapchain *swapchain, Vulkan::GPU *gpu, R_Mem<Vulkan::Render_Pass> rndr_pass)
    {
	rndr_sys.init_system(swapchain, gpu);
    }

    void
    add_renderer(Renderer_Init_Data *init_data, VkCommandPool *cmdpool, Vulkan::GPU *gpu)
    {
	rndr_sys.add_renderer(init_data, cmdpool, gpu);
    }

    void
    update_renderers(VkCommandBuffer *cmdbuf
		     , VkExtent2D swapchain_extent
		     , u32 image_index
		     , const Memory_Buffer_View<VkDescriptorSet> &additional_sets
		     , R_Mem<Vulkan::Render_Pass> rndr_pass
		     , const glm::vec3 &player_position
		     , const glm::vec4 &light_position)
    {
	rndr_sys.update(cmdbuf, swapchain_extent, image_index, additional_sets, rndr_pass, player_position, light_position);
    }

    Material_Access
    init_material(const Constant_String &id
		  , const Material_Data *data)
    {
	return(rndr_sys.init_material(id, data));
    }

}
