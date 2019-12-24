#pragma once

#include "utils.hpp"
#include "world.hpp"
#include "thread_pool.hpp"
#include "sockets.hpp"
#include "serializer.hpp"
#include "client.hpp"
#include "server.hpp"


#define MAX_CLIENTS 40
#define MAX_MESSAGE_BUFFER_SIZE 40000


constexpr uint16_t GAME_OUTPUT_PORT_CLIENT = 6001;
constexpr uint16_t GAME_OUTPUT_PORT_SERVER = 6000;


enum application_mode_t { CLIENT_MODE, SERVER_MODE };



float32_t get_snapshot_client_rate(void);
float32_t get_snapshot_server_rate(void);
application_mode_t get_app_mode(void);



void initialize_net(struct game_memory_t *memory, application_mode_t app_mode);
void tick_net(input_state_t *input_state, float32_t dt);
