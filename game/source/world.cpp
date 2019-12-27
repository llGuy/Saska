// TODO all geometry shaders for performance

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

#include "chunk.hpp"
#include "chunks_gstate.hpp"
#include "entities_gstate.hpp"
#include "particles_gstate.hpp"

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr float32_t PI = 3.14159265359f;
constexpr uint8_t VOXEL_HAS_NOT_BEEN_APPENDED_TO_HISTORY = 255;
constexpr float32_t MAX_VOXEL_VALUE = 254.0f;

// To initialize with initialize translation unit function
static bool g_initialized_world;
static uint64_t g_current_tick;

/*static int32_t lua_get_player_position(lua_State *state);
static int32_t lua_set_player_position(lua_State *state);
static int32_t lua_toggle_collision_box_render(lua_State *state);
static int32_t lua_toggle_collision_edge_render(lua_State *state);
static int32_t lua_toggle_sphere_collision_triangles_render(lua_State *state);
static int32_t lua_render_player_direction_information(lua_State *state);
static int32_t lua_set_veclocity_in_view_direction(lua_State *state);
static int32_t lua_get_player_ts_view_direction(lua_State *state);
static int32_t lua_enable_physics(lua_State *state);
static int32_t lua_disable_physics(lua_State *state);
static int32_t lua_load_mesh(lua_State *state);
static int32_t lua_load_model_information_for_mesh(lua_State *state);
static int32_t lua_load_skeleton(lua_State *state);
static int32_t lua_load_animations(lua_State *state);
static int32_t lua_initialize_player(lua_State *state);
static int32_t lua_attach_rendering_component(lua_State *state);
static int32_t lua_attach_animation_component(lua_State *state);
static int32_t lua_attach_physics_component(lua_State *state);
static int32_t lua_attach_camera_component(lua_State *state);
static int32_t lua_bind_player_to_3d_output(lua_State *state);
static int32_t lua_go_down(lua_State *state);
static int32_t lua_placeholder_c_out(lua_State *state) { return(0); }
static int32_t lua_reinitialize(lua_State *state);
*/
static void entry_point(void);
void hard_initialize_world(raw_input_t *raw_input, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode);
void initialize_world(raw_input_t *raw_input, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode);
static void clean_up_entities(void);
void clean_up_world_data(void);
void make_world_data(void /* Some kind of state */);
void update_network_world_state(void);
void sync_gpu_memory_with_world_state(gpu_command_queue_t *cmdbuf, uint32_t image_index);
//void handle_input_debug(raw_input_t *raw_input, float32_t dt);
void destroy_world(void);


uint64_t *get_current_tick(void)
{
    return(&g_current_tick);
}









static void render_world(uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_groups = get_camera_transform_uniform_groups();
    shadow_display_t shadow_display_data = get_shadow_display();
    
    uniform_group_t uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    camera_t *camera = get_camera_bound_to_3d_output();

    // Rendering to the shadow map
    begin_shadow_offscreen(lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_H, queue);
    {
        render_entities_to_shadowmap(&transforms_ubo_uniform_groups[image_index], queue);
        render_chunks_to_shadowmap(&transforms_ubo_uniform_groups[image_index], queue);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, queue);
    {
        render_entities(uniform_groups, queue);
        render_chunks(uniform_groups, queue);
        //render_3d_frustum_debug_information(&uniform_groups[0], queue, image_index, g_pipeline_manager->get(g_entities->dbg_hitbox_ppln));
        // ---- render skybox ----
        render_atmosphere({1, uniform_groups}, camera->p, queue);
        render_sun(uniform_groups, queue);
    }
    do_lighting_and_transition_to_alpha_rendering(camera->v_m, queue);
    {
        // Render particles
        // TODO: In future, render skybox and sun here
        render_alpha_particles(uniform_groups, queue);
    }
    end_deferred_rendering(queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_groups[image_index], camera->v_m, camera->p_m);
}


static void entry_point(void)
{
    // Load globals
    execute_lua("globals = require \"scripts/globals/globals\"");
    
    // Load startup code
    const char *startup_script = "scripts/sandbox/startup.lua";
    file_handle_t handle = create_file(startup_script, file_type_flags_t::TEXT | file_type_flags_t::ASSET);
    auto contents = read_file_tmp(handle);
    execute_lua((const char *)contents.content);
    remove_and_destroy_file(handle);

    // Execute startup code
    execute_lua("startup()");
}


void hard_initialize_world(raw_input_t *raw_input, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
{
    /*  add_global_to_lua(script_primitive_type_t::FUNCTION, "get_player_position", &lua_get_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "set_player_position", &lua_set_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "toggle_hit_box_display", &lua_toggle_collision_box_render);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "render_direction_info", &lua_render_player_direction_information);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_ts_view_dir", &lua_get_player_ts_view_direction);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "enable_physics", &lua_enable_physics);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "disable_physics", &lua_disable_physics);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "go_down", &lua_go_down);*/

    if (app_type == application_type_t::CONSOLE_APPLICATION_MODE)
    {
        //add_global_to_lua(script_primitive_type_t::FUNCTION, "c_out", &lua_placeholder_c_out);
    }

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        //g_entities->rolling_player_submission_queue = make_gpu_material_submission_queue(10, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
        //g_entities->player_submission_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
    }

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        //initialize_entities_graphics_data(cmdpool, raw_input);
    }

    initialize_entities_state();
    initialize_chunks_state();
    initialize_particles_state();

    initialize_world(raw_input, cmdpool, app_type, app_mode);
    //deinitialize_world();
    //initialize_world(raw_input, cmdpool, app_type, app_mode);
        
    clear_linear();



    // For server mode
    update_spectator_camera(glm::lookAt(vector3_t(-140.0f, 140.0f, -140.0f), glm::normalize(vector3_t(1.0f, -1.0f, 1.0f)) + vector3_t(-140.0f, 140.0f, -140.0f), vector3_t(0.0f, 1.0f, 0.0f)));
}


void initialize_world(game_state_initialize_packet_t *packet, raw_input_t *raw_input)
{
    g_current_tick = 0;
    
    // Initialize players
    populate_entities_state(packet, raw_input);

    populate_chunks_state(packet);
}


void initialize_world(raw_input_t *raw_input, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
{
    g_current_tick = 0;
    
    // REMOVE THIS - JUST TESTING REMOTE LEVEL STUFF
    //initialize_players(raw_input, app_type);

    //populate_chunks_state(nullptr);
}

void deinitialize_world(void)
{
    deinitialize_chunks_state();

    deinitialize_entities_state();
}





void clean_up_world_data(void)
{
    //clean_up_entities();
}


void make_world_data(void /* Some kind of state */)
{
}


void update_network_world_state(void)
{
}


void initialize_game_state_initialize_packet(game_state_initialize_packet_t *packet, player_handle_t new_client_index)
{
    fill_game_state_initialize_packet_with_chunk_state(packet);

    fill_game_state_initialize_packet_with_entities_state(packet, new_client_index);
}


void sync_gpu_memory_with_world_state(gpu_command_queue_t *cmdbuf, uint32_t image_index)
{
    update_3d_output_camera_transforms(image_index);
    
    sync_gpu_with_entities_state(cmdbuf);
    sync_gpu_with_chunks_state(cmdbuf);
    sync_gpu_with_particles_state(cmdbuf);
}


void tick_world(game_input_t *game_input, float32_t dt, uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *cmdbuf, application_type_t app_type, element_focus_t focus)
{    
    switch (app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
            tick_chunks_state(dt);
            tick_entities_state(game_input, dt, app_type);
            tick_particles_state(dt);
                
            sync_gpu_memory_with_world_state(cmdbuf, image_index);
    
            render_world(image_index, current_frame, cmdbuf);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            //            update_entities(dt, app_type);
        } break;
    }

    // Increment current tick
    ++(g_current_tick);
}


#include <glm/gtx/string_cast.hpp>



void destroy_world(void)
{
    g_render_pass_manager->clean_up();
    g_image_manager->clean_up();
    g_framebuffer_manager->clean_up();
    g_pipeline_manager->clean_up();
    g_gpu_buffer_manager->clean_up();

    destroy_graphics();
}
/*

static int32_t lua_get_player_position(lua_State *state)
{
    // For now, just sets the main player's position
    player_t *main_player = &g_entities->player_list[g_entities->main_player];
    lua_pushnumber(state, main_player->ws_position.x);
    lua_pushnumber(state, main_player->ws_position.y);
    lua_pushnumber(state, main_player->ws_position.z);
    return(3);
}


static int32_t lua_set_player_position(lua_State *state)
{
    float32_t x = lua_tonumber(state, -3);
    float32_t y = lua_tonumber(state, -2);
    float32_t z = lua_tonumber(state, -1);
    player_t *main_player = &g_entities->player_list[g_entities->main_player];
    main_player->ws_position.x = x;
    main_player->ws_position.y = y;
    main_player->ws_position.z = z;
    return(0);
}


static int32_t lua_toggle_collision_box_render(lua_State *state)
{
    g_entities->dbg.hit_box_display ^= true;
    return(0);
}


static int32_t lua_render_player_direction_information(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    g_entities->dbg.render_sliding_vector_player = get_player(kname);

    static char buffer[50];
    sprintf(buffer, "rendering for player: %s", name);
    console_out(buffer);
    
    return(0);
}


static int32_t lua_set_veclocity_in_view_direction(lua_State *state)
{
    const char *name = lua_tostring(state, -2);
    float32_t velocity = lua_tonumber(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));
    player_t *player = get_player(kname);
    player->ws_velocity += player->ws_direction * velocity;
    return(0);
}


static int32_t lua_get_player_ts_view_direction(lua_State *state)
{
    // For now, just sets the main player's position
    player_t *main_player = &g_entities->player_list[g_entities->main_player];
    //    vector4_t dir = glm::scale(main_player->on_t->size) * main_player->on_t->inverse_transform * vector4_t(main_player->ws_direction, 0.0f);
    lua_pushnumber(state, main_player->ws_direction.x);
    lua_pushnumber(state, main_player->ws_direction.y);
    lua_pushnumber(state, main_player->ws_direction.z);
    return(3);
}


static int32_t lua_enable_physics(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    player_t *player = get_player(kname);

    player->physics.enabled = 1;

    return(0);
}


static int32_t lua_disable_physics(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    player_t *player = get_player(kname);

    physics_component_t *component = &player->physics;
    component->enabled = false;
    
    return(0);
}


static int32_t lua_go_down(lua_State *state)
{
    player_t *main = get_main_player();
    auto *istate = get_raw_input();
    istate->buttons[button_type_t::LEFT_SHIFT].state = button_state_t::REPEAT;
    istate->buttons[button_type_t::LEFT_SHIFT].down_amount += 1.0f / 60.0f;
    return(0);
}


void initialize_world_translation_unit(struct game_memory_t *memory)
{
    g_entities = &memory->world_state.entities;
    g_particles = &memory->world_state.particles;
    g_initialized_world = &memory->world_state.initialized_world;
    g_current_tick = &memory->world_state.current_tick_id;
}


static int32_t lua_reinitialize(lua_State *state)
{
    return(0);
}

static int32_t lua_print_surrounding_voxel_values(lua_State *state)
{
    

    return(0);
}

*/
