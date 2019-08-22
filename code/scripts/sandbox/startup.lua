function startup ()
   c_out("- Running startup -")

   initialize_terrain_base("terrain_base.medium_terrain", 21, 21)

   local main_terrain_information = {}
   main_terrain_information.x = 0
   main_terrain_information.y = 0
   main_terrain_information.z = 0
   main_terrain_information.r = 0
   main_terrain_information.g = 0.8
   main_terrain_information.b = 0

   initialize_terrain_instance("terrain_base.medium_terrain",
                               main_terrain_information.x,
                               main_terrain_information.y,
                               main_terrain_information.z,
                               10,
                               main_terrain_information.r,
                               main_terrain_information.g,
                               main_terrain_information.b)

   load_mesh("spaceman.mesh_custom")
end
