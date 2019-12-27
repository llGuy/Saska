#pragma once

#include "graphics.hpp"
#include "utility.hpp"


void initialize_particles_state(void);
void tick_particles_state(float32_t dt);
void render_alpha_particles(uniform_group_t *uniforms, gpu_command_queue_t *queue);
void sync_gpu_with_particles_state(gpu_command_queue_t *queue);

int32_t spawn_fire(const vector3_t &position);
void declare_fire_dead(uint32_t index);
int32_t spawn_explosion(const vector3_t &position);

particle_t *get_fire_particle(uint32_t index);
