#pragma once

#include "utility.hpp"

struct map_data_t
{
    uint32_t *grid_edge_size;
    float32_t *chunk_size;
};

void load_map(const map_data_t &data, const char *src_path);
void save_map(const char *dst_path);
