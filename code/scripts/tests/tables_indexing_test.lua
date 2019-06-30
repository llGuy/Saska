random_thing = 42

function some_structure(x)
   return {
      name = "boo",
      my_x = x
   }
end

a_table = {

   structures = { [0] = some_structure(42), [1] = some_structure(43) }
   
}
