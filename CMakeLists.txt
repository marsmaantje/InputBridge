cmake_minimum_required(VERSION 3.16)
project(InputBridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

# Add SDL3
# We assume lib/sdl contains the SDL3 source code.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/sdl/CMakeLists.txt")
    add_subdirectory(lib/sdl)
else()
    # Fallback to system installed SDL3
    find_package(SDL3 REQUIRED)
endif()

# Add ImGui
# ImGui does not have a root CMakeLists.txt, so we define the library here.
set(IMGUI_DIR lib/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# InputBridge Executable
add_executable(InputBridge
    # ... put source files here ...
    src/main.cpp
    src/DeviceManager.cpp
    src/Preferences.cpp
    src/GenericVisualizer.cpp
    src/GamepadVisualizer.cpp
    src/FlightStickVisualizer.cpp
    src/SteeringWheelVisualizer.cpp
)

target_link_libraries(InputBridge PRIVATE imgui SDL3::SDL3)

# Copy SDL3.dll to the output directory on Windows
if(WIN32)
    add_custom_command(TARGET InputBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:InputBridge>
    )
endif()