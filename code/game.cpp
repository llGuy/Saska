#include "world.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "script.hpp"
#include "ui.hpp"

void initialize_game(input_state_t *input_state, const graphics_api_initialize_ret_t &graphics)
{
    // ---- Initialize game data ----
    // Initialize atmosphere, shadow, skeletal animation...
    initialize_scripting();
    initialize_game_3d_graphics(graphics.command_pool);
    initialize_game_2d_graphics(graphics.command_pool);
    initialize_game_ui(graphics.command_pool, &g_uniform_pool, get_backbuffer_resolution());
    initialize_world(input_state, graphics.command_pool);
}

void destroy_game(void)
{
    // ---- destroy world data ----
    destroy_world();
}

void game_tick(input_state_t *input_state, float32_t dt)
{
    // ---- begin recording instructions into the command buffers ----
    frame_rendering_data_t frame = begin_frame_rendering();
    gpu_command_queue_t queue{frame.command_buffer};
    {
        update_world(input_state, dt, frame.image_index, 0 /* Don't need this argument */, &queue);

        dbg_handle_input(input_state);
        update_game_ui(get_pfx_framebuffer_hdl(), input_state);
        render_game_ui(get_pfx_framebuffer_hdl(), &queue);
        
        render_final_output(frame.image_index, &queue);
    }
    end_frame_rendering_and_refresh();
}
