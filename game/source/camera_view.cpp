/*#include "camera_view.hpp"


struct camera_transforms_data_t
{
    alignas(16) matrix4_t view_matrix;
    alignas(16) matrix4_t projection_matrix;
    alignas(16) matrix4_t shadow_projection_matrix;
    alignas(16) matrix4_t shadow_view_matrix;
    
    alignas(16) vector4_t debug_vector;
};


// Global
static uint32_t camera_count = 0;
static camera_t cameras[10] = {};
static camera_handle_t camera_bound_to_3d_output = -1;
static camera_t spectator_camera = {};
static gpu_buffer_handle_t camera_transforms_ubos;
static uniform_group_handle_t camera_transforms_uniform_groups;
static uint32_t ubo_count;


void initialize_camera_view(raw_input_t *raw_input)
{
    uint32_t swapchain_image_count = get_swapchain_image_count();
    uniform_layout_handle_t ubo_layout_hdl = g_uniform_layout_manager->add("uniform_layout.camera_transforms_ubo"_hash, swapchain_image_count);
    auto *ubo_layout_ptr = g_uniform_layout_manager->get(ubo_layout_hdl);
    {
        uniform_layout_info_t blueprint = {};
        blueprint.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        *ubo_layout_ptr = make_uniform_layout(&blueprint);
    }

    camera_transforms_ubos = g_gpu_buffer_manager->add("gpu_buffer.camera_transforms_ubos"_hash, swapchain_image_count);
    auto *camera_ubos = g_gpu_buffer_manager->get(camera_transforms_ubos);
    {
        uint32_t uniform_buffer_count = swapchain_image_count;

        ubo_count = uniform_buffer_count;
	
        VkDeviceSize buffer_size = sizeof(camera_transform_uniform_data_t);

        for (uint32_t i = 0; i < uniform_buffer_count; ++i)
        {
            init_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &camera_ubos[i]);
        }
    }

    camera_transforms_uniform_groups = g_uniform_group_manager->add("uniform_group.camera_transforms_ubo"_hash, swapchain_image_count);
    auto *transforms = g_uniform_group_manager->get(camera_transforms_uniform_groups);
    {
        uniform_layout_handle_t layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        auto *layout_ptr = g_uniform_layout_manager->get(layout_hdl);
        for (uint32_t i = 0; i < swapchain_image_count; ++i)
        {
            transforms[i] = make_uniform_group(layout_ptr, g_uniform_pool);
            update_uniform_group(&transforms[i], update_binding_t{BUFFER, &camera_ubos[i], 0});
        }
    }

    spectator_camera.set_default((float32_t)get_backbuffer_resolution().width, (float32_t)get_backbuffer_resolution().height, 0.0f, 0.0f);
    
    spectator_camera.compute_projection();
    spectator_camera.v_m = matrix4_t(1.0f);
}

void deinitialize_camera_view(void)
{
    camera_count = 0;
    camera_bound_to_3d_output = -1;
}

camera_handle_t add_camera(raw_input_t *raw_input, uint32_t window_width, uint32_t window_height)
{
    uint32_t index = camera_count;
    cameras[index].set_default(window_width, window_height, raw_input->cursor_pos_x, raw_input->cursor_pos_y);
    ++camera_count;
    return(index);
}

void remove_all_cameras(void)
{
    camera_count = 0;
}

void sync_gpu_with_camera_view(uint32_t image_index)
{
    camera_t *camera = get_camera_bound_to_3d_output();

    update_shadows(150.0f, 1.0f, camera->fov, camera->asp, camera->p, camera->d, camera->u, &g_lighting->shadows.shadow_boxes[0]);

    shadow_matrices_t shadow_data = get_shadow_matrices();

    camera_transform_uniform_data_t transform_data = {};
    matrix4_t projection_matrix = camera->p_m;
    projection_matrix[1][1] *= -1.0f;
    make_camera_transform_uniform_data(&transform_data, camera->v_m, projection_matrix, shadow_data.light_view_matrix, shadow_data.projection_matrix, vector4_t(1.0f, 0.0f, 0.0f, 1.0f));
    
    gpu_buffer_t &current_ubo = *g_gpu_buffer_manager->get(camera_transforms_ubos + image_index);

    auto map = current_ubo.construct_map();
    map.begin();
    map.fill(memory_byte_buffer_t{sizeof(camera_transform_uniform_data_t), &transform_data});
    map.end();
    }

camera_t *get_camera(camera_handle_t handle)
{
    return(&cameras[handle]);
}

camera_t *get_camera_bound_to_3d_output(void)
{
    switch (camera_bound_to_3d_output)
    {
    case -1: return &spectator_camera;
    default: return &cameras[camera_bound_to_3d_output];
    }
}

void bind_camera_to_3d_scene_output(camera_handle_t handle)
{
    camera_bound_to_3d_output = handle;
}

memory_buffer_view_t<uniform_group_t> get_camera_transform_uniform_groups(void)
{
    return {ubo_count, g_uniform_group_manager->get(camera_transforms_uniform_groups)};
}
*/
