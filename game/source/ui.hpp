#pragma once

#include "vulkan.hpp"
#include "graphics.hpp"


#include "math.hpp"
#include "fonts.hpp"
#include "gui_box.hpp"

struct console_t
{
    bool render_console = false;
    bool receive_input;

    ui_box_t back_box;
    
    ui_text_t console_input;
    char input_characters[60] = {};
    uint32_t input_character_count = 0;

    static constexpr float32_t BLINK_SPEED = 2.0f;
    enum fade_t { FADE_IN = false, FADE_OUT = true };
    bool fade_in_or_out = FADE_OUT;
    uint32_t cursor_position = 0;
    uint32_t cursor_color;
    int32_t cursor_fade;
    
    ui_text_t console_output;

    uint32_t input_color = 0xFFFFBBFF;
    uint32_t output_color = 0xBBFFFFFF;
};

struct crosshair_t
{
    image2d_t crosshair_image;
    uniform_group_t crosshair_group;

    uint32_t selected_crosshair;

    ui_box_t crosshair_box;

    vector2_t uvs[4];
    
    // Crosshair image is 8x8
    void get_uvs_for_crosshair(void)
    {
        vector2_t *list = uvs;
        
        // Starting coordinate
        vector2_t starting_coord = convert_1d_to_2d_coord(selected_crosshair, 8) / 8.0f;

        float32_t unit = (1.0f / 8.0f);
        
        list[0] = vector2_t(starting_coord.x, starting_coord.y + unit);
        list[1] = vector2_t(starting_coord.x, starting_coord.y);
        list[2] = vector2_t(starting_coord.x + unit, starting_coord.y + unit);
        list[3] = vector2_t(starting_coord.x + unit, starting_coord.y);
    }
};

struct user_interface_t
{
    console_t console;

    // in-game
    crosshair_t crosshair;
};

void initialize_game_ui(gpu_command_queue_pool_t *qpool, uniform_pool_t *uniform_pool, const resolution_t &);

void update_game_ui(framebuffer_handle_t dst_framebuffer_hdl, raw_input_t *raw_input, enum element_focus_t focus);

void render_game_ui(framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue);

void load_font(const constant_string_t &font_name, const char *fmt_file);

bool console_is_receiving_input(void);

void console_clear(void);
void console_out_i(const char *string);
void console_out_i(const vector3_t &v3);
void console_out_i(const vector2_t &v2);
void console_out_i(float32_t f);
void console_out_i(int32_t f);

template <typename ...T> void console_out(T &&...t)
{
    char dummy[] = { 0, (console_out_i(t), 0)... };
}

void set_console_for_focus(void);
void remove_console_for_focus(void);

void console_out_color_override(const char *string, uint32_t color);

uint32_t vec4_color_to_ui32b(const vector4_t &color);

void initialize_ui_translation_unit(struct game_memory_t *memory);



struct gui_colored_vertex_t
{
    vector2_t position;
    uint32_t color;
};

struct gui_textured_vertex_t
{
    vector2_t position;
    vector2_t uvs;
    uint32_t color;
};


// Pushes the text to the textured quad vertex list
// NOTE: When pushing text for rendering, make sure that the current vertex section of the gui vertex render list is for the font
void push_text_to_render(ui_text_t *text, const resolution_t &resolution);

