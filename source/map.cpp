#include "map.hpp"
#include "serializer.hpp"
#include "file_system.hpp"
#include "chunks_gstate.hpp"
#include "game.hpp"

// Map file header:
// - File size | 4b
// - Grid edge size | 1b
// - Chunk size float | 4b
// - Loaded chunk count (basically chunks that need to be rendered because they contain meshes) | 1b


void load_map(map_data_t *data, const char *src_path, model_t *chunk_model) {
    file_handle_t file = create_file(src_path, file_type_flags_t::ASSET | file_type_flags_t::BINARY);
    file_contents_t content = read_file_tmp(file);

    byte_t *bin = content.content;
    serializer_t serializer {content.size, bin, 0};
    
    uint32_t file_size = serializer.deserialize_uint32();

    serializer.data_buffer_size = file_size;
    serializer.data_buffer = bin;
    serializer.data_buffer_head = 0;

    // Deserialize file size again
    file_size = serializer.deserialize_uint32();

    data->grid_edge_size = serializer.deserialize_uint8();
    data->chunk_size = serializer.deserialize_float32();
    data->max_chunks = 20 * 20 * 20;

    data->chunks = (chunk_t **)allocate_free_list(sizeof(chunk_t *) * data->max_chunks);
    memset(data->chunks, 0, sizeof(chunk_t *) * data->max_chunks);
    
    data->to_update_count = (uint32_t)serializer.deserialize_uint8();
    data->to_update = (chunk_t **)allocate_free_list(sizeof(chunk_t *) * data->max_chunks);

    for (uint32_t z = 0; z < data->grid_edge_size; ++z) {
        for (uint32_t y = 0; y < data->grid_edge_size; ++y) {
            for (uint32_t x = 0; x < data->grid_edge_size; ++x) {
                uint32_t i = convert_3d_to_1d_index(x, y, z, data->grid_edge_size);
                chunk_t **chunk_pptr = &data->chunks[i];
                *chunk_pptr = (chunk_t *)allocate_free_list(sizeof(chunk_t));
    
                vector3_t position = vector3_t(x, y, z) * (float32_t)(CHUNK_EDGE_LENGTH) - vector3_t((float32_t)data->grid_edge_size / 2) * (float32_t)(CHUNK_EDGE_LENGTH);

                chunk_t *chunk_ptr = *chunk_pptr;
                chunk_ptr->initialize(position, ivector3_t(x, y, z), true, vector3_t(data->chunk_size));

                switch (get_app_type()) {
                case application_type_t::WINDOW_APPLICATION_MODE: {
                    chunk_ptr->initialize_for_rendering(chunk_model);
                } break;
                default: break;
                }

                ++i;
            }    
        }    
    }
    
    for (uint32_t i = 0; i < data->to_update_count; ++i) {
        uint8_t x = serializer.deserialize_uint8();
        uint8_t y = serializer.deserialize_uint8();
        uint8_t z = serializer.deserialize_uint8();

        uint32_t index = convert_3d_to_1d_index(x, y, z, data->grid_edge_size);
        
        chunk_t *chunk_ptr = data->chunks[index];
        
        serializer.deserialize_bytes((uint8_t *)chunk_ptr->voxels, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);

        data->to_update[i] = chunk_ptr;

        ready_chunk_for_gpu_sync(chunk_ptr, index);
    }

    remove_and_destroy_file(file);
}


void save_map(map_data_t *data, const char *dst_path) {
    delete_file(dst_path);

    // Delete current file and write to a new one
    file_handle_t file = create_writeable_file(dst_path, file_type_flags_t::ASSET | file_type_flags_t::BINARY);

    uint32_t file_size = 0;
    // Header size
    file_size += sizeof(uint32_t) + sizeof(uint8_t) + sizeof(float32_t) + sizeof(uint8_t);
    // Chunks to load
    file_size += (sizeof(uint8_t) * 3 + sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH) * data->to_update_count;

    byte_t *bytes = (byte_t *)allocate_linear(file_size);

    serializer_t serializer{ file_size, bytes, 0 };

    serializer.serialize_uint32(file_size);
    serializer.serialize_uint8(data->grid_edge_size);
    serializer.serialize_float32(data->chunk_size);
    serializer.serialize_uint8(data->to_update_count);

    for (uint32_t i = 0; i < data->to_update_count; ++i) {
        chunk_t *chunk_ptr = data->to_update[i];

        serializer.serialize_uint8(chunk_ptr->chunk_coord.x);
        serializer.serialize_uint8(chunk_ptr->chunk_coord.y);
        serializer.serialize_uint8(chunk_ptr->chunk_coord.z);

        serializer.serialize_bytes((uint8_t *)chunk_ptr->voxels, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
    }

    write_file(file, bytes, file_size);

    remove_and_destroy_file(file);
}
