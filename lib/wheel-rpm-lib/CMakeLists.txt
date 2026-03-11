
cmake_minimum_required(VERSION 3.16)
project(wheelrpm)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SRC
    src/*.cpp
)

add_library(wheelrpm ${SRC})

target_include_directories(wheelrpm PUBLIC include)

# Only call find_package if SDL3 hasn't already been brought in by a parent
# CMakeLists (e.g. InputBridge's add_subdirectory(lib/sdl)).
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 REQUIRED)
endif()
target_link_libraries(wheelrpm PUBLIC SDL3::SDL3)
