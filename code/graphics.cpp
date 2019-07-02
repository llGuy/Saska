#include "core.hpp"
#include "vulkan.hpp"

#include "script.hpp"

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

void
make_texture(Vulkan::Image2D *img, u32 w, u32 h, VkFormat format, u32 layer_count, VkImageUsageFlags usage, u32 dimensions, Vulkan::GPU *gpu)
{
    VkImageCreateFlags flags = (dimensions == 3) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    
    Vulkan::init_image(w, h, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, layer_count, gpu, img, flags);

    VkImageAspectFlags aspect_flags;
    
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageViewType view_type = (dimensions == 3) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    
    Vulkan::init_image_view(&img->image, format, aspect_flags, gpu, &img->image_view, view_type, layer_count);

    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        Vulkan::init_image_sampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                   , VK_FALSE
                                   , 1
                                   , VK_BORDER_COLOR_INT_OPAQUE_BLACK
                                   , VK_TRUE
                                   , (VkCompareOp)0
                                   , VK_SAMPLER_MIPMAP_MODE_LINEAR
                                   , 0.0f, 0.0f, 0.0f
                                   , gpu, &img->image_sampler);
    }
}

void
make_framebuffer(Vulkan::Framebuffer *fbo
                 , u32 w, u32 h
                 , u32 layer_count
                 , Vulkan::Render_Pass *compatible
                 , const Memory_Buffer_View<Vulkan::Image2D> &colors
                 , Vulkan::Image2D *depth
                 , Vulkan::GPU *gpu)
{
    if (colors.count)
    {
        allocate_memory_buffer(fbo->color_attachments, colors.count);
        for (u32 i = 0; i < colors.count; ++i)
        {
            fbo->color_attachments[i] = colors[i].image_view;
        }
    }
    else
    {
        fbo->color_attachments.buffer = nullptr;
        fbo->color_attachments.count = 0;
    }
    if (depth)
    {
        fbo->depth_attachment = depth->image_view;
    }
    
    Vulkan::init_framebuffer(compatible, w, h, layer_count, gpu, fbo);

    fbo->extent = VkExtent2D{ w, h };
}

struct Render_Pass_Attachment
{
    VkFormat format;
    // Initial layout is always undefined
    VkImageLayout final_layout;
};

struct Render_Pass_Attachment_Reference
{
    u32 index;
    VkImageLayout layout;
};

struct Render_Pass_Subpass
{
    static constexpr u32 MAX_COLOR_ATTACHMENTS = 7;
    Render_Pass_Attachment_Reference color_attachments[MAX_COLOR_ATTACHMENTS];
    u32 color_attachment_count {0};

    Render_Pass_Attachment_Reference depth_attachment;
    bool enable_depth {0};

    Render_Pass_Attachment_Reference input_attachments[MAX_COLOR_ATTACHMENTS];
    u32 input_attachment_count {0};

    template <typename ...T> void
    set_color_attachment_references(const T &...ts)
    {
        Render_Pass_Attachment_Reference references[] { ts... };
        for (u32 i = 0; i < sizeof...(ts); ++i)
        {
            color_attachments[i] = references[i];
        }
        color_attachment_count = sizeof...(ts);
    }

    template <typename ...T> void
    set_input_attachment_references(const T &...ts)
    {
        Render_Pass_Attachment_Reference references[] { ts... };
        for (u32 i = 0; i < sizeof...(ts); ++i)
        {
            input_attachments[i] = references[i];
        }
        input_attachment_count = sizeof...(ts);
    }
};

struct Render_Pass_Dependency
{
    s32 src_index;
    VkPipelineStageFlags src_stage;
    u32 src_access;

    s32 dst_index;
    VkPipelineStageFlags dst_stage;
    u32 dst_access;
};

Render_Pass_Dependency
make_render_pass_dependency(s32 src_index,
                            VkPipelineStageFlags src_stage,
                            u32 src_access,
                            s32 dst_index,
                            VkPipelineStageFlags dst_stage,
                            u32 dst_access)
{
    return Render_Pass_Dependency{ src_index, src_stage, src_access, dst_index, dst_stage, dst_access };
}

void
make_render_pass(Vulkan::Render_Pass *render_pass
                 , const Memory_Buffer_View<Render_Pass_Attachment> &attachments
                 , const Memory_Buffer_View<Render_Pass_Subpass> &subpasses
                 , const Memory_Buffer_View<Render_Pass_Dependency> &dependencies
                 , Vulkan::GPU *gpu)
{
    VkAttachmentDescription descriptions_vk[10] = {};
    u32 att_i = 0;
    for (; att_i < attachments.count; ++att_i)
    {
        descriptions_vk[att_i] = Vulkan::init_attachment_description(attachments[att_i].format
                                                                     , VK_SAMPLE_COUNT_1_BIT
                                                                     , VK_ATTACHMENT_LOAD_OP_CLEAR
                                                                     , VK_ATTACHMENT_STORE_OP_STORE
                                                                     , VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                                     , VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                                     , VK_IMAGE_LAYOUT_UNDEFINED
                                                                     , attachments[att_i].final_layout);
    }
    // Max is 5
    VkSubpassDescription subpasses_vk[5] = {};
    u32 sub_i = 0;
    VkAttachmentReference reference_buffer[30] = {};
    u32 reference_count = 0;
    for (; sub_i < subpasses.count; ++sub_i)
    {
        // Max is 10
        u32 ref_i = 0;
        u32 color_reference_start = reference_count;
        for (; ref_i < subpasses[sub_i].color_attachment_count; ++ref_i, ++reference_count)
        {
            reference_buffer[reference_count] = Vulkan::init_attachment_reference(subpasses[sub_i].color_attachments[ref_i].index,
                                                                                  subpasses[sub_i].color_attachments[ref_i].layout);
        }

        u32 input_reference_start = reference_count;
        u32 inp_i = 0;
        for (; inp_i < subpasses[sub_i].input_attachment_count; ++inp_i, ++reference_count)
        {
            reference_buffer[reference_count] = Vulkan::init_attachment_reference(subpasses[sub_i].input_attachments[inp_i].index,
                                                                                  subpasses[sub_i].input_attachments[inp_i].layout);
        }

        u32 depth_reference_ptr = reference_count;
        if (subpasses[sub_i].enable_depth)
        {
            reference_buffer[reference_count++] = Vulkan::init_attachment_reference(subpasses[sub_i].depth_attachment.index,
                                                                                    subpasses[sub_i].depth_attachment.layout);
        }
        
        subpasses_vk[sub_i] = Vulkan::init_subpass_description({ref_i, &reference_buffer[color_reference_start]}, subpasses[sub_i].enable_depth ? &reference_buffer[depth_reference_ptr] : nullptr, {inp_i, &reference_buffer[input_reference_start]});
    }

    VkSubpassDependency dependencies_vk[10] = {};
    u32 dep_i = 0;
    for (; dep_i < dependencies.count; ++dep_i)
    {
        const Render_Pass_Dependency *info = &dependencies[dep_i];
        dependencies_vk[dep_i] = Vulkan::init_subpass_dependency(info->src_index, info->dst_index
                                                                 , info->src_stage, info->src_access
                                                                 , info->dst_stage, info->dst_access
                                                                 , VK_DEPENDENCY_BY_REGION_BIT);
    }
    
    Vulkan::init_render_pass({ att_i, descriptions_vk }, {sub_i, subpasses_vk}, {dep_i, dependencies_vk}, gpu, render_pass);
}

struct Shader_Module_Info
{
    const char *filename;
    VkShaderStageFlagBits stage;
};

struct Shader_Modules
{
    static constexpr u32 MAX_SHADERS = 5;
    Shader_Module_Info modules[MAX_SHADERS];
    u32 count;
    
    template <typename ...T>
    Shader_Modules(T ...modules_p)
        : modules{modules_p...}, count(sizeof...(modules_p))
    {
    }
};

struct Shader_Uniform_Layouts
{
    static constexpr u32 MAX_LAYOUTS = 10;
    Uniform_Layout_Handle layouts[MAX_LAYOUTS];
    u32 count;
    
    template <typename ...T>
    Shader_Uniform_Layouts(T ...layouts_p)
        : layouts{layouts_p...}, count(sizeof...(layouts_p))
    {
    }
};

struct Shader_PK_Data
{
    u32 size;
    u32 offset;
    VkShaderStageFlagBits stages;
};

struct Shader_Blend_States
{
    static constexpr u32 MAX_BLEND_STATES = 10;
    // For now, is just boolean
    bool blend_states[MAX_BLEND_STATES];
    u32 count;
    
    template <typename ...T>
    Shader_Blend_States(T ...states)
        : blend_states{states...}, count(sizeof...(states))
    {
    }
};

struct Dynamic_States
{
    static constexpr u32 MAX_DYNAMIC_STATES = 10;
    VkDynamicState dynamic_states[MAX_DYNAMIC_STATES];
    u32 count;

    template <typename ...T>
    Dynamic_States(T ...states)
        : dynamic_states{states...}, count(sizeof...(states))
    {
    }
};

// ---- TODO : Finish this tomorrow ----
void
make_graphics_pipeline(Vulkan::Graphics_Pipeline *ppln
                       , const Shader_Modules &modules
                       , bool primitive_restart, VkPrimitiveTopology topology
                       , VkPolygonMode polygonmode, VkCullModeFlags culling
                       , Shader_Uniform_Layouts &layouts
                       , const Shader_PK_Data &pk
                       , VkExtent2D viewport
                       , const Shader_Blend_States &blends
                       , Vulkan::Model *model
                       , bool enable_depth
                       , f32 depth_bias
                       , const Dynamic_States &dynamic_states
                       , Vulkan::Render_Pass *compatible
                       , u32 subpass
                       , Vulkan::GPU *gpu)
{
    VkShaderModule module_objects[Shader_Modules::MAX_SHADERS] = {};
    VkPipelineShaderStageCreateInfo infos[Shader_Modules::MAX_SHADERS] = {};
    for (u32 i = 0; i < modules.count; ++i)
    {
        File_Contents bytecode = read_file(modules.modules[i].filename);
        Vulkan::init_shader(modules.modules[i].stage, bytecode.size, bytecode.content, gpu, &module_objects[i]);
        Vulkan::init_shader_pipeline_info(&module_objects[i], modules.modules[i].stage, &infos[i]);
    }
    VkPipelineVertexInputStateCreateInfo v_input = {};
    Vulkan::init_pipeline_vertex_input_info(model, &v_input);
    VkPipelineInputAssemblyStateCreateInfo assembly = {};
    Vulkan::init_pipeline_input_assembly_info(0, topology, primitive_restart, &assembly);
    VkPipelineViewportStateCreateInfo view_info = {};
    VkViewport view = {};
    Vulkan::init_viewport(viewport.width, viewport.height, 0.0f, 1.0f, &view);
    VkRect2D scissor = {};
    Vulkan::init_rect_2D(VkOffset2D{}, VkExtent2D{viewport.width, viewport.height}, &scissor);
    Vulkan::init_pipeline_viewport_info({1, &view}, {1, &scissor}, &view_info);
    VkPipelineMultisampleStateCreateInfo multi = {};
    Vulkan::init_pipeline_multisampling_info(VK_SAMPLE_COUNT_1_BIT, 0, &multi);
    VkPipelineColorBlendStateCreateInfo blending_info = {};
    VkPipelineColorBlendAttachmentState blend_states[Shader_Blend_States::MAX_BLEND_STATES];
    for (u32 i = 0; i < blends.count; ++i)
    {
        Vulkan::init_blend_state_attachment(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                                            , VK_FALSE
                                            , VK_BLEND_FACTOR_ONE
                                            , VK_BLEND_FACTOR_ZERO
                                            , VK_BLEND_OP_ADD
                                            , VK_BLEND_FACTOR_ONE
                                            , VK_BLEND_FACTOR_ZERO
                                            , VK_BLEND_OP_ADD
                                            , &blend_states[i]);
    }
    Vulkan::init_pipeline_blending_info(VK_FALSE, VK_LOGIC_OP_COPY, {blends.count, blend_states}, &blending_info);
    VkPipelineDynamicStateCreateInfo dynamic_info = {};
    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_info.dynamicStateCount = dynamic_states.count;
    dynamic_info.pDynamicStates = dynamic_states.dynamic_states;
    VkPipelineDepthStencilStateCreateInfo depth = {};
    Vulkan::init_pipeline_depth_stencil_info(enable_depth, enable_depth, 0.0f, 1.0f, VK_FALSE, &depth);
    VkPipelineRasterizationStateCreateInfo raster = {};
    Vulkan::init_pipeline_rasterization_info(polygonmode, culling, 2.0f, 0, &raster, depth_bias);

    VkPushConstantRange pk_range = {};
    Vulkan::init_push_constant_range(pk.stages, pk.size, pk.offset, &pk_range);
    Uniform_Layout real_layouts [Shader_Uniform_Layouts::MAX_LAYOUTS] = {};
    for (u32 i = 0; i < layouts.count; ++i)
    {
        real_layouts[i] = *g_uniform_layout_manager.get(layouts.layouts[i]);
    }
    Vulkan::init_pipeline_layout({layouts.count, real_layouts}, {1, &pk_range}, gpu, &ppln->layout);
    Memory_Buffer_View<VkPipelineShaderStageCreateInfo> shaders_mb = {modules.count, infos};
    Vulkan::init_graphics_pipeline(&shaders_mb
                                   , &v_input
                                   , &assembly
                                   , &view_info
                                   , &raster
                                   , &multi
                                   , &blending_info
                                   , &dynamic_info
                                   , &depth, &ppln->layout, compatible, subpass, gpu, &ppln->pipeline);
}




// Rendering pipeline
struct Deferred_Rendering
{
    Render_Pass_Handle dfr_render_pass;
    // At the moment, dfr_framebuffer points to multiple because it is the bound to swapchain - in future, change
    Framebuffer_Handle dfr_framebuffer;
    Pipeline_Handle dfr_lighting_ppln;
    Uniform_Group_Handle dfr_subpass_group;
    Uniform_Group_Handle dfr_g_buffer_group;
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
    persist constexpr u32 CUBEMAP_W = 1000, CUBEMAP_H = 1000;
    
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
    // ---- Make render pass ----
    auto *atmosphere_render_pass = g_render_pass_manager.get(g_atmosphere.make_render_pass);
    /*    g_atmosphere.make_render_pass = g_render_pass_manager.add("render_pass.atmosphere_render_pass"_hash);
    auto *atmosphere_render_pass = g_render_pass_manager.get(g_atmosphere.make_render_pass);
    {
        // ---- Set render pass attachment data ----
        Render_Pass_Attachment cubemap_attachment {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // ---- Set render pass subpass data ----
        Render_Pass_Subpass subpass = {};
        subpass.set_color_attachment_references(Render_Pass_Attachment_Reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        // ---- Set render pass dependencies data ----
        Render_Pass_Dependency dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(atmosphere_render_pass, {1, &cubemap_attachment}, {1, &subpass}, {2, dependencies}, gpu);
        }*/
    
    //    g_atmosphere.make_render_pass      = g_render_pass_manager.get_handle("render_pass.atmosphere_render_pass"_hash);

    // ---- Make atmosphere framebuffer ----
    /*    g_atmosphere.make_fbo = g_framebuffer_manager.add("framebuffer.atmosphere_fbo"_hash);
    auto *atmosphere_fbo = g_framebuffer_manager.get(g_atmosphere.make_fbo);
    {
        Image_Handle atmosphere_cubemap_handle = g_image_manager.add("image2D.atmosphere_cubemap"_hash);
        auto *cubemap = g_image_manager.get(atmosphere_cubemap_handle);
        make_texture(cubemap, Atmosphere::CUBEMAP_W, Atmosphere::CUBEMAP_W, VK_FORMAT_R8G8B8A8_UNORM, 6, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3, gpu);
        
        make_framebuffer(atmosphere_fbo, Atmosphere::CUBEMAP_W, Atmosphere::CUBEMAP_H, 6, atmosphere_render_pass, {1, cubemap}, nullptr, gpu);
        }*/
    
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
make_dfr_rendering_data(Vulkan::GPU *gpu)
{
    // ---- Make deferred rendering render pass ----
    g_dfr_rendering.dfr_render_pass = g_render_pass_manager.add("render_pass.deferred_render_pass"_hash);
    auto *dfr_render_pass = g_render_pass_manager.get(g_dfr_rendering.dfr_render_pass);
    {
        Render_Pass_Attachment attachments[] = { Render_Pass_Attachment{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 Render_Pass_Attachment{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 Render_Pass_Attachment{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 Render_Pass_Attachment{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 Render_Pass_Attachment{gpu->supported_depth_format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} };
        Render_Pass_Subpass subpasses[2] = {};
        subpasses[0].set_color_attachment_references(Render_Pass_Attachment_Reference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     Render_Pass_Attachment_Reference{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     Render_Pass_Attachment_Reference{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     Render_Pass_Attachment_Reference{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        subpasses[0].enable_depth = 1;
        subpasses[0].depth_attachment = Render_Pass_Attachment_Reference{ 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        
        subpasses[1].set_color_attachment_references(Render_Pass_Attachment_Reference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        
        subpasses[1].set_input_attachment_references(Render_Pass_Attachment_Reference{ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     Render_Pass_Attachment_Reference{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     Render_Pass_Attachment_Reference{ 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        Render_Pass_Dependency dependencies[3] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        dependencies[2] = make_render_pass_dependency(1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(dfr_render_pass, {5, attachments}, {2, subpasses}, {3, dependencies}, gpu);
    }
}

void
make_rendering_pipeline_data(Vulkan::GPU *gpu
                             , VkDescriptorPool *pool
                             , VkCommandPool *cmdpool
                             , Vulkan::Swapchain *swapchain)
{
    g_dfr_rendering.dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
    //    make_dfr_rendering_data(gpu);

    g_dfr_rendering.dfr_framebuffer = g_framebuffer_manager.get_handle("framebuffer.deferred_fbo"_hash);
    g_dfr_rendering.dfr_lighting_ppln = g_pipeline_manager.get_handle("pipeline.deferred_pipeline"_hash);
    g_dfr_rendering.dfr_subpass_group = g_uniform_group_manager.get_handle("descriptor_set.deferred_descriptor_sets"_hash);
    g_dfr_rendering.dfr_g_buffer_group = g_uniform_group_manager.get_handle("uniform_group.g_buffer"_hash);

    make_shadow_data();
    make_atmosphere_data(gpu, pool, cmdpool);

    make_postfx_data(gpu, swapchain);
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
                             , g_dfr_rendering.dfr_framebuffer
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

struct Post_Processing
{
    // This is the FBO that gets rendered to the screen (array of 3)
    Framebuffer_Handle screen_fbo;
    Render_Pass_Handle final_render_pass;

    // There are different post processing effects : SSR, Bloom, Godrays...
    // For now, just implement SSR
    Pipeline_Handle ssr_ppln;
    Uniform_Group_Handle prev_tex_uniform;
} g_postfx;

void
make_postfx_data(Vulkan::GPU *gpu
                 , Vulkan::Swapchain *swapchain)
{
    // ---- Make the final Render Pass ----
    g_postfx.screen_fbo = g_framebuffer_manager.add("framebuffer.final_fbo"_hash, swapchain->views.count);
    
    g_postfx.final_render_pass = g_render_pass_manager.add("render_pass.final_render_pass"_hash);
    auto *final_render_pass = g_render_pass_manager.get(g_postfx.final_render_pass);
    {
        // Only one attachment
        Render_Pass_Attachment attachment = { swapchain->format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        Render_Pass_Subpass subpass;
        subpass.set_color_attachment_references(Render_Pass_Attachment_Reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        Render_Pass_Dependency dependencies[2];
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(final_render_pass, {1, &attachment}, {1, &subpass}, {2, dependencies}, gpu);
    }
    // ---- Make the framebuffer for the swapchain / screen ----
    // ---- Only has one color attachment, no depth attachment ----
    auto *final_fbo = g_framebuffer_manager.get(g_postfx.screen_fbo);
    {    
        for (u32 i = 0; i < swapchain->views.count; ++i)
        {
            final_fbo[i].extent = swapchain->extent;
            
            allocate_memory_buffer(final_fbo[i].color_attachments, 1);
            final_fbo[i].color_attachments[0] = swapchain->views[i];
            final_fbo[i].depth_attachment = VK_NULL_HANDLE;
            Vulkan::init_framebuffer(final_render_pass, final_fbo[i].extent.width, final_fbo[i].extent.height, 1, gpu, &final_fbo[i]);
        }
    }
    // ---- Make graphics pipeline for final render pass ----
    g_postfx.ssr_ppln = g_pipeline_manager.add("graphics_pipeline.pfx_ssr"_hash);
    auto *ssr_ppln = g_pipeline_manager.get(g_postfx.ssr_ppln);
    {
        Shader_Modules modules(Shader_Module_Info{"shaders/pfx_ssr.vert", VK_SHADER_STAGE_VERTEX_BIT}, Shader_Module_Info{"shaders/pfx_ssr.frag", VK_SHADER_STAGE_FRAGMENT_BIT});
        Shader_Uniform_Layouts layouts(g_uniform_layout_manager.get_handle("descriptor_set_layout.g_buffer_layout"_hash)
                                       , g_uniform_layout_manager.get_handle("descriptor_set_layout.render_atmosphere_layout"_hash)
                                       , g_uniform_layout_manager.get_handle("descriptor_set_layout.test_descriptor_set_layout"_hash));
        Shader_PK_Data push_k {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        Shader_Blend_States blending(false);
        Dynamic_States dynamic (VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(ssr_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL
                               , VK_CULL_MODE_NONE, layouts, push_k, swapchain->extent, blending, nullptr
                               , false, 0.0f, dynamic, final_render_pass, 0, gpu);
    }
}

void
apply_pfx_on_scene(u32 image_index
                   , GPU_Command_Queue *queue
                   , const VkRect2D &render_area
                   , Uniform_Group *transforms_group
                   , const glm::mat4 &view_matrix
                   , const glm::mat4 &projection_matrix
                   , Vulkan::GPU *gpu)
{
    queue->begin_render_pass(g_postfx.final_render_pass
                             , g_postfx.screen_fbo + image_index
                             , VK_SUBPASS_CONTENTS_INLINE
                             , Vulkan::init_clear_color_color(0.0f, 0.0f, 1.0f, 1.0f));
    Vulkan::command_buffer_set_viewport(render_area.extent.width, render_area.extent.height, 0.0f, 1.0f, &queue->q);
    {
        auto *pfx_ssr_ppln = g_pipeline_manager.get(g_postfx.ssr_ppln);
        Vulkan::command_buffer_bind_pipeline(pfx_ssr_ppln, &queue->q);

        auto *g_buffer_group = g_uniform_group_manager.get(g_dfr_rendering.dfr_g_buffer_group);
        auto *atmosphere_group = g_uniform_group_manager.get(g_atmosphere.cubemap_uniform_group);
        
        Uniform_Group groups[] = { *g_buffer_group, *atmosphere_group, *transforms_group };
        Vulkan::command_buffer_bind_descriptor_sets(pfx_ssr_ppln, {3, groups}, &queue->q);

        struct SSR_Lighting_Push_K
        {
            glm::vec4 ws_light_position;
            glm::mat4 view;
            glm::mat4 proj;
        } ssr_pk;

        ssr_pk.ws_light_position = view_matrix * glm::vec4(glm::normalize(-g_lighting.ws_light_position), 0.0f);
        ssr_pk.view = view_matrix;
        ssr_pk.proj = projection_matrix;
        ssr_pk.proj[1][1] *= -1.0f;
        Vulkan::command_buffer_push_constant(&ssr_pk, sizeof(ssr_pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_ssr_ppln, &queue->q);

        Vulkan::command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }
    queue->end_render_pass();
}

void
test(Vulkan::GPU *gpu
     , Vulkan::Swapchain *swapchain
     , u32 index)
{   
    g_atmosphere.make_render_pass = g_render_pass_manager.add("render_pass.atmosphere_render_pass"_hash);
    auto *atmosphere_render_pass = g_render_pass_manager.get(g_atmosphere.make_render_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        Render_Pass_Attachment cubemap_attachment {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // ---- Set render pass subpass data ----
        Render_Pass_Subpass subpass = {};
        subpass.set_color_attachment_references(Render_Pass_Attachment_Reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        // ---- Set render pass dependencies data ----
        Render_Pass_Dependency dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(atmosphere_render_pass, {1, &cubemap_attachment}, {1, &subpass}, {2, dependencies}, gpu);
    }

    g_atmosphere.make_fbo = g_framebuffer_manager.add("framebuffer.atmosphere_fbo"_hash);
    auto *atmosphere_fbo = g_framebuffer_manager.get(g_atmosphere.make_fbo);
    {
        Image_Handle atmosphere_cubemap_handle = g_image_manager.add("image2D.atmosphere_cubemap"_hash);
        auto *cubemap = g_image_manager.get(atmosphere_cubemap_handle);
        make_texture(cubemap, Atmosphere::CUBEMAP_W, Atmosphere::CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3, gpu);

        make_framebuffer(atmosphere_fbo, Atmosphere::CUBEMAP_W, Atmosphere::CUBEMAP_H, 6, atmosphere_render_pass, {1, cubemap}, nullptr, gpu);
    }

    make_dfr_rendering_data(gpu);
}
