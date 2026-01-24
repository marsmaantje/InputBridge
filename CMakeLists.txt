cmake_minimum_required(VERSION 3.15)
project(InputBridge VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Source Files: Specifically include the ImGui backend files
# If your backends are in lib/imgui, we must tell CMake to compile them
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "lib/imgui/*.cpp"
)

# 2. Define Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# 3. Include Paths
# Ensure these paths lead directly to where the .h files sit
target_include_directories(${PROJECT_NAME} PRIVATE 
    "include"
    "lib/sdl"
    "lib/imgui"
)

# 4. Library Search Paths
target_link_directories(${PROJECT_NAME} PRIVATE 
    "lib/sdl"
    "lib/imgui"
)

# 5. Linking
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    SDL3
    imgui
)

# 6. Linux System Dependencies
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL dl pthread)
endif()

# 7. Output location
set_target_properties(${PROJECT_NAME} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output"
)

# 8. Windows DLL Helper
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/lib/sdl/SDL3.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
endif()