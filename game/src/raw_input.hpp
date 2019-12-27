#pragma once

#include "utils.hpp"

enum button_state_t : uint8_t { NOT_DOWN = 0, INSTANT = 1, REPEAT = 2, RELEASE = 3 };

enum keyboard_button_type_t { A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
                              ZERO, ONE, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, UP, LEFT, DOWN, RIGHT,
                              SPACE, LEFT_SHIFT, LEFT_CONTROL, ENTER, BACKSPACE, ESCAPE, INVALID_KEY };

struct keyboard_button_t
{
    button_state_t state = button_state_t::NOT_DOWN;
    float32_t down_amount = 0.0f;
};

enum mouse_button_type_t { MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, INVALID_MOUSE_BUTTON };

struct mouse_button_t
{
    button_state_t state = button_state_t::NOT_DOWN;
    float32_t down_amount = 0.0f;
};

enum key_action_t { KEY_ACTION_DOWN, KEY_ACTION_UP };

#define MAX_PRESSED_CHARS_PER_FRAME 10

struct raw_input_t
{
    // TODO: Add gamepad (XInput)
    keyboard_button_t keyboard[keyboard_button_type_t::INVALID_KEY];
    mouse_button_t mouse_buttons[mouse_button_type_t::INVALID_MOUSE_BUTTON];

    uint32_t char_count;
    char char_stack[MAX_PRESSED_CHARS_PER_FRAME] = {};

    bool cursor_moved = 0;
    
    float32_t cursor_pos_x = 0.0f, cursor_pos_y = 0.0f;
    float32_t previous_cursor_pos_x = 0.0f, previous_cursor_pos_y = 0.0f;

    bool resized = 0;
    int32_t window_width, window_height; // Will be used in case of window resize

    vector2_t normalized_cursor_position;

    bool is_showing_cursor = 1;
    bool window_has_focus = 1;

    bool in_fullscreen = 0;
    bool toggled_fullscreen = 0;
};
