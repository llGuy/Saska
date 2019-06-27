globals = require "globals"

function Shader_Module(p_shader_type, p_path)
   return {
      shader_type = p_shader_type,
      path = p_path
   }
end

function Blend_State(p_enable)
   return {
      enabled = p_enable
   }
end

pipeline_debug_frustum = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/debug_frustum.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/debug_frustum.frag.spv") },
   restart = false,
   topology = topology_line_list_EXT,
   polymode = polymode_lines_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false), [1] = Blend_State(false), [2] = Blend_State(false), [3] = Blend_State(false) },
   vertex_input = nil,
   depth = true,
   dynamic_depth_bias = false,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 0
   }
}

pipeline_screen_quad = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/screen_quad.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/screen_quad.frag.spv") },
   restart = false,
   topology = topology_triangle_list_EXT,

   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_2D_sampler" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false) },
   vertex_input = nil,
   depth = false,
   dynamic_depth_bias = false,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 1
   }
}

pipeline_deferred_lighting = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/deferred_lighting.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/deferred_lighting.frag.spv") },
   restart = false,
   topology = topology_triangle_list_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_deferred_lighting" },
   push_constant = {
      shader_stages = stage_frag_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false) },
   vertex_input = nil,
   depth = false,
   dynamic_depth_bias = false,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 1
   }
}

pipeline_terrain_shadow = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/terrain_shadow.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/terrain_shadow.frag.spv") },
   poly_restart = true,
   topology = topology_triangle_fan_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.shadowmap_width, y = globals.shadowmap_height },
   blend = nil,
   vertex_input = "model_terrain_base",
   depth = true,
   dynamic_depth_bias = true,
   compatible_render_pass = {
      name = "render_pass_shadow", subpass = 0
   }
}

pipeline_model_shadow = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/model_shadow.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/model_shadow.frag.spv") },
   poly_restart = false,
   topology = topology_triangle_list_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.shadowmap_width, y = globals.shadowmap_height },
   blend = nil,
   vertex_input = "model_test",
   depth = true,
   dynamic_depth_bias = true,
   compatible_render_pass = {
      name = "render_pass_shadow", subpass = 0
   }
}

pipeline_make_atmosphere = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/atmosphere.vert.spv"),
	       Shader_Module(geometry_shader_type_EXT, "shaders/SPV/atmosphere.geom.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/atmosphere.frag.spv") },
   poly_restart = false,
   topology = topology_point_list_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_atmosphere" },
   push_constant = {
      shader_stages = stage_frag_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.atmosphere_width, y = globals.atmosphere_height },
   blend = { [0] = Blend_State(false) },
   vertex_input = nil,
   depth = false,
   compatible_render_pass = {
      name = "render_pass_atmosphere", subpass = 0
   }
}

pipeline_render_atmosphere = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/render_atmosphere.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/render_atmosphere.frag.spv") },
   poly_restart = false,
   topology = topology_triangle_list_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms", [1] = "uniform_layout_render_atmosphere" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false), [1] = Blend_State(false), [2] = Blend_State(false), [3] = Blend_State(false) },
   vertex_input = "model_test",
   depth = true,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 0
   }
}

pipeline_model = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/model.vert.spv"),
	       Shader_Module(geometry_shader_type_EXT, "shaders/SPV/model.geom.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/model.frag.spv") },
   poly_restart = false,
   topology = topology_triangle_list_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms", [1] = "uniform_layout_2D_sampler" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false), [1] = Blend_State(false), [2] = Blend_State(false), [3] = Blend_State(false) },
   vertex_input = "model_test",
   depth = true,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 0
   }
}

pipeline_terrain = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/terrain.vert.spv"),
	       Shader_Module(geometry_shader_type_EXT, "shaders/SPV/terrain.geom.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/terrain.frag.spv") },
   poly_restart = true,
   topology = topology_triangle_fan_EXT,
   polymode = polymode_fill_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms", [1] = "uniform_layout_2D_sampler" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false), [1] = Blend_State(false), [2] = Blend_State(false), [3] = Blend_State(false) },
   vertex_input = "model_terrain_base",
   depth = true,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 0
   }
}

pipeline_terrain_pointer = {
   modules = { Shader_Module(vertex_shader_type_EXT, "shaders/SPV/terrain_pointer.vert.spv"),
               Shader_Module(fragment_shader_type_EXT, "shaders/SPV/terrain_pointer.frag.spv") },
   poly_restart = false,
   topology = topology_line_strip_EXT,
   polymode = polymode_lines_EXT,
   culling = culling_none_EXT,
   uniform_layouts = { [0] = "uniform_layout_transforms" },
   push_constant = {
      shader_stages = stage_vertex_EXT,
      offset = 0,
      size = 160
   },
   viewport = { x = globals.swapchain_width, y = globals.swapchain_height },
   blend = { [0] = Blend_State(false), [1] = Blend_State(false), [2] = Blend_State(false), [3] = Blend_State(false) },
   vertex_input = nil,
   depth = true,
   compatible_render_pass = {
      name = "render_pass_deferred", subpass = 0
   }
}

loaded_pipelines = { "pipeline_screen_quad",
		     "pipeline_deferred_lighting",
		     "pipeline_terrain_shadow",
		     "pipeline_model_shadow",
		     "pipeline_make_atmosphere",
		     "pipeline_render_atmosphere",
		     "pipeline_model",
		     "pipeline_terrain",
		     "pipeline_terrain_pointer",
		     "pipeline_debug_frustum" }
