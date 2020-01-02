#pragma once

#include "utility.hpp"
#include "raw_input.hpp"

enum game_input_action_type_t { // Menu stuff
                               OK,
                               CANCEL,
                               MENU,
                               // Player stuff
                               MOVE_FORWARD,
                               MOVE_LEFT,
                               MOVE_BACK,
                               MOVE_RIGHT,
                               // Mouse as well
                               LOOK_UP,
                               LOOK_LEFT,
                               LOOK_DOWN,
                               LOOK_RIGHT,
                               TRIGGER1,
                               TRIGGER2,
                               TRIGGER3,
                               TRIGGER4,
                               TRIGGER5,
                               TRIGGER6,
                               // Invalid
                               INVALID_ACTION };

struct game_input_action_t
{
    button_state_t state;
    float32_t value;
    float32_t down_amount = 0.0f;
};

// To translate from raw_input_t to player_input_t
struct game_input_t
{
    game_input_action_t actions[game_input_action_type_t::INVALID_ACTION];
};

// TODO: Load from file
void initialize_game_input_settings(void);
void translate_raw_to_game_input(raw_input_t *raw_input, game_input_t *dst, float32_t dt);
