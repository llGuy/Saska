#pragma once

#include "vulkan.hpp"
#include "graphics.hpp"

struct dbg_ui_utils_t
{
    graphics_pipeline_t dbg_tx_quad_ppln;
    uniform_layout_t dbg_tx_ulayout;
};

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
    ui_vector2_t(const ivector2_t &iv) : ix(iv.x), iy(iv.y) {}
    
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

enum relative_to_t { LEFT_DOWN, LEFT_UP, CENTER, RIGHT_DOWN, RIGHT_UP };

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

struct ui_state_t
{
    struct gui_vertex_t
    {
        vector2_t position;
        uint32_t color;
    };
    persist_var constexpr uint32_t MAX_QUADS = 10;
    gui_vertex_t cpu_vertex_pool[ MAX_QUADS * 6 ];
    uint32_t cpu_vertex_count = 0;
    model_handle_t ui_quads_model;
    gpu_buffer_handle_t ui_quads_vbo;
    pipeline_handle_t ui_pipeline;

    struct textured_vertex_t
    {
        vector2_t position;
        vector2_t uvs;
        uint32_t color;
    };
    persist_var constexpr uint32_t MAX_TX_QUADS = 1000;
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
};

struct font_character_t
{
    char character_value;
    vector2_t uvs_base;
    vector2_t uvs_size;
    vector2_t display_size;
    vector2_t offset;
    float32_t advance;
};

struct font_t
{
    uint32_t char_count;
    image_handle_t font_img;

    font_character_t font_characters[126];
};

typedef int32_t font_handle_t;

struct ui_text_t
{
    ui_box_t *dst_box;
    font_t *font;
    
    // Max characters = 500
    uint32_t colors[500] = {};
    char characters[500] = {};
    uint32_t char_count = 0;

    enum font_stream_box_relative_to_t { TOP, BOTTOM /* add CENTER in the future */ };

    font_stream_box_relative_to_t relative_to;

    // Relative to xadvance
    float32_t x_start;
    float32_t y_start;

    uint32_t chars_per_line;

    // Relative to xadvance
    float32_t line_height;
};

struct console_t
{
    bool render_console = true;
    bool receive_input;

    ui_box_t back_box;
    
    ui_text_t console_input;
    char input_characters[60] = {};
    uint32_t input_character_count = 0;

    persist_var constexpr float32_t BLINK_SPEED = 2.0f;
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
    dbg_ui_utils_t dbg_ui_utils;
    ui_state_t ui_state;
    console_t console;
};

void initialize_game_ui(gpu_command_queue_pool_t *qpool, uniform_pool_t *uniform_pool, const resolution_t &);

void update_game_ui(framebuffer_handle_t dst_framebuffer_hdl, input_state_t *input_state, enum element_focus_t focus);

void render_game_ui(framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue);

void load_font(const constant_string_t &font_name, const char *fmt_file);

bool console_is_receiving_input(void);

void console_out(const char *string);
void set_console_for_focus(void);
void remove_console_for_focus(void);

void console_out_color_override(const char *string, uint32_t color);

uint32_t vec4_color_to_ui32b(const vector4_t &color);

void initialize_ui_translation_unit(struct game_memory_t *memory);
