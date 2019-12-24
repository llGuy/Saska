#include "ui.hpp"
#include "script.hpp"
#include "sockets.hpp"
#include "client.hpp"
#include "packets.hpp"
#include "network.hpp"
#include "serializer.hpp"


struct remote_client_t
{
    uint32_t client_id, player_handle;
    const char *name;
};


// Global
static bool is_connected_to_server = 0;
static network_address_t server_address;
static uint32_t client_count = 0;
static remote_client_t clients[MAX_CLIENTS];
static hash_table_inline_t<uint16_t, MAX_CLIENTS * 2, 3, 3> client_table_by_name;
static hash_table_inline_t<uint16_t, MAX_CLIENTS * 2, 3, 3> client_table_by_address;
static uint16_t client_id_stack[MAX_CLIENTS] = {};
static client_t user_client;
static circular_buffer_t<player_state_t> player_state_cbuffer;
static char *message_buffer = nullptr;
static struct
{
    uint32_t sent_active_action_flags: 1;
} client_flags;



// Static function declarations
static void join_loop_back(uint32_t client_index /* Will be the client name */);
static void join_server(const char *ip_address, const char *client_name);

static int32_t lua_join_server(lua_State *state);
static int32_t lua_join_loop_back(lua_State *state);

static void send_commands(void);



// "Public" function definitions
void initialize_client(char *msg_buffer)
{
    message_buffer = msg_buffer;
    
    initialize_socket_api(GAME_OUTPUT_PORT_CLIENT);
    
    add_global_to_lua(script_primitive_type_t::FUNCTION, "join_server", &lua_join_server);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "lb", &lua_join_loop_back);

    player_state_cbuffer.initialize(40);
}


#define MAX_MESSAGE_BUFFER_SIZE 40000


void tick_client(input_state_t *input_state, float32_t dt)
{
    static float32_t time_since_last_input_state = 0.0f;

    // Send stuff out to the server (input state and stuff...)
    // TODO: Add changeable
    if (is_connected_to_server)
    {
        time_since_last_input_state += dt;
        float32_t max_time = 1.0f / get_snapshot_client_rate();
        
        if (time_since_last_input_state > max_time)
        {
            send_commands();

            time_since_last_input_state = 0.0f;
        }
    }

    network_address_t received_address = {};
    bool received = receive_from(message_buffer, sizeof(char) * MAX_MESSAGE_BUFFER_SIZE, &received_address);

    if (received)
    {
        serializer_t in_serializer = {};
        in_serializer.data_buffer = (uint8_t *)message_buffer;
        in_serializer.data_buffer_size = MAX_MESSAGE_BUFFER_SIZE;

        packet_header_t header = {};
        deserialize_packet_header(&in_serializer, &header);

        if (header.packet_mode == packet_mode_t::PM_SERVER_MODE)
        {
            switch(header.packet_type)
            {
            case server_packet_type_t::SPT_SERVER_HANDSHAKE:
                {

                    *get_current_tick() = header.current_tick;
                    
                    game_state_initialize_packet_t game_state_init_packet = {};
                    deserialize_game_state_initialize_packet(&in_serializer, &game_state_init_packet);
                    
                    deinitialize_world();
                    
                    initialize_world(&game_state_init_packet, input_state);

                    // Add client structs (indices of the structs on the server will be the same as on the client)
                    client_count = game_state_init_packet.player_count;

                    // Existing players when client joined the game
                    for (uint32_t i = 0; i < game_state_init_packet.player_count; ++i)
                    { 
                        player_state_initialize_packet_t *player_packet = &game_state_init_packet.player[i];
                        player_t *player = get_player(player_packet->player_name);

                        remote_client_t *client = &clients[player_packet->client_id];
                        client->name = player->id.str;
                        client->client_id = player_packet->client_id;
                        client->player_handle = player->index;

                        if (client->client_id != game_state_init_packet.client_index)
                        {
                            player->network.is_remote = 1;
                            player->network.max_time = 1.0f / get_snapshot_server_rate();
                        }
                        else
                        {
                            user_client.name = player->id.str;
                            user_client.client_id = player_packet->client_id;
                            user_client.player_handle = player->index;
                        }
                    }

                    is_connected_to_server = 1;

                } break;
            case server_packet_type_t::SPT_CHUNK_VOXELS_HARD_UPDATE:
                {

                    union
                    {
                        struct
                        {
                            uint32_t is_first: 1;
                            uint32_t count: 31;
                        };
                        uint32_t to_update_count;
                    } chunks_count;

                    voxel_chunks_flags_t *flags = get_voxel_chunks_flags();
                    
                    chunks_count.to_update_count = deserialize_uint32(&in_serializer);
                    if (chunks_count.is_first)
                    {
                        flags->should_update_chunk_meshes_from_now = 0;
                        flags->chunks_received_to_update_count = 0;
                        // Don't update chunks meshes until voxels were received entirely
                        flags->chunks_to_be_received = chunks_count.count;
                    }
                    
                    uint32_t chunks_to_update = deserialize_uint32(&in_serializer);
                    for (uint32_t i = 0; i < chunks_to_update; ++i)
                    {
                        voxel_chunk_values_packet_t packet = {};
                        deserialize_voxel_chunk_values_packet(&in_serializer, &packet);
                        
                        voxel_chunk_t *chunk = *get_voxel_chunk(packet.chunk_coord_x, packet.chunk_coord_y, packet.chunk_coord_z);
                        memcpy(chunk->voxels, packet.voxels, sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);

                        ready_chunk_for_gpu_sync(chunk);
                    }

                    flags->chunks_received_to_update_count += chunks_to_update;

                    if (flags->chunks_received_to_update_count == flags->chunks_to_be_received)
                    {
                        flags->should_update_chunk_meshes_from_now = 1;
                    }
                    
                } break;
            case server_packet_type_t::SPT_GAME_STATE_SNAPSHOT:
                {
                    linear_allocator_t *voxel_allocator = get_voxel_linear_allocator();
                    reset_voxel_interpolation();
                    deserialize_game_snapshot_voxel_delta_packet(&in_serializer, get_previous_voxel_delta_packet(), voxel_allocator);

                    // Put this in the history
                    uint64_t previous_tick = deserialize_uint64(&in_serializer);

                    client_modified_voxels_packet_t modified_voxels = {};
                    deserialize_client_modified_voxels_packet(&in_serializer, &modified_voxels);

                    for (uint32_t i = 0; i < client_count; ++i)
                    {
                        // This is the player state to compare with what the server sent
                        game_snapshot_player_state_packet_t player_snapshot_packet = {};
                        deserialize_game_snapshot_player_state_packet(&in_serializer, &player_snapshot_packet);

                        remote_client_t *rclient = &clients[player_snapshot_packet.client_id];
                        player_t *current_player = get_player(rclient->player_handle);

                        player_t *local_user = get_user_player();

                        // We are dealing with the local client: we need to deal with the correction stuff
                        if (local_user->network.client_state_index == player_snapshot_packet.client_id && !player_snapshot_packet.is_to_ignore)
                        {
                            client_t *client = &user_client;
                            
                            client->previous_client_tick = previous_tick;
                            
                            if (player_snapshot_packet.need_to_do_correction)
                            {
                                // Do correction here
                                // TODO: THIS NEEDS TO HAPPEN IN UPDATE_NETWORK_COMPONENT() WHEN THE VOXEL CORRECTIONS HAPPEN
                                player_t *player = get_user_player();
                                player->ws_p = player_snapshot_packet.ws_position;
                                player->ws_d = player_snapshot_packet.ws_direction;
                                player->ws_v = player_snapshot_packet.ws_velocity;
                                player->camera.ws_next_vector = player->camera.ws_current_up_vector = player->ws_up = player_snapshot_packet.ws_up_vector;
                            }

                            if (player_snapshot_packet.need_to_do_correction)
                            {
                                if (player_snapshot_packet.need_to_do_voxel_correction)
                                {
                                    // Do voxel correction
                                    for (uint32_t chunk = 0; chunk < modified_voxels.modified_chunk_count; ++chunk)
                                    {
                                        client_modified_chunk_t *modified_chunk_data = &modified_voxels.modified_chunks[chunk];
                                        voxel_chunk_t *actual_voxel_chunk = *get_voxel_chunk(modified_chunk_data->chunk_index);
                
                                        for (uint32_t voxel = 0; voxel < modified_chunk_data->modified_voxel_count; ++voxel)
                                        {
                                            local_client_modified_voxel_t *voxel_ptr = &modified_chunk_data->modified_voxels[voxel];
                                            uint8_t actual_voxel_value = actual_voxel_chunk->voxels[voxel_ptr->x][voxel_ptr->y][voxel_ptr->z];

                                            // Needs to be corrected
                                            if (voxel_ptr->value != 255)
                                            {
                                                // voxel_ptr->value contains the real value that the server has
                                                actual_voxel_chunk->voxels[voxel_ptr->x][voxel_ptr->y][voxel_ptr->z] = voxel_ptr->value;
                                            }
                                        }
                                    }
                                    
                                    send_prediction_error_correction(previous_tick);
                                }
                                else
                                {
                                    // Send a prediction error correction packet
                                    send_prediction_error_correction(previous_tick);
                                }

                                // The correction of the voxels will happen later (deffered, but it will happen)
                            }

                            // Copy voxel data to client_t struct
                            // Voxel correction gets deferred to update_chunks_from_network if the flag need to do voxel correction is 1
                            client->modified_chunks_count = modified_voxels.modified_chunk_count;
                            for (uint32_t i = 0; i < client->modified_chunks_count; ++i)
                            {
                                client_modified_chunk_nl_t *chunk = &client->previous_received_voxel_modifications[i];
                                chunk->chunk_index = modified_voxels.modified_chunks[i].chunk_index;
                                chunk->modified_voxel_count = modified_voxels.modified_chunks[i].modified_voxel_count;
                                for (uint32_t voxel = 0; voxel < chunk->modified_voxel_count; ++voxel)
                                {
                                    chunk->modified_voxels[voxel].x = modified_voxels.modified_chunks[i].modified_voxels[voxel].x;
                                    chunk->modified_voxels[voxel].y = modified_voxels.modified_chunks[i].modified_voxels[voxel].y;
                                    chunk->modified_voxels[voxel].z = modified_voxels.modified_chunks[i].modified_voxels[voxel].z;
                                    chunk->modified_voxels[voxel].value = modified_voxels.modified_chunks[i].modified_voxels[voxel].value;
                                }
                            }
                        }
                        // We are dealing with a remote client: we need to deal with entity interpolation stuff
                        else
                        {
                            remote_player_snapshot_t remote_player_snapshot = {};
                            remote_player_snapshot.ws_position = player_snapshot_packet.ws_position;
                            remote_player_snapshot.ws_direction = player_snapshot_packet.ws_direction;
                            remote_player_snapshot.ws_rotation = player_snapshot_packet.ws_rotation;
                            remote_player_snapshot.action_flags = player_snapshot_packet.action_flags;
                            remote_player_snapshot.rolling_mode = player_snapshot_packet.is_rolling;
                            remote_player_snapshot.ws_up_vector = player_snapshot_packet.ws_up_vector;
                            current_player->network.remote_player_states.push_item(&remote_player_snapshot);
                        }
                    }
                    
                } break;
            case server_packet_type_t::SPT_CLIENT_JOINED:
                {
                    player_state_initialize_packet_t new_client_init_packet = {};
                    deserialize_player_state_initialize_packet(&in_serializer, &new_client_init_packet);

                    player_t *user = get_user_player();

                    
                    // In case server sends init data to the user
                    if (new_client_init_packet.client_id != user->network.client_state_index)
                    {
                        ++client_count;
                        
                        player_handle_t new_player_handle = initialize_player_from_player_init_packet(user->network.client_state_index, &new_client_init_packet);

                        player_t *player = get_player(new_player_handle);

                        // Sync the network.cpp's client data with the world.cpp's player data
                        remote_client_t *rclient = &clients[new_client_init_packet.client_id];
                        rclient->name = player->id.str;
                        rclient->client_id = new_client_init_packet.client_id;
                        rclient->player_handle = player->index;

                        player->network.is_remote = 1;
                        player->network.max_time = 1.0f / get_snapshot_server_rate();

                        console_out(rclient->name, " joined the game!\n");
                    }
                } break;
            }
        }
    }
}


void cache_player_state(float32_t dt)
{
    player_t *user = get_user_player();
    player_state_t player_state = initialize_player_state(user);
    player_state.dt = dt;

    player_state_cbuffer.push_item(&player_state);
}


void send_prediction_error_correction(uint64_t tick)
{
    serializer_t serializer = {};
    initialize_serializer(&serializer, sizeof_packet_header() + sizeof(uint64_t));

    player_t *user = get_user_player();
    
    packet_header_t header = {};
    header.packet_mode = PM_CLIENT_MODE;
    header.packet_type = CPT_PREDICTION_ERROR_CORRECTION;
    header.total_packet_size = sizeof_packet_header() + sizeof(uint64_t);
    header.current_tick = tick;
    header.client_id = user->network.client_state_index;
    
    serialize_packet_header(&serializer, &header);
    serialize_uint64(&serializer, tick);

    send_serialized_message(&serializer, server_address);

    *get_current_tick() = tick;
}


client_t *get_user_client(void)
{
    return &user_client;
}



// Static function definitions
static void join_loop_back(uint32_t client_index /* Will be the client name */)
{
    const char *ip_address = "127.0.0.1";
    
    char client_name[10];
    sprintf(client_name, "%d", client_index);
    
    packet_header_t header = {};
    client_join_packet_t packet = {};
    {
        header.packet_mode = packet_mode_t::PM_CLIENT_MODE;
        header.packet_type = client_packet_type_t::CPT_CLIENT_JOIN;
        packet.client_name = client_name;
    }
    header.total_packet_size = sizeof_packet_header();
    header.total_packet_size += strlen(packet.client_name) + 1;

    serializer_t serializer = {};
    initialize_serializer(&serializer, header.total_packet_size);
    serialize_packet_header(&serializer, &header);
    serialize_client_join_packet(&serializer, &packet);
    
    server_address = network_address_t{ (uint16_t)host_to_network_byte_order(GAME_OUTPUT_PORT_SERVER), str_to_ipv4_int32(ip_address) };
    send_serialized_message(&serializer, server_address);
}


static void join_server(const char *ip_address, const char *client_name)
{
    packet_header_t header = {};
    client_join_packet_t packet = {};
    {
        header.packet_mode = packet_mode_t::PM_CLIENT_MODE;
        header.packet_type = client_packet_type_t::CPT_CLIENT_JOIN;
        packet.client_name = client_name;
    }
    header.total_packet_size = sizeof_packet_header();
    header.total_packet_size += strlen(packet.client_name) + 1;

    serializer_t serializer = {};
    initialize_serializer(&serializer, header.total_packet_size);
    serialize_packet_header(&serializer, &header);
    serialize_client_join_packet(&serializer, &packet);
    
    server_address = network_address_t{ (uint16_t)host_to_network_byte_order(GAME_OUTPUT_PORT_SERVER), str_to_ipv4_int32(ip_address) };
    send_serialized_message(&serializer, server_address);
}


static void send_commands(void)
{
    player_t *user = get_user_player();    

    packet_header_t header = {};
    
    header.packet_mode = packet_mode_t::PM_CLIENT_MODE;
    header.packet_type = client_packet_type_t::CPT_INPUT_STATE;

        
    uint32_t modified_chunks_count = 0;
    voxel_chunk_t **chunks = get_modified_voxel_chunks(&modified_chunks_count);

    client_modified_voxels_packet_t voxel_packet = {};
    voxel_packet.modified_chunk_count = modified_chunks_count;
    voxel_packet.modified_chunks = (client_modified_chunk_t *)allocate_linear(sizeof(client_modified_chunk_t) * modified_chunks_count);

    for (uint32_t chunk_index = 0; chunk_index < modified_chunks_count; ++chunk_index)
    {
        voxel_chunk_t *chunk = chunks[chunk_index];
        client_modified_chunk_t *modified_chunk = &voxel_packet.modified_chunks[chunk_index];

        modified_chunk->chunk_index = convert_3d_to_1d_index(chunk->chunk_coord.x, chunk->chunk_coord.y, chunk->chunk_coord.z, get_chunk_grid_size());
        modified_chunk->modified_voxels = (local_client_modified_voxel_t *)allocate_linear(sizeof(local_client_modified_voxel_t) * chunk->modified_voxels_list_count);
        modified_chunk->modified_voxel_count = chunk->modified_voxels_list_count;
        for (uint32_t voxel = 0; voxel < chunk->modified_voxels_list_count; ++voxel)
        {
            uint16_t voxel_index = chunk->list_of_modified_voxels[voxel];
            voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_index, VOXEL_CHUNK_EDGE_LENGTH);
            modified_chunk->modified_voxels[voxel].x = coord.x;
            modified_chunk->modified_voxels[voxel].y = coord.y;
            modified_chunk->modified_voxels[voxel].z = coord.z;
            modified_chunk->modified_voxels[voxel].value = chunk->voxels[coord.x][coord.y][coord.z];
        }
    }
        
        
    header.total_packet_size = sizeof_packet_header() + sizeof(uint32_t) + sizeof_client_input_state_packet() * player_state_cbuffer.head_tail_difference + sizeof(vector3_t) * 2 + sizeof_modified_voxels_packet(modified_chunks_count, voxel_packet.modified_chunks);
    header.client_id = user->network.client_state_index;
    header.current_tick = *get_current_tick();

    serializer_t serializer = {};
    initialize_serializer(&serializer, header.total_packet_size);
    
    serialize_packet_header(&serializer, &header);

    serialize_uint32(&serializer, player_state_cbuffer.head_tail_difference);

    uint32_t player_states_to_send = player_state_cbuffer.head_tail_difference;

    player_state_t *state;
    for (uint32_t i = 0; i < player_states_to_send; ++i)
    {
        state = player_state_cbuffer.get_next_item();
        serialize_client_input_state_packet(&serializer, state);
    }

    player_state_t to_store = *state;
    to_store.ws_position = user->ws_p;
    to_store.ws_direction = user->ws_d;
    to_store.tick = header.current_tick;

    serialize_vector3(&serializer, to_store.ws_position);
    serialize_vector3(&serializer, to_store.ws_direction);

    serialize_client_modified_voxels_packet(&serializer, &voxel_packet);
        
    send_serialized_message(&serializer, server_address);

    // 
    if (to_store.action_flags)
    {
        client_flags.sent_active_action_flags = 1;
    }

    clear_chunk_history_for_server();
}


static int32_t lua_join_server(lua_State *state)
{
    const char *ip_address = lua_tostring(state, -2);
    const char *user_name = lua_tostring(state, -1);

    join_server(ip_address, user_name);

    return(0);
}

static int32_t lua_join_loop_back(lua_State *state)
{
    int32_t index = lua_tonumber(state, -1);
    
    join_loop_back(index);

    return(0);
}
