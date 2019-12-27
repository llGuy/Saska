#pragma once

#include "utility.hpp"

enum collision_primitive_type_t { CPT_FACE, CPT_EDGE, CPT_VERTEX };

struct collision_t
{
    collision_primitive_type_t primitive_type;

    uint8_t detected: 1;
    // Detected a collision but now "slid" into the air
    uint8_t is_currently_in_air: 1;
    uint8_t under_terrain: 1;

    // es_ = ellipsoid space
    float32_t es_distance_from_triangle;
    vector3_t es_velocity;
    vector3_t es_contact_point;
    vector3_t es_at;
    vector3_t es_normal;
    float32_t es_distance;
};


collision_t collide(const vector3_t &ws_center, const vector3_t &ws_size, const vector3_t &ws_velocity, uint32_t recurse_depth, collision_t previous_collision);





