#include "map.hpp"
#include "serializer.hpp"
#include "file_system.hpp"

// Map file header:
// - File size | 4b
// - Grid edge size | 1b
// - Chunk size float | 4b
// - Loaded chunk count (basically chunks that need to be rendered because they contain meshes) | 1b


void load_map(map_data_t *data, const char *src_path, model_t *chunk_model)
{
    file_handle_t file = create_file(src_path, ASSET);
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
    memset(data->chunks, 0, sizeof(chunk_t *) * max_chunks);
    
    data->to_update_count = (uint32_t)serializer.deserialize_uint8();
    data->to_update = (chunk_t **)allocate_free_list(sizeof(chunk_t *) * data->max_chunks);

    for (uint32_t z = 0; z < data->grid_edge_size; ++z)
    {
        for (uint32_t y = 0; y < data->grid_edge_size; ++y)
        {
            for (uint32_t x = 0; x < data->grid_edge_size; ++x)
            {
                chunk_t **chunk_pptr = data->chunks[i];
                *chunk_pptr = (chunk_t *)allocate_free_list(sizeof(chunk_t));
    
                vector3_t position = vector3_t(x, y, z) * (float32_t)(CHUNK_EDGE_LENGTH) - vector3_t((float32_t)grid_edge_size / 2) * (float32_t)(CHUNK_EDGE_LENGTH);

                chunk_t *chunk_ptr = *chunk_pptr;
                chunk_ptr->initialize(position, ivector3_t(x, y, z), true, vector3_t(chunk_size));

                switch (get_app_type())
                {
                case application_type_t::WINDOW_APPLICATION_MODE: {
                    chunk_ptr->initialize_for_rendering(chunk_model);
                } break;
                default: break;
                }

                ++i;
            }    
        }    
    }
    
    for (uint32_t i = 0; i < data->to_update_count; ++i)
    {
        uint8_t x = serializer.deserialize_uint8();
        uint8_t y = serializer.deserialize_uint8();
        uint8_t z = serializer.deserialize_uint8();

        chunk_t **chunk_ptr = &data->chunks[convert_3d_to_1d_index(x, y, z, data->grid_edge_size)];
        
        serializer.deserialize_bytes(chunk_ptr->voxels, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);

        data->to_update[i] = *chunk_ptr;
    }
}


void save_map(const char *dst_path)
{
    // Delete current file and write to a new one
    
}
