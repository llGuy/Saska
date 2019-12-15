/* graphics.cpp */

// TODO: Big refactor after animations are loaded

#include <Windows.h>

#include "core.hpp"
#include "vulkan.hpp"

#include "script.hpp"

#include "graphics.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include "game.hpp"

gpu_buffer_manager_t *g_gpu_buffer_manager;
image_manager_t *g_image_manager;
framebuffer_manager_t *g_framebuffer_manager;
render_pass_manager_t *g_render_pass_manager;
pipeline_manager_t *g_pipeline_manager;
uniform_layout_manager_t *g_uniform_layout_manager;
uniform_group_manager_t *g_uniform_group_manager;
model_manager_t *g_model_manager;
uniform_pool_t *g_uniform_pool;

// Will be used for multi-threading the rendering process for extra extra performance !
global_var gpu_material_submission_queue_manager_t *g_material_queue_manager;

// Stuff more linked to rendering the scene: cameras, lighting, ...
global_var cameras_t *g_cameras;
global_var lighting_t *g_lighting;
global_var atmosphere_t *g_atmosphere;

// Post processing pipeline stuff:
global_var deferred_rendering_t *g_dfr_rendering;
global_var post_processing_t *g_postfx;

// Particles
global_var particle_rendering_t *g_particle_rendering;

gpu_command_queue_t make_command_queue(VkCommandPool *pool, submit_level_t level)
{
    gpu_command_queue_t result;
    allocate_command_buffers(pool, level, {1, &result.q});
    return(result);
}

void begin_command_queue(gpu_command_queue_t *queue, VkCommandBufferInheritanceInfo *inheritance)
{
    begin_command_buffer(&queue->q, (queue->submit_level == VK_COMMAND_BUFFER_LEVEL_SECONDARY ? VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : 0) | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, inheritance);
}
    
void end_command_queue(gpu_command_queue_t *queue)
{
    end_command_buffer(&queue->q);
}



// --------------------- Uniform stuff ---------------------

internal_function void make_uniform_pool(void)
{
    VkDescriptorPoolSize pool_sizes[3] = {};

    init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20, &pool_sizes[0]);
    init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20, &pool_sizes[1]);
    init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 20, &pool_sizes[2]);
    
    init_descriptor_pool(memory_buffer_view_t<VkDescriptorPoolSize>{3, pool_sizes}, 30, g_uniform_pool);
}

// Naming is better than Descriptor in case of people familiar with different APIs / also will be useful when introducing other APIs
using uniform_binding_t = VkDescriptorSetLayoutBinding;

uniform_binding_t make_uniform_binding_s(uint32_t count, uint32_t binding, VkDescriptorType uniform_type, VkShaderStageFlags shader_flags)
{
    return init_descriptor_set_layout_binding(uniform_type, binding, count, shader_flags);
}



void uniform_layout_info_t::push(const uniform_binding_t &binding_info)
{
    bindings_buffer[binding_count++] = binding_info;
}

void uniform_layout_info_t::push(uint32_t count, uint32_t binding, VkDescriptorType uniform_type, VkShaderStageFlags shader_flags)
{
    bindings_buffer[binding_count++] = init_descriptor_set_layout_binding(uniform_type, binding, count, shader_flags);
}

using uniform_layout_t = VkDescriptorSetLayout;

uniform_layout_t make_uniform_layout(uniform_layout_info_t *blueprint)
{
    VkDescriptorSetLayout layout;
    init_descriptor_set_layout({blueprint->binding_count, blueprint->bindings_buffer}, &layout);
    return(layout);
}



// Uniform Group is the struct going to be used to alias VkDescriptorSet, and in other APIs, simply groups of uniforms
using uniform_group_t = VkDescriptorSet;

uniform_group_t make_uniform_group(uniform_layout_t *layout, VkDescriptorPool *pool)
{
    uniform_group_t uniform_group = allocate_descriptor_set(layout, pool);

    return(uniform_group);
}




// --------------------- Rendering stuff ---------------------

uint32_t gpu_material_submission_queue_t::push_material(void *push_k_ptr, uint32_t push_k_size, mesh_t *mesh, uniform_group_t *ubo)
{
    material_t new_mtrl = {};
    new_mtrl.push_k_ptr = push_k_ptr;
    new_mtrl.push_k_size = push_k_size;
    new_mtrl.mesh = mesh;
    new_mtrl.ubo = ubo;

    mtrls[mtrl_count] = new_mtrl;

    return(mtrl_count++);
}

gpu_command_queue_t *gpu_material_submission_queue_t::get_command_buffer(gpu_command_queue_t *queue)
{
    if (cmdbuf_index >= 0)
    {
	return(&g_material_queue_manager->active_queues[cmdbuf_index]);
    }
    else return(queue);
}
    
void gpu_material_submission_queue_t::submit_queued_materials(const memory_buffer_view_t<uniform_group_t> &uniform_groups, graphics_pipeline_t *graphics_pipeline, gpu_command_queue_t *main_queue, submit_level_t level)
{
    // Depends on the cmdbuf_index var, not on the submit level that is given
    gpu_command_queue_t *dst_command_queue = get_command_buffer(main_queue);
    // Now depends also on the submit level
    if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) dst_command_queue = main_queue;

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	VkCommandBufferInheritanceInfo inheritance_info = {};
	inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritance_info.renderPass = g_render_pass_manager->get(main_queue->current_pass_handle)->render_pass;
	inheritance_info.subpass = main_queue->subpass;
	inheritance_info.framebuffer = g_framebuffer_manager->get(main_queue->fbo_handle)->framebuffer;
	
	begin_command_buffer(&dst_command_queue->q, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inheritance_info);
    }
	    
    command_buffer_bind_pipeline(&graphics_pipeline->pipeline, &dst_command_queue->q);

    uniform_group_t *groups = ALLOCA_T(uniform_group_t, uniform_groups.count + 1);
    memcpy(groups, uniform_groups.buffer, sizeof(uniform_group_t) * uniform_groups.count);
    
    for (uint32_t i = 0; i < mtrl_count; ++i)
    {
        material_t *mtrl = &mtrls[i];

        VkDeviceSize *zero = ALLOCA_T(VkDeviceSize, mtrl->mesh->raw_buffer_list.count);
        for (uint32_t z = 0; z < mtrl->mesh->raw_buffer_list.count; ++z) zero[z] = 0;
        
        // TODO: Make sure this doesn't happen to materials which don't need an extra UBO
        uint32_t uniform_count = uniform_groups.count;
        if (mtrl->ubo)
        {
            groups[uniform_count] = *mtrl->ubo;
            ++uniform_count;
        }

        bool32_t is_using_index_buffer = (mtrl->mesh->index_data.index_buffer != VK_NULL_HANDLE); 
        
        command_buffer_bind_descriptor_sets(&graphics_pipeline->layout, { uniform_count, groups }, &dst_command_queue->q);
        command_buffer_bind_vbos(mtrl->mesh->raw_buffer_list, {mtrl->mesh->raw_buffer_list.count, zero}, 0, mtrl->mesh->raw_buffer_list.count, &dst_command_queue->q);
        
        if (is_using_index_buffer)
        {
            command_buffer_bind_ibo(mtrl->mesh->index_data, &dst_command_queue->q);
        }
        
        command_buffer_push_constant(mtrl->push_k_ptr, mtrl->push_k_size, 0, push_k_dst, graphics_pipeline->layout, &dst_command_queue->q);

        if (is_using_index_buffer)
        {
            command_buffer_draw_indexed(&dst_command_queue->q, mtrl->mesh->indexed_data);
        }
        else
        {
            // All of the indexed data is now used as vertex data
            command_buffer_draw(&dst_command_queue->q, mtrl->mesh->indexed_data.index_count, mtrl->mesh->indexed_data.instance_count, mtrl->mesh->indexed_data.first_index, mtrl->mesh->indexed_data.first_instance);
        }
    }

    if (cmdbuf_index >= 0 && level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	end_command_buffer(&dst_command_queue->q);
    }
}

void gpu_material_submission_queue_t::flush_queue(void)
{
    mtrl_count = 0;
}

void gpu_material_submission_queue_t::submit_to_cmdbuf(gpu_command_queue_t *queue)
{
    //    command_buffer_execute_commands(&queue->q, {1, get_command_buffer(nullptr)});
}

gpu_material_submission_queue_t make_gpu_material_submission_queue(uint32_t max_materials, VkShaderStageFlags push_k_dst, submit_level_t level, gpu_command_queue_pool_t *pool)
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
    else if (level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    {
	gpu_command_queue_t command_queue = make_command_queue(pool, level);

	material_queue.mtrl_count = 0;
	allocate_memory_buffer(material_queue.mtrls, max_materials);

	material_queue.cmdbuf_index = g_material_queue_manager->active_queue_ptr;
	g_material_queue_manager->active_queues[g_material_queue_manager->active_queue_ptr++] = command_queue;
    }

    return(material_queue);
}

void submit_queued_materials_from_secondary_queues(gpu_command_queue_t *queue)
{
    //    command_buffer_execute_commands(queue, {g_material_queue_manager->active_queue_ptr, g_material_queue_manager->active_queues});
}

void make_framebuffer_attachment(image2d_t *img, uint32_t w, uint32_t h, VkFormat format, uint32_t layer_count, VkImageUsageFlags usage, uint32_t dimensions)
{
    VkImageCreateFlags flags = (dimensions == 3) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    
    init_image(w, h, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, layer_count, img, flags);

    VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageViewType view_type = (dimensions == 3) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    
    init_image_view(&img->image, format, aspect_flags, &img->image_view, view_type, layer_count);

    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        init_image_sampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                           VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                           VK_FALSE, 1, VK_BORDER_COLOR_INT_OPAQUE_BLACK, VK_FALSE, (VkCompareOp)0, VK_SAMPLER_MIPMAP_MODE_LINEAR, 0.0f, 0.0f, 0.0f, &img->image_sampler);
    }
}

void make_texture(image2d_t *img, uint32_t w, uint32_t h, VkFormat format, uint32_t layer_count, uint32_t dimensions, VkImageUsageFlags usage, VkFilter filter)
{
    VkImageCreateFlags flags = (dimensions == 3) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    init_image(w, h, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, layer_count, img, flags);
    VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewType view_type = (dimensions == 3) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    init_image_view(&img->image, format, aspect_flags, &img->image_view, view_type, layer_count);
    init_image_sampler(filter, filter, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                       VK_FALSE, 1, VK_BORDER_COLOR_INT_OPAQUE_BLACK, VK_FALSE, (VkCompareOp)0, VK_SAMPLER_MIPMAP_MODE_LINEAR, 0.0f, 0.0f, 0.0f, &img->image_sampler);
}

void make_framebuffer(framebuffer_t *fbo, uint32_t w, uint32_t h, uint32_t layer_count, render_pass_t *compatible, const memory_buffer_view_t<image2d_t> &colors, image2d_t *depth)
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
    
    init_framebuffer(compatible, w, h, layer_count, fbo);

    fbo->extent = VkExtent2D{ w, h };
}

render_pass_dependency_t make_render_pass_dependency(int32_t src_index, VkPipelineStageFlags src_stage, uint32_t src_access, int32_t dst_index, VkPipelineStageFlags dst_stage, uint32_t dst_access)
{
    return render_pass_dependency_t{ src_index, src_stage, src_access, dst_index, dst_stage, dst_access };
}

void make_render_pass(render_pass_t *render_pass, const memory_buffer_view_t<render_pass_attachment_t> &attachments, const memory_buffer_view_t<render_pass_subpass_t> &subpasses, const memory_buffer_view_t<render_pass_dependency_t> &dependencies, bool clear_every_instance)
{
    VkAttachmentDescription descriptions_vk[10] = {};
    uint32_t att_i = 0;
    for (; att_i < attachments.count; ++att_i)
    {
        descriptions_vk[att_i] = init_attachment_description(attachments[att_i].format, VK_SAMPLE_COUNT_1_BIT, clear_every_instance ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                             VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, attachments[att_i].final_layout);
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
            reference_buffer[reference_count] = init_attachment_reference(subpasses[sub_i].color_attachments[ref_i].index, subpasses[sub_i].color_attachments[ref_i].layout);
        }

        uint32_t input_reference_start = reference_count;
        uint32_t inp_i = 0;
        for (; inp_i < subpasses[sub_i].input_attachment_count; ++inp_i, ++reference_count)
        {
            reference_buffer[reference_count] = init_attachment_reference(subpasses[sub_i].input_attachments[inp_i].index, subpasses[sub_i].input_attachments[inp_i].layout);
        }

        uint32_t depth_reference_ptr = reference_count;
        if (subpasses[sub_i].enable_depth)
        {
            reference_buffer[reference_count++] = init_attachment_reference(subpasses[sub_i].depth_attachment.index, subpasses[sub_i].depth_attachment.layout);
        }
        
        subpasses_vk[sub_i] = init_subpass_description({ref_i, &reference_buffer[color_reference_start]}, subpasses[sub_i].enable_depth ? &reference_buffer[depth_reference_ptr] : nullptr, {inp_i, &reference_buffer[input_reference_start]});
    }

    VkSubpassDependency dependencies_vk[10] = {};
    uint32_t dep_i = 0;
    for (; dep_i < dependencies.count; ++dep_i)
    {
        const render_pass_dependency_t *info = &dependencies[dep_i];
        dependencies_vk[dep_i] = init_subpass_dependency(info->src_index, info->dst_index, info->src_stage, info->src_access, info->dst_stage, info->dst_access, VK_DEPENDENCY_BY_REGION_BIT);
    }
    
    init_render_pass({ att_i, descriptions_vk }, {sub_i, subpasses_vk}, {dep_i, dependencies_vk}, render_pass);
}

void make_unmappable_gpu_buffer(gpu_buffer_t *dst_buffer, uint32_t size, void *data, gpu_buffer_usage_t usage, gpu_command_queue_pool_t *pool)
{
    memory_byte_buffer_t byte_buffer{ size, data };
    invoke_staging_buffer_for_device_local_buffer(byte_buffer, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, pool, dst_buffer);
}

void fill_graphics_pipeline_info(const shader_modules_t &modules,
                                 bool primitive_restart, VkPrimitiveTopology topology,
                                 VkPolygonMode polygonmode, VkCullModeFlags culling,
                                 shader_uniform_layouts_t &layouts,
                                 const shader_pk_data_t &pk,
                                 VkExtent2D viewport,
                                 const shader_blend_states_t &blends,
                                 model_t *model,
                                 bool enable_depth,
                                 float32_t depth_bias,
                                 const dynamic_states_t &dynamic_states,
                                 render_pass_t *compatible,
                                 uint32_t subpass,
                                 graphics_pipeline_info_t *info)
{
    info->modules = modules;
    info->primitive_restart = primitive_restart;
    info->topology = topology;
    info->polygon_mode = polygonmode;
    info->culling = culling;
    info->layouts = layouts;
    info->pk = pk;
    info->viewport = viewport;
    info->blends = blends;
    info->model = model;
    info->enable_depth = enable_depth;
    info->depth_bias = depth_bias;
    info->dynamic_states = dynamic_states;
    info->compatible = compatible;
    info->subpass = subpass;
}

void graphics_pipeline_t::destroy(void)
{
    vkDestroyPipelineLayout(g_context->gpu.logical_device, layout, nullptr);
    vkDestroyPipeline(g_context->gpu.logical_device, pipeline, nullptr);
}

void make_graphics_pipeline(graphics_pipeline_t *ppln)
{
    // Declaration of parameters
    shader_modules_t &modules = ppln->info->modules;
    bool primitive_restart = ppln->info->primitive_restart;
    VkPrimitiveTopology topology = ppln->info->topology;
    VkPolygonMode polygonmode = ppln->info->polygon_mode;
    VkCullModeFlags culling = ppln->info->culling;
    shader_uniform_layouts_t &layouts = ppln->info->layouts;
    const shader_pk_data_t &pk = ppln->info->pk;
    VkExtent2D viewport = ppln->info->viewport;
    const shader_blend_states_t &blends = ppln->info->blends;
    model_t *model = ppln->info->model;
    bool enable_depth = ppln->info->enable_depth;
    float32_t depth_bias = ppln->info->depth_bias;
    const dynamic_states_t &dynamic_states = ppln->info->dynamic_states;
    render_pass_t *compatible = ppln->info->compatible;
    uint32_t subpass = ppln->info->subpass;

    // Making graphics pipeline
    VkShaderModule module_objects[shader_modules_t::MAX_SHADERS] = {};
    VkPipelineShaderStageCreateInfo infos[shader_modules_t::MAX_SHADERS] = {};
    
    for (uint32_t i = 0; i < modules.count; ++i)
    {
        if (modules.modules[i].file_handle >= 0)
        {
            remove_and_destroy_file(modules.modules[i].file_handle);
        }
        
        modules.modules[i].file_handle = create_file(modules.modules[i].file_path, file_type_flags_t::BINARY | file_type_flags_t::ASSET);

        file_contents_t bytecode = read_file_tmp(modules.modules[i].file_handle);
        init_shader(modules.modules[i].stage, bytecode.size, bytecode.content, &module_objects[i]);
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
            init_blend_state_attachment(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                                        VK_TRUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, &blend_states[i]);
        }
        else
        {
            init_blend_state_attachment(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                                        VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, &blend_states[i]);
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
        real_layouts[i] = *g_uniform_layout_manager->get(layouts.layouts[i]);
    }
    memory_buffer_view_t<VkPushConstantRange> pk_range_mb;
    if (pk_range.size)
    {
        pk_range_mb = memory_buffer_view_t<VkPushConstantRange>{1, &pk_range};
    }
    else
    {
        pk_range_mb = memory_buffer_view_t<VkPushConstantRange>{0, nullptr};
    }
    init_pipeline_layout({layouts.count, real_layouts}, pk_range_mb, &ppln->layout);
    memory_buffer_view_t<VkPipelineShaderStageCreateInfo> shaders_mb = {modules.count, infos};
    init_graphics_pipeline(&shaders_mb, &v_input, &assembly, &view_info, &raster, &multi, &blending_info, &dynamic_info, &depth, &ppln->layout, compatible, subpass, &ppln->pipeline);

    for (uint32_t i = 0; i < modules.count; ++i)
    {
        destroy_shader_module(&module_objects[i]);
    }
}




internal_function void make_camera_data(VkDescriptorPool *pool)
{
    uint32_t swapchain_image_count = get_swapchain_image_count();
    uniform_layout_handle_t ubo_layout_hdl = g_uniform_layout_manager->add("uniform_layout.camera_transforms_ubo"_hash, swapchain_image_count);
    auto *ubo_layout_ptr = g_uniform_layout_manager->get(ubo_layout_hdl);
    {
        uniform_layout_info_t blueprint = {};
        blueprint.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        *ubo_layout_ptr = make_uniform_layout(&blueprint);
    }

    g_cameras->camera_transforms_ubos = g_gpu_buffer_manager->add("gpu_buffer.camera_transforms_ubos"_hash, swapchain_image_count);
    auto *camera_ubos = g_gpu_buffer_manager->get(g_cameras->camera_transforms_ubos);
    {
        uint32_t uniform_buffer_count = swapchain_image_count;

        g_cameras->ubo_count = uniform_buffer_count;
	
        VkDeviceSize buffer_size = sizeof(camera_transform_uniform_data_t);

        for (uint32_t i = 0; i < uniform_buffer_count; ++i)
        {
            init_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &camera_ubos[i]);
        }
    }

    g_cameras->camera_transforms_uniform_groups = g_uniform_group_manager->add("uniform_group.camera_transforms_ubo"_hash, swapchain_image_count);
    auto *transforms = g_uniform_group_manager->get(g_cameras->camera_transforms_uniform_groups);
    {
        uniform_layout_handle_t layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        auto *layout_ptr = g_uniform_layout_manager->get(layout_hdl);
        for (uint32_t i = 0; i < swapchain_image_count; ++i)
        {
            transforms[i] = make_uniform_group(layout_ptr, pool);
            update_uniform_group(&transforms[i], update_binding_t{BUFFER, &camera_ubos[i], 0});
        }
    }

    g_cameras->spectator_camera.set_default((float32_t)get_backbuffer_resolution().width, (float32_t)get_backbuffer_resolution().height, 0.0f, 0.0f);
    
    g_cameras->spectator_camera.compute_projection();
    g_cameras->spectator_camera.v_m = matrix4_t(1.0f);
}

void remove_all_cameras(void)
{
    g_cameras->camera_count = 0;
}

bool is_in_spectator_mode(void)
{
    return(!g_cameras->camera_count);
}

void update_spectator_camera(const matrix4_t &view_matrix)
{
    g_cameras->spectator_camera.v_m = view_matrix;
}

void make_camera_transform_uniform_data(camera_transform_uniform_data_t *data, const matrix4_t &view_matrix, const matrix4_t &projection_matrix, const matrix4_t &shadow_view_matrix, const matrix4_t &shadow_projection_matrix, const vector4_t &debug_vector)
{
    *data = { view_matrix, projection_matrix, shadow_view_matrix, shadow_projection_matrix, debug_vector };
}

void clean_up_cameras(void)
{
    g_cameras->camera_count = 0;
    g_cameras->camera_bound_to_3d_output = -1;
}

void update_3d_output_camera_transforms(uint32_t image_index)
{
    camera_t *camera = get_camera_bound_to_3d_output();

    update_shadows(100.0f, 1.0f, camera->fov, camera->asp, camera->p, camera->d, camera->u);

    shadow_matrices_t shadow_data = get_shadow_matrices();

    camera_transform_uniform_data_t transform_data = {};
    matrix4_t projection_matrix = camera->p_m;
    projection_matrix[1][1] *= -1.0f;
    make_camera_transform_uniform_data(&transform_data, camera->v_m, projection_matrix, shadow_data.light_view_matrix, shadow_data.projection_matrix, vector4_t(1.0f, 0.0f, 0.0f, 1.0f));
    
    gpu_buffer_t &current_ubo = *g_gpu_buffer_manager->get(g_cameras->camera_transforms_ubos + image_index);

    auto map = current_ubo.construct_map();
    map.begin();
    map.fill(memory_byte_buffer_t{sizeof(camera_transform_uniform_data_t), &transform_data});
    map.end();
}


camera_handle_t add_camera(input_state_t *input_state, resolution_t resolution)
{
    uint32_t index = g_cameras->camera_count;
    g_cameras->cameras[index].set_default(resolution.width, resolution.height, input_state->cursor_pos_x, input_state->cursor_pos_y);
    ++g_cameras->camera_count;
    return(index);
}

#undef near
#undef far

void make_camera(camera_t *camera, float32_t fov, float32_t asp, float32_t near, float32_t far)
{
    camera->fov = camera->current_fov = fov;
    camera->asp = asp;
    camera->n = near;
    camera->f = far;
}

camera_t *get_camera(camera_handle_t handle)
{
    return(&g_cameras->cameras[handle]);
}

camera_t *get_camera_bound_to_3d_output(void)
{
    switch (g_cameras->camera_bound_to_3d_output)
    {
    case -1: return &g_cameras->spectator_camera;
    default: return &g_cameras->cameras[g_cameras->camera_bound_to_3d_output];
    }
}

void bind_camera_to_3d_scene_output(camera_handle_t handle)
{
    g_cameras->camera_bound_to_3d_output = handle;
}

memory_buffer_view_t<gpu_buffer_t> get_camera_transform_ubos(void)
{
    return {g_cameras->ubo_count, g_gpu_buffer_manager->get(g_cameras->camera_transforms_ubos)};
}

memory_buffer_view_t<uniform_group_t> get_camera_transform_uniform_groups(void)
{
    return {g_cameras->ubo_count, g_uniform_group_manager->get(g_cameras->camera_transforms_uniform_groups)};
}

resolution_t get_backbuffer_resolution(void)
{
    return(g_dfr_rendering->backbuffer_res);
}

void update_atmosphere_cubemap(gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_atmosphere->make_render_pass, g_atmosphere->make_fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0, 0.0, 0.0, 0));

    VkViewport viewport;
    init_viewport(0, 0, 1000, 1000, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);    

    auto *make_ppln = g_pipeline_manager->get(g_atmosphere->make_pipeline);
    
    command_buffer_bind_pipeline(&make_ppln->pipeline, &queue->q);

    struct atmos_push_k_t
    {
	alignas(16) matrix4_t inverse_projection;
	vector4_t light_dir[2];
        vector4_t light_color[2];
	vector2_t viewport;
    } k;

    matrix4_t atmos_proj = glm::perspective(glm::radians(90.0f), 1000.0f / 1000.0f, 0.1f, 10000.0f);
    k.inverse_projection = glm::inverse(atmos_proj);
    k.viewport = vector2_t(1000.0f, 1000.0f);

    k.light_dir[0] = vector4_t(glm::normalize(-g_lighting->ws_light_position[0]), 1.0f);
    k.light_dir[1] = vector4_t(glm::normalize(-g_lighting->ws_light_position[1]), 1.0f);

    k.light_color[0] = vector4_t(vector3_t(g_lighting->light_color[0]), 1.0f);
    k.light_color[1] = vector4_t(vector3_t(g_lighting->light_color[1]), 1.0f);
    
    command_buffer_push_constant(&k, sizeof(k), 0, VK_SHADER_STAGE_FRAGMENT_BIT, make_ppln->layout, &queue->q);
    command_buffer_draw(&queue->q, 1, 1, 0, 0);

    queue->end_render_pass();
}

void update_atmosphere_irradiance_cubemap(gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_atmosphere->generate_irradiance_pass, g_atmosphere->generate_irradiance_fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0, 0.0, 0.0, 0));

    VkViewport viewport;
    init_viewport(0, 0, atmosphere_t::IRRADIANCE_CUBEMAP_W, atmosphere_t::IRRADIANCE_CUBEMAP_H, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);

    auto *irradiance_ppln = g_pipeline_manager->get(g_atmosphere->generate_irradiance_pipeline);
    
    command_buffer_bind_pipeline(&irradiance_ppln->pipeline, &queue->q);

    uniform_group_t *atmosphere_uniform = g_uniform_group_manager->get(g_atmosphere->cubemap_uniform_group);
    command_buffer_bind_descriptor_sets(&irradiance_ppln->layout, {1, atmosphere_uniform}, &queue->q);
    
    command_buffer_draw(&queue->q, 1, 1, 0, 0);

    queue->end_render_pass();
}

void update_atmosphere(gpu_command_queue_t *queue)
{
    update_atmosphere_cubemap(queue);

    // Need to do a pipeline barrier on the atmosphere cubemap
    /*image2d_t *atmosphere_cubemap = g_image_manager->get(g_atmosphere->cubemap_handle);
    image_memory_barrier_t image_memory_barrier = {};
    initialize_image_memory_barrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    atmosphere_cubemap,
                                    VK_IMAGE_ASPECT_COLOR_BIT,
                                    6,
                                    &image_memory_barrier);
    issue_pipeline_barrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, // Just to be safe
                           null_buffer<buffer_memory_barrier_t>(),
                           {1, &image_memory_barrier},
                           &queue->q);*/
    
    update_atmosphere_irradiance_cubemap(queue);
}

void render_atmosphere(const memory_buffer_view_t<uniform_group_t> &sets, const vector3_t &camera_position, gpu_command_queue_t *queue)
{
    auto *render_pipeline = g_pipeline_manager->get(g_atmosphere->render_pipeline);
    command_buffer_bind_pipeline(&render_pipeline->pipeline, &queue->q);

    uniform_group_t *groups = ALLOCA_T(uniform_group_t, sets.count + 1);
    for (uint32_t i = 0; i < sets.count; ++i) groups[i] = sets[i];
    groups[sets.count] = *g_uniform_group_manager->get(g_atmosphere->cubemap_uniform_group);
    //groups[sets.count] = *g_uniform_group_manager->get(g_atmosphere->atmosphere_irradiance_uniform_group);
    
    command_buffer_bind_descriptor_sets(&render_pipeline->layout, {sets.count + 1, groups}, &queue->q);

    model_t *cube = g_model_manager->get(g_atmosphere->cube_handle);
    
    VkDeviceSize zero = 0;
    command_buffer_bind_vbos(cube->raw_cache_for_rendering, {1, &zero}, 0, 1, &queue->q);

    command_buffer_bind_ibo(cube->index_data, &queue->q);

    struct skybox_push_constant_t
    {
	matrix4_t model_matrix;
    } push_k;

    push_k.model_matrix = glm::scale(vector3_t(100000.0f));

    command_buffer_push_constant(&push_k, sizeof(push_k), 0, VK_SHADER_STAGE_VERTEX_BIT, render_pipeline->layout, &queue->q);

    command_buffer_draw_indexed(&queue->q, cube->index_data.init_draw_indexed_data(0, 0));
}

internal_function void make_atmosphere_data(VkDescriptorPool *pool, VkCommandPool *cmdpool)
{
    g_atmosphere->make_render_pass = g_render_pass_manager->add("render_pass.atmosphere_render_pass"_hash);
    auto *atmosphere_render_pass = g_render_pass_manager->get(g_atmosphere->make_render_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t cubemap_attachment {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(atmosphere_render_pass, {1, &cubemap_attachment}, {1, &subpass}, {2, dependencies});
    }

    g_atmosphere->make_fbo = g_framebuffer_manager->add("framebuffer.atmosphere_fbo"_hash);
    auto *atmosphere_fbo = g_framebuffer_manager->get(g_atmosphere->make_fbo);
    {
        image_handle_t atmosphere_cubemap_handle = g_image_manager->add("image2D.atmosphere_cubemap"_hash);
        auto *cubemap = g_image_manager->get(atmosphere_cubemap_handle);
        make_framebuffer_attachment(cubemap, atmosphere_t::CUBEMAP_W, atmosphere_t::CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3);

        make_framebuffer(atmosphere_fbo, atmosphere_t::CUBEMAP_W, atmosphere_t::CUBEMAP_H, 6, atmosphere_render_pass, {1, cubemap}, nullptr);
        g_atmosphere->cubemap_handle = atmosphere_cubemap_handle;
    }

    g_atmosphere->generate_irradiance_pass = g_render_pass_manager->add("render_pass.irradiance_render_pass"_hash);
    auto *irradiance_render_pass = g_render_pass_manager->get(g_atmosphere->generate_irradiance_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t cubemap_attachment {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(irradiance_render_pass, {1, &cubemap_attachment}, {1, &subpass}, {2, dependencies});
    }

    g_atmosphere->generate_irradiance_fbo = g_framebuffer_manager->add("framebuffer.irradiance_fbo"_hash);
    auto *irradiance_fbo = g_framebuffer_manager->get(g_atmosphere->generate_irradiance_fbo);
    {
        image_handle_t irradiance_cubemap_handle = g_image_manager->add("image2D.irradiance_cubemap"_hash);
        auto *cubemap = g_image_manager->get(irradiance_cubemap_handle);
        make_framebuffer_attachment(cubemap, atmosphere_t::IRRADIANCE_CUBEMAP_W, atmosphere_t::IRRADIANCE_CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3);

        make_framebuffer(irradiance_fbo, atmosphere_t::IRRADIANCE_CUBEMAP_W, atmosphere_t::IRRADIANCE_CUBEMAP_H, 6, irradiance_render_pass, {1, cubemap}, nullptr);
    }

    uniform_layout_handle_t render_atmosphere_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash);
    auto *render_atmosphere_layout_ptr = g_uniform_layout_manager->get(render_atmosphere_layout_hdl);

    g_atmosphere->cubemap_uniform_group = g_uniform_group_manager->add("descriptor_set.cubemap"_hash);
    auto *cubemap_group_ptr = g_uniform_group_manager->get(g_atmosphere->cubemap_uniform_group);
    {
        image_handle_t cubemap_hdl = g_image_manager->get_handle("image2D.atmosphere_cubemap"_hash);
        auto *cubemap_ptr = g_image_manager->get(cubemap_hdl);

        *cubemap_group_ptr = make_uniform_group(render_atmosphere_layout_ptr, g_uniform_pool);
        update_uniform_group(cubemap_group_ptr, update_binding_t{TEXTURE, cubemap_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_atmosphere->atmosphere_irradiance_uniform_group = g_uniform_group_manager->add("descriptor_set.irradiance_cubemap"_hash);
    auto *irradiance_group_ptr = g_uniform_group_manager->get(g_atmosphere->atmosphere_irradiance_uniform_group);
    {
        image_handle_t cubemap_hdl = g_image_manager->get_handle("image2D.irradiance_cubemap"_hash);
        auto *cubemap_ptr = g_image_manager->get(cubemap_hdl);

        *irradiance_group_ptr = make_uniform_group(render_atmosphere_layout_ptr, g_uniform_pool);
        update_uniform_group(irradiance_group_ptr, update_binding_t{TEXTURE, cubemap_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_atmosphere->make_pipeline = g_pipeline_manager->add("pipeline.atmosphere_pipeline"_hash);
    auto *make_ppln = g_pipeline_manager->get(g_atmosphere->make_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        VkExtent2D atmosphere_extent {atmosphere_t::CUBEMAP_W, atmosphere_t::CUBEMAP_H};
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/atmosphere.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts = {};
        shader_pk_data_t push_k = {200, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending{blend_type_t::NO_BLENDING};
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
                                    false, 0.0f, dynamic, atmosphere_render_pass, 0, info);
        make_ppln->info = info;
        make_graphics_pipeline(make_ppln);
    }

    g_atmosphere->generate_irradiance_pipeline = g_pipeline_manager->add("pipeline.irradiance_pipeline"_hash);
    auto *irradiance_ppln = g_pipeline_manager->get(g_atmosphere->generate_irradiance_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        VkExtent2D atmosphere_extent {atmosphere_t::IRRADIANCE_CUBEMAP_W, atmosphere_t::IRRADIANCE_CUBEMAP_H};
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/cubemap_irradiance_generator.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/cubemap_irradiance_generator.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                                 shader_module_info_t{"shaders/SPV/cubemap_irradiance_generator.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts = {render_atmosphere_layout_hdl};
        shader_pk_data_t push_k = {200, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending{blend_type_t::NO_BLENDING};
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
                                    false, 0.0f, dynamic, irradiance_render_pass, 0, info);
        irradiance_ppln->info = info;
        make_graphics_pipeline(irradiance_ppln);
    }

    g_atmosphere->render_pipeline = g_pipeline_manager->add("pipeline.render_atmosphere"_hash);
    auto *render_ppln = g_pipeline_manager->get(g_atmosphere->render_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        resolution_t backbuffer_res = g_dfr_rendering->backbuffer_res;
        model_handle_t cube_hdl = g_model_manager->get_handle("model.cube_model"_hash);
        auto *model_ptr = g_model_manager->get(cube_hdl);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/render_atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/render_atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        uniform_layout_handle_t camera_transforms_layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        shader_uniform_layouts_t layouts(camera_transforms_layout_hdl, render_atmosphere_layout_hdl);
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, model_ptr,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(g_dfr_rendering->dfr_render_pass), 0, info);
        render_ppln->info = info;
        make_graphics_pipeline(render_ppln);
    }

    g_atmosphere->cube_handle = g_model_manager->get_handle("model.cube_model"_hash);

    // ---- Update the atmosphere (initialize it) ----
    VkCommandBuffer cmdbuf;
    init_single_use_command_buffer(cmdpool, &cmdbuf);

    gpu_command_queue_t queue{cmdbuf};
    update_atmosphere(&queue);
    
    destroy_single_use_command_buffer(&cmdbuf, cmdpool);
}

internal_function void make_sun_data(void)
{
    g_lighting->sun.sun_ppln = g_pipeline_manager->add("pipeline.sun"_hash);
    auto *sun_ppln = g_pipeline_manager->get(g_lighting->sun.sun_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        resolution_t backbuffer_res = g_dfr_rendering->backbuffer_res;
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/sun.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/sun.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        uniform_layout_handle_t camera_transforms_layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        uniform_layout_handle_t single_tx_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash);
        shader_uniform_layouts_t layouts(camera_transforms_layout_hdl, single_tx_layout_hdl, g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::ADDITIVE_BLENDING);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, nullptr,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(g_dfr_rendering->dfr_render_pass), 0, info);
        sun_ppln->info = info;
        make_graphics_pipeline(sun_ppln);
    }

    // Make sun texture
    {
        file_handle_t sun_png_handle = create_file("textures/sun/sun_test.png", file_type_flags_t::IMAGE | file_type_flags_t::ASSET);
        external_image_data_t image_data = read_image(sun_png_handle);

        make_texture(&g_lighting->sun.sun_texture, image_data.width, image_data.height,
                     VK_FORMAT_R8G8B8A8_UNORM, 1, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_FILTER_LINEAR);
        transition_image_layout(&g_lighting->sun.sun_texture.image, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                get_global_command_pool());
        invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                     get_global_command_pool(),
                                                     &g_lighting->sun.sun_texture,
                                                     (uint32_t)image_data.width,
                                                     (uint32_t)image_data.height);
        transition_image_layout(&g_lighting->sun.sun_texture.image,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                get_global_command_pool());

        free_external_image_data(&image_data);
    }

    // Make sun uniform group
    uniform_layout_handle_t single_tx_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash);
    g_lighting->sun.sun_group = make_uniform_group(g_uniform_layout_manager->get(single_tx_layout_hdl), g_uniform_pool);
    {
        update_uniform_group(&g_lighting->sun.sun_group,
                             update_binding_t{ TEXTURE, &g_lighting->sun.sun_texture, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
}

internal_function void make_shadow_data(void)
{
    vector3_t light_pos_normalized = glm::normalize(g_lighting->ws_light_position[0]);
    light_pos_normalized.x *= -1.0f;
    light_pos_normalized.z *= -1.0f;

    g_lighting->shadows.light_view_matrix = glm::lookAt(vector3_t(0.0f), light_pos_normalized, vector3_t(0.0f, 1.0f, 0.0f));
    
    g_lighting->shadows.inverse_light_view = glm::inverse(g_lighting->shadows.light_view_matrix);

    // ---- Make shadow render pass ----
    g_lighting->shadows.pass = g_render_pass_manager->add("render_pass.shadow_render_pass"_hash);
    auto *shadow_pass = g_render_pass_manager->get(g_lighting->shadows.pass);
    {
        render_pass_attachment_t shadow_attachment { get_device_supported_depth_format(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        render_pass_subpass_t subpass = {};
        subpass.set_depth(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        
        make_render_pass(shadow_pass, {1, &shadow_attachment}, {1, &subpass}, {2, dependencies});
    }

    // ---- Make shadow framebuffer ----
    g_lighting->shadows.fbo = g_framebuffer_manager->add("framebuffer.shadow_fbo"_hash);
    auto *shadow_fbo = g_framebuffer_manager->get(g_lighting->shadows.fbo);
    {
        image_handle_t shadowmap_handle = g_image_manager->add("image2D.shadow_map"_hash);
        auto *shadowmap_texture = g_image_manager->get(shadowmap_handle);
        make_framebuffer_attachment(shadowmap_texture, lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_W, get_device_supported_depth_format(), 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 2);
        
        make_framebuffer(shadow_fbo, lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_W, 1, shadow_pass, null_buffer<image2d_t>(), shadowmap_texture);
    }
    
    uniform_layout_handle_t sampler2D_layout_hdl = g_uniform_layout_manager->add("descriptor_set_layout.2D_sampler_layout"_hash);
    g_lighting->shadows.ulayout = sampler2D_layout_hdl;
    auto *sampler2D_layout_ptr = g_uniform_layout_manager->get(sampler2D_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *sampler2D_layout_ptr = make_uniform_layout(&layout_info);
    }

    g_lighting->shadows.set = g_uniform_group_manager->add("descriptor_set.shadow_map_set"_hash);
    auto *shadow_map_ptr = g_uniform_group_manager->get(g_lighting->shadows.set);
    {
        image_handle_t shadowmap_handle = g_image_manager->get_handle("image2D.shadow_map"_hash);
        auto *shadowmap_texture = g_image_manager->get(shadowmap_handle);
        
        *shadow_map_ptr = make_uniform_group(sampler2D_layout_ptr, g_uniform_pool);
        update_uniform_group(shadow_map_ptr, update_binding_t{TEXTURE, shadowmap_texture, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_lighting->shadows.debug_frustum_ppln = g_pipeline_manager->add("pipeline.debug_frustum"_hash);
    auto *frustum_ppln = g_pipeline_manager->get(g_lighting->shadows.debug_frustum_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/debug_frustum.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/debug_frustum.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts (g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_LINE,
                                    VK_CULL_MODE_NONE, layouts, push_k, g_dfr_rendering->backbuffer_res, blending, nullptr,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(g_dfr_rendering->dfr_render_pass), 0, info);
        frustum_ppln->info = info;
        make_graphics_pipeline(frustum_ppln);
    }
}

internal_function void calculate_ws_frustum_corners(vector4_t *corners, vector4_t *shadow_corners)
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

internal_function void render_debug_frustum(gpu_command_queue_t *queue, VkDescriptorSet ubo, graphics_pipeline_t *graphics_pipeline)
{
    if (get_camera_bound_to_3d_output()->captured)
    {
        command_buffer_bind_pipeline(&graphics_pipeline->pipeline, &queue->q);

        command_buffer_bind_descriptor_sets(&graphics_pipeline->layout, {1, &ubo}, &queue->q);

        struct push_k_t
        {
            alignas(16) matrix4_t model_matrix;
            alignas(16) vector4_t positions[8];
            alignas(16) vector4_t color;
        } push_k1, push_k2;

        calculate_ws_frustum_corners(push_k1.positions, push_k2.positions);

        push_k1.model_matrix = matrix4_t(1.0f);
        push_k1.color = vector4_t(1.0f, 0.0f, 0.0f, 1.0f);
        push_k2.model_matrix = matrix4_t(1.0f);
        push_k2.color = vector4_t(0.0f, 0.0f, 1.0f, 1.0f);
    
        command_buffer_push_constant(&push_k1, sizeof(push_k1), 0, VK_SHADER_STAGE_VERTEX_BIT, graphics_pipeline->layout, &queue->q);
        command_buffer_draw(&queue->q, 24, 1, 0, 0);
        command_buffer_push_constant(&push_k2, sizeof(push_k2), 0, VK_SHADER_STAGE_VERTEX_BIT, graphics_pipeline->layout, &queue->q);
        command_buffer_draw(&queue->q, 24, 1, 0, 0);
    }
}

void render_3d_frustum_debug_information(uniform_group_t *group, gpu_command_queue_t *queue, uint32_t image_index, graphics_pipeline_t *graphics_pipeline)
{
    auto *camera_transforms = g_uniform_group_manager->get(g_cameras->camera_transforms_ubos);
    render_debug_frustum(queue, *group, graphics_pipeline);
}

void dbg_render_shadow_map_quad(gpu_command_queue_t *queue)
{
    graphics_pipeline_t *graphics_pipeline = &g_lighting->shadows.dbg_shadow_tx_quad_ppln;
    
    command_buffer_bind_pipeline(&graphics_pipeline->pipeline, &queue->q);

    command_buffer_bind_descriptor_sets(&graphics_pipeline->layout, {1, g_uniform_group_manager->get(g_lighting->shadows.set)}, &queue->q);

    struct push_k_t
    {
        alignas(16) vector2_t positions[4];
    } push_k = {};

    push_k.positions[0] = vector2_t(0.2f, -0.1f);
    push_k.positions[1] = vector2_t(0.2f, -1.0f);
    push_k.positions[2] = vector2_t(1.0f, -0.1f);
    push_k.positions[3] = vector2_t(1.0f, -1.0f);

    get_backbuffer_resolution();

    command_buffer_push_constant(&push_k, sizeof(push_k), 0, VK_SHADER_STAGE_VERTEX_BIT, graphics_pipeline->layout, &queue->q);
    command_buffer_draw(&queue->q, 4, 1, 0, 0);
}

shadow_matrices_t get_shadow_matrices(void)
{
    shadow_matrices_t ret;
    ret.projection_matrix = g_lighting->shadows.projection_matrix;
    ret.light_view_matrix = g_lighting->shadows.light_view_matrix;
    ret.inverse_light_view = g_lighting->shadows.inverse_light_view;
    return(ret);
}

shadow_debug_t get_shadow_debug(void)
{
    shadow_debug_t ret;    
    for (uint32_t i = 0; i < 6; ++i)
    {
        ret.corner_values[i] = g_lighting->shadows.corner_values[i];
    }
    for (uint32_t i = 0; i < 8; ++i)
    {
        ret.frustum_corners[i] = g_lighting->shadows.ls_corners[i];
    }
    return(ret);
}

shadow_display_t get_shadow_display(void)
{
    auto *texture = g_uniform_group_manager->get(g_lighting->shadows.set);
    shadow_display_t ret{lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_H, *texture};
    return(ret);
}

void update_shadows(float32_t far, float32_t near, float32_t fov, float32_t aspect, const vector3_t &ws_p, const vector3_t &ws_d, const vector3_t &ws_up)
{
    float32_t far_width, near_width, far_height, near_height;
    
    far_width = 2.0f * far * tan(fov);
    near_width = 2.0f * near * tan(fov);
    far_height = far_width / aspect;
    near_height = near_width / aspect;

    vector3_t center_near = ws_p + ws_d * near;
    vector3_t center_far = ws_p + ws_d * far;
    
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

    // Light space
    g_lighting->shadows.ls_corners[flt] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * far - right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    g_lighting->shadows.ls_corners[flb] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * far - right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    g_lighting->shadows.ls_corners[frt] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * far + right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    g_lighting->shadows.ls_corners[frb] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * far + right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    g_lighting->shadows.ls_corners[nlt] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * near - right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    g_lighting->shadows.ls_corners[nlb] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * near - right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);
    
    g_lighting->shadows.ls_corners[nrt] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * near + right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    g_lighting->shadows.ls_corners[nrb] = g_lighting->shadows.light_view_matrix * vector4_t(ws_p + ws_d * near + right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);

    float32_t x_min, x_max, y_min, y_max, z_min, z_max;

    x_min = x_max = g_lighting->shadows.ls_corners[0].x;
    y_min = y_max = g_lighting->shadows.ls_corners[0].y;
    z_min = z_max = g_lighting->shadows.ls_corners[0].z;

    for (uint32_t i = 1; i < 8; ++i)
    {
	if (x_min > g_lighting->shadows.ls_corners[i].x) x_min = g_lighting->shadows.ls_corners[i].x;
	if (x_max < g_lighting->shadows.ls_corners[i].x) x_max = g_lighting->shadows.ls_corners[i].x;

	if (y_min > g_lighting->shadows.ls_corners[i].y) y_min = g_lighting->shadows.ls_corners[i].y;
	if (y_max < g_lighting->shadows.ls_corners[i].y) y_max = g_lighting->shadows.ls_corners[i].y;

	if (z_min > g_lighting->shadows.ls_corners[i].z) z_min = g_lighting->shadows.ls_corners[i].z;
	if (z_max < g_lighting->shadows.ls_corners[i].z) z_max = g_lighting->shadows.ls_corners[i].z;
    }
    
    g_lighting->shadows.x_min = x_min = x_min;
    g_lighting->shadows.x_max = x_max = x_max;
    g_lighting->shadows.y_min = y_min = y_min;
    g_lighting->shadows.y_max = y_max = y_max;
    g_lighting->shadows.z_min = z_min = z_min;
    g_lighting->shadows.z_max = z_max = z_max;

    z_min = z_min - (z_max - z_min);

    g_lighting->shadows.projection_matrix = glm::transpose(matrix4_t(2.0f / (x_max - x_min), 0.0f, 0.0f, -(x_max + x_min) / (x_max - x_min),
                                                                     0.0f, 2.0f / (y_max - y_min), 0.0f, -(y_max + y_min) / (y_max - y_min),
                                                                     0.0f, 0.0f, 2.0f / (z_max - z_min), -(z_max + z_min) / (z_max - z_min),
                                                                     0.0f, 0.0f, 0.0f, 1.0f));
}

void render_sun(uniform_group_t *camera_transforms, gpu_command_queue_t *queue)
{
    auto *sun_pipeline = g_pipeline_manager->get(g_lighting->sun.sun_ppln);
    command_buffer_bind_pipeline(&sun_pipeline->pipeline, &queue->q);

    uniform_group_t groups[3] = {*camera_transforms, g_lighting->sun.sun_group, *g_uniform_group_manager->get(g_atmosphere->cubemap_uniform_group)};
    
    command_buffer_bind_descriptor_sets(&sun_pipeline->layout, {3, groups}, &queue->q);

    struct sun_push_constant_t
    {
	matrix4_t model_matrix;
        vector3_t ws_light_direction;
    } push_k;

    vector3_t light_pos = vector3_t(g_lighting->ws_light_position[0] * 1000.0f);
    light_pos.x *= -1.0f;
    light_pos.z *= -1.0f;
    push_k.model_matrix = glm::translate(light_pos) * glm::scale(vector3_t(20.0f));

    push_k.ws_light_direction = -glm::normalize(g_lighting->ws_light_position[0]);

    command_buffer_push_constant(&push_k, sizeof(push_k), 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sun_pipeline->layout, &queue->q);

    command_buffer_draw(&queue->q, 4, 1, 0, 0);

    matrix4_t model_matrix_transpose_rotation = push_k.model_matrix;

    camera_t *camera = get_camera_bound_to_3d_output();

    matrix4_t view_matrix_no_translation = camera->v_m;
    matrix3_t rotation_part = matrix3_t(view_matrix_no_translation);
    rotation_part = glm::transpose(rotation_part);

    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            model_matrix_transpose_rotation[i][j] = rotation_part[i][j];
        }
    }

    view_matrix_no_translation[3][0] = 0;
    view_matrix_no_translation[3][1] = 0;
    view_matrix_no_translation[3][2] = 0;

    vector4_t sun_ndc = camera->p_m * view_matrix_no_translation * push_k.model_matrix * vector4_t(vector3_t(0, 0, 0), 1.0);

    sun_ndc /= sun_ndc.w;
    g_lighting->sun.ss_light_pos = (vector2_t(sun_ndc) + vector2_t(1.0f)) / 2.0f;
    g_lighting->sun.ss_light_pos.y = 1.0f - g_lighting->sun.ss_light_pos.y;
}

void make_dfr_rendering_data(void)
{
    // ---- Make deferred rendering render pass ----
    g_dfr_rendering->dfr_render_pass = g_render_pass_manager->add("render_pass.deferred_render_pass"_hash);
    auto *dfr_render_pass = g_render_pass_manager->get(g_dfr_rendering->dfr_render_pass);
    {
        render_pass_attachment_t attachments[] = { render_pass_attachment_t{get_swapchain_format(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                   render_pass_attachment_t{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                   render_pass_attachment_t{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                   render_pass_attachment_t{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                   render_pass_attachment_t{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                   render_pass_attachment_t{get_device_supported_depth_format(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} };
        render_pass_subpass_t subpasses[3] = {};
        subpasses[0].set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        subpasses[0].enable_depth = 1;
        subpasses[0].depth_attachment = render_pass_attachment_reference_t{ 5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        
        subpasses[1].set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        
        subpasses[1].set_input_attachment_references(render_pass_attachment_reference_t{ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                     render_pass_attachment_reference_t{ 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

        subpasses[2].set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        subpasses[2].enable_depth = 1;
        subpasses[2].depth_attachment = render_pass_attachment_reference_t{ 5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        subpasses[2].set_input_attachment_references(render_pass_attachment_reference_t{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        render_pass_dependency_t dependencies[4] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        dependencies[2] = make_render_pass_dependency(1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      2, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        dependencies[3] = make_render_pass_dependency(2, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        
        make_render_pass(dfr_render_pass, {6, attachments}, {3, subpasses}, {4, dependencies});
    }

    // ---- Make deferred rendering framebuffer ----
    g_dfr_rendering->dfr_framebuffer = g_framebuffer_manager->add("framebuffer.deferred_fbo"_hash);
    auto *dfr_framebuffer = g_framebuffer_manager->get(g_dfr_rendering->dfr_framebuffer);
    {
        uint32_t w = g_dfr_rendering->backbuffer_res.width, h = g_dfr_rendering->backbuffer_res.height;

        // May have more attachments (for example for bloom...) : ALL post processing effects which need rendering to a separate image (and are cheap), will be part of the "initialization" subpass of the deferred render pass
        image_handle_t final_tx_hdl = g_image_manager->add("image2D.fbo_final"_hash);
        auto *final_tx = g_image_manager->get(final_tx_hdl);
        image_handle_t albedo_tx_hdl = g_image_manager->add("image2D.fbo_albedo"_hash);
        auto *albedo_tx = g_image_manager->get(albedo_tx_hdl);
        image_handle_t position_tx_hdl = g_image_manager->add("image2D.fbo_position"_hash);
        auto *position_tx = g_image_manager->get(position_tx_hdl);
        image_handle_t normal_tx_hdl = g_image_manager->add("image2D.fbo_normal"_hash);
        auto *normal_tx = g_image_manager->get(normal_tx_hdl);
        image_handle_t sun_tx_hdl = g_image_manager->add("image2D.fbo_sun"_hash);
        auto *sun_tx = g_image_manager->get(sun_tx_hdl);
        image_handle_t depth_tx_hdl = g_image_manager->add("image2D.fbo_depth"_hash);
        auto *depth_tx = g_image_manager->get(depth_tx_hdl);

        make_framebuffer_attachment(final_tx, w, h, get_swapchain_format(), 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(albedo_tx, w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(position_tx, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(normal_tx, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(sun_tx, w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(depth_tx, w, h, get_device_supported_depth_format(), 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 2);

        image2d_t color_attachments[5] = {};
        color_attachments[0] = *final_tx;
        color_attachments[1] = *albedo_tx;
        color_attachments[2] = *position_tx;
        color_attachments[3] = *normal_tx;
        color_attachments[4] = *sun_tx;
        
        // Can put final_tx as the pointer to the array because, the other 3 textures will be stored contiguously just after it in memory
        make_framebuffer(dfr_framebuffer, w, h, 1, dfr_render_pass, {5, color_attachments}, depth_tx);
    }

    uniform_layout_handle_t gbuffer_layout_hdl = g_uniform_layout_manager->add("descriptor_set_layout.g_buffer_layout"_hash);
    auto *gbuffer_layout_ptr = g_uniform_layout_manager->get(gbuffer_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *gbuffer_layout_ptr = make_uniform_layout(&layout_info);
    }

    uniform_layout_handle_t gbuffer_input_layout_hdl = g_uniform_layout_manager->add("descriptor_set_layout.deferred_layout"_hash);
    auto *gbuffer_input_layout_ptr = g_uniform_layout_manager->get(gbuffer_input_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        layout_info.push(1, 3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        *gbuffer_input_layout_ptr = make_uniform_layout(&layout_info);
    }

    g_dfr_rendering->dfr_g_buffer_group = g_uniform_group_manager->add("uniform_group.g_buffer"_hash);
    auto *gbuffer_group_ptr = g_uniform_group_manager->get(g_dfr_rendering->dfr_g_buffer_group);
    {
        *gbuffer_group_ptr = make_uniform_group(gbuffer_layout_ptr, g_uniform_pool);

        image_handle_t final_tx_hdl = g_image_manager->get_handle("image2D.fbo_final"_hash);
        image2d_t *final_tx = g_image_manager->get(final_tx_hdl);
        
        image_handle_t position_tx_hdl = g_image_manager->get_handle("image2D.fbo_position"_hash);
        image2d_t *position_tx = g_image_manager->get(position_tx_hdl);
        
        image_handle_t normal_tx_hdl = g_image_manager->get_handle("image2D.fbo_normal"_hash);
        image2d_t *normal_tx = g_image_manager->get(normal_tx_hdl);

        image_handle_t sun_tx_hdl = g_image_manager->get_handle("image2D.fbo_sun"_hash);
        image2d_t *sun_tx = g_image_manager->get(sun_tx_hdl);
        
        update_uniform_group(gbuffer_group_ptr,
                             update_binding_t{TEXTURE, final_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{TEXTURE, position_tx, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{TEXTURE, normal_tx, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{TEXTURE, sun_tx, 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_dfr_rendering->dfr_subpass_group = g_uniform_group_manager->add("descriptor_set.deferred_descriptor_sets"_hash);
    auto *gbuffer_input_group_ptr = g_uniform_group_manager->get(g_dfr_rendering->dfr_subpass_group);
    {
        image_handle_t albedo_tx_hdl = g_image_manager->get_handle("image2D.fbo_albedo"_hash);
        auto *albedo_tx = g_image_manager->get(albedo_tx_hdl);
        
        image_handle_t position_tx_hdl = g_image_manager->get_handle("image2D.fbo_position"_hash);
        auto *position_tx = g_image_manager->get(position_tx_hdl);
        
        image_handle_t normal_tx_hdl = g_image_manager->get_handle("image2D.fbo_normal"_hash);
        auto *normal_tx = g_image_manager->get(normal_tx_hdl);

        image_handle_t sun_tx_hdl = g_image_manager->get_handle("image2D.fbo_sun"_hash);
        image2d_t *sun_tx = g_image_manager->get(sun_tx_hdl);

        *gbuffer_input_group_ptr = make_uniform_group(gbuffer_input_layout_ptr, g_uniform_pool);
        update_uniform_group(gbuffer_input_group_ptr,
                             update_binding_t{INPUT_ATTACHMENT, albedo_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{INPUT_ATTACHMENT, position_tx, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{INPUT_ATTACHMENT, normal_tx, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                             update_binding_t{INPUT_ATTACHMENT, sun_tx, 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    g_dfr_rendering->dfr_lighting_ppln = g_pipeline_manager->add("pipeline.deferred_pipeline"_hash);
    auto *deferred_ppln = g_pipeline_manager->get(g_dfr_rendering->dfr_lighting_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/deferred_lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/deferred_lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("descriptor_set_layout.deferred_layout"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash));
        shader_pk_data_t push_k{ 160, 0, VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blend_states(blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic_states(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, g_dfr_rendering->backbuffer_res, blend_states, nullptr,
                                    false, 0.0f, dynamic_states, dfr_render_pass, 1, info);
        deferred_ppln->info = info;
        make_graphics_pipeline(deferred_ppln);
    }
}

void begin_shadow_offscreen(uint32_t shadow_map_width, uint32_t shadow_map_height, gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_lighting->shadows.pass, g_lighting->shadows.fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_depth(1.0f, 0));
    
    VkViewport viewport = {};
    init_viewport(0, 0, shadow_map_width, shadow_map_height, 0.0f, 1.0f, &viewport);
    
    vkCmdSetViewport(queue->q, 0, 1, &viewport);
    vkCmdSetDepthBias(queue->q, 0.0f, 0.0f, 0.0f);

    // Render the world to the shadow map
}

void end_shadow_offscreen(gpu_command_queue_t *queue)
{
    command_buffer_end_render_pass(&queue->q);
}

void begin_deferred_rendering(uint32_t image_index /* to_t remove in the future */, gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_dfr_rendering->dfr_render_pass,
                             g_dfr_rendering->dfr_framebuffer,
                             VK_SUBPASS_CONTENTS_INLINE,
                             // Clear values hre
                             init_clear_color_color(0, 0.4, 0.7, 0),
                             init_clear_color_color(0, 0.4, 0.7, 0),
                             init_clear_color_color(0, 0.4, 0.7, 0),
                             init_clear_color_color(0, 0.4, 0.7, 0),
                             init_clear_color_color(0, 0.0, 0.0, 1),
                             init_clear_color_depth(1.0f, 0));

    command_buffer_set_viewport(g_dfr_rendering->backbuffer_res.width, g_dfr_rendering->backbuffer_res.height, 0.0f, 1.0f, &queue->q);
    command_buffer_set_line_width(2.0f, &queue->q);

    // User renders what is needed ...
}    

void do_lighting_and_transition_to_alpha_rendering(const matrix4_t &view_matrix, gpu_command_queue_t *queue)
{
    queue->next_subpass(VK_SUBPASS_CONTENTS_INLINE);

    auto *dfr_lighting_ppln = g_pipeline_manager->get(g_dfr_rendering->dfr_lighting_ppln);

    command_buffer_bind_pipeline(&dfr_lighting_ppln->pipeline, &queue->q);

    auto *dfr_subpass_group = g_uniform_group_manager->get(g_dfr_rendering->dfr_subpass_group);
    auto *irradiance_cubemap = g_uniform_group_manager->get(g_atmosphere->atmosphere_irradiance_uniform_group);
    
    VkDescriptorSet deferred_sets[] = {*dfr_subpass_group, *irradiance_cubemap};
    
    command_buffer_bind_descriptor_sets(&dfr_lighting_ppln->layout, {2, deferred_sets}, &queue->q);
    
    struct deferred_lighting_push_k_t
    {
	vector4_t light_position;
	matrix4_t view_matrix;
        matrix4_t inverse_view_matrix;
    } deferred_push_k;
    
    deferred_push_k.light_position = vector4_t(glm::normalize(-g_lighting->ws_light_position[0]), 1.0f);
    deferred_push_k.view_matrix = view_matrix;
    deferred_push_k.inverse_view_matrix = glm::inverse(view_matrix);
    
    command_buffer_push_constant(&deferred_push_k, sizeof(deferred_push_k), 0, VK_SHADER_STAGE_FRAGMENT_BIT, dfr_lighting_ppln->layout, &queue->q);
    command_buffer_draw(&queue->q, 4, 1, 0, 0);

    queue->next_subpass(VK_SUBPASS_CONTENTS_INLINE);
    // Now start alpha blending scene (particles, etc...)
}

void end_deferred_rendering(gpu_command_queue_t *queue)
{
    queue->end_render_pass();
}

internal_function float32_t float16_to_float32(uint16_t f)
{
    int f32i32 =  ((f & 0x8000) << 16);
    f32i32 |= ((f & 0x7fff) << 13) + 0x38000000;

    float ret;
    memcpy(&ret, &f32i32, sizeof(float32_t));
    return ret;
}

internal_function vector4_t glsl_texture_function(dbg_pfx_frame_capture_t::dbg_sampler2d_t *sampler, const vector2_t &uvs)
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
    VkSubresourceLayout layout = get_image_subresource_layout(&sampler->blitted_linear_image.image, &subresources);

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

internal_function uint32_t get_pixel_color(image2d_t *image, mapped_gpu_memory_t *memory, float32_t x, float32_t y, VkFormat format, const resolution_t &resolution)
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
    VkSubresourceLayout layout = get_image_subresource_layout(&image->image, &subresources);

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

vector4_t invoke_glsl_code(dbg_pfx_frame_capture_t *capture, const vector2_t &uvs, camera_t *camera);

void dbg_handle_input(input_state_t *input_state)
{
    /*if (input_state->keyboard[keyboard_button_type_t::P].is_down)
    {
        camera_t *camera = get_camera_bound_to_3d_output();

        update_shadows(100.0f
                       , 1.0f
                       , camera->fov
                       , camera->asp
                       , camera->p
                       , camera->d
                       , camera->u);
                       }*/
    
    if (g_postfx->dbg_in_frame_capture_mode)
    {
        if (input_state->mouse_buttons[mouse_button_type_t::MOUSE_LEFT].is_down)
        {
            // Isn't normalized
            g_postfx->dbg_capture.window_cursor_position = input_state->normalized_cursor_position;
            g_postfx->dbg_capture.window_cursor_position.y = input_state->window_height - g_postfx->dbg_capture.window_cursor_position.y;

            g_postfx->dbg_capture.backbuffer_cursor_position = g_postfx->dbg_capture.window_cursor_position -
                vector2_t(g_postfx->render_rect2D.offset.x, g_postfx->render_rect2D.offset.y);

            g_postfx->dbg_capture.backbuffer_cursor_position /= vector2_t(g_postfx->render_rect2D.extent.width, g_postfx->render_rect2D.extent.height);
            g_postfx->dbg_capture.backbuffer_cursor_position = g_postfx->dbg_capture.backbuffer_cursor_position * 2.0f - vector2_t(1.0f);
            
            vector4_t final_color = invoke_glsl_code(&g_postfx->dbg_capture,
                                                     vector2_t((g_postfx->dbg_capture.backbuffer_cursor_position.x + 1.0f) / 2.0f,
                                                               (g_postfx->dbg_capture.backbuffer_cursor_position.y + 1.0f) / 2.0f),
                                                     get_camera_bound_to_3d_output());
            uint32_t final_color_ui = vec4_color_to_ui32b(final_color);
            
            
            g_postfx->dbg_capture.mapped_image = g_postfx->dbg_capture.blitted_image_linear.construct_map();
            g_postfx->dbg_capture.mapped_image.begin();
            
            uint32_t pixel_color = get_pixel_color(&g_postfx->dbg_capture.blitted_image_linear,
                                                   &g_postfx->dbg_capture.mapped_image,
                                                   (g_postfx->dbg_capture.backbuffer_cursor_position.x + 1.0f) / 2.0f,
                                                   (g_postfx->dbg_capture.backbuffer_cursor_position.y + 1.0f) / 2.0f,
                                                   VK_FORMAT_B8G8R8A8_UNORM,
                                                   get_backbuffer_resolution());
            g_postfx->dbg_capture.mapped_image.end();

                        // Print cursor position and the color of the pixel
            persist_var char buffer[100];
            sprintf(buffer, "sh_out = 0x%08x db_out = 0x%08x\n", pixel_color, final_color_ui);
            console_out(buffer);

            input_state->mouse_buttons[mouse_button_type_t::MOUSE_LEFT].is_down = is_down_t::NOT_DOWN;
        }
    }
}

internal_function void dbg_make_frame_capture_blit_image(image2d_t *dst_img, uint32_t w, uint32_t h, VkFormat format, VkImageAspectFlags aspect)
{
    VkImageAspectFlags aspect_flags = aspect;
    init_image(w, h, format, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1, dst_img, 0);
    init_image_view(&dst_img->image, format, aspect_flags, &dst_img->image_view, VK_IMAGE_VIEW_TYPE_2D, 1);
}

internal_function void dbg_make_frame_capture_output_blit_image(image2d_t *dst_image, image2d_t *dst_image_linear, uint32_t w, uint32_t h, VkFormat format, VkImageAspectFlags aspect)
{
    make_framebuffer_attachment(dst_image, w, h, format, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 2);
    dbg_make_frame_capture_blit_image(dst_image_linear, w, h, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

internal_function void dbg_make_frame_capture_uniform_data(dbg_pfx_frame_capture_t *capture)
{
    uniform_layout_handle_t layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.pfx_single_tx_output"_hash);
    uniform_layout_t *layout_ptr = g_uniform_layout_manager->get(layout_hdl);
    
    capture->blitted_image_uniform = make_uniform_group(layout_ptr, g_uniform_pool);
    update_uniform_group(&capture->blitted_image_uniform, update_binding_t{TEXTURE, &capture->blitted_image, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
}

internal_function void dbg_make_frame_capture_samplers(pfx_stage_t *stage, dbg_pfx_frame_capture_t *capture)
{
    capture->sampler_count = stage->input_count;
    framebuffer_t *stage_fbo = g_framebuffer_manager->get(stage->fbo);
    for (uint32_t i = 0; i < capture->sampler_count; ++i)
    {
        dbg_pfx_frame_capture_t::dbg_sampler2d_t *sampler = &capture->samplers[i];
        sampler->index = 0;
        sampler->original_image = stage->inputs[i];
        dbg_make_frame_capture_blit_image(&sampler->blitted_linear_image, stage_fbo->extent.width, stage_fbo->extent.height, sampler->format, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

internal_function void dbg_make_frame_capture_data(gpu_command_queue_pool_t *pool)
{
    // Trying to debug the SSR stage
    pfx_stage_t *stage = &g_postfx->ssr_stage;
    framebuffer_t *stage_fbo = g_framebuffer_manager->get(stage->fbo);
    
    g_postfx->ssr_stage.input_count = 3;
    
    g_postfx->ssr_stage.inputs[0] = g_image_manager->get_handle("image2D.fbo_final"_hash);
    g_postfx->ssr_stage.inputs[1] = g_image_manager->get_handle("image2D.fbo_position"_hash);
    g_postfx->ssr_stage.inputs[2] = g_image_manager->get_handle("image2D.fbo_normal"_hash);

    g_postfx->ssr_stage.output = g_image_manager->get_handle("image2D.pfx_ssr_color"_hash);

    dbg_pfx_frame_capture_t *frame_capture = &g_postfx->dbg_capture;
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
                                             VK_IMAGE_ASPECT_COLOR_BIT);
    
    transition_image_layout(&frame_capture->blitted_image.image, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, pool);
    
    // Initialize the uniform group for the blitted image
    dbg_make_frame_capture_uniform_data(frame_capture);
    // Initialize the samplers
    g_postfx->dbg_capture.sampler_count = 3;
    g_postfx->dbg_capture.samplers[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    g_postfx->dbg_capture.samplers[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    g_postfx->dbg_capture.samplers[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    g_postfx->dbg_capture.samplers[0].resolution = (resolution_t)stage_fbo->extent;
    g_postfx->dbg_capture.samplers[1].resolution = (resolution_t)stage_fbo->extent;
    g_postfx->dbg_capture.samplers[2].resolution = (resolution_t)stage_fbo->extent;
    dbg_make_frame_capture_samplers(stage, frame_capture);
}

void pfx_stage_t::introduce_to_managers(const constant_string_t &ppln_name,
                                   const constant_string_t &fbo_name,
                                   const constant_string_t &group_name)
{
    ppln = g_pipeline_manager->add(ppln_name);
    fbo = g_framebuffer_manager->add(fbo_name);
    output_group = g_uniform_group_manager->add(group_name);        
}

internal_function void make_pfx_stage(pfx_stage_t *dst_stage,
                                      const shader_modules_t &shader_modules,
                                      const shader_uniform_layouts_t &uniform_layouts,
                                      const resolution_t &resolution,
                                      image2d_t *stage_output,
                                      uniform_layout_t *single_tx_layout_ptr,
                                      render_pass_t *pfx_render_pass)
{
    auto *stage_pipeline = g_pipeline_manager->get(dst_stage->ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        resolution_t backbuffer_res = resolution;
        shader_modules_t modules = shader_modules;
        shader_uniform_layouts_t layouts = uniform_layouts;
        shader_pk_data_t push_k {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic (VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, nullptr,
                                    false, 0.0f, dynamic, pfx_render_pass, 0, info);
        stage_pipeline->info = info;
        make_graphics_pipeline(stage_pipeline);
    }

    auto *stage_framebuffer = g_framebuffer_manager->get(dst_stage->fbo);
    {
        auto *tx_ptr = stage_output;
        make_framebuffer_attachment(tx_ptr, resolution.width, resolution.height,
                                    get_swapchain_format(), 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 2);
        make_framebuffer(stage_framebuffer, resolution.width, resolution.height, 1, pfx_render_pass, {1, tx_ptr}, nullptr);
    }

    auto *output_group = g_uniform_group_manager->get(dst_stage->output_group);
    {
        *output_group = make_uniform_group(single_tx_layout_ptr, g_uniform_pool);
        update_uniform_group(output_group, update_binding_t{TEXTURE, stage_output, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }
}

void make_postfx_data(gpu_command_queue_pool_t *pool)
{
    g_postfx->pfx_single_tx_layout = g_uniform_layout_manager->add("uniform_layout.pfx_single_tx_output"_hash);
    auto *single_tx_layout_ptr = g_uniform_layout_manager->get(g_postfx->pfx_single_tx_layout);
    {
        uniform_layout_info_t info = {};
        info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *single_tx_layout_ptr = make_uniform_layout(&info);
    }
    
    g_postfx->final_stage.fbo = g_framebuffer_manager->add("framebuffer.display_fbo"_hash, get_swapchain_image_count());

    VkFormat swapchain_format = get_swapchain_format();
    
    g_postfx->final_render_pass = g_render_pass_manager->add("render_pass.final_render_pass"_hash);
    auto *final_render_pass = g_render_pass_manager->get(g_postfx->final_render_pass);
    {
        // only_t one attachment
        render_pass_attachment_t attachment = { swapchain_format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        render_pass_subpass_t subpass;
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2];
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(final_render_pass, {1, &attachment}, {1, &subpass}, {2, dependencies});
    }
    
    g_postfx->pfx_render_pass = g_render_pass_manager->add("render_pass.pfx_render_pass"_hash);
    auto *pfx_render_pass = g_render_pass_manager->get(g_postfx->pfx_render_pass);
    {
        // only_t one attachment
        render_pass_attachment_t attachment = { swapchain_format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        render_pass_subpass_t subpass;
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2];
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(pfx_render_pass, {1, &attachment}, {1, &subpass}, {2, dependencies});
    }
    // ---- make_t the framebuffer for the swapchain / screen ----
    // ---- only_t has one color attachment, no depth attachment ----
    // ---- uses_t swapchain extent ----
    auto *final_fbo = g_framebuffer_manager->get(g_postfx->final_stage.fbo);
    {
        VkImageView *swapchain_image_views = get_swapchain_image_views();
        
        for (uint32_t i = 0; i < get_swapchain_image_count(); ++i)
        {
            final_fbo[i].extent = get_swapchain_extent();
            
            allocate_memory_buffer(final_fbo[i].color_attachments, 1);
            final_fbo[i].color_attachments[0] = swapchain_image_views[i];
            final_fbo[i].depth_attachment = VK_NULL_HANDLE;
            init_framebuffer(pfx_render_pass, final_fbo[i].extent.width, final_fbo[i].extent.height, 1, &final_fbo[i]);
        }
    }

    g_postfx->final_stage.ppln = g_pipeline_manager->add("graphics_pipeline.pfx_final"_hash);
    auto *final_ppln = g_pipeline_manager->get(g_postfx->final_stage.ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/pfx_final.vert.spv", VK_SHADER_STAGE_VERTEX_BIT}, shader_module_info_t{"shaders/SPV/pfx_final.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_postfx->pfx_single_tx_layout);
        shader_pk_data_t push_k {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic (VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR);
        fill_graphics_pipeline_info(modules, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_swapchain_extent(), blending, nullptr,
                                    false, 0.0f, dynamic, pfx_render_pass, 0, info);
        final_ppln->info = info;
        make_graphics_pipeline(final_ppln);
    }
    
    shader_modules_t modules(shader_module_info_t{"shaders/SPV/pfx_ssr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                             shader_module_info_t{"shaders/SPV/pfx_ssr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
    shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("descriptor_set_layout.g_buffer_layout"_hash),
                                     g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
                                     g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
    image_handle_t ssr_tx_hdl = g_image_manager->add("image2D.pfx_ssr_color"_hash);
    auto *ssr_tx_ptr = g_image_manager->get(ssr_tx_hdl);
    g_postfx->ssr_stage.introduce_to_managers("graphics_pipeline.pfx_ssr"_hash, "framebuffer.pfx_ssr"_hash, "uniform_group.pfx_ssr_output"_hash);
    make_pfx_stage(&g_postfx->ssr_stage, modules, layouts, get_backbuffer_resolution(), ssr_tx_ptr, single_tx_layout_ptr, pfx_render_pass);

    shader_modules_t pre_final_modules(shader_module_info_t{"shaders/SPV/pfx_final.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                       shader_module_info_t{"shaders/SPV/pfx_final.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
    shader_uniform_layouts_t pre_final_layouts(g_postfx->pfx_single_tx_layout);
    image_handle_t pre_final_tx_hdl = g_image_manager->add("image2D.pre_final_color"_hash);
    auto *pre_final_tx_ptr = g_image_manager->get(pre_final_tx_hdl);
    g_postfx->pre_final_stage.introduce_to_managers("graphics_pipeline.pre_final"_hash, "framebuffer.pre_final"_hash, "uniform_group.pre_final_output"_hash);
    make_pfx_stage(&g_postfx->pre_final_stage, pre_final_modules, pre_final_layouts, get_backbuffer_resolution(), pre_final_tx_ptr, single_tx_layout_ptr, pfx_render_pass);

    dbg_make_frame_capture_data(pool);
}

framebuffer_handle_t get_pfx_framebuffer_hdl(void)
{
    return(g_postfx->pre_final_stage.fbo);
}

inline void apply_ssr(gpu_command_queue_t *queue, uniform_group_t *transforms_group, const matrix4_t &view_matrix, const matrix4_t &projection_matrix)
{
    queue->begin_render_pass(g_postfx->pfx_render_pass
                             , g_postfx->ssr_stage.fbo
                             , VK_SUBPASS_CONTENTS_INLINE
                             , init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f));
    VkViewport v = {};
    init_viewport(0, 0, g_dfr_rendering->backbuffer_res.width, g_dfr_rendering->backbuffer_res.height, 0.0f, 1.0f, &v);
    command_buffer_set_viewport(&v, &queue->q);
    {
        auto *pfx_ssr_ppln = g_pipeline_manager->get(g_postfx->ssr_stage.ppln);
        command_buffer_bind_pipeline(&pfx_ssr_ppln->pipeline, &queue->q);

        auto *g_buffer_group = g_uniform_group_manager->get(g_dfr_rendering->dfr_g_buffer_group);
        auto *atmosphere_group = g_uniform_group_manager->get(g_atmosphere->cubemap_uniform_group);
        
        uniform_group_t groups[] = { *g_buffer_group, *atmosphere_group, *transforms_group };
        command_buffer_bind_descriptor_sets(&pfx_ssr_ppln->layout, {3, groups}, &queue->q);

        struct ssr_lighting_push_k_t
        {
            vector4_t ws_light_position;
            matrix4_t view;
            matrix4_t proj;
            vector2_t ss_light_position;
        } ssr_pk;

        ssr_pk.ws_light_position = view_matrix * vector4_t(glm::normalize(-g_lighting->ws_light_position[0]), 0.0f);
        ssr_pk.view = view_matrix;
        ssr_pk.proj = projection_matrix;
        ssr_pk.proj[1][1] *= -1.0f;
        ssr_pk.ss_light_position = g_lighting->sun.ss_light_pos;
        
        command_buffer_push_constant(&ssr_pk, sizeof(ssr_pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_ssr_ppln->layout, &queue->q);
        command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }
    queue->end_render_pass();
}

inline void render_to_pre_final(gpu_command_queue_t *queue)
{
    if (g_postfx->dbg_requested_capture)
    {
        g_postfx->dbg_in_frame_capture_mode = true;
        g_postfx->dbg_requested_capture = false;

        // Do image copy
        blit_image(g_image_manager->get(g_postfx->ssr_stage.output),
                   &g_postfx->dbg_capture.blitted_image,
                   g_dfr_rendering->backbuffer_res.width,
                   g_dfr_rendering->backbuffer_res.height,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   &queue->q);
        copy_image(g_image_manager->get(g_postfx->ssr_stage.output),
                   &g_postfx->dbg_capture.blitted_image_linear,
                   g_dfr_rendering->backbuffer_res.width,
                   g_dfr_rendering->backbuffer_res.height,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   &queue->q);

        for (uint32_t i = 0; i < g_postfx->ssr_stage.input_count; ++i)
        {
            copy_image(g_image_manager->get(g_postfx->ssr_stage.inputs[i]),
                       &g_postfx->dbg_capture.samplers[i].blitted_linear_image,
                       g_dfr_rendering->backbuffer_res.width,
                       g_dfr_rendering->backbuffer_res.height,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       &queue->q);
        }
    }
    
    queue->begin_render_pass(g_postfx->pfx_render_pass, g_postfx->pre_final_stage.fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f));

    uniform_group_t tx_to_render = {};
    
    // TODO: If requested capture mode -> go into capture mode
    if (g_postfx->dbg_in_frame_capture_mode)
    {
        tx_to_render = g_postfx->dbg_capture.blitted_image_uniform;
    }
    else
    {
        tx_to_render = *g_uniform_group_manager->get(g_postfx->ssr_stage.output_group);
    }
    
    VkViewport v = {};
    init_viewport(0, 0, g_dfr_rendering->backbuffer_res.width, g_dfr_rendering->backbuffer_res.height, 0.0f, 1.0f, &v);
    command_buffer_set_viewport(&v, &queue->q);
    {
        auto *pfx_pre_final_ppln = g_pipeline_manager->get(g_postfx->pre_final_stage.ppln);
        command_buffer_bind_pipeline(&pfx_pre_final_ppln->pipeline, &queue->q);

        auto *ssr_group = g_uniform_group_manager->get(g_postfx->ssr_stage.output_group);
        
        uniform_group_t groups[] = { tx_to_render };
        command_buffer_bind_descriptor_sets(&pfx_pre_final_ppln->layout, {1, groups}, &queue->q);

        struct push_k
        {
            vector4_t db;
        } pk;

        if (g_postfx->dbg_in_frame_capture_mode)
        {
            pk.db = vector4_t(0.0f);
        }
        else
        {
            pk.db = vector4_t(-10.0f);
        }
        
        command_buffer_push_constant(&pk, sizeof(pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_pre_final_ppln->layout, &queue->q);
        command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }

    queue->end_render_pass();
}

void apply_pfx_on_scene(gpu_command_queue_t *queue, uniform_group_t *transforms_group, const matrix4_t &view_matrix, const matrix4_t &projection_matrix)
{
    apply_ssr(queue, transforms_group, view_matrix, projection_matrix);
    render_to_pre_final(queue);
}

void render_final_output(uint32_t image_index, gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_postfx->final_render_pass, g_postfx->final_stage.fbo + image_index, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f));

    VkExtent2D swapchain_extent = get_swapchain_extent();
    
    float32_t backbuffer_asp = (float32_t)g_dfr_rendering->backbuffer_res.width / (float32_t)g_dfr_rendering->backbuffer_res.height;
    float32_t swapchain_asp = (float32_t)get_swapchain_extent().width / (float32_t)get_swapchain_extent().height;

    uint32_t rect2D_width, rect2D_height, rect2Dx, rect2Dy;
    
    if (backbuffer_asp >= swapchain_asp)
    {
        rect2D_width = swapchain_extent.width;
        rect2D_height = (uint32_t)((float32_t)swapchain_extent.width / backbuffer_asp);
        rect2Dx = 0;
        rect2Dy = (swapchain_extent.height - rect2D_height) / 2;
    }

    if (backbuffer_asp < swapchain_asp)
    {
        rect2D_width = (uint32_t)(swapchain_extent.height * backbuffer_asp);
        rect2D_height = swapchain_extent.height;
        rect2Dx = (swapchain_extent.width - rect2D_width) / 2;
        rect2Dy = 0;
    }
    
    VkViewport v = {};
    init_viewport(rect2Dx, rect2Dy, rect2D_width, rect2D_height, 0.0f, 1.0f, &v);
    command_buffer_set_viewport(&v, &queue->q);
    {
        auto *pfx_final_ppln = g_pipeline_manager->get(g_postfx->final_stage.ppln);
        command_buffer_bind_pipeline(&pfx_final_ppln->pipeline, &queue->q);

        auto rect2D = make_rect2D(rect2Dx, rect2Dy, rect2D_width, rect2D_height);
        command_buffer_set_rect2D(&rect2D, &queue->q);
        g_postfx->render_rect2D = rect2D;
        
        uniform_group_t groups[] = { *g_uniform_group_manager->get(g_postfx->pre_final_stage.output_group) };
        command_buffer_bind_descriptor_sets(&pfx_final_ppln->layout, {1, groups}, &queue->q);

        struct ssr_lighting_push_k_t
        {
            vector4_t debug;
        } pk;

        pk.debug = vector4_t(0.0f, 0.0f, 0.0f, -10.0f);
        
        command_buffer_push_constant(&pk, sizeof(pk), 0, VK_SHADER_STAGE_FRAGMENT_BIT, pfx_final_ppln->layout, &queue->q);
        command_buffer_draw(&queue->q, 4, 1, 0, 0);
    }
    queue->end_render_pass();
}

internal_function void make_cube_model(gpu_command_queue_pool_t *pool)
{
    model_handle_t cube_model_hdl = g_model_manager->add("model.cube_model"_hash);
    auto *cube_model_ptr = g_model_manager->get(cube_model_hdl);
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

    gpu_buffer_handle_t cube_vbo_hdl = g_gpu_buffer_manager->add("vbo.cube_model_vbo"_hash);
    auto *vbo = g_gpu_buffer_manager->get(cube_vbo_hdl);
    {
        struct vertex_t { vector3_t pos, color; vector2_t uvs; };
        
        vector3_t gray = vector3_t(0.2);
	
        float32_t radius = 1.0f;
	
        persist_var vertex_t vertices[]
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
	
        invoke_staging_buffer_for_device_local_buffer(byte_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, pool, vbo);

        main_binding->buffer = vbo->buffer;
        cube_model_ptr->create_vbo_list();
    }
    
    gpu_buffer_handle_t model_ibo_hdl = g_gpu_buffer_manager->add("ibo.cube_model_ibo"_hash);
    auto *ibo = g_gpu_buffer_manager->get(model_ibo_hdl);
    {
	persist_var uint32_t mesh_indices[] = 
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
	    
	invoke_staging_buffer_for_device_local_buffer(byte_buffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, pool, ibo);

	cube_model_ptr->index_data.index_buffer = ibo->buffer;
    }
}

internal_function int32_t lua_begin_frame_capture(lua_State *state);
internal_function int32_t lua_end_frame_capture(lua_State *state);

void make_cubemap_uniform_layout(void)
{
    uniform_layout_handle_t render_atmosphere_layout_hdl = g_uniform_layout_manager->add("descriptor_set_layout.render_atmosphere_layout"_hash);
    auto *render_atmosphere_layout_ptr = g_uniform_layout_manager->get(render_atmosphere_layout_hdl);
    {
        uniform_layout_info_t layout_info = {};
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *render_atmosphere_layout_ptr = make_uniform_layout(&layout_info);
    }
}

void initialize_game_3d_graphics(gpu_command_queue_pool_t *pool, input_state_t *input_state)
{
    g_dfr_rendering->backbuffer_res = {(uint32_t)input_state->window_width, (uint32_t)input_state->window_height};
    g_dfr_rendering->backbuffer_res = {2500, 1400};

    make_cubemap_uniform_layout();
    
    make_uniform_pool();
    make_dfr_rendering_data();
    make_camera_data(g_uniform_pool);
    make_shadow_data();
    make_cube_model(pool);
    make_atmosphere_data(g_uniform_pool, pool);
    make_sun_data();
    initialize_particle_rendering();

    add_global_to_lua(script_primitive_type_t::FUNCTION, "begin_frame_capture", &lua_begin_frame_capture);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "end_frame_capture", &lua_end_frame_capture);

    clear_linear();
}

// 2D Graphics


void initialize_game_2d_graphics(gpu_command_queue_pool_t *pool)
{
    make_postfx_data(pool);

    // TODO: URGENT: REMOVE THIS ONCE DONE WITH DEBUGGING SHADOWS
    auto *dbg_shadow_ppln = &g_lighting->shadows.dbg_shadow_tx_quad_ppln;
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/screen_quad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/screen_quad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_lighting->shadows.ulayout);
        shader_pk_data_t push_k {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic (VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR);
        fill_graphics_pipeline_info(modules, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, nullptr,
                                    false, 0.0f, dynamic, g_render_pass_manager->get(g_postfx->pfx_render_pass), 0, info);
        dbg_shadow_ppln->info = info;
        make_graphics_pipeline(dbg_shadow_ppln);
    }

    clear_linear();
}

void hotreload_assets_if_changed(void)
{
    bool hotreloading = false;
    for (uint32_t i = 0; i < g_pipeline_manager->count; ++i)
    {
        graphics_pipeline_t *ppln = g_pipeline_manager->get(i);
        for (uint32_t shader_module = 0; shader_module < ppln->info->modules.count; ++shader_module)
        {
            shader_module_info_t *info = &ppln->info->modules.modules[shader_module];
            if (has_file_changed(info->file_handle))
            {
                if (!hotreloading)
                {
                    hotreloading = true;
                    idle_gpu();

                    ppln->destroy();
                    make_graphics_pipeline(ppln);
                }
            }
        }
    }
}

void destroy_graphics(void)
{
    //    vkDestroyDescriptorPool(gpu->logical_device, g_uniform_pool, nullptr);
}

internal_function int32_t lua_begin_frame_capture(lua_State *state)
{
    // Copy images into samplers blitted images
    g_postfx->dbg_requested_capture = true;

    console_out("=== Begin frame capture ===\n");
    
    enable_cursor_display();
    
    return(0);
}

internal_function int32_t lua_end_frame_capture(lua_State *state)
{
    g_postfx->dbg_in_frame_capture_mode = false;

    console_out("=== End frame capture ===\n");
    
    disable_cursor_display();
    
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

internal_function vec4 glsl_texture_func(sampler2d_ptr_t &sampler, vec2 &uvs);

#define Push_K struct pk_t
#define in
#define out funccall_inout::
#define inout funccall_inout::
#define uniform internal_function
#define texture glsl_texture_func

#define VS_DATA struct vs_data_t

using sampler2D = sampler2d_ptr_t;

namespace glsl
{
// =============== INCLUDE GLSL CODE HERE =================
#include "../assets/shaders/pfx_ssr_c.hpp"
// ===============
}

internal_function vec4 glsl_texture_func(sampler2d_ptr_t &sampler, vec2 &uvs)
{
    vector4_t sampled = glsl_texture_function(sampler.sampler_data, vector2_t(uvs.x, uvs.y));
    return vec4(sampled.x, sampled.y, sampled.z, sampled.w);
}

#define PUSH_CONSTANT(name, push_k_data) glsl::##name = push_k_data
#define SET_UNIFORM(name, uni) glsl::##name = uni
#define SET_VS_DATA(name, data) glsl::##name = data

vector4_t invoke_glsl_code(dbg_pfx_frame_capture_t *capture, const vector2_t &uvs, camera_t *camera)
{
    glsl::pk_t pk;

    vector4_t n_light_dir = camera->v_m * vector4_t(glm::normalize(-g_lighting->ws_light_position[0]), 0.0f);
    
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

    capture->prepare_memory_maps();
    {
        // If in debug mode of course
        __debugbreak();
        glsl::main();
    }
    capture->end_memory_maps();

    vector4_t final_color = vector4_t(glsl::final_color.x, glsl::final_color.y, glsl::final_color.z, 1.0f);
    return(final_color);
}




// Different formats
mesh_t load_obj_format_mesh(const char *path, gpu_command_queue_pool_t *cmd_pool)
{
    return {};
}

/*struct mesh_t
{
    enum buffer_type_t { VERTEX, NORMAL, UVS, COLOR, JOINT_INDICES, JOINT_WEIGHT, EXTRA, INVALID_BUFFER_TYPE };
    
    persist_var constexpr uint32_t MAX_BUFFERS = 10;
    gpu_buffer_t buffers[MAX_BUFFERS];

    model_t attribute_and_binding_information;
};*/

void push_buffer_to_mesh(buffer_type_t buffer_type, mesh_t *mesh)
{
    mesh->buffer_types_stack[mesh->buffer_count++] = buffer_type;
    // Validate the buffer type in the buffer object
    mesh->buffers[buffer_type].type = buffer_type;
}

bool32_t mesh_has_buffer_type(buffer_type_t buffer_type, mesh_t *mesh)
{
    return(mesh->buffers[buffer_type].type == buffer_type);
}

mesh_buffer_t *get_mesh_buffer_object(buffer_type_t buffer_type, mesh_t *mesh)
{
    if (mesh_has_buffer_type(buffer_type, mesh))
    {
        return(&mesh->buffers[buffer_type]);
    }
    return(nullptr);
}

struct custom_mesh_header_t
{
    uint32_t vertices_offset;
    uint32_t vertices_size;
    
    uint32_t normals_offset;
    uint32_t normals_size;
    
    uint32_t uvs_offset;
    uint32_t uvs_size;
    
    uint32_t affected_joint_weights_offset;
    uint32_t affected_joint_weights_size;
    
    uint32_t affected_joint_ids_offset;
    uint32_t affected_joint_ids_size;
    
    uint32_t indices_offset;
    uint32_t indices_size;
};

// First work on this
mesh_t load_custom_mesh_format_mesh(const char *path, gpu_command_queue_pool_t *cmd_pool)
{
    file_handle_t mesh_file_handle = create_file(path, file_type_flags_t::BINARY | file_type_flags_t::ASSET);
    file_contents_t mesh_data = read_file_tmp(mesh_file_handle);
    remove_and_destroy_file(mesh_file_handle);

    mesh_t mesh = {};

    custom_mesh_header_t *header = (custom_mesh_header_t *)mesh_data.content;

    if (header->indices_size)
    {
        push_buffer_to_mesh(buffer_type_t::INDICES, &mesh);
        mesh_buffer_t *indices_gpu_buffer = get_mesh_buffer_object(buffer_type_t::INDICES, &mesh);
        uint32_t *indices = (uint32_t *)(mesh_data.content + header->indices_offset);
        make_unmappable_gpu_buffer(&indices_gpu_buffer->gpu_buffer,
                                   header->indices_size,
                                   mesh_data.content + header->indices_offset,
                                   gpu_buffer_usage_t::INDEX_BUFFER,
                                   cmd_pool);

        mesh.index_data.index_type = VK_INDEX_TYPE_UINT32;
        mesh.index_data.index_offset = 0;
        mesh.index_data.index_count = header->indices_size / sizeof(uint32_t);
        mesh.index_data.index_buffer = indices_gpu_buffer->gpu_buffer.buffer;
    }
    
    push_buffer_to_mesh(buffer_type_t::VERTEX, &mesh);
    {
        // Create vertices buffer
        mesh_buffer_t *vertices_gpu_buffer = get_mesh_buffer_object(buffer_type_t::VERTEX, &mesh);
        vector3_t *vertices = (vector3_t *)(mesh_data.content + header->vertices_offset);
        make_unmappable_gpu_buffer(&vertices_gpu_buffer->gpu_buffer,
                                   header->vertices_size,
                                   mesh_data.content + header->vertices_offset,
                                   gpu_buffer_usage_t::VERTEX_BUFFER,
                                   cmd_pool);
    }
    
    if (header->normals_size)
    {
        push_buffer_to_mesh(buffer_type_t::NORMAL, &mesh);
        mesh_buffer_t *normals_gpu_buffer = get_mesh_buffer_object(buffer_type_t::NORMAL, &mesh);
        make_unmappable_gpu_buffer(&normals_gpu_buffer->gpu_buffer,
                                   header->normals_size,
                                   mesh_data.content + header->normals_offset,
                                   gpu_buffer_usage_t::VERTEX_BUFFER,
                                   cmd_pool);
        
    }
    if (header->uvs_size)
    {
        push_buffer_to_mesh(buffer_type_t::UVS, &mesh);
        mesh_buffer_t *uvs_gpu_buffer = get_mesh_buffer_object(buffer_type_t::UVS, &mesh);
        make_unmappable_gpu_buffer(&uvs_gpu_buffer->gpu_buffer,
                                   header->uvs_size,
                                   mesh_data.content + header->uvs_offset,
                                   gpu_buffer_usage_t::VERTEX_BUFFER,
                                   cmd_pool);        
    }
    if (header->affected_joint_weights_size)
    {
        push_buffer_to_mesh(buffer_type_t::JOINT_WEIGHT, &mesh);
        mesh_buffer_t *weights_gpu_buffer = get_mesh_buffer_object(buffer_type_t::JOINT_WEIGHT, &mesh);
        vector3_t *weights = (vector3_t *)(mesh_data.content + header->affected_joint_weights_offset);
        make_unmappable_gpu_buffer(&weights_gpu_buffer->gpu_buffer,
                                   header->affected_joint_weights_size,
                                   mesh_data.content + header->affected_joint_weights_offset,
                                   gpu_buffer_usage_t::VERTEX_BUFFER,
                                   cmd_pool);
    }
    if (header->affected_joint_ids_size)
    {
        push_buffer_to_mesh(buffer_type_t::JOINT_INDICES, &mesh);
        mesh_buffer_t *joint_ids_gpu_buffer = get_mesh_buffer_object(buffer_type_t::JOINT_INDICES, &mesh);
        ivector3_t *joint_ids = (ivector3_t *)(mesh_data.content + header->affected_joint_ids_offset);
        make_unmappable_gpu_buffer(&joint_ids_gpu_buffer->gpu_buffer,
                                   header->affected_joint_ids_size,
                                   mesh_data.content + header->affected_joint_ids_offset,
                                   gpu_buffer_usage_t::VERTEX_BUFFER,
                                   cmd_pool);
    }
    
    return(mesh);
}

model_t make_mesh_attribute_and_binding_information(mesh_t *mesh)
{
    model_t model = {};
    uint32_t binding_count = 0;
    if (mesh_has_buffer_type(buffer_type_t::INDICES, mesh))
    {
        binding_count = mesh->buffer_count - 1;
    }
    else
    {
        binding_count = mesh->buffer_count;
    }
    model.attribute_count = binding_count;
    model.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * model.attribute_count);
    model.binding_count = binding_count;
    model.bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t) * model.binding_count);

    uint32_t current_binding = 0;
    for (uint32_t buffer = 0; buffer < mesh->buffer_count; ++buffer)
    {
        if (mesh->buffer_types_stack[buffer] != buffer_type_t::INDICES)
        {
            // For vertex-type buffers only (vertices buffer, normal buffer, uvs buffer, extra buffers, whatever)
            model_binding_t *binding = &model.bindings[current_binding];
            binding->binding = current_binding;
            binding->buffer = mesh->buffers[mesh->buffer_types_stack[buffer]].gpu_buffer.buffer;
            binding->begin_attributes_creation(model.attributes_buffer);

            VkFormat format;
            uint32_t attribute_size = 0;
            switch(mesh->buffer_types_stack[buffer])
            {
            case buffer_type_t::VERTEX: { format = VK_FORMAT_R32G32B32_SFLOAT; attribute_size = sizeof(vector3_t); } break;
            case buffer_type_t::NORMAL: { format = VK_FORMAT_R32G32B32_SFLOAT; attribute_size = sizeof(vector3_t); } break;
            case buffer_type_t::UVS: { format = VK_FORMAT_R32G32_SFLOAT; attribute_size = sizeof(vector2_t); } break;
            case buffer_type_t::COLOR: { format = VK_FORMAT_R32G32B32_SFLOAT; attribute_size = sizeof(vector3_t); } break;
            case buffer_type_t::JOINT_INDICES: { format = VK_FORMAT_R32G32B32_SINT; attribute_size = sizeof(ivector3_t); } break;
            case buffer_type_t::JOINT_WEIGHT: { format = VK_FORMAT_R32G32B32_SFLOAT; attribute_size = sizeof(vector3_t); } break;
            case buffer_type_t::EXTRA_V3: { format = VK_FORMAT_R32G32B32_SFLOAT; attribute_size = sizeof(vector3_t); } break;
            case buffer_type_t::EXTRA_V2: { format = VK_FORMAT_R32G32_SFLOAT; attribute_size = sizeof(vector2_t); } break;
            case buffer_type_t::EXTRA_V1: { format = VK_FORMAT_R32_SFLOAT; attribute_size = sizeof(float32_t); } break;
            }
            
            binding->push_attribute(current_binding, format, attribute_size);

            binding->end_attributes_creation();
            
            ++current_binding;
        }
    }

    create_mesh_raw_buffer_list(mesh);
    mesh->indexed_data = init_draw_indexed_data_default(1, mesh->index_data.index_count);

    return(model);
}

// Model loading
mesh_t load_mesh(mesh_file_format_t format, const char *path, gpu_command_queue_pool_t *cmd_pool)
{
    switch(format)
    {
    case mesh_file_format_t::CUSTOM_MESH:
        {
            return(load_custom_mesh_format_mesh(path, cmd_pool));
        } break;
    }
    return {};
}

joint_t *get_joint(uint32_t joint_id, skeleton_t *skeleton)
{
    return &skeleton->joints[joint_id];
}

skeleton_t load_skeleton(const char *path)
{
    file_handle_t skeleton_handle = create_file(path, file_type_flags_t::BINARY | file_type_flags_t::ASSET);
    file_contents_t skeleton_data = read_file_tmp(skeleton_handle);
    remove_and_destroy_file(skeleton_handle);

    skeleton_t skeleton = {};
    skeleton.joint_count = *((uint32_t *)skeleton_data.content);

    skeleton.joint_names = (const char **)allocate_free_list(skeleton.joint_count * sizeof(const char *));
    
    // Allocate names
    char *names_bytes = (char *)(skeleton_data.content + sizeof(uint32_t));
    uint32_t char_count = 0;
    for (uint32_t joint = 0; joint < skeleton.joint_count; ++joint)
    {
        uint32_t length = strlen(names_bytes + char_count);
        char *name = (char *)allocate_free_list(length + 1);

        memcpy(name, names_bytes + char_count, length + 1);

        char_count += length + 1;

        skeleton.joint_names[joint] = name;
    }

    skeleton.joints = (joint_t *)allocate_free_list(skeleton.joint_count * sizeof(joint_t));

    byte_t *joint_data_pointer = (byte_t *)(names_bytes + char_count);
    for (uint32_t i = 0; i < skeleton.joint_count; ++i)
    {
        joint_t *current_joint = &skeleton.joints[i];
        memcpy(current_joint, joint_data_pointer + i * sizeof(joint_t), sizeof(joint_t));
    }
    
    return skeleton;
}

animation_cycles_t load_animations(const char *path)
{
    file_handle_t animation_handle = create_file(path, file_type_flags_t::BINARY | file_type_flags_t::ASSET);
    file_contents_t animation_data = read_file_tmp(animation_handle);
    remove_and_destroy_file(animation_handle);

    byte_t *current_byte = animation_data.content;
    
    animation_cycles_t animation_cycles = {};
    memcpy(&animation_cycles.cycle_count, current_byte, sizeof(uint32_t));
    current_byte += sizeof(uint32_t);

    for (uint32_t i = 0; i < animation_cycles.cycle_count; ++i)
    {
        animation_cycle_t *current_cycle = &animation_cycles.cycles[i];

        uint32_t name_length = sizeof(char) * strlen((char *)current_byte) + 1;
        current_cycle->name = (char *)allocate_linear(name_length);
        memcpy((void *)current_cycle->name, current_byte, name_length);
        current_byte += name_length;

        uint32_t *key_frame_count_ptr = (uint32_t *)current_byte;
        uint32_t key_frame_count = *key_frame_count_ptr;
        uint32_t joints_per_key_frame = *(key_frame_count_ptr + 1);
        current_byte += sizeof(uint32_t) * 2;

        byte_t *key_frame_bytes = current_byte;

        current_cycle->key_frame_count = key_frame_count;
        current_cycle->key_frames = (key_frame_t *)allocate_free_list(sizeof(key_frame_t) * key_frame_count);

        uint32_t key_frame_formatted_size = sizeof(float32_t) + sizeof(key_frame_joint_transform_t) * joints_per_key_frame;
    
        for (uint32_t k = 0; k < key_frame_count; ++k)
        {
            key_frame_t *current_frame = &current_cycle->key_frames[k];
            current_frame->joint_transforms_count = joints_per_key_frame;
            current_frame->joint_transforms = (key_frame_joint_transform_t *)allocate_free_list(sizeof(key_frame_joint_transform_t) * joints_per_key_frame);

            float32_t *time_stamp_ptr = (float32_t *)key_frame_bytes;
            current_frame->time_stamp = *time_stamp_ptr;

            byte_t *joint_transforms_ptr = (byte_t *)(time_stamp_ptr) + sizeof(float32_t);

            memcpy(current_frame->joint_transforms, joint_transforms_ptr, sizeof(key_frame_joint_transform_t) * joints_per_key_frame);

            key_frame_bytes += key_frame_formatted_size;
            current_byte += key_frame_formatted_size;

            if (k == key_frame_count - 1)
            {
                current_cycle->total_animation_time = current_frame->time_stamp;
            }
        }
    }

    animation_cycles.destroyed_uniform_groups = (uniform_group_t *)allocate_free_list(sizeof(uniform_group_t) * 30);
    
    return animation_cycles;
}

void push_uniform_group_to_destroyed_uniform_group_cache(animation_cycles_t *cycles, animated_instance_t *instance)
{
    cycles->destroyed_uniform_groups[cycles->destroyed_groups_count++] = instance->group;
    instance->group = VK_NULL_HANDLE;
}

animated_instance_t initialize_animated_instance(gpu_command_queue_pool_t *pool, uniform_layout_t *gpu_ubo_layout, skeleton_t *skeleton, animation_cycles_t *cycles)
{
    animated_instance_t instance = {};
    
    instance.current_animation_time = 0.0f;
    instance.cycles = cycles;
    instance.skeleton = skeleton;
    instance.is_interpolating_between_cycles = 0;
    instance.next_bound_cycle = 0;
    instance.interpolated_transforms = (matrix4_t *)allocate_free_list(sizeof(matrix4_t) * skeleton->joint_count);
    instance.current_joint_transforms = (key_frame_joint_transform_t *)allocate_free_list(sizeof(key_frame_joint_transform_t) * skeleton->joint_count);
    for (uint32_t i = 0; i < skeleton->joint_count; ++i)
    {
        instance.interpolated_transforms[i] = matrix4_t(1.0f);
    }

    make_unmappable_gpu_buffer(&instance.interpolated_transforms_ubo,
                               sizeof(matrix4_t) * skeleton->joint_count,
                               instance.interpolated_transforms,
                               gpu_buffer_usage_t::UNIFORM_BUFFER,
                               pool);

    if (cycles->destroyed_groups_count)
    {
        instance.group = cycles->destroyed_uniform_groups[--cycles->destroyed_groups_count];
    }
    else if (instance.group == VK_NULL_HANDLE)
    {
        instance.group = make_uniform_group(gpu_ubo_layout, g_uniform_pool);
    }
    
    update_uniform_group(&instance.group, update_binding_t{BUFFER, &instance.interpolated_transforms_ubo, 0});
    
    return(instance);
}

void destroy_animated_instance(animated_instance_t *instance)
{
    instance->current_animation_time = 0.0f;
    instance->skeleton = nullptr;
    instance->next_bound_cycle = 0;
    deallocate_free_list(instance->interpolated_transforms);
}

void update_joint(uint32_t current_joint, skeleton_t *skeleton, matrix4_t *transforms_array, const matrix4_t &ms_parent_transform = matrix4_t(1.0f))
{
    // Relative to parent bone
    matrix4_t *local_transform = &transforms_array[current_joint];
    matrix4_t current_transform = ms_parent_transform * *local_transform;

    joint_t *joint_ptr = &skeleton->joints[current_joint];
    for (uint32_t i = 0; i < joint_ptr->children_joint_count; ++i)
    {
        update_joint(joint_ptr->children_joint_ids[i], skeleton, transforms_array, current_transform);
    }

    // Model space transform from default pose to current pose (relative to default position NOT 0,0,0)
    matrix4_t iterative_model_space_transform = current_transform * joint_ptr->inverse_bind_transform;
    transforms_array[current_joint] = iterative_model_space_transform;
}

void interpolate_skeleton_joints_into_instance(float32_t dt, animated_instance_t *instance)
{
    if (instance->is_interpolating_between_cycles)
    {
        animation_cycle_t *next_cycle = &instance->cycles->cycles[instance->next_bound_cycle];
        key_frame_t *first_key_frame_of_next_cycle = &next_cycle->key_frames[0];

        instance->current_animation_time += dt;
        if (instance->current_animation_time > instance->in_between_interpolation_time)
        {
            instance->is_interpolating_between_cycles = 0;
            instance->current_animation_time = 0.0f;
        }
        else
        {
            float32_t progression = instance->current_animation_time / instance->in_between_interpolation_time;
            
            // Interpolate (but all of the transforms are in bone space)
            for (uint32_t joint = 0; joint < instance->skeleton->joint_count; ++joint)
            {
                key_frame_joint_transform_t *joint_next_state = &first_key_frame_of_next_cycle->joint_transforms[joint];

                vector3_t previous_position = instance->current_joint_transforms[joint].position;
                quaternion_t previous_rotation = instance->current_joint_transforms[joint].rotation;
                // TODO: Check if memory usage goes to high in which case, don't cache the current joint transforms
                vector3_t translation = previous_position + (joint_next_state->position - previous_position) * progression;
                quaternion_t rotation = glm::slerp(previous_rotation, joint_next_state->rotation, progression);
                instance->interpolated_transforms[joint] = glm::translate(translation) * glm::mat4_cast(rotation);
            }

            // Convert all the transforms to model space
            update_joint(0, instance->skeleton, instance->interpolated_transforms);
        }
    }

    if (!instance->is_interpolating_between_cycles)
    {
        animation_cycle_t *bound_cycle = &instance->cycles->cycles[instance->next_bound_cycle];
        // Increase the animation time
        instance->current_animation_time += dt;
        if (instance->current_animation_time >= bound_cycle->total_animation_time)
        {
            instance->current_animation_time = 0.0f;
        }

        // Get the frames to which the current time stamp is in between (frame_a and frame_b)
        // frame_a  ----- current_time --- frame_b
        key_frame_t *frame_before = &bound_cycle->key_frames[0];
        key_frame_t *frame_after = &bound_cycle->key_frames[1];
        for (uint32_t i = 0; i < bound_cycle->key_frame_count - 1; ++i)
        {
            float32_t previous_stamp = bound_cycle->key_frames[i].time_stamp;
            float32_t next_stamp = bound_cycle->key_frames[i + 1].time_stamp;

            if (previous_stamp < instance->current_animation_time && next_stamp >= instance->current_animation_time)
            {
                frame_before = &bound_cycle->key_frames[i];
                frame_after = &bound_cycle->key_frames[i + 1];
            }
        }

        float32_t progression = (instance->current_animation_time - frame_before->time_stamp) / (frame_after->time_stamp - frame_before->time_stamp);

        // Interpolate (but all of the transforms are in bone space)
        for (uint32_t joint = 0; joint < instance->skeleton->joint_count; ++joint)
        {
            key_frame_joint_transform_t *joint_previous_state = &frame_before->joint_transforms[joint];
            key_frame_joint_transform_t *joint_next_state = &frame_after->joint_transforms[joint];

            // TODO: Check if memory usage goes to high in which case, don't cache the current joint transforms
            vector3_t translation = joint_previous_state->position + (joint_next_state->position - joint_previous_state->position) * progression;
            quaternion_t rotation = glm::slerp(joint_previous_state->rotation, joint_next_state->rotation, progression);
            instance->current_joint_transforms[joint] = { rotation, translation };
            instance->interpolated_transforms[joint] = glm::translate(translation) * glm::mat4_cast(rotation);
        }

        // Convert all the transforms to model space
        update_joint(0, instance->skeleton, instance->interpolated_transforms);
    }
}

void update_animated_instance_ubo(gpu_command_queue_t *queue, animated_instance_t *instance)
{
    update_gpu_buffer(&instance->interpolated_transforms_ubo,
                      instance->interpolated_transforms,
                      sizeof(matrix4_t) * instance->skeleton->joint_count,
                      0,
                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                      &queue->q);
}


void initialize_particle_rendering(void)
{
    matrix4_t translate = glm::translate(vector3_t(1, 2, 3));
    
    g_particle_rendering->particle_instanced_model.binding_count = 1;
    g_particle_rendering->particle_instanced_model.bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));
    g_particle_rendering->particle_instanced_model.attribute_count = 3;
    g_particle_rendering->particle_instanced_model.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);

    g_particle_rendering->particle_instanced_model.bindings[0].begin_attributes_creation(g_particle_rendering->particle_instanced_model.attributes_buffer);
    // Position
    g_particle_rendering->particle_instanced_model.bindings[0].push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vector3_t));
    // Life
    g_particle_rendering->particle_instanced_model.bindings[0].push_attribute(1, VK_FORMAT_R32_SFLOAT, sizeof(float32_t));
    // Size
    g_particle_rendering->particle_instanced_model.bindings[0].push_attribute(2, VK_FORMAT_R32_SFLOAT, sizeof(float32_t));
    g_particle_rendering->particle_instanced_model.bindings[0].end_attributes_creation();

    g_particle_rendering->particle_instanced_model.bindings[0].input_rate = VK_VERTEX_INPUT_RATE_INSTANCE;


    uniform_layout_handle_t subpass_input_layout_hdl = g_uniform_layout_manager->add("uniform_layout.particle_position_subpass_input"_hash);
    uniform_layout_t *subpass_input_layout_ptr = g_uniform_layout_manager->get(subpass_input_layout_hdl);
    uniform_layout_info_t info = {};
    info.push(1, 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
    *subpass_input_layout_ptr = make_uniform_layout(&info);


    g_particle_rendering->position_subpass_input = make_uniform_group(subpass_input_layout_ptr, g_uniform_pool);
    image_handle_t position_tx_hdl = g_image_manager->get_handle("image2D.fbo_position"_hash);
    auto *position_tx = g_image_manager->get(position_tx_hdl);
    update_uniform_group(&g_particle_rendering->position_subpass_input, update_binding_t{INPUT_ATTACHMENT, position_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
}


particle_spawner_t initialize_particle_spawner(uint32_t max_particle_count, particle_effect_function_t effect, pipeline_handle_t shader, float32_t max_life_length, const char *texture_atlas, uint32_t x_count, uint32_t y_count, uint32_t num_images)
{
    particle_spawner_t particles = {};
    particles.max_particles = max_particle_count;
    particles.particles_stack_head = 0;
    particles.particles = (particle_t *)allocate_free_list(sizeof(particle_t) * particles.max_particles);
    memset(particles.particles, 0, sizeof(particle_t) * particles.max_particles);
    
    particles.rendered_particles_stack_head = 0;
    particles.rendered_particles = (rendered_particle_data_t *)allocate_free_list(sizeof(rendered_particle_data_t) * particles.max_particles);
    memset(particles.rendered_particles, 0, sizeof(rendered_particle_data_t) * particles.max_particles);

    particles.distances_from_camera = (float32_t *)allocate_free_list(sizeof(float32_t) * particles.max_particles);
    memset(particles.distances_from_camera, 0, sizeof(float32_t) * particles.max_particles);
    
    particles.max_dead = max_particle_count / 2;
    particles.dead_count = 0;
    particles.dead = (uint16_t *)allocate_free_list(sizeof(uint16_t) * particles.max_dead);
    
    particles.update = effect;
    
    make_unmappable_gpu_buffer(&particles.gpu_particles_buffer, sizeof(rendered_particle_data_t) * particles.max_particles, particles.particles, gpu_buffer_usage_t::VERTEX_BUFFER, get_global_command_pool());
    
    particles.shader = shader;
    particles.max_life_length = max_life_length;
    particles.num_images = num_images;
    particles.image_x_count = x_count;
    particles.image_y_count = y_count;

    file_handle_t png_handle = create_file(texture_atlas, file_type_flags_t::IMAGE | file_type_flags_t::ASSET);
    external_image_data_t image_data = read_image(png_handle);
    make_texture(&particles.texture_atlas, image_data.width, image_data.height, VK_FORMAT_R8G8B8A8_UNORM, 1, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR);
    transition_image_layout(&particles.texture_atlas.image, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            get_global_command_pool());
    invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                 get_global_command_pool(),
                                                 &particles.texture_atlas,
                                                 (uint32_t)image_data.width,
                                                 (uint32_t)image_data.height);
    transition_image_layout(&particles.texture_atlas.image,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            get_global_command_pool());

    free_external_image_data(&image_data);

    uniform_layout_handle_t single_tx_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash);
    particles.texture_uniform = make_uniform_group(g_uniform_layout_manager->get(single_tx_layout_hdl), g_uniform_pool);
    update_uniform_group(&particles.texture_uniform, update_binding_t{TEXTURE, &particles.texture_atlas, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    
    return(particles);
}


pipeline_handle_t initialize_particle_rendering_shader(const constant_string_t &shader_name, const char *vsh_path, const char *fsh_path, uniform_layout_handle_t texture_atlas)
{
    pipeline_handle_t handle = g_pipeline_manager->add(shader_name);
    graphics_pipeline_t *pipeline = g_pipeline_manager->get(handle);
    graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
    shader_modules_t modules(shader_module_info_t{vsh_path, VK_SHADER_STAGE_VERTEX_BIT},
                             shader_module_info_t{fsh_path, VK_SHADER_STAGE_FRAGMENT_BIT});
    shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash), texture_atlas, g_uniform_layout_manager->get_handle("uniform_layout.particle_position_subpass_input"_hash));
    shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
    shader_blend_states_t blending{blend_type_t::ADDITIVE_BLENDING };
    dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
    fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                                VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_particle_rendering->particle_instanced_model,
                                true, 0.0f, dynamic, g_render_pass_manager->get(g_dfr_rendering->dfr_render_pass), 2, info);
    pipeline->info = info;
    make_graphics_pipeline(pipeline);

    return(handle);
}


void render_particles(gpu_command_queue_t *queue, uniform_group_t *camera_transforms, particle_spawner_t *spawner, void *push_constant, uint32_t push_constant_size)
{
    graphics_pipeline_t *particle_pipeline = g_pipeline_manager->get(spawner->shader);
    
    command_buffer_bind_pipeline(&particle_pipeline->pipeline, &queue->q);

    uniform_group_t groups[] { *camera_transforms, spawner->texture_uniform, g_particle_rendering->position_subpass_input };
    command_buffer_bind_descriptor_sets(&particle_pipeline->layout, {3, groups}, &queue->q);

    VkDeviceSize zero = 0;
    command_buffer_bind_vbos({1, &spawner->gpu_particles_buffer.buffer}, {1, &zero}, 0, 1, &queue->q);

    command_buffer_push_constant(push_constant, push_constant_size, 0, VK_SHADER_STAGE_VERTEX_BIT, particle_pipeline->layout, &queue->q);
    
    command_buffer_draw_instanced(&queue->q, 4, spawner->rendered_particles_stack_head);
}


void particle_spawner_t::clear(void)
{
    rendered_particles_stack_head = 0;
}


void particle_spawner_t::push_for_render(uint32_t index)
{
    particle_t *particle = &particles[index];
    rendered_particle_data_t *rendered_particle = &rendered_particles[rendered_particles_stack_head];
    float32_t *distance_to_camera = &distances_from_camera[rendered_particles_stack_head++];
    camera_t *bound_camera = get_camera_bound_to_3d_output();
    vector3_t direction_to_camera = bound_camera->p - particle->ws_position;
    *distance_to_camera = glm::dot(direction_to_camera, direction_to_camera);
    rendered_particle->ws_position = particle->ws_position;
    rendered_particle->size = particle->size;
    rendered_particle->life = particle->life;
}


void particle_spawner_t::sort_for_render(void)
{
    for (uint32_t i = 1; i < rendered_particles_stack_head; ++i)
    {
        for (uint32_t j = i; j > 0; --j)
        {
            float32_t current_distance = distances_from_camera[j];
            float32_t below_distance = distances_from_camera[j - 1];
            if (current_distance > below_distance)
            {
                // Swap
                distances_from_camera[j - 1] = current_distance;
                distances_from_camera[j] = below_distance;

                rendered_particle_data_t below = rendered_particles[j - 1];
                rendered_particles[j - 1] = rendered_particles[j];
                rendered_particles[j] = below;
            }
            else
            {
                break;
            }
        }
    }
}


void particle_spawner_t::declare_dead(uint32_t index)
{
    particles[index].life = -1.0f;
    
    if (index == particles_stack_head - 1)
    {
        --particles_stack_head;
    }
    else if (dead_count < max_dead)
    {
        dead[dead_count++] = index;
    }
}

    
particle_t *particle_spawner_t::fetch_next_dead_particle(uint32_t *index)
{
    if (dead_count)
    {
        uint16_t next_dead = dead[dead_count - 1];
        --dead_count;
        *index = next_dead;
        return(&particles[next_dead]);
    }
    else if (particles_stack_head < max_particles)
    {
        *index = particles_stack_head;
        particle_t *next_dead = &particles[particles_stack_head++];
        return(next_dead);
    }
    return(nullptr);
}


particle_t *particle_spawner_t::particle(uint32_t *index)
{
    return(fetch_next_dead_particle(index));
}


void create_mesh_raw_buffer_list(mesh_t *mesh)
{
    uint32_t vbo_count = mesh->buffer_count;
    if (mesh_has_buffer_type(buffer_type_t::INDICES, mesh))
    {
        --vbo_count;
    }
    allocate_memory_buffer(mesh->raw_buffer_list, vbo_count);

    uint32_t buffer_index = 0;
    for (uint32_t i = 0; i < mesh->buffer_count; ++i)
    {
        if (mesh->buffer_types_stack[i] != buffer_type_t::INDICES)
        {
            mesh->raw_buffer_list[buffer_index] = mesh->buffers[mesh->buffer_types_stack[i]].gpu_buffer.buffer;
            ++buffer_index;
        }
    }
}

mesh_t initialize_mesh(memory_buffer_view_t<VkBuffer> &vbos, draw_indexed_data_t *index_data, model_index_data_t *model_index_data)
{
    mesh_t mesh;

    allocate_memory_buffer(mesh.raw_buffer_list, vbos.count);

    memcpy(mesh.raw_buffer_list.buffer, vbos.buffer, vbos.count * sizeof(VkBuffer));
    mesh.index_data = *model_index_data;
    mesh.indexed_data = *index_data;

    return(mesh);
}

void destroy_animation_instance(animated_instance_t *instance)
{
    instance->interpolated_transforms_ubo.destroy();
}

void switch_to_cycle(animated_instance_t *instance, uint32_t cycle_index, bool32_t force)
{
    instance->current_animation_time = 0.0f;
    if (!force)
    {
        instance->is_interpolating_between_cycles = 1;
    }
    instance->prev_bound_cycle = instance->next_bound_cycle;
    instance->next_bound_cycle = cycle_index;
}

void initialize_graphics_translation_unit(game_memory_t *memory)
{
    g_gpu_buffer_manager = &memory->graphics_state.gpu_buffer_manager;
    g_image_manager = &memory->graphics_state.image_manager;
    g_framebuffer_manager = &memory->graphics_state.framebuffer_manager;
    g_render_pass_manager = &memory->graphics_state.render_pass_manager;
    g_pipeline_manager = &memory->graphics_state.pipeline_manager;
    g_uniform_layout_manager = &memory->graphics_state.uniform_layout_manager;
    g_uniform_group_manager = &memory->graphics_state.uniform_group_manager;
    g_model_manager = &memory->graphics_state.model_manager;
    g_uniform_pool = &memory->graphics_state.uniform_pool;

    // Will be used for multi-threading the rendering process for extra extra performance !
    g_material_queue_manager = &memory->graphics_state.material_queue_manager;

    // Stuff more linked to rendering the scene: cameras, lighting, ...
    g_cameras = &memory->graphics_state.cameras;
    g_lighting = &memory->graphics_state.lighting;
    g_atmosphere = &memory->graphics_state.atmosphere;

    // Post processing pipeline stuff:
    g_dfr_rendering = &memory->graphics_state.deferred_rendering;
    g_postfx = &memory->graphics_state.postfx;

    // Particles
    g_particle_rendering = &memory->graphics_state.particle_rendering;
}

void handle_window_resize(input_state_t *input_state)
{
    idle_gpu();
    
    // Destroy final framebuffer as well as final framebuffer attachments
    auto *final_fbo = g_framebuffer_manager->get(g_postfx->final_stage.fbo);
    for (uint32_t i = 0; i < get_swapchain_image_count(); ++i)
    {
        destroy_framebuffer(&final_fbo[i].framebuffer);
    }

    g_postfx->final_render_pass = g_render_pass_manager->add("render_pass.final_render_pass"_hash);
    auto *final_render_pass = g_render_pass_manager->get(g_postfx->final_render_pass);

    destroy_render_pass(&final_render_pass->render_pass);
    
    recreate_swapchain(input_state);

    VkFormat swapchain_format = get_swapchain_format();
    {
        // only_t one attachment
        render_pass_attachment_t attachment = { swapchain_format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        render_pass_subpass_t subpass;
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2];
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(final_render_pass, {1, &attachment}, {1, &subpass}, {2, dependencies});
    }
    
    {
        VkImageView *swapchain_image_views = get_swapchain_image_views();
        
        for (uint32_t i = 0; i < get_swapchain_image_count(); ++i)
        {
            final_fbo[i].extent = get_swapchain_extent();
            
            allocate_memory_buffer(final_fbo[i].color_attachments, 1);
            final_fbo[i].color_attachments[0] = swapchain_image_views[i];
            final_fbo[i].depth_attachment = VK_NULL_HANDLE;
            init_framebuffer(final_render_pass, final_fbo[i].extent.width, final_fbo[i].extent.height, 1, &final_fbo[i]);
        }
    }
}
