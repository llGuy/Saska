#pragma once

#include "gui_math.hpp"

struct ui_box_t
{
    ui_box_t *parent {nullptr};
    relative_to_t relative_to;
    ui_vector2_t relative_position;
    ui_vector2_t gls_position;
    ui_vector2_t px_position;
    ui_vector2_t gls_max_values;
    ui_vector2_t px_current_size;
    ui_vector2_t gls_current_size;
    ui_vector2_t gls_relative_size;
    float32_t aspect_ratio;
    uint32_t color;

    void initialize(relative_to_t relative_to, float32_t aspect_ratio,
                    ui_vector2_t position /* coord_t space agnostic */,
                    ui_vector2_t gls_max_values /* max_t X and Y size */,
                    ui_box_t *parent,
                    const uint32_t &color,
                    resolution_t backbuffer_resolution = {});
    
    void update_size(const resolution_t &backbuffer_resolution);
    void update_position(const resolution_t &backbuffer_resolution);
};
