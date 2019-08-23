local globals = {}

--- w = width, d = depth
globals.terrain_dimensions = function (w, d) return { width = w, depth = d } end
globals.vector2 = function (v_x, v_y) return { x = v_x, y = v_y } end
globals.vector3 = function (v_x, v_y, v_z) return { x = v_x, y = v_y, z = v_z } end
globals.color3 = function (c_r, c_g, c_b) return { r = c_r, g = c_g, v = c_b } end
globals.quaternion = function (r_x, r_y, r_z) return { x = r_x, y = r_y, z = r_z } end

globals.initialize_terrain_base = function (base_name, dimensions_wd)
   --- Call internal function
   internal_initialize_terrain_base(base_name,
                                    dimensions_wd.width, dimensions_wd.depth)
end

globals.initialize_terrain_instance = function (base_name, position_xyz, rotation_xyz, size_f, color_rgb, gravity_f)
   internal_initialize_terrain_instance(base_name,
                                        position_xyz.x, position_xyz.y, position_xyz.z,
                                        rotation_xyz.x, rotation_xyz.y, rotation_xyz.z,
                                        size_f,
                                        color_rgb.r, color_rgb.g, color_rgb.b,
                                        gravity_f)
end

return globals
