#pragma once

#include "utility.hpp"

struct map_data_t
{
    uint32_t max_chunks;
    uint32_t grid_edge_size;
    float32_t chunk_size;
    uint32_t to_update_count;

    struct chunk_t **chunks;
    struct chunk_t **to_update;
};

void load_map(map_data_t *data, const char *src_path, struct model_t *model);
void save_map(map_data_t *data, const char *dst_path);
