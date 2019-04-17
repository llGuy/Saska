#pragma once

#include "vulkan.hpp"

// to use later on in the project
enum Vertex_Attribute_Bits {POSITION = 1 << 0
			    , NORMAL = 1 << 1
			    , UVS = 1 << 2
			    , COLOR = 1 << 3};
    
void
load_model_from_obj(const char *filename
		    , Vulkan_API::Model *dst
		    , const char *model_name
		    , Vulkan_API::GPU *gpu);

struct Terrain_Mesh_Instance
{
    f32 *ys;

    Vulkan_API::Model model;
    Vulkan_API::Buffer ys_gpu;
};

// function only loads the prototype information for each terrain mesh
void
load_3D_terrain_mesh(u32 width_x
		     , u32 depth_z
		     , f32 random_displacement_factor
		     , Vulkan_API::GPU *gpu);

Terrain_Mesh_Instance
load_3D_terrain_mesh_instance(u32 width_x
			      , u32 depth_z
			      , Vulkan_API::Model *prototype
			      , Vulkan_API::GPU *gpu);

void
load_3D_terrain_mesh_graphics_pipeline(Vulkan_API::Graphics_Pipeline *dst
				       , Vulkan_API::Model *terrain_prototype
				       , Vulkan_API::GPU *gpu);

// later will use proprietary binary file format
void
load_pipelines_from_json(Vulkan_API::GPU *gpu
			 , Vulkan_API::Swapchain *swapchain);

void
load_renderers_from_json(Vulkan_API::GPU *gpu
			 , VkCommandPool *command_pool);

void
load_framebuffers_from_json(Vulkan_API::GPU *gpu
			    , Vulkan_API::Swapchain *swapchain);

void
load_render_passes_from_json(Vulkan_API::GPU *gpu
			     , Vulkan_API::Swapchain *swapchain);
