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

struct user_interface_t
{
    console_t console;
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





// Pushes the text to the textured quad vertex list
// TODO: MAKE SURE THAT THESE ARE PART OF THE RENDER LIST CLASSES
void push_text_to_render(ui_text_t *text, const resolution_t &resolution);
void push_box_to_render(ui_box_t *box);

