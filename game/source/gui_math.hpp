#pragma once

#include "utility.hpp"
#include "graphics.hpp"

enum coordinate_type_t { PIXEL, GLSL };

struct ui_vector2_t
{
    union
    {
        struct {int32_t ix, iy;};
        struct {float32_t fx, fy;};
    };
    coordinate_type_t type;

    ui_vector2_t(void) = default;
    ui_vector2_t(float32_t x, float32_t y) : fx(x), fy(y), type(GLSL) {}
    ui_vector2_t(int32_t x, int32_t y) : ix(x), iy(y), type(PIXEL) {}
    ui_vector2_t(const ivector2_t &iv) : ix(iv.x), iy(iv.y) {}
    
    inline vector2_t to_fvec2(void) const
    {
        return vector2_t(fx, fy);
    }

    inline ivector2_t to_ivec2(void) const
    {
        return ivector2_t(ix, iy);
    }
};

enum relative_to_t { LEFT_DOWN, LEFT_UP, CENTER, RIGHT_DOWN, RIGHT_UP };


// Math functions
inline vector2_t convert_glsl_to_normalized(const vector2_t &position)
{
    return(position * 2.0f - 1.0f);
}


inline ui_vector2_t glsl_to_pixel_coord(const ui_vector2_t &position, const resolution_t &resolution)
{
    ui_vector2_t ret((int32_t)(position.fx * (float32_t)resolution.width), (int32_t)(position.fy * (float32_t)resolution.height));
    return(ret);
}


inline ui_vector2_t pixel_to_glsl_coord(const ui_vector2_t &position, const resolution_t &resolution)
{
    ui_vector2_t ret((float32_t)position.ix / (float32_t)resolution.width,
                     (float32_t)position.iy / (float32_t)resolution.height);
    return(ret);
}


inline uint32_t vec4_color_to_ui32b(const vector4_t &color)
{
    float32_t xf = color.x * 255.0f;
    float32_t yf = color.y * 255.0f;
    float32_t zf = color.z * 255.0f;
    float32_t wf = color.w * 255.0f;
    uint32_t xui = (uint32_t)xf;
    uint32_t yui = (uint32_t)yf;
    uint32_t zui = (uint32_t)zf;
    uint32_t wui = (uint32_t)wf;
    return (xui << 24) | (yui << 16) | (zui << 8) | wui;
}



struct gui_colored_vertex_t
{
    vector2_t position;
    uint32_t color;
};

struct gui_textured_vertex_t
{
    vector2_t position;
    vector2_t uvs;
    uint32_t color;
};
