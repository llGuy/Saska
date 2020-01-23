#include "core.hpp"
#include "utils.hpp"
#include "script.hpp"
#include "graphics.hpp"
#include "game.hpp"
#include "ui.hpp"

#include "file_system.hpp"

#include "hud.hpp"
#include "menu.hpp"
#include "gui_math.hpp"
#include "gui_box.hpp"
#include "gui_render_list.hpp"



static gui_textured_vertex_render_list_t textured_vertex_render_list = {};
static gui_colored_vertex_render_list_t colored_vertex_render_list = {};
static render_pass_t gui_render_pass;
static gpu_command_queue_t gui_secondary_queue;
static uniform_group_t font_png_image_uniform;





static ivector2_t get_px_cursor_position(ui_box_t *box, ui_text_t *text, const resolution_t &resolution)
{
    uint32_t px_char_width = (box->px_current_size.ix) / text->chars_per_line;
    uint32_t x_start = (uint32_t)((float32_t)px_char_width * text->x_start);
    px_char_width = (box->px_current_size.ix - 2 * x_start) / text->chars_per_line;
    uint32_t px_char_height = (uint32_t)(text->line_height * (float32_t)px_char_width);
    
    ivector2_t px_cursor_position;
    switch(text->relative_to)
    {
    case ui_text_t::font_stream_box_relative_to_t::TOP:
        {
            uint32_t px_box_top = box->px_position.iy + box->px_current_size.iy;
            px_cursor_position = ivector2_t(x_start + box->px_position.ix, px_box_top - (uint32_t)(text->y_start * (float32_t)px_char_width));
            break;
        }
     case ui_text_t::font_stream_box_relative_to_t::BOTTOM:
        {
            uint32_t px_box_bottom = box->px_position.iy;
            px_cursor_position = ivector2_t(x_start + box->px_position.ix, px_box_bottom + ((uint32_t)(text->y_start * (float32_t)(px_char_width)) + px_char_height));
            break;
        }
    }
    return(px_cursor_position);
}

void push_text_to_render(ui_text_t *text, const resolution_t &resolution)
{
    ui_box_t *box = text->dst_box;

    uint32_t px_char_width = (box->px_current_size.ix) / text->chars_per_line;
    uint32_t x_start = (uint32_t)((float32_t)px_char_width * text->x_start);
    px_char_width = (box->px_current_size.ix - 2 * x_start) / text->chars_per_line;
    uint32_t px_char_height = (uint32_t)(text->line_height * (float32_t)px_char_width);
    
    ivector2_t px_cursor_position = get_px_cursor_position(box, text, resolution);

    uint32_t chars_since_new_line = 0;
    
    for (uint32_t character = 0;
         character < text->char_count;)
    {
        char current_char_value = text->characters[character];
        if (current_char_value == '\n')
        {
            px_cursor_position.y -= px_char_height;
            px_cursor_position.x = x_start + box->px_position.ix;
            ++character;
            chars_since_new_line = 0;
            continue;
        }
        
        font_character_t *font_character_data = &text->font->font_characters[(uint32_t)current_char_value];
        uint32_t color = text->colors[character];

        // Top left
        
        vector2_t px_character_size = vector2_t(vector2_t(font_character_data->display_size) * (float32_t)px_char_width);
        vector2_t px_character_base_position =  vector2_t(px_cursor_position) + vector2_t(vector2_t(font_character_data->offset) * (float32_t)px_char_width);
        vector2_t normalized_base_position = px_character_base_position;
        normalized_base_position /= vector2_t((float32_t)resolution.width, (float32_t)resolution.height);
        normalized_base_position *= 2.0f;
        normalized_base_position -= vector2_t(1.0f);
        vector2_t normalized_size = (px_character_size / vector2_t((float32_t)resolution.width, (float32_t)resolution.height)) * 2.0f;
        vector2_t adjust = vector2_t(0.0f, -normalized_size.y);
        
        vector2_t current_uvs = font_character_data->uvs_base;
        current_uvs.y = 1.0f - current_uvs.y;
        
        textured_vertex_render_list.push_vertex({normalized_base_position + adjust,
                                                current_uvs, color});
        
        current_uvs = font_character_data->uvs_base + vector2_t(0.0f, font_character_data->uvs_size.y);
        current_uvs.y = 1.0f - current_uvs.y;
        textured_vertex_render_list.push_vertex({normalized_base_position + adjust + vector2_t(0.0f, normalized_size.y),
                                                 current_uvs, color});
        
        current_uvs = font_character_data->uvs_base + vector2_t(font_character_data->uvs_size.x, 0.0f);
        current_uvs.y = 1.0f - current_uvs.y;
        textured_vertex_render_list.push_vertex({normalized_base_position + adjust + vector2_t(normalized_size.x, 0.0f),
                                                 current_uvs, color});
        
        current_uvs = font_character_data->uvs_base + vector2_t(0.0f, font_character_data->uvs_size.y);
        current_uvs.y = 1.0f - current_uvs.y;
        textured_vertex_render_list.push_vertex({normalized_base_position + adjust + vector2_t(0.0f, normalized_size.y),
                                                 current_uvs, color});

        current_uvs = font_character_data->uvs_base + vector2_t(font_character_data->uvs_size.x, 0.0f);
        current_uvs.y = 1.0f - current_uvs.y;
        textured_vertex_render_list.push_vertex({normalized_base_position + adjust + vector2_t(normalized_size.x, 0.0f),
                                                current_uvs, color});

        current_uvs = font_character_data->uvs_base + font_character_data->uvs_size;
        current_uvs.y = 1.0f - current_uvs.y;
        textured_vertex_render_list.push_vertex({normalized_base_position + adjust + normalized_size,
                                                 current_uvs, color});

        px_cursor_position += ivector2_t(px_char_width, 0.0f);

        ++character;
        ++chars_since_new_line;
        if (chars_since_new_line % text->chars_per_line == 0)
        {
            px_cursor_position.y -= px_char_height;
            px_cursor_position.x = text->x_start + box->px_position.ix;
        }
    }
}




static void initialize_debug_ui_utils(void)
{
    
}

void push_box_to_render_with_texture(ui_box_t *box, uniform_group_t group)
{
    textured_vertex_render_list.mark_section(group);
    
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    
    textured_vertex_render_list.push_vertex({normalized_base_position, vector2_t(0.0f, 1.0f), box->color});
    textured_vertex_render_list.push_vertex({normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f), box->color});
    textured_vertex_render_list.push_vertex({normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f), box->color});
    textured_vertex_render_list.push_vertex({normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f), box->color});
    textured_vertex_render_list.push_vertex({normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f), box->color});
    textured_vertex_render_list.push_vertex({normalized_base_position + normalized_size, vector2_t(1.0f, 0.0f), box->color});
}

void push_box_to_render(ui_box_t *box)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    
    colored_vertex_render_list.push_vertex({normalized_base_position, box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position + vector2_t(0.0f, normalized_size.y), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position + vector2_t(normalized_size.x, 0.0f), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position + vector2_t(0.0f, normalized_size.y), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position + vector2_t(normalized_size.x, 0.0f), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position + normalized_size, box->color});
}

void push_box_to_render_reversed(ui_box_t *box, const vector2_t &size)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;

    normalized_base_position += size;

    colored_vertex_render_list.push_vertex({normalized_base_position, box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position - vector2_t(0.0f, normalized_size.y), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position - vector2_t(normalized_size.x, 0.0f), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position - vector2_t(0.0f, normalized_size.y), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position - vector2_t(normalized_size.x, 0.0f), box->color});
    colored_vertex_render_list.push_vertex({normalized_base_position - normalized_size, box->color});
}

/*static void push_font_character_to_render(ui_box_t *box)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position, vector2_t(0.0f, 0.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f, 1.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f, 0.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f, 1.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f, 0.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + normalized_size, vector2_t(1.0f)};
    }*/

static console_t *g_console;

// Lua function declarations
static int32_t lua_console_out(lua_State *state);
static int32_t lua_console_clear(lua_State *state);
static int32_t lua_get_fps(lua_State *state);
static int32_t lua_print_fps(lua_State *state);
static int32_t lua_break(lua_State *state);
static int32_t lua_quit(lua_State *state);

bool console_is_receiving_input(void)
{
    return(g_console->receive_input);
}

static void output_to_input_section(const char *string, uint32_t color)
{
    g_console->cursor_position += strlen(string);
    g_console->console_input.draw_string(string, color);
}

static void output_to_output_section(const char *string, uint32_t color)
{
    g_console->console_output.draw_string(string, color);
}

void console_clear(void)
{
    g_console->console_output.char_count = 0;
}

void console_out_i(const char *string)
{
    g_console->console_output.draw_string(string, g_console->output_color);
}

void console_out_i(int32_t i)
{
    char buffer[15] = {};
    sprintf(buffer, "%i\0", i);
    
    g_console->console_output.draw_string(buffer, g_console->output_color);
}

void console_out_i(float32_t f)
{
    char buffer[15] = {};
    sprintf(buffer, "%f\0", f);

    g_console->console_output.draw_string(buffer, g_console->output_color);
}

void console_out_i(const vector2_t &v2)
{
    char buffer[40] = {};
    sprintf(buffer, "%f %f\0", v2.x, v2.y);

    g_console->console_output.draw_string(buffer, g_console->output_color);
}

void console_out_i(const vector3_t &v3)
{
    char buffer[40] = {};
    sprintf(buffer, "%f %f %f\0", v3.x, v3.y, v3.z);

    g_console->console_output.draw_string(buffer, g_console->output_color);
}

void console_out_color_override(const char *string, uint32_t color)
{
    g_console->console_output.draw_string(string, color);
}

static void output_char_to_output_section(char character)
{
    g_console->console_output.draw_char(character, 0x00000000);
}

static void clear_input_section(void)
{
    g_console->input_character_count = 0;
    g_console->console_input.char_count = 0;
    g_console->cursor_position = 0;
    output_to_input_section("> ", g_console->input_color);
}

static void initialize_console(void)
{
    g_console->cursor_color = 0x00ee00ff;
    g_console->cursor_fade = 0xff;
    
    g_console->back_box.initialize(LEFT_DOWN, 0.8f,
                                     ui_vector2_t(0.05f, 0.05f),
                                     ui_vector2_t(1.0f, 0.9f),
                                     nullptr,
                                     0x16161636,
                                     get_backbuffer_resolution());

    font_t *font_ptr = load_font("liberation_mono.font"_hash, "fonts/liberation_mono.fnt", "");

    g_console->console_input.initialize(&g_console->back_box,
                                        font_ptr,
                                        ui_text_t::font_stream_box_relative_to_t::BOTTOM,
                                        0.7f,
                                        1.0f,
                                        55,
                                        1.5f);

    g_console->console_output.initialize(&g_console->back_box,
                                         font_ptr,
                                         ui_text_t::font_stream_box_relative_to_t::TOP,
                                         0.7f,
                                         0.7f,
                                         55,
                                         1.8f);

    output_to_input_section("> ", g_console->input_color);

    add_global_to_lua(script_primitive_type_t::FUNCTION, "c_out", &lua_console_out);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "c_clear", &lua_console_clear);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_fps", &lua_get_fps);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "print_fps", &lua_print_fps);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "debug_break", &lua_break);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "quit", &lua_quit);
}

static void handle_console_input(raw_input_t *raw_input, element_focus_t focus)
{
    if (raw_input->buttons[button_type_t::ESCAPE].state)
    {
        g_console->receive_input = false;
        g_console->render_console = 0;
    }
    
    // Open console - This happens no matter if console has focus or not (or if no "typing" element has focus)
    if (raw_input->char_stack[0] == 't' && !g_console->receive_input)
    {
        g_console->receive_input = true;
        g_console->render_console = 1;
        raw_input->char_stack[0] = 0;
    }
    
    if (focus == element_focus_t::UI_ELEMENT_CONSOLE)
    {
        for (uint32_t i = 0; i < raw_input->char_count; ++i)
        {
            char character[2] = {raw_input->char_stack[i], 0};
            if (character[0])
            {
                output_to_input_section(character, g_console->input_color);
                g_console->input_characters[g_console->input_character_count++] = raw_input->char_stack[i];
                raw_input->char_stack[i] = 0;
            }
        }

        if (raw_input->buttons[button_type_t::BACKSPACE].state)
        {
            if (g_console->input_character_count)
            {
                --g_console->cursor_position;
                --g_console->console_input.char_count;
                --g_console->input_character_count;
            }
        }

        if (raw_input->buttons[button_type_t::ENTER].state)
        {
            g_console->input_characters[g_console->input_character_count] = '\0';
            output_to_output_section(g_console->input_characters, g_console->input_color);
            output_char_to_output_section('\n');
            // Register this to the Lua API
            execute_lua(g_console->input_characters);
            output_char_to_output_section('\n');
           
            clear_input_section();
        }
    }
}

static void push_console_to_render(raw_input_t *raw_input)
{
    textured_vertex_render_list.mark_section(font_png_image_uniform);
    
    if (g_console->cursor_fade > 0xff || g_console->cursor_fade <= 0x00)
    {
        g_console->fade_in_or_out ^= 0x1;
        int32_t adjust = (int32_t)g_console->fade_in_or_out * 2 - 1;
        g_console->cursor_fade -= adjust;
    }
    if (g_console->fade_in_or_out == console_t::fade_t::FADE_OUT)
    {
        g_console->cursor_fade -= (int32_t)(console_t::BLINK_SPEED * raw_input->dt * (float32_t)0xff);
        if (g_console->cursor_fade < 0x00)
        {
            g_console->cursor_fade = 0x00;
        }
        g_console->cursor_color >>= 8;
        g_console->cursor_color <<= 8;
        g_console->cursor_color |= g_console->cursor_fade;
    }
    else
    {
        g_console->cursor_fade += (int32_t)(console_t::BLINK_SPEED * raw_input->dt * (float32_t)0xff);
        g_console->cursor_color >>= 8;
        g_console->cursor_color <<= 8;
        g_console->cursor_color |= g_console->cursor_fade;
    }
    
    resolution_t resolution = get_backbuffer_resolution();
    
    // Push input text
    push_box_to_render(&g_console->back_box);
    push_text_to_render(&g_console->console_input, resolution);
    push_text_to_render(&g_console->console_output, resolution);
    // Push cursor quad
    {
        ui_box_t *box = &g_console->back_box;
        ui_text_t *text = &g_console->console_input;
        
        /*        uint32_t px_char_width = (box->px_current_size.ix - 2 * text->x_start) / text->chars_per_line;
        // TODO: get rid of magic
        uint32_t px_char_height = (uint32_t)(text->line_height * (float32_t)px_char_width) * magic;
        ivector2_t px_cursor_start = get_px_cursor_position(&g_console->back_box, &g_console->console_input, get_backbuffer_resolution());
        ivector2_t px_cursor_position = px_cursor_start + ivector2_t(px_char_width, 0.0f) * (int32_t)g_console->cursor_position;*/

        float32_t magic = 1.4f;
        uint32_t px_char_width = (box->px_current_size.ix) / text->chars_per_line;
        uint32_t x_start = (uint32_t)((float32_t)px_char_width * text->x_start);
        px_char_width = (box->px_current_size.ix - 2 * x_start) / text->chars_per_line;
        uint32_t px_char_height = (uint32_t)(text->line_height * (float32_t)px_char_width) * magic;
        ivector2_t px_cursor_start = get_px_cursor_position(&g_console->back_box, &g_console->console_input, get_backbuffer_resolution());
        ivector2_t px_cursor_position = px_cursor_start + ivector2_t(px_char_width, 0.0f) * (int32_t)g_console->cursor_position;
        
        vector2_t px_cursor_size = vector2_t((float32_t)px_char_width, (float32_t)px_char_height);
        vector2_t px_cursor_base_position =  vector2_t(px_cursor_position) + vector2_t(vector2_t(0.0f, 0.0f));
        vector2_t normalized_cursor_position = px_cursor_base_position;
        normalized_cursor_position /= vector2_t((float32_t)resolution.width, (float32_t)resolution.height);
        normalized_cursor_position *= 2.0f;
        normalized_cursor_position -= vector2_t(1.0f);
        vector2_t normalized_size = (px_cursor_size / vector2_t((float32_t)resolution.width, (float32_t)resolution.height)) * 2.0f;
        vector2_t adjust = vector2_t(0.0f, -normalized_size.y);
        
        colored_vertex_render_list.push_vertex({normalized_cursor_position + adjust,
                                                g_console->cursor_color});
        
        colored_vertex_render_list.push_vertex({normalized_cursor_position + adjust + vector2_t(0.0f, normalized_size.y),
                              g_console->cursor_color});
        
        colored_vertex_render_list.push_vertex({normalized_cursor_position + adjust + vector2_t(normalized_size.x, 0.0f),
                              g_console->cursor_color});
        
        colored_vertex_render_list.push_vertex({normalized_cursor_position + adjust + vector2_t(0.0f, normalized_size.y),
                              g_console->cursor_color});

        colored_vertex_render_list.push_vertex({normalized_cursor_position + adjust + vector2_t(normalized_size.x, 0.0f),
                              g_console->cursor_color});

        colored_vertex_render_list.push_vertex({normalized_cursor_position + adjust + normalized_size,
                              g_console->cursor_color});
    }
    // Push output text
}    

static void initialize_ui_elements(const resolution_t &backbuffer_resolution)
{    
    initialize_console();

    initialize_hud();

    initialize_menus();
}

uniform_group_t create_texture_uniform(const char *path, image2d_t *image)
{
    file_handle_t font_png_handle = create_file(path, file_type_flags_t::IMAGE | file_type_flags_t::ASSET);
    external_image_data_t image_data = read_image(font_png_handle);
        
    make_texture(image,
                 image_data.width,
                 image_data.height,
                 VK_FORMAT_R8G8B8A8_UNORM,
                 1,
                 1,
                 2,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_FILTER_NEAREST);
    transition_image_layout(&image->image,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            get_global_command_pool());
    invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                 get_global_command_pool(),
                                                 image,
                                                 (uint32_t)image_data.width,
                                                 (uint32_t)image_data.height);
    transition_image_layout(&image->image,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            get_global_command_pool());

    free_external_image_data(&image_data);

    uniform_group_t uniform;

    auto *layout = g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.tx_ui_quad"_hash));
    
    uniform = make_uniform_group(layout, g_uniform_pool);
    update_uniform_group(&uniform,
                         update_binding_t{ TEXTURE, image, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

    return(uniform);
}

void initialize_ui_rendering_state(VkFormat swapchain_format,
                                   uniform_pool_t *uniform_pool,
                                   const resolution_t &resolution,
                                   gpu_command_queue_pool_t *queue_pool)
{
    auto *ui_quads_ptr = &colored_vertex_render_list.vtx_attribs;
    {
        ui_quads_ptr->attribute_count = 2;
	ui_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	ui_quads_ptr->binding_count = 1;
	ui_quads_ptr->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

	// only one binding
	model_binding_t *binding = ui_quads_ptr->bindings;
	binding->begin_attributes_creation(ui_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(gui_colored_vertex_t::position));
	binding->push_attribute(1, VK_FORMAT_R32_UINT, sizeof(gui_colored_vertex_t::color));

	binding->end_attributes_creation();
    }
    auto *tx_quads_ptr = &textured_vertex_render_list.vtx_attribs;
    {
        tx_quads_ptr->attribute_count = 3;
	tx_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	tx_quads_ptr->binding_count = 1;
	tx_quads_ptr->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

	// only one binding
	model_binding_t *binding = tx_quads_ptr->bindings;
	binding->begin_attributes_creation(tx_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(gui_textured_vertex_t::position));
	binding->push_attribute(1, VK_FORMAT_R32G32_SFLOAT, sizeof(gui_textured_vertex_t::uvs));
        binding->push_attribute(2, VK_FORMAT_R32_UINT, sizeof(gui_textured_vertex_t::color));

	binding->end_attributes_creation();
    }

    auto *vbo = &colored_vertex_render_list.vtx_buffer;
    {
        auto *main_binding = &ui_quads_ptr->bindings[0];
	
        init_buffer(MAX_QUADS_TO_RENDER * 6 * sizeof(gui_colored_vertex_t),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    vbo);

        main_binding->buffer = vbo->buffer;
        ui_quads_ptr->create_vbo_list();
    }

    auto *tx_vbo = &textured_vertex_render_list.vtx_buffer;
    {
        auto *main_binding = &tx_quads_ptr->bindings[0];
	
        init_buffer(MAX_QUADS_TO_RENDER * 6 * sizeof(gui_textured_vertex_t),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    tx_vbo);

        main_binding->buffer = tx_vbo->buffer;
        tx_quads_ptr->create_vbo_list();
    }

    auto *ui_render_pass = &gui_render_pass;
    {
        render_pass_attachment_t color_attachment = {swapchain_format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        render_pass_subpass_t subpass = {};
        subpass.set_color_attachment_references(render_pass_attachment_reference_t{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT
                                                      , 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      , VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        make_render_pass(ui_render_pass, {1, &color_attachment}, {1, &subpass}, {2, dependencies}, false /*don_t't clear*/);
    }

    auto *ui_pipeline = &colored_vertex_render_list.gfx_pipeline;
    {
        graphics_pipeline_info_t *pipeline_info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/uiquad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/uiquad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        // Later will be the UI texture uniform layout
        shader_uniform_layouts_t layouts = {};
        shader_pk_data_t pk = {};
        shader_blend_states_t blending(blend_type_t::ONE_MINUS_SRC_ALPHA);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules,
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
                                    pipeline_info);

        ui_pipeline->info = pipeline_info;
        
        make_graphics_pipeline(ui_pipeline);
    }
    
    uniform_layout_handle_t tx_layout_hdl = g_uniform_layout_manager->add("uniform_layout.tx_ui_quad"_hash);
    auto *tx_layout_ptr = g_uniform_layout_manager->get(tx_layout_hdl);
    {
        uniform_layout_info_t layout_info;
        layout_info.push(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        *tx_layout_ptr = make_uniform_layout(&layout_info);
    }
    image_handle_t tx_hdl = g_image_manager->add("image2D.fontmap"_hash);
    auto *tx_ptr = g_image_manager->get(tx_hdl);
    {
        file_handle_t font_png_handle = create_file("fonts/liberation_mono.png", file_type_flags_t::IMAGE | file_type_flags_t::ASSET);
        external_image_data_t image_data = read_image(font_png_handle);
        
        make_texture(tx_ptr,
                     image_data.width,
                     image_data.height,
                     VK_FORMAT_R8G8B8A8_UNORM,
                     1,
                     1,
                     2,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_FILTER_NEAREST);
        transition_image_layout(&tx_ptr->image,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                queue_pool);
        invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                     queue_pool,
                                                     tx_ptr,
                                                     (uint32_t)image_data.width,
                                                     (uint32_t)image_data.height);
        transition_image_layout(&tx_ptr->image,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                queue_pool);

        free_external_image_data(&image_data);
    }
    
    auto *tx_group_ptr = &font_png_image_uniform;
    {
        *tx_group_ptr = make_uniform_group(tx_layout_ptr, uniform_pool);
        update_uniform_group(tx_group_ptr,
                             update_binding_t{ TEXTURE, tx_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
    
    auto *tx_pipeline = &textured_vertex_render_list.gfx_pipeline;
    {
        graphics_pipeline_info_t *pipeline_info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/uifontquad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/uifontquad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        // Later will be the UI texture uniform layout
        shader_uniform_layouts_t layouts(tx_layout_hdl);
        shader_pk_data_t pk = {};
        shader_blend_states_t blending(blend_type_t::ONE_MINUS_SRC_ALPHA);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules,
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
                                    pipeline_info);
        tx_pipeline->info = pipeline_info;
        make_graphics_pipeline(tx_pipeline);
    }

    gui_secondary_queue.submit_level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
}

// will be rendered to backbuffer first
void initialize_game_ui(gpu_command_queue_pool_t *qpool, uniform_pool_t *uniform_pool, const resolution_t &resolution)
{
    gui_secondary_queue = make_command_queue(qpool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);

    initialize_ui_rendering_state(get_swapchain_format(), uniform_pool, resolution, qpool);
    initialize_ui_elements(resolution);
}

void update_game_ui(framebuffer_handle_t dst_framebuffer_hdl, raw_input_t *raw_input, element_focus_t focus)
{
    if (focus == element_focus_t::UI_ELEMENT_CONSOLE)
    {
        handle_console_input(raw_input, focus);

        if (g_console->render_console)
        {
            push_console_to_render(raw_input);
        }
    }
    
    // in-game stuff
    push_hud_to_render(&textured_vertex_render_list, focus);

    if (focus == element_focus_t::UI_ELEMENT_MENU)
    {
        update_menus(raw_input, focus);
        push_menus_to_render(&textured_vertex_render_list, &colored_vertex_render_list, focus);
    }
    
    VkCommandBufferInheritanceInfo inheritance = make_queue_inheritance_info(&gui_render_pass,
                                                                             g_framebuffer_manager->get(get_pfx_framebuffer_hdl()));
    begin_command_queue(&gui_secondary_queue, &inheritance);
    {
        // may_t execute other stuff
        auto *dst_framebuffer = g_framebuffer_manager->get(dst_framebuffer_hdl);

        textured_vertex_render_list.render_textured_quads(&gui_secondary_queue, dst_framebuffer);
        colored_vertex_render_list.render_colored_quads(&gui_secondary_queue, dst_framebuffer);
    }
    end_command_queue(&gui_secondary_queue);
}

void render_game_ui(framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue)
{
    // for_t the moment, this just executes one command buffer
    textured_vertex_render_list.sync_gpu_with_vertex_list(queue);
    colored_vertex_render_list.sync_gpu_with_vertex_list(queue);
    
    queue->begin_render_pass(&gui_render_pass,
                             g_framebuffer_manager->get(dst_framebuffer_hdl),
                             VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    command_buffer_execute_commands(&queue->q, {1, &gui_secondary_queue.q});
    queue->end_render_pass();

    textured_vertex_render_list.clear_containers();
    colored_vertex_render_list.clear_containers();
}

// All console-and-ui-linked commands (e.g. printing, ui stuff)
static int32_t lua_console_out(lua_State *state)
{
    int32_t type = lua_type(state, -1);
    switch(type)
    {
    case LUA_TNUMBER:
        {
            int32_t number =  lua_tonumber(state, -1);
            char buffer[20] = {};
            sprintf(buffer, "%d", number);
            output_to_output_section(buffer, g_console->output_color);
            break;
        }
    case LUA_TSTRING:
        {
            const char *string = lua_tostring(state, -1);
            output_to_output_section(string, g_console->output_color);            
            break;
        }
    }

    return(0);
}

static int32_t lua_console_clear(lua_State *state)
{
    g_console->console_output.char_count = 0;
    return(0);
}

static int32_t lua_get_fps(lua_State *state)
{
    //    inpus_state_t *win = get_window_data();
    //    lua_pushnumber(state, 1.0f / win->dt);
    return(0);
}

static int32_t lua_print_fps(lua_State *state)
{
    raw_input_t *is = get_raw_input();
    
    console_out(1.0f / (is->dt), " | ");
    
    return(0);
}

static int32_t lua_break(lua_State *state)
{
    __debugbreak();
    return(0);
}

void initialize_ui_translation_unit(struct game_memory_t *memory)
{
    g_console = &memory->user_interface_state.console;
}

static int32_t lua_quit(lua_State *state)
{
    request_quit();
    return(0);
}
