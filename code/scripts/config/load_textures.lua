globals = require "globals"

texture_g_buffer_albedo = {
   resolution = {
      width = swapchain_width_EXT,
      height = swapchain_height_EXT
   },

   layer_count = 1,
   
   format = swapchain_format_EXT,
   
   usage = texture_usage_sampled_EXT | texture_usage_color_attachment_EXT,

   --- Can be 3 for a cubemap
   dimensions = 2
}

texture_g_buffer_position = {
   resolution = {
      width = swapchain_width_EXT,
      height = swapchain_height_EXT
   },

   layer_count = 1,
   
   format = format_r16g16b16a16_EXT,
   
   usage = texture_usage_sampled_EXT | texture_usage_color_attachment_EXT,

   dimensions = 2
}

texture_g_buffer_normal = {
   resolution = {
      width = swapchain_width_EXT,
      height = swapchain_height_EXT
   },

   layer_count = 1,
   
   format = format_r16g16b16a16_EXT,
   
   usage = texture_usage_sampled_EXT | texture_usage_color_attachment_EXT,

   dimensions = 2
}

texture_g_buffer_depth = {
   resolution = {
      width = swapchain_width_EXT,
      height = swapchain_height_EXT
   },

   layer_count = 1,
   
   format = gpu_supported_depth_format_EXT,
   
   usage = texture_usage_depth_attachment_EXT,

   dimensions = 2
}

texture_atmosphere_cubemap = {
   resolution = {
      width = globals.atmosphere_width,
      height = globals.atmosphere_height
   },

   layer_count = 6,
   
   format = format_r8g8b8a8_EXT,
   
   usage = texture_usage_sampler_EXT | texture_usage_color_attachment_EXT,

   dimensions = 3
}

texture_shadowmap = {
   resolution = {
      width = globals.shadowmap_width,
      height = globals.shadowmap_height
   },
   --- Later will be more with PSSM
   layer_count = 1,
   
   format = gpu_supported_depth_format_EXT,
   
   usage = texture_usage_sampler_EXT | texture_usage_depth_attachment_EXT,

   dimensions = 3
}

loaded_textures = { "texture_g_buffer_albedo"
		    , "texture_g_buffer_position"
		    , "texture_g_buffer_normal"
		    , "texture_g_buffer_depth"
		    , "texture_atmosphere_cubemap"
		    , "texture_shadowmap" }
