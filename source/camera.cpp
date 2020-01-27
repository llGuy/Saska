#include "math.hpp"
#include "camera.hpp"

void camera_t::initialize(float32_t window_width, float32_t window_height, float32_t fov, float32_t inear_distance, float32_t ifar_distance, const vector2_t &mouse_position)
{
    current_mouse_position = vector2_t(mouse_position.x, mouse_position.y);
    ws_position = vector3_t(50.0f, 10.0f, 280.0f);
    ws_direction = vector3_t(+1, 0.0f, +1);
    ws_up = vector3_t(0, 1, 0);

    fov = current_fov = glm::radians(fov);
    aspect_ratio = window_width / window_height;
    near_distance = near_distance;
    far_distance = far_distance;
}

void camera_t::update_projection_matrix(void)
{
    projection_matrix = glm::perspective(current_fov, aspect_ratio, near_distance, far_distance);
}
