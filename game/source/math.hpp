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
