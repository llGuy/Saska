#include "player.hpp"

void player_t::initialize(player_create_info_t *info)
{
    
}


player_state_t player_t::create_player_state(void)
{
    player_state_t state = {};
    state.action_flags = action_flags;
    state.mouse_x_diff = camera.mouse_diff.x;
    state.mouse_y_diff = camera.mouse_diff.y;

    state.is_entering = is_entering;
    state.rolling_mode = rolling_mode;
    state.ws_position = ws_position;
    state.ws_direction = ws_direction;
    state.ws_velocity = ws_velocity;

    return(state);
}
