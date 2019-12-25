#include "core.hpp"
#include "utils.hpp"
#include "script.hpp"
#include "graphics.hpp"
#include "game.hpp"
#include "ui.hpp"

#include "file_system.hpp"

static inline vector2_t convert_glsl_to_normalized(const vector2_t &position)
{
    return(position * 2.0f - 1.0f);
}

static inline ui_vector2_t glsl_to_pixel_coord(const ui_vector2_t &position, const resolution_t &resolution)
{
    ui_vector2_t ret((int32_t)(position.fx * (float32_t)resolution.width), (int32_t)(position.fy * (float32_t)resolution.height));
    return(ret);
}

static inline ui_vector2_t pixel_to_glsl_coord(const ui_vector2_t &position, const resolution_t &resolution)
{
    ui_vector2_t ret((float32_t)position.ix / (float32_t)resolution.width,
                     (float32_t)position.iy / (float32_t)resolution.height);
    return(ret);
}

// for_t now, doesn't support the relative to function (need to add in the fufure)
static constexpr vector2_t RELATIVE_TO_ADD_VALUES[] { vector2_t(0.0f, 0.0f),
        vector2_t(0.0f, 1.0f),
        vector2_t(0.5f, 0.5f),
        vector2_t(1.0f, 0.0f),
        vector2_t(1.0f, 1.0f)};
static constexpr vector2_t RELATIVE_TO_FACTORS[] { vector2_t(0.0f, 0.0f),
        vector2_t(0.0f, -1.0f),
        vector2_t(-0.5f, -0.5f),
        vector2_t(-1.0f, 0.0f),
        vector2_t(-1.0f, -1.0f)};

uint32_t vec4_color_to_ui32b(const vector4_t &color)
{
    float32_t xf = color.x * 255.0f;
    float32_t yf = color.y * 255.0f;
    float32_t zf = color.z * 255.0f;
    float32_t wf = color.w * 255.0f;
    uint32_t xui = (uint32_t)xf;
    uint32_t yui = (uint32_t)yf;
    uint32_t zui = (uint32_t)zf;
    uint32_t wui = (uint32_t)wf;
    return (xui << 24) | (yui << 16) | (zui << 8) | wui;
}

static void update_ui_box_size(ui_box_t *box, const resolution_t &backbuffer_resolution)
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

static void update_ui_box_position(ui_box_t *box, const resolution_t &backbuffer_resolution)
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

static ui_box_t make_ui_box(relative_to_t relative_to, float32_t aspect_ratio,
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

static void make_text(ui_box_t *box,
                                 font_t *font,
                                 ui_text_t::font_stream_box_relative_to_t relative_to,
                                 float32_t px_x_start,
                                 float32_t px_y_start,
                                 uint32_t chars_per_line,
                                 float32_t line_height,
                                 ui_text_t *dst_text)
{
    dst_text->dst_box = box;
    dst_text->font = font;
    dst_text->relative_to = relative_to;
    dst_text->x_start = px_x_start;
    dst_text->y_start = px_y_start;
    dst_text->chars_per_line = chars_per_line;
    dst_text->line_height = line_height;
}

struct fonts_t
{
    static constexpr uint32_t MAX_FONTS = 5;
    font_t fonts[MAX_FONTS];
    uint32_t font_count;

    hash_table_inline_t<uint32_t, 10, 3, 4> font_map;

    ui_text_t test_text;
} g_fonts;

struct fnt_word_t
{
    char *pointer;
    uint16_t size;
};

// Except new line
static char *fnt_skip_break_characters(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case ' ': case '=': ++pointer;
        default: return pointer;
        }
    }
    return nullptr;
}

static char *fnt_goto_next_break_character(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case ' ': case '=': case '\n': return pointer;
        default: ++pointer;
        }
    }
    return nullptr;
}

static char *fnt_skip_line(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case '\n': return ++pointer;
        default: ++pointer;
        }
    }
    return nullptr;
}

static char *fnt_move_and_get_word(char *pointer, fnt_word_t *dst_word)
{
    char *new_pointer = fnt_goto_next_break_character(pointer);
    if (dst_word)
    {
        dst_word->pointer = pointer;
        dst_word->size = new_pointer - pointer;
    }
    return(new_pointer);
}

static char *fnt_skip_until_digit(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': return pointer;
        default: ++pointer;
        }
    }
    return nullptr;
}

struct fnt_string_number_t
{
    fnt_word_t word;
    // Maximum 10 chars for a number
    char characters[10];
};

static int32_t fnt_atoi(fnt_word_t word)
{
    fnt_string_number_t number;
    memcpy(number.characters, word.pointer, sizeof(char) * word.size);
    number.characters[word.size] = '\0';
    return(atoi(number.characters));
}

static char *fnt_get_char_count(char *pointer, int32_t *count)
{
    for(;;)
    {
        fnt_word_t chars_str;
        pointer = fnt_move_and_get_word(pointer, &chars_str);
        if (chars_str.size == strlen("chars"))
        {
            pointer = fnt_skip_until_digit(pointer);
            fnt_word_t count_str;
            pointer = fnt_move_and_get_word(pointer, &count_str);
            *count = fnt_atoi(count_str);
            pointer = fnt_skip_line(pointer);
            return pointer;
        }
        pointer = fnt_skip_line(pointer);
    }
}

char *fnt_get_font_attribute_value(char *pointer, int32_t *value)
{
    pointer = fnt_skip_until_digit(pointer);
    fnt_word_t value_str;
    pointer = fnt_move_and_get_word(pointer, &value_str);
    *value = fnt_atoi(value_str);
    return(pointer);
}

font_handle_t add_font(const constant_string_t &font_name)
{
    uint32_t current_count = g_fonts.font_count++;
    g_fonts.font_map.insert(font_name.hash, current_count);
    return(current_count);
}

font_t *get_font(font_handle_t handle)
{
    return(&g_fonts.fonts[handle]);
}

font_t *load_font(const constant_string_t &font_name, const char *fnt_file, const char *png_file)
{
    // TODO : Make sure this is parameterised, not fixed!
    static constexpr float32_t FNT_MAP_W = 512.0f, FNT_MAP_H = 512.0f;
    
    font_handle_t hdl = add_font(font_name);
    font_t *font_ptr = get_font(hdl);

    file_handle_t fnt_file_handle = create_file(fnt_file, file_type_flags_t::TEXT | file_type_flags_t::ASSET);
    file_contents_t fnt = read_file_tmp(fnt_file_handle);
    int32_t char_count = 0;
    char *current_char = fnt_get_char_count((char *)fnt.content, &char_count);
    // Ready to start parsing the file
    for (uint32_t i = 0; i < char_count; ++i)
    {
        // Char ID value
        int32_t char_id = 0;
        current_char = fnt_get_font_attribute_value(current_char, &char_id);

        // X value
        int32_t x = 0;
        current_char = fnt_get_font_attribute_value(current_char, &x);

        // X value
        int32_t y = 0;
        current_char = fnt_get_font_attribute_value(current_char, &y);
        y = FNT_MAP_H - y;
        
        // Width value
        int32_t width = 0;
        current_char = fnt_get_font_attribute_value(current_char, &width);

        // Height value
        int32_t height = 0;
        current_char = fnt_get_font_attribute_value(current_char, &height);

        // XOffset value
        int32_t xoffset = 0;
        current_char = fnt_get_font_attribute_value(current_char, &xoffset);

        // YOffset value
        int32_t yoffset = 0;
        current_char = fnt_get_font_attribute_value(current_char, &yoffset);

        // XAdvanc value
        int32_t xadvance = 0;
        current_char = fnt_get_font_attribute_value(current_char, &xadvance);

        font_character_t *character = &font_ptr->font_characters[char_id];
        character->character_value = (char)char_id;
        // ----------------------------------------------------------------------------- \/ Do y - height so that base position is at bottom of character
        character->uvs_base = vector2_t((float32_t)x / (float32_t)FNT_MAP_W, (float32_t)(y - height) / (float32_t)FNT_MAP_H);
        character->uvs_size = vector2_t((float32_t)width / (float32_t)FNT_MAP_W, (float32_t)height / (float32_t)FNT_MAP_H);
        character->display_size = vector2_t((float32_t)width / (float32_t)xadvance, (float32_t)height / (float32_t)xadvance);
        character->offset = vector2_t((float32_t)xoffset / (float32_t)xadvance, (float32_t)yoffset / (float32_t)xadvance);
        character->offset.y *= -1.0f;
        character->advance = (float32_t)xadvance / (float32_t)xadvance;
        
        current_char = fnt_skip_line(current_char);
    }

    remove_and_destroy_file(fnt_file_handle);

    return(font_ptr);
}

static void draw_char(ui_text_t *text, char character, uint32_t color)
{
    text->colors[text->char_count] = color;
    text->characters[text->char_count++] = character;
}

static void draw_string(ui_text_t *text, const char *string, uint32_t color)
{
    uint32_t string_length = strlen(string);
    memcpy(text->characters + text->char_count, string, sizeof(char) * string_length);
    for (uint32_t i = 0; i < string_length; ++i)
    {
        text->colors[text->char_count + i] = color;
    }
    text->char_count += string_length;
}

// TODO: Make a textured quad renderer
static dbg_ui_utils_t *dbg_ui_utils;

static void initialize_debug_ui_utils(void)
{
    
}

static ui_state_t *g_ui;

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

static void push_text(ui_text_t *text, const resolution_t &resolution)
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
        
        g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + adjust,
                                                               current_uvs, color};
        
        current_uvs = font_character_data->uvs_base + vector2_t(0.0f, font_character_data->uvs_size.y);
        current_uvs.y = 1.0f - current_uvs.y;
        g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + adjust + vector2_t(0.0f, normalized_size.y),
                                                               current_uvs, color};
        
        current_uvs = font_character_data->uvs_base + vector2_t(font_character_data->uvs_size.x, 0.0f);
        current_uvs.y = 1.0f - current_uvs.y;
        g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + adjust + vector2_t(normalized_size.x, 0.0f),
                                                               current_uvs, color};
        
        current_uvs = font_character_data->uvs_base + vector2_t(0.0f, font_character_data->uvs_size.y);
        current_uvs.y = 1.0f - current_uvs.y;
        g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + adjust + vector2_t(0.0f, normalized_size.y),
                                                               current_uvs, color};

        current_uvs = font_character_data->uvs_base + vector2_t(font_character_data->uvs_size.x, 0.0f);
        current_uvs.y = 1.0f - current_uvs.y;
        g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + adjust + vector2_t(normalized_size.x, 0.0f),
                                                               current_uvs, color};

        current_uvs = font_character_data->uvs_base + font_character_data->uvs_size;
        current_uvs.y = 1.0f - current_uvs.y;
        g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + adjust + normalized_size,
                                                               current_uvs, color};

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

static void push_box_to_render(ui_box_t *box)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_base_position, box->color};
    g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), box->color};
    g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), box->color};
    g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), box->color};
    g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), box->color};
    g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_base_position + normalized_size, box->color};
}

static void push_font_character_to_render(ui_box_t *box)
{
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position, vector2_t(0.0f, 0.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f, 1.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f, 0.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(0.0f, normalized_size.y), vector2_t(0.0f, 1.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + vector2_t(normalized_size.x, 0.0f), vector2_t(1.0f, 0.0f)};
    g_ui->cpu_tx_vertex_pool[g_ui->cpu_tx_vertex_count++] = {normalized_base_position + normalized_size, vector2_t(1.0f)};
}

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
    draw_string(&g_console->console_input, string, color);
}

static void output_to_output_section(const char *string, uint32_t color)
{
    draw_string(&g_console->console_output, string, color);
}

void console_clear(void)
{
    g_console->console_output.char_count = 0;
}

void console_out_i(const char *string)
{
    draw_string(&g_console->console_output, string, g_console->output_color);
}

void console_out_i(int32_t i)
{
    char buffer[15] = {};
    sprintf(buffer, "%i\0", i);
    
    draw_string(&g_console->console_output, buffer, g_console->output_color);
}

void console_out_i(float32_t f)
{
    char buffer[15] = {};
    sprintf(buffer, "%f\0", f);

    draw_string(&g_console->console_output, buffer, g_console->output_color);
}

void console_out_i(const vector2_t &v2)
{
    char buffer[40] = {};
    sprintf(buffer, "%f %f\0", v2.x, v2.y);

    draw_string(&g_console->console_output, buffer, g_console->output_color);
}

void console_out_i(const vector3_t &v3)
{
    char buffer[40] = {};
    sprintf(buffer, "%f %f %f\0", v3.x, v3.y, v3.z);

    draw_string(&g_console->console_output, buffer, g_console->output_color);
}

void console_out_color_override(const char *string, uint32_t color)
{
    draw_string(&g_console->console_output, string, color);
}

static void output_char_to_output_section(char character)
{
    draw_char(&g_console->console_output, character, 0x00000000);
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
    
    g_console->back_box = make_ui_box(LEFT_DOWN, 0.8f,
                                     ui_vector2_t(0.05f, 0.05f),
                                     ui_vector2_t(1.0f, 0.9f),
                                     nullptr,
                                     0x16161636,
                                     get_backbuffer_resolution());

    font_t *font_ptr = load_font("liberation_mono_font"_hash, "fonts/liberation_mono.fnt", "");
    make_text(&g_console->back_box,
              font_ptr,
              ui_text_t::font_stream_box_relative_to_t::BOTTOM,
              0.7f,
              1.0f,
              55,
              1.5f,
              &g_console->console_input);

    make_text(&g_console->back_box,
              font_ptr,
              ui_text_t::font_stream_box_relative_to_t::TOP,
              0.7f,
              0.7f,
              55,
              1.8f,
              &g_console->console_output);

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

    if (raw_input->buttons[button_type_t::ESCAPE].state)
    {
        g_console->receive_input = false;
    }
    // Open console - This happens no matter if console has focus or not (or if no "typing" element has focus)
    if (raw_input->char_stack[0] == 'c' && !g_console->receive_input)
    {
        g_console->render_console ^= 0x1;
        raw_input->char_stack[0] = 0;
    }
}

static void push_console_to_render(raw_input_t *raw_input)
{
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
    push_text(&g_console->console_input, resolution);
    push_text(&g_console->console_output, resolution);
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
        
        g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_cursor_position + adjust,
                                                               g_console->cursor_color};
        
        g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_cursor_position + adjust + vector2_t(0.0f, normalized_size.y),
                                                               g_console->cursor_color};
        
        g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_cursor_position + adjust + vector2_t(normalized_size.x, 0.0f),
                                                               g_console->cursor_color};
        
        g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_cursor_position + adjust + vector2_t(0.0f, normalized_size.y),
                                                               g_console->cursor_color};

        g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_cursor_position + adjust + vector2_t(normalized_size.x, 0.0f),
                                                               g_console->cursor_color};

        g_ui->cpu_vertex_pool[g_ui->cpu_vertex_count++] = {normalized_cursor_position + adjust + normalized_size,
                                                               g_console->cursor_color};
    }
    // Push output text
}    

static void initialize_ui_elements(const resolution_t &backbuffer_resolution)
{    
    initialize_console();
}

void initialize_ui_rendering_state(VkFormat swapchain_format,
                                   uniform_pool_t *uniform_pool,
                                   const resolution_t &resolution,
                                   gpu_command_queue_pool_t *queue_pool)
{
    g_ui->ui_quads_model = g_model_manager->add("model.ui_quads"_hash);
    auto *ui_quads_ptr = g_model_manager->get(g_ui->ui_quads_model);
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
    g_ui->tx_quads_model = g_model_manager->add("model.tx_quads"_hash);
    auto *tx_quads_ptr = g_model_manager->get(g_ui->tx_quads_model);
    {
        tx_quads_ptr->attribute_count = 3;
	tx_quads_ptr->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	tx_quads_ptr->binding_count = 1;
	tx_quads_ptr->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

	// only one binding
	model_binding_t *binding = tx_quads_ptr->bindings;
	binding->begin_attributes_creation(tx_quads_ptr->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(ui_state_t::textured_vertex_t::position));
	binding->push_attribute(1, VK_FORMAT_R32G32_SFLOAT, sizeof(ui_state_t::textured_vertex_t::uvs));
        binding->push_attribute(2, VK_FORMAT_R32_UINT, sizeof(ui_state_t::textured_vertex_t::color));

	binding->end_attributes_creation();
    }

    g_ui->ui_quads_vbo = g_gpu_buffer_manager->add("vbo.ui_quads"_hash);
    auto *vbo = g_gpu_buffer_manager->get(g_ui->ui_quads_vbo);
    {
        auto *main_binding = &ui_quads_ptr->bindings[0];
	
        init_buffer(ui_state_t::MAX_QUADS * 6 * sizeof(ui_state_t::gui_vertex_t),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    vbo);

        main_binding->buffer = vbo->buffer;
        ui_quads_ptr->create_vbo_list();
    }
    g_ui->tx_quads_vbo = g_gpu_buffer_manager->add("vbo.tx_quads"_hash);
    auto *tx_vbo = g_gpu_buffer_manager->get(g_ui->tx_quads_vbo);
    {
        auto *main_binding = &tx_quads_ptr->bindings[0];
	
        init_buffer(ui_state_t::MAX_TX_QUADS * 6 * sizeof(ui_state_t::textured_vertex_t),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    tx_vbo);

        main_binding->buffer = tx_vbo->buffer;
        tx_quads_ptr->create_vbo_list();
    }

    g_ui->ui_render_pass = g_render_pass_manager->add("render_pass.ui"_hash);
    auto *ui_render_pass = g_render_pass_manager->get(g_ui->ui_render_pass);
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

    g_ui->ui_pipeline = g_pipeline_manager->add("pipeline.uibox"_hash);
    auto *ui_pipeline = g_pipeline_manager->get(g_ui->ui_pipeline);
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
    g_ui->tx_group = g_uniform_group_manager->add("uniform_group.tx_ui_quad"_hash);
    auto *tx_group_ptr = g_uniform_group_manager->get(g_ui->tx_group);
    {
        *tx_group_ptr = make_uniform_group(tx_layout_ptr, uniform_pool);
        update_uniform_group(tx_group_ptr,
                             update_binding_t{ TEXTURE, tx_ptr, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
    
    g_ui->tx_pipeline = g_pipeline_manager->add("pipeline.txbox"_hash);
    auto *tx_pipeline = g_pipeline_manager->get(g_ui->tx_pipeline);
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

    g_ui->secondary_ui_q.submit_level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
}

// will be rendered to backbuffer first
void initialize_game_ui(gpu_command_queue_pool_t *qpool, uniform_pool_t *uniform_pool, const resolution_t &resolution)
{
    g_ui->secondary_ui_q = make_command_queue(qpool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);

    initialize_ui_rendering_state(get_swapchain_format(), uniform_pool, resolution, qpool);
    initialize_ui_elements(resolution);
}

void update_game_ui(framebuffer_handle_t dst_framebuffer_hdl, raw_input_t *raw_input, element_focus_t focus)
{
    handle_console_input(raw_input, focus);
    
    if (g_console->render_console)
    {
        push_console_to_render(raw_input);
    }
    
    VkCommandBufferInheritanceInfo inheritance = make_queue_inheritance_info(g_render_pass_manager->get(g_ui->ui_render_pass),
                                                                             g_framebuffer_manager->get(get_pfx_framebuffer_hdl()));
    begin_command_queue(&g_ui->secondary_ui_q, &inheritance);
    {
        // may_t execute other stuff
        auto *dst_framebuffer = g_framebuffer_manager->get(dst_framebuffer_hdl);
        command_buffer_set_viewport(dst_framebuffer->extent.width, dst_framebuffer->extent.height, 0.0f, 1.0f, &g_ui->secondary_ui_q.q);
        
        auto *ui_pipeline = g_pipeline_manager->get(g_ui->ui_pipeline);
        command_buffer_bind_pipeline(&ui_pipeline->pipeline, &g_ui->secondary_ui_q.q);
        VkDeviceSize zero = 0;
        auto *quads_model = g_model_manager->get(g_ui->ui_quads_model);
        command_buffer_bind_vbos(quads_model->raw_cache_for_rendering,
                                 {1, &zero},
                                 0,
                                 quads_model->binding_count,
                                 &g_ui->secondary_ui_q.q);
        
        struct UI_PK
        {
            alignas(16) vector4_t color;
        } pk;
        pk.color = vector4_t(0.2f, 0.2f, 0.2f, 1.0f);

        if (g_ui->cpu_vertex_count)
        {
            command_buffer_draw(&g_ui->secondary_ui_q.q,
                                g_ui->cpu_vertex_count,
                                1,
                                0,
                                0);
        }

        auto *font_pipeline = g_pipeline_manager->get(g_ui->tx_pipeline);
        command_buffer_bind_pipeline(&font_pipeline->pipeline, &g_ui->secondary_ui_q.q);

        auto *font_tx_group = g_uniform_group_manager->get(g_ui->tx_group);
        command_buffer_bind_descriptor_sets(&font_pipeline->layout, {1, font_tx_group}, &g_ui->secondary_ui_q.q);
        
        auto *tx_quads_model = g_model_manager->get(g_ui->tx_quads_model);
        command_buffer_bind_vbos(tx_quads_model->raw_cache_for_rendering, {1, &zero}, 0, tx_quads_model->binding_count, &g_ui->secondary_ui_q.q);
        if (g_ui->cpu_tx_vertex_count)
        {
            command_buffer_draw(&g_ui->secondary_ui_q.q, g_ui->cpu_tx_vertex_count, 1, 0, 0);
        }
    }
    end_command_queue(&g_ui->secondary_ui_q);
}

void render_game_ui(framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue)
{
    // for_t the moment, this just executes one command buffer
    auto *vbo = g_gpu_buffer_manager->get(g_ui->ui_quads_vbo);
    if (g_ui->cpu_vertex_count)
    {
        update_gpu_buffer(vbo,
                          g_ui->cpu_vertex_pool,
                          sizeof(ui_state_t::gui_vertex_t) * g_ui->cpu_vertex_count,
                          0,
                          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                          VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                          &queue->q);
    }
    auto *tx_vbo = g_gpu_buffer_manager->get(g_ui->tx_quads_vbo);
    if (g_ui->cpu_tx_vertex_count)
    {
        update_gpu_buffer(tx_vbo,
                          g_ui->cpu_tx_vertex_pool,
                          sizeof(ui_state_t::textured_vertex_t) * g_ui->cpu_tx_vertex_count,
                          0,
                          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                          VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                          &queue->q);
    }
    queue->begin_render_pass(g_ui->ui_render_pass,
                             dst_framebuffer_hdl,
                             VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    command_buffer_execute_commands(&queue->q, {1, &g_ui->secondary_ui_q.q});
    queue->end_render_pass();

    g_ui->cpu_vertex_count = 0;
    g_ui->cpu_tx_vertex_count = 0;
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
    g_ui = &memory->user_interface_state.ui_state;
    dbg_ui_utils = &memory->user_interface_state.dbg_ui_utils;
}

static int32_t lua_quit(lua_State *state)
{
    request_quit();
    return(0);
}
