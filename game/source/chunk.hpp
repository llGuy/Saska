#pragma once


#include "utility.hpp"
#include "graphics.hpp"


#define CHUNK_EDGE_LENGTH 16
#define MAX_VERTICES_PER_CHUNK 5 * (CHUNK_EDGE_LENGTH - 1) * (CHUNK_EDGE_LENGTH - 1) * (CHUNK_EDGE_LENGTH - 1)


// Will always be allocated on the heap
struct chunk_t
{
    ivector3_t xs_bottom_corner;
    ivector3_t chunk_coord;

    // Make the maximum voxel value be 254 (255 will be reserved for *not* modified in the second section of the 2-byte voxel value)
    uint8_t voxels[CHUNK_EDGE_LENGTH][CHUNK_EDGE_LENGTH][CHUNK_EDGE_LENGTH];

    // These will be for the server
    uint8_t *voxel_history = nullptr; // Array size will be VOXEL_CHUNK_EDGE_LENGTH ^ 3
    static constexpr uint32_t MAX_MODIFIED_VOXELS = (CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH) / 4;
    uint32_t modified_voxels_list_count = 0;
    uint16_t *list_of_modified_voxels = nullptr;

    uint32_t vertex_count;
    vector3_t mesh_vertices[MAX_VERTICES_PER_CHUNK];

    // Chunk rendering data
    mesh_t gpu_mesh;
    gpu_buffer_t chunk_mesh_gpu_buffer;

    struct push_k // Rendering stuff
    {
        matrix4_t model_matrix;
        vector4_t color;
    } push_k;

    bool should_do_gpu_sync = 0;

    bool added_to_history = 0;

    union
    {
        // Flags and stuff
        uint32_t flags;
        struct
        {
            uint32_t was_previously_modified_by_client: 1;
            uint32_t index_of_modified_chunk: 31;
        };
    };


    
    void initialize(const vector3_t &position, ivector3_t chunk_coord, bool allocate_history, const vector3_t &size, model_t *chunk_model);
    void deinitialize(void);
    // TODO: Defer triangles that are between chunks to a higher level function
    void update_mesh(uint8_t surface_level, struct gpu_command_queue_t *queue);

    uint8_t chunk_edge_voxel_value(int32_t x, int32_t y, int32_t z, bool *doesnt_exist);
private:
    void update_chunk_mesh_voxel_pair(uint8_t *voxel_values, uint32_t x, uint32_t y, uint32_t z, uint8_t surface_level);
    void push_vertex_to_triangle_array(uint8_t v0, uint8_t v1, vector3_t *vertices, uint8_t *voxel_values, uint8_t surface_level);
};
