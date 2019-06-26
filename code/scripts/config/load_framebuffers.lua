globals = require "globals"

framebuffer_g_buffer = {
   resolution = {
      --- Marked with _EXT means defined from C++ side
      width = swapchain_width_EXT,
      height = swapchain_height_EXT
   },

   layer_count = 1,

   compatible_render_pass = "render_pass_deferred",

   color_attachments = { "texture_g_buffer_albedo"
			 , "texture_g_buffer_position"
			 , "texture_g_buffer_normal" },

   depth_attachment = "texture_g_buffer_depth"
}

framebuffer_atmosphere = {
   resolution = {
      width = globals.atmosphere_width,
      height = globals.atmosphere_height
   },

   layer_count = 6;
   
   compatible_render_pass = "render_pass_atmosphere",

   color_attachments = { "texture_atmosphere_cubemap" }
}

framebuffer_shadowmap = {
   resolution = {
      --- Maybe make this configurable in the future
      width = globals.shadowmap_width,
      height = globals.shadowmap_height
   },

   --- When PSSM will be implemented, this will change
   layer_count = 1,

   compatible_render_pass = "render_pass_shadowmap",

   depth_attachment = "texture_shadowmap"
}

--- Need to specify the loaded framebuffer names for the game to know which ones to load
loaded_framebuffers = { "framebuffer_g_buffer"
			, "framebuffer_atmosphere"
			, "framebuffer_shadowmap" }
