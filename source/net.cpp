// TODO: Find out why the hell the "next_value" of the voxels sent to the client not match the seemingly actual value of the voxels!!!

// TODO: Make sure that packet header's total packet size member matches the actual amount of bytes received by the socket

#include <cassert>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "net.hpp"
#include "game.hpp"
#include "ui.hpp"
#include "script.hpp"
#include "core.hpp"
#include "thread_pool.hpp"



// Global
static application_mode_t current_app_mode;
static float32_t server_game_state_snapshot_rate = 20.0f;
static float32_t client_input_snapshot_rate = 25.0f;
static char message_buffer[MAX_MESSAGE_BUFFER_SIZE] = {};



// Public function definitions
float32_t get_snapshot_server_rate(void)
{
    return server_game_state_snapshot_rate;
}


float32_t get_snapshot_client_rate(void)
{
    return client_input_snapshot_rate;
}


application_mode_t get_app_mode(void)
{
    return current_app_mode;
}


void tick_net(raw_input_t *raw_input, float32_t dt)
{
    switch(current_app_mode)
    {
    case application_mode_t::CLIENT_MODE: { tick_client(raw_input, dt); } break;
    case application_mode_t::SERVER_MODE: { tick_server(raw_input, dt); } break;
    }
}

void initialize_net(application_mode_t app_mode, event_dispatcher_t *dispatcher)
{
    current_app_mode = app_mode;

    switch(current_app_mode)
    {
    case application_mode_t::CLIENT_MODE: { initialize_client(message_buffer, dispatcher); } break;
    case application_mode_t::SERVER_MODE: { initialize_server(message_buffer); } break;
    }
}

void deinitialize_net()
{
    switch(current_app_mode)
    {
    case application_mode_t::CLIENT_MODE: { deinitialize_client(); } break;
    }
}
