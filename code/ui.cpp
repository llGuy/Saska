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
enum Relative_To { LEFT_UP, LEFT_DOWN, CENTER, RIGHT_UP, RIGHT_DOWN };

struct UI_Box
{
    UI_Box *parent {nullptr};
    Relative_To relative_to;
    UI_V2 gls_position;
    UI_V2 px_position;
    f32 aspect_ratio;
    UI_V2 gls_max_values;
    UI_V2 px_current_size;
    UI_V2 gls_current_size;
};

internal void
update_ui_box_size(UI_Box *box, const Resolution &backbuffer_resolution)
{
    UI_V2 px_max_values = glsl_to_pixel_coord(box->gls_max_values, backbuffer_resolution);
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
    box->gls_current_size = pixel_to_glsl_coord(box->px_current_size, backbuffer_resolution);
}

internal UI_Box
make_ui_box(Relative_To relative_to, f32 aspect_ratio,
            UI_V2 position /* Coord space agnostic */,
            UI_V2 gls_max_values /* Max X and Y size */,
            UI_Box *parent,
            Resolution backbuffer_resolution = {})
{
    UI_Box box = {};
    box.parent = parent;
    box.aspect_ratio = aspect_ratio;
    box.gls_max_values = gls_max_values;
    update_ui_box_size(&box, backbuffer_resolution);
    box.relative_to = relative_to;
    if (position.type == GLSL)
    {
        box.gls_position = position;
        box.px_position = glsl_to_pixel_coord(position, backbuffer_resolution);
    }
    else if (position.type == PIXEL)
    {
        box.px_position = position;
        box.gls_position = pixel_to_glsl_coord(position, backbuffer_resolution);
    }
    return(box);
}

struct UI_State
{
    struct GUI_Vertex
    {
        v2 position;
        v2 uvs;
    };

    persist constexpr u32 FONT_VBO_SIZE = 65536;
    persist constexpr u32 MAX_FONT_VERTICES_PER_UPDATE = FONT_VBO_SIZE / sizeof(GUI_Vertex);
    persist constexpr u32 MAX_FONT_QUADS_PER_UPDATE = MAX_FONT_VERTICES_PER_UPDATE / 6;
    GUI_Vertex cpu_vertex_pool[ MAX_FONT_VERTICES_PER_UPDATE ];
    u32 cpu_vertex_count = 0;
    
    Model_Handle ui_quads_model;
    GPU_Buffer_Handle ui_quads_vbo;
    Pipeline_Handle ui_pipeline;
    Render_Pass_Handle ui_render_pass;
    GPU_Command_Queue secondary_ui_q;

    UI_Box box;
} g_ui;

internal void
push_box_to_render(UI_Box *box)
{
    v2 normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    v2 normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(0.0f, normalized_size.y)};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(normalized_size.x, 0.0f)};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(0.0f, normalized_size.y)};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + v2(normalized_size.x, 0.0f)};
    g_ui.cpu_vertex_pool[g_ui.cpu_vertex_count++] = {normalized_base_position + normalized_size};
}

internal void
initialize_ui_elements(GPU *gpu, const Resolution &backbuffer_resolution)
{
    //    f32 aspect_ratio = (f32)backbuffer_resolution.width / (f32)backbuffer_resolution.height;
    f32 aspect_ratio = 2.0f;
    g_ui.box = make_ui_box(LEFT_DOWN, aspect_ratio,
                           UI_V2(0.05f, 0.05f),
                           UI_V2(0.2f, 0.2f),
                           nullptr,
                           backbuffer_resolution);
}

void
initialize_ui_rendering_state(GPU *gpu, VkFormat swapchain_format, const Resolution &resolution)
{
    g_ui.ui_quads_model = g_model_manager.add("model.font_quads"_hash);
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
	binding->push_attribute(1, VK_FORMAT_R32G32_SFLOAT, sizeof(UI_State::GUI_Vertex::uvs));

	binding->end_attributes_creation();
    }

    g_ui.ui_quads_vbo = g_gpu_buffer_manager.add("vbo.font_quads"_hash);
    auto *vbo = g_gpu_buffer_manager.get(g_ui.ui_quads_vbo);
    {
        auto *main_binding = &ui_quads_ptr->bindings[0];
	
        init_buffer(UI_State::FONT_VBO_SIZE,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    gpu,
                    vbo);

        main_binding->buffer = vbo->buffer;
        ui_quads_ptr->create_vbo_list();
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
        Shader_PK_Data pk = {160, 0, VK_SHADER_STAGE_FRAGMENT_BIT};
        Shader_Blend_States blending(false);
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
    g_ui.secondary_ui_q.submit_level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
}

// will be rendered to backbuffer first
void
initialize_game_ui(GPU *gpu, GPU_Command_Queue_Pool *qpool, Swapchain *swapchain, const Resolution &resolution)
{
    g_ui.secondary_ui_q = make_command_queue(qpool, VK_COMMAND_BUFFER_LEVEL_SECONDARY, gpu);

    initialize_ui_rendering_state(gpu, swapchain->format, resolution);
    initialize_ui_elements(gpu, resolution);
}

void
update_game_ui(GPU *gpu, Framebuffer_Handle dst_framebuffer_hdl)
{
    // Loop through all boxes
    push_box_to_render(&g_ui.box);
    VkCommandBufferInheritanceInfo inheritance = make_queue_inheritance_info(g_render_pass_manager.get(g_ui.ui_render_pass),
                                                                             g_framebuffer_manager.get(get_pfx_framebuffer_hdl()));
    begin_command_queue(&g_ui.secondary_ui_q, gpu, &inheritance);
    {
        // May execute other stuf
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
        pk.color = v4(1.0f, 0.0f, 0.0f, 1.0f);

        command_buffer_push_constant(&pk,
                                     sizeof(pk),
                                     0,
                                     VK_SHADER_STAGE_FRAGMENT_BIT,
                                     ui_pipeline,
                                     &g_ui.secondary_ui_q.q);
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
