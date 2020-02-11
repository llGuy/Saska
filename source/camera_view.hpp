#pragma once

#include "graphics.hpp"
#include "raw_input.hpp"

struct camera_t
{
    bool captured = 0;

    vector2_t mp;
    vector3_t p; // position
    vector3_t d; // direction
    vector3_t u; // up

    float32_t fov, current_fov;
    float32_t asp; // aspect ratio
    float32_t n, f; // near and far planes

    vector4_t captured_frustum_corners[8]{};
    vector4_t captured_shadow_corners[8]{};

    matrix4_t p_m;
    matrix4_t v_m;

    void set_default(float32_t w, float32_t h, float32_t m_x, float32_t m_y)
    {
        mp = vector2_t(m_x, m_y);
        p = vector3_t(50.0f, 10.0f, 280.0f);
        d = vector3_t(+1, 0.0f, +1);
        u = vector3_t(0, 1, 0);

        fov = current_fov = glm::radians(80.0f);
        asp = w / h;
        n = 1.0f;
        f = 10000000.0f;
    }

    void compute_projection(void)
    {
        p_m = glm::perspective(current_fov, asp, n, f);
    }
};

typedef int32_t camera_handle_t;

enum { CAMERA_BOUND_TO_3D_OUTPUT = -1 };

camera_handle_t add_camera(raw_input_t *input, resolution_t resolution);
void remove_all_cameras();

void initialize_camera(camera_t *camera, float32_t fov, float32_t asp, float32_t nearp, float32_t farp);

void update_camera_transforms(gpu_command_queue_t *queue);

void initialize_cameras();
void deinitialize_cameras();

camera_t *get_camera(camera_handle_t handle);
camera_t *camera_bound_to_3d_output();
void bind_camera_to_3d_output(camera_handle_t handle);

gpu_buffer_t camera_transforms();
uniform_group_t camera_transforms_uniform();

bool is_in_spectator_mode(void);
void update_spectator_camera(const matrix4_t &view_matrix);