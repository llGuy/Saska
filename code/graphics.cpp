
#include "core.hpp"
#include "vulkan.hpp"

#include "script.hpp"

#include "graphics.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

gpu_buffer_manager_t g_gpu_buffer_manager;
image_manager_t g_image_manager;
framebuffer_manager_t g_framebuffer_manager;
render_pass_manager_t g_render_pass_manager;
pipeline_manager_t g_pipeline_manager;
uniform_layout_manager_t g_uniform_layout_manager;
uniform_group_manager_t g_uniform_group_manager;
model_manager_t g_model_manager;

gpu_command_queue_t
make_command_queue(VkCommandPool *pool, submit_level_t level, gpu_t *gpu)
{
    gpu_command_queue_t result;
    allocate_command_buffers(pool, level, gpu, {1, &result.q});
    return(result);
}

void
begin_command_queue(gpu_command_queue_t *queue, gpu_t *gpu, VkCommandBufferInheritanceInfo *inheritance)
{
    begin_command_buffer(&queue->q,
                         (queue->submit_level == VK_COMMAND_BUFFER_LEVEL_SECONDARY ? VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : 0) | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
                         inheritance);
}
    
void
end_command_queue(gpu_command_queue_t *queue, gpu_t *gpu)
{
    end_command_buffer(&queue->q);
}



// --------------------- Uniform stuff ---------------------
uniform_pool_t g_uniform_pool;

internal void
make_uniform_pool(gpu_t *gpu)
{
    VkDescriptorPoolSize pool_sizes[3] = {};

    init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20, &pool_sizes[0]);
    init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20, &pool_sizes[1]);
    init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 20, &pool_sizes[2]);
    
    init_descriptor_pool(memory_buffer_view_t<VkDescriptorPoolSize>{3, pool_sizes}, 30, gpu, &g_uniform_pool);
}

// Naming is better than Descriptor in case of people familiar with different APIs / also will be useful when introducing other APIs
using uniform_binding_t = VkDescriptorSetLayoutBinding;

uniform_binding_t
make_uniform_binding_s(uint32_t count
		       , uint32_t binding
		       , VkDescriptorType uniform_type
		       , VkShaderStageFlags shader_flags)
{
    return init_descriptor_set_layout_binding(uniform_type, binding, count, shader_flags);
}



void
uniform_layout_info_t::push(const uniform_binding_t &binding_info)
{
    bindings_buffer[binding_count++] = binding_info;
}

void
uniform_layout_info_t::push(uint32_t count
			  , uint32_t binding
			  , VkDescriptorType uniform_type
			  , VkShaderStageFlags shader_flags)
{
    bindings_buffer[binding_count++] = init_descriptor_set_layout_binding(uniform_type, binding, count, shader_flags);
}

using uniform_layout_t = VkDescriptorSetLayout;

uniform_layout_t
make_uniform_layout(uniform_layout_info_t *blueprint, gpu_t *gpu)
{
    VkDescriptorSetLayout layout;
    init_descriptor_set_layout({blueprint->binding_count, blueprint->bindings_buffer}, gpu, &layout);
    return(layout);
}



// Uniform Group is the struct going to be used to alias VkDescriptorSet, and in other APIs, simply groups of uniforms
using uniform_group_t = VkDescriptorSet;

uniform_group_t
make_uniform_group(uniform_layout_t *layout, VkDescriptorPool *pool, gpu_t *gpu)
{
    uniform_group_t uniform_group = allocate_descriptor_set(layout, gpu, pool);

    return(uniform_group);
}




// --------------------- Rendering stuff ---------------------

// will be used for multi-threading the rendering process for extra extra performance !
global_var struct gpu_material_submission_queue_manager_t // maybe in the future this will be called multi-threaded rendering manager
{
    persist constexpr uint32_t MAX_ACTIVE_QUEUES = 10;

    uint32_t active_queue_ptr {0};
    gpu_command_queue_t active_queues[MAX_ACTIVE_QUEUES];
} material_queue_manager;

uint32_t
gpu_material_submission_queue_t::push_material(void *push_k_ptr, uint32_t push_k_size
					     , const memory_buffer_view_t<VkBuffer> &vbo_bindings
					     , const model_index_data_t &index_data
					     , const draw_indexed_data_t &draw_index_data)
{
    material_t new_mtrl = {};
    new_mtrl.push_k_ptr = push_k_ptr;
    new_mtrl.push_k_size = push_k_size;
    new_mtrl.vbo_bindings = vbo_bindings;
    new_mtrl.index_data = index_data;
    new_mtrl.draw_index_data = draw_index_data;

    mtrls[mtrl_count] = new_mtrl;

    return(mtrl_count++);
}

gpu_command_queue_t *
gpu_material_submission_queue_t::get_command_buffer(gpu_command_queue_t *queue)
{
    if (cmdbuf_index >= 0)
    {
	return(&material_queue_manager.active_queues[cmdbuf_index]);
    }
    else return(queue);
}
    
void
gpu_material_submission_queue_t::submit_queued_materials(const memory_buffer_view_t<uniform_group_t> &uniform_groups
						       , graphics_pipeline_t *graphics_pipeline
						       , gpu_command_queue_t *main_queue
						       , submit_level_t level)
{
    // Depends on the cmdbuf_index var, not on the submit level that is given
    gpu_command_queue_t *dst_command_queue = get_command_buffer(main_queue);
    // Now depends also on the submit level
    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) dst_command_queue = main_queue;

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	VkCommandBufferInheritanceInfo inheritance_info = {};
	inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritance_info.renderPass = g_render_pass_manager.get(main_queue->current_pass_handle)->render_pass;
	inheritance_info.subpass = main_queue->subpass;
	inheritance_info.framebuffer = g_framebuffer_manager.get(main_queue->fbo_handle)->framebuffer;
	
	begin_command_buffer(&dst_command_queue->q
				     , VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
				     , &inheritance_info);
    }
	    
    command_buffer_bind_pipeline(graphics_pipeline, &dst_command_queue->q);

    command_buffer_bind_descriptor_sets(graphics_pipeline
						, uniform_groups
						, &dst_command_queue->q);

    for (uint32_t i = 0; i < mtrl_count; ++i)
    {
        material_t *mtrl = &mtrls[i];

        VkDeviceSize *zero = ALLOCA_T(VkDeviceSize, mtrl->vbo_bindings.count);
        for (uint32_t z = 0; z < mtrl->vbo_bindings.count; ++z) zero[z] = 0;
			
        command_buffer_bind_vbos(mtrl->vbo_bindings
                                         , {mtrl->vbo_bindings.count, zero}
                                         , 0
                                         , mtrl->vbo_bindings.count
                                         , &dst_command_queue->q);
			
        command_buffer_bind_ibo(mtrl->index_data
                                        , &dst_command_queue->q);

        command_buffer_push_constant(mtrl->push_k_ptr
                                             , mtrl->push_k_size
                                             , 0
                                             , push_k_dst
                                             , graphics_pipeline
                                             , &dst_command_queue->q);

        command_buffer_draw_indexed(&dst_command_queue->q
                                            , mtrl->draw_index_data);
    }

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	end_command_buffer(&dst_command_queue->q);
    }
}

void
gpu_material_submission_queue_t::flush_queue(void)
{
    mtrl_count = 0;
}

void
gpu_material_submission_queue_t::submit_to_cmdbuf(gpu_command_queue_t *queue)
{
    //    command_buffer_execute_commands(&queue->q, {1, get_command_buffer(nullptr)});
}

gpu_material_submission_queue_t
make_gpu_material_submission_queue(uint32_t max_materials, VkShaderStageFlags push_k_dst // for rendering purposes (quite Vulkan specific)
				   , submit_level_t level, gpu_command_queue_pool_t *pool, gpu_t *gpu)
{
    gpu_material_submission_queue_t material_queue;

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
	gpu_command_queue_t command_queue = make_command_queue(pool, level, gpu);

	material_queue.mtrl_count = 0;
	allocate_memory_buffer(material_queue.mtrls, max_materials);

	material_queue.cmdbuf_index = material_queue_manager.active_queue_ptr;
	material_queue_manager.active_queues[material_queue_manager.active_queue_ptr++] = command_queue;
    }

    return(material_queue);
}

void
submit_queued_materials_from_secondary_queues(gpu_command_queue_t *queue)
{
    //    command_buffer_execute_commands(queue, {material_queue_manager.active_queue_ptr, material_queue_manager.active_queues});
}

void
make_framebuffer_attachment(image2d_t *img, uint32_t w, uint32_t h, VkFormat format, uint32_t layer_count, VkImageUsageFlags usage, uint32_t dimensions, gpu_t *gpu)
{
    VkImageCreateFlags flags = (dimensions == 3) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    
    init_image(w, h, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, layer_count, gpu, img, flags);

    VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageViewType view_type = (dimensions == 3) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    
    init_image_view(&img->image, format, aspect_flags, gpu, &img->image_view, view_type, layer_count);

    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        init_image_sampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                   , VK_FALSE
                                   , 1
                                   , VK_BORDER_COLOR_INT_OPAQUE_BLACK
                                   , VK_FALSE
                                   , (VkCompareOp)0
                                   , VK_SAMPLER_MIPMAP_MODE_LINEAR
                                   , 0.0f, 0.0f, 0.0f
                                   , gpu, &img->image_sampler);
    }
}

void
make_texture(image2d_t *img, uint32_t w, uint32_t h, VkFormat format, uint32_t layer_count, uint32_t dimensions, VkImageUsageFlags usage, VkFilter filter, gpu_t *gpu)
{
    VkImageCreateFlags flags = (dimensions == 3) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    init_image(w, h, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, layer_count, gpu, img, flags);
    VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewType view_type = (dimensions == 3) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    init_image_view(&img->image, format, aspect_flags, gpu, &img->image_view, view_type, layer_count);
    init_image_sampler(filter, filter
                       , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                       , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                       , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                       , VK_FALSE
                       , 1
                       , VK_BORDER_COLOR_INT_OPAQUE_BLACK
                       , VK_FALSE
                       , (VkCompareOp)0
                       , VK_SAMPLER_MIPMAP_MODE_LINEAR
                       , 0.0f, 0.0f, 0.0f
                       , gpu, &img->image_sampler);
}

void
make_framebuffer(framebuffer_t *fbo
                 , uint32_t w, uint32_t h
                 , uint32_t layer_count
                 , render_pass_t *compatible
                 , const memory_buffer_view_t<image2d_t> &colors
                 , image2d_t *depth
                 , gpu_t *gpu)
{
    if (colors.count)
    {
        allocate_memory_buffer(fbo->color_attachments, colors.count);
        for (uint32_t i = 0; i < colors.count; ++i)
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
    
    init_framebuffer(compatible, w, h, layer_count, gpu, fbo);

    fbo->extent = VkExtent2D{ w, h };
}

render_pass_dependency_t
make_render_pass_dependency(int32_t src_index,
                            VkPipelineStageFlags src_stage,
                            uint32_t src_access,
                            int32_t dst_index,
                            VkPipelineStageFlags dst_stage,
                            uint32_t dst_access)
{
    return render_pass_dependency_t{ src_index, src_stage, src_access, dst_index, dst_stage, dst_access };
}

void
make_render_pass(render_pass_t *render_pass
                 , const memory_buffer_view_t<render_pass_attachment_t> &attachments
                 , const memory_buffer_view_t<render_pass_subpass_t> &subpasses
                 , const memory_buffer_view_t<render_pass_dependency_t> &dependencies
                 , gpu_t *gpu
                 , bool clear_every_instance)
{
    VkAttachmentDescription descriptions_vk[10] = {};
    uint32_t att_i = 0;
    for (; att_i < attachments.count; ++att_i)
    {
        descriptions_vk[att_i] = init_attachment_description(attachments[att_i].format
                                                             , VK_SAMPLE_COUNT_1_BIT
                                                             , clear_every_instance ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                             , VK_ATTACHMENT_STORE_OP_STORE
                                                             , VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                             , VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                             , VK_IMAGE_LAYOUT_UNDEFINED
                                                             , attachments[att_i].final_layout);
    }
    // Max is 5
    VkSubpassDescription subpasses_vk[5] = {};
    uint32_t sub_i = 0;
    VkAttachmentReference reference_buffer[30] = {};
    uint32_t reference_count = 0;
    for (; sub_i < subpasses.count; ++sub_i)
    {
        // Max is 10
        uint32_t ref_i = 0;
        uint32_t color_reference_start = reference_count;
        for (; ref_i < subpasses[sub_i].color_attachment_count; ++ref_i, ++reference_count)
        {
            reference_buffer[reference_count] = init_attachment_reference(subpasses[sub_i].color_attachments[ref_i].index,
                                                                                  subpasses[sub_i].color_attachments[ref_i].layout);
        }

        uint32_t input_reference_start = reference_count;
        uint32_t inp_i = 0;
        for (; inp_i < subpasses[sub_i].input_attachment_count; ++inp_i, ++reference_count)
        {
            reference_buffer[reference_count] = init_attachment_reference(subpasses[sub_i].input_attachments[inp_i].index,
                                                                                  subpasses[sub_i].input_attachments[inp_i].layout);
        }

        uint32_t depth_reference_ptr = reference_count;
        if (subpasses[sub_i].enable_depth)
        {
            reference_buffer[reference_count++] = init_attachment_reference(subpasses[sub_i].depth_attachment.index,
                                                                                    subpasses[sub_i].depth_attachment.layout);
        }
        
        subpasses_vk[sub_i] = init_subpass_description({ref_i, &reference_buffer[color_reference_start]}, subpasses[sub_i].enable_depth ? &reference_buffer[depth_reference_ptr] : nullptr, {inp_i, &reference_buffer[input_reference_start]});
    }

    VkSubpassDependency dependencies_vk[10] = {};
    uint32_t dep_i = 0;
    for (; dep_i < dependencies.count; ++dep_i)
    {
        const render_pass_dependency_t *info = &dependencies[dep_i];
        dependencies_vk[dep_i] = init_subpass_dependency(info->src_index, info->dst_index
                                                                 , info->src_stage, info->src_access
                                                                 , info->dst_stage, info->dst_access
                                                                 , VK_DEPENDENCY_BY_REGION_BIT);
    }
    
    init_render_pass({ att_i, descriptions_vk }, {sub_i, subpasses_vk}, {dep_i, dependencies_vk}, gpu, render_pass);
}

void
make_graphics_pipeline(graphics_pipeline_t *ppln
                       , const shader_modules_t &modules
                       , bool primitive_restart, VkPrimitiveTopology topology
                       , VkPolygonMode polygonmode, VkCullModeFlags culling
                       , shader_uniform_layouts_t &layouts
                       , const shader_pk_data_t &pk
                       , VkExtent2D viewport
                       , const shader_blend_states_t &blends
                       , model_t *model
                       , bool enable_depth
                       , float32_t depth_bias
                       , const dynamic_states_t &dynamic_states
                       , render_pass_t *compatible
                       , uint32_t subpass
                       , gpu_t *gpu)
{
    VkShaderModule module_objects[shader_modules_t::MAX_SHADERS] = {};
    VkPipelineShaderStageCreateInfo infos[shader_modules_t::MAX_SHADERS] = {};
    for (uint32_t i = 0; i < modules.count; ++i)
    {
        file_contents_t bytecode = read_file(modules.modules[i].filename);
        init_shader(modules.modules[i].stage, bytecode.size, bytecode.content, gpu, &module_objects[i]);
        init_shader_pipeline_info(&module_objects[i], modules.modules[i].stage, &infos[i]);
    }
    VkPipelineVertexInputStateCreateInfo v_input = {};
    init_pipeline_vertex_input_info(model, &v_input);
    VkPipelineInputAssemblyStateCreateInfo assembly = {};
    init_pipeline_input_assembly_info(0, topology, primitive_restart, &assembly);
    VkPipelineViewportStateCreateInfo view_info = {};
    VkViewport view = {};
    init_viewport(0.0f, 0.0f, viewport.width, viewport.height, 0.0f, 1.0f, &view);
    VkRect2D scissor = {};
    init_rect2D(VkOffset2D{}, VkExtent2D{viewport.width, viewport.height}, &scissor);
    init_pipeline_viewport_info({1, &view}, {1, &scissor}, &view_info);
    VkPipelineMultisampleStateCreateInfo multi = {};
    init_pipeline_multisampling_info(VK_SAMPLE_COUNT_1_BIT, 0, &multi);
    VkPipelineColorBlendStateCreateInfo blending_info = {};
    VkPipelineColorBlendAttachmentState blend_states[shader_blend_states_t::MAX_BLEND_STATES];
    for (uint32_t i = 0; i < blends.count; ++i)
    {
        if (blends.blend_states[i])
        {
            init_blend_state_attachment(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                                        , VK_TRUE
                                        , VK_BLEND_FACTOR_SRC_ALPHA
                                        , VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                                        , VK_BLEND_OP_ADD
                                        , VK_BLEND_FACTOR_ONE
                                        , VK_BLEND_FACTOR_ZERO
                                        , VK_BLEND_OP_ADD
                                        , &blend_states[i]);
        }
        else
        {
            init_blend_state_attachment(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                                        , VK_FALSE
                                        , VK_BLEND_FACTOR_ONE
                                        , VK_BLEND_FACTOR_ZERO
                                        , VK_BLEND_OP_ADD
                                        , VK_BLEND_FACTOR_ONE
                                        , VK_BLEND_FACTOR_ZERO
                                        , VK_BLEND_OP_ADD
                                        , &blend_states[i]);
        }
    }
    init_pipeline_blending_info(VK_FALSE, VK_LOGIC_OP_COPY, {blends.count, blend_states}, &blending_info);
    VkPipelineDynamicStateCreateInfo dynamic_info = {};
    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_info.dynamicStateCount = dynamic_states.count;
    dynamic_info.pDynamicStates = dynamic_states.dynamic_states;
    VkPipelineDepthStencilStateCreateInfo depth = {};
    init_pipeline_depth_stencil_info(enable_depth, enable_depth, 0.0f, 1.0f, VK_FALSE, &depth);
    VkPipelineRasterizationStateCreateInfo raster = {};
    init_pipeline_rasterization_info(polygonmode, culling, 2.0f, 0, &raster, depth_bias);

    VkPushConstantRange pk_range = {};
    init_push_constant_range(pk.stages, pk.size, pk.offset, &pk_range);
    uniform_layout_t real_layouts [shader_uniform_layouts_t::MAX_LAYOUTS] = {};
    for (uint32_t i = 0; i < layouts.count; ++i)
    {
        real_layouts[i] = *g_uniform_layout_manager.get(layouts.layouts[i]);
    }
    init_pipeline_layout({layouts.count, real_layouts}, {1, &pk_range}, gpu, &ppln->layout);
    memory_buffer_view_t<VkPipelineShaderStageCreateInfo> shaders_mb = {modules.count, infos};
    init_graphics_pipeline(&shaders_mb
                                   , &v_input
                                   , &assembly
                                   , &view_info
                                   , &raster
                                   , &multi
                                   , &blending_info
                                   , &dynamic_info
                                   , &depth, &ppln->layout, compatible, subpass, gpu, &ppln->pipeline);

    for (uint32_t i = 0; i < modules.count; ++i)
    {
        vkDestroyShaderModule(gpu->logical_device, module_objects[i], nullptr);
    }
}




// Rendering pipeline
struct cameras_t
{
    persist constexpr uint32_t MAX_CAMERAS = 10;
    uint32_t camera_count = 0;
    camera_t cameras[MAX_CAMERAS] = {};
    camera_handle_t camera_bound_to_3d_output;

    gpu_buffer_handle_t camera_transforms_ubos;
    uniform_group_handle_t camera_transforms_uniform_groups;
    uint32_t ubo_count;
} g_cameras;

internal void
make_camera_data(gpu_t *gpu, VkDescriptorPool *pool, swapchain_t *swapchain)
{
    uniform_layout_handle_t ubo_layout_hdl = g_uniform_layout_manager.add("uniform_layout.camera_transforms_ubo"_hash, swapchain->imgs.count);
    auto *ubo_layout_ptr = g_uniform_layout_manager.get(ubo_layout_hdl);
    {
        uniform_layout_info_t blueprint = {};
        blueprint.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        *ubo_layout_ptr = make_uniform_layout(&blueprint, gpu);
    }

    g_cameras.camera_transforms_ubos = g_gpu_buffer_manager.add("gpu_buffer.camera_transforms_ubos"_hash, swapchain->imgs.count);
    auto *camera_ubos = g_gpu_buffer_manager.get(g_cameras.camera_transforms_ubos);
    {
        uint32_t uniform_buffer_count = swapchain->imgs.count;

        g_cameras.ubo_count = uniform_buffer_count;
	
        VkDeviceSize buffer_size = sizeof(camera_transform_uniform_data_t);

        for (uint32_t i = 0
                 ; i < uniform_buffer_count
                 ; ++i)
        {
            init_buffer(buffer_size
                                , VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                                , VK_SHARING_MODE_EXCLUSIVE
                                , VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                , gpu
                                , &camera_ubos[i]);
        }
    }

    g_cameras.camera_transforms_uniform_groups = g_uniform_group_manager.add("uniform_group.camera_transforms_ubo"_hash, swapchain->imgs.count);
    auto *transforms = g_uniform_group_manager.get(g_cameras.camera_transforms_uniform_groups);
    {
        uniform_layout_handle_t layout_hdl = g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash);
        auto *layout_ptr = g_uniform_layout_manager.get(layout_hdl);
        for (uint32_t i = 0; i < swapchain->imgs.count; ++i)
        {
            transforms[i] = make_uniform_group(layout_ptr, pool, gpu);
            update_uniform_group(gpu, &transforms[i],
                                 update_binding_t{BUFFER, &camera_ubos[i], 0});
        }
    }
}

void
make_camera_transform_uniform_data(camera_transform_uniform_data_t *data,
                                   const matrix4_t &view_matrix,
                                   const matrix4_t &projection_matrix,
                                   const matrix4_t &shadow_view_matrix,
                                   const matrix4_t &shadow_projection_matrix,
                                   const vector4_t &debug_vector)
{
    *data = { view_matrix, projection_matrix, shadow_view_matrix, shadow_projection_matrix, debug_vector };
}

void
update_3d_output_camera_transforms(uint32_t image_index, gpu_t *gpu)
{
    camera_t *camera = get_camera_bound_to_3d_output();
    
    update_shadows(500.0f
                   , 1.0f
                   , camera->fov
                   , camera->asp
                   , camera->p
                   , camera->d
                   , camera->u);

    shadow_matrices_t shadow_data = get_shadow_matrices();

    camera_transform_uniform_data_t transform_data = {};
    matrix4_t projection_matrix = camera->p_m;
    projection_matrix[1][1] *= -1.0f;
    make_camera_transform_uniform_data(&transform_data,
                                       camera->v_m,
                                       projection_matrix,
                                       shadow_data.light_view_matrix,
                                       shadow_data.projection_matrix,
                                       vector4_t(1.0f, 0.0f, 0.0f, 1.0f));
    
    gpu_buffer_t &current_ubo = *g_gpu_buffer_manager.get(g_cameras.camera_transforms_ubos + image_index);

    auto map = current_ubo.construct_map();
    map.begin(gpu);
    map.fill(memory_byte_buffer_t{sizeof(camera_transform_uniform_data_t), &transform_data});
    map.end(gpu);
}


camera_handle_t
add_camera(window_data_t *window, resolution_t resolution)
{
    uint32_t index = g_cameras.camera_count;
    g_cameras.cameras[index].set_default(resolution.width, resolution.height, window->m_x, window->m_y);
    ++g_cameras.camera_count;
    return(index);
}

void
make_camera(camera_t *camera, float32_t fov, float32_t asp, float32_t near, float32_t far)
{
    camera->fov = fov;
    camera->asp = asp;
    camera->n = near;
    camera->f = far;
}

camera_t *
get_camera(camera_handle_t handle)
{
    return(&g_cameras.cameras[handle]);
}

camera_t *
get_camera_bound_to_3d_output(void)
{
    return(&g_cameras.cameras[g_cameras.camera_bound_to_3d_output]);
}

void
bind_camera_to_3d_scene_output(camera_handle_t handle)
{
    g_cameras.camera_bound_to_3d_output = handle;
}

memory_buffer_view_t<gpu_buffer_t>
get_camera_transform_ubos(void)
{
    return {g_cameras.ubo_count, g_gpu_buffer_manager.get(g_cameras.camera_transforms_ubos)};
}

memory_buffer_view_t<uniform_group_t>
get_camera_transform_uniform_groups(void)
{
    return {g_cameras.ubo_count, g_uniform_group_manager.get(g_cameras.camera_transforms_uniform_groups)};
}

struct deferred_rendering_t
{
    resolution_t backbuffer_res = {1280, 720};
    
    render_pass_handle_t dfr_render_pass;
    // at the moment, dfr_framebuffer points to multiple because it is the bound to swapchain - in future, change
    framebuffer_handle_t dfr_framebuffer;
    pipeline_handle_t dfr_lighting_ppln;
    uniform_group_handle_t dfr_subpass_group;
    uniform_group_handle_t dfr_g_buffer_group;
} g_dfr_rendering;

resolution_t
get_backbuffer_resolution(void)
{
    return(g_dfr_rendering.backbuffer_res);
}

struct lighting_t
{
    // Default value
    vector3_t ws_light_position {0.00000001f, 10.0f, 0.00000001f};

    // Later, need to add PSSM
    struct shadows_t
    {
        persist constexpr uint32_t SHADOWMAP_W = 4000, SHADOWMAP_H = 4000;
        
        framebuffer_handle_t fbo;
        render_pass_handle_t pass;
        image_handle_t map;
        uniform_group_handle_t set;
    
        pipeline_handle_t debug_frustum_ppln;
    
        matrix4_t light_view_matrix;
        matrix4_t projection_matrix;
        matrix4_t inverse_light_view;

        vector4_t ls_corners[8];
        
        union
        {
            struct {float32_t x_min, x_max, y_min, y_max, z_min, z_max;};
            float32_t corner_values[6];
        };
    } shadows;
} g_lighting;

struct atmosphere_t
{
    persist constexpr uint32_t CUBEMAP_W = 1000, CUBEMAP_H = 1000;
    
    // gpu_t objects needed to create the atmosphere skybox cubemap
    render_pass_handle_t make_render_pass;
    framebuffer_handle_t make_fbo;
    pipeline_handle_t make_pipeline;

    // pipeline needed to render the cubemap to the screen
    pipeline_handle_t render_pipeline;

    // Descriptor set that will be used to sample (should not be used in world.cpp)
    uniform_group_handle_t cubemap_uniform_group;
} g_atmosphere;

void
update_atmosphere(gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_atmosphere.make_render_pass
                             , g_atmosphere.make_fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , init_clear_color_color(0, 0.0, 0.0, 0));

    VkViewport viewport;
    init_viewport(0, 0, 1000, 1000, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);    

    auto *make_ppln = g_pipeline_manager.get(g_atmosphere.make_pipeline);
    
    command_buffer_bind_pipeline(make_ppln, &queue->q);

    struct atmos_push_k_t
    {
	alignas(16) matrix4_t inverse_projection;
	vector4_t light_dir;
	vector2_t viewport;
    } k;

    matrix4_t atmos_proj = glm::perspective(glm::radians(90.0f), 1000.0f / 1000.0f, 0.1f, 10000.0f);
    k.inverse_projection = glm::inverse(atmos_proj);
    k.viewport = vector2_t(1000.0f, 1000.0f);

    k.light_dir = vector4_t(glm::normalize(-g_lighting.ws_light_position), 1.0f);
    
    command_buffer_push_constant(&k
                                 , sizeof(k)
                                 , 0
                                 , VK_SHADER_STAGE_FRAGMENT_BIT
                                 , make_ppln
                                 , &queue->q);
    
    command_buffer_draw(&queue->q
                        , 1, 1, 0, 0);

    queue->end_render_pass();
}

void
render_atmosphere(const memory_buffer_view_t<uniform_group_t> &sets
                  , const vector3_t &camera_position // To change to camera structure
                  , model_t *cube
                  , gpu_command_queue_t *queue)
{
    auto *render_pipeline = g_pipeline_manager.get(g_atmosphere.render_pipeline);
    command_buffer_bind_pipeline(render_pipeline, &queue->q);

    uniform_group_t *groups = ALLOCA_T(uniform_group_t, sets.count + 1);
    for (uint32_t i = 0; i < sets.count; ++i) groups[i] = sets[i];
    groups[sets.count] = *g_uniform_group_manager.get(g_atmosphere.cubemap_uniform_group);
    
    command_buffer_bind_descriptor_sets(render_pipeline, {sets.count + 1, groups}, &queue->q);

    VkDeviceSize zero = 0;
    command_buffer_bind_vbos(cube->raw_cache_for_rendering, {1, &zero}, 0, 1, &queue->q);

    command_buffer_bind_ibo(cube->index_data, &queue->q);

    struct skybox_push_constant_t
    {
	matrix4_t model_matrix;
    } push_k;

    push_k.model_matrix = glm::scale(vector3_t(1000.0f));

    command_buffer_push_constant(&push_k
                                 , sizeof(push_k)
                                 , 0
                                 , VK_SHADER_STAGE_VERTEX_BIT
                                 , render_pipeline
                                 , &queue->q);

    command_buffer_draw_indexed(&queue->q
                                , cube->index_data.init_draw_indexed_data(0, 0));
}

internal void
make_atmosphere_data(gpu_t *gpu
                     , VkDescriptorPool *pool
                     , swapchain_t *swapchain
                     , VkCommandPool *cmdpool)
{
    g_atmosphere.make_render_pass = g_render_pass_manager.add("render_pass.atmosphere_render_pass"_hash);
    auto *atmosphere_render_pass = g_render_pass_manager.get(g_atmosphere.make_render_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t cubemap_attachment {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(atmosphere_render_pass, {1, &cubemap_attachment}, {1, &subpass}, {2, dependencies}, gpu);
    }

    g_atmosphere.make_fbo = g_framebuffer_manager.add("framebuffer.atmosphere_fbo"_hash);
    auto *atmosphere_fbo = g_framebuffer_manager.get(g_atmosphere.make_fbo);
    {
        image_handle_t atmosphere_cubemap_handle = g_image_manager.add("image2D.atmosphere_cubemap"_hash);
        auto *cubemap = g_image_manager.get(atmosphere_cubemap_handle);
        make_framebuffer_attachment(cubemap, atmosphere_t::CUBEMAP_W, atmosphere_t::CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3, gpu);

        make_framebuffer(atmosphere_fbo, atmosphere_t::CUBEMAP_W, atmosphere_t::CUBEMAP_H, 6, atmosphere_render_pass, {1, cubemap}, nullptr, gpu);
    }

    // ---- Make render atmosphere uniform layout
    uniform_layout_handle_t render_atmosphere_layout_hdl = g_uniform_layout_manager.add("descriptor_set_layout.render_atmosphere_layout"_hash);
    auto *render_atmosphere_layout_ptr = g_uniform_layout_manager.get(render_atmosphere_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *render_atmosphere_layout_ptr = make_uniform_layout(&layout_info, gpu);
    }

    g_atmosphere.cubemap_uniform_group = g_uniform_group_manager.add("descriptor_set.cubemap"_hash);
    auto *cubemap_group_ptr = g_uniform_group_manager.get(g_atmosphere.cubemap_uniform_group);
    {
        image_handle_t cubemap_hdl = g_image_manager.get_handle("image2D.atmosphere_cubemap"_hash);
        auto *cubemap_ptr = g_image_manager.get(cubemap_hdl);

        *cubemap_group_ptr = make_uniform_group(render_atmosphere_layout_ptr, &g_uniform_pool, gpu);
        update_uniform_group(gpu, cubemap_group_ptr,
                             update_binding_t{TEXTURE, cubemap_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_atmosphere.make_pipeline = g_pipeline_manager.add("pipeline.atmosphere_pipeline"_hash);
    auto *make_ppln = g_pipeline_manager.get(g_atmosphere.make_pipeline);
    {
        VkExtent2D atmosphere_extent {atmosphere_t::CUBEMAP_W, atmosphere_t::CUBEMAP_H};
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/atmosphere.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts = {};
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending{false};
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(make_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
                               false, 0.0f, dynamic, atmosphere_render_pass, 0, gpu);
    }

    g_atmosphere.render_pipeline = g_pipeline_manager.add("pipeline.render_atmosphere"_hash);
    auto *render_ppln = g_pipeline_manager.get(g_atmosphere.render_pipeline);
    {
        resolution_t backbuffer_res = g_dfr_rendering.backbuffer_res;
        model_handle_t cube_hdl = g_model_manager.get_handle("model.cube_model"_hash);
        auto *model_ptr = g_model_manager.get(cube_hdl);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/render_atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/render_atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        uniform_layout_handle_t camera_transforms_layout_hdl = g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash);
        shader_uniform_layouts_t layouts(camera_transforms_layout_hdl, render_atmosphere_layout_hdl);
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(render_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, model_ptr,
                               true, 0.0f, dynamic, g_render_pass_manager.get(g_dfr_rendering.dfr_render_pass), 0, gpu);
    }

    // ---- Update the atmosphere (initialize it) ----
    VkCommandBuffer cmdbuf;
    init_single_use_command_buffer(cmdpool, gpu, &cmdbuf);

    gpu_command_queue_t queue{cmdbuf};
    update_atmosphere(&queue);
    
    destroy_single_use_command_buffer(&cmdbuf, cmdpool, gpu);
}

internal void
make_shadow_data(gpu_t *gpu, swapchain_t *swapchain)
{
    vector3_t light_pos_normalized = glm::normalize(g_lighting.ws_light_position);
    g_lighting.shadows.light_view_matrix = glm::lookAt(light_pos_normalized, vector3_t(0.0f), vector3_t(0.0f, 1.0f, 0.0f));
    g_lighting.shadows.inverse_light_view = glm::inverse(g_lighting.shadows.light_view_matrix);

    // ---- Make shadow render pass ----
    g_lighting.shadows.pass = g_render_pass_manager.add("render_pass.shadow_render_pass"_hash);
    auto *shadow_pass = g_render_pass_manager.get(g_lighting.shadows.pass);
    {
        render_pass_attachment_t shadow_attachment { gpu->supported_depth_format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        render_pass_subpass_t subpass = {};
        subpass.set_depth(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        
        make_render_pass(shadow_pass, {1, &shadow_attachment}, {1, &subpass}, {2, dependencies}, gpu);
    }

    // ---- Make shadow framebuffer ----
    g_lighting.shadows.fbo = g_framebuffer_manager.add("framebuffer.shadow_fbo"_hash);
    auto *shadow_fbo = g_framebuffer_manager.get(g_lighting.shadows.fbo);
    {
        image_handle_t shadowmap_handle = g_image_manager.add("image2D.shadow_map"_hash);
        auto *shadowmap_texture = g_image_manager.get(shadowmap_handle);
        make_framebuffer_attachment(shadowmap_texture, lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_W, gpu->supported_depth_format, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 2, gpu);
        
        make_framebuffer(shadow_fbo, lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_W, 1, shadow_pass, null_buffer<image2d_t>(), shadowmap_texture, gpu);
    }
    
    uniform_layout_handle_t sampler2D_layout_hdl = g_uniform_layout_manager.add("descriptor_set_layout.2D_sampler_layout"_hash);
    auto *sampler2D_layout_ptr = g_uniform_layout_manager.get(sampler2D_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *sampler2D_layout_ptr = make_uniform_layout(&layout_info, gpu);
    }

    g_lighting.shadows.set = g_uniform_group_manager.add("descriptor_set.shadow_map_set"_hash);
    auto *shadow_map_ptr = g_uniform_group_manager.get(g_lighting.shadows.set);
    {
        image_handle_t shadowmap_handle = g_image_manager.get_handle("image2D.shadow_map"_hash);
        auto *shadowmap_texture = g_image_manager.get(shadowmap_handle);
        
        *shadow_map_ptr = make_uniform_group(sampler2D_layout_ptr, &g_uniform_pool, gpu);
        update_uniform_group(gpu, shadow_map_ptr,
                             update_binding_t{TEXTURE, shadowmap_texture, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_lighting.shadows.debug_frustum_ppln = g_pipeline_manager.add("pipeline.debug_frustum"_hash);
    auto *frustum_ppln = g_pipeline_manager.get(g_lighting.shadows.debug_frustum_ppln);
    {
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/debug_frustum.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/debug_frustum.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts (g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(frustum_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_LINE,
                               VK_CULL_MODE_NONE, layouts, push_k, g_dfr_rendering.backbuffer_res, blending, nullptr,
                               true, 0.0f, dynamic, g_render_pass_manager.get(g_dfr_rendering.dfr_render_pass), 0, gpu);
    }
}

internal void
calculate_ws_frustum_corners(vector4_t *corners
			     , vector4_t *shadow_corners)
{
    shadow_matrices_t shadow_data = get_shadow_matrices();

    camera_t *camera = get_camera_bound_to_3d_output();
    
    corners[0] = shadow_data.inverse_light_view * camera->captured_frustum_corners[0];
    corners[1] = shadow_data.inverse_light_view * camera->captured_frustum_corners[1];
    corners[3] = shadow_data.inverse_light_view * camera->captured_frustum_corners[2];
    corners[2] = shadow_data.inverse_light_view * camera->captured_frustum_corners[3];
    
    corners[4] = shadow_data.inverse_light_view * camera->captured_frustum_corners[4];
    corners[5] = shadow_data.inverse_light_view * camera->captured_frustum_corners[5];
    corners[7] = shadow_data.inverse_light_view * camera->captured_frustum_corners[6];
    corners[6] = shadow_data.inverse_light_view * camera->captured_frustum_corners[7];

    shadow_corners[0] = shadow_data.inverse_light_view * camera->captured_shadow_corners[0];
    shadow_corners[1] = shadow_data.inverse_light_view * camera->captured_shadow_corners[1];
    shadow_corners[2] = shadow_data.inverse_light_view * camera->captured_shadow_corners[2];
    shadow_corners[3] = shadow_data.inverse_light_view * camera->captured_shadow_corners[3];
    
    shadow_corners[4] = shadow_data.inverse_light_view * camera->captured_shadow_corners[4];
    shadow_corners[5] = shadow_data.inverse_light_view * camera->captured_shadow_corners[5];
    shadow_corners[6] = shadow_data.inverse_light_view * camera->captured_shadow_corners[6];
    shadow_corners[7] = shadow_data.inverse_light_view * camera->captured_shadow_corners[7];
}

internal void
render_debug_frustum(gpu_command_queue_t *queue
                     , VkDescriptorSet ubo)
{
    auto *debug_frustum_ppln = g_pipeline_manager.get(g_lighting.shadows.debug_frustum_ppln);
    command_buffer_bind_pipeline(debug_frustum_ppln, &queue->q);

    command_buffer_bind_descriptor_sets(debug_frustum_ppln
						, {1, &ubo}
						, &queue->q);

    struct push_k_t
    {
	alignas(16) vector4_t positions[8];
	alignas(16) vector4_t color;
    } push_k1, push_k2;

    calculate_ws_frustum_corners(push_k1.positions, push_k2.positions);

    push_k1.color = vector4_t(1.0f, 0.0f, 0.0f, 1.0f);
    push_k2.color = vector4_t(0.0f, 0.0f, 1.0f, 1.0f);
    
    command_buffer_push_constant(&push_k1
					 , sizeof(push_k1)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , debug_frustum_ppln
					 , &queue->q);
    
    command_buffer_draw(&queue->q, 24, 1, 0, 0);

    command_buffer_push_constant(&push_k2
					 , sizeof(push_k2)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , debug_frustum_ppln
					 , &queue->q);
    
    command_buffer_draw(&queue->q, 24, 1, 0, 0);
}

void
render_3d_frustum_debug_information(gpu_command_queue_t *queue, uint32_t image_index)
{
    auto *camera_transforms = g_uniform_group_manager.get(g_cameras.camera_transforms_ubos);
    //    render_debug_frustum(queue, camera_transforms[image_index]);
}

shadow_matrices_t
get_shadow_matrices(void)
{
    shadow_matrices_t ret;
    ret.projection_matrix = g_lighting.shadows.projection_matrix;
    ret.light_view_matrix = g_lighting.shadows.light_view_matrix;
    ret.inverse_light_view = g_lighting.shadows.inverse_light_view;
    return(ret);
}

shadow_debug_t
get_shadow_debug(void)
{
    shadow_debug_t ret;    
    for (uint32_t i = 0; i < 6; ++i)
    {
        ret.corner_values[i] = g_lighting.shadows.corner_values[i];
    }
    for (uint32_t i = 0; i < 8; ++i)
    {
        ret.frustum_corners[i] = g_lighting.shadows.ls_corners[i];
    }
    return(ret);
}

shadow_display_t
get_shadow_display(void)
{
    auto *texture = g_uniform_group_manager.get(g_lighting.shadows.set);
    shadow_display_t ret{lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_H, *texture};
    return(ret);
}

void
update_shadows(float32_t far, float32_t near, float32_t fov, float32_t aspect
               // Later to replace with a Camera structure
               , const vector3_t &ws_p
               , const vector3_t &ws_d
               , const vector3_t &ws_up)
{
    float32_t far_width, near_width, far_height, near_height;
    
    far_width = 2.0f * far * tan(fov);
    near_width = 2.0f * near * tan(fov);
    far_height = far_width / aspect;
    near_height = near_width / aspect;

    vector3_t right_view_ax = glm::normalize(glm::cross(ws_d, ws_up));
    vector3_t up_view_ax = glm::normalize(glm::cross(ws_d, right_view_ax));

    float32_t far_width_half = far_width / 2.0f;
    float32_t near_width_half = near_width / 2.0f;
    float32_t far_height_half = far_height / 2.0f;
    float32_t near_height_half = near_height / 2.0f;

    // f = far, n = near, l = left, r = right, t = top, b = bottom
    enum ortho_corner_t : int32_t
    {
	flt, flb,
	frt, frb,
	nlt, nlb,
	nrt, nrb
    };    

    // light space

    g_lighting.shadows.ls_corners[flt] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * far - right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    g_lighting.shadows.ls_corners[flb] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * far - right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    g_lighting.shadows.ls_corners[frt] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * far + right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    g_lighting.shadows.ls_corners[frb] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * far + right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    g_lighting.shadows.ls_corners[nlt] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * near - right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    g_lighting.shadows.ls_corners[nlb] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * near - right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);

    g_lighting.shadows.ls_corners[nrt] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * near + right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    g_lighting.shadows.ls_corners[nrb] = g_lighting.shadows.light_view_matrix * vector4_t(ws_p + ws_d * near + right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);

    float32_t x_min, x_max, y_min, y_max, z_min, z_max;

    x_min = x_max = g_lighting.shadows.ls_corners[0].x;
    y_min = y_max = g_lighting.shadows.ls_corners[0].y;
    z_min = z_max = g_lighting.shadows.ls_corners[0].z;

    for (uint32_t i = 1; i < 8; ++i)
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

    g_lighting.shadows.projection_matrix = glm::ortho<float32_t>(x_min, x_max, y_min, y_max, z_min, z_max);
}

void
make_dfr_rendering_data(gpu_t *gpu, swapchain_t *swapchain)
{
    // ---- Make deferred rendering render pass ----
    g_dfr_rendering.dfr_render_pass = g_render_pass_manager.add("render_pass.deferred_render_pass"_hash);
    auto *dfr_render_pass = g_render_pass_manager.get(g_dfr_rendering.dfr_render_pass);
    {
        render_pass_attachment_t attachments[] = { render_pass_attachment_t{swapchain->format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 render_pass_attachment_t{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 render_pass_attachment_t{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 render_pass_attachment_t{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 render_pass_attachment_t{gpu->supported_depth_format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} };
        render_pass_subpass_t subpasses[2] = {};
        subpasses[0].set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        subpasses[0].enable_depth = 1;
        subpasses[0].depth_attachment = render_pass_attachment_reference_t{ 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        
        subpasses[1].set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        
        subpasses[1].set_input_attachment_references(render_pass_attachment_reference_t{ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        render_pass_dependency_t dependencies[3] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        dependencies[2] = make_render_pass_dependency(1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(dfr_render_pass, {5, attachments}, {2, subpasses}, {3, dependencies}, gpu);
    }

    // ---- Make deferred rendering framebuffer ----
    g_dfr_rendering.dfr_framebuffer = g_framebuffer_manager.add("framebuffer.deferred_fbo"_hash);
    auto *dfr_framebuffer = g_framebuffer_manager.get(g_dfr_rendering.dfr_framebuffer);
    {
        uint32_t w = g_dfr_rendering.backbuffer_res.width, h = g_dfr_rendering.backbuffer_res.height;
        
        image_handle_t final_tx_hdl = g_image_manager.add("image2D.fbo_final"_hash);
        auto *final_tx = g_image_manager.get(final_tx_hdl);
        image_handle_t albedo_tx_hdl = g_image_manager.add("image2D.fbo_albedo"_hash);
        auto *albedo_tx = g_image_manager.get(albedo_tx_hdl);
        image_handle_t position_tx_hdl = g_image_manager.add("image2D.fbo_position"_hash);
        auto *position_tx = g_image_manager.get(position_tx_hdl);
        image_handle_t normal_tx_hdl = g_image_manager.add("image2D.fbo_normal"_hash);
        auto *normal_tx = g_image_manager.get(normal_tx_hdl);
        image_handle_t depth_tx_hdl = g_image_manager.add("image2D.fbo_depth"_hash);
        auto *depth_tx = g_image_manager.get(depth_tx_hdl);

        make_framebuffer_attachment(final_tx, w, h, swapchain->format, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 2, gpu);
        make_framebuffer_attachment(albedo_tx, w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2, gpu);
        make_framebuffer_attachment(position_tx, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2, gpu);
        make_framebuffer_attachment(normal_tx, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2, gpu);
        make_framebuffer_attachment(depth_tx, w, h, gpu->supported_depth_format, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 2, gpu);

        image2d_t color_attachments[4] = {};
        color_attachments[0] = *final_tx;
        color_attachments[1] = *albedo_tx;
        color_attachments[2] = *position_tx;
        color_attachments[3] = *normal_tx;
        
        // Can put final_tx as the pointer to the array because, the other 3 textures will be stored contiguously just after it in memory
        make_framebuffer(dfr_framebuffer, w, h, 1, dfr_render_pass, {4, color_attachments}, depth_tx, gpu);
    }

    uniform_layout_handle_t gbuffer_layout_hdl = g_uniform_layout_manager.add("descriptor_set_layout.g_buffer_layout"_hash);
    auto *gbuffer_layout_ptr = g_uniform_layout_manager.get(gbuffer_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *gbuffer_layout_ptr = make_uniform_layout(&layout_info, gpu);
    }

    uniform_layout_handle_t gbuffer_input_layout_hdl = g_uniform_layout_manager.add("descriptor_set_layout.deferred_layout"_hash);
    auto *gbuffer_input_layout_ptr = g_uniform_layout_manager.get(gbuffer_input_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        *gbuffer_input_layout_ptr = make_uniform_layout(&layout_info, gpu);
    }

    g_dfr_rendering.dfr_g_buffer_group = g_uniform_group_manager.add("uniform_group.g_buffer"_hash);
    auto *gbuffer_group_ptr = g_uniform_group_manager.get(g_dfr_rendering.dfr_g_buffer_group);
    {
        *gbuffer_group_ptr = make_uniform_group(gbuffer_layout_ptr, &g_uniform_pool, gpu);

        image_handle_t final_tx_hdl = g_image_manager.get_handle("image2D.fbo_final"_hash);
        image2d_t *final_tx = g_image_manager.get(final_tx_hdl);
        
        image_handle_t position_tx_hdl = g_image_manager.get_handle("image2D.fbo_position"_hash);
        image2d_t *position_tx = g_image_manager.get(position_tx_hdl);
        
        image_handle_t normal_tx_hdl = g_image_manager.get_handle("image2D.fbo_normal"_hash);
        image2d_t *normal_tx = g_image_manager.get(normal_tx_hdl);
        
        update_uniform_group(gpu, gbuffer_group_ptr,
                             update_binding_t{TEXTURE, final_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{TEXTURE, position_tx, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{TEXTURE, normal_tx, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_dfr_rendering.dfr_subpass_group = g_uniform_group_manager.add("descriptor_set.deferred_descriptor_sets"_hash);
    auto *gbuffer_input_group_ptr = g_uniform_group_manager.get(g_dfr_rendering.dfr_subpass_group);
    {
        image_handle_t albedo_tx_hdl = g_image_manager.get_handle("image2D.fbo_albedo"_hash);
        auto *albedo_tx = g_image_manager.get(albedo_tx_hdl);
        
        image_handle_t position_tx_hdl = g_image_manager.get_handle("image2D.fbo_position"_hash);
        auto *position_tx = g_image_manager.get(position_tx_hdl);
        
        image_handle_t normal_tx_hdl = g_image_manager.get_handle("image2D.fbo_normal"_hash);
        auto *normal_tx = g_image_manager.get(normal_tx_hdl);

        *gbuffer_input_group_ptr = make_uniform_group(gbuffer_input_layout_ptr, &g_uniform_pool, gpu);
        update_uniform_group(gpu, gbuffer_input_group_ptr,
                             update_binding_t{INPUT_ATTACHMENT, albedo_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{INPUT_ATTACHMENT, position_tx, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{INPUT_ATTACHMENT, normal_tx, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_dfr_rendering.dfr_lighting_ppln = g_pipeline_manager.add("pipeline.deferred_pipeline"_hash);
    auto *deferred_ppln = g_pipeline_manager.get(g_dfr_rendering.dfr_lighting_ppln);
    {
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/deferred_lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/deferred_lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("descriptor_set_layout.deferred_layout"_hash));
        shader_pk_data_t push_k{ 160, 0, VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blend_states(false);
        dynamic_states_t dynamic_states(VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(deferred_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, g_dfr_rendering.backbuffer_res, blend_states, nullptr,
                               false, 0.0f, dynamic_states, dfr_render_pass, 1, gpu);
    }
}

void
begin_shadow_offscreen(uint32_t shadow_map_width, uint32_t shadow_map_height
                       , gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_lighting.shadows.pass
                             , g_lighting.shadows.fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , init_clear_color_depth(1.0f, 0));
    
    VkViewport viewport = {};
    init_viewport(0, 0, shadow_map_width, shadow_map_height, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);

    vkCmdSetDepthBias(queue->q, 1.25f, 0.0f, 1.75f);

    // Render the world to the shadow map
}

void
end_shadow_offscreen(gpu_command_queue_t *queue)
{
    command_buffer_end_render_pass(&queue->q);
}

void
begin_deferred_rendering(uint32_t image_index /* to_t remove in the future */
                         , gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_dfr_rendering.dfr_render_pass
                             , g_dfr_rendering.dfr_framebuffer
                             , VK_SUBPASS_CONTENTS_INLINE
                             // Clear values hre
                             , init_clear_color_color(0, 0.4, 0.7, 0)
                             , init_clear_color_color(0, 0.4, 0.7, 0)
                             , init_clear_color_color(0, 0.4, 0.7, 0)
                             , init_clear_color_color(0, 0.4, 0.7, 0)
                             , init_clear_color_depth(1.0f, 0));

    command_buffer_set_viewport(g_dfr_rendering.backbuffer_res.width, g_dfr_rendering.backbuffer_res.height, 0.0f, 1.0f, &queue->q);
    command_buffer_set_line_width(2.0f, &queue->q);

    // User renders what is needed ...
}    

void
end_deferred_rendering(const matrix4_t &view_matrix // In future, change this to camera structure
                       , gpu_command_queue_t *queue)
{
    queue->next_subpass(VK_SUBPASS_CONTENTS_INLINE);

    auto *dfr_lighting_ppln = g_pipeline_manager.get(g_dfr_rendering.dfr_lighting_ppln);

    command_buffer_bind_pipeline(dfr_lighting_ppln
					 , &queue->q);

    auto *dfr_subpass_group = g_uniform_group_manager.get(g_dfr_rendering.dfr_subpass_group);
    
    VkDescriptorSet deferred_sets[] = {*dfr_subpass_group};
    
    command_buffer_bind_descriptor_sets(dfr_lighting_ppln
						, {1, deferred_sets}
						, &queue->q);
    
    struct deferred_lighting_push_k_t
    {
	vector4_t light_position;
	matrix4_t view_matrix;
    } deferred_push_k;
    
    deferred_push_k.light_position = vector4_t(glm::normalize(-g_lighting.ws_light_position), 1.0f);
    deferred_push_k.view_matrix = view_matrix;
    
    command_buffer_push_constant(&deferred_push_k
					 , sizeof(deferred_push_k)
					 , 0
					 , VK_SHADER_STAGE_FRAGMENT_BIT
					 , dfr_lighting_ppln
					 , &queue->q);
    
    command_buffer_draw(&queue->q
                                , 4, 1, 0, 0);

    queue->end_render_pass();
}

struct pfx_stage_t
{
    // Max inputs is 10
    image_handle_t inputs[10] = {};
    uint32_t input_count;
    // One input for now
    image_handle_t output;
    
    pipeline_handle_t ppln;
    framebuffer_handle_t fbo;
    uniform_group_handle_t output_group; // For next one

    inline void
    introduce_to_managers(const constant_string_t &ppln_name,
                          const constant_string_t &fbo_name,
                          const constant_string_t &group_name)
    {
        ppln = g_pipeline_manager.add(ppln_name);
        fbo = g_framebuffer_manager.add(fbo_name);
        output_group = g_uniform_group_manager.add(group_name);        
    }
};

// For debugging
// TODO: Make sure that this functionality doesn't use Saska's abstraction of vulkan and uses pure and raw vulkan code for possible future users in their projects (if this thing works well)
struct dbg_pfx_frame_capture_t
{
    // Debug a shader with this data:
    void *pk_ptr;
    uint32_t pk_size;

    // Create a blitted image with uniform sampler to display
    image2d_t blitted_image;
    image2d_t blitted_image_linear;
    mapped_gpu_memory_t mapped_image;
    uniform_group_t blitted_image_uniform;

    // Create blitted images (tiling linear) of input attachments / samplers
    struct dbg_sampler2d_t
    {
        VkFormat format;

        resolution_t resolution;
        image_handle_t original_image;
        image2d_t blitted_linear_image;
        uint32_t index;

        mapped_gpu_memory_t mapped;
    };

    uint32_t sampler_count;
    // Maximum is 10 samplers
    dbg_sampler2d_t samplers[10];

    bool selected_pixel = false;
    vector2_t window_cursor_position;
    vector2_t backbuffer_cursor_position;

    inline void
    prepare_memory_maps(gpu_t *gpu)
    {
        for (uint32_t i = 0; i < sampler_count; ++i)
        {
            samplers[i].mapped = samplers[i].blitted_linear_image.construct_map(gpu);
            samplers[i].mapped.begin(gpu);
        }
    }

    inline void
    end_memory_maps(gpu_t *gpu)
    {
        for (uint32_t i = 0; i < sampler_count; ++i)
        {
            samplers[i].mapped.end(gpu);
        }
    }
};

struct post_processing_t
{
    pfx_stage_t ssr_stage;
    pfx_stage_t pre_final_stage;
    pfx_stage_t final_stage;

    uniform_layout_handle_t pfx_single_tx_layout;
    render_pass_handle_t pfx_render_pass;
    render_pass_handle_t final_render_pass;

    // For debugging
    bool dbg_requested_capture = false;
    bool dbg_in_frame_capture_mode = false;
    dbg_pfx_frame_capture_t dbg_capture;

    rect2D_t render_rect2D;
} g_postfx;

internal float32_t
float16_to_float32(uint16_t f)
{
    int f32i32 =  ((f & 0x8000) << 16);
    f32i32 |= ((f & 0x7fff) << 13) + 0x38000000;

    float ret;
    memcpy(&ret, &f32i32, sizeof(float32_t));
    return ret;
}

internal vector4_t
glsl_texture_function(dbg_pfx_frame_capture_t::dbg_sampler2d_t *sampler, const vector2_t &uvs, gpu_t *gpu)
{
    uint32_t ui_x = (uint32_t)(uvs.x * (float32_t)sampler->resolution.width);
    uint32_t ui_y = (uint32_t)(uvs.y * (float32_t)sampler->resolution.height);

    ui_y = sampler->resolution.height - ui_y;

    uint32_t pixel_size;
    switch(sampler->format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_R8G8B8A8_UNORM: {pixel_size = sizeof(uint8_t) * 4; break;}
    case VK_FORMAT_R16G16B16A16_SFLOAT: {pixel_size = sizeof(uint16_t) * 4; break;}
    }
    uint8_t *pixels = (uint8_t *)sampler->mapped.data;

    VkImageSubresource subresources {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(gpu->logical_device,
                                sampler->blitted_linear_image.image,
                                &subresources,
                                &layout);

    pixels += layout.offset;
    pixels += ui_y * (layout.rowPitch);
    uint8_t *pixel = ui_x * pixel_size + pixels;

    vector4_t final_color = {};

    switch(sampler->format)
    {
        /*    case VK_FORMAT_R8G8B8A8_UNORM:
        {
            float32_t r = (float32_t)(*(pixel + 0)) / 256.0f;
            float32_t g = (float32_t)(*(pixel + 1)) / 256.0f;
            float32_t b = (float32_t)(*(pixel + 2)) / 256.0f;
            float32_t a = (float32_t)(*(pixel + 3)) / 256.0f;
            final_color = vector4_t(r, g, b, a);
            break;
            }*/
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_R8G8B8A8_UNORM:
        {
            float32_t r = (float32_t)(*(pixel + 2)) / 256.0f;
            float32_t g = (float32_t)(*(pixel + 1)) / 256.0f;
            float32_t b = (float32_t)(*(pixel + 0)) / 256.0f;
            float32_t a = (float32_t)(*(pixel + 3)) / 256.0f;
            final_color = vector4_t(r, g, b, a);
            break;
        }
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        {
            uint16_t *pixel_16b = (uint16_t *)pixel;
            float32_t r = float16_to_float32(*(pixel_16b));
            float32_t g = float16_to_float32(*(pixel_16b + 1));
            float32_t b = float16_to_float32(*(pixel_16b + 2));
            float32_t a = float16_to_float32(*(pixel_16b + 3));
            final_color = vector4_t(r, g, b, a);
            break;
        }
    }
    return(final_color);
}

internal uint32_t
get_pixel_color(image2d_t *image, mapped_gpu_memory_t *memory, float32_t x, float32_t y, VkFormat format, const resolution_t &resolution, gpu_t *gpu)
{
    uint32_t ui_x = (uint32_t)(x * (float32_t)resolution.width);
    uint32_t ui_y = (uint32_t)(y * (float32_t)resolution.height);
    
    ui_y = resolution.height - ui_y;
    uint32_t pixel_size;
    switch(format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM: pixel_size = sizeof(uint8_t) * 4; break;
    case VK_FORMAT_R8G8B8A8_UNORM: pixel_size = sizeof(uint8_t) * 4; break;
    case VK_FORMAT_R16G16B16A16_SFLOAT: pixel_size = sizeof(uint16_t) * 4; break;
    }
    uint8_t *pixels = (uint8_t *)memory->data;

    VkImageSubresource subresources { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(gpu->logical_device,
                                image->image,
                                &subresources,
                                &layout);

    pixels += layout.offset;

    pixels += ui_y * (layout.rowPitch);

    uint8_t *pixel = (uint8_t *)(ui_x * 4 + pixels);

    // convert from bgra to rgba
    uint32_t r = *(pixel + 2);
    uint32_t g = *(pixel + 1);
    uint32_t b = *(pixel + 0);
    uint32_t a = *(pixel + 3);

    uint32_t final_color = (r << 24) | (g << 16) | (b << 8) | a;
    
    return(final_color);
}

vector4_t
invoke_glsl_code(dbg_pfx_frame_capture_t *capture, const vector2_t &uvs, camera_t *camera, gpu_t *gpu);

void
dbg_handle_input(window_data_t *window, gpu_t *gpu)
{
    if (g_postfx.dbg_in_frame_capture_mode)
    {
        if (window->mb_map[GLFW_MOUSE_BUTTON_LEFT])
        {
            // Isn't normalized
            g_postfx.dbg_capture.window_cursor_position = window->normalized_cursor_position;
            g_postfx.dbg_capture.window_cursor_position.y = window->h - g_postfx.dbg_capture.window_cursor_position.y;

            g_postfx.dbg_capture.backbuffer_cursor_position = g_postfx.dbg_capture.window_cursor_position -
                vector2_t(g_postfx.render_rect2D.offset.x, g_postfx.render_rect2D.offset.y);

            g_postfx.dbg_capture.backbuffer_cursor_position /= vector2_t(g_postfx.render_rect2D.extent.width, g_postfx.render_rect2D.extent.height);
            g_postfx.dbg_capture.backbuffer_cursor_position = g_postfx.dbg_capture.backbuffer_cursor_position * 2.0f - vector2_t(1.0f);
            
            vector4_t final_color = invoke_glsl_code(&g_postfx.dbg_capture,
                                                     vector2_t((g_postfx.dbg_capture.backbuffer_cursor_position.x + 1.0f) / 2.0f,
                                                               (g_postfx.dbg_capture.backbuffer_cursor_position.y + 1.0f) / 2.0f),
                                                     get_camera_bound_to_3d_output(),
                                                     gpu);
            uint32_t final_color_ui = vec4_color_to_ui32b(final_color);
            
            
            g_postfx.dbg_capture.mapped_image = g_postfx.dbg_capture.blitted_image_linear.construct_map(gpu);
            g_postfx.dbg_capture.mapped_image.begin(gpu);
            
            uint32_t pixel_color = get_pixel_color(&g_postfx.dbg_capture.blitted_image_linear,
                                                   &g_postfx.dbg_capture.mapped_image,
                                                   (g_postfx.dbg_capture.backbuffer_cursor_position.x + 1.0f) / 2.0f,
                                                   (g_postfx.dbg_capture.backbuffer_cursor_position.y + 1.0f) / 2.0f,
                                                   VK_FORMAT_B8G8R8A8_UNORM,
                                                   get_backbuffer_resolution(),
                                                   gpu);
            g_postfx.dbg_capture.mapped_image.end(gpu);

                        // Print cursor position and the color of the pixel
            persist char buffer[100];
            sprintf(buffer, "sh_out = 0x%08x db_out = 0x%08x\n", pixel_color, final_color_ui);
            console_out(buffer);

            window->mb_map[GLFW_MOUSE_BUTTON_LEFT] = false;
        }
    }
}

internal void
dbg_make_frame_capture_blit_image(image2d_t *dst_img, uint32_t w, uint32_t h, VkFormat format, VkImageAspectFlags aspect, gpu_t *gpu)
{
    init_image(w,
               h,
               format,
               VK_IMAGE_TILING_LINEAR,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
               1,
               gpu,
               dst_img,
               0);
    VkImageAspectFlags aspect_flags = aspect;
    init_image_view(&dst_img->image,
                    format,
                    aspect_flags,
                    gpu,
                    &dst_img->image_view,
                    VK_IMAGE_VIEW_TYPE_2D,
                    1);
}

internal void
dbg_make_frame_capture_output_blit_image(image2d_t *dst_image, image2d_t *dst_image_linear, uint32_t w, uint32_t h, VkFormat format, VkImageAspectFlags aspect, gpu_t *gpu)
{
    make_framebuffer_attachment(dst_image, w, h, format, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 2, gpu);
    dbg_make_frame_capture_blit_image(dst_image_linear, w, h, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, gpu);
}

internal void
dbg_make_frame_capture_uniform_data(dbg_pfx_frame_capture_t *capture, gpu_t *gpu)
{
    uniform_layout_handle_t layout_hdl = g_uniform_layout_manager.get_handle("uniform_layout.pfx_single_tx_output"_hash);
    uniform_layout_t *layout_ptr = g_uniform_layout_manager.get(layout_hdl);
    
    capture->blitted_image_uniform = make_uniform_group(layout_ptr, &g_uniform_pool, gpu);
    update_uniform_group(gpu,
                         &capture->blitted_image_uniform,
                         update_binding_t{TEXTURE, &capture->blitted_image, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
}

internal void
dbg_make_frame_capture_samplers(pfx_stage_t *stage, dbg_pfx_frame_capture_t *capture, gpu_t *gpu)
{
    capture->sampler_count = stage->input_count;
    framebuffer_t *stage_fbo = g_framebuffer_manager.get(stage->fbo);
    for (uint32_t i = 0; i < capture->sampler_count; ++i)
    {
        dbg_pfx_frame_capture_t::dbg_sampler2d_t *sampler = &capture->samplers[i];
        sampler->index = 0;
        sampler->original_image = stage->inputs[i];
        dbg_make_frame_capture_blit_image(&sampler->blitted_linear_image,
                                          stage_fbo->extent.width,
                                          stage_fbo->extent.height,
                                          sampler->format,
                                          VK_IMAGE_ASPECT_COLOR_BIT,
                                          gpu);
    }
}

internal void
dbg_make_frame_capture_data(gpu_command_queue_pool_t *pool, gpu_t *gpu)
{
    // Trying to debug the SSR stage
    pfx_stage_t *stage = &g_postfx.ssr_stage;
    framebuffer_t *stage_fbo = g_framebuffer_manager.get(stage->fbo);
    
    g_postfx.ssr_stage.input_count = 3;
    
    g_postfx.ssr_stage.inputs[0] = g_image_manager.get_handle("image2D.fbo_final"_hash);
    g_postfx.ssr_stage.inputs[1] = g_image_manager.get_handle("image2D.fbo_position"_hash);
    g_postfx.ssr_stage.inputs[2] = g_image_manager.get_handle("image2D.fbo_normal"_hash);

    g_postfx.ssr_stage.output = g_image_manager.get_handle("image2D.pfx_ssr_color"_hash);

    dbg_pfx_frame_capture_t *frame_capture = &g_postfx.dbg_capture;
    // Initialize what will later on be the blitted image of the output
    /*make_framebuffer_attachment(&frame_capture->blitted_image,
                                stage_fbo->extent.width,
                                stage_fbo->extent.height,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                1,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                2,
                                gpu);*/
    
    dbg_make_frame_capture_output_blit_image(&frame_capture->blitted_image,
                                             &frame_capture->blitted_image_linear,
                                             stage_fbo->extent.width,
                                             stage_fbo->extent.height,
                                             VK_FORMAT_B8G8R8A8_UNORM,
                                             VK_IMAGE_ASPECT_COLOR_BIT,
                                             gpu);
    
    transition_image_layout(&frame_capture->blitted_image.image,
                            VK_FORMAT_B8G8R8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            pool,
                            gpu);
    
    // Initialize the uniform group for the blitted image
    dbg_make_frame_capture_uniform_data(frame_capture,
                                        gpu);
    // Initialize the samplers
    g_postfx.dbg_capture.sampler_count = 3;
    g_postfx.dbg_capture.samplers[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    g_postfx.dbg_capture.samplers[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    g_postfx.dbg_capture.samplers[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    g_postfx.dbg_capture.samplers[0].resolution = (resolution_t)stage_fbo->extent;
    g_postfx.dbg_capture.samplers[1].resolution = (resolution_t)stage_fbo->extent;
    g_postfx.dbg_capture.samplers[2].resolution = (resolution_t)stage_fbo->extent;
    dbg_make_frame_capture_samplers(stage, frame_capture, gpu);
}

internal void
make_pfx_stage(pfx_stage_t *dst_stage,
               const shader_modules_t &shader_modules,
               const shader_uniform_layouts_t &uniform_layouts,
               const resolution_t &resolution,
               image2d_t *stage_output,
               gpu_t *gpu,
               swapchain_t *swapchain,
               uniform_layout_t *single_tx_layout_ptr,
               render_pass_t *pfx_render_pass)
{
    auto *stage_pipeline = g_pipeline_manager.get(dst_stage->ppln);
    {
        resolution_t backbuffer_res = resolution;
        shader_modules_t modules = shader_modules;
        shader_uniform_layouts_t layouts = uniform_layouts;
        shader_pk_data_t push_k {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic (VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(stage_pipeline, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL
                               , VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, nullptr
                               , false, 0.0f, dynamic, pfx_render_pass, 0, gpu);
    }

    auto *stage_framebuffer = g_framebuffer_manager.get(dst_stage->fbo);
    {
        auto *tx_ptr = stage_output;
        make_framebuffer_attachment(tx_ptr, resolution.width, resolution.height,
                                    swapchain->format, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 2, gpu);
        make_framebuffer(stage_framebuffer, resolution.width, resolution.height, 1, pfx_render_pass, {1, tx_ptr}, nullptr, gpu);
    }

    auto *output_group = g_uniform_group_manager.get(dst_stage->output_group);
    {
        *output_group = make_uniform_group(single_tx_layout_ptr, &g_uniform_pool, gpu);
        update_uniform_group(gpu, output_group,
                             update_binding_t{TEXTURE, stage_output, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }
}

void
make_postfx_data(gpu_t *gpu,
                 swapchain_t *swapchain,
                 gpu_command_queue_pool_t *pool)
{
    g_postfx.pfx_single_tx_layout = g_uniform_layout_manager.add("uniform_layout.pfx_single_tx_output"_hash);
    auto *single_tx_layout_ptr = g_uniform_layout_manager.get(g_postfx.pfx_single_tx_layout);
    {
        uniform_layout_info_t info = {};
        info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *single_tx_layout_ptr = make_uniform_layout(&info, gpu);
    }
    
    g_postfx.final_stage.fbo = g_framebuffer_manager.add("framebuffer.display_fbo"_hash, swapchain->views.count);

    g_postfx.final_render_pass = g_render_pass_manager.add("render_pass.final_render_pass"_hash);
    auto *final_render_pass = g_render_pass_manager.get(g_postfx.final_render_pass);
    {
        // only_t one attachment
        render_pass_attachment_t attachment = { swapchain->format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        render_pass_subpass_t subpass;
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2];
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(final_render_pass, {1, &attachment}, {1, &subpass}, {2, dependencies}, gpu);
    }
    
    g_postfx.pfx_render_pass = g_render_pass_manager.add("render_pass.pfx_render_pass"_hash);
    auto *pfx_render_pass = g_render_pass_manager.get(g_postfx.pfx_render_pass);
    {
        // only_t one attachment
        render_pass_attachment_t attachment = { swapchain->format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        render_pass_subpass_t subpass;
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2];
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(pfx_render_pass, {1, &attachment}, {1, &subpass}, {2, dependencies}, gpu);
    }
    // ---- make_t the framebuffer for the swapchain / screen ----
    // ---- only_t has one color attachment, no depth attachment ----
    // ---- uses_t swapchain extent ----
    auto *final_fbo = g_framebuffer_manager.get(g_postfx.final_stage.fbo);
    {    
        for (uint32_t i = 0; i < swapchain->views.count; ++i)
        {
            final_fbo[i].extent = swapchain->extent;
            
            allocate_memory_buffer(final_fbo[i].color_attachments, 1);
            final_fbo[i].color_attachments[0] = swapchain->views[i];
            final_fbo[i].depth_attachment = VK_NULL_HANDLE;
            init_framebuffer(pfx_render_pass, final_fbo[i].extent.width, final_fbo[i].extent.height, 1, gpu, &final_fbo[i]);
        }
    }

    g_postfx.final_stage.ppln = g_pipeline_manager.add("graphics_pipeline.pfx_final"_hash);
    auto *final_ppln = g_pipeline_manager.get(g_postfx.final_stage.ppln);
    {        
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/pfx_final.vert.spv", VK_SHADER_STAGE_VERTEX_BIT}, shader_module_info_t{"shaders/SPV/pfx_final.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_postfx.pfx_single_tx_layout);
        shader_pk_data_t push_k {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic (VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR);
        make_graphics_pipeline(final_ppln, modules, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, swapchain->extent, blending, nullptr,
                               false, 0.0f, dynamic, pfx_render_pass, 0, gpu);
    }
    
    shader_modules_t modules(shader_module_info_t{"shaders/pfx_ssr.vert", VK_SHADER_STAGE_VERTEX_BIT},
                             shader_module_info_t{"shaders/pfx_ssr.frag", VK_SHADER_STAGE_FRAGMENT_BIT});
    shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("descriptor_set_layout.g_buffer_layout"_hash),
                                     g_uniform_layout_manager.get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
                                     g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
    image_handle_t ssr_tx_hdl = g_image_manager.add("image2D.pfx_ssr_color"_hash);
    auto *ssr_tx_ptr = g_image_manager.get(ssr_tx_hdl);
    g_postfx.ssr_stage.introduce_to_managers("graphics_pipeline.pfx_ssr"_hash,
                                             "framebuffer.pfx_ssr"_hash,
                                             "uniform_group.pfx_ssr_output"_hash);
    make_pfx_stage(&g_postfx.ssr_stage,
                   modules,
                   layouts,
                   get_backbuffer_resolution(),
                   ssr_tx_ptr,
                   gpu,
                   swapchain,
                   single_tx_layout_ptr,
                   pfx_render_pass);

    shader_modules_t pre_final_modules(shader_module_info_t{"shaders/SPV/pfx_final.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                       shader_module_info_t{"shaders/SPV/pfx_final.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
    shader_uniform_layouts_t pre_final_layouts(g_postfx.pfx_single_tx_layout);
    image_handle_t pre_final_tx_hdl = g_image_manager.add("image2D.pre_final_color"_hash);
    auto *pre_final_tx_ptr = g_image_manager.get(pre_final_tx_hdl);
    g_postfx.pre_final_stage.introduce_to_managers("graphics_pipeline.pre_final"_hash,
                                             "framebuffer.pre_final"_hash,
                                             "uniform_group.pre_final_output"_hash);
    make_pfx_stage(&g_postfx.pre_final_stage,
                   pre_final_modules,
                   pre_final_layouts,
                   get_backbuffer_resolution(),
                   pre_final_tx_ptr,
                   gpu,
                   swapchain,
                   single_tx_layout_ptr,
                   pfx_render_pass);

    dbg_make_frame_capture_data(pool, gpu);
}

framebuffer_handle_t
get_pfx_framebuffer_hdl(void)
{
    return(g_postfx.pre_final_stage.fbo);
}

inline void
apply_ssr(gpu_command_queue_t *queue
          , uniform_group_t *transforms_group
          , const matrix4_t &view_matrix
          , const matrix4_t &projection_matrix
          , gpu_t *gpu)
{
    queue->begin_render_pass(g_postfx.pfx_render_pass
                             , g_postfx.ssr_stage.fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f));
    VkViewport v = {};
    init_viewport(0, 0, g_dfr_rendering.backbuffer_res.width, g_dfr_rendering.backbuffer_res.height, 0.0f, 1.0f, &v);
    command_buffer_set_viewport(&v, &queue->q);
    {
        auto *pfx_ssr_ppln = g_pipeline_manager.get(g_postfx.ssr_stage.ppln);
        command_buffer_bind_pipeline(pfx_ssr_ppln, &queue->q);

        auto *g_buffer_group = g_uniform_group_manager.get(g_dfr_rendering.dfr_g_buffer_group);
        auto *atmosphere_group = g_uniform_group_manager.get(g_atmosphere.cubemap_uniform_group);
        
        uniform_group_t groups[] = { *g_buffer_group, *atmosphere_group, *transforms_group };
        command_buffer_bind_descriptor_sets(pfx_ssr_ppln, {3, groups}, &queue->q);

        struct ssr_lighting_push_k_t
        {
            vector4_t ws_light_position;
            matrix4_t view;
            matrix4_t proj;
        } ssr_pk;

        ssr_pk.ws_light_position = view_matrix * vector4_t(glm::normalize(-g_lighting.ws_light_position), 0.0f);
        ssr_pk.view = view_matrix;
        ssr_pk.proj = projection_matrix;
        ssr_pk.proj[1][1] *= -1.0f;
        command_buffer_push_constant(&ssr_pk, sizeof(ssr_pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_ssr_ppln, &queue->q);

        command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }
    queue->end_render_pass();
}

inline void
render_to_pre_final(gpu_command_queue_t *queue,
                    gpu_t *gpu)
{
    if (g_postfx.dbg_requested_capture)
    {
        g_postfx.dbg_in_frame_capture_mode = true;
        g_postfx.dbg_requested_capture = false;

        // Do image copy
        blit_image(g_image_manager.get(g_postfx.ssr_stage.output),
                   &g_postfx.dbg_capture.blitted_image,
                   g_dfr_rendering.backbuffer_res.width,
                   g_dfr_rendering.backbuffer_res.height,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   &queue->q,
                   gpu);
        copy_image(g_image_manager.get(g_postfx.ssr_stage.output),
                   &g_postfx.dbg_capture.blitted_image_linear,
                   g_dfr_rendering.backbuffer_res.width,
                   g_dfr_rendering.backbuffer_res.height,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   &queue->q,
                   gpu);

        for (uint32_t i = 0; i < g_postfx.ssr_stage.input_count; ++i)
        {
            copy_image(g_image_manager.get(g_postfx.ssr_stage.inputs[i]),
                       &g_postfx.dbg_capture.samplers[i].blitted_linear_image,
                       g_dfr_rendering.backbuffer_res.width,
                       g_dfr_rendering.backbuffer_res.height,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       &queue->q,
                       gpu);
        }
    }
    
    queue->begin_render_pass(g_postfx.pfx_render_pass
                             , g_postfx.pre_final_stage.fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f));

    uniform_group_t tx_to_render = {};
    
    // TODO: If requested capture mode -> go into capture mode
    if (g_postfx.dbg_in_frame_capture_mode)
    {
        tx_to_render = g_postfx.dbg_capture.blitted_image_uniform;
    }
    else
    {
        tx_to_render = *g_uniform_group_manager.get(g_postfx.ssr_stage.output_group);
    }
    
    VkViewport v = {};
    init_viewport(0, 0, g_dfr_rendering.backbuffer_res.width, g_dfr_rendering.backbuffer_res.height, 0.0f, 1.0f, &v);
    command_buffer_set_viewport(&v, &queue->q);
    {
        auto *pfx_pre_final_ppln = g_pipeline_manager.get(g_postfx.pre_final_stage.ppln);
        command_buffer_bind_pipeline(pfx_pre_final_ppln, &queue->q);

        auto *ssr_group = g_uniform_group_manager.get(g_postfx.ssr_stage.output_group);
        
        uniform_group_t groups[] = { tx_to_render };
        command_buffer_bind_descriptor_sets(pfx_pre_final_ppln, {1, groups}, &queue->q);

        struct push_k
        {
            vector4_t db;
        } pk;

        if (g_postfx.dbg_in_frame_capture_mode)
        {
            pk.db = vector4_t(0.0f);
        }
        else
        {
            pk.db = vector4_t(-10.0f);
        }
        command_buffer_push_constant(&pk, sizeof(pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_pre_final_ppln, &queue->q);

        command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }
    queue->end_render_pass();
}

void
apply_pfx_on_scene(gpu_command_queue_t *queue
                   , uniform_group_t *transforms_group
                   , const matrix4_t &view_matrix
                   , const matrix4_t &projection_matrix
                   , gpu_t *gpu)
{
    apply_ssr(queue, transforms_group,
              view_matrix,
              projection_matrix,
              gpu);

    render_to_pre_final(queue, gpu);
}

void
render_final_output(uint32_t image_index, gpu_command_queue_t *queue, swapchain_t *swapchain)
{
    queue->begin_render_pass(g_postfx.final_render_pass
                             , g_postfx.final_stage.fbo + image_index
                             , VK_SUBPASS_CONTENTS_INLINE
                             , init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f));

    float32_t backbuffer_asp = (float32_t)g_dfr_rendering.backbuffer_res.width / (float32_t)g_dfr_rendering.backbuffer_res.height;
    float32_t swapchain_asp = (float32_t)swapchain->extent.width / (float32_t)swapchain->extent.height;

    uint32_t rect2D_width, rect2D_height, rect2Dx, rect2Dy;
    
    if (backbuffer_asp >= swapchain_asp)
    {
        rect2D_width = swapchain->extent.width;
        rect2D_height = (uint32_t)((float32_t)swapchain->extent.width / backbuffer_asp);
        rect2Dx = 0;
        rect2Dy = (swapchain->extent.height - rect2D_height) / 2;
    }

    if (backbuffer_asp < swapchain_asp)
    {
        rect2D_width = (uint32_t)(swapchain->extent.height * backbuffer_asp);
        rect2D_height = swapchain->extent.height;
        rect2Dx = (swapchain->extent.width - rect2D_width) / 2;
        rect2Dy = 0;
    }
    
    VkViewport v = {};
    init_viewport(rect2Dx, rect2Dy, rect2D_width, rect2D_height, 0.0f, 1.0f, &v);
    command_buffer_set_viewport(&v, &queue->q);
    {
        auto *pfx_final_ppln = g_pipeline_manager.get(g_postfx.final_stage.ppln);
        command_buffer_bind_pipeline(pfx_final_ppln, &queue->q);

        auto rect2D = make_rect2D(rect2Dx, rect2Dy, rect2D_width, rect2D_height);
        command_buffer_set_rect2D(&rect2D, &queue->q);
        g_postfx.render_rect2D = rect2D;
        
        uniform_group_t groups[] = { *g_uniform_group_manager.get(g_postfx.pre_final_stage.output_group) };
        command_buffer_bind_descriptor_sets(pfx_final_ppln, {1, groups}, &queue->q);

        struct ssr_lighting_push_k_t
        {
            vector4_t debug;
        } pk;

        pk.debug = vector4_t(0.0f, 0.0f, 0.0f, -10.0f);
        command_buffer_push_constant(&pk, sizeof(pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_final_ppln, &queue->q);

        command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }
    queue->end_render_pass();
}

internal void
make_cube_model(gpu_t *gpu,
                swapchain_t *swapchain,
                gpu_command_queue_pool_t *pool)
{
    model_handle_t cube_model_hdl = g_model_manager.add("model.cube_model"_hash);
    auto *cube_model_ptr = g_model_manager.get(cube_model_hdl);
    {
        cube_model_ptr->attribute_count = 3;
	cube_model_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	cube_model_ptr->binding_count = 1;
	cube_model_ptr->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

	struct vertex_t { vector3_t pos; vector3_t color; vector2_t uvs; };
	
	// only one binding
	model_binding_t *binding = cube_model_ptr->bindings;
	binding->begin_attributes_creation(cube_model_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vertex_t::pos));
	binding->push_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vertex_t::color));
	binding->push_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(vertex_t::uvs));

	binding->end_attributes_creation();
    }

    gpu_buffer_handle_t cube_vbo_hdl = g_gpu_buffer_manager.add("vbo.cube_model_vbo"_hash);
    auto *vbo = g_gpu_buffer_manager.get(cube_vbo_hdl);
    {
        struct vertex_t { vector3_t pos, color; vector2_t uvs; };

        vector3_t gray = vector3_t(0.2);
	
        float32_t radius = 2.0f;
	
        persist vertex_t vertices[]
        {
            {{-radius, -radius, radius	}, gray},
            {{radius, -radius, radius	}, gray},
            {{radius, radius, radius	}, gray},
            {{-radius, radius, radius	}, gray},
												 
            {{-radius, -radius, -radius	}, gray},
            {{radius, -radius, -radius	}, gray},
            {{radius, radius, -radius	}, gray},
            {{-radius, radius, -radius	}, gray}
        };
	
        auto *main_binding = &cube_model_ptr->bindings[0];
	    
        memory_byte_buffer_t byte_buffer{sizeof(vertices), vertices};
	
        invoke_staging_buffer_for_device_local_buffer(byte_buffer
                                                              , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                              , pool
                                                              , vbo
                                                              , gpu);

        main_binding->buffer = vbo->buffer;
        cube_model_ptr->create_vbo_list();
    }
    
    gpu_buffer_handle_t model_ibo_hdl = g_gpu_buffer_manager.add("ibo.cube_model_ibo"_hash);
    auto *ibo = g_gpu_buffer_manager.get(model_ibo_hdl);
    {
	persist uint32_t mesh_indices[] = 
            {
                0, 1, 2,
                2, 3, 0,

                1, 5, 6,
                6, 2, 1,

                7, 6, 5,
                5, 4, 7,
	    
                3, 7, 4,
                4, 0, 3,
	    
                4, 5, 1,
                1, 0, 4,
	    
                3, 2, 6,
                6, 7, 3,
            };

	cube_model_ptr->index_data.index_type = VK_INDEX_TYPE_UINT32;
	cube_model_ptr->index_data.index_offset = 0;
	cube_model_ptr->index_data.index_count = sizeof(mesh_indices) / sizeof(mesh_indices[0]);

	memory_byte_buffer_t byte_buffer{sizeof(mesh_indices), mesh_indices};
	    
	invoke_staging_buffer_for_device_local_buffer(byte_buffer
							      , VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							      , pool
							      , ibo
							      , gpu);

	cube_model_ptr->index_data.index_buffer = ibo->buffer;
    }
}

internal int32_t
lua_begin_frame_capture(lua_State *state);

internal int32_t
lua_end_frame_capture(lua_State *state);

void
initialize_game_3d_graphics(gpu_t *gpu,
                            swapchain_t *swapchain,
                            gpu_command_queue_pool_t *pool)
{
    g_dfr_rendering.backbuffer_res = {1500, 1000};
    
    make_uniform_pool(gpu);
    make_dfr_rendering_data(gpu, swapchain);
    make_camera_data(gpu, &g_uniform_pool, swapchain);
    make_shadow_data(gpu, swapchain);
    make_cube_model(gpu, swapchain, pool);
    make_atmosphere_data(gpu, &g_uniform_pool, swapchain, pool);

    add_global_to_lua(script_primitive_type_t::FUNCTION, "begin_frame_capture", &lua_begin_frame_capture);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "end_frame_capture", &lua_end_frame_capture);    
}

// 2D Graphics


void
initialize_game_2d_graphics(gpu_t *gpu,
                            swapchain_t *swapchain,
                            gpu_command_queue_pool_t *pool)
{
    make_postfx_data(gpu, swapchain, pool);
}

void
destroy_graphics(gpu_t *gpu)
{
    vkDestroyDescriptorPool(gpu->logical_device, g_uniform_pool, nullptr);
}

#include <GLFW/glfw3.h>

internal int32_t
lua_begin_frame_capture(lua_State *state)
{
    // Copy images into samplers blitted images
    g_postfx.dbg_requested_capture = true;

    console_out("=== Begin frame capture ===\n");
    
    window_data_t *window = get_window_data();
    glfwSetInputMode(window->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    return(0);
}

internal int32_t
lua_end_frame_capture(lua_State *state)
{
    g_postfx.dbg_in_frame_capture_mode = false;

    console_out("=== End frame capture ===\n");
    
    window_data_t *window = get_window_data();
    glfwSetInputMode(window->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    return(0);
}






// ====================== Debugging for SSR =========================

#include <matrix.h>
#include <vector.h>
#include <vector_functions.h>

using  vec4 = vml::vector<float, 0, 1, 2, 3>;
using  vec3 = vml::vector<float, 0, 1, 2>;
using  vec2 = vml::vector<float, 0, 1>;
using   _01 = vml::indices_pack<0, 1>;
using  _012 = vml::indices_pack<0, 1, 2>;
using _0123 = vml::indices_pack<0, 1, 2, 3>;
using  mat2 = vml::matrix<float, vml::vector, _01, _01>;
using  mat3 = vml::matrix<float, vml::vector, _012, _012>;
using  mat4 = vml::matrix<float, vml::vector, _0123, _0123>;

#define layout(...) // NOTHING

namespace funccall_inout
{
    using vec2 = vec2 &;
    using vec3 = vec3 &;
    using vec4 = vec4 &;
    using mat2 = mat2 &;
    using mat3 = mat3 &;
    using mat4 = mat4 &;
    using float32_t = float32_t &;
    using bool_t = bool &;
}

struct sampler2d_ptr_t
{
    dbg_pfx_frame_capture_t::dbg_sampler2d_t *sampler_data;
};

internal vec4
glsl_texture_func(sampler2d_ptr_t &sampler, vec2 &uvs);

#define Push_K struct pk_t
#define in
#define out funccall_inout::
#define inout funccall_inout::
#define uniform internal
#define texture glsl_texture_func

#define VS_DATA struct vs_data_t

using sampler2D = sampler2d_ptr_t;

namespace glsl
{
    gpu_t *g_dbg_gpu_ptr;
    
// =============== INCLUDE GLSL CODE HERE =================
#include "shaders/pfx_ssr_c.hpp"
// ===============
}

internal vec4
glsl_texture_func(sampler2d_ptr_t &sampler, vec2 &uvs)
{
    vector4_t sampled = glsl_texture_function(sampler.sampler_data, vector2_t(uvs.x, uvs.y), glsl::g_dbg_gpu_ptr);
    return vec4(sampled.x, sampled.y, sampled.z, sampled.w);
}

#define PUSH_CONSTANT(name, push_k_data) glsl::##name = push_k_data
#define SET_UNIFORM(name, uni) glsl::##name = uni
#define SET_VS_DATA(name, data) glsl::##name = data

vector4_t
invoke_glsl_code(dbg_pfx_frame_capture_t *capture, const vector2_t &uvs, camera_t *camera, gpu_t *gpu)
{
    glsl::g_dbg_gpu_ptr = gpu;

    glsl::pk_t pk;

    vector4_t n_light_dir = camera->v_m * vector4_t(glm::normalize(-g_lighting.ws_light_position), 0.0f);
    
    pk.ws_light_direction = vec4(n_light_dir.x, n_light_dir.y, n_light_dir.z, n_light_dir.w);
    for (uint32_t x = 0; x < 4; ++x)
    {
        for (uint32_t y = 0; y < 4; ++y)
        {
            pk.view[x][y] = camera->v_m[x][y];
            pk.proj[x][y] = camera->p_m[x][y];
        }
    }

    PUSH_CONSTANT(light_info_pk, pk);
    
    sampler2d_ptr_t gfinal = sampler2d_ptr_t{&capture->samplers[0]};
    SET_UNIFORM(g_final, gfinal);
    
    sampler2d_ptr_t gposition = sampler2d_ptr_t{&capture->samplers[1]};
    SET_UNIFORM(g_position, gposition);
    
    sampler2d_ptr_t gnormal = sampler2d_ptr_t{&capture->samplers[2]};
    SET_UNIFORM(g_normal, gnormal);

    glsl::vs_data_t vs_data = {};
    vs_data.uvs.x = uvs.x;
    vs_data.uvs.y = uvs.y;
    SET_VS_DATA(fs_in, vs_data);

    capture->prepare_memory_maps(gpu);
    {
        // If in debug mode of course
        __debugbreak();
        glsl::main();
    }
    capture->end_memory_maps(gpu);

    vector4_t final_color = vector4_t(glsl::final_color.x, glsl::final_color.y, glsl::final_color.z, 1.0f);
    return(final_color);
}
