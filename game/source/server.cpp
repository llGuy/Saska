#include "gamestate.hpp"
#include "entities_gstate.hpp"
#include "packets.hpp"
#include "server.hpp"
#include "thread_pool.hpp"
#include "net.hpp"
#include "serializer.hpp"
#include "ui.hpp"

#include "chunks_gstate.hpp"
#include "chunk.hpp"


#define MAX_RECEIVED_PACKETS_IN_QUEUE 60

struct receiver_thread_t
{
    linear_allocator_t packet_allocator = {};
    thread_process_t process;

    // Pointers into the packet allocator
    uint32_t packet_count;
    void *packets[MAX_RECEIVED_PACKETS_IN_QUEUE];
    uint32_t packet_sizes[MAX_RECEIVED_PACKETS_IN_QUEUE];
    network_address_t addresses[MAX_RECEIVED_PACKETS_IN_QUEUE];
    
    // Mutex
    mutex_t *mutex;

    uint32_t receiver_thread_loop_count = 0;

    bool receiver_freezed = 0;
};



// Global
static uint32_t client_count = {};
static client_t clients[MAX_CLIENTS] = {};
static hash_table_inline_t<uint16_t, MAX_CLIENTS * 2, 3, 3> client_table_by_name;
static hash_table_inline_t<uint16_t, MAX_CLIENTS * 2, 3, 3> client_table_by_address;
static uint16_t client_id_stack[MAX_CLIENTS] = {};
static receiver_thread_t receiver_thread;
static char *message_buffer;



// Static function declarations
static void receiver_thread_process(void *receiver_thread_data);
static void initialize_receiver_thread(void);
static client_t *get_client(uint32_t index);
static void send_chunks_hard_update_packets(network_address_t address);
static void dispatch_newcoming_client_to_clients(uint32_t new_client_index);
static void dispatch_snapshot_to_clients(void);



// Public functions
void initialize_server(char *msg_buffer)
{
    message_buffer = msg_buffer;
    
    initialize_socket_api(GAME_OUTPUT_PORT_SERVER);
}


void tick_server(raw_input_t *raw_input, float32_t dt)
{
    //if (wait_for_mutex_and_own(receiver_thread.mutex))
    {
        // Send Snapshots (25 per second)
        // Every 40ms (25 sps) - basically every frame
        // TODO: Add function to send snapshot
        static float32_t time_since_previous_snapshot = 0.0f;
    
        // Send stuff out to the clients (game state and stuff...)
        // TODO: Add changeable
    
        time_since_previous_snapshot += dt;
        float32_t max_time = 1.0f / get_snapshot_server_rate(); // 20 per second
        
        if (time_since_previous_snapshot > max_time)
        {
            // Dispath game state to all clients
            dispatch_snapshot_to_clients();
            
            time_since_previous_snapshot = 0.0f;
        }
        
        receiver_thread.receiver_thread_loop_count = 0;

        //        for (uint32_t packet_index = 0; packet_index < receiver_thread.packet_count; ++packet_index)
        for (uint32_t i = 0; i < 1 + 2 * client_count; ++i)
        {
            //network_address_t received_address = receiver_thread.addresses[packet_index];
            
            //serializer_t in_serializer = {};
            //in_serializer.data_buffer = (uint8_t *)(receiver_thread.packets[packet_index]);
            //in_serializer.data_buffer_size = receiver_thread.packet_sizes[packet_index];

            network_address_t received_address = {};
            int32_t bytes_received = receive_from(message_buffer, MAX_MESSAGE_BUFFER_SIZE, &received_address);

            if (bytes_received > 0)
            {
                serializer_t in_serializer = {};
                in_serializer.data_buffer = (uint8_t *)message_buffer;
                in_serializer.data_buffer_size = bytes_received;
                
                packet_header_t header = {};
                in_serializer.deserialize_packet_header(&header);

                if (header.total_packet_size == in_serializer.data_buffer_size)
                {
                    if (header.packet_mode == packet_mode_t::PM_CLIENT_MODE)
                    {
                        switch (header.packet_type)
                        {
                        case client_packet_type_t::CPT_CLIENT_JOIN:
                            {

                                client_join_packet_t client_join = {};
                                in_serializer.deserialize_client_join_packet(&client_join);

                                // Add client
                                client_t *client = get_client(client_count);
                                client->name = client_join.client_name;
                                client->client_id = client_count;
                                client->network_address = received_address;
                                client->current_packet_count = 0;

                                // Add the player to the actual entities list (spawn the player in the world)
                                client->player_handle = spawn_player(client->name, player_color_t::GRAY, client->client_id);
                                player_t *local_player_data_ptr = get_player(client->player_handle);
                                local_player_data_ptr->network.client_state_index = client->client_id;

                                ++client_count;
                                constant_string_t constant_str_name = make_constant_string(client_join.client_name, strlen(client_join.client_name));
                                client_table_by_name.insert(constant_str_name.hash, client->client_id);
                                client_table_by_address.insert(received_address.ipv4_address, client->client_id);
                        
                                // LOG:
                                console_out(client_join.client_name);
                                console_out(" joined the game!\n");
                        
                                // Reply - Create handshake packet
                                serializer_t out_serializer = {};
                                out_serializer.initialize(2000);
                                game_state_initialize_packet_t game_state_init_packet = {};
                                fill_game_state_initialize_packet(&game_state_init_packet, client->client_id);


                        
                                packet_header_t handshake_header = {};
                                handshake_header.packet_mode = packet_mode_t::PM_SERVER_MODE;
                                handshake_header.packet_type = server_packet_type_t::SPT_SERVER_HANDSHAKE;
                                handshake_header.total_packet_size = sizeof_packet_header();
                                handshake_header.total_packet_size += sizeof(voxel_state_initialize_packet_t);
                                handshake_header.total_packet_size += sizeof(game_state_initialize_packet_t::client_index) + sizeof(game_state_initialize_packet_t::player_count);
                                handshake_header.total_packet_size += sizeof(player_state_initialize_packet_t) * game_state_init_packet.player_count;
                                handshake_header.current_tick = *get_current_tick();
                        
                                out_serializer.serialize_packet_header(&handshake_header);
                                out_serializer.serialize_game_state_initialize_packet(&game_state_init_packet);
                                out_serializer.send_serialized_message(client->network_address);

                                send_chunks_hard_update_packets(client->network_address);

                                dispatch_newcoming_client_to_clients(client->client_id);
                        
                            } break;

                        case client_packet_type_t::CPT_INPUT_STATE:
                            {
                                client_t *client = get_client(header.client_id);
                                if (!client->needs_to_acknowledge_prediction_error)
                                {
                                    client->received_input_commands = 1;
                        
                                    // Current client tick (will be used for the snapshot that will be sent to the clients)
                                    // Clients will compare the state at the tick that the server recorded as being the last client tick at which server received input state (commands)
                                    client->previous_client_tick = header.current_tick;

                                    player_t *player = get_player(client->player_handle);
                        
                                    uint32_t player_state_count = in_serializer.deserialize_uint32();

                                    player_state_t last_player_state = {};
                        
                                    for (uint32_t i = 0; i < player_state_count; ++i)
                                    {
                                        client_input_state_packet_t input_packet = {};
                                        player_state_t player_state = {};
                                        in_serializer.deserialize_client_input_state_packet(&input_packet);

                                        player_state.action_flags = input_packet.action_flags;
                                        player_state.mouse_x_diff = input_packet.mouse_x_diff;
                                        player_state.mouse_y_diff = input_packet.mouse_y_diff;
                                        player_state.flags_byte = input_packet.flags_byte;
                                        player_state.dt = input_packet.dt;

                                        player->network.player_states_cbuffer.push_item(&player_state);

                                        last_player_state = player_state;
                                    }

                                    // Will use the data in here to check whether the client needs correction or not
                                    client->previous_received_player_state = last_player_state;

                                    client->previous_received_player_state.ws_position = in_serializer.deserialize_vector3();
                                    client->previous_received_player_state.ws_direction = in_serializer.deserialize_vector3();

                                    player->network.commands_to_flush += player_state_count;

                                    client_modified_voxels_packet_t voxel_packet = {};
                                    in_serializer.deserialize_client_modified_voxels_packet(&voxel_packet);

                                    for (uint32_t i = 0; i < voxel_packet.modified_chunk_count; ++i)
                                    {
                                        client_modified_chunk_nl_t *chunk = &client->previous_received_voxel_modifications[i + client->modified_chunks_count];
                                        chunk->chunk_index = voxel_packet.modified_chunks[i].chunk_index;
                                        chunk->modified_voxel_count = voxel_packet.modified_chunks[i].modified_voxel_count;
                                        for (uint32_t voxel = 0; voxel < chunk->modified_voxel_count && voxel < 80; ++voxel)
                                        {
                                            chunk->modified_voxels[voxel].x = voxel_packet.modified_chunks[i].modified_voxels[voxel].x;
                                            chunk->modified_voxels[voxel].y = voxel_packet.modified_chunks[i].modified_voxels[voxel].y;
                                            chunk->modified_voxels[voxel].z = voxel_packet.modified_chunks[i].modified_voxels[voxel].z;
                                            chunk->modified_voxels[voxel].value = voxel_packet.modified_chunks[i].modified_voxels[voxel].value;
                                        }
                                    }
                                    client->modified_chunks_count += voxel_packet.modified_chunk_count;
                                }
                            } break;
                        case client_packet_type_t::CPT_PREDICTION_ERROR_CORRECTION:
                            {
                                client_t *client = get_client(header.client_id);
                                client->needs_to_acknowledge_prediction_error = 0;

                                player_t *player = get_player(client->player_handle);

                                client->previous_client_tick = in_serializer.deserialize_uint64();
                                //client->received_input_commands = 0;
                            } break;
                        case client_packet_type_t::CPT_ACKNOWLEDGED_GAME_STATE_RECEPTION:
                            {
                                uint64_t game_state_acknowledged_tick = in_serializer.deserialize_uint64();
                                client_t *client = get_client(header.client_id);
                        
                            } break;
                        }
                    }
                }
            }
        }

        //receiver_thread.packet_count = 0;
        //clear_linear(&receiver_thread.packet_allocator);
        //release_mutex(receiver_thread.mutex);
    }
}



// Static function definitions
static void receiver_thread_process(void *receiver_thread_data)
{
    output_to_debug_console("Started receiver thread\n");
    
    receiver_thread_t *process_data = (receiver_thread_t *)receiver_thread_data;

    for (;;)
    {
        if (wait_for_mutex_and_own(process_data->mutex) && !process_data->receiver_freezed)
        {
            if (process_data->receiver_thread_loop_count == 0)
            {
            }
            
            ++(process_data->receiver_thread_loop_count);
            // ^^^^^^^^^^ debug stuff ^^^^^^^^^^^^

            char *message_buffer = (char *)process_data->packet_allocator.current;
        
            network_address_t received_address = {};
            int32_t bytes_received = receive_from(message_buffer, process_data->packet_allocator.capacity - process_data->packet_allocator.used_capacity, &received_address);

            if (bytes_received > 0)
            {
                // Actually officially allocate memory on linear allocator (even though it will already have been filled)
                process_data->packets[process_data->packet_count] = allocate_linear(bytes_received, 1, "", &process_data->packet_allocator);
                process_data->packet_sizes[process_data->packet_count] = bytes_received;
                process_data->addresses[process_data->packet_count] = received_address;

                ++(process_data->packet_count);
            }

            release_mutex(process_data->mutex);
        }
    }
}


static void initialize_receiver_thread(void)
{
    receiver_thread.packet_allocator.capacity = megabytes(30);
    receiver_thread.packet_allocator.start = receiver_thread.packet_allocator.current = malloc(receiver_thread.packet_allocator.capacity);

    receiver_thread.mutex = request_mutex();
    request_thread_for_process(&receiver_thread_process, &receiver_thread);
}


static client_t *get_client(uint32_t index)
{
    return(&clients[index]);
}


static void send_chunks_hard_update_packets(network_address_t address)
{
    serializer_t chunks_serializer = {};
    chunks_serializer.initialize(sizeof(uint32_t) + (sizeof(uint8_t) * 3 + sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH) * 8 /* Maximum amount of chunks to "hard update per packet" */);
    
    packet_header_t header = {};
    header.packet_mode = packet_mode_t::PM_SERVER_MODE;
    header.packet_type = server_packet_type_t::SPT_CHUNK_VOXELS_HARD_UPDATE;
    // TODO: Increment this with every packet sent
    header.current_tick = *get_current_tick();

    uint32_t hard_update_count = 0;
    voxel_chunk_values_packet_t *voxel_update_packets = initialize_chunk_values_packets(&hard_update_count);

    union
    {
        struct
        {
            uint32_t is_first: 1;
            uint32_t count: 31;
        };
        uint32_t to_update_count;
    } chunks_count;

    chunks_count.is_first = 0;
    chunks_count.count = hard_update_count;
    
    uint32_t loop_count = (hard_update_count / 8);
    for (uint32_t packet = 0; packet < loop_count; ++packet)
    {
        voxel_chunk_values_packet_t *pointer = &voxel_update_packets[packet * 8];

        header.total_packet_size = sizeof_packet_header() + sizeof(uint32_t) + sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * 8;
        chunks_serializer.serialize_packet_header(&header);

        if (packet == 0)
        {
            chunks_count.is_first = 1;
            // This is the total amount of chunks that the client is waiting for
            chunks_serializer.serialize_uint32(chunks_count.to_update_count);
            chunks_count.is_first = 0;
        }
        else
        {
            chunks_serializer.serialize_uint32(chunks_count.to_update_count);
        }

        chunks_serializer.serialize_uint32(8);
        
        for (uint32_t chunk = 0; chunk < 8; ++chunk)
        {
            chunks_serializer.serialize_voxel_chunk_values_packet(&pointer[chunk]);
        }

        chunks_serializer.send_serialized_message(address);
        chunks_serializer.data_buffer_head = 0;

        hard_update_count -= 8;
    }

    if (hard_update_count)
    {
        voxel_chunk_values_packet_t *pointer = &voxel_update_packets[loop_count * 8];

        header.total_packet_size = sizeof_packet_header() + sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * hard_update_count;
        chunks_serializer.serialize_packet_header(&header);

        if (loop_count == 0)
        {
            chunks_count.is_first = 1;
            chunks_serializer.serialize_uint32(chunks_count.to_update_count);
            chunks_count.is_first = 0;
        }
        else
        {
            chunks_serializer.serialize_uint32(chunks_count.to_update_count);
        }
        
        chunks_serializer.serialize_uint32(hard_update_count);
        
        for (uint32_t chunk = 0; chunk < hard_update_count; ++chunk)
        {
            chunks_serializer.serialize_voxel_chunk_values_packet(&pointer[chunk]);
        }

        
        chunks_serializer.send_serialized_message(address);
    }
}


static void dispatch_newcoming_client_to_clients(uint32_t new_client_index)
{
    client_t *newcoming_client = get_client(new_client_index);
    player_t *player = get_player(newcoming_client->player_handle);
    
    serializer_t serializer = {};
    serializer.initialize(80);

    packet_header_t header = {};
    header.packet_mode = packet_mode_t::PM_SERVER_MODE;
    header.packet_type = server_packet_type_t::SPT_CLIENT_JOINED;
    header.current_tick = *get_current_tick();

    serializer.serialize_packet_header(&header);

    player_state_initialize_packet_t player_initialize_packet = {};
    player_initialize_packet.client_id = new_client_index;
    player_initialize_packet.player_name = newcoming_client->name;
    player_initialize_packet.ws_position = player->ws_position;
    player_initialize_packet.ws_direction = player->ws_direction;
    
    serializer.serialize_player_state_initialize_packet(&player_initialize_packet);

    for (uint32_t client_index = 0; client_index < client_count; ++client_index)
    {
        client_t *current_client = get_client(client_index);

        serializer.send_serialized_message(current_client->network_address);
    }
}


static void dispatch_snapshot_to_clients(void)
{
    packet_header_t header = {};
    header.packet_mode = packet_mode_t::PM_SERVER_MODE;
    header.packet_type = server_packet_type_t::SPT_GAME_STATE_SNAPSHOT;

    // First game snapshot voxel deltas, then game snapshot players
    game_snapshot_voxel_delta_packet_t voxel_packet = {};
    game_snapshot_player_state_packet_t player_snapshots[MAX_CLIENTS] = {};

    // Prepare voxel delta snapshot packets
    uint32_t modified_chunks_count = 0;
    chunk_t **chunks = get_modified_chunks(&modified_chunks_count);

    voxel_packet.modified_count = modified_chunks_count;
    voxel_packet.modified_chunks = (modified_chunk_t *)allocate_linear(sizeof(modified_chunk_t) * modified_chunks_count);

    // FOR DEBUGGING
    uint32_t output_count = 0;
    
    for (uint32_t chunk_index = 0; chunk_index < modified_chunks_count; ++chunk_index)
    {
        chunk_t *chunk = chunks[chunk_index];
        modified_chunk_t *modified_chunk = &voxel_packet.modified_chunks[chunk_index];

        modified_chunk->chunk_index = convert_3d_to_1d_index(chunk->chunk_coord.x, chunk->chunk_coord.y, chunk->chunk_coord.z, get_chunk_grid_size());
        modified_chunk->modified_voxels = (modified_voxel_t *)allocate_linear(sizeof(modified_voxel_t) * chunk->modified_voxels_list_count);
        modified_chunk->modified_voxel_count = chunk->modified_voxels_list_count;
        for (uint32_t voxel = 0; voxel < chunk->modified_voxels_list_count; ++voxel)
        {
            uint16_t voxel_index = chunk->list_of_modified_voxels[voxel];
            modified_chunk->modified_voxels[voxel].previous_value = chunk->voxel_history[voxel_index];
            voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_index, CHUNK_EDGE_LENGTH);
            modified_chunk->modified_voxels[voxel].next_value = chunk->voxels[coord.x][coord.y][coord.z];
            modified_chunk->modified_voxels[voxel].index = voxel_index;

            ++output_count;
            output_to_debug_console((int32_t)(modified_chunk->modified_voxels[voxel].next_value), " ");
        }
    }

    output_to_debug_console(" -> ");
    
    // Prepare the player snapshot packets
    for (uint32_t client_index = 0; client_index < client_count; ++client_index)
    {
        client_t *client = get_client(client_index);
        player_t *player = get_player(client->player_handle);

        player_snapshots[client_index].client_id = client->client_id;
        player_snapshots[client_index].ws_position = player->ws_position;
        player_snapshots[client_index].ws_direction = player->ws_direction;
        player_snapshots[client_index].ws_velocity = player->ws_velocity;
        player_snapshots[client_index].ws_up_vector = player->camera.ws_current_up_vector;
        player_snapshots[client_index].ws_rotation = player->ws_rotation;
        player_snapshots[client_index].action_flags = (uint32_t)player->previous_action_flags;

        player_snapshots[client_index].is_rolling = player->rolling_mode;
    }

    serializer_t out_serializer = {};
    out_serializer.initialize(sizeof_packet_header() +
                              sizeof(uint64_t) +
                              sizeof_game_snapshot_voxel_delta_packet(modified_chunks_count, voxel_packet.modified_chunks) +
                              sizeof_game_snapshot_player_state_packet() * client_count);

    // TODO: FIX THIS IS NOT THE ACTUAL PACKET SIZE: IT VARIES DEPENDING ON THE CLIENT
    header.total_packet_size = sizeof_packet_header() +
        sizeof(uint64_t) +
        sizeof_game_snapshot_voxel_delta_packet(modified_chunks_count, voxel_packet.modified_chunks) +
        sizeof_game_snapshot_player_state_packet() * client_count;
        
    out_serializer.serialize_packet_header(&header);



    // These are the actual current voxel values
    out_serializer.serialize_game_snapshot_voxel_delta_packet(&voxel_packet);


    
    uint32_t player_snapshots_start = out_serializer.data_buffer_head;

    // TODO: Find way so that sending packets to each client does not need packets to be reserialized
    for (uint32_t client_index = 0; client_index < client_count; ++client_index)
    {
        client_t *client = get_client(client_index);
        player_t *player = get_player(client->player_handle);

        if (client->received_input_commands)
        {
            game_snapshot_player_state_packet_t *player_snapshot_packet = &player_snapshots[client_index];

            out_serializer.data_buffer_head = player_snapshots_start;

            player_state_t *previous_received_player_state = &client->previous_received_player_state;

            uint64_t previous_received_tick = client->previous_client_tick;
            out_serializer.serialize_uint64(previous_received_tick);

            // Now need to serialize the chunks / voxels that the client has modified, so that the client can remember which voxels it modified so that it does not do interpolation for the "correct" voxels that it just calculated
            out_serializer.serialize_uint32(client->modified_chunks_count);

            bool force_client_to_do_voxel_correction = 0;
            for (uint32_t chunk = 0; chunk < client->modified_chunks_count; ++chunk)
            {
                out_serializer.serialize_uint16(client->previous_received_voxel_modifications[chunk].chunk_index);
                out_serializer.serialize_uint32(client->previous_received_voxel_modifications[chunk].modified_voxel_count);

                client_modified_chunk_nl_t *modified_chunk_data = &client->previous_received_voxel_modifications[chunk];
                chunk_t *actual_voxel_chunk = *get_chunk(modified_chunk_data->chunk_index);
                
                for (uint32_t voxel = 0; voxel < client->previous_received_voxel_modifications[chunk].modified_voxel_count; ++voxel)
                {
                    local_client_modified_voxel_t *voxel_ptr = &modified_chunk_data->modified_voxels[voxel];
                    uint8_t actual_voxel_value = actual_voxel_chunk->voxels[voxel_ptr->x][voxel_ptr->y][voxel_ptr->z];

                    if (actual_voxel_value != voxel_ptr->value)
                    {
                        force_client_to_do_voxel_correction = 1;

                        client->needs_to_do_voxel_correction = 1;
                        client->needs_to_acknowledge_prediction_error = 1;
                        
                        out_serializer.serialize_uint8(client->previous_received_voxel_modifications[chunk].modified_voxels[voxel].x);
                        out_serializer.serialize_uint8(client->previous_received_voxel_modifications[chunk].modified_voxels[voxel].y);
                        out_serializer.serialize_uint8(client->previous_received_voxel_modifications[chunk].modified_voxels[voxel].z);
                        out_serializer.serialize_uint8(actual_voxel_value);
                    }
                    // If the prediction was correct, do not force client to do the correction
                    else
                    {
                        out_serializer.serialize_uint8(client->previous_received_voxel_modifications[chunk].modified_voxels[voxel].x);
                        out_serializer.serialize_uint8(client->previous_received_voxel_modifications[chunk].modified_voxels[voxel].y);
                        out_serializer.serialize_uint8(client->previous_received_voxel_modifications[chunk].modified_voxels[voxel].z);
                        out_serializer.serialize_uint8(255);
                    }
                }
            }

            if (force_client_to_do_voxel_correction)
            {
                output_to_debug_console("Client needs to do voxel correction: waiting for correction\n"); 
                            
                player_snapshot_packet->need_to_do_voxel_correction = 1;
                player_snapshot_packet->need_to_do_correction = 1;
                client->needs_to_acknowledge_prediction_error = 1;
            }
            
            for (uint32_t i = 0; i < client_count; ++i)
            {
                if (i == client_index)
                {
                    if (client->received_input_commands)
                    {
                        // Do comparison to determine if client correction is needed
                        float32_t precision = 0.1f;
                        vector3_t ws_position_difference = glm::abs(previous_received_player_state->ws_position - player_snapshot_packet->ws_position);
                        vector3_t ws_direction_difference = glm::abs(previous_received_player_state->ws_direction - player_snapshot_packet->ws_direction);

                        bool position_is_different = (ws_position_difference.x > precision ||
                                                      ws_position_difference.y > precision ||
                                                      ws_position_difference.z > precision);

                        bool direction_is_different = (ws_direction_difference.x > precision ||
                                                       ws_direction_difference.y > precision ||
                                                       ws_direction_difference.z > precision);

                        if (position_is_different)
                        {
                            output_to_debug_console("pos-");
                        }

                        if (direction_is_different)
                        {
                            output_to_debug_console("dir-");
                        }
                        
                        if (position_is_different || direction_is_different)
                        {
                            output_to_debug_console("correction-");
                            
                            // Make sure that server invalidates all packets previously sent by the client
                            player->network.player_states_cbuffer.tail = player->network.player_states_cbuffer.head;
                            player->network.player_states_cbuffer.head_tail_difference = 0;
                            
                            // Force client to do correction
                            player_snapshot_packet->need_to_do_correction = 1;

                            // Server will now wait until reception of a prediction error packet
                            client->needs_to_acknowledge_prediction_error = 1;
                        }
                        
                        player_snapshot_packet->is_to_ignore = 0;
                    }
                    else
                    {
                        player_snapshot_packet->is_to_ignore = 1;
                    }
                }

                out_serializer.serialize_game_snapshot_player_state_packet(&player_snapshots[i]);
            }

            client->modified_chunks_count = 0;
            out_serializer.send_serialized_message(client->network_address);
            output_to_debug_console(client->name, " ");
       }
    }

    output_to_debug_console("\n");

    clear_chunk_history_for_server();
}
