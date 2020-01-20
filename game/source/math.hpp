#pragma once

#include "utility.hpp"

// Linear interpolation (constant speed)
template <typename T> struct smooth_linear_interpolation_t
{
    bool in_animation;

    T current;
    T prev;
    T next;
    float32_t current_time;
    float32_t max_time;

    void animate(float32_t dt)
    {
        if (in_animation)
        {
            current_time += dt;
            float32_t progression = current_time / max_time;
        
            if (progression >= 1.0f)
            {
                in_animation = 0;
                current = next;
            }
            else
            {
                current = prev + progression * (next - prev);
            }
        }
    }
};


typedef bool (*smooth_exponential_interpolation_compare_float_t)(float32_t current, float32_t destination);
inline bool smooth_exponential_interpolation_compare_float(float32_t current, float32_t destination)
{
    return(abs(destination - current) < 0.001f);
}


// Starts fast, then slows down
template <typename T, typename Compare> struct smooth_exponential_interpolation_t
{
    bool in_animation;
    
    T destination;
    T current;
    float32_t speed = 1.0f;

    Compare compare;

    void animate(float32_t dt)
    {
        if (in_animation)
        {
            current += (destination - current) * dt * speed;
            if (compare(current, destination))
            {
                in_animation = 0;
                current = destination;
            }
        }
    }
};


// Some grid math
inline bool is_within_boundaries(const ivector3_t &coord, uint32_t edge_length)
{
    return(coord.x >= 0 && coord.x < edge_length &&
           coord.y >= 0 && coord.y < edge_length &&
           coord.z >= 0 && coord.z < edge_length);
}


inline int32_t convert_3d_to_1d_index(uint32_t x, uint32_t y, uint32_t z, uint32_t edge_length)
{
    if (is_within_boundaries(ivector3_t(x, y, z), edge_length))
    {
        return(z * (edge_length * edge_length) + y * edge_length + x);
    }
    else
    {
        return -1;
    }
}


struct voxel_coordinate_t {uint8_t x, y, z;};

inline voxel_coordinate_t convert_1d_to_3d_coord(uint16_t index, uint32_t edge_length)
{
    uint8_t x = index % edge_length;
    uint8_t y = ((index - x) % (edge_length * edge_length)) / (edge_length);
    uint8_t z = (index - x) / (edge_length * edge_length);

    return{x, y, z};
}

inline vector2_t convert_1d_to_2d_coord(uint32_t index, uint32_t edge_length)
{
    return vector2_t(index % edge_length, index / edge_length);
}



inline float32_t lerp(float32_t a, float32_t b, float32_t x)
{
    return((x - a) / (b - a));
}


inline vector3_t interpolate(const vector3_t &a, const vector3_t &b, float32_t x)
{
    return(a + x * (b - a));
}


inline float32_t interpolate(float32_t a, float32_t b, float32_t x)
{
    return(a + x * (b - a));
}


inline float32_t squared(float32_t f)
{
    return(f * f);
}


inline float32_t distance_squared(const vector3_t &dir)
{
    return glm::dot(dir, dir);
}


inline float32_t calculate_sphere_circumference(float32_t radius)
{
    return(2.0f * 3.1415f * radius);
}


enum matrix4_mul_vec3_with_translation_flag { WITH_TRANSLATION, WITHOUT_TRANSLATION, TRANSLATION_DONT_CARE };


struct movement_axes_t
{
    vector3_t right;
    vector3_t up;
    vector3_t forward;
};


inline movement_axes_t compute_movement_axes(const vector3_t &view_direction, const vector3_t &up)
{
    vector3_t right = glm::normalize(glm::cross(view_direction, up));
    vector3_t forward = glm::normalize(glm::cross(up, right));
    movement_axes_t axes = {right, up, forward};
    return(axes);
}
