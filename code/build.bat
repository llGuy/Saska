@echo off

set CC=cl
set CFLAGS=-Zi /EHsc /std:c++latest /DEBUG:FULL
set DEF=/DGLM_ENABLE_EXPERIMENTAL /DUNITY_BUILD /DSTB_IMAGE_IMPLEMENTATION
set GLFW_INC_DIR=/I C:/dependencies/glfw-3.2.1.bin.WIN64/include
set GLM_INC_DIR=/I C:/dependencies
set VULKAN_INC_DIR=/I C:/VulkanSDK/1.1.108.0/Include
set STB_INC_DIR=/I C:/dependencies/stb-master
set LUA_INC_DIR=/I C:/dependencies/Lua/include/
set INC=%GLM_INC_DIR% %VULKAN_INC_DIR% %GLFW_INC_DIR% %STB_INC_DIR% %LUA_INC_DIR% %STB_INC_DIR%
set BIN=Saska.exe
set SRC=core.cpp
set LIBS=user32.lib User32.lib Gdi32.lib Shell32.lib kernel32.lib gdi32.lib msvcrt.lib msvcmrt.lib C:/VulkanSDK/1.1.108.0/Lib/vulkan-1.lib C:/dependencies/glfw-3.2.1.bin.WIN64/lib-vc2015/glfw3.lib C:/dependencies/Lua/lib/lua5.1.lib

%CC% %CFLAGS% %DEF% %INC% /Fe%BIN% %SRC% %LIBS%
