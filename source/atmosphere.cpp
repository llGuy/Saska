#include "graphics.hpp"
#include "atmosphere.hpp"

static constexpr uint32_t CUBEMAP_W = 1000, CUBEMAP_H = 1000;
static constexpr uint32_t IRRADIANCE_CUBEMAP_W = 32, IRRADIANCE_CUBEMAP_H = 32;
static constexpr uint32_t PREFILTERED_ENVIRONMENT_CUBEMAP_W = 128, PREFILTERED_ENVIRONMENT_CUBEMAP_H = 128;

struct atmosphere_building_t 
{
    render_pass_handle_t make_render_pass;
    framebuffer_handle_t make_fbo;
    pipeline_handle_t make_pipeline;
    image_handle_t cubemap_handle;
    uniform_layout_handle_t render_atmosphere_layout;
};

static atmosphere_building_t builder;

// Initializes all objects contributing to building the atmosphere like the shaders/FBOs/render passes...
static void s_atmosphere_builder_init()
{
    builder.make_render_pass = g_render_pass_manager->add("render_pass.atmosphere_render_pass"_hash);
    auto *atmosphere_render_pass = g_render_pass_manager->get(builder.make_render_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t cubemap_attachment{ VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
            0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);

        make_render_pass(atmosphere_render_pass, { 1, &cubemap_attachment }, { 1, &subpass }, { 2, dependencies });
    }


    builder.make_fbo = g_framebuffer_manager->add("framebuffer.atmosphere_fbo"_hash);
    auto *atmosphere_fbo = g_framebuffer_manager->get(builder.make_fbo);
    {
        image_handle_t atmosphere_cubemap_handle = g_image_manager->add("image2D.atmosphere_cubemap"_hash);
        auto *cubemap = g_image_manager->get(atmosphere_cubemap_handle);
        make_framebuffer_attachment(cubemap, CUBEMAP_W, CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3);

        make_framebuffer(atmosphere_fbo, CUBEMAP_W, CUBEMAP_H, 6, atmosphere_render_pass, { 1, cubemap }, nullptr);
        builder.cubemap_handle = atmosphere_cubemap_handle;
    }

    builder.render_atmosphere_layout = g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash);

    builder.make_pipeline = g_pipeline_manager->add("pipeline.atmosphere_pipeline"_hash);
    auto *make_ppln = g_pipeline_manager->get(builder.make_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        VkExtent2D atmosphere_extent{ CUBEMAP_W, CUBEMAP_H };
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/atmosphere.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT },
            shader_module_info_t{ "shaders/SPV/atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts = {};
        shader_pk_data_t push_k = { 200, 0, VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blending{ blend_type_t::NO_BLENDING };
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
            false, 0.0f, dynamic, atmosphere_render_pass, 0, info);
        make_ppln->info = info;
        make_graphics_pipeline(make_ppln);
    }
}


struct atmosphere_lighting_t
{
    // Irradiance
    render_pass_handle_t generate_irradiance_pass;
    framebuffer_handle_t generate_irradiance_fbo;
    pipeline_handle_t generate_irradiance_pipeline;

    // Prefiltered environment
    render_pass_handle_t generate_prefiltered_environment_pass;
    framebuffer_handle_t generate_prefiltered_environment_fbo;

    pipeline_handle_t generate_prefiltered_environment_pipeline;

    image_handle_t prefiltered_environment_handle;
    // This is the image that gets attached to the framebuffer
    image_handle_t prefiltered_environment_interm_handle;
    image_handle_t integrate_lookup;

    render_pass_handle_t integrate_lookup_pass;
    framebuffer_handle_t integrate_lookup_fbo;
    pipeline_handle_t integrate_pipeline_handle;
    uniform_group_handle_t integrate_lookup_uniform_group;

    uniform_group_handle_t atmosphere_irradiance_uniform_group;
    uniform_group_handle_t atmosphere_prefiltered_environment_uniform_group;
};

static atmosphere_lighting_t lighting;


// Initializes all objects that contribute to allowing for better PBR lighting (IBL, prefiltered maps, ...)
static void s_atmosphere_lighting_init()
{
    lighting.generate_irradiance_pass = g_render_pass_manager->add("render_pass.irradiance_render_pass"_hash);
    auto *irradiance_render_pass = g_render_pass_manager->get(lighting.generate_irradiance_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t cubemap_attachment{ VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
            0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);

        make_render_pass(irradiance_render_pass, { 1, &cubemap_attachment }, { 1, &subpass }, { 2, dependencies });
    }


    lighting.generate_irradiance_fbo = g_framebuffer_manager->add("framebuffer.irradiance_fbo"_hash);
    auto *irradiance_fbo = g_framebuffer_manager->get(lighting.generate_irradiance_fbo);
    {
        image_handle_t irradiance_cubemap_handle = g_image_manager->add("image2D.irradiance_cubemap"_hash);
        auto *cubemap = g_image_manager->get(irradiance_cubemap_handle);
        make_framebuffer_attachment(cubemap, IRRADIANCE_CUBEMAP_W, IRRADIANCE_CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3);

        make_framebuffer(irradiance_fbo, IRRADIANCE_CUBEMAP_W, IRRADIANCE_CUBEMAP_H, 6, irradiance_render_pass, { 1, cubemap }, nullptr);
    }


    lighting.generate_prefiltered_environment_pass = g_render_pass_manager->add("render_pass.prefiltered_environment_render_pass"_hash);
    auto *prefiltered_environment_render_pass = g_render_pass_manager->get(lighting.generate_prefiltered_environment_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t cubemap_attachment{ VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
            0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);

        make_render_pass(prefiltered_environment_render_pass, { 1, &cubemap_attachment }, { 1, &subpass }, { 2, dependencies });
    }


    lighting.generate_prefiltered_environment_fbo = g_framebuffer_manager->add("framebuffer.prefiltered_environment_fbo"_hash);
    auto *prefiltered_environment_fbo = g_framebuffer_manager->get(lighting.generate_prefiltered_environment_fbo);
    {
        lighting.prefiltered_environment_interm_handle = g_image_manager->add("image2D.prefiltered_environment_interm"_hash);
        auto *interm = g_image_manager->get(lighting.prefiltered_environment_interm_handle);
        make_framebuffer_attachment(interm, PREFILTERED_ENVIRONMENT_CUBEMAP_W, PREFILTERED_ENVIRONMENT_CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 2);

        make_framebuffer(prefiltered_environment_fbo, PREFILTERED_ENVIRONMENT_CUBEMAP_W, PREFILTERED_ENVIRONMENT_CUBEMAP_H, 1, prefiltered_environment_render_pass, { 1, interm }, nullptr);
    }


    lighting.integrate_lookup_pass = g_render_pass_manager->add("render_pass.integrate_lookup_render_pass"_hash);
    auto *integrate_lookup_pass = g_render_pass_manager->get(lighting.integrate_lookup_pass);
    // ---- Make render pass ----
    {
        // ---- Set render pass attachment data ----
        render_pass_attachment_t texture_attachment{ VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // ---- Set render pass subpass data ----
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        // ---- Set render pass dependencies data ----
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT,
            0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);

        make_render_pass(integrate_lookup_pass, { 1, &texture_attachment }, { 1, &subpass }, { 2, dependencies });
    }


    lighting.integrate_lookup_fbo = g_framebuffer_manager->add("framebuffer.integrate_lookup_fbo"_hash);
    auto *integrate_lookup_fbo = g_framebuffer_manager->get(lighting.integrate_lookup_fbo);
    {
        lighting.integrate_lookup = g_image_manager->add("image2D.integrate_lookup"_hash);
        auto *lookup = g_image_manager->get(lighting.integrate_lookup);
        make_framebuffer_attachment(lookup, 512, 512, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 2);

        make_framebuffer(integrate_lookup_fbo, 512, 512, 1, integrate_lookup_pass, { 1, lookup }, nullptr);
    }

    auto *render_atmosphere_layout_ptr = g_uniform_layout_manager->get(builder.render_atmosphere_layout);

    lighting.integrate_lookup_uniform_group = g_uniform_group_manager->add("descriptor_set.integrate_lookup"_hash);
    auto *integrate_lookup_group = g_uniform_group_manager->get(lighting.integrate_lookup_uniform_group);
    {
        image_handle_t integrate_lookup_hdl = g_image_manager->get_handle("image2D.integrate_lookup"_hash);
        auto *integrate_lookup_ptr = g_image_manager->get(integrate_lookup_hdl);

        *integrate_lookup_group = make_uniform_group(render_atmosphere_layout_ptr, g_uniform_pool);
        update_uniform_group(integrate_lookup_group, update_binding_t{ TEXTURE, integrate_lookup_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }


    // Initialize pre-filtered environment cubemap
    lighting.prefiltered_environment_handle = g_image_manager->add("image2D.prefiltered_environment_cubemap"_hash);
    {
        auto *cubemap = g_image_manager->get(lighting.prefiltered_environment_handle);
        make_framebuffer_attachment(cubemap, PREFILTERED_ENVIRONMENT_CUBEMAP_W, PREFILTERED_ENVIRONMENT_CUBEMAP_H, VK_FORMAT_R8G8B8A8_UNORM, 6, 5, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 3);
        transition_image_layout(&cubemap->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, get_global_command_pool(), 6, 5);
    }

    lighting.atmosphere_irradiance_uniform_group = g_uniform_group_manager->add("descriptor_set.irradiance_cubemap"_hash);
    auto *irradiance_group_ptr = g_uniform_group_manager->get(lighting.atmosphere_irradiance_uniform_group);
    {
        image_handle_t cubemap_hdl = g_image_manager->get_handle("image2D.irradiance_cubemap"_hash);
        auto *cubemap_ptr = g_image_manager->get(cubemap_hdl);

        *irradiance_group_ptr = make_uniform_group(render_atmosphere_layout_ptr, g_uniform_pool);
        update_uniform_group(irradiance_group_ptr, update_binding_t{ TEXTURE, cubemap_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }


    lighting.atmosphere_prefiltered_environment_uniform_group = g_uniform_group_manager->add("descriptor_set.prefiltered_environment_cubemap"_hash);
    auto *prefiltered_environment_group_ptr = g_uniform_group_manager->get(lighting.atmosphere_prefiltered_environment_uniform_group);
    {
        image_handle_t cubemap_hdl = g_image_manager->get_handle("image2D.prefiltered_environment_cubemap"_hash);
        auto *cubemap_ptr = g_image_manager->get(cubemap_hdl);

        *prefiltered_environment_group_ptr = make_uniform_group(render_atmosphere_layout_ptr, g_uniform_pool);
        update_uniform_group(prefiltered_environment_group_ptr, update_binding_t{ TEXTURE, cubemap_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }

    lighting.integrate_pipeline_handle = g_pipeline_manager->add("pipeline.integrate_lookup_pipeline"_hash);
    auto *integrate_ppln = g_pipeline_manager->get(lighting.integrate_pipeline_handle);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        VkExtent2D atmosphere_extent{ 512, 512 };
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/integrate_lookup.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/integrate_lookup.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts = {};
        shader_pk_data_t push_k = { 200, 0, VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blending{ blend_type_t::NO_BLENDING };
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
            false, 0.0f, dynamic, integrate_lookup_pass, 0, info);
        integrate_ppln->info = info;
        make_graphics_pipeline(integrate_ppln);
    }


    lighting.generate_irradiance_pipeline = g_pipeline_manager->add("pipeline.irradiance_pipeline"_hash);
    auto *irradiance_ppln = g_pipeline_manager->get(lighting.generate_irradiance_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        VkExtent2D atmosphere_extent{ IRRADIANCE_CUBEMAP_W, IRRADIANCE_CUBEMAP_H };
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/cubemap_irradiance_generator.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/cubemap_irradiance_generator.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT },
            shader_module_info_t{ "shaders/SPV/cubemap_irradiance_generator.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts = { builder.render_atmosphere_layout };
        shader_pk_data_t push_k = { 200, 0, VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blending{ blend_type_t::NO_BLENDING };
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
            false, 0.0f, dynamic, irradiance_render_pass, 0, info);
        irradiance_ppln->info = info;
        make_graphics_pipeline(irradiance_ppln);
    }


    lighting.generate_prefiltered_environment_pipeline = g_pipeline_manager->add("pipeline.prefiltered_environment_pipeline"_hash);
    auto *prefiltered_environment_ppln = g_pipeline_manager->get(lighting.generate_prefiltered_environment_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        VkExtent2D atmosphere_extent{ PREFILTERED_ENVIRONMENT_CUBEMAP_W, PREFILTERED_ENVIRONMENT_CUBEMAP_H };
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/cubemap_prefiltered_generator.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/cubemap_prefiltered_generator.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts = { builder.render_atmosphere_layout };
        shader_pk_data_t push_k = { 200, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT };
        shader_blend_states_t blending{ blend_type_t::NO_BLENDING };
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE, layouts, push_k, atmosphere_extent, blending, nullptr,
            false, 0.0f, dynamic, prefiltered_environment_render_pass, 0, info);
        prefiltered_environment_ppln->info = info;
        make_graphics_pipeline(prefiltered_environment_ppln);
    }
}

struct atmosphere_rendering_t
{
    pipeline_handle_t render_pipeline;
    uniform_group_handle_t cubemap_uniform_group;
    model_handle_t cube_handle;
};

static atmosphere_rendering_t presenter;


// Initializes all objects that contribute to rendering the atmosphere to the screen
static void s_atmosphere_presenter_init()
{
    auto *render_atmosphere_layout_ptr = g_uniform_layout_manager->get(builder.render_atmosphere_layout);

    presenter.cubemap_uniform_group = g_uniform_group_manager->add("descriptor_set.cubemap"_hash);
    auto *cubemap_group_ptr = g_uniform_group_manager->get(presenter.cubemap_uniform_group);
    {
        image_handle_t cubemap_hdl = g_image_manager->get_handle("image2D.atmosphere_cubemap"_hash);
        auto *cubemap_ptr = g_image_manager->get(cubemap_hdl);

        *cubemap_group_ptr = make_uniform_group(render_atmosphere_layout_ptr, g_uniform_pool);
        update_uniform_group(cubemap_group_ptr, update_binding_t{ TEXTURE, cubemap_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }

    presenter.render_pipeline = g_pipeline_manager->add("pipeline.render_atmosphere"_hash);
    auto *render_ppln = g_pipeline_manager->get(presenter.render_pipeline);
    {
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        resolution_t backbuffer_res = get_backbuffer_resolution();
        model_handle_t cube_hdl = g_model_manager->get_handle("model.cube_model"_hash);
        auto *model_ptr = g_model_manager->get(cube_hdl);
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/render_atmosphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/render_atmosphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        uniform_layout_handle_t camera_transforms_layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        shader_uniform_layouts_t layouts(camera_transforms_layout_hdl, builder.render_atmosphere_layout);
        shader_pk_data_t push_k = { 160, 0, VK_SHADER_STAGE_VERTEX_BIT };
        shader_blend_states_t blending(blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, model_ptr,
            true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        render_ppln->info = info;
        make_graphics_pipeline(render_ppln);
    }

    presenter.cube_handle = g_model_manager->get_handle("model.cube_model"_hash);
}


static void s_update_atmosphere_main(sun_t *light_pos, uint32_t light_count, gpu_command_queue_t *queue)
{
    queue->begin_render_pass(builder.make_render_pass, builder.make_fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0, 0.0, 0.0, 0));

    VkViewport viewport;
    init_viewport(0, 0, 1000, 1000, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);

    auto *make_ppln = g_pipeline_manager->get(builder.make_pipeline);

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

    k.light_dir[0] = vector4_t(glm::normalize(-light_pos[0].ws_position), 1.0f);
    k.light_dir[1] = vector4_t(glm::normalize(-light_pos[1].ws_position), 1.0f);

    k.light_color[0] = vector4_t(vector3_t(light_pos[0].color), 1.0f);
    k.light_color[1] = vector4_t(vector3_t(light_pos[1].color), 1.0f);

    command_buffer_push_constant(&k, sizeof(k), 0, VK_SHADER_STAGE_FRAGMENT_BIT, make_ppln->layout, &queue->q);
    command_buffer_draw(&queue->q, 1, 1, 0, 0);

    queue->end_render_pass();
}


static void s_update_atmosphere_irradiance(gpu_command_queue_t *queue)
{
    queue->begin_render_pass(lighting.generate_irradiance_pass, lighting.generate_irradiance_fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0, 0.0, 0.0, 0));

    VkViewport viewport;
    init_viewport(0, 0, IRRADIANCE_CUBEMAP_W, IRRADIANCE_CUBEMAP_H, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);

    auto *irradiance_ppln = g_pipeline_manager->get(lighting.generate_irradiance_pipeline);

    command_buffer_bind_pipeline(&irradiance_ppln->pipeline, &queue->q);

    uniform_group_t *atmosphere_uniform = g_uniform_group_manager->get(presenter.cubemap_uniform_group);
    command_buffer_bind_descriptor_sets(&irradiance_ppln->layout, { 1, atmosphere_uniform }, &queue->q);

    command_buffer_draw(&queue->q, 1, 1, 0, 0);

    queue->end_render_pass();
}


void s_update_atmosphere_prefiltered(gpu_command_queue_t *queue)
{
    auto *prefiltered_environment_ppln = g_pipeline_manager->get(lighting.generate_prefiltered_environment_pipeline);

    matrix4_t projection_matrix = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f);

    matrix4_t view_matrices[6] = {
                                  glm::rotate(glm::rotate(matrix4_t(1.0f), glm::radians(90.0f), vector3_t(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), vector3_t(1.0f, 0.0f, 0.0f)),
                                  glm::rotate(glm::rotate(matrix4_t(1.0f), glm::radians(-90.0f), vector3_t(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), vector3_t(1.0f, 0.0f, 0.0f)),
                                  glm::rotate(matrix4_t(1.0f), glm::radians(-90.0f), vector3_t(1.0f, 0.0f, 0.0f)),
                                  glm::rotate(matrix4_t(1.0f), glm::radians(90.0f), vector3_t(1.0f, 0.0f, 0.0f)),
                                  glm::rotate(matrix4_t(1.0f), glm::radians(180.0f), vector3_t(1.0f, 0.0f, 0.0f)),
                                  glm::rotate(matrix4_t(1.0f), glm::radians(180.0f), vector3_t(0.0f, 0.0f, 1.0f)),
    };

    image2d_t *cubemap = g_image_manager->get(lighting.prefiltered_environment_handle);
    image2d_t *interm = g_image_manager->get(lighting.prefiltered_environment_interm_handle);

    for (uint32_t mip = 0; mip < 5; ++mip)
    {
        uint32_t width = PREFILTERED_ENVIRONMENT_CUBEMAP_W * (uint32_t)pow(0.5, mip);
        uint32_t height = PREFILTERED_ENVIRONMENT_CUBEMAP_H * (uint32_t)pow(0.5, mip);

        float32_t roughness = (float32_t)mip / (float32_t)(5 - 1);

        for (uint32_t layer = 0; layer < 6; ++layer)
        {
            queue->begin_render_pass(lighting.generate_prefiltered_environment_pass, lighting.generate_prefiltered_environment_fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0, 0.0, 0.0, 0));

            // Set viewport
            VkViewport viewport;
            init_viewport(0, 0, width, height, 0.0f, 1.0f, &viewport);
            vkCmdSetViewport(queue->q, 0, 1, &viewport);

            command_buffer_bind_pipeline(&prefiltered_environment_ppln->pipeline, &queue->q);

            uniform_group_t *atmosphere_uniform = g_uniform_group_manager->get(presenter.cubemap_uniform_group);
            command_buffer_bind_descriptor_sets(&prefiltered_environment_ppln->layout, { 1, atmosphere_uniform }, &queue->q);

            // Push constant
            struct push_constant_t
            {
                matrix4_t mvp;
                float32_t roughness;
                float32_t layer;
            } pk;

            pk.roughness = roughness;
            pk.mvp = projection_matrix * view_matrices[layer];
            pk.layer = (float32_t)layer;

            command_buffer_push_constant(&pk, sizeof(pk), 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, prefiltered_environment_ppln->layout, &queue->q);

            command_buffer_draw(&queue->q, 36, 1, 0, 0);

            queue->end_render_pass();

            // Copy into correct cubemap mip and layer
            copy_image(interm,
                cubemap,
                width,
                height,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                &queue->q,
                layer,
                mip);
        }
    }
}


static void s_update_atmosphere_integrate_lookup(gpu_command_queue_t *queue)
{
    auto *integrate_lookup_ppln = g_pipeline_manager->get(lighting.integrate_pipeline_handle);

    queue->begin_render_pass(lighting.integrate_lookup_pass, lighting.integrate_lookup_fbo, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_color(0, 0.0, 0.0, 0));

    // Set viewport
    VkViewport viewport;
    init_viewport(0, 0, 512, 512, 0.0f, 1.0f, &viewport);
    vkCmdSetViewport(queue->q, 0, 1, &viewport);

    command_buffer_bind_pipeline(&integrate_lookup_ppln->pipeline, &queue->q);

    command_buffer_draw(&queue->q, 4, 1, 0, 0);

    queue->end_render_pass();
}


static void s_update_atmosphere(sun_t *light_pos, uint32_t light_count)
{
    VkCommandBuffer cmdbuf;
    init_single_use_command_buffer(get_global_command_pool(), &cmdbuf);

    gpu_command_queue_t queue{ cmdbuf };
    update_atmosphere(light_pos, light_count, &queue);

    destroy_single_use_command_buffer(&cmdbuf, get_global_command_pool());
}


void initialize_atmosphere(sun_t *light_pos, uint32_t light_count)
{
    s_atmosphere_builder_init();
    s_atmosphere_lighting_init();
    s_atmosphere_presenter_init();
    s_update_atmosphere(light_pos, light_count);
}


void update_atmosphere(sun_t *light_pos, uint32_t light_count, gpu_command_queue_t *queue)
{
    s_update_atmosphere_main(light_pos, light_count, queue);
    s_update_atmosphere_irradiance(queue);
    s_update_atmosphere_prefiltered(queue);
    s_update_atmosphere_integrate_lookup(queue);
}


void render_atmosphere(uniform_group_t *camera_transforms, const vector3_t &camera_pos, gpu_command_queue_t *queue)
{
    auto *render_pipeline = g_pipeline_manager->get(presenter.render_pipeline);
    command_buffer_bind_pipeline(&render_pipeline->pipeline, &queue->q);

    uniform_group_t groups[2] = { *camera_transforms, *g_uniform_group_manager->get(presenter.cubemap_uniform_group) };
    command_buffer_bind_descriptor_sets(&render_pipeline->layout, { 2, groups }, &queue->q);

    model_t *cube = g_model_manager->get(presenter.cube_handle);

    VkDeviceSize zero = 0;
    command_buffer_bind_vbos(cube->raw_cache_for_rendering, { 1, &zero }, 0, 1, &queue->q);

    command_buffer_bind_ibo(cube->index_data, &queue->q);

    struct skybox_push_constant_t
    {
        matrix4_t model_matrix;
    } push_k;

    push_k.model_matrix = glm::scale(vector3_t(100000.0f));

    command_buffer_push_constant(&push_k, sizeof(push_k), 0, VK_SHADER_STAGE_VERTEX_BIT, render_pipeline->layout, &queue->q);

    command_buffer_draw_indexed(&queue->q, cube->index_data.init_draw_indexed_data(0, 0));
}


uniform_group_t *atmosphere_diffuse_uniform()
{
    return g_uniform_group_manager->get(presenter.cubemap_uniform_group);
}


uniform_group_t *atmosphere_irradiance_uniform()
{
    return g_uniform_group_manager->get(lighting.atmosphere_irradiance_uniform_group);
}


uniform_group_t *atmosphere_prefiltered_uniform()
{
    return g_uniform_group_manager->get(lighting.atmosphere_prefiltered_environment_uniform_group);
}


uniform_group_t *atmosphere_integrate_lookup_uniform()
{
    return g_uniform_group_manager->get(lighting.integrate_lookup_uniform_group);
}