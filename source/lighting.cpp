#include <math.h>
#include "lighting.hpp"
#include "atmosphere.hpp"
#include "camera_view.hpp"
#include "deferred_renderer.hpp"


#undef far
#undef near

struct lights_t
{
    sun_t suns[2];

    pipeline_handle_t sun_ppln;
    image2d_t sun_texture;
    uniform_group_t sun_group;
};

static lights_t lights;

static void s_lights_init()
{
    lights.suns[0].ws_position = vector3_t(10.0000001f, 10.0000000001f, 10.00000001f);
    lights.suns[0].color = vector3_t(0.18867780, 0.5784429, 0.6916065);
    
    lights.suns[1].ws_position = -vector3_t(30.0000001f, -10.0000000001f, 10.00000001f);
    lights.suns[1].color = vector3_t(0.18867780, 0.5784429, 0.6916065);

    lights.sun_ppln = g_pipeline_manager->add("pipeline.sun"_hash);
    auto *sun_ppln = g_pipeline_manager->get(lights.sun_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        resolution_t backbuffer_res = backbuffer_resolution();
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/sun.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/sun.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        uniform_layout_handle_t camera_transforms_layout_hdl = g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash);
        uniform_layout_handle_t single_tx_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash);
        shader_uniform_layouts_t layouts(camera_transforms_layout_hdl, single_tx_layout_hdl, g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
        shader_blend_states_t blending(blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::ADDITIVE_BLENDING);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, backbuffer_res, blending, nullptr,
                                    true, 0.0f, dynamic, deferred_render_pass(), 0, info);
        sun_ppln->info = info;
        make_graphics_pipeline(sun_ppln);
    }

    // Make sun texture
    {
        file_handle_t sun_png_handle = create_file("textures/sun/sun_test.PNG", file_type_flags_t::IMAGE | file_type_flags_t::ASSET);
        external_image_data_t image_data = read_image(sun_png_handle);

        make_texture(&lights.sun_texture, image_data.width, image_data.height,
                     VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_FILTER_LINEAR);
        transition_image_layout(&lights.sun_texture.image, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                get_global_command_pool());
        invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                     get_global_command_pool(),
                                                     &lights.sun_texture,
                                                     (uint32_t)image_data.width,
                                                     (uint32_t)image_data.height);
        transition_image_layout(&lights.sun_texture.image,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                get_global_command_pool());

        free_external_image_data(&image_data);
    }

    // Make sun uniform group
    uniform_layout_handle_t single_tx_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash);
    lights.sun_group = make_uniform_group(g_uniform_layout_manager->get(single_tx_layout_hdl), g_uniform_pool);
    {
        update_uniform_group(&lights.sun_group,
                             update_binding_t{ TEXTURE, &lights.sun_texture, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
}

struct shadow_target_t
{
    static constexpr uint32_t SHADOWMAP_W = 3000, SHADOWMAP_H = 3000;
        
    framebuffer_handle_t framebuffer;
    render_pass_handle_t render_pass;
    image_handle_t shadowmap;
    
    uniform_group_handle_t set;
};

static shadow_target_t target;

static void s_targets_init()
{
    // ---- Make shadow render pass ----
    target.render_pass = g_render_pass_manager->add("render_pass.shadow_render_pass"_hash);
    auto *shadow_pass = g_render_pass_manager->get(target.render_pass);
    {
        render_pass_attachment_t shadow_attachment { get_device_supported_depth_format(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        render_pass_subpass_t subpass = {};
        subpass.set_depth(render_pass_attachment_reference_t{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL });
        render_pass_dependency_t dependencies[2] = {};
        dependencies[0] = make_render_pass_dependency(VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                      0, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        dependencies[1] = make_render_pass_dependency(0, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                      VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        
        make_render_pass(shadow_pass, {1, &shadow_attachment}, {1, &subpass}, {2, dependencies});
    }

    // ---- Make shadow framebuffer ----
    target.framebuffer = g_framebuffer_manager->add("framebuffer.shadow_fbo"_hash);
    auto *shadow_fbo = g_framebuffer_manager->get(target.framebuffer);
    {
        image_handle_t shadowmap_handle = g_image_manager->add("image2D.shadow_map"_hash);
        auto *shadowmap_texture = g_image_manager->get(shadowmap_handle);
        make_framebuffer_attachment(shadowmap_texture, shadow_target_t::SHADOWMAP_W, shadow_target_t::SHADOWMAP_W, get_device_supported_depth_format(), 4, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 2);
        
        make_framebuffer(shadow_fbo, shadow_target_t::SHADOWMAP_W, shadow_target_t::SHADOWMAP_W, 4, shadow_pass, null_buffer<image2d_t>(), shadowmap_texture);
    }

    uniform_layout_t *sampler2D_layout_ptr = g_uniform_layout_manager->get("descriptor_set_layout.2D_sampler_layout"_hash);

    target.set = g_uniform_group_manager->add("descriptor_set.shadow_map_set"_hash);
    auto *shadow_map_ptr = g_uniform_group_manager->get(target.set);
    {
        image_handle_t shadowmap_handle = g_image_manager->get_handle("image2D.shadow_map"_hash);
        auto *shadowmap_texture = g_image_manager->get(shadowmap_handle);
        
        *shadow_map_ptr = make_uniform_group(sampler2D_layout_ptr, g_uniform_pool);
        update_uniform_group(shadow_map_ptr, update_binding_t{TEXTURE, shadowmap_texture, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }
}

#define SHADOW_BOX_COUNT 4

static shadow_box_t shadow_boxes[SHADOW_BOX_COUNT];
static float32_t far_planes[4];

static void s_shadow_boxes_init()
{
    vector3_t light_pos_normalized = glm::normalize(lights.suns[0].ws_position);
    light_pos_normalized.x *= -1.0f;
    light_pos_normalized.z *= -1.0f;

    matrix4_t light_view_matrix = glm::lookAt(vector3_t(0.0f), light_pos_normalized, vector3_t(0.0f, 1.0f, 0.0f));
    matrix4_t inverse_light_view_matrix = glm::inverse(light_view_matrix);
    
    for (uint32_t i = 0; i < SHADOW_BOX_COUNT; ++i)
    {
        shadow_boxes[i].light_view_matrix = light_view_matrix;
        shadow_boxes[i].inverse_light_view = inverse_light_view_matrix;
    }

    far_planes[0] = 20.0f;
    far_planes[1] = 50.0f;
    far_planes[2] = 150.0f;
    far_planes[3] = 500.0f;
}

void initialize_lighting()
{
    s_lights_init();
    s_shadow_boxes_init();
    s_targets_init();
}

// TODO: Make this update PSSM shadows
static void s_update_shadow_box(float32_t far, float32_t near, float32_t fov, float32_t aspect, const vector3_t &ws_p, const vector3_t &ws_d, const vector3_t &ws_up, shadow_box_t *shadow_box)
{
    float32_t far_width, near_width, far_height, near_height;
    
    far_width = 2.0f * far * tan(fov);
    near_width = 2.0f * near * tan(fov);
    far_height = far_width / aspect;
    near_height = near_width / aspect;

    vector3_t center_near = ws_p + ws_d * near;
    vector3_t center_far = ws_p + ws_d * far;
    
    vector3_t right_view_ax = glm::normalize(glm::cross(ws_d, ws_up));
    vector3_t up_view_ax = glm::normalize(glm::cross(ws_d, right_view_ax));

    float32_t far_width_half = far_width / 2.0f;
    float32_t near_width_half = near_width / 2.0f;
    float32_t far_height_half = far_height / 2.0f;
    float32_t near_height_half = near_height / 2.0f;

    // f = far, n = near, l = left, r = right, t = top, b = bottom
    enum ortho_corner_t : int32_t
    {
	flt, flb,
	frt, frb,
	nlt, nlb,
	nrt, nrb
    };    

    // Light space
    shadow_box->ls_corners[flt] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * far - right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    shadow_box->ls_corners[flb] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * far - right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    shadow_box->ls_corners[frt] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * far + right_view_ax * far_width_half + up_view_ax * far_height_half, 1.0f);
    shadow_box->ls_corners[frb] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * far + right_view_ax * far_width_half - up_view_ax * far_height_half, 1.0f);
    
    shadow_box->ls_corners[nlt] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * near - right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    shadow_box->ls_corners[nlb] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * near - right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);
    
    shadow_box->ls_corners[nrt] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * near + right_view_ax * near_width_half + up_view_ax * near_height_half, 1.0f);
    shadow_box->ls_corners[nrb] = shadow_box->light_view_matrix * vector4_t(ws_p + ws_d * near + right_view_ax * near_width_half - up_view_ax * near_height_half, 1.0f);

    float32_t x_min, x_max, y_min, y_max, z_min, z_max;

    x_min = x_max = shadow_box->ls_corners[0].x;
    y_min = y_max = shadow_box->ls_corners[0].y;
    z_min = z_max = shadow_box->ls_corners[0].z;

    for (uint32_t i = 1; i < 8; ++i)
    {
	if (x_min > shadow_box->ls_corners[i].x) x_min = shadow_box->ls_corners[i].x;
	if (x_max < shadow_box->ls_corners[i].x) x_max = shadow_box->ls_corners[i].x;

	if (y_min > shadow_box->ls_corners[i].y) y_min = shadow_box->ls_corners[i].y;
	if (y_max < shadow_box->ls_corners[i].y) y_max = shadow_box->ls_corners[i].y;

	if (z_min > shadow_box->ls_corners[i].z) z_min = shadow_box->ls_corners[i].z;
	if (z_max < shadow_box->ls_corners[i].z) z_max = shadow_box->ls_corners[i].z;
    }
    
    shadow_box->x_min = x_min = x_min;
    shadow_box->x_max = x_max = x_max;
    shadow_box->y_min = y_min = y_min;
    shadow_box->y_max = y_max = y_max;
    shadow_box->z_min = z_min = z_min;
    shadow_box->z_max = z_max = z_max;

    z_min = z_min - (z_max - z_min);

    shadow_box->projection_matrix = glm::transpose(matrix4_t(2.0f / (x_max - x_min), 0.0f, 0.0f, -(x_max + x_min) / (x_max - x_min),
                                                                     0.0f, 2.0f / (y_max - y_min), 0.0f, -(y_max + y_min) / (y_max - y_min),
                                                                     0.0f, 0.0f, 2.0f / (z_max - z_min), -(z_max + z_min) / (z_max - z_min),
                                                                     0.0f, 0.0f, 0.0f, 1.0f));
}

void update_lighting()
{
    camera_t *main_camera = camera_bound_to_3d_output();

    float32_t nears[] = {1.0f, far_planes[0], far_planes[1], far_planes[2] };
    
    for (uint32_t i = 0; i < SHADOW_BOX_COUNT; ++i)
    {
        s_update_shadow_box(far_planes[i], nears[i], main_camera->fov, main_camera->asp, main_camera->p, main_camera->d, main_camera->u, &shadow_boxes[i]);
    }
}

void begin_shadow_offscreen(gpu_command_queue_t *queue)
{
    queue->begin_render_pass(target.render_pass, target.framebuffer, VK_SUBPASS_CONTENTS_INLINE, init_clear_color_depth(1.0f, 0));
    
    VkViewport viewport = {};
    init_viewport(0, 0, shadow_target_t::SHADOWMAP_W, shadow_target_t::SHADOWMAP_H, 0.0f, 1.0f, &viewport);
    
    vkCmdSetViewport(queue->q, 0, 1, &viewport);
    vkCmdSetDepthBias(queue->q, 0.0f, 0.0f, 0.0f);

    // Render the world to the shadow map
}

void end_shadow_offscreen(gpu_command_queue_t *queue)
{
    command_buffer_end_render_pass(&queue->q);
}

void render_sun(uniform_group_t *camera_transforms, gpu_command_queue_t *queue)
{
    auto *sun_pipeline = g_pipeline_manager->get(lights.sun_ppln);
    command_buffer_bind_pipeline(&sun_pipeline->pipeline, &queue->q);

    uniform_group_t groups[3] = { *camera_transforms, lights.sun_group, *atmosphere_diffuse_uniform()};
    
    command_buffer_bind_descriptor_sets(&sun_pipeline->layout, {3, groups}, &queue->q);

    struct sun_push_constant_t
    {
	matrix4_t model_matrix;
        vector3_t ws_light_direction;
    } push_k;

    vector3_t light_pos = vector3_t(lights.suns[0].ws_position * 1000.0f);
    light_pos.x *= -1.0f;
    light_pos.z *= -1.0f;
    push_k.model_matrix = glm::translate(light_pos) * glm::scale(vector3_t(20.0f));

    push_k.ws_light_direction = -glm::normalize(lights.suns[0].ws_position);

    command_buffer_push_constant(&push_k, sizeof(push_k), 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sun_pipeline->layout, &queue->q);

    command_buffer_draw(&queue->q, 4, 1, 0, 0);

    matrix4_t model_matrix_transpose_rotation = push_k.model_matrix;

    camera_t *camera = camera_bound_to_3d_output();

    matrix4_t view_matrix_no_translation = camera->v_m;
    matrix3_t rotation_part = matrix3_t(view_matrix_no_translation);
    rotation_part = glm::transpose(rotation_part);

    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            model_matrix_transpose_rotation[i][j] = rotation_part[i][j];
        }
    }

    view_matrix_no_translation[3][0] = 0;
    view_matrix_no_translation[3][1] = 0;
    view_matrix_no_translation[3][2] = 0;

    vector4_t sun_ndc = camera->p_m * view_matrix_no_translation * push_k.model_matrix * vector4_t(vector3_t(0, 0, 0), 1.0);

    sun_ndc /= sun_ndc.w;
    lights.suns[0].ss_light_pos = (vector2_t(sun_ndc) + vector2_t(1.0f)) / 2.0f;
    lights.suns[0].ss_light_pos.y = 1.0f - lights.suns[0].ss_light_pos.y;
}

sun_t *get_sun()
{
    return &lights.suns[0];
}

shadow_display_t get_shadow_display()
{
    auto *texture = g_uniform_group_manager->get(target.set);
    shadow_display_t ret{shadow_target_t::SHADOWMAP_W, shadow_target_t::SHADOWMAP_H, *texture};
    return(ret);
}

shadow_matrices_t get_shadow_matrices()
{
    shadow_matrices_t ret;

    for (uint32_t i = 0; i < SHADOW_BOX_COUNT; ++i)
    {
        ret.boxes[i].projection_matrix = shadow_boxes[i].projection_matrix;
        ret.boxes[i].light_view_matrix = shadow_boxes[i].light_view_matrix;
        ret.boxes[i].inverse_light_view = shadow_boxes[i].inverse_light_view;
        ret.far_planes[i] = far_planes[i];
    }
    
    return(ret);
}
