#include "deferred_renderer.hpp"
#include "camera_view.hpp"

#undef near
#undef far


struct cameras_t {
    static constexpr uint32_t MAX_CAMERAS = 10;
    uint32_t camera_count = 0;
    camera_t cameras[MAX_CAMERAS] = {};
    camera_handle_t camera_bound_to_3d_output = -1;
    camera_t spectator_camera = {};

    gpu_buffer_handle_t camera_transforms_ubo;
    uniform_group_handle_t camera_transforms_uniform_group;
};


static cameras_t cameras;


static void s_cameras_init() {
    uniform_layout_handle_t ubo_layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
    auto *ubo_layout_ptr = g_uniform_layout_manager->get(ubo_layout_hdl);

    cameras.camera_transforms_ubo = g_gpu_buffer_manager->add("gpu_buffer.camera_transforms_ubos"_hash);
    auto *camera_ubo = g_gpu_buffer_manager->get(cameras.camera_transforms_ubo);
    {
        VkDeviceSize buffer_size = sizeof(camera_transform_uniform_data_t);

        init_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, camera_ubo);
    }

    cameras.camera_transforms_uniform_group = g_uniform_group_manager->add("uniform_group.camera_transforms_ubo"_hash);
    auto *transforms = g_uniform_group_manager->get(cameras.camera_transforms_uniform_group);
    {
        uniform_layout_handle_t layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        auto *layout_ptr = g_uniform_layout_manager->get(layout_hdl);

        *transforms = make_uniform_group(layout_ptr, g_uniform_pool);
        update_uniform_group(transforms, update_binding_t{ BUFFER, camera_ubo, 0 });
    }

    cameras.spectator_camera.set_default((float32_t)backbuffer_resolution().width, (float32_t)backbuffer_resolution().height, 0.0f, 0.0f);

    cameras.spectator_camera.compute_projection();
    cameras.spectator_camera.v_m = matrix4_t(1.0f);
}


void initialize_cameras() {
    s_cameras_init();
}


camera_handle_t add_camera(raw_input_t *input, resolution_t resolution) {
    uint32_t index = cameras.camera_count;
    cameras.cameras[index].set_default((float32_t)(resolution.width), (float32_t)(resolution.height), input->cursor_pos_x, input->cursor_pos_y);
    ++cameras.camera_count;
    return(index);
}


void remove_all_cameras() {
    cameras.camera_count = 0;
}


void initialize_camera(camera_t *camera, float32_t fov, float32_t asp, float32_t nearp, float32_t farp) {
    camera->fov = camera->current_fov = fov;
    camera->asp = asp;
    camera->n = nearp;
    camera->f = farp;
}


void update_camera_transforms(gpu_command_queue_t *queue) {
    camera_t *camera = camera_bound_to_3d_output();

    shadow_matrices_t shadow_data = get_shadow_matrices();

    camera_transform_uniform_data_t transform_data = {};
    matrix4_t projection_matrix = camera->p_m;
    projection_matrix[1][1] *= -1.0f;

    
    transform_data.view_matrix = camera->v_m;
    transform_data.projection_matrix = projection_matrix;

    for (uint32_t i = 0; i < SHADOW_BOX_COUNT; ++i) {
        transform_data.shadow_projection_matrix[i] = shadow_data.boxes[i].projection_matrix;
        transform_data.shadow_view_matrix[i] = shadow_data.boxes[i].light_view_matrix;
        transform_data.far_planes[i] = shadow_data.far_planes[i];
    }
    
    transform_data.debug_vector = vector4_t(1.0f, 0.0f, 0.0f, 1.0f);
    transform_data.light_direction = vector4_t(glm::normalize(-get_sun()->ws_position), 1.0f);
    transform_data.inverse_view_matrix = glm::inverse(camera->v_m);
    transform_data.view_direction = vector4_t(camera->d, 1.0f);
    
    gpu_buffer_t ubo = camera_transforms();

    update_gpu_buffer(&ubo, &transform_data, sizeof(camera_transform_uniform_data_t), 0, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, &queue->q);
}


void deinitialize_cameras() {
    cameras.camera_count = 0;
    cameras.camera_bound_to_3d_output = -1;
}


camera_t *get_camera(camera_handle_t handle) {
    return &cameras.cameras[handle];
}


camera_t *camera_bound_to_3d_output() {
    switch (cameras.camera_bound_to_3d_output) {
    case -1: return &cameras.spectator_camera;
    default: return &cameras.cameras[cameras.camera_bound_to_3d_output];
    }
}


void bind_camera_to_3d_output(camera_handle_t handle) {
    cameras.camera_bound_to_3d_output = handle;
}


gpu_buffer_t camera_transforms() {
    return *g_gpu_buffer_manager->get(cameras.camera_transforms_ubo);
}


uniform_group_t camera_transforms_uniform() {
    return *g_uniform_group_manager->get(cameras.camera_transforms_uniform_group);
}


bool is_in_spectator_mode(void) {
    return !cameras.camera_count;
}


void update_spectator_camera(const matrix4_t &view_matrix) {
    cameras.spectator_camera.v_m = view_matrix;
}
