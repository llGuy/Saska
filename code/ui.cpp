#include "utils.hpp"
#include "graphics.hpp"

enum Coordinate_Type { PIXEL, GLSL };

struct UI_V2
{
    union
    {
        struct {s32 ix, iy;};
        struct {f32 fx, fy;};
    };
    Coordinate_Type type;

    UI_V2(void) = default;
    UI_V2(f32 x, f32 y) : fx(x), fy(y), type(GLSL) {}
    UI_V2(s32 x, s32 y) : ix(x), iy(y), type(PIXEL) {}

    inline v2
    to_fvec2(void) const
    {
        return v2(fx, fy);
    }

    inline iv2
    to_ivec2(void) const
    {
        return iv2(ix, iy);
    }
};

internal inline v2
convert_glsl_to_normalized(const v2 &position)
{
    return(position * 2.0f - 1.0f);
}

internal inline UI_V2
glsl_to_pixel_coord(const UI_V2 &position,
                    const Resolution &resolution)
{
    UI_V2 ret((s32)(position.fx * (f32)resolution.width), (s32)(position.fy * (f32)resolution.height));
    return(ret);
}

internal inline UI_V2
pixel_to_glsl_coord(const UI_V2 &position,
                    const Resolution &resolution)
{
    UI_V2 ret((f32)position.ix / (f32)resolution.width
                   , (f32)position.iy / (f32)resolution.height);
    return(ret);
}

// For now, doesn't support the relative to function (need to add in the fufure)
enum Relative_To { LEFT_DOWN, LEFT_UP, CENTER, RIGHT_DOWN, RIGHT_UP };
global_var constexpr v2 RELATIVE_TO_ADD_VALUES[] { v2(0.0f, 0.0f),
        v2(0.0f, 1.0f),
        v2(0.5f, 0.5f),
        v2(1.0f, 0.0f),
        v2(1.0f, 1.0f)};
global_var constexpr v2 RELATIVE_TO_FACTORS[] { v2(0.0f, 0.0f),
        v2(0.0f, -1.0f),
        v2(-0.5f, -0.5f),
        v2(-1.0f, 0.0f),
        v2(-1.0f, -1.0f)};

struct UI_Box
{
    UI_Box *parent {nullptr};
    Relative_To relative_to;
    UI_V2 relative_position;
    UI_V2 gls_position;
    UI_V2 px_position;
    UI_V2 gls_max_values;
    UI_V2 px_current_size;
    UI_V2 gls_current_size;
    UI_V2 gls_relative_size;
    f32 aspect_ratio;
    u32 color;
};

internal void
update_ui_box_size(UI_Box *box, const Resolution &backbuffer_resolution)
{
    UI_V2 px_max_values;
    if (box->parent)
    {
        // Relative to the parent aspect ratio
        px_max_values = glsl_to_pixel_coord(box->gls_max_values,
                                            Resolution{(u32)box->parent->px_current_size.ix, (u32)box->parent->px_current_size.iy});
    }
    else
    {
        px_max_values = glsl_to_pixel_coord(box->gls_max_values, backbuffer_resolution);
    }
    UI_V2 px_max_xvalue_coord(px_max_values.ix, (s32)((f32)px_max_values.ix / box->aspect_ratio));
    // Check if, using glsl max x value, the y still fits in the given glsl max y value
    if (px_max_xvalue_coord.iy <= px_max_values.iy)
    {
        // Then use this new size;
        box->px_current_size = px_max_xvalue_coord;
    }
    else
    {
        // Then use y max value, and modify the x dependending on the new y
        UI_V2 px_max_yvalue_coord((u32)((f32)px_max_values.iy * box->aspect_ratio), px_max_values.iy);
        box->px_current_size = px_max_yvalue_coord;
    }
    if (box->parent)
    {
        box->gls_relative_size = UI_V2((f32)box->px_current_size.ix / (f32)box->parent->px_current_size.ix,
                                       (f32)box->px_current_size.iy / (f32)box->parent->px_current_size.iy);
        box->gls_current_size = UI_V2((f32)box->px_current_size.ix / (f32)backbuffer_resolution.width,
                                       (f32)box->px_current_size.iy / (f32)backbuffer_resolution.height);
    }
    else
    {
        box->gls_current_size = pixel_to_glsl_coord(box->px_current_size, backbuffer_resolution);
        box->gls_relative_size = pixel_to_glsl_coord(box->px_current_size, backbuffer_resolution);
    }
}

internal void
update_ui_box_position(UI_Box *box, const Resolution &backbuffer_resolution)
{
    v2 gls_size = box->gls_relative_size.to_fvec2();
    v2 gls_relative_position;
    if (box->relative_position.type == GLSL)
    {
        gls_relative_position = box->relative_position.to_fvec2();
    }
    if (box->relative_position.type == PIXEL)
    {
        gls_relative_position = pixel_to_glsl_coord(box->relative_position,
                                                    Resolution{(u32)box->parent->px_current_size.ix, (u32)box->parent->px_current_size.iy}).to_fvec2();
    }
    gls_relative_position += RELATIVE_TO_ADD_VALUES[box->relative_to];
    gls_relative_position += RELATIVE_TO_FACTORS[box->relative_to] * gls_size;
    if (box->parent)
    {
        UI_V2 px_size = glsl_to_pixel_coord(UI_V2(gls_size.x, gls_size.y),
                                            Resolution{(u32)box->parent->px_current_size.ix, (u32)box->parent->px_current_size.iy});
        
        UI_V2 px_relative_position = glsl_to_pixel_coord(UI_V2(gls_relative_position.x, gls_relative_position.y),
                                                         Resolution{(u32)box->parent->px_current_size.ix, (u32)box->parent->px_current_size.iy});
        iv2 px_real_position = box->parent->px_position.to_ivec2() + px_relative_position.to_ivec2();
        gls_relative_position = pixel_to_glsl_coord(UI_V2(px_real_position.x, px_real_position.y), backbuffer_resolution).to_fvec2();
    }

    box->gls_position = UI_V2(gls_relative_position.x, gls_relative_position.y);
    box->px_position = glsl_to_pixel_coord(box->gls_position, backbuffer_resolution);
}

internal UI_Box
make_ui_box(Relative_To relative_to, f32 aspect_ratio,
            UI_V2 position /* Coord space agnostic */,
            UI_V2 gls_max_values /* Max X and Y size */,
            UI_Box *parent,
            const u32 &color,
            Resolution backbuffer_resolution = {})
{
    Resolution dst_resolution = backbuffer_resolution;
    if (parent)
    {
        dst_resolution = Resolution{ (u32)parent->px_current_size.ix, (u32)parent->px_current_size.iy };
    }
    
    UI_Box box = {};
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

struct UI_State
{
    struct GUI_Vertex
    {
        v2 position;
        u32 color;
    };
    persist constexpr u32 MAX_QUADS = 10;
    GUI_Vertex cpu_vertex_pool[ MAX_QUADS * 6 ];
    u32 cpu_vertex_count = 0;
    Model_Handle ui_quads_model;
    GPU_Buffer_Handle ui_quads_vbo;
    Pipeline_Handle ui_pipeline;

    struct Textured_Vertex
    {
        v2 position;
        v2 uvs;
    };
    persist constexpr u32 MAX_TX_QUADS = 100;
    Textured_Vertex cpu_tx_vertex_pool[ MAX_TX_QUADS * 6 ];
    u32 cpu_tx_vertex_count = 0;    
    Model_Handle tx_quads_model;
    GPU_Buffer_Handle tx_quads_vbo;
    Pipeline_Handle tx_pipeline;
    // May contain an array of textures
    Uniform_Group tx_group;

    Render_Pass_Handle ui_render_pass;
    GPU_Command_Queue secondary_ui_q;
    
    UI_Box box;
    UI_Box child;
} g_ui;

internal void
push_box_to_render(UI_Box *box)
{
    v2 normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    v2 normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position, box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(0.0f, normalized_size.y), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(normalized_size.x, 0.0f), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(0.0f, normalized_size.y), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(normalized_size.x, 0.0f), box->color};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + normalized_size, box->color};
}

internal void
initialize_ui_elements(GPU *gpu, const Resolution &backbuffer_resolution)
{
    g_ui.box = make_ui_box(LEFT_DOWN, 0.5f,
                           UI_V2(0.05f, 0.05f),
                           UI_V2(1.0f, 0.9f),
                           nullptr,
                           0x16161636,
                           backbuffer_resolution);
    g_ui.child = make_ui_box(RIGHT_UP, 1.0f,
                             UI_V2(0.0f, 0.0f),
                             UI_V2(0.3f, 0.3f),
                             &g_ui.box,
                             0xaa000036,
                             backbuffer_resolution);
}

void
initialize_ui_rendering_state(GPU *gpu, VkFormat swapchain_format, Uniform_Pool *uniform_pool, const Resolution &resolution)
{
    g_ui.ui_quads_model = g_model_manager.add("model.ui_quads"_hash);
    auto *ui_quads_ptr = g_model_manager.get(g_ui.ui_quads_model);
    {
        ui_quads_ptr->attribute_count = 2;
	ui_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	ui_quads_ptr->binding_count = 1;
	ui_quads_ptr->bindings = (Model_Binding *)allocate_free_list(sizeof(Model_Binding));

	// only one binding
	Model_Binding *binding = ui_quads_ptr->bindings;
	binding->begin_attributes_creation(ui_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(UI_State::GUI_Vertex::position));
	binding->push_attribute(1, VK_FORMAT_R32_UINT, sizeof(UI_State::GUI_Vertex::color));

	binding->end_attributes_creation();
    }
    g_ui.tx_quads_model = g_model_manager.add("model.tx_quads"_hash);
    auto *tx_quads_ptr = g_model_manager.get(g_ui.tx_quads_model);
    {
        tx_quads_ptr->attribute_count = 2;
	tx_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	tx_quads_ptr->binding_count = 1;
	tx_quads_ptr->bindings = (Model_Binding *)allocate_free_list(sizeof(Model_Binding));

	// only one binding
	Model_Binding *binding = tx_quads_ptr->bindings;
	binding->begin_attributes_creation(tx_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(UI_State::Textured_Vertex::position));
	binding->push_attribute(1, VK_FORMAT_R32G32_SFLOAT, sizeof(UI_State::Textured_Vertex::uvs));

	binding->end_attributes_creation();
    }

    g_ui.ui_quads_vbo = g_gpu_buffer_manager.add("vbo.ui_quads"_hash);
    auto *vbo = g_gpu_buffer_manager.get(g_ui.ui_quads_vbo);
    {
        auto *main_binding = &ui_quads_ptr->bindings[0];
	
        init_buffer(UI_State::MAX_QUADS * 6 * sizeof(UI_State::GUI_Vertex),
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
	
        init_buffer(UI_State::MAX_TX_QUADS * 6 * sizeof(UI_State::Textured_Vertex),
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
        Render_Pass_Attachment color_attachment = {swapchain_format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        Render_Pass_Subpass subpass = {};
        subpass.set_color_attachment_references(Render_Pass_Attachment_Reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        Render_Pass_Dependency dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(ui_render_pass, {1, &color_attachment}, {1, &subpass}, {2, dependencies}, gpu, false /*Don't clear*/);
    }

    g_ui.ui_pipeline = g_pipeline_manager.add("pipeline.uibox"_hash);
    auto *ui_pipeline = g_pipeline_manager.get(g_ui.ui_pipeline);
    {
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/uiquad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/uiquad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        // Later will be the UI texture uniform layout
        Shader_Uniform_Layouts layouts = {};
        Shader_PK_Data pk = {};
        Shader_Blend_States blending(true);
        Dynamic_States dynamic(VK_DYNAMIC_STATE_VIEWPORT);
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
    
    Uniform_Layout_Handle tx_layout_hdl = g_uniform_layout_manager.add("uniform_layout.tx_ui_quad"_hash);
    auto *tx_layout_ptr = g_uniform_layout_manager.get(tx_layout_hdl);
    {
        Uniform_Layout_Info layout_info;
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *tx_layout_ptr = make_uniform_layout(&layout_info, gpu);
    }
    Image_Handle tx_hdl = g_image_manager.add("image2D.fontmap"_hash);
    auto *tx_ptr = g_image_manager.get(tx_hdl);
    {
        
    }
    Uniform_Group_Handle tx_group_hdl = g_uniform_group_manager.add("uniform_group.tx_ui_quad"_hash);
    auto *tx_group_ptr = g_uniform_group_manager.get(tx_group_hdl);
    {
        *tx_group_ptr = make_uniform_group(tx_layout_ptr, uniform_pool, gpu);
        update_uniform_group(gpu, tx_group_ptr,
                             Update_Binding{ TEXTURE, tx_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
    
    g_ui.tx_pipeline = g_pipeline_manager.add("pipeline.txbox"_hash);
    auto *tx_pipeline = g_pipeline_manager.get(g_ui.tx_pipeline);
    {
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/uifontquad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/uifontquad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        // Later will be the UI texture uniform layout
        Shader_Uniform_Layouts layouts(tx_layout_hdl);
        Shader_PK_Data pk = {};
        Shader_Blend_States blending(true);
        Dynamic_States dynamic(VK_DYNAMIC_STATE_VIEWPORT);
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
initialize_game_ui(GPU *gpu, GPU_Command_Queue_Pool *qpool, Swapchain *swapchain, Uniform_Pool *uniform_pool, const Resolution &resolution)
{
    g_ui.secondary_ui_q = make_command_queue(qpool, VK_COMMAND_BUFFER_LEVEL_SECONDARY, gpu);

    initialize_ui_rendering_state(gpu, swapchain->format, uniform_pool, resolution);
    initialize_ui_elements(gpu, resolution);
}

void
update_game_ui(GPU *gpu, Framebuffer_Handle dst_framebuffer_hdl)
{
    // Loop through all boxes
    push_box_to_render(&g_ui.box);
    push_box_to_render(&g_ui.child);
    
    VkCommandBufferInheritanceInfo inheritance = make_queue_inheritance_info(g_render_pass_manager.get(g_ui.ui_render_pass),
                                                                             g_framebuffer_manager.get(get_pfx_framebuffer_hdl()));
    begin_command_queue(&g_ui.secondary_ui_q, gpu, &inheritance);
    {
        // May execute other stuff
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
            alignas(16) v4 color;
        } pk;
        pk.color = v4(0.2f, 0.2f, 0.2f, 1.0f);

        command_buffer_draw(&g_ui.secondary_ui_q.q,
                            g_ui.cpu_vertex_count,
                            1,
                            0,
                            0);
    }
    end_command_queue(&g_ui.secondary_ui_q, gpu);
}

void
render_game_ui(GPU *gpu, Framebuffer_Handle dst_framebuffer_hdl, GPU_Command_Queue *queue)
{
    // For the moment, this just executes one command buffer
    auto *vbo = g_gpu_buffer_manager.get(g_ui.ui_quads_vbo);
    update_gpu_buffer(vbo,
                      g_ui.cpu_vertex_pool,
                      sizeof(UI_State::GUI_Vertex) * g_ui.cpu_vertex_count,
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
}
