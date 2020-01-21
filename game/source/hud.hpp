#pragma once

#include "game.hpp"
#include "gui_render_list.hpp"

void initialize_hud(void);
void push_hud_to_render(gui_textured_vertex_render_list_t *render_list, element_focus_t focus);
     
