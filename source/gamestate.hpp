#pragma once

#include "utility.hpp"
#include "graphics.hpp"
#include "player.hpp"

void initialize_gamestate(struct raw_input_t *raw_input);
void populate_gamestate(struct game_state_initialize_packet_t *packet, struct raw_input_t *raw_input);
void deinitialize_gamestate(void);

void fill_game_state_initialize_packet(struct game_state_initialize_packet_t *packet, player_handle_t new_client_index);

void tick_gamestate(struct game_input_t *game_input, float32_t dt, gpu_command_queue_t *queue, enum application_type_t type, enum element_focus_t focus);

uint64_t *get_current_tick(void);
