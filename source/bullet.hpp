#pragma once

#include "player.hpp"
#include "entity.hpp"
#include "component.hpp"

struct bullet_create_info_t {
    uint32_t bullet_index;
    vector3_t ws_position;
    vector3_t ws_direction;
    quaternion_t ws_rotation;
    vector3_t ws_size;
    player_color_t color;
    bounce_physics_component_create_info_t bounce_info;
    rendering_component_create_info_t rendering_info;
};


struct bullet_t : entity_t {
    uint32_t bullet_index;
    uint32_t player_index;
    rendering_component_t rendering;
    bounce_physics_component_t bounce_physics;
    burnable_component_t burnable;

    void initialize(bullet_create_info_t *info);
};
