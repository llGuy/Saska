#pragma once

#include "utility.hpp"

struct camera_t
{
    vector2_t current_mouse_position;
    vector3_t ws_position;
    vector3_t ws_direction;
    vector3_t ws_up;

    float32_t fov, current_fov;
    float32_t aspect_ratio;
    float32_t near_distance;
    float32_t far_distance;

    matrix4_t projection_matrix;
    matrix4_t view_matrix;


    // TODO: Need to take in the game settings
    void initialize(float32_t window_width, float32_t window_height, float32_t fov, float32_t near_distance, float32_t far_distance, const vector2_t &mouse_position);

    void update_projection_matrix(void);
};
