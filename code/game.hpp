#pragma once

#include "vulkan.hpp"

void
initialize_game(input_state_t *input_state);

void
destroy_game(gpu_t *gpu);

void
update_game(input_state_t *input_state, float32_t dt);


