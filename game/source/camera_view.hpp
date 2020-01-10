#pragma once

#include "graphics.hpp"
#include "raw_input.hpp"

typedef int32_t camera_handle_t;

void initialize_camera_view(raw_input_t *raw_input);
void deinitialize_camera_view(void);

camera_handle_t add_camera(raw_input_t *raw_input, uint32_t window_width, uint32_t window_height);
void remove_all_cameras(void);

void sync_gpu_with_camera_view(uint32_t image_index);

camera_t *get_camera(camera_handle_t handle);
camera_t *get_camera_bound_to_3d_output(void);
void bind_camera_to_3d_scene_output(camera_handle_t handle);
memory_buffer_view_t<uniform_group_t> get_camera_transform_uniform_groups(void);
