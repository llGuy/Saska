#pragma once

#include "utility.hpp"

struct entity_t
{
    bool dead = false;
    
    vector3_t ws_up = vector3_t(0, 1, 0);
    
    vector3_t ws_position = vector3_t(0.0f);
    vector3_t ws_direction = vector3_t(0.0f);
    vector3_t ws_velocity = vector3_t(0.0f);
    vector3_t ws_input_velocity = vector3_t(0.0f);
    
    quaternion_t ws_rotation = quaternion_t(0.0f, 0.0f, 0.0f, 0.0f);
    
    vector3_t size = vector3_t(1.0f);
};
