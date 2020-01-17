#pragma once


#include "player.hpp"
#include "packets.hpp"
#include "graphics.hpp"


void initialize_entities_state(void);
void populate_entities_state(struct game_state_initialize_packet_t *packet, struct raw_input_t *raw_input);
void deinitialize_entities_state(void);

void fill_game_state_initialize_packet_with_entities_state(struct game_state_initialize_packet_t *packet, player_handle_t new_client_index);

void tick_entities_state(struct game_input_t *input, float32_t dt, enum application_type_t app_type);
void render_entities_to_shadowmap(uniform_group_t *transforms, gpu_command_queue_t *queue);
void render_entities(uniform_group_t *uniforms, gpu_command_queue_t *queue);
// For example if an entity is transitioning from third to first or vice versa
void render_transparent_entities(uniform_group_t *uniforms, gpu_command_queue_t *queue);
void sync_gpu_with_entities_state(struct gpu_command_queue_t *queue);

player_handle_t create_player_from_player_init_packet(uint32_t local_user_client_index, player_state_initialize_packet_t *player_init_packet, camera_handle_t main_camera = 0);

// Spawns player in random location of the world
player_handle_t spawn_player(const char *player_name, player_color_t color, uint32_t client_id /* Index into the clients array */);
void spawn_bullet(player_t *shooter);
void destroy_bullet(bullet_t *bullet);

// When player is standing
// TODO: Specify which meshes to push (when in future there may be different types of player entities)
void push_entity_to_skeletal_animation_queue(rendering_component_t *rendering, animation_component_t *animation);
void push_entity_to_skeletal_animation_alpha_queue(rendering_component_t *rendering, animation_component_t *animation);
void push_entity_to_skeletal_animation_shadow_queue(rendering_component_t *rendering, animation_component_t *animation);
// When player is rolling
void push_entity_to_rolling_queue(rendering_component_t *rendering);
void push_entity_to_rolling_alpha_queue(rendering_component_t *rendering);
void push_entity_to_rolling_shadow_queue(rendering_component_t *rendering);

player_t *get_user_player(void);
player_t *get_player(const char *name);
player_t *get_player(const constant_string_t &kstring);
player_t *get_player(player_handle_t handle);
