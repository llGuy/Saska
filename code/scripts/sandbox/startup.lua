function startup()
   c_out("- Executing Startup Script -\n")

   globals.initialize_terrain_base("terrain_base.medium_base", globals.terrain_dimensions(21, 21))

   grass_color = globals.color3(118.0 / 255.0, 169.0 / 255.0, 72.0 / 255.0)
   orange_color = globals.color3(169.0 / 255.0, 118.0 / 255.0, 72.0 / 255.0)
   globals.initialize_terrain_instance("terrain_base.medium_base",
                                       globals.vector3(0, 0, 0),
                                       globals.quaternion(0, 0, 0),
                                       15,
                                       grass_color,
                                       8.5)

   distance_from_center_of_world = 15 * 30
   globals.initialize_terrain_instance("terrain_base.medium_base",
                                       globals.vector3(-distance_from_center_of_world, 170, 0),
                                       globals.quaternion(0, 0, -30),
                                       15,
                                       orange_color,
                                       8.5)

   globals.initialize_terrain_instance("terrain_base.medium_base",
                                       globals.vector3(distance_from_center_of_world, 30, 0),
                                       globals.quaternion(0, 0, 30),
                                       15,
                                       orange_color,
                                       8.5)
   
end
