#pragma once

#include "vulkan.hpp"

// to use later on in the project
enum Vertex_Attribute_Bits {POSITION = 1 << 0
			    , NORMAL = 1 << 1
			    , UVS = 1 << 2
			    , COLOR = 1 << 3};
    
void
load_model_from_obj(const char *filename
		    , Vulkan::Model *dst
		    , const char *model_name
		    , Vulkan::GPU *gpu);

struct Terrain_Mesh_Instance
{
    f32 *ys;

    Vulkan::Model model;
    Vulkan::Buffer ys_gpu;
};

// function only loads the prototype information for each terrain mesh
Terrain_Mesh_Instance
load_3D_terrain_mesh_instance(u32 width_x
			      , u32 depth_z
			      , Vulkan::Model *prototype
			      , Vulkan::Buffer *ys_buffer
			      , Vulkan::GPU *gpu);

void
load_3D_terrain_mesh(u32 width_x
		     , u32 depth_z
		     , f32 random_displacement_factor
		     , Vulkan::Model *terrain_mesh_base_model_info
		     , Vulkan::Buffer *mesh_buffer_vbo
		     , Vulkan::Buffer *mesh_buffer_ibo
		     , Vulkan::GPU *gpu);

// later will use proprietary binary file format
void
load_pipelines_from_json(Vulkan::GPU *gpu
			 , Vulkan::Swapchain *swapchain);

void
load_renderers_from_json(Vulkan::GPU *gpu
			 , VkCommandPool *command_pool);

void
load_framebuffers_from_json(Vulkan::GPU *gpu
			    , Vulkan::Swapchain *swapchain);

void
load_render_passes_from_json(Vulkan::GPU *gpu
			     , Vulkan::Swapchain *swapchain);

void
load_descriptors_from_json(Vulkan::GPU *gpu
			   , Vulkan::Swapchain *swapchain
			   , VkDescriptorPool *pool);
