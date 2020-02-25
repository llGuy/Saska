#include "bullet.hpp"
#include "graphics.hpp"
#include "collision.hpp"
#include "component.hpp"
#include "chunks_gstate.hpp"
#include "entities_gstate.hpp"
#include "particles_gstate.hpp"
#include <glm/gtx/projection.hpp>

// Physics
void physics_component_t::tick(player_t *affected_player, float32_t dt) {
    if (enabled) {
        if (affected_player->rolling_mode) {
            tick_rolling_player_physics(affected_player, dt);
        }
        else {
            tick_standing_player_physics(affected_player, dt);
        }
    }
    else {
        tick_not_physically_affected_player(affected_player, dt);
    }
}

void physics_component_t::tick_standing_player_physics(player_t *player, float32_t dt) {
    if (state == entity_physics_state_t::IN_AIR) {
        player->ws_velocity += -player->ws_up * 9.81f * dt;
    }
    else if (state == entity_physics_state_t::ON_GROUND) {
        float32_t speed = 2.5f;
        
        bool moved = 1;

        movement_axes_t m_axes = compute_movement_axes(player->ws_direction, player->ws_up);

        axes = vector3_t(0);
        if (player->action_flags & (1 << action_flags_t::ACTION_FORWARD)) {
            axes.z += acceleration;
            moved &= 1;

            if (player->action_flags & (1 << action_flags_t::ACTION_RUN)) {
                speed *= 2.0f;
            }
        }
        if (player->action_flags & (1 << action_flags_t::ACTION_LEFT)) {
            axes.x -= acceleration;
            moved &= 1;
        }
        if (player->action_flags & (1 << action_flags_t::ACTION_BACK)) {
            axes.z -= acceleration;
            moved &= 1;
        }
        if (player->action_flags & (1 << action_flags_t::ACTION_RIGHT)) {
            axes.x += acceleration;
            moved &= 1;
        }

        vector3_t result_acceleration_vector = axes.x * m_axes.right + axes.y * m_axes.up + axes.z * m_axes.forward;

        vector3_t current_velocity = result_acceleration_vector * speed;
        
        player->ws_velocity = current_velocity - player->ws_up * 9.81f * dt;

        // Friction
        /*static constexpr float32_t TERRAIN_ROUGHNESS = .5f;
          float32_t cos_theta = glm::dot(-player->ws_up, -player->ws_up);
          vector3_t friction = -player->ws_velocity * TERRAIN_ROUGHNESS * 9.81f * .5f;
          player->ws_velocity += friction * dt;*/
    }

    collision_t collision = collide(player->ws_position, player->size, player->ws_velocity * dt, 0, {});
    if (collision.detected) {
        if (player->is_entering) {
            player->is_entering = 0;
        }

        if (state == entity_physics_state_t::IN_AIR) {
            movement_axes_t m_axes = compute_movement_axes(player->ws_direction, player->ws_up);
            player->ws_velocity = glm::normalize(glm::proj(player->ws_velocity, m_axes.forward));
        }

        player->ws_position = collision.es_at * player->size;

        vector3_t ws_normal = glm::normalize((collision.es_normal * player->size));
        player->ws_up = ws_normal;
        
        state = entity_physics_state_t::ON_GROUND;

        if (collision.under_terrain) {
            player->ws_position = collision.es_at * player->size;
        }
    }
    else {
        // If there was no collision, update position (velocity should be the same)
        player->ws_position = collision.es_at * player->size;
        player->ws_velocity = (collision.es_velocity * player->size) / dt;

        state = entity_physics_state_t::IN_AIR;
    }
}

void physics_component_t::tick_rolling_player_physics(player_t *player, float32_t dt) {
    // Only happens at the beginning of the game
    if (player->is_entering) {
        player->entering_acceleration += dt * 2.0f;
        // Go in the direction that the player is facing
        player->ws_velocity = player->entering_acceleration * player->ws_direction;
    }
    else {
        if (state == entity_physics_state_t::IN_AIR) {
            player->ws_velocity += -player->ws_up * 9.81f * dt;
        }
        else if (state == entity_physics_state_t::ON_GROUND) {
            movement_axes_t m_axes = compute_movement_axes(player->ws_direction, player->ws_up);

            axes = vector3_t(0);
            if (player->action_flags & (1 << action_flags_t::ACTION_FORWARD)) {
                axes.z += acceleration;
            }
            if (player->action_flags & (1 << action_flags_t::ACTION_LEFT)) {
                axes.x -= acceleration;
            }
            if (player->action_flags & (1 << action_flags_t::ACTION_BACK)) {
                axes.z -= acceleration;
            }
            if (player->action_flags & (1 << action_flags_t::ACTION_RIGHT)) {
                axes.x += acceleration;
            }
            vector3_t result_acceleration_vector = axes.x * m_axes.right + axes.y * m_axes.up + axes.z * m_axes.forward;

            player->ws_velocity += result_acceleration_vector * dt * 20.0f;
            player->ws_velocity -= player->ws_up * 9.81f * dt;

            // Friction
            static constexpr float32_t TERRAIN_ROUGHNESS = .5f;
            float32_t cos_theta = glm::dot(-player->ws_up, -player->ws_up);
            vector3_t friction = -player->ws_velocity * TERRAIN_ROUGHNESS * 9.81f * .5f;
            player->ws_velocity += friction * dt;
        }
    }

    vector3_t previous_position = player->ws_position;
    
    collision_t collision = collide(player->ws_position, player->size, player->ws_velocity * dt, 0, {});
    if (collision.detected) {
        if (player->is_entering) {
            player->is_entering = 0;
        }

        if (state == entity_physics_state_t::IN_AIR) {
            movement_axes_t m_axes = compute_movement_axes(player->ws_direction, player->ws_up);
            player->ws_velocity = glm::normalize(glm::proj(player->ws_velocity, m_axes.forward));
        }

        player->ws_position = collision.es_at * player->size;
        
        // If there "was" a collision (may not be on the ground right now as might have "slid" off) player's gravity pull direction changed
        vector3_t ws_normal = glm::normalize((collision.es_normal * player->size));

        player->ws_up = ws_normal;
        player->camera.ws_next_vector = player->ws_up;

        state = entity_physics_state_t::ON_GROUND;

        // Update rolling rotation speed
        if (!collision.under_terrain) {
            /*vector3_t velocity = player->ws_velocity;
            
              if (glm::dot(player->ws_velocity, player->ws_velocity) < 0.001f) {
              velocity = p
              }*/

            // TODO: FIX SO THAT WS_V IS EQUAL TO THE ACTUAL VELOCITY OF THE PLAYER


            vector3_t actual_player_v = player->ws_position - previous_position;
            float32_t velocity_length = glm::length(actual_player_v);

            if (glm::dot(actual_player_v, actual_player_v) < 0.0001f) {
                actual_player_v = previous_velocity;
                velocity_length = 0.0f;
            }
            else {
                previous_velocity = actual_player_v;

                vector3_t cross = glm::cross(actual_player_v, player->ws_up);
                vector3_t right = glm::normalize(cross);

                player->rolling_rotation_axis = right;
            }
            
            player->current_rotation_speed = ((velocity_length) / calculate_sphere_circumference(player->size.x)) * 360.0f;
        }
        else {
            // Player is under the terrain
            player->ws_position = collision.es_at * player->size;

            /*collision_t new_collision = collide(player->ws_position, player->size, player->ws_velocity * dt, 0, {});
              uint32_t loop_count = 0;
              while (new_collision.under_terrain && loop_count < 10)
              {
              output_to_debug_console("Waiting for player to no longer be under the terrain\n");
              player->ws_position += collision.es_normal * player->size;
              new_collision = collide(player->ws_position, player->size, player->ws_velocity * dt, 0, {});

              ++loop_count;
              }

              if (loop_count)
              {
              output_to_debug_console("Player no longer under terrain after: ", (int32_t)loop_count, " collision calculations\n");
              }*/
        }
    }
    else {
        // If there was no collision, update position (velocity should be the same)
        player->ws_position = collision.es_at * player->size;
        player->ws_velocity = (collision.es_velocity * player->size) / dt;

        state = entity_physics_state_t::IN_AIR;
    }

    // Update actual rolling rotation
    {
        player->current_rolling_rotation_angle += player->current_rotation_speed;
        if (player->current_rolling_rotation_angle > 360.0f) {
            player->current_rolling_rotation_angle = player->current_rolling_rotation_angle - 360.0f;
        }

        player->rolling_rotation = glm::rotate(glm::radians(player->current_rolling_rotation_angle), -player->rolling_rotation_axis);
    }

    player->ws_rotation = glm::quat_cast(player->rolling_rotation);

    //output_to_debug_console("Rotation: ", player->ws_rotation[0], " ; ", player->ws_rotation[1], " ; ", player->ws_rotation[2], " ; ", player->ws_rotation[3], "\n");
}

void physics_component_t::tick_not_physically_affected_player(player_t *player, float32_t dt) {
    vector3_t result_force = vector3_t(0.0f);

    vector3_t right = glm::normalize(glm::cross(player->ws_direction, player->ws_up));
    vector3_t forward = glm::normalize(glm::cross(vector3_t(0.0f, 1.0f, 0.0f), right));

    if (player->action_flags & (1 << action_flags_t::ACTION_FORWARD)) result_force += forward;
    if (player->action_flags & (1 << action_flags_t::ACTION_BACK)) result_force -= forward;
    if (player->action_flags & (1 << action_flags_t::ACTION_RIGHT)) result_force += right;
    if (player->action_flags & (1 << action_flags_t::ACTION_LEFT)) result_force -= right;
    if (player->action_flags & (1 << action_flags_t::ACTION_UP)) result_force += player->ws_up;
    if (player->action_flags & (1 << action_flags_t::ACTION_DOWN)) result_force -= player->ws_up;

    result_force *= 20.0f * player->size.x;
    
    collision_t collision = collide(player->ws_position, player->size, result_force * dt, 0, {});
    player->ws_position = collision.es_at * player->size;
}


// Terraforming
void terraform_power_component_t::tick(player_t *affected_player, float32_t dt) {
    if (affected_player->action_flags & (1 << action_flags_t::ACTION_TERRAFORM_DESTROY)) {
        send_vibration_to_gamepad();
        
        ray_cast_terraform(affected_player->ws_position, affected_player->ws_direction, 70.0f, dt, 60, 1, speed);
    }

    if (affected_player->action_flags & (1 << action_flags_t::ACTION_TERRAFORM_ADD)) {
        send_vibration_to_gamepad();
        
        ray_cast_terraform(affected_player->ws_position, affected_player->ws_direction, 70.0f, dt, 60, 0, speed);
    }
}


// Camera
void camera_component_t::tick(player_t *affected_player, float32_t dt) {
    camera_t *camera_ptr = get_camera(camera);

    vector3_t up = ws_current_up_vector;

    vector3_t diff_vector = ws_next_vector - ws_current_up_vector;
    if (glm::dot(diff_vector, diff_vector) > 0.0001f) {
        up = glm::normalize(ws_current_up_vector + diff_vector * dt * 3.0f);
        ws_current_up_vector = up;
    }
        
    vector3_t camera_position = affected_player->ws_position + affected_player->size.x * up;
    if (is_third_person) {
        static bool was_entering = 1;

        camera_distance.animate(dt);
        fov.animate(dt);
        
        if (was_entering && !affected_player->is_entering) {
            // Swap the animation
            camera_distance.destination = distance_from_player;
            camera_distance.in_animation = 1;

            fov.destination = 1.0f;
            fov.in_animation = 1;
        }

        was_entering = affected_player->is_entering;
        
        camera_position += -camera_distance.current * affected_player->ws_direction * (1.0f - transition_first_third.current);

        if (initialized_previous_position) {
            // Check that camera isn't underneath the terrain
            collision_t collision = collide(camera_position, vector3_t(2.0f), camera_position - previous_position, 0, collision_t{});

            if (collision.under_terrain || collision.detected) {
                camera_position = collision.es_at * vector3_t(2.0f);
            }
            else {
                collision_t post_collision = collide(camera_position, vector3_t(1.5f), camera_position - previous_position, 0, collision_t{});
            }

            // Update camera position and previous camera position
            previous_position = camera_position;
        }
        else {
            previous_position = camera_position;
            initialized_previous_position = 1;
        }
    }

    if (transition_first_third.in_animation) {
        transition_first_third.animate(dt);

        output_to_debug_console(transition_first_third.current);
    }

    camera_ptr->current_fov = camera_ptr->fov * fov.current;

    camera_ptr->p = camera_position;
    camera_ptr->d = affected_player->ws_direction;
    camera_ptr->u = up;
    
    camera_ptr->v_m = glm::lookAt(camera_ptr->p, camera_ptr->p + camera_ptr->d, camera_ptr->u);

    // TODO: Don't need to calculate this every frame, just when parameters change
    camera_ptr->compute_projection();
}


// Animation
void animation_component_t::tick(player_t *affected_player, float32_t dt) {
    player_t::animated_state_t previous_state = affected_player->animated_state;
    
    player_t::animated_state_t new_state;
        
    uint32_t moving = 0;
        
    if (affected_player->action_flags & (1 << action_flags_t::ACTION_FORWARD)) {
        if (affected_player->action_flags & (1 << action_flags_t::ACTION_RUN)) {
            new_state = player_t::animated_state_t::RUN; moving = 1;
        }
        else {
            new_state = player_t::animated_state_t::WALK; moving = 1;
        }
    }
    //if (affected_player->action_flags & (1 << action_flags_t::ACTION_LEFT)); 
    //if (affected_player->action_flags & (1 << action_flags_t::ACTION_DOWN));
    //if (affected_player->action_flags & (1 << action_flags_t::ACTION_RIGHT));
        
    if (!moving) {
        new_state = player_t::animated_state_t::IDLE;
    }

    if (affected_player->is_sitting) {
        new_state = player_t::animated_state_t::SITTING;
    }

    if (affected_player->is_in_air) {
        new_state = player_t::animated_state_t::HOVER;
    }

    if (affected_player->is_sliding_not_rolling_mode) {
        new_state = player_t::animated_state_t::SLIDING_NOT_ROLLING_MODE;
    }

    if (new_state != previous_state) {
        affected_player->animated_state = new_state;
        switch_to_cycle(&animation_instance, new_state);
    }
        
    interpolate_skeleton_joints_into_instance(dt, &animation_instance);
}


// Rendering
void rendering_component_t::tick(player_t *affected_player, float32_t dt) {
    static const matrix4_t CORRECTION_90 = glm::rotate(glm::radians(180.0f), vector3_t(0.0f, 1.0f, 0.0f));

    movement_axes_t axes = compute_movement_axes(affected_player->ws_direction, affected_player->camera.ws_next_vector);
    matrix3_t normal_rotation_matrix3 = (matrix3_t(glm::normalize(axes.right), glm::normalize(axes.up), glm::normalize(-axes.forward)));
    matrix4_t normal_rotation_matrix4 = matrix4_t(normal_rotation_matrix3);
    normal_rotation_matrix4[3][3] = 1;
    
    vector3_t view_dir = glm::normalize(affected_player->ws_direction);
    float32_t dir_x = view_dir.x;
    float32_t dir_z = view_dir.z;
    float32_t rotation_angle = atan2(dir_z, dir_x);

    matrix4_t rot_matrix = glm::rotate(-rotation_angle, vector3_t(0, 1, 0));

    if (enabled) {
        if (affected_player->rolling_mode) {
            normal_rotation_matrix4 = affected_player->rolling_rotation;
        }
        
        push_k.ws_t = glm::translate(affected_player->ws_position) * normal_rotation_matrix4 * CORRECTION_90 * /*rot_matrix * */glm::scale(affected_player->size);
    }
    else {
        push_k.ws_t = matrix4_t(0.0f);
    }

    if (affected_player->camera.transition_first_third.in_animation) {
        push_k_alpha.fade = (1.0f - affected_player->camera.transition_first_third.current);
        push_k_alpha.ws_t = push_k.ws_t;
        push_k_alpha.color = push_k.color;

        if (affected_player->rolling_mode) {
            push_entity_to_rolling_alpha_queue(this);
            push_entity_to_rolling_shadow_queue(this);
        }
        else {
            push_entity_to_skeletal_animation_alpha_queue(this, &affected_player->animation);
            push_entity_to_skeletal_animation_shadow_queue(this, &affected_player->animation);
        }
    }
    else {
        if (player_t *main_player = get_user_player(); main_player) {
            // Always render the rolling player for main player
            if (main_player->index == affected_player->index) {
                if (affected_player->rolling_mode) {
                    push_entity_to_rolling_queue(this);
                    push_entity_to_rolling_shadow_queue(this);
                }
                else {
                    push_entity_to_skeletal_animation_shadow_queue(this, &affected_player->animation);
                }
            }
            else {
                // Don't push anything. Player will be in first person mode if in standing mode anyway.
                // Maybe just push to the shadowmap
                if (affected_player->rolling_mode) {
                    push_entity_to_rolling_queue(this);
                    push_entity_to_rolling_shadow_queue(this);
                }
                else {
                    push_entity_to_skeletal_animation_queue(this, &affected_player->animation);
                    push_entity_to_skeletal_animation_shadow_queue(this, &affected_player->animation);
                }
            }
        }
        else {
            if (affected_player->rolling_mode) {
                push_entity_to_rolling_queue(this);
                push_entity_to_rolling_shadow_queue(this);
            }
            else {
                push_entity_to_skeletal_animation_queue(this, &affected_player->animation);
                push_entity_to_skeletal_animation_shadow_queue(this, &affected_player->animation);
            }
        }
    }
}

void rendering_component_t::tick(bullet_t *affected_bullet, float32_t dt) {
    push_k.ws_t = glm::translate(affected_bullet->ws_position) * glm::scale(affected_bullet->size);

    push_entity_to_rolling_queue(this);
}


// Shooting
void shoot_component_t::tick(player_t *affected_player, float32_t dt) {
    if (affected_player->shoot.cool_off < affected_player->shoot.shoot_speed) {
        affected_player->shoot.cool_off += dt;
    }
    
    if (affected_player->action_flags & (1 << action_flags_t::SHOOT)) {
        if (affected_player->shoot.cool_off > affected_player->shoot.shoot_speed) {
            send_vibration_to_gamepad();
            
            spawn_bullet(affected_player);
            
            affected_player->shoot.cool_off = 0.0f;
        }
    }
}


// Burnable
void burnable_component_t::tick(bullet_t *affected_bullet, float32_t dt) {
    if (burning) {
        // Particle stuff
        particle_t *fire_particle = get_fire_particle(particle_index);

        fire_particle->ws_position = affected_bullet->ws_position;
    }
}

void burnable_component_t::set_on_fire(const vector3_t &position) {
    burning = 1;
    particle_index = spawn_fire(position);
}

void burnable_component_t::extinguish_fire(void) {
    burning = 0;
    declare_fire_dead(particle_index);
    particle_index = 0;
}


// Network
float32_t network_component_t::tick(player_t *affected_player, float32_t dt) {
    if (is_remote) {
        if (remote_player_states.head_tail_difference >= 3) {
            uint32_t previous_snapshot_index = remote_player_states.tail;
            uint32_t next_snapshot_index = remote_player_states.tail;
            if (++next_snapshot_index == remote_player_states.buffer_size) {
                next_snapshot_index = 0;
            }
        
            elapsed_time += dt;
            float32_t progression = elapsed_time / max_time;

            if (progression >= 1.0f) {
                elapsed_time = elapsed_time - max_time;
                remote_player_states.get_next_item();

                previous_snapshot_index = remote_player_states.tail;
                next_snapshot_index = remote_player_states.tail;
                if (++next_snapshot_index == remote_player_states.buffer_size) {
                    next_snapshot_index = 0;
                }

                progression -= 1.0f;
            }

            remote_player_snapshot_t *previous_remote_snapshot = &remote_player_states.buffer[previous_snapshot_index],
                *next_remote_snapshot = &remote_player_states.buffer[next_snapshot_index];

            affected_player->ws_position = interpolate(previous_remote_snapshot->ws_position, next_remote_snapshot->ws_position, progression);
            affected_player->ws_direction = interpolate(previous_remote_snapshot->ws_direction, next_remote_snapshot->ws_direction, progression);
            affected_player->ws_rotation = glm::mix(previous_remote_snapshot->ws_rotation, next_remote_snapshot->ws_rotation, progression);
            affected_player->camera.ws_next_vector = affected_player->ws_up = interpolate(previous_remote_snapshot->ws_up_vector, next_remote_snapshot->ws_up_vector, progression);

            if (true) {
                affected_player->action_flags = previous_remote_snapshot->action_flags;
                affected_player->rolling_mode = previous_remote_snapshot->rolling_mode;
            }
            else {
                affected_player->action_flags = next_remote_snapshot->action_flags;
                affected_player->rolling_mode = next_remote_snapshot->rolling_mode;
            }

            affected_player->rolling_rotation = glm::mat4_cast(affected_player->ws_rotation);
        }
    }
    else {
        // Basically sets the action flags, toggles rolling mode, etc...
        // Get next player_state_t

        // Is there any player states to burn through
        player_state_t *player_state = player_states_cbuffer.get_next_item();

        if (player_state) {
            output_to_debug_console("------------- state_count: ", (int32_t)player_state->current_state_count, "\n");
            output_to_debug_console("              BEFORE\n");
            output_to_debug_console("position: ",  affected_player->ws_position, "\n");
            output_to_debug_console("direction: ", affected_player->ws_direction, "\n");
            output_to_debug_console("velocity: ",  affected_player->ws_velocity, "\n");
            
            float32_t dt = player_state->dt;

            player_state_t *next_player_state = player_state;

            affected_player->action_flags = next_player_state->action_flags;
            affected_player->previous_action_flags = affected_player->action_flags;
            affected_player->rolling_mode = next_player_state->rolling_mode;

            // Update view direction with mouse differences
            vector3_t up = affected_player->camera.ws_current_up_vector;
        
            vector3_t res = affected_player->ws_direction;
            vector2_t d = vector2_t(next_player_state->mouse_x_diff, next_player_state->mouse_y_diff);

            affected_player->camera.mouse_diff = d;

            static constexpr float32_t SENSITIVITY = 15.0f;
        
            float32_t x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
            float32_t y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
                
            res = matrix3_t(glm::rotate(x_angle, up)) * res;
            vector3_t rotate_y = glm::cross(res, up);
            res = matrix3_t(glm::rotate(y_angle, rotate_y)) * res;

            res = glm::normalize(res);
            
            affected_player->ws_direction = res;

            return(dt);
        }
    }
    
    return(-1.0f);
}


void bounce_physics_component_t::tick(bullet_t *affected_bullet, float32_t dt) {
    affected_bullet->ws_velocity -= affected_bullet->ws_up * 14.81f * dt;

    // Project and test if is going to be within chunk zone
    vector3_t projected_limit = affected_bullet->ws_position + affected_bullet->ws_velocity * dt + affected_bullet->ws_velocity * affected_bullet->size;
    if (get_chunk_encompassing_point(ws_to_xs(projected_limit)) == nullptr) {
        affected_bullet->burnable.extinguish_fire();
        destroy_bullet(affected_bullet);
        return;
    }
    else {
        collision_t collision = collide(affected_bullet->ws_position, affected_bullet->size, affected_bullet->ws_velocity * dt, 0, {});

        if (collision.detected) {
            //vector3_t normal = glm::normalize(collision.es_normal * affected_bullet->size);
            // Reflect velocity vector
            //affected_bullet->ws_velocity = glm::reflect(affected_bullet->ws_velocity, normal);

            vector3_t collision_position = collision.es_at * affected_bullet->size;
            // EXPLODE !!!
            spawn_explosion(collision_position);
            terraform_client(ws_to_xs(collision_position), 2, 1, 1, 100.0f);

            affected_bullet->burnable.extinguish_fire();

            // TODO: HAVE EVENT SYSTEM
            destroy_bullet(affected_bullet);
        }
        else {
            affected_bullet->ws_position = collision.es_at * affected_bullet->size;
        }
    }
}
