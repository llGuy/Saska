@echo off

set VULKAN_SHADER_COMPILER=C:/VulkanSDK/1.1.108.0/Bin32/glslangValidator.exe

If "%1" == "compile" goto compile
If "%1" == "all" goto all

:all
%VULKAN_SHADER_COMPILER% -V -o SPV/atmosphere.frag.spv atmosphere.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/atmosphere.geom.spv atmosphere.geom
%VULKAN_SHADER_COMPILER% -V -o SPV/atmosphere.vert.spv atmosphere.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/atomsphere_init.vert.spv atomsphere_init.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/debug_frustum.frag.spv debug_frustum.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/debug_frustum.vert.spv debug_frustum.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/deferred_lighting.frag.spv deferred_lighting.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/deferred_lighting.vert.spv deferred_lighting.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/explosion_particle.frag.spv explosion_particle.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/explosion_particle.vert.spv explosion_particle.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/hitbox_render.frag.spv hitbox_render.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/hitbox_render.vert.spv hitbox_render.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_animated.frag.spv lp_notex_animated.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_animated.geom.spv lp_notex_animated.geom
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_animated.vert.spv lp_notex_animated.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_model.frag.spv lp_notex_model.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_model.geom.spv lp_notex_model.geom
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_model_shadow.frag.spv lp_notex_model_shadow.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_model_shadow.vert.spv lp_notex_model_shadow.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/lp_notex_model.vert.spv lp_notex_model.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/model.frag.spv model.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/model.geom.spv model.geom
%VULKAN_SHADER_COMPILER% -V -o SPV/model_shadow.frag.spv model_shadow.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/model_shadow.vert.spv model_shadow.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/model.vert.spv model.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/pfx_final.frag.spv pfx_final.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/pfx_final.vert.spv pfx_final.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/pfx_ssr.frag.spv pfx_ssr.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/pfx_ssr.vert.spv pfx_ssr.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/render_atmosphere.frag.spv render_atmosphere.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/render_atmosphere.vert.spv render_atmosphere.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/screen_quad.frag.spv screen_quad.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/screen_quad.vert.spv screen_quad.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/skybox.vert.spv skybox.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/sun.frag.spv sun.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/sun.vert.spv sun.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain.frag.spv terrain.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain.geom.spv terrain.geom
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain_pointer.frag.spv terrain_pointer.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain_pointer.vert.spv terrain_pointer.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain_shadow.frag.spv terrain_shadow.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain_shadow.vert.spv terrain_shadow.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/terrain.vert.spv terrain.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/ui2Dquad.vert.spv ui2Dquad.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/uifontquad.frag.spv uifontquad.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/uifontquad.vert.spv uifontquad.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/uiquad.frag.spv uiquad.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/uiquad.vert.spv uiquad.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_mesh.frag.spv voxel_mesh.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_mesh.geom.spv voxel_mesh.geom
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_mesh_shadow.frag.spv voxel_mesh_shadow.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_mesh_shadow.vert.spv voxel_mesh_shadow.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_mesh.vert.spv voxel_mesh.vert
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_point.frag.spv voxel_point.frag
%VULKAN_SHADER_COMPILER% -V -o SPV/voxel_point.vert.spv voxel_point.vert
goto eof

:compile
make
goto eof

:eof
popd
