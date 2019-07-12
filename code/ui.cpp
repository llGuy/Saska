#include "utils.hpp"
#include "graphics.hpp"

enum coordinate_type_t { PIXEL, GLSL };

struct ui_vector2_t
{
    union
    {
        struct {int32_t ix, iy;};
        struct {float32_t fx, fy;};
    };
    coordinate_type_t type;

    ui_vector2_t(void) = default;
    ui_vector2_t(float32_t x, float32_t y) : fx(x), fy(y), type(GLSL) {}
    ui_vector2_t(int32_t x, int32_t y) : ix(x), iy(y), type(PIXEL) {}

    inline vector2_t
    to_fvec2(void) const
    {
        return vector2_t(fx, fy);
    }

    inline ivector2_t
    to_ivec2(void) const
    {
        return ivector2_t(ix, iy);
    }
};

internal inline vector2_t
convert_glsl_to_normalized(const vector2_t &position)
{
    return(position * 2.0f - 1.0f);
}

internal inline ui_vector2_t
glsl_to_pixel_coord(const ui_vector2_t &position,
                    const resolution_t &resolution)
{
    ui_vector2_t ret((int32_t)(position.fx * (float32_t)resolution.width), (int32_t)(position.fy * (float32_t)resolution.height));
    return(ret);
}

internal inline ui_vector2_t
pixel_to_glsl_coord(const ui_vector2_t &position,
                    const resolution_t &resolution)
{
    ui_vector2_t ret((float32_t)position.ix / (float32_t)resolution.width
                   , (float32_t)position.iy / (float32_t)resolution.height);
    return(ret);
}

// for_t now, doesn't support the relative to function (need to add in the fufure)
enum relative_to_t { LEFT_DOWN, LEFT_UP, CENTER, RIGHT_DOWN, RIGHT_UP };
global_var constexpr vector2_t RELATIVE_TO_ADD_VALUES[] { vector2_t(0.0f, 0.0f),
        vector2_t(0.0f, 1.0f),
        vector2_t(0.5f, 0.5f),
        vector2_t(1.0f, 0.0f),
        vector2_t(1.0f, 1.0f)};
global_var constexpr vector2_t RELATIVE_TO_FACTORS[] { vector2_t(0.0f, 0.0f),
        vector2_t(0.0f, -1.0f),
        vector2_t(-0.5f, -0.5f),
        vector2_t(-1.0f, 0.0f),
        vector2_t(-1.0f, -1.0f)};

struct ui_box_t
{
    ui_box_t *parent {nullptr};
    relative_to_t relative_to;
    ui_vector2_t relative_position;
    ui_vector2_t gls_position;
    ui_vector2_t px_position;
    ui_vector2_t gls_max_values;
    ui_vector2_t px_current_size;
    ui_vector2_t gls_current_size;
    ui_vector2_t gls_relative_size;
    float32_t aspect_ratio;
    uint32_t color;
};

internal void
update_ui_box_size(ui_box_t *box, const resolution_t &backbuffer_resolution)
{
    ui_vector2_t px_max_values;
    if (box->parent)
    {
        // relative_t to the parent aspect ratio
        px_max_values = glsl_to_pixel_coord(box->gls_max_values,
                                            resolution_t{(uint32_t)box->parent->px_current_size.ix, (uint32_t)box->parent->px_current_size.iy});
    }
    else
    {
        px_max_values = glsl_to_pixel_coord(box->gls_max_values, backbuffer_resolution);
    }
    ui_vector2_t px_max_xvalue_coord(px_max_values.ix, (int32_t)((float32_t)px_max_values.ix / box->aspect_ratio));
    // Check if, using glsl max x value, the y still fits in the given glsl max y value
    if (px_max_xvalue_coord.iy <= px_max_values.iy)
    {
        // Then use this new size;
        box->px_current_size = px_max_xvalue_coord;
    }
    else
    {
        // Then use y max value, and modify the x dependending on the new y
        ui_vector2_t px_max_yvalue_coord((uint32_t)((float32_t)px_max_values.iy * box->aspect_ratio), px_max_values.iy);
        box->px_current_size = px_max_yvalue_coord;
    }
    if (box->parent)
    {
        box->gls_relative_size = ui_vector2_t((float32_t)box->px_current_size.ix / (float32_t)box->parent->px_current_size.ix,
                                       (float32_t)box->px_current_size.iy / (float32_t)box->parent->px_current_size.iy);
        box->gls_current_size = ui_vector2_t((float32_t)box->px_current_size.ix / (float32_t)backbuffer_resolution.width,
                                       (float32_t)box->px_current_size.iy / (float32_t)backbuffer_resolution.height);
    }
    else
    {
        box->gls_current_size = pixel_to_glsl_coord(box->px_current_size, backbuffer_resolution);
        box->gls_relative_size = pixel_to_glsl_coord(box->px_current_size, backbuffer_resolution);
    }
}

internal void
update_ui_box_position(ui_box_t *box, const resolution_t &backbuffer_resolution)
{
    vector2_t gls_size = box->gls_relative_size.to_fvec2();
    vector2_t gls_relative_position;
    if (box->relative_position.type == GLSL)
    {
        gls_relative_position = box->relative_position.to_fvec2();
    }
    if (box->relative_position.type == PIXEL)
    {
        gls_relative_position = pixel_to_glsl_coord(box->relative_position,
                                                    resolution_t{(uint32_t)box->parent->px_current_size.ix, (uint32_t)box->parent->px_current_size.iy}).to_fvec2();
    }
    gls_relative_position += RELATIVE_TO_ADD_VALUES[box->relative_to];
    gls_relative_position += RELATIVE_TO_FACTORS[box->relative_to] * gls_size;
    if (box->parent)
    {
        ui_vector2_t px_size = glsl_to_pixel_coord(ui_vector2_t(gls_size.x, gls_size.y),
                                            resolution_t{(uint32_t)box->parent->px_current_size.ix, (uint32_t)box->parent->px_current_size.iy});
        
        ui_vector2_t px_relative_position = glsl_to_pixel_coord(ui_vector2_t(gls_relative_position.x, gls_relative_position.y),
                                                         resolution_t{(uint32_t)box->parent->px_current_size.ix, (uint32_t)box->parent->px_current_size.iy});
        ivector2_t px_real_position = box->parent->px_position.to_ivec2() + px_relative_position.to_ivec2();
        gls_relative_position = pixel_to_glsl_coord(ui_vector2_t(px_real_position.x, px_real_position.y), backbuffer_resolution).to_fvec2();
    }

    box->gls_position = ui_vector2_t(gls_relative_position.x, gls_relative_position.y);
    box->px_position = glsl_to_pixel_coord(box->gls_position, backbuffer_resolution);
}

internal ui_box_t
make_ui_box(relative_to_t relative_to, float32_t aspect_ratio,
            ui_vector2_t position /* coord_t space agnostic */,
            ui_vector2_t gls_max_values /* max_t X and Y size */,
            ui_box_t *parent,
            const uint32_t &color,
            resolution_t backbuffer_resolution = {})
{
    resolution_t dst_resolution = backbuffer_resolution;
    if (parent)
    {
        dst_resolution = resolution_t{ (uint32_t)parent->px_current_size.ix, (uint32_t)parent->px_current_size.iy };
    }
    
    ui_box_t box = {};
    box.relative_position = position;
    box.parent = parent;
    box.aspect_ratio = aspect_ratio;
    box.gls_max_values = gls_max_values;
    update_ui_box_size(&box, backbuffer_resolution);
    box.relative_to = relative_to;
    update_ui_box_position(&box, backbuffer_resolution);
    box.color = color;
    return(box);
}

struct ui_state_t
{
    struct gui_vertex_t
    {
        vector2_t position;
        uint32_t color;
    };
    persist constexpr uint32_t MAX_QUADS = 10;
    gui_vertex_t cpu_vertex_pool[ MAX_QUADS * 6 ];
    uint32_t cpu_vertex_count = 0;
    model_handle_t ui_quads_model;
    gpu_buffer_handle_t ui_quads_vbo;
    pipeline_handle_t ui_pipeline;

    struct textured_vertex_t
    {
        vector2_t position;
        vector2_t uvs;
    };
    persist constexpr uint32_t MAX_TX_QUADS = 100;
    textured_vertex_t cpu_tx_vertex_pool[ MAX_TX_QUADS * 6 ];
    uint32_t cpu_tx_vertex_count = 0;    
    model_handle_t tx_quads_model;
    gpu_buffer_handle_t tx_quads_vbo;
    pipeline_handle_t tx_pipeline;
    // may_t contain an array of textures
    uniform_group_handle_t tx_group;

    render_pass_handle_t ui_render_pass;
    gpu_command_queue_t secondary_ui_q;
    
    ui_box_t box;
    ui_box_t child;
    ui_box_t test_character_placeholder;
} g_ui;

internal void
push_box_to_render(ui_box_t *box)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position, box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + normalized_size, box->color};
}

internal void
push_font_character_to_render(ui_box_t *box)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui.cpu_tx_vertex_pool[g_ui.cpu_tx_vertex_count++] = {normalized_base_position, vector2_t(0.0f, 0.0f)};
    g_ui.cpu_tx_vertex_pool[g_ui.cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f, 1.0f)};
    g_ui.cpu_tx_vertex_pool[g_ui.cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f, 0.0f)};
    g_ui.cpu_tx_vertex_pool[g_ui.cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f, 1.0f)};
    g_ui.cpu_tx_vertex_pool[g_ui.cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f, 0.0f)};
    g_ui.cpu_tx_vertex_pool[g_ui.cpu_tx_vertex_count++] = {normalized_base_position + normalized_size, vector2_t(1.0f)};
}

internal void
initialize_ui_elements(gpu_t *gpu, const resolution_t &backbuffer_resolution)
{
    g_ui.box = make_ui_box(LEFT_DOWN, 0.5f,
                           ui_vector2_t(0.05f, 0.05f),
                           ui_vector2_t(1.0f, 0.9f),
                           nullptr,
                           0x16161636,
                           backbuffer_resolution);
    g_ui.child = make_ui_box(RIGHT_UP, 1.0f,
                             ui_vector2_t(0.0f, 0.0f),
                             ui_vector2_t(0.3f, 0.3f),
                             &g_ui.box,
                             0xaa000036,
                             backbuffer_resolution);
    g_ui.test_character_placeholder = make_ui_box(LEFT_DOWN, 1.0f,
                                                  ui_vector2_t(0.0f, 0.0f),
                                                  ui_vector2_t(0.3f, 0.3f),
                                                  &g_ui.box,
                                                  0xaa000036,
                                                  backbuffer_resolution);
}

void
initialize_ui_rendering_state(gpu_t *gpu,
                              VkFormat swapchain_format,
                              uniform_pool_t *uniform_pool,
                              const resolution_t &resolution,
                              gpu_command_queue_pool_t *queue_pool)
{
    g_ui.ui_quads_model = g_model_manager.add("model.ui_quads"_hash);
    auto *ui_quads_ptr = g_model_manager.get(g_ui.ui_quads_model);
    {
        ui_quads_ptr->attribute_count = 2;
	ui_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	ui_quads_ptr->binding_count = 1;
	ui_quads_ptr->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

	// only one binding
	model_binding_t *binding = ui_quads_ptr->bindings;
	binding->begin_attributes_creation(ui_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(ui_state_t::gui_vertex_t::position));
	binding->push_attribute(1, VK_FORMAT_R32_UINT, sizeof(ui_state_t::gui_vertex_t::color));

	binding->end_attributes_creation();
    }
    g_ui.tx_quads_model = g_model_manager.add("model.tx_quads"_hash);
    auto *tx_quads_ptr = g_model_manager.get(g_ui.tx_quads_model);
    {
        tx_quads_ptr->attribute_count = 2;
	tx_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	tx_quads_ptr->binding_count = 1;
	tx_quads_ptr->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

	// only one binding
	model_binding_t *binding = tx_quads_ptr->bindings;
	binding->begin_attributes_creation(tx_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(ui_state_t::textured_vertex_t::position));
	binding->push_attribute(1, VK_FORMAT_R32G32_SFLOAT, sizeof(ui_state_t::textured_vertex_t::uvs));

	binding->end_attributes_creation();
    }

    g_ui.ui_quads_vbo = g_gpu_buffer_manager.add("vbo.ui_quads"_hash);
    auto *vbo = g_gpu_buffer_manager.get(g_ui.ui_quads_vbo);
    {
        auto *main_binding = &ui_quads_ptr->bindings[0];
	
        init_buffer(ui_state_t::MAX_QUADS * 6 * sizeof(ui_state_t::gui_vertex_t),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    gpu,
                    vbo);

        main_binding->buffer = vbo->buffer;
        ui_quads_ptr->create_vbo_list();
    }
    g_ui.tx_quads_vbo = g_gpu_buffer_manager.add("vbo.tx_quads"_hash);
    auto *tx_vbo = g_gpu_buffer_manager.get(g_ui.tx_quads_vbo);
    {
        auto *main_binding = &tx_quads_ptr->bindings[0];
	
        init_buffer(ui_state_t::MAX_TX_QUADS * 6 * sizeof(ui_state_t::textured_vertex_t),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    gpu,
                    tx_vbo);

        main_binding->buffer = tx_vbo->buffer;
        tx_quads_ptr->create_vbo_list();
    }

    g_ui.ui_render_pass = g_render_pass_manager.add("render_pass.ui"_hash);
    auto *ui_render_pass = g_render_pass_manager.get(g_ui.ui_render_pass);
    {
        render_pass_attachment_t color_attachment = {swapchain_format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(ui_render_pass, {1, &color_attachment}, {1, &subpass}, {2, dependencies}, gpu, false /*don_t't clear*/);
    }

    g_ui.ui_pipeline = g_pipeline_manager.add("pipeline.uibox"_hash);
    auto *ui_pipeline = g_pipeline_manager.get(g_ui.ui_pipeline);
    {
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/uiquad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/uiquad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        // Later will be the UI texture uniform layout
        shader_uniform_layouts_t layouts = {};
        shader_pk_data_t pk = {};
        shader_blend_states_t blending(true);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(ui_pipeline,
                               modules,
                               false,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE,
                               layouts,
                               pk,
                               resolution,
                               blending,
                               ui_quads_ptr,
                               false,
                               0.0f,
                               dynamic,
                               ui_render_pass,
                               0,
                               gpu);
    }
    
    uniform_layout_handle_t tx_layout_hdl = g_uniform_layout_manager.add("uniform_layout.tx_ui_quad"_hash);
    auto *tx_layout_ptr = g_uniform_layout_manager.get(tx_layout_hdl);
    {
        uniform_layout_info_t layout_info;
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *tx_layout_ptr = make_uniform_layout(&layout_info, gpu);
    }
    image_handle_t tx_hdl = g_image_manager.add("image2D.fontmap"_hash);
    auto *tx_ptr = g_image_manager.get(tx_hdl);
    {
        make_texture(tx_ptr, 500, 500, VK_FORMAT_R8G8B8A8_UNORM, 1, 2, gpu);
        external_image_data_t image_data = read_image("font/consolas.png");
        invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                     queue_pool,
                                                     tx_ptr,
                                                     (uint32_t)image_data.width,
                                                     (uint32_t)image_data.height,
                                                     gpu);
    }
    g_ui.tx_group = g_uniform_group_manager.add("uniform_group.tx_ui_quad"_hash);
    auto *tx_group_ptr = g_uniform_group_manager.get(g_ui.tx_group);
    {
        *tx_group_ptr = make_uniform_group(tx_layout_ptr, uniform_pool, gpu);
        update_uniform_group(gpu, tx_group_ptr,
                             update_binding_t{ TEXTURE, tx_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
    
    g_ui.tx_pipeline = g_pipeline_manager.add("pipeline.txbox"_hash);
    auto *tx_pipeline = g_pipeline_manager.get(g_ui.tx_pipeline);
    {
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/uifontquad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/uifontquad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        // Later will be the UI texture uniform layout
        shader_uniform_layouts_t layouts(tx_layout_hdl);
        shader_pk_data_t pk = {};
        shader_blend_states_t blending(true);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(tx_pipeline,
                               modules,
                               false,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE,
                               layouts,
                               pk,
                               resolution,
                               blending,
                               tx_quads_ptr,
                               false,
                               0.0f,
                               dynamic,
                               ui_render_pass,
                               0,
                               gpu);
    }

    g_ui.secondary_ui_q.submit_level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
}

// will be rendered to backbuffer first
void
initialize_game_ui(gpu_t *gpu, gpu_command_queue_pool_t *qpool, swapchain_t *swapchain, uniform_pool_t *uniform_pool, const resolution_t &resolution)
{
    g_ui.secondary_ui_q = make_command_queue(qpool, VK_COMMAND_BUFFER_LEVEL_SECONDARY, gpu);

    initialize_ui_rendering_state(gpu, swapchain->format, uniform_pool, resolution, qpool);
    initialize_ui_elements(gpu, resolution);
}

void
update_game_ui(gpu_t *gpu, framebuffer_handle_t dst_framebuffer_hdl)
{
    // loop_t through all boxes
    push_box_to_render(&g_ui.box);
    push_box_to_render(&g_ui.child);
    push_font_character_to_render(&g_ui.test_character_placeholder);
    
    VkCommandBufferInheritanceInfo inheritance = make_queue_inheritance_info(g_render_pass_manager.get(g_ui.ui_render_pass),
                                                                             g_framebuffer_manager.get(get_pfx_framebuffer_hdl()));
    begin_command_queue(&g_ui.secondary_ui_q, gpu, &inheritance);
    {
        // may_t execute other stuff
        auto *dst_framebuffer = g_framebuffer_manager.get(dst_framebuffer_hdl);
        command_buffer_set_viewport(dst_framebuffer->extent.width, dst_framebuffer->extent.height, 0.0f, 1.0f, &g_ui.secondary_ui_q.q);
        
        auto *ui_pipeline = g_pipeline_manager.get(g_ui.ui_pipeline);
        command_buffer_bind_pipeline(ui_pipeline, &g_ui.secondary_ui_q.q);
        VkDeviceSize zero = 0;
        auto *quads_model = g_model_manager.get(g_ui.ui_quads_model);
        command_buffer_bind_vbos(quads_model->raw_cache_for_rendering,
                                 {1, &zero},
                                 0,
                                 quads_model->binding_count,
                                 &g_ui.secondary_ui_q.q);
        
        struct UI_PK
        {
            alignas(16) vector4_t color;
        } pk;
        pk.color = vector4_t(0.2f, 0.2f, 0.2f, 1.0f);

        command_buffer_draw(&g_ui.secondary_ui_q.q,
                            g_ui.cpu_vertex_count,
                            1,
                            0,
                            0);

        auto *font_pipeline = g_pipeline_manager.get(g_ui.tx_pipeline);
        command_buffer_bind_pipeline(font_pipeline, &g_ui.secondary_ui_q.q);

        auto *font_tx_group = g_uniform_group_manager.get(g_ui.tx_group);
        command_buffer_bind_descriptor_sets(font_pipeline, {1, font_tx_group}, &g_ui.secondary_ui_q.q);
        
        auto *tx_quads_model = g_model_manager.get(g_ui.tx_quads_model);
        command_buffer_bind_vbos(tx_quads_model->raw_cache_for_rendering,
                                 {1, &zero},
                                 0,
                                 tx_quads_model->binding_count,
                                 &g_ui.secondary_ui_q.q);
        command_buffer_draw(&g_ui.secondary_ui_q.q,
                            g_ui.cpu_tx_vertex_count,
                            1,
                            0,
                            0);
    }
    end_command_queue(&g_ui.secondary_ui_q, gpu);
}

void
render_game_ui(gpu_t *gpu, framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue)
{
    // for_t the moment, this just executes one command buffer
    auto *vbo = g_gpu_buffer_manager.get(g_ui.ui_quads_vbo);
    update_gpu_buffer(vbo,
                      g_ui.cpu_vertex_pool,
                      sizeof(ui_state_t::gui_vertex_t) * g_ui.cpu_vertex_count,
                      0,
                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                      &queue->q);
    auto *tx_vbo = g_gpu_buffer_manager.get(g_ui.tx_quads_vbo);
    update_gpu_buffer(tx_vbo,
                      g_ui.cpu_tx_vertex_pool,
                      sizeof(ui_state_t::textured_vertex_t) * g_ui.cpu_tx_vertex_count,
                      0,
                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                      &queue->q);
    queue->begin_render_pass(g_ui.ui_render_pass,
                                          dst_framebuffer_hdl,
                                          VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    command_buffer_execute_commands(&queue->q, {1, &g_ui.secondary_ui_q.q});
    queue->end_render_pass();

    g_ui.cpu_vertex_count = 0;
    g_ui.cpu_tx_vertex_count = 0;
}
