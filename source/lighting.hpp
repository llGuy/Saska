#pragma once

#include "utility.hpp"
#include "graphics.hpp"

struct shadow_box_t
{
    matrix4_t light_view_matrix;
    matrix4_t projection_matrix;
    matrix4_t inverse_light_view;

    vector4_t ls_corners[8];
    
    union
    {
        struct {float32_t x_min, x_max, y_min, y_max, z_min, z_max;};
        float32_t corner_values[6];
    };
};

struct shadow_display_t
{
    uint32_t shadowmap_w, shadowmap_h;
    uniform_group_t texture;
};

struct sun_t
{
    vector3_t ws_position;
    vector3_t color;

    vector2_t ss_light_pos;

    // Shadow data for each sun
    image_handle_t shadow_map;
    uniform_group_handle_t uniform;

    shadow_box_t shadow_boxes[4];
};

#define SHADOW_BOX_COUNT 4

struct shadow_matrices_t
{
    struct
    {
        matrix4_t projection_matrix;
        matrix4_t light_view_matrix;
        matrix4_t inverse_light_view;
    } boxes[SHADOW_BOX_COUNT] = {};

    float32_t far_planes[4];
};

void initialize_lighting();
// Update shadow boxes, etc...
void update_lighting();

void begin_shadow_offscreen(gpu_command_queue_t *queue);
void end_shadow_offscreen(gpu_command_queue_t *queue);

void render_sun(uniform_group_t *camera_transforms, gpu_command_queue_t *queue);

sun_t *get_sun();
shadow_display_t get_shadow_display();
shadow_matrices_t get_shadow_matrices();
