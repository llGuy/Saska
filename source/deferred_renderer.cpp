#include "atmosphere.hpp"
#include "camera_view.hpp"
#include "deferred_renderer.hpp"


struct backbuffer_t
{
    resolution_t resolution = {};
    render_pass_handle_t render_pass;
    framebuffer_handle_t framebuffer;
};


static backbuffer_t backbuffer;


static void s_backbuffer_init()
{
    backbuffer.resolution = { 2500, 1400 };

    // ---- Make deferred rendering render pass ----
    backbuffer.render_pass = g_render_pass_manager->add("render_pass.deferred_render_pass"_hash);
    auto *dfr_render_pass = g_render_pass_manager->get(backbuffer.render_pass);
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
    backbuffer.framebuffer = g_framebuffer_manager->add("framebuffer.deferred_fbo"_hash);
    auto *dfr_framebuffer = g_framebuffer_manager->get(backbuffer.framebuffer);
    {
        uint32_t w = backbuffer.resolution.width, h = backbuffer.resolution.height;

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

        make_framebuffer_attachment(final_tx, w, h, get_swapchain_format(), 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(albedo_tx, w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(position_tx, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(normal_tx, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(sun_tx, w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, 2);
        make_framebuffer_attachment(depth_tx, w, h, get_device_supported_depth_format(), 1, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 2);

        image2d_t color_attachments[5] = {};
        color_attachments[0] = *final_tx;
        color_attachments[1] = *albedo_tx;
        color_attachments[2] = *position_tx;
        color_attachments[3] = *normal_tx;
        color_attachments[4] = *sun_tx;
        
        // Can put final_tx as the pointer to the array because, the other 3 textures will be stored contiguously just after it in memory
        make_framebuffer(dfr_framebuffer, w, h, 1, dfr_render_pass, {5, color_attachments}, depth_tx);
    }
}


struct renderer_t
{
    pipeline_handle_t lighting;
    uniform_group_handle_t subpass_input;
    uniform_group_handle_t gbuffer;
};


static renderer_t renderer;


static void s_renderer_init()
{
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

    renderer.gbuffer = g_uniform_group_manager->add("uniform_group.g_buffer"_hash);
    auto *gbuffer_group_ptr = g_uniform_group_manager->get(renderer.gbuffer);
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
            update_binding_t{ TEXTURE, final_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            update_binding_t{ TEXTURE, position_tx, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            update_binding_t{ TEXTURE, normal_tx, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            update_binding_t{ TEXTURE, sun_tx, 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }

    renderer.subpass_input = g_uniform_group_manager->add("descriptor_set.deferred_descriptor_sets"_hash);
    auto *gbuffer_input_group_ptr = g_uniform_group_manager->get(renderer.subpass_input);
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
            update_binding_t{ INPUT_ATTACHMENT, albedo_tx, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            update_binding_t{ INPUT_ATTACHMENT, position_tx, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            update_binding_t{ INPUT_ATTACHMENT, normal_tx, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            update_binding_t{ INPUT_ATTACHMENT, sun_tx, 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }

    renderer.lighting = g_pipeline_manager->add("pipeline.deferred_pipeline"_hash);
    auto *deferred_ppln = g_pipeline_manager->get(renderer.lighting);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/deferred_lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/deferred_lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("descriptor_set_layout.deferred_layout"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash),
                                         g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k{ 160, 0, VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blend_states(blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic_states(VK_DYNAMIC_STATE_VIEWPORT);
        auto *dfr_render_pass = g_render_pass_manager->get(backbuffer.render_pass);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE, layouts, push_k, backbuffer.resolution, blend_states, nullptr,
            false, 0.0f, dynamic_states, dfr_render_pass, 1, info);
        deferred_ppln->info = info;
        make_graphics_pipeline(deferred_ppln);
    }
}


void initialize_deferred_renderer()
{
    s_backbuffer_init();
    s_renderer_init();
}


void begin_deferred_rendering(gpu_command_queue_t *queue)
{
    queue->begin_render_pass(g_render_pass_manager->get(backbuffer.render_pass),
        g_framebuffer_manager->get(backbuffer.framebuffer),
        VK_SUBPASS_CONTENTS_INLINE,
        // Clear values here
        init_clear_color_color(0.0f, 0.4f, 0.7f, 0.0f),
        init_clear_color_color(0.0f, 0.4f, 0.7f, 0.0f),
        init_clear_color_color(0.0f, 0.4f, 0.7f, 0.0f),
        init_clear_color_color(0.0f, 0.4f, 0.7f, 0.0f),
        init_clear_color_color(0.0f, 0.0f, 0.0f, 1.0f),
        init_clear_color_depth(1.0f, 0));

    command_buffer_set_viewport(backbuffer.resolution.width, backbuffer.resolution.height, 0.0f, 1.0f, &queue->q);
    command_buffer_set_line_width(2.0f, &queue->q);
}


void do_lighting_and_begin_alpha_rendering(sun_t *sun, const matrix4_t &view_matrix, gpu_command_queue_t *queue)
{
    queue->next_subpass(VK_SUBPASS_CONTENTS_INLINE);

    auto *dfr_lighting_ppln = g_pipeline_manager->get(renderer.lighting);

    command_buffer_bind_pipeline(&dfr_lighting_ppln->pipeline, &queue->q);

    auto *dfr_subpass_group = g_uniform_group_manager->get(renderer.subpass_input);
    auto *irradiance_cubemap = atmosphere_irradiance_uniform();
    auto *prefiltered_env_group = atmosphere_prefiltered_uniform();
    auto *integrate_lookup_group = atmosphere_integrate_lookup_uniform();
    auto transforms = camera_transforms_uniform();

    VkDescriptorSet deferred_sets[] = { *dfr_subpass_group, *irradiance_cubemap, *prefiltered_env_group, *integrate_lookup_group, get_shadow_display().texture, transforms };

    command_buffer_bind_descriptor_sets(&dfr_lighting_ppln->layout, { 6, deferred_sets }, &queue->q);

    struct deferred_lighting_push_k_t
    {
        vector4_t light_position;
        matrix4_t view_matrix;
        matrix4_t inverse_view_matrix;
        vector4_t ws_view_direction;
    } deferred_push_k;

    deferred_push_k.light_position = vector4_t(glm::normalize(-sun->ws_position), 1.0f);
    deferred_push_k.view_matrix = view_matrix;
    deferred_push_k.inverse_view_matrix = glm::inverse(view_matrix);

    camera_t *camera = camera_bound_to_3d_output();

    deferred_push_k.ws_view_direction = vector4_t(camera->d, 1.0f);

    command_buffer_push_constant(&deferred_push_k, sizeof(deferred_push_k), 0, VK_SHADER_STAGE_FRAGMENT_BIT, dfr_lighting_ppln->layout, &queue->q);
    command_buffer_draw(&queue->q, 4, 1, 0, 0);

    queue->next_subpass(VK_SUBPASS_CONTENTS_INLINE);
    // Now start alpha blending scene (particles, etc...)
}


void end_deferred_rendering(gpu_command_queue_t *queue)
{
    queue->end_render_pass();
}


resolution_t backbuffer_resolution()
{
    return backbuffer.resolution;
}


render_pass_t *deferred_render_pass()
{
    return g_render_pass_manager->get(backbuffer.render_pass);
}


uniform_group_t *gbuffer_uniform()
{
    return g_uniform_group_manager->get(renderer.gbuffer);
}
