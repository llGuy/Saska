#include "game.hpp"
#include "bullet.hpp"
#include "packets.hpp"
#include "raw_input.hpp"
#include "game_input.hpp"
#include "entities_gstate.hpp"

#include "atmosphere.hpp"


#define MAX_PLAYERS 30
#define MAX_BULLETS 100

// Global
static int32_t player_count = 0;
static player_t player_list[MAX_PLAYERS];
static int32_t bullet_count = 0;
static bullet_t bullet_list[MAX_BULLETS];
static uint32_t removed_bullets_stack_head;
static uint16_t removed_bullets_stack[MAX_BULLETS];
static hash_table_inline_t<player_handle_t, 30, 5, 5> name_map{"map.entities"};
static pipeline_handle_t player_ppln;
static pipeline_handle_t player_alpha_ppln;
static pipeline_handle_t player_shadow_ppln;
static pipeline_handle_t rolling_player_ppln;
static pipeline_handle_t rolling_player_alpha_ppln;
static pipeline_handle_t rolling_player_shadow_ppln;
static mesh_t rolling_player_mesh;
static model_t rolling_player_model;
static mesh_t player_mesh;
static skeleton_t player_mesh_skeleton;
static animation_cycles_t player_mesh_cycles;
static uniform_layout_t animation_ubo_layout;
static model_t player_model;
static int32_t main_player = -1;
static gpu_material_submission_queue_t player_submission_queue;
static gpu_material_submission_queue_t player_submission_alpha_queue;
static gpu_material_submission_queue_t player_submission_shadow_queue;

static gpu_material_submission_queue_t rolling_player_submission_queue;
static gpu_material_submission_queue_t rolling_player_submission_alpha_queue;
static gpu_material_submission_queue_t rolling_player_submission_shadow_queue;



// Static declarations
static void handle_main_player_mouse_movement(player_t *player, game_input_t *game_input, float32_t dt);
static void handle_main_player_mouse_button_input(player_t *player, game_input_t *game_input, float32_t dt);
static void handle_main_player_keyboard_input(player_t *player, game_input_t *game_input, float32_t dt);
static player_handle_t add_player(const player_t &player);




// "Public" definitions
void initialize_entities_state(void)
{
    VkCommandPool *cmdpool = get_global_command_pool();
    
    rolling_player_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/icosphere.mesh_custom", cmdpool);
    rolling_player_model = make_mesh_attribute_and_binding_information(&rolling_player_mesh);
    rolling_player_model.index_data = rolling_player_mesh.index_data;
    
    player_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/spaceman.mesh_custom", cmdpool);
    player_model = make_mesh_attribute_and_binding_information(&player_mesh);
    player_model.index_data = player_mesh.index_data;
    player_mesh_skeleton = load_skeleton("models/spaceman_walk.skeleton_custom");
    player_mesh_cycles = load_animations("models/spaceman.animations_custom");

    uniform_layout_handle_t animation_layout_hdl = g_uniform_layout_manager->add("uniform_layout.joint_ubo"_hash);
    uniform_layout_t *animation_layout_ptr = g_uniform_layout_manager->get(animation_layout_hdl);
    uniform_layout_info_t animation_ubo_info = {};
    animation_ubo_info.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    *animation_layout_ptr = make_uniform_layout(&animation_ubo_info);
    
    player_ppln = g_pipeline_manager->add("pipeline.model"_hash);
    auto *player_ppln_ptr = g_pipeline_manager->get(player_ppln);
    initialize_3d_animated_shader(player_ppln_ptr, "shaders/SPV/lp_notex_animated", &player_model, animation_layout_hdl);
    
    rolling_player_ppln = g_pipeline_manager->add("pipeline.ball"_hash);
    auto *rolling_player_ppln_ptr = g_pipeline_manager->get(rolling_player_ppln);
    initialize_3d_unanimated_shader(rolling_player_ppln_ptr, "shaders/SPV/lp_notex_model", &rolling_player_model);

    player_shadow_ppln = g_pipeline_manager->add("pipeline.model_shadow"_hash);
    auto *player_shadow_ppln_ptr = g_pipeline_manager->get(player_shadow_ppln);
    initialize_3d_animated_shadow_shader(player_shadow_ppln_ptr, "shaders/SPV/lp_notex_model_shadow", &player_model, animation_layout_hdl);

    rolling_player_shadow_ppln = g_pipeline_manager->add("pipeline.ball_shadow"_hash);
    auto *rolling_player_shadow_ppln_ptr = g_pipeline_manager->get(rolling_player_shadow_ppln);
    initialize_3d_unanimated_shadow_shader(rolling_player_shadow_ppln_ptr, "shaders/SPV/model_shadow", &rolling_player_model);

    rolling_player_alpha_ppln = g_pipeline_manager->add("pipeline.ball"_hash);
    auto *rolling_player_alpha_ppln_ptr = g_pipeline_manager->get(rolling_player_alpha_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/lp_notex_model.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/lp_notex_model.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT },
            shader_module_info_t{ "shaders/SPV/lp_notex_model_alpha.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
            g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash),
            // Lighting stuff
            g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
            g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
            g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash));
        shader_pk_data_t push_k = { 160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(blend_type_t::ONE_MINUS_SRC_ALPHA);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_BACK_BIT, layouts, push_k, get_backbuffer_resolution(), blending, &rolling_player_model,
            true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 2, info);
        rolling_player_alpha_ppln_ptr->info = info;
        make_graphics_pipeline(rolling_player_alpha_ppln_ptr);
    }

    player_alpha_ppln = g_pipeline_manager->add("pipeline.model_alpha"_hash);
    // Uses forward-rendering style for lighting
    auto *player_alpha_ppln_ptr = g_pipeline_manager->get(player_alpha_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{ "shaders/SPV/lp_notex_animated_alpha.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
            shader_module_info_t{ "shaders/SPV/lp_notex_animated.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT },
            shader_module_info_t{ "shaders/SPV/lp_notex_animated_alpha.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
            g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash),
            // Lighting stuff
            g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
            g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
            g_uniform_layout_manager->get_handle("descriptor_set_layout.render_atmosphere_layout"_hash),
            animation_layout_hdl);
        shader_pk_data_t push_k = { 160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(blend_type_t::ONE_MINUS_SRC_ALPHA);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_BACK_BIT, layouts, push_k, get_backbuffer_resolution(), blending, &player_model,
            true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 2, info);
        player_alpha_ppln_ptr->info = info;
        make_graphics_pipeline(player_alpha_ppln_ptr);
    }

    rolling_player_submission_queue = make_gpu_material_submission_queue(10, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
    player_submission_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);

    rolling_player_submission_alpha_queue = make_gpu_material_submission_queue(10, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
    player_submission_alpha_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);

    rolling_player_submission_shadow_queue = make_gpu_material_submission_queue(10, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
    player_submission_shadow_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
}


void populate_entities_state(game_state_initialize_packet_t *packet, raw_input_t *raw_input)
{
    camera_handle_t main_camera = add_camera(raw_input, get_backbuffer_resolution());
    bind_camera_to_3d_scene_output(main_camera);
    
    for (uint32_t i = 0; i < packet->player_count; ++i)
    {
        player_state_initialize_packet_t *player_init_packet = &packet->player[i];

        create_player_from_player_init_packet(packet->client_index, &packet->player[i], main_camera);
    }
}


void deinitialize_entities_state(void)
{
    // Gets rid of all the entities, terrains, etc..., but not rendering stuff.
    player_count = 0;
    
    main_player = -1;

    name_map.clean_up();

    rolling_player_submission_queue.mtrl_count = 0;


    // Deinitialize entities:
    for (uint32_t i = 0; i < (uint32_t)player_count; ++i)
    {
        player_t *player = &player_list[i];
        deallocate_free_list(player->animation.animation_instance.interpolated_transforms);
        deallocate_free_list(player->animation.animation_instance.current_joint_transforms);
        player->animation.animation_instance.interpolated_transforms_ubo.destroy();
        push_uniform_group_to_destroyed_uniform_group_cache(&player_mesh_cycles, &player->animation.animation_instance);
        player->network.player_states_cbuffer.deinitialize();
        player->network.remote_player_states.deinitialize();
    }

    player_count = 0;
    main_player = -1;
    bullet_count = 0;
    removed_bullets_stack_head = 0;

    bind_camera_to_3d_scene_output(-1);
    remove_all_cameras();
}


void fill_game_state_initialize_packet_with_entities_state(struct game_state_initialize_packet_t *packet, player_handle_t new_client_index)
{
    packet->client_index = new_client_index;
    packet->player_count = player_count;
    packet->player = (player_state_initialize_packet_t *)allocate_linear(sizeof(player_state_initialize_packet_t) * player_count);

    for (uint32_t player = 0; player < (uint32_t)player_count; ++player)
    {
        player_t *p_player = &player_list[player];

        packet->player[player].client_id = p_player->network.client_state_index;
        packet->player[player].player_name = p_player->id.str;

        packet->player[player].ws_position_x = p_player->ws_position.x;
        packet->player[player].ws_position_y = p_player->ws_position.y;
        packet->player[player].ws_position_z = p_player->ws_position.z;

        packet->player[player].ws_view_direction_x = p_player->ws_direction.x;
        packet->player[player].ws_view_direction_y = p_player->ws_direction.y;
        packet->player[player].ws_view_direction_z = p_player->ws_direction.z;

        // TODO: Also need to set the rolling mode and other flags here
    }
}


void tick_entities_state(game_input_t *game_input, float32_t dt, application_type_t app_type)
{
    switch (app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE: 
    {
        player_t *main_player_ptr = get_user_player();
        if (main_player_ptr)
        {
            handle_main_player_keyboard_input(main_player_ptr, game_input, dt);
            handle_main_player_mouse_movement(main_player_ptr, game_input, dt);
            handle_main_player_mouse_button_input(main_player_ptr, game_input, dt);
        }
    } break;

    default: break;
    }

    
    
    // TODO: Change the structure of this loop
    // Make it so that it loops through each player_state one at a time, executing every player's player states "synchronously
    // Instead of crunching through each player's player_states in one go (this should fix a couple bugs to do with terraforming)
    // TODO: FIND OUT WHY SOMETIMES TERRAFORMING ISN'T THE SAME FOR SERVER AND CLIENT (THEY DON'T AMOUNT TO THE SAME RESULTS!)
    bool still_have_commands_to_go_through = 1;

    while (still_have_commands_to_go_through)
    {
        still_have_commands_to_go_through = 0;
        
        for (uint32_t player_index = 0; player_index < (uint32_t)player_count; ++player_index)
        {
            player_t *player = &player_list[player_index];

            // Commands to flush basically equals the amount of player_states that the player has left
            if (player->network.commands_to_flush > 0)
            {
                switch (app_type)
                {
                case application_type_t::WINDOW_APPLICATION_MODE:
                    {
                        
                        float32_t client_local_dt = player->network.tick(player, 0.0f);
                        player->physics.tick(player, client_local_dt);
                        player->camera.tick(player, client_local_dt);
                        player->rendering.tick(player, client_local_dt);
                        player->animation.tick(player, client_local_dt);
                        player->terraform_power.tick(player, client_local_dt);
                        player->shoot.tick(player, client_local_dt);
                        
                    } break;
                case application_type_t::CONSOLE_APPLICATION_MODE:
                    {
                        float32_t client_local_dt = player->network.tick(player, 0.0f);
                        player->physics.tick(player, client_local_dt);
                        player->camera.tick(player, client_local_dt);
                        player->terraform_power.tick(player, client_local_dt);
                        player->shoot.tick(player, client_local_dt);
                    } break;
                }

                --player->network.commands_to_flush;
                still_have_commands_to_go_through |= (player->network.commands_to_flush > 0);
            }
            // Basically if it is the client program running
            else if (player_index == main_player)
            {
                // Print to console player state before
                player->network.tick(player, 0.0f);
                player->physics.tick(player, dt);
                player->camera.tick(player, dt);
                player->rendering.tick(player, dt);
                player->animation.tick(player, dt);
                player->terraform_power.tick(player, dt);
                player->shoot.tick(player, dt);
            }
            // Local client (if entity is not controlled by user) or server when there are no commands to flush
            else
            {
                if (player->network.is_remote)
                {
                    player->network.tick(player, dt);
                }
            
                switch (app_type)
                {
                case application_type_t::WINDOW_APPLICATION_MODE:
                {
                    player->rendering.tick(player, dt);
                    player->animation.tick(player, dt);
                } break;

                default: break;
                }
            }

            if (player_index == main_player)
            {
                cache_player_state(dt);
            }

            if (!player->network.is_remote) player->action_flags = 0;
        }
    }

    for (uint32_t bullet_index = 0; bullet_index < (uint32_t)bullet_count; ++bullet_index)
    {
        bullet_t *bullet = &bullet_list[bullet_index];

        if (!bullet->dead)
        {
            switch (app_type)
            {
            case application_type_t::WINDOW_APPLICATION_MODE:
                {
                    bullet->rendering.tick(bullet, dt);
                    bullet->bounce_physics.tick(bullet, dt);
                    bullet->burnable.tick(bullet, dt);
                } break;
            case application_type_t::CONSOLE_APPLICATION_MODE:
                {
                    bullet->bounce_physics.tick(bullet, dt);
                    bullet->burnable.tick(bullet, dt);
                } break;
            }
        }
    }
}


void render_entities_to_shadowmap(uniform_group_t *transforms, gpu_command_queue_t *queue)
{
    // TODO: Make sure this uses the shadow submission queue
    auto *model_ppln = g_pipeline_manager->get(player_shadow_ppln);
    player_submission_shadow_queue.submit_queued_materials({1, transforms}, model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    
    auto *rolling_model_ppln = g_pipeline_manager->get(rolling_player_shadow_ppln);
    rolling_player_submission_shadow_queue.submit_queued_materials({1, transforms}, rolling_model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    player_submission_shadow_queue.flush_queue();
    rolling_player_submission_shadow_queue.flush_queue();
}


void render_entities(uniform_group_t *uniforms, gpu_command_queue_t *queue)
{
    auto *player_ppln_ptr = g_pipeline_manager->get(player_ppln);
    player_submission_queue.submit_queued_materials({2, uniforms}, player_ppln_ptr, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    
    auto *rolling_player_ppln_ptr = g_pipeline_manager->get(rolling_player_ppln);
    rolling_player_submission_queue.submit_queued_materials({2, uniforms}, rolling_player_ppln_ptr, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    player_submission_queue.flush_queue();
    rolling_player_submission_queue.flush_queue();
}


void render_transparent_entities(uniform_group_t *uniforms, gpu_command_queue_t *queue)
{
    uniform_group_t groups[] = { uniforms[0], uniforms[1], *atmosphere_irradiance_uniform(), *atmosphere_integrate_lookup_uniform(), *atmosphere_integrate_lookup_uniform() };
    
    auto *player_ppln_alpha_ptr = g_pipeline_manager->get(player_alpha_ppln);
    player_submission_alpha_queue.submit_queued_materials({5, groups}, player_ppln_alpha_ptr, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    
    auto *rolling_player_alpha_ppln_ptr = g_pipeline_manager->get(rolling_player_alpha_ppln);
    rolling_player_submission_alpha_queue.submit_queued_materials({5, groups}, rolling_player_alpha_ppln_ptr, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    player_submission_alpha_queue.flush_queue();
    rolling_player_submission_alpha_queue.flush_queue();
}


void sync_gpu_with_entities_state(gpu_command_queue_t *queue)
{
    for (uint32_t i = 0; i < (uint32_t)player_count; ++i)
    {
        player_t *player = &player_list[i];
        animation_component_t *animation = &player->animation;

        update_animated_instance_ubo(queue, &animation->animation_instance);
    }
}


player_handle_t create_player_from_player_init_packet(uint32_t local_user_client_index, player_state_initialize_packet_t *player_init_packet, camera_handle_t main_camera)
{
    bool is_current_client = (player_init_packet->client_id == local_user_client_index);
        
    player_create_info_t player_create_info = {};
    player_create_info.name = make_constant_string(player_init_packet->player_name, (uint32_t)strlen(player_init_packet->player_name));
    player_create_info.ws_position = vector3_t(player_init_packet->ws_position_x, player_init_packet->ws_position_y, player_init_packet->ws_position_z);
    player_create_info.ws_direction = vector3_t(player_init_packet->ws_view_direction_x, player_init_packet->ws_view_direction_y, player_init_packet->ws_view_direction_z);
    player_create_info.ws_rotation = quaternion_t(glm::radians(45.0f), vector3_t(0, 1, 0));
    player_create_info.ws_size = vector3_t(2);
    player_create_info.starting_velocity = 15.0f;
    player_create_info.color = player_color_t::DARK_GRAY;
    player_create_info.physics_info.enabled = 1;
    player_create_info.terraform_power_info.speed = 300.0f;
    player_create_info.terraform_power_info.terraform_radius = 20.0f;
        
    if (is_current_client)
    {
        player_create_info.camera_info.camera_index = main_camera;
        player_create_info.camera_info.is_third_person = 1;
        player_create_info.camera_info.distance_from_player = 15.0f;
    }
        
    player_create_info.animation_info.ubo_layout = g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.joint_ubo"_hash));
    player_create_info.animation_info.skeleton = &player_mesh_skeleton;
    player_create_info.animation_info.cycles = &player_mesh_cycles;
    player_create_info.shoot_info.cool_off = 0.0f;
    player_create_info.shoot_info.shoot_speed = 0.3f;
    player_create_info.network_info.client_state_index = player_init_packet->client_id;

    player_t user;
    user.initialize(&player_create_info);
        
    player_handle_t user_handle = add_player(user);
    
    if (is_current_client)
    {
        main_player = user_handle;
    }

    return(user_handle);
}


player_handle_t spawn_player(const char *player_name, player_color_t color, uint32_t client_id /* Index into the clients array */)
{
    // Spawns a player at the edge of a world
    player_create_info_t player_create_info = {};
    player_create_info.name = make_constant_string(player_name, (uint32_t)strlen(player_name));
    // TODO: Spawn player in a random position on the edge of the chunk grid
    player_create_info.ws_position = vector3_t(-140, 140, -140);
    player_create_info.ws_direction = -glm::normalize(player_create_info.ws_position);
    player_create_info.ws_rotation = quaternion_t(glm::radians(45.0f), vector3_t(0, 1, 0));
    player_create_info.ws_size = vector3_t(2);
    player_create_info.starting_velocity = 15.0f;
    player_create_info.color = player_color_t::DARK_GRAY;
    player_create_info.physics_info.enabled = 1;
    player_create_info.terraform_power_info.speed = 300.0f;
    player_create_info.terraform_power_info.terraform_radius = 20.0f;
    //    player_create_info.camera_info.camera_index = main_camera;
    player_create_info.camera_info.is_third_person = 1;
    player_create_info.camera_info.distance_from_player = 15.0f;
    player_create_info.animation_info.ubo_layout = g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.joint_ubo"_hash));
    player_create_info.animation_info.skeleton = &player_mesh_skeleton;
    player_create_info.animation_info.cycles = &player_mesh_cycles;
    player_create_info.shoot_info.cool_off = 0.0f;
    player_create_info.shoot_info.shoot_speed = 0.3f;
    player_create_info.network_info.entity_index /* = will be set in the add_player() function*/;
    player_create_info.network_info.client_state_index = client_id;

    player_t player;
    player.initialize(&player_create_info);

    return(add_player(player));
}


void spawn_bullet(player_t *shooter)
{
    bullet_t *new_bullet;
    uint32_t bullet_index;
    if (removed_bullets_stack_head > 0)
    {
        bullet_index = removed_bullets_stack_head;
        new_bullet = &bullet_list[removed_bullets_stack[removed_bullets_stack_head--]];
        new_bullet->dead = 0;
    }
    else
    {
        bullet_index = bullet_count;
        new_bullet = &bullet_list[bullet_count++];
    }
    bullet_create_info_t info = {};
    info.ws_position = shooter->ws_position;
    info.ws_direction = glm::normalize(shooter->ws_direction);
    info.ws_rotation = quaternion_t(glm::radians(45.0f), vector3_t(0, 1, 0));
    info.ws_size = vector3_t(0.7f);
    info.color = player_color_t::DARK_GRAY;
    info.bullet_index = bullet_index;
    new_bullet->initialize(&info);

    new_bullet->ws_velocity = shooter->ws_direction * 50.0f;
    new_bullet->ws_up = shooter->ws_up;

    new_bullet->burnable.set_on_fire(new_bullet->ws_position);
}


void destroy_bullet(bullet_t *bullet)
{
    bullet->dead = 1;
    removed_bullets_stack[removed_bullets_stack_head++] = bullet->bullet_index;
}


void push_entity_to_skeletal_animation_queue(rendering_component_t *rendering, animation_component_t *animation)
{
    uniform_group_t *group = &animation->animation_instance.group;
    player_submission_queue.push_material(&rendering->push_k, sizeof(rendering->push_k), &player_mesh, group);
}


void push_entity_to_skeletal_animation_alpha_queue(rendering_component_t *rendering, animation_component_t *animation)
{
    uniform_group_t *group = &animation->animation_instance.group;
    player_submission_alpha_queue.push_material(&rendering->push_k_alpha, sizeof(rendering->push_k_alpha), &player_mesh, group);
}


void push_entity_to_skeletal_animation_shadow_queue(rendering_component_t *rendering, animation_component_t *animation)
{
    uniform_group_t *group = &animation->animation_instance.group;
    player_submission_shadow_queue.push_material(&rendering->push_k, sizeof(rendering->push_k), &player_mesh, group);
}


void push_entity_to_rolling_queue(rendering_component_t *rendering)
{
    rolling_player_submission_queue.push_material(&rendering->push_k, sizeof(rendering->push_k), &rolling_player_mesh, nullptr);
}


void push_entity_to_rolling_alpha_queue(rendering_component_t *rendering)
{
    rolling_player_submission_alpha_queue.push_material(&rendering->push_k_alpha, sizeof(rendering->push_k_alpha), &rolling_player_mesh, nullptr);
}


void push_entity_to_rolling_shadow_queue(rendering_component_t *rendering)
{
    rolling_player_submission_shadow_queue.push_material(&rendering->push_k, sizeof(rendering->push_k), &rolling_player_mesh, nullptr);
}


player_t *get_user_player(void)
{
    if (main_player == -1)
    {
        return nullptr;
    }
    else
    {
        return &player_list[main_player];
    }
}


player_t *get_player(const char *name)
{
    return(get_player(make_constant_string(name, (uint32_t)strlen(name))));
}


player_t *get_player(const constant_string_t &kstring)
{
    player_handle_t v = *name_map.get(kstring.hash);
    return(&player_list[v]);
}


player_t *get_player(player_handle_t handle)
{
    return(&player_list[handle]);
}



// Static definitions
static void handle_main_player_mouse_movement(player_t *player, game_input_t *game_input, float32_t dt)
{
    if (game_input->actions[game_input_action_type_t::LOOK_RIGHT].state || game_input->actions[game_input_action_type_t::LOOK_LEFT].state || game_input->actions[game_input_action_type_t::LOOK_UP].state || game_input->actions[game_input_action_type_t::LOOK_DOWN].state)
    {
        vector3_t up = player->camera.ws_current_up_vector;
        
        // TODO: Make sensitivity configurable with a file or something, and later menu
        static constexpr float32_t SENSITIVITY = 15.0f;
    
        vector3_t res = player->ws_direction;
	    
        vector2_t d;
        d.x = game_input->actions[game_input_action_type_t::LOOK_RIGHT].value + game_input->actions[game_input_action_type_t::LOOK_LEFT].value;
        d.y = game_input->actions[game_input_action_type_t::LOOK_UP].value + game_input->actions[game_input_action_type_t::LOOK_DOWN].value;

        player->camera.mouse_diff = d;

        float32_t x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
        float32_t y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
                
        res = matrix3_t(glm::rotate(x_angle, up)) * res;
        vector3_t rotate_y = glm::cross(res, up);
        res = matrix3_t(glm::rotate(y_angle, rotate_y)) * res;

        res = glm::normalize(res);
                
        player->ws_direction = res;
    }
    else
    {
        player->camera.mouse_diff = vector2_t(0.0f);
    }
}


static void handle_main_player_mouse_button_input(player_t *player, game_input_t *game_input, float32_t dt)
{
    uint32_t *action_flags = &player->action_flags;
    if (game_input->actions[game_input_action_type_t::TRIGGER2].state) *action_flags |= (1 << action_flags_t::ACTION_TERRAFORM_ADD);
    if (game_input->actions[game_input_action_type_t::TRIGGER1].state) *action_flags |= (1 << action_flags_t::SHOOT);
}


static void handle_main_player_keyboard_input(player_t *player, game_input_t *game_input, float32_t dt)
{
    uint32_t *action_flags = &player->action_flags;
    
    vector3_t up = player->ws_up;
    
    uint32_t movements = 0;
    float32_t accelerate = 1.0f;
    
    auto acc_v = [&movements, &accelerate](const vector3_t &d, vector3_t &dst){ ++movements; dst += d * accelerate; };
    vector3_t d = glm::normalize(vector3_t(player->ws_direction.x, player->ws_direction.y, player->ws_direction.z));

    vector3_t res = {};

    *action_flags = 0;
    if (game_input->actions[game_input_action_type_t::TRIGGER3].state) {accelerate = 6.0f; *action_flags |= (1 << action_flags_t::ACTION_RUN);}
    if (game_input->actions[game_input_action_type_t::MOVE_FORWARD].state) {acc_v(d, res); *action_flags |= (1 << action_flags_t::ACTION_FORWARD);}
    if (game_input->actions[game_input_action_type_t::MOVE_LEFT].state) {acc_v(-glm::cross(d, up), res); *action_flags |= (1 << action_flags_t::ACTION_LEFT);}
    if (game_input->actions[game_input_action_type_t::MOVE_BACK].state) {acc_v(-d, res); *action_flags |= (1 << action_flags_t::ACTION_BACK);} 
    if (game_input->actions[game_input_action_type_t::MOVE_RIGHT].state) {acc_v(glm::cross(d, up), res); *action_flags |= (1 << action_flags_t::ACTION_RIGHT);}
    if (game_input->actions[game_input_action_type_t::TRIGGER4].state) *action_flags |= (1 << action_flags_t::ACTION_UP);
    if (game_input->actions[game_input_action_type_t::TRIGGER6].state) *action_flags |= (1 << action_flags_t::ACTION_DOWN);
    if (game_input->actions[game_input_action_type_t::TRIGGER5].state && !player->toggled_rolling_previous_frame)
    {
        player->toggled_rolling_previous_frame = 1;
        player->rolling_mode ^= 1;
        if (!player->rolling_mode)
        {
            player->rolling_rotation = matrix4_t(1.0f);
            player->current_rolling_rotation_angle = 0.0f;

            // Going from standing to rolling
            player->camera.transition_first_third.max_time = 0.3f;
            player->camera.transition_first_third.current = 0.0f;
            
            player->camera.transition_first_third.prev = 0.0f; // 1 = standing
            player->camera.transition_first_third.next = 1.0f; // 0 = rolling
            
            player->camera.transition_first_third.current_time = 0.0f;
            player->camera.transition_first_third.in_animation = 1;
        }
        else
        {
            // Going from rolling to standing
            player->camera.transition_first_third.max_time = 0.3f;
            player->camera.transition_first_third.current = 1.0f;
            
            player->camera.transition_first_third.prev = 1.0f;
            player->camera.transition_first_third.next = 0.0f;
            
            player->camera.transition_first_third.current_time = 0.0f;
            player->camera.transition_first_third.in_animation = 1;
        }
    }
    else if (!game_input->actions[game_input_action_type_t::TRIGGER5].state)
    {
        player->toggled_rolling_previous_frame = 0;
    }
            

    if (movements > 0)
    {
        res = res * 15.0f;

        player->ws_input_velocity = res;
    }
    else
    {
        player->ws_input_velocity = vector3_t(0.0f);
    }
}


static player_handle_t add_player(const player_t &player)
{
    player_handle_t view;
    view = player_count;

    name_map.insert(player.id.hash, view);
    
    player_list[player_count++] = player;

    player_t *player_ptr = get_player(view);
    player_ptr->index = view;
    player_ptr->network.entity_index = view;

    return(view);
}
