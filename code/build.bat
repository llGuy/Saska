@echo off

set CFLAGS=/std:c++latest -Zi /EHsc

cl /std:c++latest -Zi /EHsc /DGLM_ENABLE_EXPERIMENTAL /DUNITY_BUILD /DSTB_IMAGE_IMPLEMENTATION /I C:/dependencies /I C:/dependencies/c++ /I C:/VulkanSDK/1.1.108.0/Include /I C:/dependencies/glfw-3.2.1.bin.WIN64/include /I C:/dependencies/stb-master /I C:/dependencies/Lua/include/ /DEBUG:FULL /FeSaska.exe core.cpp user32.lib User32.lib Gdi32.lib Shell32.lib kernel32.lib gdi32.lib msvcrt.lib msvcmrt.lib C:/VulkanSDK/1.1.108.0/Lib/vulkan-1.lib C:/dependencies/glfw-3.2.1.bin.WIN64/lib-vc2015/glfw3.lib C:/dependencies/Lua/lib/lua5.1.lib
