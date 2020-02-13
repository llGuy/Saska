cmake_minimum_required(VERSION "3.7.1")

project(saska)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

file(GLOB_RECURSE SOURCES "source/*.cpp" "source/*.hpp")

include_directories("${CMAKE_SOURCE_DIR}/dependencies/stb")
include_directories("${CMAKE_SOURCE_DIR}/dependencies/glm")
include_directories("${CMAKE_SOURCE_DIR}/dependencies/lua")



find_package(Vulkan)



if(NOT Vulkan_FOUND)

       link_directories("${CMAKE_SOURCE_DIR}/dependencies/vulkan/lib")
       include_directories("${CMAKE_SOURCE_DIR}/dependencies/vulkan/include")

       message("Using bundled Vulkan version")

else(NOT Vulkan_FOUND)

         include_directories("${Vulkan_INCLUDE_DIRS}")
         link_libraries("${Vulkan_LIBRARY}")

         message("Using preinstalled Vulkan")

endif()

if(WIN32)

        link_libraries("user32.lib" "gdi32.lib" "xinput.lib" "ws2_32.lib" "winmm.lib" "msvcrt.lib")

endif(WIN32)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGLM_ENABLE_EXPERIMENTAL -DSTB_IMAGE_IMPLEMENTATION -D_MBCS -DVK_USE_PLATFORM_WIN32_KHR")

add_library(lua "dependencies/lua/lapi.c"
                "dependencies/lua/lauxlib.c"
                "dependencies/lua/lbaselib.c"
                "dependencies/lua/lcode.c"
                "dependencies/lua/ldblib.c"
                "dependencies/lua/ldebug.c"
                "dependencies/lua/ldo.c"
                "dependencies/lua/ldump.c"
                "dependencies/lua/lfunc.c"
                "dependencies/lua/lgc.c"
                "dependencies/lua/linit.c"
                "dependencies/lua/liolib.c"
                "dependencies/lua/llex.c"
                "dependencies/lua/lmathlib.c"
                "dependencies/lua/lmem.c"
                "dependencies/lua/loadlib.c"
                "dependencies/lua/lobject.c"
                "dependencies/lua/lopcodes.c"
                "dependencies/lua/loslib.c"
                "dependencies/lua/lparser.c"
                "dependencies/lua/lstate.c"
                "dependencies/lua/lstring.c"
                "dependencies/lua/lstrlib.c"
                "dependencies/lua/ltable.c"
                "dependencies/lua/ltablib.c"
                "dependencies/lua/ltm.c"
                "dependencies/lua/lua.c"
                "dependencies/lua/luac.c"
                "dependencies/lua/lundump.c"
                "dependencies/lua/lvm.c"
                "dependencies/lua/lzio.c"
                "dependencies/lua/print.c" )


add_executable(saska WIN32 "${SOURCES}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries)

target_link_libraries(saska lua)

if(WIN32)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT saska)
endif(WIN32)
