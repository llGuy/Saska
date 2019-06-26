globals = require "globals"

Attachment = {
   format = nil,
   initial_layout = nil,
   final_layout = nil
}

function Attachment.make(p_format, p_initial_layout, p_final_layout)
   self.format = p_format
   self.initial_layout = p_initial_layout
   self.final_layout = p_final_layout
   return self
end

Subpass_Attachment_Reference = {
   index = nil,
   layout = nil
}

function Subpass_Output.make(p_index, p_layout)
   self.index = p_index
   self.layout = p_layout
   return self
end

render_pass_deferred = {
   attachment_count = 5,

   color_attachments = { Attachment.make(swapchain_format_EXT, layout_undefined_EXT, layout_color_attachment_EXT)
			 , Attachment.make(format_r8g8b8a8_EXT, layout_undefined_EXT, layout_color_attachment_EXT)
			 , Attachment.make(format_r16g16b16a16_EXT, layout_undefined_EXT, layout_color_attachment_EXT)
   			 , Attachment.make(format_r16g16b16a16_EXT, layout_undefined_EXT, layout_color_attachment_EXT) },

   depth_attachment = Attachment.make(gpu_supported_depth_format_EXT, layout_undefined_EXT, layout_depth_attachment_EXT),
   
   subpasses = {
      subpass0 = {
	 color_output = { Subpass_Attachment_Reference.make(0, layout_color_attachment_EXT)
			  , Subpass_Attachment_Reference.make(1, layout_color_attachment_EXT)
			  , Subpass_Attachment_Reference.make(2, layout_color_attachment_EXT)
			  , Subpass_Attachment_Reference.make(3, layout_color_attachment_EXT) },
	 
	 depth_output = Subpass_Attachment_Reference.make(4, layout_depth_attachment_EXT)
      },
      subpass1 = {
	 color_output = { Subpass_Attachment_Reference.make(0, layout_color_attachment_EXT) },

	 input = { Subpass_Attachment_Reference.make(1, layout_shader_read_EXT)
		   , Subpass_Attachment_Reference.make(2, layout_shader_read_EXT)
		   , Subpass_Attachment_Reference.make(3, layout_shader_read_EXT) }
      }
   }

---   dependencies = {
      
---   }
}
