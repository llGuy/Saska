#pragma once

#include "gui_box.hpp"

struct font_character_t {
    char character_value;
    vector2_t uvs_base;
    vector2_t uvs_size;
    vector2_t display_size;
    vector2_t offset;
    float32_t advance;
};

struct font_t {
    uint32_t char_count;
    image_handle_t font_img;

    font_character_t font_characters[126];
};

typedef int32_t font_handle_t;

struct ui_text_t {
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

    void initialize(ui_box_t *box,
                    font_t *font,
                    font_stream_box_relative_to_t relative_to,
                    float32_t px_x_start,
                    float32_t px_y_start,
                    uint32_t chars_per_line,
                    float32_t line_height);

    void draw_char(char character, uint32_t color);
    void draw_string(const char *string, uint32_t color);
    void null_terminate(void);
};


struct ui_input_text_t {
    uint32_t cursor_position = 0;
    ui_text_t text;

    uint32_t text_color;
    
    uint32_t cursor_fade = 0;
    bool fade_in_or_out = 0;

    // If enter was pressed
    void input(raw_input_t *raw_input);
};



font_handle_t add_font(const constant_string_t &font_name);
font_t *get_font(const constant_string_t &name);

font_t *load_font(const constant_string_t &font_name, const char *fnt_file, const char *png_file);
