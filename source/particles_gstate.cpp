#include "game.hpp"
#include "graphics.hpp"
#include "particles_gstate.hpp"



// Global
static particle_spawner_t explosion_particle_spawner;
static particle_spawner_t fire_particle_spawner;



// Static declarations
static void particle_effect_explosion(particle_spawner_t *spawner, float32_t dt);
static void particle_effect_fire(particle_spawner_t *spawner, float32_t dt);



// Public definitions
void initialize_particles_state(void)
{
    switch (get_app_type())
    {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        uniform_layout_handle_t single_tx_layout_hdl = g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash);

        pipeline_handle_t explosion_shader_handle = initialize_particle_rendering_shader("pipeline.explosion_particle_effect"_hash, "shaders/SPV/explosion_particle.vert.spv", "shaders/SPV/explosion_particle.frag.spv", single_tx_layout_hdl);
        explosion_particle_spawner = initialize_particle_spawner(50, &particle_effect_explosion, explosion_shader_handle, 0.9f, "textures/particles/explosion.png", 4, 4, 14);
        fire_particle_spawner = initialize_particle_spawner(50, &particle_effect_fire, explosion_shader_handle, 2.0f, "textures/particles/smoke.png", 6, 5, 30);
    } break;

    default: break;
    }
}


void tick_particles_state(float32_t dt)
{
    switch (get_app_type())
    {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        explosion_particle_spawner.clear();
        {
            (*explosion_particle_spawner.update)(&explosion_particle_spawner, dt);
        }
        explosion_particle_spawner.sort_for_render();

        fire_particle_spawner.clear();
        {
            (*fire_particle_spawner.update)(&fire_particle_spawner, dt);
        }
        fire_particle_spawner.sort_for_render();
    } break;

    default: break;
    }
}


void render_alpha_particles(uniform_group_t *uniform_groups, gpu_command_queue_t *queue)
{
    struct particle_push_constant_t
    {
        float32_t max_time;
        uint32_t atlas_width;
        uint32_t atlas_height;
        uint32_t num_images;
    } explosion_pk, fire_pk;
    explosion_pk.max_time = explosion_particle_spawner.max_life_length;
    explosion_pk.atlas_width = explosion_particle_spawner.image_x_count;
    explosion_pk.atlas_height = explosion_particle_spawner.image_y_count;
    explosion_pk.num_images = explosion_particle_spawner.num_images;
    render_particles(queue, uniform_groups, &explosion_particle_spawner, &explosion_pk, sizeof(explosion_pk));

    fire_pk.max_time = fire_particle_spawner.max_life_length;
    fire_pk.atlas_width = fire_particle_spawner.image_x_count;
    fire_pk.atlas_height = fire_particle_spawner.image_y_count;
    fire_pk.num_images = fire_particle_spawner.num_images;
    render_particles(queue, uniform_groups, &fire_particle_spawner, &fire_pk, sizeof(fire_pk));
}


void sync_gpu_with_particles_state(gpu_command_queue_t *queue)
{
    update_gpu_buffer(&explosion_particle_spawner.gpu_particles_buffer, explosion_particle_spawner.rendered_particles, sizeof(rendered_particle_data_t) * explosion_particle_spawner.rendered_particles_stack_head, 0, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, &queue->q);

    update_gpu_buffer(&fire_particle_spawner.gpu_particles_buffer, fire_particle_spawner.rendered_particles, sizeof(rendered_particle_data_t) * fire_particle_spawner.rendered_particles_stack_head, 0, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, &queue->q);
}


int32_t spawn_fire(const vector3_t &position)
{
    uint32_t index = 0;
    particle_t *particle = fire_particle_spawner.particle(&index);
    particle->ws_position = position;
    particle->ws_velocity = vector3_t(0.0f);
    particle->life = 0.0f;
    particle->size = 3.0f;
    return(index);    
}


void declare_fire_dead(uint32_t index)
{
    fire_particle_spawner.declare_dead(index);
}


int32_t spawn_explosion(const vector3_t &position)
{
    uint32_t index;
    particle_t *particle = explosion_particle_spawner.particle(&index);
    particle->ws_position = position;
    particle->ws_velocity = vector3_t(0.0f);
    particle->life = 0.0f;
    particle->size = 15.0f;
    return index;
}


particle_t *get_fire_particle(uint32_t index)
{
    return &fire_particle_spawner.particles[index];
}



// Static definitions
static void particle_effect_fire(particle_spawner_t *spawner, float32_t dt)
{
    for (uint32_t i = 0; i < spawner->particles_stack_head; ++i)
    {
        particle_t *particle = &spawner->particles[i];

        if (particle->life >= 0.0f)
        {        
            if (particle->life < spawner->max_life_length)
            {
                float32_t direction = 1.0f;

                if (particle->flag0)
                {
                    direction = -1.0f;
                }
                
                particle->life += dt * direction;

                // Doesn't die unless from another source
                if (particle->life > spawner->max_life_length)
                {
                    particle->flag0 = 1;
                    particle->life -= dt * direction;
                }
                else if (particle->life < 0.0f)
                {
                    particle->flag0 = 0;
                    particle->life += dt * direction;
                }

                spawner->push_for_render(i);
            }
        }
    }
}


static void particle_effect_explosion(particle_spawner_t *spawner, float32_t dt)
{
    for (uint32_t i = 0; i < spawner->particles_stack_head; ++i)
    {
        particle_t *particle = &spawner->particles[i];

        if (particle->life >= 0)
        {
        
            if (particle->life < spawner->max_life_length)
            {
                particle->life += dt;
            
                if (particle->life > spawner->max_life_length)
                {
                    particle->life += 10.0f;
                    spawner->declare_dead(i);
                }
                else
                {
                    spawner->push_for_render(i);
                }
            }

        }
    }
}
