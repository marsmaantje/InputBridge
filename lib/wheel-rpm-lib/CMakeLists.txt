
cmake_minimum_required(VERSION 3.16)
project(wheelrpm)

set(CMAKE_CXX_STANDARD 17)

# Glob only the real translation units (.cpp).
# The per-wheel class definitions live in .ipp files under src/moza/ and are
# #included directly into wheel_manager.cpp so they share one TU.  They must
# NOT be compiled separately to avoid duplicate-symbol linker errors.
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
