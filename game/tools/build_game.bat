@echo off

REM ------------- Run vcvars64.bat to get access to cl and devenv -------------
set CC=cl
set DB=devenv
REM ---------------------------------------------------------------------------

set CFLAGS=-Zi /EHsc /std:c++latest /DEBUG:FULL

REM REMOVE DEVELOPMENT MODE FOR RELEASE
set DEF=/DGLM_ENABLE_EXPERIMENTAL /DUNITY_BUILD /DSTB_IMAGE_IMPLEMENTATION /D_MBCS /DDEVELOPMENT_MODE
REM set GLFW_INC_DIR=/I C:/dependencies/glfw-3.2.1.bin.WIN64/include



REM -------------- TO MODIFY -------------------
set GLM_INC_DIR=C:/dependencies/
set VULKAN_INC_DIR=C:/VulkanSDK/1.1.108.0
set STB_INC_DIR=C:/dependencies/stb-master/
set VML_INC_DIR=C:/dependencies/vml/vml/
set LUA_INC_DIR=C:/dependencies/Lua
REM --------------------------------------------



set INC=/I %GLM_INC_DIR% /I %VULKAN_INC_DIR%/Include /I %LUA_INC_DIR%/include /I %VML_INC_DIR% /I %STB_INC_DIR%
 
set CLIENT_BIN=Saska.exe
set VULKAN_DLL=vulkan.dll
set SERVER_BIN=Server.exe
set DLL_NAME=Saska.dll

set SRC=../source/win32_core.cpp ../source/allocators.cpp ../source/bullet.cpp ../source/camera.cpp ../source/camera_view.cpp ../source/chunk.cpp ../source/chunks_gstate.cpp ../source/client.cpp ../source/collision.cpp ../source/component.cpp ../source/core.cpp ../source/entities_gstate.cpp ../source/file_system.cpp ../source/game.cpp ../source/game_input.cpp ../source/gamestate.cpp ../source/graphics.cpp ../source/memory.cpp ../source/net.cpp ../source/particles_gstate.cpp ../source/player.cpp ../source/script.cpp ../source/serializer.cpp ../source/server.cpp ../source/sockets.cpp ../source/thread_pool.cpp ../source/ui.cpp ../source/vulkan.cpp

set VULKAN_SRC=../source/renderer/vulkan.cpp
set DLL_SRC=game.cpp

REM Don't need GLFW for now: C:/dependencies/glfw-3.2.1.bin.WIN64/lib-vc2015/glfw3.lib
REM If link errors appear, maybe add these libs into the list: Shell32.lib kernel32.lib msvcmrt.lib 
set LIBS=xinput.lib ws2_32.lib winmm.lib user32.lib gdi32.lib msvcrt.lib %VULKAN_INC_DIR%/Lib/vulkan-1.lib %LUA_INC_DIR%/lib/lua5.1.lib

pushd ..\binaries

If "%1" == "all" goto all
If "%1" == "compile" goto compile
If "%1" == "debug" goto debug
If "%1" == "clean" goto clean
If "%1" == "run" goto run
If "%1" == "help" goto help

:all

%CC% %CFLAGS% /DCLIENT_APPLICATION %DEF% %INC% /Fe%CLIENT_BIN% %SRC% %LIBS%
popd

pushd ..\assets\shaders
build.bat all
popd

pushd ..\binaries

:compile
%CC% %CFLAGS% /DCLIENT_APPLICATION %DEF% %INC% /Fe%CLIENT_BIN% %SRC% %LIBS%

popd

goto :eof

:debug
%DB% %CLIENT_BIN%
goto :eof

:clean
rm *.exe *.obj *.ilk *.pdb TAGS
popd
goto :eof

:run
%CLIENT_BIN%

popd
goto :eof

:help
echo To build application, enter into command line: build.bat compile
echo To debug application, enter into command line: build.bat debug
echo To run application, enter into command line: build.bat run
echo To clean application, enter into command line: build.bat clean

:eof
