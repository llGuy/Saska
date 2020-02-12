#pragma once

#include "graphics.hpp"
#include "lighting.hpp"

void initialize_atmosphere(sun_t *light_pos, uint32_t light_count);
void update_atmosphere(sun_t *light_pos, uint32_t light_count, gpu_command_queue_t *queue);
void render_atmosphere(uniform_group_t *camera_transforms, const vector3_t &camera_pos, gpu_command_queue_t *queue);

uniform_group_t *atmosphere_diffuse_uniform();
uniform_group_t *atmosphere_irradiance_uniform();
uniform_group_t *atmosphere_prefiltered_uniform();
uniform_group_t *atmosphere_integrate_lookup_uniform();
