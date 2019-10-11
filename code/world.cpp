/* world.cpp */
// TODO: DEFER ALL GPU RELATED OPERATIONS!!!
// TODO: Get rid of most of the glm::length calls in the physics calculations (terrain collision stuff)
// TODO: Have a startup script so that you can reload the game

#include "ui.hpp"
#include "script.hpp"
#include "world.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>

#include "graphics.hpp"

#include "core.hpp"

#include "game.hpp"

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr float32_t PI = 3.14159265359f;

// To initialize with initialize translation unit function
global_var struct entities_t *g_entities;
global_var struct voxel_chunks_t *g_voxel_chunks;


enum matrix4_mul_vec3_with_translation_flag { WITH_TRANSLATION, WITHOUT_TRANSLATION, TRANSLATION_DONT_CARE };

// ******************************** Voxel code ********************************

// Initialize rendering data, stuff that should only be initialize at the beginning of the game running in general
internal_function void hard_initialize_chunks(void)
{        
    g_voxel_chunks->chunk_model.attribute_count = 1;
    g_voxel_chunks->chunk_model.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription));
    g_voxel_chunks->chunk_model.binding_count = 1;
    g_voxel_chunks->chunk_model.bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

    model_binding_t *binding = g_voxel_chunks->chunk_model.bindings;
    binding->begin_attributes_creation(g_voxel_chunks->chunk_model.attributes_buffer);

    // There is only one attribute for now
    binding->push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float32_t));

    binding->end_attributes_creation();

    g_voxel_chunks->chunk_pipeline = g_pipeline_manager->add("pipeline.chunk_model"_hash);
    graphics_pipeline_t *voxel_pipeline = g_pipeline_manager->get(g_voxel_chunks->chunk_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/voxel_point.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/voxel_point.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_POINT,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_voxel_chunks->chunk_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        voxel_pipeline->info = info;
        make_graphics_pipeline(voxel_pipeline);
    }

    g_voxel_chunks->gpu_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, get_global_command_pool());
}

void push_chunk_to_render_queue(voxel_chunk_t *chunk)
{
    g_voxel_chunks->gpu_queue.push_material(&chunk->push_k, sizeof(chunk->push_k), &chunk->gpu_mesh);
}

internal_function void initialize_chunk_vertices(voxel_chunk_t *chunk)
{
    uint32_t i = 0;
    
    for (uint32_t z = 0; z < VOXEL_CHUNK_EDGE_LENGTH; ++z)
    {
        for (uint32_t y = 0; y < VOXEL_CHUNK_EDGE_LENGTH; ++y)
        {
            for (uint32_t x = 0; x < VOXEL_CHUNK_EDGE_LENGTH; ++x)
            {
                chunk->mesh_vertices[i++] = vector3_t(x, y, z);
            }
        }
    }

    chunk->vertex_count = i;
}

void initialize_chunk(voxel_chunk_t *chunk, vector3_t chunk_position)
{
    memset(chunk->voxels, 0, sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);
    memset(chunk->mesh_vertices, 0, sizeof(vector3_t) * MAX_VERTICES_PER_VOXEL_CHUNK);
    uint32_t buffer_size = sizeof(vector3_t) * MAX_VERTICES_PER_VOXEL_CHUNK;

    
    initialize_chunk_vertices(chunk);

    uint32_t vertices_size = sizeof(vector3_t) * chunk->vertex_count;
    

    make_unmappable_gpu_buffer(&chunk->chunk_mesh_gpu_buffer, vertices_size, chunk->mesh_vertices, gpu_buffer_usage_t::VERTEX_BUFFER, get_global_command_pool());

    draw_indexed_data_t indexed_data = init_draw_indexed_data_default(1, chunk->vertex_count);
    model_index_data_t model_indexed_data;
    memory_buffer_view_t<VkBuffer> buffers{1, &chunk->chunk_mesh_gpu_buffer.buffer};
    
    chunk->gpu_mesh = initialize_mesh(buffers, &indexed_data, &g_voxel_chunks->chunk_model.index_data);

    chunk->push_k.model_matrix = glm::translate(chunk_position);
}

voxel_chunk_t **get_voxel_chunk(uint32_t index)
{
    return(&g_voxel_chunks->chunks[index]);
}



// ********************* Entity code ***************************

internal_function entity_t *get_main_entity(void)
{
    if (g_entities->main_entity == -1)
    {
        return nullptr;
    }
    else
    {
        return &g_entities->entity_list[g_entities->main_entity];
    }
}

internal_function void push_entity_to_queue(entity_t *e_ptr, mesh_t *mesh, gpu_material_submission_queue_t *queue)
{
    rendering_component_t *component = &g_entities->rendering_components[ e_ptr->components.rendering_component ];

    uniform_group_t *group = nullptr;
    
    if (e_ptr->components.animation_component >= 0)
    {
        struct animation_component_t *component = &g_entities->animation_components[ e_ptr->components.animation_component ];
        group = &component->animation_instance.group;
    }
    
    queue->push_material(&component->push_k,
			 sizeof(component->push_k),
                         mesh,
                         group);
}

internal_function void push_entity_to_animated_queue(entity_t *e)
{
    push_entity_to_queue(e, &g_entities->entity_mesh, &g_entities->entity_submission_queue);
}

internal_function void push_entity_to_rolling_queue(entity_t *e)
{
    rendering_component_t *component = &g_entities->rendering_components[ e->components.rendering_component ];

    uniform_group_t *group = nullptr;
    
    g_entities->rolling_entity_submission_queue.push_material(&component->push_k,
                                                                  sizeof(component->push_k),
                                                                  &g_entities->rolling_entity_mesh,
                                                                  group);
}

entity_t construct_entity(const constant_string_t &name, vector3_t gs_p, vector3_t ws_d, quaternion_t gs_r)
{
    entity_t e;
    //    e.is_group = is_group;
    e.ws_p = gs_p;
    e.ws_d = ws_d;
    e.ws_r = gs_r;
    e.id = name;
    return(e);
}

internal_function entity_t *get_entity(const constant_string_t &name)
{
    entity_handle_t v = *g_entities->name_map.get(name.hash);
    return(&g_entities->entity_list[v]);
}

entity_t *get_entity(entity_handle_t v)
{
    return(&g_entities->entity_list[v]);
}

void attach_camera_to_entity(entity_t *e, int32_t camera_index)
{
    
}

internal_function struct camera_component_t * add_camera_component(entity_t *e, uint32_t camera_index)
{
    e->components.camera_component = g_entities->camera_component_count++;
    camera_component_t *component = &g_entities->camera_components[ e->components.camera_component ];
    component->entity_index = e->index;
    component->camera = camera_index;

    return(component);
}

internal_function void update_camera_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->camera_component_count; ++i)
    {
        struct camera_component_t *component = &g_entities->camera_components[ i ];
        struct camera_t *camera = get_camera(component->camera);
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
        
        vector3_t camera_position = e->ws_p + vector3_t(0.0f, 1.0f, 0.0f);
        if (component->is_third_person)
        {
            vector3_t right = glm::cross(e->ws_d, vector3_t(0.0f, 1.0f, 0.0f));
            camera_position += right * 5.0f + -component->distance_from_player * e->ws_d;
        }
        
        camera->v_m = glm::lookAt(camera_position, e->ws_p + vector3_t(0.0f, 1.0f, 0.0f) + e->ws_d, up);

        // TODO: Don't need to calculate this every frame, just when parameters change
        camera->compute_projection();

        camera->p = camera_position;
        camera->d = e->ws_d;
        camera->u = up;
    }
}

internal_function struct rendering_component_t *add_rendering_component(entity_t *e)
{
    e->components.rendering_component = g_entities->rendering_component_count++;
    rendering_component_t *component = &g_entities->rendering_components[ e->components.rendering_component ];
    component->entity_index = e->index;
    component->push_k = {};

    return(component);
}

internal_function struct animation_component_t *add_animation_component(entity_t *e,
                                                                        uniform_layout_t *ubo_layout,
                                                                        skeleton_t *skeleton,
                                                                        animation_cycles_t *cycles,
                                                                        gpu_command_queue_pool_t *cmdpool)
{
    e->components.animation_component = g_entities->animation_component_count++;
    animation_component_t *component = &g_entities->animation_components[ e->components.animation_component ];
    component->entity_index = e->index;
    component->cycles = cycles;
    component->animation_instance = initialize_animated_instance(cmdpool,
                                                                 ubo_layout,
                                                                 skeleton,
                                                                 cycles);
    switch_to_cycle(&component->animation_instance, entity_t::animated_state_t::IDLE, 1);

    return(component);
}

internal_function void update_animation_component(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->animation_component_count; ++i)
    {
        struct animation_component_t *component = &g_entities->animation_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        entity_t::animated_state_t previous_state = e->animated_state;
        entity_t::animated_state_t new_state;
        
        uint32_t moving = 0;
        
        if (e->action_flags & (1 << action_flags_t::ACTION_FORWARD))
        {
            if (e->action_flags & (1 << action_flags_t::ACTION_RUN))
            {
                new_state = entity_t::animated_state_t::RUN; moving = 1;
            }
            else
            {
                new_state = entity_t::animated_state_t::WALK; moving = 1;
            }
        }
        if (e->action_flags & (1 << action_flags_t::ACTION_LEFT)); 
        if (e->action_flags & (1 << action_flags_t::ACTION_DOWN));
        if (e->action_flags & (1 << action_flags_t::ACTION_RIGHT));
        
        if (!moving)
        {
            new_state = entity_t::animated_state_t::IDLE;
        }

        if (e->is_sitting)
        {
            new_state = entity_t::animated_state_t::SITTING;
        }

        if (e->is_in_air)
        {
            new_state = entity_t::animated_state_t::HOVER;
        }

        if (e->is_sliding_not_rolling_mode)
        {
            new_state = entity_t::animated_state_t::SLIDING_NOT_ROLLING_MODE;
        }

        if (new_state != previous_state)
        {
            e->animated_state = new_state;
            switch_to_cycle(&component->animation_instance, new_state);
        }
        
        interpolate_skeleton_joints_into_instance(dt, &component->animation_instance);
    }
}

internal_function void update_animation_gpu_data(gpu_command_queue_t *queue)
{
    for (uint32_t i = 0; i < g_entities->animation_component_count; ++i)
    {
        struct animation_component_t *component = &g_entities->animation_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        update_animated_instance_ubo(queue, &component->animation_instance);
    }
}

internal_function void push_entity_to_animated_queue(entity_t *e);
internal_function void push_entity_to_rolling_queue(entity_t *e);

internal_function void update_rendering_component(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->rendering_component_count; ++i)
    {
        struct rendering_component_t *component = &g_entities->rendering_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        persist_var const matrix4_t CORRECTION_90 = glm::rotate(glm::radians(90.0f), vector3_t(0.0f, 1.0f, 0.0f));

        vector3_t view_dir = glm::normalize(e->ws_d);
        float32_t dir_x = view_dir.x;
        float32_t dir_z = view_dir.z;
        float32_t rotation_angle = atan2(dir_z, dir_x);

        matrix4_t rot_matrix = glm::rotate(-rotation_angle, vector3_t(0.0f, 1.0f, 0.0f));
        
        if (component->enabled)
        {
            component->push_k.ws_t = glm::translate(e->ws_p) * CORRECTION_90 * rot_matrix * e->rolling_rotation * glm::scale(e->size);
        }
        else
        {
            component->push_k.ws_t = matrix4_t(0.0f);
        }

        if (e->rolling_mode)
        {
            push_entity_to_rolling_queue(e);
        }
        else
        {
            push_entity_to_animated_queue(e);
        }
    }
}

uint32_t add_network_component(void)
{
    uint32_t component_index = g_entities->network_component_count++;
    return(component_index);
}

struct network_component_t *get_network_component(uint32_t index)
{
    return(&g_entities->network_components[index]);
}

internal_function struct physics_component_t *add_physics_component(entity_t *e, bool enabled)
{
    e->components.physics_component = g_entities->physics_component_count++;
    struct physics_component_t *component = &g_entities->physics_components[ e->components.physics_component ];
    component->entity_index = e->index;
    component->enabled = enabled;

    return(component);
}

internal_function void update_standing_entity_physics(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
}

internal_function void update_rolling_entity_physics(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
}

internal_function void update_not_physically_affected_entity(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
    vector3_t result_force = vector3_t(0.0f);

    vector3_t right = glm::normalize(glm::cross(e->ws_d, vector3_t(0.0f, 1.0f, 0.0f)));
    vector3_t forward = glm::normalize(glm::cross(vector3_t(0.0f, 1.0f, 0.0f), right));

    if (*action_flags & (1 << action_flags_t::ACTION_FORWARD)) result_force += forward;
    if (*action_flags & (1 << action_flags_t::ACTION_BACK)) result_force -= forward;
    if (*action_flags & (1 << action_flags_t::ACTION_RIGHT)) result_force += right;
    if (*action_flags & (1 << action_flags_t::ACTION_LEFT)) result_force -= right;
    if (*action_flags & (1 << action_flags_t::ACTION_UP)) result_force += vector3_t(0.0f, 1.0f, 0.0f);
    if (*action_flags & (1 << action_flags_t::ACTION_DOWN)) result_force -= vector3_t(0.0f, 1.0f, 0.0f);

    result_force *= 3.5f;

    e->ws_p += result_force * dt;
}

internal_function void update_physics_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->physics_component_count; ++i)
    {
        struct physics_component_t *component = &g_entities->physics_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];
        uint32_t *action_flags = &e->action_flags;

        if (component->enabled)
        {
            if (e->rolling_mode)
            {
                update_rolling_entity_physics(component, e, action_flags, dt);
            }
            else
            {
                update_standing_entity_physics(component, e, action_flags, dt);
            }
        }
        // Flying, not falling down towards earth
        else
        {
            update_not_physically_affected_entity(component, e, action_flags, dt);
        }
    }
}

internal_function entity_handle_t add_entity(const entity_t &e)

{
    entity_handle_t view;
    view = g_entities->entity_count;

    g_entities->name_map.insert(e.id.hash, view);
    
    g_entities->entity_list[g_entities->entity_count++] = e;

    auto e_ptr = get_entity(view);
    e_ptr->rolling_mode = 0;
    e_ptr->index = view;

    return(view);
}

internal_function void make_entity_instanced_renderable(model_handle_t model_handle, const constant_string_t &e_mtrl_name)
{
    // TODO(luc) : first need to add support for instance rendering in material renderers.
}

internal_function void update_entities(float32_t dt, application_type_t app_type)
{
    switch (app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
        update_physics_components(dt);
        update_camera_components(dt);

        update_rendering_component(dt);
        update_animation_component(dt);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            update_physics_components(dt);
        } break;
    }
}

void make_entity_main(entity_handle_t entity_handle, input_state_t *input_state)
{
    entity_t *entity = get_entity(entity_handle);

    camera_component_t *camera_component_ptr = add_camera_component(entity, add_camera(input_state, get_backbuffer_resolution()));
    camera_component_ptr->is_third_person = true;
        
    bind_camera_to_3d_scene_output(camera_component_ptr->camera);

    g_entities->main_entity = entity_handle;
}

void make_entity_renderable(entity_handle_t entity_handle, entity_color_t color)
{
    entity_t *entity_ptr = get_entity(entity_handle);
    
    rendering_component_t *entity_ptr_rendering = add_rendering_component(entity_ptr);
    animation_component_t *entity_animation = add_animation_component(entity_ptr,
                                                                      g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.joint_ubo"_hash)),
                                                                      &g_entities->entity_mesh_skeleton,
                                                                      &g_entities->entity_mesh_cycles,
                                                                      get_global_command_pool());

    persist_var vector4_t colors[entity_color_t::INVALID_COLOR] = { vector4_t(0.0f, 0.0f, 0.7f, 1.0f),
                                                                    vector4_t(0.7f, 0.0f, 0.0f, 1.0f),
                                                                    vector4_t(0.4f, 0.4f, 0.4f, 1.0f),
                                                                    vector4_t(0.1f, 0.1f, 0.1f, 1.0f),
                                                                    vector4_t(0.0f, 0.7f, 0.0f, 1.0f) };
        
    entity_ptr_rendering->push_k.color = colors[color];
    entity_ptr_rendering->push_k.roughness = 0.8f;
    entity_ptr_rendering->push_k.metalness = 0.6f;
}

internal_function void initialize_entities_graphics_data(VkCommandPool *cmdpool, input_state_t *input_state)
{
    g_entities->rolling_entity_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/icosphere.mesh_custom", cmdpool);
    g_entities->rolling_entity_model = make_mesh_attribute_and_binding_information(&g_entities->rolling_entity_mesh);
    g_entities->rolling_entity_model.index_data = g_entities->rolling_entity_mesh.index_data;
    
    g_entities->entity_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/spaceman.mesh_custom", cmdpool);
    g_entities->entity_model = make_mesh_attribute_and_binding_information(&g_entities->entity_mesh);
    g_entities->entity_model.index_data = g_entities->entity_mesh.index_data;
    g_entities->entity_mesh_skeleton = load_skeleton("models/spaceman_walk.skeleton_custom");
    g_entities->entity_mesh_cycles = load_animations("models/spaceman.animations_custom");

    uniform_layout_handle_t animation_layout_hdl = g_uniform_layout_manager->add("uniform_layout.joint_ubo"_hash);
    uniform_layout_t *animation_layout_ptr = g_uniform_layout_manager->get(animation_layout_hdl);
    uniform_layout_info_t animation_ubo_info = {};
    animation_ubo_info.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    *animation_layout_ptr = make_uniform_layout(&animation_ubo_info);
    
    g_entities->entity_ppln = g_pipeline_manager->add("pipeline.model"_hash);
    auto *entity_ppln = g_pipeline_manager->get(g_entities->entity_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/lp_notex_animated.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/lp_notex_animated.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/lp_notex_animated.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash),
                                         animation_layout_hdl);
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_entities->entity_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        entity_ppln->info = info;
        make_graphics_pipeline(entity_ppln);
    }
    // TODO: Rename all the pipelines correctly : animated / normal
    g_entities->rolling_entity_ppln = g_pipeline_manager->add("pipeline.ball"_hash);
    auto *rolling_entity_ppln = g_pipeline_manager->get(g_entities->rolling_entity_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/lp_notex_model.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/lp_notex_model.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                                 shader_module_info_t{"shaders/SPV/lp_notex_model.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_entities->rolling_entity_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        rolling_entity_ppln->info = info;
        make_graphics_pipeline(rolling_entity_ppln);
    }

    g_entities->dbg_hitbox_ppln = g_pipeline_manager->add("pipeline.hitboxes"_hash);
    auto *hitbox_ppln = g_pipeline_manager->get(g_entities->dbg_hitbox_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/hitbox_render.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/hitbox_render.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {240, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_LINE,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, nullptr,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        hitbox_ppln->info = info;
        make_graphics_pipeline(hitbox_ppln);
    }

    g_entities->entity_shadow_ppln = g_pipeline_manager->add("pipeline.model_shadow"_hash);
    auto *entity_shadow_ppln = g_pipeline_manager->get(g_entities->entity_shadow_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/lp_notex_model_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/lp_notex_model_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         animation_layout_hdl);
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, &g_entities->entity_model,
                               true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
        entity_shadow_ppln->info = info;
        make_graphics_pipeline(entity_shadow_ppln);
    }

    g_entities->rolling_entity_shadow_ppln = g_pipeline_manager->add("pipeline.ball_shadow"_hash);
    auto *rolling_entity_shadow_ppln = g_pipeline_manager->get(g_entities->rolling_entity_shadow_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/model_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/model_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, &g_entities->rolling_entity_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
        rolling_entity_shadow_ppln->info = info;
        make_graphics_pipeline(rolling_entity_shadow_ppln);
    }
}

internal_function void initialize_entities_data(VkCommandPool *cmdpool, input_state_t *input_state, application_type_t app_type)
{
    entity_t r2 = construct_entity("main"_hash,
                                   vector3_t(0.0f),
                                   vector3_t(1.0f, 0.0f, 1.0f),
                                   quaternion_t(glm::radians(45.0f), vector3_t(0.0f, 1.0f, 0.0f)));

    r2.size = vector3_t(5.0f);
    //r2.ws_v = vector3_t(0.0f, 0.0f, -20.0f);
    entity_handle_t rv2 = add_entity(r2);
    g_entities->main_entity = rv2;
    auto *r2_ptr = get_entity(rv2);

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        rendering_component_t *r2_ptr_rendering = add_rendering_component(r2_ptr);
        animation_component_t *r2_animation = add_animation_component(r2_ptr,
                                                                      g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.joint_ubo"_hash)),
                                                                      &g_entities->entity_mesh_skeleton,
                                                                      &g_entities->entity_mesh_cycles,
                                                                      cmdpool);

        r2_ptr_rendering->push_k.color = vector4_t(0.7f, 0.7f, 0.7f, 1.0f);
        r2_ptr_rendering->push_k.roughness = 0.8f;
        r2_ptr_rendering->push_k.metalness = 0.6f;

        auto *camera_component_ptr = add_camera_component(r2_ptr, add_camera(input_state, get_backbuffer_resolution()));
        camera_component_ptr->is_third_person = true;
        
        bind_camera_to_3d_scene_output(camera_component_ptr->camera);
    }

    physics_component_t *physics = add_physics_component(r2_ptr, false);
    physics->enabled = false;
    physics->hitbox.x_min = -1.001f;
    physics->hitbox.x_max = 1.001f;
    physics->hitbox.y_min = -1.001f;
    physics->hitbox.y_max = 1.001f;
    physics->hitbox.z_min = -1.001f;
    physics->hitbox.z_max = 1.001f;
}

internal_function void render_world(uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_groups = get_camera_transform_uniform_groups();
    shadow_display_t shadow_display_data = get_shadow_display();
    
    uniform_group_t uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    camera_t *camera = get_camera_bound_to_3d_output();
    
    // Rendering to the shadow map
    begin_shadow_offscreen(4000, 4000, queue);
    {
        auto *model_ppln = g_pipeline_manager->get(g_entities->entity_shadow_ppln);
        g_entities->entity_submission_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        auto *rolling_model_ppln = g_pipeline_manager->get(g_entities->rolling_entity_shadow_ppln);
        g_entities->rolling_entity_submission_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, rolling_model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, queue);
    {
        auto *entity_ppln = g_pipeline_manager->get(g_entities->entity_ppln);
        auto *rolling_entity_ppln = g_pipeline_manager->get(g_entities->rolling_entity_ppln);
    
        g_entities->entity_submission_queue.submit_queued_materials({2, uniform_groups}, entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        g_entities->rolling_entity_submission_queue.submit_queued_materials({2, uniform_groups}, rolling_entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        g_voxel_chunks->gpu_queue.submit_queued_materials({1, uniform_groups}, g_pipeline_manager->get(g_voxel_chunks->chunk_pipeline), queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        g_entities->entity_submission_queue.flush_queue();
        g_entities->rolling_entity_submission_queue.flush_queue();
        
        render_3d_frustum_debug_information(&uniform_groups[0], queue, image_index, g_pipeline_manager->get(g_entities->dbg_hitbox_ppln));
        
        // ---- render skybox ----
        render_atmosphere({1, uniform_groups}, camera->p, queue);
    }
    end_deferred_rendering(camera->v_m, queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_groups[image_index], camera->v_m, camera->p_m);
}

internal_function int32_t lua_get_player_position(lua_State *state);
internal_function int32_t lua_set_player_position(lua_State *state);
internal_function int32_t lua_toggle_collision_box_render(lua_State *state);
internal_function int32_t lua_toggle_collision_edge_render(lua_State *state);
internal_function int32_t lua_toggle_sphere_collision_triangles_render(lua_State *state);
internal_function int32_t lua_render_entity_direction_information(lua_State *state);
internal_function int32_t lua_set_veclocity_in_view_direction(lua_State *state);
internal_function int32_t lua_get_player_ts_view_direction(lua_State *state);
internal_function int32_t lua_stop_simulation(lua_State *state);
internal_function int32_t lua_load_mesh(lua_State *state);
internal_function int32_t lua_load_model_information_for_mesh(lua_State *state);
internal_function int32_t lua_load_skeleton(lua_State *state);
internal_function int32_t lua_load_animations(lua_State *state);
internal_function int32_t lua_initialize_entity(lua_State *state);
internal_function int32_t lua_attach_rendering_component(lua_State *state);
internal_function int32_t lua_attach_animation_component(lua_State *state);
internal_function int32_t lua_attach_physics_component(lua_State *state);
internal_function int32_t lua_attach_camera_component(lua_State *state);
internal_function int32_t lua_bind_entity_to_3d_output(lua_State *state);
internal_function int32_t lua_go_down(lua_State *state);
internal_function int32_t lua_placeholder_c_out(lua_State *state) { return(0); }
internal_function int32_t lua_reinitialize(lua_State *state);

internal_function void entry_point(void)
{
    // Load globals
    execute_lua("globals = require \"scripts/globals/globals\"");
    
    // Load startup code
    const char *startup_script = "scripts/sandbox/startup.lua";
    file_handle_t handle = create_file(startup_script, file_type_t::TEXT);
    auto contents = read_file_tmp(handle);
    execute_lua((const char *)contents.content);
    remove_and_destroy_file(handle);

    // Execute startup code
    execute_lua("startup()");
}

void hard_initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
{
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_player_position", &lua_get_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "set_player_position", &lua_set_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "toggle_hit_box_display", &lua_toggle_collision_box_render);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "render_direction_info", &lua_render_entity_direction_information);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_ts_view_dir", &lua_get_player_ts_view_direction);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "stop_simulation", &lua_stop_simulation);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "go_down", &lua_go_down);

    if (app_type == application_type_t::CONSOLE_APPLICATION_MODE)
    {
        add_global_to_lua(script_primitive_type_t::FUNCTION, "c_out", &lua_placeholder_c_out);
    }

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        g_entities->rolling_entity_submission_queue = make_gpu_material_submission_queue(10, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
        g_entities->entity_submission_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
    }

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        initialize_entities_graphics_data(cmdpool, input_state);
    }

    hard_initialize_chunks();
    
    initialize_world(input_state, cmdpool, app_type, app_mode);
    
    clear_linear();
}

void initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
{
    initialize_entities_data(cmdpool, input_state, app_type);
    
    g_voxel_chunks->max_chunks = 20 * 20 * 20;
    g_voxel_chunks->chunks = (voxel_chunk_t **)allocate_free_list(sizeof(voxel_chunk_t *) * g_voxel_chunks->max_chunks);
    memset(g_voxel_chunks->chunks, 0, sizeof(voxel_chunk_t *) * g_voxel_chunks->max_chunks);

    voxel_chunk_t **chunk_ptr = get_voxel_chunk(0);
    *chunk_ptr = (voxel_chunk_t *)allocate_free_list(sizeof(voxel_chunk_t));
    
    initialize_chunk(*chunk_ptr, vector3_t(0.0f));
    push_chunk_to_render_queue(*chunk_ptr);
}

internal_function void clean_up_entities(void)
{
    // Gets rid of all the entities, terrains, etc..., but not rendering stuff.
    g_entities->entity_count = 0;
    g_entities->physics_component_count = 0;
    g_entities->camera_component_count = 0;
    g_entities->rendering_component_count = 0;

    for (uint32_t i = 0; i < g_entities->animation_component_count; ++i)
    {
        destroy_animated_instance(&g_entities->animation_components[i].animation_instance);
    }
    g_entities->animation_component_count = 0;
    
    g_entities->main_entity = -1;

    g_entities->name_map.clean_up();

    g_entities->rolling_entity_submission_queue.mtrl_count = 0;
}

void clean_up_world_data(void)
{
    clean_up_entities();
}

void make_world_data(void /* Some kind of state */)
{
}

void update_network_world_state(void)
{
}

void sync_gpu_memory_with_world_state(gpu_command_queue_t *cmdbuf, uint32_t image_index)
{
    update_animation_gpu_data(cmdbuf);
    update_3d_output_camera_transforms(image_index);
}

void handle_all_input(input_state_t *input_state, float32_t dt, element_focus_t focus)
{
    // If world has focus
    if (focus == WORLD_3D_ELEMENT_FOCUS)
    {
        handle_world_input(input_state, dt);
        handle_input_debug(input_state, dt);
    }
}

void update_world(input_state_t *input_state,
                  float32_t dt,
                  uint32_t image_index,
                  uint32_t current_frame,
                  gpu_command_queue_t *cmdbuf,
                  application_type_t app_type,
                  element_focus_t focus)
{    
    switch (app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
            handle_all_input(input_state, dt, focus);
            
            update_entities(dt, app_type);

            sync_gpu_memory_with_world_state(cmdbuf, image_index);
    
            render_world(image_index, current_frame, cmdbuf);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            update_entities(dt, app_type);
        } break;
    }
}


#include <glm/gtx/string_cast.hpp>

void handle_main_entity_mouse_movement(entity_t *e, uint32_t *action_flags, input_state_t *input_state, float32_t dt)
{
    if (input_state->cursor_moved)
    {
        vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
        
        // TODO: Make sensitivity configurable with a file or something, and later menu
        persist_var constexpr uint32_t SENSITIVITY = 15.0f;
    
        vector2_t prev_mp = vector2_t(input_state->previous_cursor_pos_x, input_state->previous_cursor_pos_y);
        vector2_t curr_mp = vector2_t(input_state->cursor_pos_x, input_state->cursor_pos_y);

        vector3_t res = e->ws_d;
	    
        vector2_t d = (curr_mp - prev_mp);

        float32_t x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
        float32_t y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
                
        res = matrix3_t(glm::rotate(x_angle, up)) * res;
        vector3_t rotate_y = glm::cross(res, up);
        res = matrix3_t(glm::rotate(y_angle, rotate_y)) * res;

        res = glm::normalize(res);
                
        float32_t up_dot_view = glm::dot(up, res);
        float32_t minus_up_dot_view = glm::dot(-up, res);
                

        float32_t limit = 0.99f;
        if (up_dot_view > -limit && up_dot_view < limit && minus_up_dot_view > -limit && minus_up_dot_view < limit)
        {
            e->ws_d = res;

            char buffer[40] = {};
            sprintf(buffer, "%f, %f\n", up_dot_view, minus_up_dot_view);
            OutputDebugString(buffer);
        }
        else
        {
            OutputDebugString("Too far\n");
        }
    }
}

void handle_main_entity_mouse_button_input(entity_t *e, uint32_t *action_flags, input_state_t *input_state, float32_t dt)
{
}

void handle_main_entity_keyboard_input(entity_t *e, uint32_t *action_flags, physics_component_t *e_physics, input_state_t *input_state, float32_t dt)
{
    vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
    
    uint32_t movements = 0;
    float32_t accelerate = 1.0f;
    
    auto acc_v = [&movements, &accelerate](const vector3_t &d, vector3_t &dst){ ++movements; dst += d * accelerate; };

    vector3_t d = glm::normalize(vector3_t(e->ws_d.x,
                                           e->ws_d.y,
                                           e->ws_d.z));

    vector3_t res = {};

    *action_flags = 0;
    if (input_state->keyboard[keyboard_button_type_t::R].is_down) {accelerate = 6.0f; *action_flags |= (1 << action_flags_t::ACTION_RUN);}
    if (input_state->keyboard[keyboard_button_type_t::W].is_down) {acc_v(d, res); *action_flags |= (1 << action_flags_t::ACTION_FORWARD);}
    if (input_state->keyboard[keyboard_button_type_t::A].is_down) {acc_v(-glm::cross(d, up), res); *action_flags |= (1 << action_flags_t::ACTION_LEFT);}
    if (input_state->keyboard[keyboard_button_type_t::S].is_down) {acc_v(-d, res); *action_flags |= (1 << action_flags_t::ACTION_BACK);} 
    if (input_state->keyboard[keyboard_button_type_t::D].is_down) {acc_v(glm::cross(d, up), res); *action_flags |= (1 << action_flags_t::ACTION_RIGHT);}
    
    if (input_state->keyboard[keyboard_button_type_t::SPACE].is_down)
    {
        *action_flags |= (1 << action_flags_t::ACTION_UP);
    }
    
    if (input_state->keyboard[keyboard_button_type_t::LEFT_SHIFT].is_down)
    {
        acc_v(-up, res);
        *action_flags |= (1 << action_flags_t::ACTION_DOWN);
    }

    if (input_state->keyboard[keyboard_button_type_t::E].is_down && !e->toggled_rolling_previous_frame)
    {
        e->toggled_rolling_previous_frame = 1;
        e->rolling_mode ^= 1;
        if (!e->rolling_mode)
        {
            e->rolling_rotation = matrix4_t(1.0f);
            e->rolling_rotation_angle = 0.0f;
        }
    }
    else if (!input_state->keyboard[keyboard_button_type_t::E].is_down)
    {
        e->toggled_rolling_previous_frame = 0;
    }
            

    if (movements > 0)
    {
        res = res * 15.0f;

        e->ws_input_v = res;
    }
    else
    {
        e->ws_input_v = vector3_t(0.0f);
    }
}

void handle_main_entity_action(input_state_t *input_state, float32_t dt)
{
    entity_t *main_entity = get_main_entity();
    if (main_entity)
    {
        entity_t *e = main_entity;
        physics_component_t *e_physics = &g_entities->physics_components[e->components.physics_component];

        handle_main_entity_mouse_movement(e, &e->action_flags, input_state, dt);
        handle_main_entity_mouse_button_input(e, &e->action_flags, input_state, dt);
        handle_main_entity_keyboard_input(e, &e->action_flags, e_physics, input_state, dt);
    }
}

void handle_world_input(input_state_t *input_state, float32_t dt)
{
    handle_main_entity_action(input_state, dt);
}

// Not to do with moving the entity, just debug stuff : will be used later for stuff like opening menus
void handle_input_debug(input_state_t *input_state, float32_t dt)
{
    // ---- get bound entity ----
    // TODO make sure to check if main_entity < 0
    /*entity_t *e_ptr = &g_entities->entity_list[g_entities->main_entity];
      camera_component_t *e_camera_component = &g_entities->camera_components[e_ptr->components.camera_component];
      physics_component_t *e_physics = &g_entities->physics_components[e_ptr->components.physics_component];
      camera_t *e_camera = get_camera(e_camera_component->camera);
      vector3_t up = e_ptr->on_t->ws_n;
    
      shadow_matrices_t shadow_data = get_shadow_matrices();
      shadow_debug_t    shadow_debug = get_shadow_debug();
    
      //    shadow_data.light_view_matrix = glm::lookAt(vector3_t(0.0f), -glm::normalize(light_pos), vector3_t(0.0f, 1.0f, 0.0f));

      if (input_state->keyboard[keyboard_button_type_t::P].is_down)
      {
      for (uint32_t i = 0; i < 8; ++i)
      {
      e_camera->captured_frustum_corners[i] = shadow_debug.frustum_corners[i];
      }

      e_camera->captured = 1;
      e_camera->captured_shadow_corners[0] = vector4_t(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
      e_camera->captured_shadow_corners[1] = vector4_t(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
      e_camera->captured_shadow_corners[2] = vector4_t(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_min, 1.0f);
      e_camera->captured_shadow_corners[3] = vector4_t(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_min, 1.0f);

      e_camera->captured_shadow_corners[4] = vector4_t(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
      e_camera->captured_shadow_corners[5] = vector4_t(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
      e_camera->captured_shadow_corners[6] = vector4_t(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
      e_camera->captured_shadow_corners[7] = vector4_t(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
      }*/
}



void destroy_world(void)
{
    g_render_pass_manager->clean_up();
    g_image_manager->clean_up();
    g_framebuffer_manager->clean_up();
    g_pipeline_manager->clean_up();
    g_gpu_buffer_manager->clean_up();

    destroy_graphics();
}

internal_function int32_t lua_get_player_position(lua_State *state)
{
    // For now, just sets the main player's position
    entity_t *main_entity = &g_entities->entity_list[g_entities->main_entity];
    lua_pushnumber(state, main_entity->ws_p.x);
    lua_pushnumber(state, main_entity->ws_p.y);
    lua_pushnumber(state, main_entity->ws_p.z);
    return(3);
}

internal_function int32_t lua_set_player_position(lua_State *state)
{
    float32_t x = lua_tonumber(state, -3);
    float32_t y = lua_tonumber(state, -2);
    float32_t z = lua_tonumber(state, -1);
    entity_t *main_entity = &g_entities->entity_list[g_entities->main_entity];
    main_entity->ws_p.x = x;
    main_entity->ws_p.y = y;
    main_entity->ws_p.z = z;
    return(0);
}

internal_function int32_t lua_toggle_collision_box_render(lua_State *state)
{
    g_entities->dbg.hit_box_display ^= true;
    return(0);
}

internal_function int32_t lua_render_entity_direction_information(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    g_entities->dbg.render_sliding_vector_entity = get_entity(kname);

    persist_var char buffer[50];
    sprintf(buffer, "rendering for entity: %s", name);
    console_out(buffer);
    
    return(0);
}

internal_function int32_t lua_set_veclocity_in_view_direction(lua_State *state)
{
    const char *name = lua_tostring(state, -2);
    float32_t velocity = lua_tonumber(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));
    entity_t *entity = get_entity(kname);
    entity->ws_v += entity->ws_d * velocity;
    return(0);
}

internal_function int32_t lua_get_player_ts_view_direction(lua_State *state)
{
    // For now, just sets the main player's position
    entity_t *main_entity = &g_entities->entity_list[g_entities->main_entity];
    //    vector4_t dir = glm::scale(main_entity->on_t->size) * main_entity->on_t->inverse_transform * vector4_t(main_entity->ws_d, 0.0f);
    lua_pushnumber(state, main_entity->ws_d.x);
    lua_pushnumber(state, main_entity->ws_d.y);
    lua_pushnumber(state, main_entity->ws_d.z);
    return(3);
}

internal_function int32_t lua_stop_simulation(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    entity_t *entity = get_entity(kname);

    physics_component_t *component = &g_entities->physics_components[ entity->components.physics_component ];
    component->enabled = false;
    component->ws_velocity = vector3_t(0.0f);
    
    return(0);
}

internal_function int32_t lua_go_down(lua_State *state)
{
    entity_t *main = get_main_entity();
    auto *istate = get_input_state();
    istate->keyboard[keyboard_button_type_t::LEFT_SHIFT].is_down = is_down_t::REPEAT;
    istate->keyboard[keyboard_button_type_t::LEFT_SHIFT].down_amount += 1.0f / 60.0f;
    return(0);
}

void initialize_world_translation_unit(struct game_memory_t *memory)
{
    g_entities = &memory->world_state.entities;
    g_voxel_chunks = &memory->world_state.voxel_chunks;
}

internal_function int32_t lua_reinitialize(lua_State *state)
{
    return(0);
}

