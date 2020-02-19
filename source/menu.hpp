#pragma once


#include "game.hpp"

#include "gui_render_list.hpp"

#include "raw_input.hpp"


void initialize_menus(void);


void update_menus(raw_input_t *raw_input, element_focus_t focus, struct event_dispatcher_t *dispatcher);


void push_menus_to_render(gui_textured_vertex_render_list_t *textured_render_list,
                          gui_colored_vertex_render_list_t *colored_render_list,
                          element_focus_t focus,
                          float32_t dt);


void prompt_user_for_name(void);
