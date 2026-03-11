cmake_minimum_required(VERSION 3.16)
project(InputBridge)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

set(ENABLE_EXCLUSIVE_INPUT OFF CACHE BOOL "Enable exclusive input mode support" FORCE)
set(ENABLE_WEBSOCKETS ON CACHE BOOL "Enable WebSocket server support" FORCE)
set(ENABLE_OSC ON CACHE BOOL "Enable WebSocket server support" FORCE)

# Add SDL3
# We assume lib/sdl contains the SDL3 source code.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/sdl/CMakeLists.txt")
    # Fix for CI: Disable XScrnSaver dependency
    #set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "Disable XScrnSaver" FORCE)
    #set(SDL_X11_XCURSOR OFF CACHE BOOL "Disable XCursor" FORCE)
    #set(SDL_X11_XINPUT OFF CACHE BOOL "Disable XInput" FORCE)
    #set(SDL_X11_XRANDR OFF CACHE BOOL "Disable XRandR" FORCE)
    #set(SDL_X11_XSHAPE OFF CACHE BOOL "Disable XShape" FORCE)
    #set(SDL_X11_XFIXES OFF CACHE BOOL "Disable XFixes" FORCE)
    add_subdirectory(lib/sdl)
else()
    # Fallback to system installed SDL3
    find_package(SDL3 REQUIRED)
endif()

# Add nlohmann_json
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE lib/nlohmann_json/include)

# Add liblo
# Configure liblo options to minimize build targets and enforce static linking
set(WITH_TOOLS OFF CACHE BOOL "Disable liblo tools" FORCE)
set(WITH_TESTS OFF CACHE BOOL "Disable liblo tests" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "Disable liblo examples" FORCE)
set(WITH_CPP_TESTS OFF CACHE BOOL "Disable liblo C++ tests" FORCE)
set(WITH_STATIC ON CACHE BOOL "Enable liblo static lib" FORCE)

# liblo's CMakeLists.txt is located in the 'cmake' subdirectory
if(DEFINED CMAKE_WARN_DEPRECATED)
    set(CMAKE_WARN_DEPRECATED_OLD ${CMAKE_WARN_DEPRECATED})
else()
    unset(CMAKE_WARN_DEPRECATED_OLD)
endif()
set(CMAKE_WARN_DEPRECATED OFF)
add_subdirectory(lib/liblo/cmake lib/liblo)
if(DEFINED CMAKE_WARN_DEPRECATED_OLD)
    set(CMAKE_WARN_DEPRECATED ${CMAKE_WARN_DEPRECATED_OLD})
else()
    unset(CMAKE_WARN_DEPRECATED)
endif()

# Create an alias 'lo' to the static library target 'liblo_static'
# This ensures compatibility with the rest of the project that links against 'lo'
add_library(lo ALIAS liblo_static)

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
if(ENABLE_WEBSOCKETS)
    # For flatpak, we provide sources via the manifest.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/uSockets_download/src" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/uWebSockets_download/src")
        message(STATUS "Using pre-downloaded uSockets/uWebSockets for Flatpak build.")
        set(usockets_lib_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/uSockets_download)
        set(uwebsockets_lib_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/uWebSockets_download)
    else()
        include(FetchContent)

        # Suppress warning about FetchContent_Populate being deprecated (CMake 3.30+)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()

        # Libuv (Required for uSockets on Windows)
        if(WIN32)
            FetchContent_Declare(
                libuv
                GIT_REPOSITORY https://github.com/libuv/libuv.git
                GIT_TAG v1.48.0
                GIT_SUBMODULES ""
            )
            FetchContent_MakeAvailable(libuv)
        endif()

        # uSockets
        FetchContent_Declare(
            usockets_lib
            GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
            GIT_TAG master
            GIT_SUBMODULES ""
        )
        FetchContent_GetProperties(usockets_lib)
        if(NOT usockets_lib_POPULATED)
            FetchContent_Populate(usockets_lib)
        endif()

        # uWebSockets
        FetchContent_Declare(
            uwebsockets_lib
            GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
            GIT_TAG master
            GIT_SUBMODULES ""
        )
        FetchContent_GetProperties(uwebsockets_lib)
        if(NOT uwebsockets_lib_POPULATED)
            FetchContent_Populate(uwebsockets_lib)
        endif()
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

    # Add compile definitions to prevent winsock conflicts
    if(WIN32)
        target_compile_definitions(uSockets PUBLIC
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _WINSOCK_DEPRECATED_NO_WARNINGS
        )
    endif()

    if(OpenSSL_FOUND)
        target_link_libraries(uSockets PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(uSockets PUBLIC LIBUS_USE_OPENSSL)

        if(WIN32)
            set(OPENSSL_LIB_PATH "${OPENSSL_CRYPTO_LIBRARY}")
            # Handle the case where OPENSSL_CRYPTO_LIBRARY is a list (e.g., optimized;path;debug;path)
            if("${OPENSSL_LIB_PATH}" MATCHES ";")
                foreach(ITEM ${OPENSSL_LIB_PATH})
                    if(EXISTS "${ITEM}")
                        set(OPENSSL_LIB_PATH "${ITEM}")
                        break()
                    endif()
                endforeach()
            endif()

            get_filename_component(OPENSSL_LIB_DIR "${OPENSSL_LIB_PATH}" DIRECTORY)
            find_file(OPENSSL_CRYPTO_DLL NAMES libcrypto-3-x64.dll libcrypto-1_1-x64.dll HINTS "${OPENSSL_LIB_DIR}/../bin" NO_DEFAULT_PATH)
            find_file(OPENSSL_SSL_DLL NAMES libssl-3-x64.dll libssl-1_1-x64.dll HINTS "${OPENSSL_LIB_DIR}/../bin" NO_DEFAULT_PATH)
            if(OPENSSL_CRYPTO_DLL AND OPENSSL_SSL_DLL)
                install(FILES ${OPENSSL_CRYPTO_DLL} ${OPENSSL_SSL_DLL} DESTINATION bin)
            endif()
        endif()
    else()
        target_compile_definitions(uSockets PUBLIC LIBUS_NO_SSL)
    endif()

    if(WIN32)
        target_compile_definitions(uSockets PUBLIC LIBUS_USE_LIBUV)
        target_link_libraries(uSockets PUBLIC uv_a ws2_32)
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

endif() # if(ENABLE_WEBSOCKETS)

# Add wheel-rpm-lib (steering wheel RPM LED library)
# Must come after SDL3 is set up so SDL3::SDL3 is already defined.
add_subdirectory(lib/wheel-rpm-lib)

# Find Threads (required for std::thread)
find_package(Threads REQUIRED)

#if(ENABLE_OSC AND NOT TARGET lo)
#    message(WARNING "liblo not found. Disabling OSC support.")
#    set(ENABLE_OSC OFF)
#endif()

# InputBridge Executable
add_executable(InputBridge
    WIN32 MACOSX_BUNDLE
    # ... put source files here ...
    src/main.cpp
    # Core utilities
    src/Core/UndoRedo.cpp
    src/Core/BackupManager.cpp
    src/Core/ProtocolValidator.cpp
    # Device management
    src/Devices/DeviceManager.cpp
    src/Devices/DeviceFactory.cpp
    src/Devices/VirtualDeviceManager.cpp
    # Haptic and force feedback
    src/Haptics/HapticDevice.cpp
    src/Haptics/GamepadHaptics.cpp
    src/Haptics/SteeringWheelHaptics.cpp
    src/Haptics/DualSenseTriggerEffectGenerator.cpp
    src/Haptics/DualSenseController.cpp
    src/Haptics/XboxController.cpp
    # Settings
    src/Preferences/Preferences.cpp
    # Input device visualizers
    src/Visualizers/GenericVisualizer.cpp
    src/Visualizers/GamepadVisualizer.cpp
    src/Visualizers/GamepadHapticsVisualizer.cpp
    src/Visualizers/FlightStickVisualizer.cpp
    src/Visualizers/SteeringWheelVisualizer.cpp
    src/Visualizers/SteeringWheelHapticsVisualizer.cpp
    src/Visualizers/VirtualDeviceVisualizer.cpp
    src/Visualizers/WiimoteVisualizer.cpp
    # Networking
    src/Network/NetworkStatusWindow.cpp
    # Device input and protocol output mappings
    src/Mappers/InputMapper.cpp
    src/Mappers/OutputMapper.cpp
    # Webseerver and OSC protocol
    src/Protocols/OSCProtocol.cpp
    src/Protocols/ProtocolManager.cpp
    src/Protocols/WebSocketProtocol.cpp
    src/Protocols/MarsmaantjeOldProtocol.cpp
    src/Protocols/MarsmaantjeNewProtocol.cpp
    src/Protocols/OSCBaseProtocol.cpp
    src/Protocols/OSCBackAllyRacingProtocol.cpp
    src/Protocols/OSCSteamLinkProtocol.cpp
    src/Protocols/OSCProjectBabbleProtocol.cpp
    src/Protocols/ProtocolEditorWindow.cpp
    src/Protocols/ProtocolRegistry.cpp
    # Utilities
    src/Utils/FileDialog.cpp
    # UI helpers
    src/UI/ThemeManager.cpp
)

if(ENABLE_WEBSOCKETS)
    target_sources(InputBridge PRIVATE
        src/Network/WebSocketServer.cpp
        src/Protocols/WebSocketProtocol.cpp
    )
endif()

if(ENABLE_OSC)
    target_sources(InputBridge PRIVATE
        src/Network/OSCServer.cpp
        src/Protocols/OSCProtocol.cpp
    )
endif()

target_include_directories(InputBridge PRIVATE src)

if(ENABLE_EXCLUSIVE_INPUT)
    target_sources(InputBridge PRIVATE
        src/Mappers/InputExclusiveMode.cpp
        src/Mappers/LinuxExclusiveMode.cpp
        src/Mappers/MacOSExclusiveMode.cpp
        src/Mappers/WindowsExclusiveMode.cpp
    )
endif()

# Add compile definitions to InputBridge
if(WIN32)
    target_compile_definitions(InputBridge PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# CRITICAL: Apply the preprocessor definitions based on CMake options
if(ENABLE_EXCLUSIVE_INPUT)
    target_compile_definitions(InputBridge PRIVATE ENABLE_EXCLUSIVE_INPUT=1)
    message(STATUS "Exclusive input mode: ENABLED")
else()
    message(STATUS "Exclusive input mode: DISABLED")
endif()

if(ENABLE_WEBSOCKETS)
    target_compile_definitions(InputBridge PRIVATE ENABLE_WEBSOCKETS=1)
    message(STATUS "WebSocket support: ENABLED")
else()
    message(STATUS "WebSocket support: DISABLED")
endif()

if(ENABLE_OSC)
    target_compile_definitions(InputBridge PRIVATE ENABLE_OSC=1)
    message(STATUS "OSC support: ENABLED")
else()
    message(STATUS "OSC support: DISABLED")
endif()

# Link libraries - order matters on Windows!
target_link_libraries(InputBridge PRIVATE
    imgui
    SDL3::SDL3
    Threads::Threads
    nlohmann_json
    wheelrpm
)

if(ENABLE_WEBSOCKETS)
    target_link_libraries(InputBridge PRIVATE uWebSockets)
endif()

# Platform-specific libs for exclusive device access
if(TARGET lo)
    target_link_libraries(InputBridge PRIVATE lo)
    # Explicitly add liblo include directories
    target_include_directories(InputBridge PRIVATE lib/liblo ${CMAKE_BINARY_DIR}/lib/liblo)
endif()

if(WIN32)
    if(ENABLE_EXCLUSIVE_INPUT)
        # Link in correct order: dinput8 -> dxguid
        target_link_libraries(InputBridge PRIVATE dinput8 dxguid)
    endif()
    # Windows native file dialogs and networking
    target_link_libraries(InputBridge PRIVATE ws2_32 comdlg32)
endif()

if(APPLE AND ENABLE_EXCLUSIVE_INPUT)
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

    # Generate a script to copy other runtime dependencies (OpenSSL, ZLib, etc.) to the build directory
    set(COPY_DEPS_SCRIPT "${CMAKE_BINARY_DIR}/CopyRuntimeDependencies.cmake")
    file(WRITE "${COPY_DEPS_SCRIPT}" "
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \"\${EXECUTABLE_FILE}\"
            RESOLVED_DEPENDENCIES_VAR RDEPS
            UNRESOLVED_DEPENDENCIES_VAR URDEPS
            DIRECTORIES \"\${BINARY_DIR}\"
            POST_EXCLUDE_REGEXES \"^C:/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*\"
        )
        foreach(DEP \${RDEPS})
            file(INSTALL DESTINATION \"\${BINARY_DIR}\" TYPE SHARED_LIBRARY FILES \"\${DEP}\")
        endforeach()
    ")
    add_custom_command(TARGET InputBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXECUTABLE_FILE=$<TARGET_FILE:InputBridge> -DBINARY_DIR=$<TARGET_FILE_DIR:InputBridge> -P "${COPY_DEPS_SCRIPT}"
        COMMENT "Resolving and copying runtime dependencies..."
    )

    install(FILES $<TARGET_FILE:SDL3::SDL3> DESTINATION bin)
endif()

install(TARGETS InputBridge
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# Copy the themes/ folder next to the executable at build time so the
# program can find its themes in the development build layout.
add_custom_command(TARGET InputBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/themes"
    "$<TARGET_FILE_DIR:InputBridge>/themes"
    COMMENT "Copying themes/ to build output directory"
)

# Install the themes/ folder alongside the binary.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/themes"
    DESTINATION bin
    FILES_MATCHING PATTERN "*.json"
)

# Copy the fonts/ folder next to the executable at build time so the
# program can find its fonts in the development build layout.
add_custom_command(TARGET InputBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
    "$<TARGET_FILE_DIR:InputBridge>/fonts"
    COMMENT "Copying fonts/ to build output directory"
)

# Install the fonts/ folder alongside the binary.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
    DESTINATION bin
    FILES_MATCHING PATTERN "*.ttf" PATTERN "*.otf"
)

if(UNIX AND NOT APPLE)
    # Generate basic desktop file and icon for packaging (Required for AppImage/Flatpak)
    file(WRITE ${CMAKE_BINARY_DIR}/org.inputbridge.InputBridge.desktop
        "[Desktop Entry]\nName=InputBridge\nExec=InputBridge\nType=Application\nCategories=Utility;\nIcon=org.inputbridge.InputBridge\n"
    )
    install(FILES ${CMAKE_BINARY_DIR}/org.inputbridge.InputBridge.desktop DESTINATION share/applications)

    file(WRITE ${CMAKE_BINARY_DIR}/org.inputbridge.InputBridge.svg "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect width='64' height='64' fill='#333'/><text x='50%' y='50%' dominant-baseline='middle' text-anchor='middle' fill='white' font-size='10'>IB</text></svg>")
    install(FILES ${CMAKE_BINARY_DIR}/org.inputbridge.InputBridge.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

if(WIN32)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin)
    endif()

    # This script will be executed at install time.
    # It finds all runtime dependencies of InputBridge.exe and installs them into the package.
    install(CODE "
        set(INSTALL_DIR \"\${CMAKE_INSTALL_PREFIX}/bin\")
        message(STATUS \"Gathering runtime dependencies for \${INSTALL_DIR}/InputBridge.exe\")
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \"\${INSTALL_DIR}/InputBridge.exe\"
            RESOLVED_DEPENDENCIES_VAR RDEPS
            UNRESOLVED_DEPENDENCIES_VAR URDEPS
            POST_EXCLUDE_REGEXES \"^C:/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*\"
        )
        if(URDEPS)
            message(WARNING \"CPack: Unresolved dependencies: \${URDEPS}\")
        endif()
        if(RDEPS)
            message(STATUS \"CPack: Installing runtime dependencies: \${RDEPS}\")
            file(INSTALL DESTINATION \"\${INSTALL_DIR}\" FILES \${RDEPS})
        endif()
    ")
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

# Install Licenses
set(LICENSE_DIR "bin/licenses")

install(FILES ${IMGUI_DIR}/LICENSE.txt DESTINATION ${LICENSE_DIR} RENAME IMGUI_LICENSE.txt OPTIONAL)
install(FILES lib/nlohmann_json/LICENSE.MIT DESTINATION ${LICENSE_DIR} RENAME JSON_LICENSE.txt OPTIONAL)
install(FILES lib/liblo/COPYING DESTINATION ${LICENSE_DIR} RENAME LIBLO_LICENSE.txt OPTIONAL)
install(FILES lib/sdl/LICENSE.txt DESTINATION ${LICENSE_DIR} RENAME SDL3_LICENSE.txt OPTIONAL)

if(ENABLE_WEBSOCKETS)
    if(DEFINED usockets_lib_SOURCE_DIR)
        install(FILES ${usockets_lib_SOURCE_DIR}/LICENSE DESTINATION ${LICENSE_DIR} RENAME USOCKETS_LICENSE OPTIONAL)
    endif()
    if(DEFINED uwebsockets_lib_SOURCE_DIR)
        install(FILES ${uwebsockets_lib_SOURCE_DIR}/LICENSE DESTINATION ${LICENSE_DIR} RENAME UWEBSOCKETS_LICENSE OPTIONAL)
        install(FILES ${uwebsockets_lib_SOURCE_DIR}/libdeflate/COPYING DESTINATION ${LICENSE_DIR} RENAME LIBDEFLATE_COPYING OPTIONAL)
    endif()
    if(WIN32 AND DEFINED libuv_SOURCE_DIR)
        install(FILES ${libuv_SOURCE_DIR}/LICENSE DESTINATION ${LICENSE_DIR} RENAME LIBUV_LICENSE OPTIONAL)
    endif()
endif()
