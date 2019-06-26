globals = require "globals"

--- Just for testing
swapchain_format_EXT = 1
layout_undefined_EXT = 2
layout_color_attachment_EXT = 3
format_r8g8b8a8_EXT = 4
format_r16g16b16a16_EXT = 5
layout_depth_attachment_EXT = 6
gpu_supported_depth_format_EXT = 7
layout_shader_read_EXT = 8

function Attachment(p_format, p_initial_layout, p_final_layout)
   return {
      format = p_format,
      initial_layout = p_initial_layout,
      final_layout = p_final_layout
   }
end

function Attachment_Reference(p_index, p_layout)
   return {
      index = p_index,
      layout = p_layout
   }
end

function Subpass(p_input, p_input_count, p_color_output, p_color_output_count, p_depth_output)
   return {
      --- Needs to be arrays
      input = p_input,
      input_count = p_input_count,

      --- Needs to be arrays
      color_output = p_color_output,
      color_output_count = p_color_output_count,

      --- Just one
      depht_output = depth_output
   }
end

function Dependency(p_src_index, p_src_stage, p_src_access, p_dst_index, p_dst_stage, p_dst_access, flags)
   return {
      src_index = p_src_index,
      src_stage = p_src_stage,
      src_access = p_src_access,
      
      dst_index = p_dst_index,
      dst_stage = p_dst_stage,
      dst_access = p_dst_access
   }
end

render_pass_deferred = {
   attachment_count = 5,

   color_attachments = { [0] = Attachment(swapchain_format_EXT, layout_undefined_EXT, layout_color_attachment_EXT)
                         , [1] = Attachment(format_r8g8b8a8_EXT, layout_undefined_EXT, layout_color_attachment_EXT)
			 , [2] = Attachment(format_r16g16b16a16_EXT, layout_undefined_EXT, layout_color_attachment_EXT)
   			 , [3] = Attachment(format_r16g16b16a16_EXT, layout_undefined_EXT, layout_color_attachment_EXT) },

   depth_attachment = Attachment(gpu_supported_depth_format_EXT, layout_undefined_EXT, layout_depth_attachment_EXT),

   subpasses = { [0] = Subpass( --- Inputs
	                        nil,
				--- Input count
				0,
				--- Color outputs
				{ [0] = Attachment_Reference(0, layout_color_attachment_EXT),
				  [1] = Attachment_Reference(1, layout_color_attachment_EXT),
				  [2] = Attachment_Reference(2, layout_color_attachment_EXT),
				  [3] = Attachment_Reference(3, layout_color_attachment_EXT) },
				--- Color output count
				4,
				--- Depth output
				Attachment_Reference(4, layout_depth_attachment_EXT)),
      
                 [1] = Subpass( 3,
                                { [0] = Attachment_Reference(1, layout_shader_read_EXT),
				  [1] = Attachment_Reference(2, layout_shader_read_EXT),
				  [2] = Attachment_Reference(3, layout_shader_read_EXT) },
				1,
				{ [0] = Attachment_Reference(0, layout_color_attachment_EXT) },
				--- No depth output
				nil) },

   dependencies = { [0] = Dependency(subpass_external_EXT, stage_bottom_of_pipe_EXT, memory_read_EXT, 0, stage_color_output_EXT, color_attachment_read_EXT | color_attachment_write_EXT, by_region_EXT),
                    [1] = Dependency(0, stage_color_ouptut_EXT, color_attachment_write_EXT, 1, stage_frag_EXT, memory_read_EXT, by_region_EXT),
		    [2] = Dependency(1, stage_color_ouptut_EXT, color_attachment_write_EXT | color_attachment_read_EXT, subpass_external_EXT, stage_bottom_of_pipe_EXT, memory_read_EXT, by_region_EXT) }
}

render_pass_atmosphere = {
   attachment_count = 1,

   color_attachments = { [0] = Attachment(format_r8g8b8a8_EXT, layout_undefined_EXT, layout_shader_read_EXT) },

   subpasses = { [0] = Subpass(nil,
	                       0,
	                       { [0] = Attachment_Reference(0, layout_color_attachment_EXT) },
	                       1,
	                       nil) },

   dependencies = {
      
   }
}
