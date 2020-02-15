#pragma once

#include "graphics.hpp"

void initialize_debug_overlay();

void render_debug_overlay(struct gui_textured_vertex_render_list_t *list);

void handle_debug_overlay_input(raw_input_t *raw_input);
