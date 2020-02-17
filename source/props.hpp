#pragma once

#include "graphics.hpp"

void initialize_props();

void update_props(float32_t dt);
void render_props(gpu_command_queue_t *queue);
