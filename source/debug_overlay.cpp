#include "fonts.hpp"
#include "ui.hpp"
#include "gui_render_list.hpp"
#include "deferred_renderer.hpp"
#include "debug_overlay.hpp"


struct overlay_t {
    ui_box_t overlay;
    font_t *debug_font;

    int32_t fps;
    ui_text_t fps_counter;

    bool display = 0;
};

static overlay_t debug_overlay;


void initialize_debug_overlay() {
    debug_overlay.debug_font = get_font("liberation_mono.font"_hash);

    debug_overlay.overlay.initialize(LEFT_UP, 0.8f,
                                     ui_vector2_t(0.05f, -0.05f),
                                     ui_vector2_t(0.4f, 0.4f),
                                     nullptr,
                                     0x16161636,
                                     backbuffer_resolution());

    debug_overlay.fps_counter.initialize(&debug_overlay.overlay,
                                         debug_overlay.debug_font,
                                         ui_text_t::font_stream_box_relative_to_t::TOP,
                                         0.4f,
                                         0.5f,
                                         18,
                                         1.5f);
}


void render_debug_overlay(gui_textured_vertex_render_list_t *textured_render_list) {
    if (debug_overlay.display) {
        textured_render_list->mark_section(debug_font_uniform());
        push_box_to_render(&debug_overlay.overlay);
        push_text_to_render(&debug_overlay.fps_counter, backbuffer_resolution());
    }
}


void handle_debug_overlay_input(raw_input_t *raw_input) {
    if (raw_input->buttons[button_type_t::F3].state != button_state_t::NOT_DOWN) {
        debug_overlay.display ^= 1;
    }

    static float32_t fps_update_elapsed = 0.0f;
    if (raw_input->dt > 0) {
        fps_update_elapsed += raw_input->dt;

        if (fps_update_elapsed > 0.05f) {
            fps_update_elapsed = 0.0f;
            debug_overlay.fps = (int32_t)(1.0f / raw_input->dt);
            char buffer[4] = {};
            sprintf_s(buffer, "%d", debug_overlay.fps);
            debug_overlay.fps_counter.char_count = 0;
            debug_overlay.fps_counter.draw_string(buffer, 0xFFFFFFFF);
        }
    }
}
