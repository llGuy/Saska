globals = require("globals")

function Binding_Layout(p_binding_type, p_binding_index, p_object_count, p_shader_stage)
   return {
      binding_type = p_binding_type,
      binding_index = p_binding_index,
      object_count = p_object_count,
      shader_stage = p_shader_stage
   }
end

uniform_layout_render_atmosphere = {
   binding_count = 1,
   bindings = { [0] = Binding_Layout(type_combined_sampler_EXT, 0, 1, stage_frag_EXT) }
}

--- Empty layout
uniform_layout_make_atmosphere = {}

uniform_layout_2D_sampler = {
   binding_count = 1,
   bindings = { [0] = Binding_Layout(type_combined_sampler_EXT, 0, 1, stage_frag_EXT) }
}

uniform_layout_deferred_lighting = {
   binding_count = 3,
   bindings = { [0] = Binding_Layout(type_input_attachment_EXT, 0, 1, stage_frag_EXT),
                [1] = Binding_Layout(type_input_attachment_EXT, 1, 1, stage_frag_EXT),
	        [2] = Binding_Layout(type_input_attachment_EXT, 2, 1, stage_frag_EXT) }
}

function Buffer_Binding(p_binding_index, p_dst_element, p_buffer_name, p_buffer_index)
   return {
      binding = p_binding_index,
      destination_element = p_dst_element,
      count = 1,
      buffer_offset = 0,
      
      binding_type = type_buffer_EXT,
      buffer = {
	 name = p_buffer_name,
	 index = p_buffer_index
      }
   }
end

function Input_Attachment_Binding(p_binding_index, dst_element, p_texture_name, p_texture_index, p_layout)
   return {
      binding = p_binding_index,
      destination_element = p_dst_element,
      count = 1,

      binding_type = type_input_attachment_EXT,
      texture = {
	 name = p_texture_name,
	 index = p_texture_index,
	 layout = p_layout
      }
   }
end

function Texture_Binding(p_binding_index, dst_element, p_texture_name, p_texture_index, p_layout)
   return {
      binding = p_binding_index,
      destination_element = p_dst_element,
      count = 1,

      binding_type = type_combined_sampler_EXT,
      texture = {
	 name = p_texture_name,
	 index = p_texture_index,
	 layout = p_layout
      }
   }
end

uniform_group_transforms = {
   --- There will be 3 uniform groups mapping to these uniform buffers
   --- META_* is just a prefix saying that there will be some sort of loop in the C++ side creatingan array of these with a max, using some index
   META_CREATE_COUNT = 3,
   
   uniform_layout = "uniform_layout_transforms",

   binding_count = 1,
   bindings = { Buffer_Binding(0, 0, "buffer_transforms_ubos", META_CREATE_INDEX) }
}

uniform_group_shadowmap = {
   uniform_layout = "uniform_layout_2D_sampler",

   binding_count = 1,
   bindings = { Texture_Binding(0, 0, "texture_shadowmap", 0, layout_shader_read_EXT) }
}

uniform_group_atmosphere_cubemap = {
   uniform_layout = "uniform_layout_render_atmosphere",

   binding_count = 1,
   bindings = { Texture_Binding(0, 0, "texture_atmosphere_cubemap", 0, layout_shader_read_EXT) }
}

uniform_group_deferred_lighting = {
   uniform_layout = "uniform_layout_deferred_lighting",

   binding_count = 1,
   bindings = { Input_Attachment_Binding(0, 0, "texture_deferred_albedo", 0, layout_shader_read_EXT),
		Input_Attachment_Binding(1, 0, "texture_deferred_position", 0, layout_shader_read_EXT),
		Input_Attachment_Binding(2, 0, "texture_deferred_normal", 0, layout_shader_read_EXT) }
}

loaded_uniform_layouts = { "uniform_layout_2D_sampler",
			   "uniform_layout_make_atmosphere",
			   "uniform_layout_render_atmosphere",
			   "uniform_layout_deferred_lighting" }

loaded_uniform_groups = { "uniform_group_atmosphere_cubemap",
			  "uniform_group_shadowmap",
			  "uniform_group_transforms",
			  "uniform_group_deferred_lighting" }
