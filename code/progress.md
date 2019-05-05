# 4/5/19
Decided to refactor the game - really need to organize the ownership problems...
Decided to completely take out the rendering.hpp/cpp files because I don't like them.
They cause a lot of problems regarding ownership and it really bothers me.
I'm going to slowly take out the functions one by one, to eventually totally get rid of the file.
However, one thing that I need to sort out before even coming close to that, is loading descriptors from JSON
I am working on that right now.