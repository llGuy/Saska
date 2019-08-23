#pragma once

#include "vulkan.hpp"

void initialize_game(input_state_t *input_state);

void destroy_game(gpu_t *gpu);

void game_tick(input_state_t *input_state, float32_t dt);

gpu_command_queue_pool_t *get_global_command_pool(void);


