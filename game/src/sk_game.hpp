#pragma once

#include "platform.hpp"
#include "raw_input.hpp"


enum application_type_t { WINDOW_APPLICATION_MODE, CONSOLE_APPLICATION_MODE };
enum element_focus_t { WORLD_3D_ELEMENT_FOCUS, UI_ELEMENT_CONSOLE, UI_ELEMENT_MENU };


void initialize_sk_game(display_window_information_t window_info);
void tick(raw_input_t *raw_input, float32_t dt);
void destroy_sk_game(void);
