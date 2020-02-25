#pragma once

#include "gui_math.hpp"
#include "graphics.hpp"
#include "utility.hpp"


#define MAX_TEXTURES_IN_RENDER_LIST 20
#define MAX_QUADS_TO_RENDER 1000


struct gui_textured_vertex_render_list_t {
    model_t vtx_attribs;
    graphics_pipeline_t gfx_pipeline;
    
    struct vertex_section_t {
        uint32_t section_start, section_size;
    };
    
    uint32_t section_count = 0;
    // Index where there is a transition to the next texture to bind for the following vertices
    vertex_section_t sections[MAX_TEXTURES_IN_RENDER_LIST] = {};
    // Corresponds to the same index as the index of the current section
    uniform_group_t textures[MAX_TEXTURES_IN_RENDER_LIST] = {};

    uint32_t vertex_count;
    gui_textured_vertex_t vertices[MAX_QUADS_TO_RENDER * 6];

    gpu_buffer_t vtx_buffer;

    void mark_section(uniform_group_t group) {
        if (section_count) {
            // Get current section
            vertex_section_t *next = &sections[section_count];

            next->section_start = sections[section_count - 1].section_start + sections[section_count - 1].section_size;
            // Starts at 0. Every vertex that gets pushed adds to this variable
            next->section_size = 0;

            textures[section_count] = group;

            ++section_count;
        }
        else {
            sections[0].section_start = 0;
            sections[0].section_size = 0;

            textures[0] = group;
            
            ++section_count;
        }
    }

    void push_vertex(const gui_textured_vertex_t &vertex) {
        vertices[vertex_count++] = vertex;
        sections[section_count - 1].section_size += 1;
    }

    void clear_containers(void) {
        section_count = 0;
        vertex_count = 0;
    }

    void sync_gpu_with_vertex_list(gpu_command_queue_t *queue) {
        if (vertex_count) {
            update_gpu_buffer(&vtx_buffer, vertices, sizeof(gui_textured_vertex_t) * vertex_count, 0, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, &queue->q);
        }
    }
    
    void render_textured_quads(gpu_command_queue_t *queue, framebuffer_t *fbo) {
        if (vertex_count) {
            command_buffer_set_viewport(fbo->extent.width, fbo->extent.height, 0.0f, 1.0f, &queue->q);
            command_buffer_bind_pipeline(&gfx_pipeline.pipeline, &queue->q);

            // Loop through each section, bind texture, and render the quads from the section
            for (uint32_t current_section = 0; current_section < section_count; ++current_section) {
                vertex_section_t *section = &sections[current_section];
                
                command_buffer_bind_descriptor_sets(&gfx_pipeline.layout, {1, &textures[current_section]}, &queue->q);
            
                VkDeviceSize zero = 0;
                command_buffer_bind_vbos(vtx_attribs.raw_cache_for_rendering, {1, &zero}, 0, vtx_attribs.binding_count, &queue->q);

                command_buffer_draw(&queue->q, section->section_size, 1, section->section_start, 0);
            }
        }
    }
};

struct gui_colored_vertex_render_list_t {
    model_t vtx_attribs;
    graphics_pipeline_t gfx_pipeline;
    
    uint32_t vertex_count;
    gui_colored_vertex_t vertices[MAX_QUADS_TO_RENDER * 6];

    gpu_buffer_t vtx_buffer;

    void push_vertex(const gui_colored_vertex_t &vertex) {
        vertices[vertex_count++] = vertex;
    }
    
    void clear_containers(void) {
        vertex_count = 0;
    }

    void sync_gpu_with_vertex_list(gpu_command_queue_t *queue) {
        update_gpu_buffer(&vtx_buffer, &vertices, sizeof(gui_colored_vertex_t) * vertex_count, 0, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, &queue->q);
    }

    void render_colored_quads(gpu_command_queue_t *queue, framebuffer_t *fbo) {
        if (vertex_count) {
            command_buffer_set_viewport(fbo->extent.width, fbo->extent.height, 0.0f, 1.0f, &queue->q);
            command_buffer_bind_pipeline(&gfx_pipeline.pipeline, &queue->q);

            VkDeviceSize zero = 0;
            command_buffer_bind_vbos(vtx_attribs.raw_cache_for_rendering, {1, &zero}, 0, vtx_attribs.binding_count, &queue->q);
            command_buffer_draw(&queue->q, vertex_count, 1, 0, 0);
        }
    }
};
