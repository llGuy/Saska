#include "map.hpp"
#include "serializer.hpp"
#include "file_system.hpp"

// Map file header:
// - File size | 4b
// - Grid edge size | 1b
// - Chunk size float | 4b
// - Loaded chunk count (basically chunks that need to be rendered because they contain meshes) | 1b


void load_map(map_data_t *data, const char *src_path)
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

    uint8_t grid_edge_size = serializer.deserialize_uint8();
    float32_t chunk_size = serializer.deserialize_float32();

    
}


void save_map(const char *dst_path)
{
    // Delete current file and write to a new one
}
