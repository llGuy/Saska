#pragma once

#include "utility.hpp"

void initialize_server(char *message_buffer);
void tick_server(struct raw_input_t *raw_input, float32_t dt);