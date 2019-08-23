function startup()
   c_out("- Executing Startup Script -")

   globals.initialize_terrain_base("terrain_base.medium_base", globals.terrain_dimensions(21, 21))

   grass_color = globals.color3(118.0 / 255.0, 169.0 / 255.0, 72.0 / 255.0)
   globals.initialize_terrain_instance("terrain_base.medium_base",
                                       globals.vector3(0, 0, 200),
                                       globals.quaternion(60, 20, 0),
                                       15,
                                       grass_color,
                                       8.5)
end
