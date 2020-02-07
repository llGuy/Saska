#include "ui.hpp"
#include "net.hpp"
#include "script.hpp"
#include "sockets.hpp"
#include "client.hpp"
#include "packets.hpp"
#include "serializer.hpp"
#include "chunk.hpp"
#include "chunks_gstate.hpp"
#include "entities_gstate.hpp"
#include "gamestate.hpp"

#include "game.hpp"


struct remote_client_t
{
    uint32_t client_id, player_handle;
    const char *name;
};


#define MAX_HISTORY_INSTANCES 20
// For the local client, for every send_commands() call, client program will add the voxel modifications to an array of local_client_voxel_modification_history. If the server forces a correction, client will traverse through the local_client_voxel_modification_history array and revert so that client is in sync with client (in case client terraformed while server was checking clients' state)
struct local_client_voxel_modification_history_t
{
    // The voxel modifications will actually store the "previous value"
    client_modified_chunk_nl_t modified_chunks[10];
    uint32_t modified_chunks_count = 0;
    uint64_t tick;
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
static circular_buffer_t<local_client_voxel_modification_history_t> vmod_history; // Voxel-modification history


static bool force_correction = 0;


// Static function declarations
static void join_loop_back(uint32_t client_index /* Will be the client name */);
void join_server(const char *ip_address, const char *client_name);

static int32_t lua_join_server(lua_State *state);
static int32_t lua_join_loop_back(lua_State *state);

static void send_commands(void);
static void push_pending_modified_voxels(void);
static void revert_voxels_against_modifications_until(uint64_t tick);




void client_event_callback(void *object, event_t *e)
{

}

// "Public" function definitions
void initialize_client(char *msg_buffer, event_dispatcher_t *dispatcher)
{
    message_buffer = msg_buffer;
    
    initialize_socket_api(GAME_OUTPUT_PORT_CLIENT);
    
    add_global_to_lua(script_primitive_type_t::FUNCTION, "join_server", &lua_join_server);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "lb", &lua_join_loop_back);

    player_state_cbuffer.initialize(40);
    vmod_history.initialize(MAX_HISTORY_INSTANCES);
}


#define MAX_MESSAGE_BUFFER_SIZE 40000


void tick_client(raw_input_t *raw_input, float32_t dt)
{
    if (raw_input->buttons[button_type_t::F].state != NOT_DOWN)
    {
        force_correction = 1;
    }
    else
    {
        force_correction = 0;
    }
    
    static float32_t time_since_last_input_state = 0.0f;

    // Send stuff out to the server (input state and stuff...)
    // TODO: Add changeable
    if (is_connected_to_server)
    {
        time_since_last_input_state += dt;
        float32_t max_time = 1.0f / get_snapshot_client_rate();
        
        if (time_since_last_input_state > max_time)
        {
            if (!force_correction)
            {
                send_commands();
            }
            else
            {
                player_t *user = get_user_player();

                uint32_t player_states_to_send = player_state_cbuffer.head_tail_difference;

                for (uint32_t i = 0; i < player_states_to_send; ++i)
                {
                    player_state_cbuffer.get_next_item();
                }
            }

            time_since_last_input_state = 0.0f;
        }
    }
    
    network_address_t received_address = {};
    bool received = receive_from(message_buffer, sizeof(char) * MAX_MESSAGE_BUFFER_SIZE, &received_address);

    bool just_did_correction = 0;
    
    if (received)
    {
        serializer_t in_serializer = {};
        in_serializer.data_buffer = (uint8_t *)message_buffer;
        in_serializer.data_buffer_size = MAX_MESSAGE_BUFFER_SIZE;

        packet_header_t header = {};
        in_serializer.deserialize_packet_header(&header);

        if (header.packet_mode == packet_mode_t::PM_SERVER_MODE)
        {
            switch(header.packet_type)
            {
            case server_packet_type_t::SPT_SERVER_HANDSHAKE:
                {

                    *get_current_tick() = header.current_tick;
                    
                    game_state_initialize_packet_t game_state_init_packet = {};
                    in_serializer.deserialize_game_state_initialize_packet(&game_state_init_packet);
                    
                    deinitialize_gamestate();
                    
                    populate_gamestate(&game_state_init_packet, raw_input);

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

                    // Need to go into 3D world focus
                    clear_and_request_focus(element_focus_t::WORLD_3D_ELEMENT_FOCUS);

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

                    chunks_state_flags_t *flags = get_chunks_state_flags();
                    
                    chunks_count.to_update_count = in_serializer.deserialize_uint32();
                    if (chunks_count.is_first)
                    {
                        flags->should_update_chunk_meshes_from_now = 0;
                        flags->chunks_received_to_update_count = 0;
                        // Don't update chunks meshes until voxels were received entirely
                        flags->chunks_to_be_received = chunks_count.count;
                    }
                    
                    uint32_t chunks_to_update = in_serializer.deserialize_uint32();
                    for (uint32_t i = 0; i < chunks_to_update; ++i)
                    {
                        voxel_chunk_values_packet_t packet = {};
                        in_serializer.deserialize_voxel_chunk_values_packet(&packet);
                        
                        chunk_t *chunk = *get_chunk(packet.chunk_coord_x, packet.chunk_coord_y, packet.chunk_coord_z);
                        memcpy(chunk->voxels, packet.voxels, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);

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
                    in_serializer.deserialize_game_snapshot_voxel_delta_packet(get_previous_voxel_delta_packet(), voxel_allocator);

                    // Put this in the history - VERY IMPORTANT IF NEED TO REVERT VOXELS
                    uint64_t previous_tick = in_serializer.deserialize_uint64();

                    client_modified_voxels_packet_t modified_voxels = {};
                    in_serializer.deserialize_client_modified_voxels_packet(&modified_voxels);

                    for (uint32_t i = 0; i < client_count; ++i)
                    {
                        // This is the player state to compare with what the server sent
                        game_snapshot_player_state_packet_t player_snapshot_packet = {};
                        in_serializer.deserialize_game_snapshot_player_state_packet(&player_snapshot_packet);

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
                                player->ws_position = player_snapshot_packet.ws_position;
                                player->ws_direction = player_snapshot_packet.ws_direction;

                                output_to_debug_console("position: ", player->ws_position, " direction: ", player->ws_direction, "\n");

                                player->ws_velocity = player_snapshot_packet.ws_velocity;
                                player->camera.ws_next_vector = player->camera.ws_current_up_vector = player->ws_up = player_snapshot_packet.ws_up_vector;
                                player->physics.state = (entity_physics_state_t)player_snapshot_packet.physics_state;
                                player->physics.previous_velocity = player_snapshot_packet.ws_previous_velocity;
                                player->physics.axes = vector3_t(0);
                                player->action_flags = 0;

                                just_did_correction = 1;
                            }

                            if (player_snapshot_packet.need_to_do_correction)
                            {
                                if (player_snapshot_packet.need_to_do_voxel_correction)
                                {
                                    // NEED TO DO VOXEL CORRECTION :
                                    // Step 1: Push voxel modifications that are currently pending
                                    // Step 2: Revert voxel state against all modifications until the "client->previous_client_tick"
                                    // Step 3: Correct all voxels


                                    
                                    // Step 1: Push pending voxel modifications
                                    push_pending_modified_voxels();
                                    
                                    // Step 2: Revert
                                    revert_voxels_against_modifications_until(previous_tick);

                                    // Step 3: Do correction
                                    for (uint32_t chunk = 0; chunk < modified_voxels.modified_chunk_count; ++chunk)
                                    {
                                        client_modified_chunk_t *modified_chunk_data = &modified_voxels.modified_chunks[chunk];
                                        chunk_t *actual_voxel_chunk = *get_chunk(modified_chunk_data->chunk_index);
                
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

                                    console_out("Did voxel correction\n");
                                    
                                    *get_current_tick() = previous_tick;
                                    client->just_received_correction = 1;
                                }
                                else
                                {
                                    // Send a prediction error correction packet
                                    *get_current_tick() = previous_tick;
                                    client->just_received_correction = 1;
                                }

                                just_did_correction = 1;
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
                    in_serializer.deserialize_player_state_initialize_packet(&new_client_init_packet);

                    player_t *user = get_user_player();

                    
                    // In case server sends init data to the user
                    if (new_client_init_packet.client_id != user->network.client_state_index)
                    {
                        ++client_count;
                        
                        player_handle_t new_player_handle = create_player_from_player_init_packet(user->network.client_state_index, &new_client_init_packet);

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
    player_state_t player_state = user->create_player_state();
    player_state.dt = dt;
    player_state.current_state_count = (get_current_player_state_count())++;

    player_state_cbuffer.push_item(&player_state);
}


void send_prediction_error_correction(uint64_t tick)
{
    serializer_t serializer = {};
    serializer.initialize(sizeof_packet_header() + sizeof(uint64_t));

    player_t *user = get_user_player();
    
    packet_header_t header = {};
    header.packet_mode = PM_CLIENT_MODE;
    header.packet_type = CPT_PREDICTION_ERROR_CORRECTION;
    header.total_packet_size = sizeof_packet_header() + sizeof(uint64_t);
    header.current_tick = tick;
    header.client_id = user->network.client_state_index;
    header.current_packet_id = ++(get_user_client()->current_packet_count);
    
    serializer.serialize_packet_header(&header);
    serializer.serialize_uint64(tick);

    serializer.send_serialized_message(server_address);

    *get_current_tick() = tick;
}


client_t *get_user_client(void)
{
    return &user_client;
}


void join_server(const char *ip_address, const char *client_name)
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

    header.current_packet_id = get_user_client()->current_packet_count = 0;

    serializer_t serializer = {};
    serializer.initialize(header.total_packet_size);
    serializer.serialize_packet_header(&header);
    serializer.serialize_client_join_packet(&packet);
    
    server_address = network_address_t{ (uint16_t)host_to_network_byte_order(GAME_OUTPUT_PORT_SERVER), str_to_ipv4_int32(ip_address) };
    serializer.send_serialized_message(server_address);
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
    header.current_packet_id = get_user_client()->current_packet_count = 0;


    serializer_t serializer = {};
    serializer.initialize(header.total_packet_size);
    serializer.serialize_packet_header(&header);
    serializer.serialize_client_join_packet(&packet);
    
    server_address = network_address_t{ (uint16_t)host_to_network_byte_order(GAME_OUTPUT_PORT_SERVER), str_to_ipv4_int32(ip_address) };
    serializer.send_serialized_message(server_address);
}


static void send_commands(void)
{
    player_t *user = get_user_player();    
    client_t *user_client = get_user_client();

    packet_header_t header = {};
    
    header.packet_mode = packet_mode_t::PM_CLIENT_MODE;
    header.packet_type = client_packet_type_t::CPT_INPUT_STATE;
    header.just_did_correction = user_client->just_received_correction;

    if (user_client->just_received_correction)
    {
        user_client->just_received_correction = 0;
    }
        
    uint32_t modified_chunks_count = 0;
    chunk_t **chunks = get_modified_chunks(&modified_chunks_count);

    client_modified_voxels_packet_t voxel_packet = {};
    voxel_packet.modified_chunk_count = modified_chunks_count;
    voxel_packet.modified_chunks = (client_modified_chunk_t *)allocate_linear(sizeof(client_modified_chunk_t) * modified_chunks_count);

    local_client_voxel_modification_history_t *history_instance = nullptr;
    
    if (modified_chunks_count)
    {
        history_instance = vmod_history.push_item();
        history_instance->tick = *get_current_tick();

        history_instance->modified_chunks_count = modified_chunks_count;
    }
    
    for (uint32_t chunk_index = 0; chunk_index < modified_chunks_count; ++chunk_index)
    {
        chunk_t *chunk = chunks[chunk_index];
        client_modified_chunk_t *modified_chunk = &voxel_packet.modified_chunks[chunk_index];

        modified_chunk->chunk_index = convert_3d_to_1d_index(chunk->chunk_coord.x, chunk->chunk_coord.y, chunk->chunk_coord.z, get_chunk_grid_size());
        modified_chunk->modified_voxels = (local_client_modified_voxel_t *)allocate_linear(sizeof(local_client_modified_voxel_t) * chunk->modified_voxels_list_count);
        modified_chunk->modified_voxel_count = chunk->modified_voxels_list_count;

        client_modified_chunk_nl_t *history_to_keep = &history_instance->modified_chunks[chunk_index];
        history_to_keep->chunk_index = modified_chunk->chunk_index;
        history_to_keep->modified_voxel_count = modified_chunk->modified_voxel_count;

        for (uint32_t voxel = 0; voxel < chunk->modified_voxels_list_count; ++voxel)
        {
            uint16_t voxel_index = chunk->list_of_modified_voxels[voxel];
            voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_index, CHUNK_EDGE_LENGTH);
            modified_chunk->modified_voxels[voxel].x = coord.x;
            modified_chunk->modified_voxels[voxel].y = coord.y;
            modified_chunk->modified_voxels[voxel].z = coord.z;
            modified_chunk->modified_voxels[voxel].value = chunk->voxels[coord.x][coord.y][coord.z];

            if (voxel < MAX_VOXELS_MODIFIED_PER_CHUNK)
            {
                history_to_keep->modified_voxels[voxel].x = coord.x;
                history_to_keep->modified_voxels[voxel].y = coord.y;
                history_to_keep->modified_voxels[voxel].z = coord.z;
                // Set it to the "previous" value so that revert is possible
                history_to_keep->modified_voxels[voxel].value = chunk->voxel_history[convert_3d_to_1d_index(coord.x, coord.y, coord.z, CHUNK_EDGE_LENGTH)];
            }
        }
    }
        
        
    header.total_packet_size = sizeof_packet_header() + sizeof(uint32_t) + sizeof_client_input_state_packet() * player_state_cbuffer.head_tail_difference + sizeof(vector3_t) * 2 + sizeof_modified_voxels_packet(modified_chunks_count, voxel_packet.modified_chunks);
    header.client_id = user->network.client_state_index;
    header.current_tick = *get_current_tick();
    header.current_packet_id = ++(get_user_client()->current_packet_count);

    serializer_t serializer = {};
    serializer.initialize(header.total_packet_size);
    
    serializer.serialize_packet_header(&header);

    serializer.serialize_uint32(player_state_cbuffer.head_tail_difference);

    uint32_t player_states_to_send = player_state_cbuffer.head_tail_difference;

    player_state_t *state;
    for (uint32_t i = 0; i < player_states_to_send; ++i)
    {
        state = player_state_cbuffer.get_next_item();
        serializer.serialize_client_input_state_packet(state);
    }

    player_state_t to_store = *state;
    to_store.ws_position = user->ws_position;
    to_store.ws_direction = user->ws_direction;
    to_store.tick = header.current_tick;

    serializer.serialize_vector3(to_store.ws_position);
    serializer.serialize_vector3(to_store.ws_direction);

    serializer.serialize_client_modified_voxels_packet(&voxel_packet);
        
    serializer.send_serialized_message(server_address);

    // 
    if (to_store.action_flags)
    {
        client_flags.sent_active_action_flags = 1;
    }

     clear_chunk_history();
}


static void serialize_header(serializer_t *serializer, client_packet_type_t type, uint32_t size, uint64_t tick, uint32_t client_id)
{
    
}


static void push_pending_modified_voxels(void)
{
    uint32_t modified_chunks_count = 0;
    chunk_t **chunks = get_modified_chunks(&modified_chunks_count);
    
    if (modified_chunks_count)
    {
        local_client_voxel_modification_history_t *history_instance = vmod_history.push_item();
        history_instance->tick = *get_current_tick();

        history_instance->modified_chunks_count = modified_chunks_count;
        
        for (uint32_t chunk_index = 0; chunk_index < modified_chunks_count; ++chunk_index)
        {
            chunk_t *chunk = chunks[chunk_index];
            client_modified_chunk_nl_t *history_to_keep = &history_instance->modified_chunks[chunk_index];
            
            for (uint32_t voxel = 0; voxel < chunk->modified_voxels_list_count; ++voxel)
            {
                uint16_t voxel_index = chunk->list_of_modified_voxels[voxel];
                voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_index, CHUNK_EDGE_LENGTH);
                
                if (voxel < MAX_VOXELS_MODIFIED_PER_CHUNK)
                {
                    history_to_keep->modified_voxels[voxel].x = coord.x;
                    history_to_keep->modified_voxels[voxel].y = coord.y;
                    history_to_keep->modified_voxels[voxel].z = coord.z;
                    // Set it to the "previous" value so that revert is possible
                    history_to_keep->modified_voxels[voxel].value = chunk->voxel_history[convert_3d_to_1d_index(coord.x, coord.y, coord.z, CHUNK_EDGE_LENGTH)];
                }
            }
        }

        clear_chunk_history();
    }
}


static void revert_voxels_against_modifications_until(uint64_t previous_tick)
{
    uint32_t current_history_instance_index = vmod_history.head;
    local_client_voxel_modification_history_t *instance = &vmod_history.buffer[current_history_instance_index];

    uint32_t i = 0;
    uint32_t max_loop = MAX_HISTORY_INSTANCES;
    while (i < max_loop && i < vmod_history.head_tail_difference)
    {
        // Decrement current_history_instance_index
        current_history_instance_index = vmod_history.decrement_index(current_history_instance_index);
        
        instance = &vmod_history.buffer[current_history_instance_index];

        for (uint32_t c = 0; c < instance->modified_chunks_count; ++c)
        {
            client_modified_chunk_nl_t *modified_chunk = &instance->modified_chunks[c];
            chunk_t *real_chunk = *get_chunk(modified_chunk->chunk_index);
            
            for (uint32_t v = 0 ; v < modified_chunk->modified_voxel_count; ++v)
            {
                local_client_modified_voxel_t *vptr = &modified_chunk->modified_voxels[v];
                // ->value = the "before" value of the voxel at tick
                real_chunk->voxels[vptr->x][vptr->y][vptr->z] = vptr->value;
            }
        }

        // We are finished
        if (instance->tick == previous_tick)
        {
            vmod_history.head_tail_difference = 0;
            vmod_history.head = 0;
            vmod_history.tail = 0;

            break;
        }
        
        ++i;
    }
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

static uint64_t current_player_state_count = 0;

uint64_t &get_current_player_state_count(void)
{
    return current_player_state_count;
}
