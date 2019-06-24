#include "core.hpp"
#include "vulkan.hpp"

#include "graphics.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

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
//using GPU_Command_Queue = VkCommandBuffer;
using GPU_Command_Queue_Pool = VkCommandPool;
// Submit level of a Material Submission Queue Manager which will either submit to a secondary queue or directly into the main queue
using Submit_Level = VkCommandBufferLevel;

GPU_Command_Queue
make_command_queue(VkCommandPool *pool, Submit_Level level, Vulkan::GPU *gpu)
{
    GPU_Command_Queue result;
    Vulkan::allocate_command_buffers(pool, level, gpu, {1, &result.q});
    return(result);
}

void
begin_command_queue(GPU_Command_Queue *queue, Vulkan::GPU *gpu)
{
    Vulkan::begin_command_buffer(&queue->q, 0, nullptr);
}
    
void
end_command_queue(GPU_Command_Queue *queue, Vulkan::GPU *gpu)
{
    Vulkan::end_command_buffer(&queue->q);
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
	inheritance_info.renderPass = g_render_pass_manager.get(main_queue->current_pass_handle)->render_pass;
	inheritance_info.subpass = main_queue->subpass;
	inheritance_info.framebuffer = g_framebuffer_manager.get(main_queue->fbo_handle)->framebuffer;
	
	Vulkan::begin_command_buffer(&dst_command_queue->q
				     , VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
				     , &inheritance_info);
    }
	    
    Vulkan::command_buffer_bind_pipeline(graphics_pipeline, &dst_command_queue->q);

    Vulkan::command_buffer_bind_descriptor_sets(graphics_pipeline
						, uniform_groups
						, &dst_command_queue->q);

    for (u32 i = 0; i < mtrl_count; ++i)
    {
        Material *mtrl = &mtrls[i];

        VkDeviceSize *zero = ALLOCA_T(VkDeviceSize, mtrl->vbo_bindings.count);
        for (u32 z = 0; z < mtrl->vbo_bindings.count; ++z) zero[z] = 0;
			
        Vulkan::command_buffer_bind_vbos(mtrl->vbo_bindings
                                         , {mtrl->vbo_bindings.count, zero}
                                         , 0
                                         , mtrl->vbo_bindings.count
                                         , &dst_command_queue->q);
			
        Vulkan::command_buffer_bind_ibo(mtrl->index_data
                                        , &dst_command_queue->q);

        Vulkan::command_buffer_push_constant(mtrl->push_k_ptr
                                             , mtrl->push_k_size
                                             , 0
                                             , push_k_dst
                                             , graphics_pipeline
                                             , &dst_command_queue->q);

        Vulkan::command_buffer_draw_indexed(&dst_command_queue->q
                                            , mtrl->draw_index_data);
    }

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	Vulkan::end_command_buffer(&dst_command_queue->q);
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
    //    Vulkan::command_buffer_execute_commands(&queue->q, {1, get_command_buffer(nullptr)});
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
    //    Vulkan::command_buffer_execute_commands(queue, {material_queue_manager.active_queue_ptr, material_queue_manager.active_queues});
}



// Rendering pipeline
struct Deferred_Rendering
{
    Render_Pass_Handle dfr_render_pass;
    // At the moment, dfr_framebuffer points to multiple because it is the bound to swapchain - in future, change
    Framebuffer_Handle dfr_framebuffer;
    Pipeline_Handle dfr_lighting_ppln;
    Uniform_Group_Handle dfr_subpass_group;
} g_dfr_rendering;

// TODO : Need to organise this in the future
struct Lighting
{
    // Default value
    glm::vec3 ws_light_position {0.00000001f, 10.0f, 0.00000001f};

    // Later, need to add PSSM
    struct Shadows
    {
        Framebuffer_Handle fbo;
        Render_Pass_Handle pass;
        Image_Handle map;
        Uniform_Group_Handle set;
    
        Pipeline_Handle model_shadow_ppln;
        Pipeline_Handle terrain_shadow_ppln;
        Pipeline_Handle debug_frustum_ppln;
    
        glm::mat4 light_view_matrix;
        glm::mat4 projection_matrix;
        glm::mat4 inverse_light_view;

        glm::vec4 ls_corners[8];
        
        union
        {
            struct {f32 x_min, x_max, y_min, y_max, z_min, z_max;};
            f32 corner_values[6];
        };
    } shadows;
} g_lighting;

struct Atmosphere
{
    // GPU objects needed to create the atmosphere skybox cubemap
    Render_Pass_Handle make_render_pass;
    Framebuffer_Handle make_fbo;
    Pipeline_Handle make_pipeline;

    // Pipeline needed to render the cubemap to the screen
    Pipeline_Handle render_pipeline;

    // Descriptor set that will be used to sample (should not be used in world.cpp)
    Uniform_Group_Handle cubemap_uniform_group;
} g_atmosphere;

void
update_atmosphere(GPU_Command_Queue *queue)
{
    queue->begin_render_pass(g_atmosphere.make_render_pass
                             , g_atmosphere.make_fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , Vulkan::init_clear_color_color(0, 0.0, 0.0, 0));

    VkViewport viewport;
    Vulkan::init_viewport(1000, 1000, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);    

    auto *make_ppln = g_pipeline_manager.get(g_atmosphere.make_pipeline);
    
    Vulkan::command_buffer_bind_pipeline(make_ppln, &queue->q);

    struct Atmos_Push_K
    {
	alignas(16) glm::mat4 inverse_projection;
	glm::vec4 light_dir;
	glm::vec2 viewport;
    } k;

    glm::mat4 atmos_proj = glm::perspective(glm::radians(90.0f), 1000.0f / 1000.0f, 0.1f, 10000.0f);
    k.inverse_projection = glm::inverse(atmos_proj);
    k.viewport = glm::vec2(1000.0f, 1000.0f);

    k.light_dir = glm::vec4(glm::normalize(-g_lighting.ws_light_position), 1.0f);
    
    Vulkan::command_buffer_push_constant(&k
					 , sizeof(k)
					 , 0
					 , VK_SHADER_STAGE_FRAGMENT_BIT
					 , make_ppln
					 , &queue->q);
    
    Vulkan::command_buffer_draw(&queue->q
				, 1, 1, 0, 0);

    queue->end_render_pass();
}

void
render_atmosphere(const Memory_Buffer_View<Uniform_Group> &sets
                  , const glm::vec3 &camera_position // To change to camera structure
                  , Vulkan::Model *cube
                  , GPU_Command_Queue *queue)
{
    auto *render_pipeline = g_pipeline_manager.get(g_atmosphere.render_pipeline);
    Vulkan::command_buffer_bind_pipeline(render_pipeline, &queue->q);

    Uniform_Group *groups = ALLOCA_T(Uniform_Group, sets.count + 1);
    for (u32 i = 0; i < sets.count; ++i) groups[i] = sets[i];
    groups[sets.count] = *g_uniform_group_manager.get(g_atmosphere.cubemap_uniform_group);
    
    Vulkan::command_buffer_bind_descriptor_sets(render_pipeline, {sets.count + 1, groups}, &queue->q);

    VkDeviceSize zero = 0;
    Vulkan::command_buffer_bind_vbos(cube->raw_cache_for_rendering, {1, &zero}, 0, 1, &queue->q);

    Vulkan::command_buffer_bind_ibo(cube->index_data, &queue->q);

    struct Skybox_Push_Constant
    {
	glm::mat4 model_matrix;
    } push_k;

    push_k.model_matrix = glm::scale(glm::vec3(1000.0f));

    Vulkan::command_buffer_push_constant(&push_k
					 , sizeof(push_k)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , render_pipeline
					 , &queue->q);

    Vulkan::command_buffer_draw_indexed(&queue->q
					, cube->index_data.init_draw_indexed_data(0, 0));
}

internal void
make_atmosphere_data(Vulkan::GPU *gpu
                     , VkDescriptorPool *pool
                     , VkCommandPool *cmdpool)
{
    g_atmosphere.make_render_pass      = g_render_pass_manager.get_handle("render_pass.atmosphere_render_pass"_hash);
    g_atmosphere.make_fbo              = g_framebuffer_manager.get_handle("framebuffer.atmosphere_fbo"_hash);
    g_atmosphere.render_pipeline       = g_pipeline_manager.get_handle("pipeline.render_atmosphere"_hash);
    g_atmosphere.make_pipeline         = g_pipeline_manager.get_handle("pipeline.atmosphere_pipeline"_hash);
    g_atmosphere.cubemap_uniform_group = g_uniform_group_manager.get_handle("descriptor_set.cubemap"_hash);

    {
        VkCommandBuffer cmdbuf;
        Vulkan::init_single_use_command_buffer(cmdpool, gpu, &cmdbuf);

        GPU_Command_Queue queue{cmdbuf};
        update_atmosphere(&queue);
    
        Vulkan::destroy_single_use_command_buffer(&cmdbuf, cmdpool, gpu);
    }
}

internal void
make_shadow_data(void)
{
    glm::vec3 light_pos_normalized = glm::normalize(g_lighting.ws_light_position);
    g_lighting.shadows.light_view_matrix = glm::lookAt(light_pos_normalized, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    g_lighting.shadows.inverse_light_view = glm::inverse(g_lighting.shadows.light_view_matrix);
    
    g_lighting.shadows.fbo                 = g_framebuffer_manager.get_handle("framebuffer.shadow_fbo"_hash);
    g_lighting.shadows.pass                = g_render_pass_manager.get_handle("render_pass.shadow_render_pass"_hash);
    g_lighting.shadows.map                 = g_image_manager.get_handle("image2D.shadow_map"_hash);
    g_lighting.shadows.set                 = g_uniform_group_manager.get_handle("descriptor_set.shadow_map_set"_hash);
    g_lighting.shadows.model_shadow_ppln   = g_pipeline_manager.get_handle("pipeline.model_shadow"_hash);
    g_lighting.shadows.terrain_shadow_ppln = g_pipeline_manager.get_handle("pipeline.terrain_shadow"_hash);
}

Shadow_Matrices
get_shadow_matrices(void)
{
    Shadow_Matrices ret;
    ret.projection_matrix = g_lighting.shadows.projection_matrix;
    ret.light_view_matrix = g_lighting.shadows.light_view_matrix;
    ret.inverse_light_view = g_lighting.shadows.inverse_light_view;
    return(ret);
}

Shadow_Debug
get_shadow_debug(void)
{
    Shadow_Debug ret;    
    for (u32 i = 0; i < 6; ++i)
    {
        ret.corner_values[i] = g_lighting.shadows.corner_values[i];
    }
    for (u32 i = 0; i < 8; ++i)
    {
        ret.frustum_corners[i] = g_lighting.shadows.ls_corners[i];
    }
    return(ret);
}

Shadow_Display
get_shadow_display(void)
{
    auto *texture = g_uniform_group_manager.get(g_lighting.shadows.set);
    Shadow_Display ret{*texture};
    return(ret);
}

void
update_shadows(f32 far, f32 near, f32 fov, f32 aspect
               // Later to replace with a Camera structure
               , const glm::vec3 &ws_p
               , const glm::vec3 &ws_d
               , const glm::vec3 &ws_up)
{
    f32 far_width, near_width, far_height, near_height;
    
    far_width = 2.0f * far * tan(fov);
    near_width = 2.0f * near * tan(fov);
    far_height = far_width / aspect;
    near_height = near_width / aspect;

    glm::vec3 right_view_ax = glm::normalize(glm::cross(ws_d, ws_up));
    glm::vec3 up_view_ax = glm::normalize(glm::cross(ws_d, right_view_ax));

    f32 far_width_half = far_width / 2.0f;
    f32 near_width_half = near_width / 2.0f;
    f32 far_height_half = far_height / 2.0f;
    f32 near_height_half = near_height / 2.0f;

    // f = far, n = near, l = left, r = right, t = top, b = bottom
    enum Ortho_Corner : s32
    {
	flt, flb,
	frt, frb,
	nlt, nlb,
	nrt, nrb
    };    

    // light space

    g_lighting.shadows.ls_corners[flt] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * far - right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    g_lighting.shadows.ls_corners[flb] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * far - right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    g_lighting.shadows.ls_corners[frt] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * far + right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    g_lighting.shadows.ls_corners[frb] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * far + right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    g_lighting.shadows.ls_corners[nlt] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * near - right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    g_lighting.shadows.ls_corners[nlb] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * near - right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);

    g_lighting.shadows.ls_corners[nrt] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * near + right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    g_lighting.shadows.ls_corners[nrb] = g_lighting.shadows.light_view_matrix * glm::vec4(ws_p + ws_d * near + right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);

    f32 x_min, x_max, y_min, y_max, z_min, z_max;

    x_min = x_max = g_lighting.shadows.ls_corners[0].x;
    y_min = y_max = g_lighting.shadows.ls_corners[0].y;
    z_min = z_max = g_lighting.shadows.ls_corners[0].z;

    for (u32 i = 1; i < 8; ++i)
    {
	if (x_min > g_lighting.shadows.ls_corners[i].x) x_min = g_lighting.shadows.ls_corners[i].x;
	if (x_max < g_lighting.shadows.ls_corners[i].x) x_max = g_lighting.shadows.ls_corners[i].x;

	if (y_min > g_lighting.shadows.ls_corners[i].y) y_min = g_lighting.shadows.ls_corners[i].y;
	if (y_max < g_lighting.shadows.ls_corners[i].y) y_max = g_lighting.shadows.ls_corners[i].y;

	if (z_min > g_lighting.shadows.ls_corners[i].z) z_min = g_lighting.shadows.ls_corners[i].z;
	if (z_max < g_lighting.shadows.ls_corners[i].z) z_max = g_lighting.shadows.ls_corners[i].z;
    }

    g_lighting.shadows.x_min = x_min = x_min;
    g_lighting.shadows.x_max = x_max = x_max;
    
    g_lighting.shadows.y_min = y_min = y_min;
    g_lighting.shadows.y_max = y_max = y_max;
    
    g_lighting.shadows.z_min = z_min = z_min;
    g_lighting.shadows.z_max = z_max = z_max;

    g_lighting.shadows.projection_matrix = glm::ortho<f32>(x_min, x_max, y_min, y_max, z_min, z_max);
}

void
make_rendering_pipeline_data(Vulkan::GPU *gpu
                             , VkDescriptorPool *pool
                             , VkCommandPool *cmdpool)
{
    g_dfr_rendering.dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
    g_dfr_rendering.dfr_framebuffer = g_framebuffer_manager.get_handle("framebuffer.main_fbo"_hash);
    g_dfr_rendering.dfr_lighting_ppln = g_pipeline_manager.get_handle("pipeline.deferred_pipeline"_hash);
    g_dfr_rendering.dfr_subpass_group = g_uniform_group_manager.get_handle("descriptor_set.deferred_descriptor_sets"_hash);

    make_shadow_data();
    make_atmosphere_data(gpu, pool, cmdpool);
}

void
begin_shadow_offscreen(u32 shadow_map_width, u32 shadow_map_height
                       , GPU_Command_Queue *queue)
{
    queue->begin_render_pass(g_lighting.shadows.pass
                             , g_lighting.shadows.fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , Vulkan::init_clear_color_depth(1.0f, 0));
    
    VkViewport viewport = {};
    Vulkan::init_viewport(shadow_map_width, shadow_map_height, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);

    vkCmdSetDepthBias(queue->q, 1.25f, 0.0f, 1.75f);

    // Render the world to the shadow map
}

void
end_shadow_offscreen(GPU_Command_Queue *queue)
{
    Vulkan::command_buffer_end_render_pass(&queue->q);
}

void
begin_deferred_rendering(u32 image_index /* To remove in the future */
                         , const VkRect2D &render_area
                         , GPU_Command_Queue *queue)
{
    queue->begin_render_pass(g_dfr_rendering.dfr_render_pass
                             , g_dfr_rendering.dfr_framebuffer + image_index
                             , VK_SUBPASS_CONTENTS_INLINE
                             // Clear values hre
                             , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
                             , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
                             , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
                             , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
                             , Vulkan::init_clear_color_depth(1.0f, 0));

    Vulkan::command_buffer_set_viewport(render_area.extent.width, render_area.extent.height, 0.0f, 1.0f, &queue->q);
    Vulkan::command_buffer_set_line_width(2.0f, &queue->q);

    // User renders what is needed ...
}    

void
end_deferred_rendering(const glm::mat4 &view_matrix // In future, change this to camera structure
                       , GPU_Command_Queue *queue)
{
    queue->next_subpass(VK_SUBPASS_CONTENTS_INLINE);

    auto *dfr_lighting_ppln = g_pipeline_manager.get(g_dfr_rendering.dfr_lighting_ppln);

    Vulkan::command_buffer_bind_pipeline(dfr_lighting_ppln
					 , &queue->q);

    auto *dfr_subpass_group = g_uniform_group_manager.get(g_dfr_rendering.dfr_subpass_group);
    
    VkDescriptorSet deferred_sets[] = {*dfr_subpass_group};
    
    Vulkan::command_buffer_bind_descriptor_sets(dfr_lighting_ppln
						, {1, deferred_sets}
						, &queue->q);
    
    struct Deferred_Lighting_Push_K
    {
	glm::vec4 light_position;
	glm::mat4 view_matrix;
    } deferred_push_k;
    
    deferred_push_k.light_position = glm::vec4(glm::normalize(-g_lighting.ws_light_position), 1.0f);
    deferred_push_k.view_matrix = view_matrix;
    
    Vulkan::command_buffer_push_constant(&deferred_push_k
					 , sizeof(deferred_push_k)
					 , 0
					 , VK_SHADER_STAGE_FRAGMENT_BIT
					 , dfr_lighting_ppln
					 , &queue->q);
    
    Vulkan::command_buffer_draw(&queue->q
                                , 4, 1, 0, 0);

    queue->end_render_pass();
}
