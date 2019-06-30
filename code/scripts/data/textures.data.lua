globals = require "globals"

function make_resolution(p_w, p_h)
   return {
      width = w,
      height = h
   }
end

--- make_texture_EXT params : name, resolution, layer_count, format, usage, dimensions (2 or 3)

--- Make GBuffer objects
swapchain_resolution = make_resolution(swapchain_width_EXT, swapchain_height_EXT)
g_buffer_color_usage = texture_usage_input_attachment_EXT | texture_usage_color_attachment_EXT
make_texture_EXT("texture_g_buffer_albedo", swapchain_resolution, 1, swapchain_format_EXT, g_buffer_color_usage, 2)
make_texture_EXT("texture_g_buffer_position", swapchain_resolution, 1, format_r16g16b16a16_EXT, g_buffer_color_usage, 2)
make_texture_EXT("texture_g_buffer_normal", swapchain_resolution, 1, format_r16g16b16a16_EXT, g_buffer_color_usage, 2)
make_texture_EXT("texture_g_buffer_depth", swapchain_resolution, 1, gpu_supported_depth_format_EXT. texture_usage_depth_attachment_EXT, 2)

--- Make atmosphere cubemap
atmosphere_resolution = make_resolution(globals.atmosphere_width, globals.atmosphere_height)
make_texture_EXT("atmosphere_cubemap", atmosphere_resolution, 6, format_r8g8b8a8_EXT, texture_usage_sampler_EXT | texture_usage_color_attachment_EXT, 3)

--- Make shadowmap
shadowmap_resolution = make_resolution(globals.shadowmap_width, globals.shadowmap_height)
make_texture_EXT("texture_shadowmap", shadowmap_resolution, 1, gpu_supported_depth_format_EXT, texture_usage_sampler_EXT | texture_usage_depth_attachment_EXT, 2)
