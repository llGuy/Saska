#pragma once

#include "utility.hpp"
#include "graphics.hpp"

void initialize_gamestate(struct raw_input_t *raw_input);
void populate_gamestate(struct raw_input_t *raw_input);
void deinitialize_gamestate(void);
void tick_gamestate(struct game_input_t *game_input, float32_t dt, gpu_command_queue_t *queue, enum application_type_t type, enum element_focus_t focus);

