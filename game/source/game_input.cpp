#include "game_input.hpp"



// Settings
struct action_bound_mouse_keyboard_button_t
{
    // Keyboard/Mouse button
    button_type_t bound_button;
};

struct action_bound_gamepad_button_t
{
    gamepad_button_type_t bound_button;
};



// Global
static action_bound_mouse_keyboard_button_t bound_key_mouse_buttons[game_input_action_type_t::INVALID_ACTION];
static action_bound_gamepad_button_t bound_gamepad_buttons[game_input_action_type_t::INVALID_ACTION];



void initialize_game_input_settings(void)
{
    bound_key_mouse_buttons[OK].bound_button = button_type_t::ENTER;
    bound_key_mouse_buttons[CANCEL].bound_button = button_type_t::ESCAPE;
    bound_key_mouse_buttons[MENU].bound_button = button_type_t::ESCAPE;

    bound_key_mouse_buttons[MOVE_FORWARD].bound_button = button_type_t::W;
    bound_key_mouse_buttons[MOVE_LEFT].bound_button = button_type_t::A;
    bound_key_mouse_buttons[MOVE_BACK].bound_button = button_type_t::S;
    bound_key_mouse_buttons[MOVE_RIGHT].bound_button = button_type_t::D;

    bound_key_mouse_buttons[LOOK_UP].bound_button = button_type_t::MOUSE_MOVE_UP;
    bound_key_mouse_buttons[LOOK_LEFT].bound_button = button_type_t::MOUSE_MOVE_LEFT;
    bound_key_mouse_buttons[LOOK_DOWN].bound_button = button_type_t::MOUSE_MOVE_DOWN;
    bound_key_mouse_buttons[LOOK_RIGHT].bound_button = button_type_t::MOUSE_MOVE_RIGHT;

    bound_key_mouse_buttons[TRIGGER1].bound_button = button_type_t::MOUSE_LEFT;
    bound_key_mouse_buttons[TRIGGER2].bound_button = button_type_t::MOUSE_RIGHT;
    bound_key_mouse_buttons[TRIGGER3].bound_button = button_type_t::R;
    bound_key_mouse_buttons[TRIGGER4].bound_button = button_type_t::SPACE;
    bound_key_mouse_buttons[TRIGGER5].bound_button = button_type_t::E;
    bound_key_mouse_buttons[TRIGGER6].bound_button = button_type_t::LEFT_SHIFT;



    bound_gamepad_buttons[OK].bound_button = gamepad_button_type_t::CONTROLLER_A;
    bound_gamepad_buttons[CANCEL].bound_button = gamepad_button_type_t::CONTROLLER_B;
    bound_gamepad_buttons[MENU].bound_button = gamepad_button_type_t::BACK;

    bound_gamepad_buttons[MOVE_FORWARD].bound_button = gamepad_button_type_t::LTHUMB_MOVE_UP;
    bound_gamepad_buttons[MOVE_LEFT].bound_button = gamepad_button_type_t::LTHUMB_MOVE_LEFT;
    bound_gamepad_buttons[MOVE_BACK].bound_button = gamepad_button_type_t::LTHUMB_MOVE_DOWN;
    bound_gamepad_buttons[MOVE_RIGHT].bound_button = gamepad_button_type_t::LTHUMB_MOVE_RIGHT;

    bound_gamepad_buttons[LOOK_UP].bound_button = gamepad_button_type_t::RTHUMB_MOVE_UP;
    bound_gamepad_buttons[LOOK_LEFT].bound_button = gamepad_button_type_t::RTHUMB_MOVE_LEFT;
    bound_gamepad_buttons[LOOK_DOWN].bound_button = gamepad_button_type_t::RTHUMB_MOVE_DOWN;
    bound_gamepad_buttons[LOOK_RIGHT].bound_button = gamepad_button_type_t::RTHUMB_MOVE_RIGHT;

    bound_gamepad_buttons[TRIGGER1].bound_button = gamepad_button_type_t::RIGHT_SHOULDER;
    bound_gamepad_buttons[TRIGGER2].bound_button = gamepad_button_type_t::LEFT_TRIGGER;
    bound_gamepad_buttons[TRIGGER3].bound_button = gamepad_button_type_t::CONTROLLER_Y;
    bound_gamepad_buttons[TRIGGER4].bound_button = gamepad_button_type_t::CONTROLLER_B;
    bound_gamepad_buttons[TRIGGER5].bound_button = gamepad_button_type_t::CONTROLLER_X;
    bound_gamepad_buttons[TRIGGER6].bound_button = gamepad_button_type_t::CONTROLLER_A;
}


static void set_button_action_state(uint32_t action, raw_input_t *raw_input, game_input_t *dst, float32_t dt)
{
    button_input_t *raw_key_mouse_input = &raw_input->buttons[bound_key_mouse_buttons[action].bound_button];
    gamepad_button_input_t *raw_gamepad_input = &raw_input->gamepad_buttons[bound_gamepad_buttons[action].bound_button];

    dst->actions[action].state = raw_key_mouse_input->state;
    dst->actions[action].down_amount = raw_key_mouse_input->down_amount;
    dst->actions[action].value = raw_key_mouse_input->value;
    
    if (raw_gamepad_input->state != button_state_t::NOT_DOWN)
    {
        dst->actions[action].state = raw_gamepad_input->state;
        dst->actions[action].down_amount = raw_gamepad_input->down_amount;
        dst->actions[action].value = raw_gamepad_input->value;
    }
}


void translate_raw_to_game_input(raw_input_t *raw_input, game_input_t *dst, float32_t dt)
{
    // For buttons
    for (uint32_t action = 0; action < game_input_action_type_t::INVALID_ACTION; ++action)
    {
        set_button_action_state(action, raw_input, dst, dt);
    }
}
