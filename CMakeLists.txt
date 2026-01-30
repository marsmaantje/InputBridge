cmake_minimum_required(VERSION 3.16)
project(InputBridge)

set(CMAKE_CXX_STANDARD 20)
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

# Add uWebSockets
include(FetchContent)

# Libuv (Required for uSockets on Windows)
if(WIN32)
    FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG        v1.48.0
    )
    FetchContent_MakeAvailable(libuv)
endif()

# uSockets
FetchContent_Declare(
    usockets_lib
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG        master
)
FetchContent_GetProperties(usockets_lib)
if(NOT usockets_lib_POPULATED)
    FetchContent_Populate(usockets_lib)
endif()

# Define uSockets library manually
set(USOCKETS_SOURCES
    ${usockets_lib_SOURCE_DIR}/src/bsd.c
    ${usockets_lib_SOURCE_DIR}/src/context.c
    ${usockets_lib_SOURCE_DIR}/src/loop.c
    ${usockets_lib_SOURCE_DIR}/src/socket.c
    ${usockets_lib_SOURCE_DIR}/src/udp.c
)

if(WIN32)
    list(APPEND USOCKETS_SOURCES ${usockets_lib_SOURCE_DIR}/src/eventing/libuv.c)
else()
    list(APPEND USOCKETS_SOURCES ${usockets_lib_SOURCE_DIR}/src/eventing/epoll_kqueue.c)
endif()

find_package(OpenSSL)
if(OpenSSL_FOUND)
    list(APPEND USOCKETS_SOURCES
        ${usockets_lib_SOURCE_DIR}/src/crypto/openssl.c
        ${usockets_lib_SOURCE_DIR}/src/crypto/sni_tree.cpp
    )
endif()

add_library(uSockets STATIC ${USOCKETS_SOURCES})
target_include_directories(uSockets PUBLIC ${usockets_lib_SOURCE_DIR}/src)

if(OpenSSL_FOUND)
    target_link_libraries(uSockets PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(uSockets PUBLIC LIBUS_USE_OPENSSL)
else()
    target_compile_definitions(uSockets PUBLIC LIBUS_NO_SSL)
endif()

if(WIN32)
    target_compile_definitions(uSockets PUBLIC LIBUS_USE_LIBUV)
    target_link_libraries(uSockets PUBLIC uv_a ws2_32)
endif()

# uWebSockets
FetchContent_Declare(
    uwebsockets_lib
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG        master
)
FetchContent_GetProperties(uwebsockets_lib)
if(NOT uwebsockets_lib_POPULATED)
    FetchContent_Populate(uwebsockets_lib)
endif()

add_library(uWebSockets INTERFACE)
target_include_directories(uWebSockets INTERFACE ${uwebsockets_lib_SOURCE_DIR}/src)
target_link_libraries(uWebSockets INTERFACE uSockets)

# ZLIB is required for uWebSockets
find_package(ZLIB)
if(ZLIB_FOUND)
    target_link_libraries(uWebSockets INTERFACE ZLIB::ZLIB)
else()
    target_compile_definitions(uWebSockets INTERFACE UWS_NO_ZLIB)
endif()

# Find Threads (required for std::thread)
find_package(Threads REQUIRED)

# InputBridge Executable
add_executable(InputBridge
    # ... put source files here ...
    src/main.cpp
    src/Devices/DeviceManager.cpp
    src/Preferences/Preferences.cpp
    src/Visualizers/GenericVisualizer.cpp
    src/Visualizers/GamepadVisualizer.cpp
    src/Visualizers/FlightStickVisualizer.cpp
    src/Visualizers/SteeringWheelVisualizer.cpp
    src/Mappers/InputMapper.cpp
    src/Network/WebSocketServer.cpp
    src/Protocols/OSCProtocol.cpp
    src/Protocols/WebSocketProtocol.cpp
    src/Protocols/ProtocolManager.cpp
)

target_include_directories(InputBridge PRIVATE src)
target_link_libraries(InputBridge PRIVATE imgui SDL3::SDL3 uWebSockets Threads::Threads)
target_compile_definitions(InputBridge PRIVATE ENABLE_WEBSOCKETS)

# Libs for exclusive device access
if(WIN32)
    target_link_libraries(InputBridge PRIVATE dinput8 dxguid)
endif()

if(APPLE)
    find_library(IOKIT_LIB IOKit)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    target_link_libraries(InputBridge PRIVATE ${IOKIT_LIB} ${COREFOUNDATION_LIB})
endif()

# Copy SDL3.dll to the output directory on Windows
if(WIN32)
    add_custom_command(TARGET InputBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:InputBridge>
    )
endif()