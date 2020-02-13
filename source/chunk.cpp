#include "math.hpp"
#include "chunk.hpp"

#include "game.hpp"

#include "chunks_gstate.hpp"

// For updating mesh
#include "ttable.inc"



void chunk_t::initialize(const vector3_t &position, ivector3_t in_chunk_coord, bool allocate_history, const vector3_t &size)
{
    should_do_gpu_sync = 0;
    
    xs_bottom_corner = in_chunk_coord * CHUNK_EDGE_LENGTH;
    this->chunk_coord = in_chunk_coord;
    
    memset(voxels, 0, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
    memset(mesh_vertices, 0, sizeof(vector3_t) * MAX_VERTICES_PER_CHUNK);
    
    push_k.model_matrix = glm::scale(size) * glm::translate(position);
    //push_k.color = vector4_t(122.0 / 255.0, 177.0 / 255.0, 213.0 / 255.0, 1.0f);
    push_k.color = vector4_t(122.0 / 255.0, 213.0 / 255.0, 77.0 / 255.0, 1.0f);

    if (allocate_history)
    {
        voxel_history = (uint8_t *)allocate_free_list(sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
        memset(voxel_history, 255, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
        modified_voxels_list_count = 0;
        list_of_modified_voxels = (uint16_t *)allocate_free_list(sizeof(uint16_t) * (CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH) / 4);
    }
}


void chunk_t::initialize_for_rendering(model_t *chunk_model)
{
    uint32_t buffer_size = sizeof(vector3_t) * MAX_VERTICES_PER_CHUNK;

    make_unmappable_gpu_buffer(&chunk_mesh_gpu_buffer, buffer_size, mesh_vertices, gpu_buffer_usage_t::VERTEX_BUFFER, get_global_command_pool());

    draw_indexed_data_t indexed_data = init_draw_indexed_data_default(1, vertex_count);
    memory_buffer_view_t<VkBuffer> buffers{ 1, &chunk_mesh_gpu_buffer.buffer };

    gpu_mesh = initialize_mesh(buffers, &indexed_data, &chunk_model->index_data);
}


void chunk_t::deinitialize(void)
{
    chunk_mesh_gpu_buffer.destroy();
    deallocate_free_list(gpu_mesh.raw_buffer_list.buffer);

    if (voxel_history)
    {
        deallocate_free_list(voxel_history);
    }
    if (list_of_modified_voxels)
    {
        modified_voxels_list_count = 0;
        deallocate_free_list(list_of_modified_voxels);
    }
}


void chunk_t::update_mesh(uint8_t surface_level, gpu_command_queue_t *queue)
{
    vertex_count = 0;

    chunk_t *x_superior = *get_chunk(chunk_coord.x + 1, chunk_coord.y, chunk_coord.z);
    chunk_t *y_superior = *get_chunk(chunk_coord.x, chunk_coord.y + 1, chunk_coord.z);
    chunk_t *z_superior = *get_chunk(chunk_coord.x, chunk_coord.y, chunk_coord.z + 1);
    
    chunk_t *xy_superior = *get_chunk(chunk_coord.x + 1, chunk_coord.y + 1, chunk_coord.z);
    chunk_t *xz_superior = *get_chunk(chunk_coord.x + 1, chunk_coord.y, chunk_coord.z + 1);
    chunk_t *yz_superior = *get_chunk(chunk_coord.x, chunk_coord.y + 1, chunk_coord.z + 1);
    chunk_t *xyz_superior = *get_chunk(chunk_coord.x + 1, chunk_coord.y + 1, chunk_coord.z + 1);

    // First do the vertices that will need information from other chunks
    bool doesnt_exist = 0;
    if (x_superior)
    {
        // x_superior
        for (uint32_t z = 0; z < CHUNK_EDGE_LENGTH; ++z)
        {
            for (uint32_t y = 0; y < CHUNK_EDGE_LENGTH - 1; ++y)
            {
                doesnt_exist = 0;
                
                uint32_t x = CHUNK_EDGE_LENGTH - 1;

                uint8_t voxel_values[8] = { voxels[x]    [y][z],
                                            chunk_edge_voxel_value(x + 1, y, z, &doesnt_exist),//voxels[x + 1][y][z],
                                            chunk_edge_voxel_value(x + 1, y, z + 1, &doesnt_exist),//voxels[x + 1][y][z + 1],
                                            chunk_edge_voxel_value(x,     y, z + 1, &doesnt_exist),//voxels[x]    [y][z + 1],
                     
                                            voxels[x]    [y + 1][z],
                                            chunk_edge_voxel_value(x + 1, y + 1, z,&doesnt_exist),//voxels[x + 1][y + 1][z],
                                            chunk_edge_voxel_value(x + 1, y + 1, z + 1, &doesnt_exist),//voxels[x + 1][y + 1][z + 1],
                                            chunk_edge_voxel_value(x,     y + 1, z + 1, &doesnt_exist) };//voxels[x]    [y + 1][z + 1] };

                if (!doesnt_exist)
                {
                    update_chunk_mesh_voxel_pair(voxel_values, x, y, z, surface_level);
                }
            }
        }
    }

    if (y_superior)
    {
        // y_superior    
        for (uint32_t z = 0; z < CHUNK_EDGE_LENGTH; ++z)
        {
            for (uint32_t x = 0; x < CHUNK_EDGE_LENGTH; ++x)
            {
                doesnt_exist = 0;
                
                uint32_t y = CHUNK_EDGE_LENGTH - 1;

                uint8_t voxel_values[8] = { voxels[x]    [y][z],
                                            chunk_edge_voxel_value(x + 1, y, z, &doesnt_exist),//voxels[x + 1][y][z],
                                            chunk_edge_voxel_value(x + 1, y, z + 1, &doesnt_exist),//voxels[x + 1][y][z + 1],
                                            chunk_edge_voxel_value(x,     y, z + 1, &doesnt_exist),//voxels[x]    [y][z + 1],
                     
                                            chunk_edge_voxel_value(x, y + 1, z, &doesnt_exist),
                                            chunk_edge_voxel_value(x + 1, y + 1, z, &doesnt_exist),//voxels[x + 1][y + 1][z],
                                            chunk_edge_voxel_value(x + 1, y + 1, z + 1, &doesnt_exist),//voxels[x + 1][y + 1][z + 1],
                                            chunk_edge_voxel_value(x,     y + 1, z + 1, &doesnt_exist) };//voxels[x]    [y + 1][z + 1] };

                if (!doesnt_exist)
                {
                    update_chunk_mesh_voxel_pair(voxel_values, x, y, z, surface_level);
                }
            }
        }
    }

    if (z_superior)
    {
        // z_superior
        for (uint32_t y = 0; y < CHUNK_EDGE_LENGTH - 1; ++y)
        {
            for (uint32_t x = 0; x < CHUNK_EDGE_LENGTH - 1; ++x)
            {
                doesnt_exist = 0;
                
                uint32_t z = CHUNK_EDGE_LENGTH - 1;

                uint8_t voxel_values[8] = { voxels[x]    [y][z],
                                            chunk_edge_voxel_value(x + 1, y, z, &doesnt_exist),//voxels[x + 1][y][z],
                                            chunk_edge_voxel_value(x + 1, y, z + 1, &doesnt_exist),//voxels[x + 1][y][z + 1],
                                            chunk_edge_voxel_value(x,     y, z + 1, &doesnt_exist),//voxels[x]    [y][z + 1],
                     
                                            voxels[x]    [y + 1][z],
                                            chunk_edge_voxel_value(x + 1, y + 1, z, &doesnt_exist),//voxels[x + 1][y + 1][z],
                                            chunk_edge_voxel_value(x + 1, y + 1, z + 1, &doesnt_exist),//voxels[x + 1][y + 1][z + 1],
                                            chunk_edge_voxel_value(x,     y + 1, z + 1, &doesnt_exist) };//voxels[x]    [y + 1][z + 1] };

                if (!doesnt_exist)
                {
                    update_chunk_mesh_voxel_pair(voxel_values, x, y, z, surface_level);
                }
            }
        }
    }
    
    for (uint32_t z = 0; z < CHUNK_EDGE_LENGTH - 1; ++z)
    {
        for (uint32_t y = 0; y < CHUNK_EDGE_LENGTH - 1; ++y)
        {
            for (uint32_t x = 0; x < CHUNK_EDGE_LENGTH - 1; ++x)
            {
                uint8_t voxel_values[8] = { voxels[x]    [y][z],
                                            voxels[x + 1][y][z],
                                            voxels[x + 1][y][z + 1],
                                            voxels[x]    [y][z + 1],
                     
                                            voxels[x]    [y + 1][z],
                                            voxels[x + 1][y + 1][z],
                                            voxels[x + 1][y + 1][z + 1],
                                            voxels[x]    [y + 1][z + 1] };

                update_chunk_mesh_voxel_pair(voxel_values, x, y, z, surface_level);
            }
        }
    }



    switch (get_app_type())
    {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        gpu_mesh.indexed_data.index_count = vertex_count;

        update_gpu_buffer(&chunk_mesh_gpu_buffer,
            mesh_vertices,
            sizeof(vector3_t) * vertex_count,
            0,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            &queue->q);
    } break;
    }

    should_do_gpu_sync = 0;
}



uint8_t chunk_t::chunk_edge_voxel_value(int32_t x, int32_t y, int32_t z, bool *doesnt_exist)
{
    if (x < 0 || y < 0 || z < 0)
    {
        //OutputDebugString("Weird\n");
    }
    
    // Voxel coords
    int32_t chunk_coord_offset_x = 0, chunk_coord_offset_y = 0, chunk_coord_offset_z = 0;
    int32_t final_x = x, final_y = y, final_z = z;

    if (x == CHUNK_EDGE_LENGTH)
    {
        final_x = 0;
        chunk_coord_offset_x = 1;
    }
    if (y == CHUNK_EDGE_LENGTH)
    {
        final_y = 0;
        chunk_coord_offset_y = 1;
    }
    if (z == CHUNK_EDGE_LENGTH)
    {
        final_z = 0;
        chunk_coord_offset_z = 1;
    }

    chunk_t **chunk_ptr = get_chunk(chunk_coord.x + chunk_coord_offset_x,
                                    chunk_coord.y + chunk_coord_offset_y,
                                    chunk_coord.z + chunk_coord_offset_z);
    *doesnt_exist = (bool)(*chunk_ptr == nullptr);
    if (*doesnt_exist)
    {
        return 0;
    }
    
    return (*chunk_ptr)->voxels[final_x][final_y][final_z];
}



// Private:
static constexpr vector3_t NORMALIZED_CUBE_VERTICES[8] = { vector3_t(-0.5f, -0.5f, -0.5f),
                                                           vector3_t(+0.5f, -0.5f, -0.5f),
                                                           vector3_t(+0.5f, -0.5f, +0.5f),
                                                           vector3_t(-0.5f, -0.5f, +0.5f),
                                                           vector3_t(-0.5f, +0.5f, -0.5f),
                                                           vector3_t(+0.5f, +0.5f, -0.5f),
                                                           vector3_t(+0.5f, +0.5f, +0.5f),
                                                           vector3_t(-0.5f, +0.5f, +0.5f) };

static constexpr ivector3_t NORMALIZED_CUBE_VERTEX_INDICES[8] = { ivector3_t(0, 0, 0),
                                                                  ivector3_t(1, 0, 0),
                                                                  ivector3_t(1, 0, 1),
                                                                  ivector3_t(0, 0, 1),
                                                                  ivector3_t(0, 1, 0),
                                                                  ivector3_t(1, 1, 0),
                                                                  ivector3_t(1, 1, 1),
                                                                  ivector3_t(0, 1, 1) };


void chunk_t::update_chunk_mesh_voxel_pair(uint8_t *voxel_values, uint32_t x, uint32_t y, uint32_t z, uint8_t surface_level)
{    
    uint8_t bit_combination = 0;
    for (uint32_t i = 0; i < 8; ++i)
    {
        bool is_over_surface = (voxel_values[i] > surface_level);
        bit_combination |= is_over_surface << i;
    }

    const int8_t *triangle_entry = &TRIANGLE_TABLE[bit_combination][0];

    uint32_t edge = 0;

    int8_t edge_pair[3] = {};
                
    while(triangle_entry[edge] != -1)
    {
        int8_t edge_index = triangle_entry[edge];
        edge_pair[edge % 3] = edge_index;

        if (edge % 3 == 2)
        {
            vector3_t vertices[8] = {};
            for (uint32_t i = 0; i < 8; ++i)
            {
                vertices[i] = NORMALIZED_CUBE_VERTICES[i] + vector3_t(0.5f) + vector3_t((float32_t)x, (float32_t)y, (float32_t)z);
            }

            for (uint32_t i = 0; i < 3; ++i)
            {
                switch(edge_pair[i])
                {
                case 0: { push_vertex_to_triangle_array(0, 1, vertices, voxel_values, surface_level); } break;
                case 1: { push_vertex_to_triangle_array(1, 2, vertices, voxel_values, surface_level); } break;
                case 2: { push_vertex_to_triangle_array(2, 3, vertices, voxel_values, surface_level); } break;
                case 3: { push_vertex_to_triangle_array(3, 0, vertices, voxel_values, surface_level); } break;
                case 4: { push_vertex_to_triangle_array(4, 5, vertices, voxel_values, surface_level); } break;
                case 5: { push_vertex_to_triangle_array(5, 6, vertices, voxel_values, surface_level); } break;
                case 6: { push_vertex_to_triangle_array(6, 7, vertices, voxel_values, surface_level); } break;
                case 7: { push_vertex_to_triangle_array(7, 4, vertices, voxel_values, surface_level); } break;
                case 8: { push_vertex_to_triangle_array(0, 4, vertices, voxel_values, surface_level); } break;
                case 9: { push_vertex_to_triangle_array(1, 5, vertices, voxel_values, surface_level); } break;
                case 10: { push_vertex_to_triangle_array(2, 6, vertices, voxel_values, surface_level); } break;
                case 11: { push_vertex_to_triangle_array(3, 7, vertices, voxel_values, surface_level); } break;
                }
            }
        }

        ++edge;
    }
}


void chunk_t::push_vertex_to_triangle_array(uint8_t v0, uint8_t v1, vector3_t *vertices, uint8_t *voxel_values, uint8_t surface_level)
{
    float32_t surface_level_f = (float32_t)surface_level;
    float32_t voxel_value0 = (float32_t)voxel_values[v0];
    float32_t voxel_value1 = (float32_t)voxel_values[v1];

    if (voxel_value0 > voxel_value1)
    {
        float32_t tmp = voxel_value0;
        voxel_value0 = voxel_value1;
        voxel_value1 = tmp;

        uint8_t tmp_v = v0;
        v0 = v1;
        v1 = tmp_v;
    }

    float32_t interpolated_voxel_values = lerp(voxel_value0, voxel_value1, surface_level_f);
    
    vector3_t vertex = interpolate(vertices[v0], vertices[v1], interpolated_voxel_values);
    
    mesh_vertices[vertex_count++] = vertex;
}
