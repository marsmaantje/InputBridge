cmake_minimum_required(VERSION 3.16)
project(InputBridgeTests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Locate SDL3 ───────────────────────────────────────────────────────────────
if(NOT TARGET SDL3::SDL3)
    set(SDL3_PARENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl")
    if(EXISTS "${SDL3_PARENT_SRC}/CMakeLists.txt")
        add_subdirectory("${SDL3_PARENT_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)
    else()
        find_package(SDL3 REQUIRED)
    endif()
endif()

find_package(Threads REQUIRED)

# ── Fetch GoogleTest ──────────────────────────────────────────────────────────
include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ── Common paths ──────────────────────────────────────────────────────────────
set(IB_SRC      "${CMAKE_SOURCE_DIR}/src")
set(IB_JSON     "${CMAKE_SOURCE_DIR}/lib/nlohmann_json/include")
set(IB_WHEELRPM "${CMAKE_SOURCE_DIR}/src/WheelRpm/include")

# 1. test_result ───────────────────────────────────────────────────────────────
add_executable(test_result test_result.cpp)
target_include_directories(test_result PRIVATE ${IB_SRC})
target_link_libraries(test_result PRIVATE GTest::gtest_main)
gtest_discover_tests(test_result)

# 2. test_protocol_data ────────────────────────────────────────────────────────
add_executable(test_protocol_data test_protocol_data.cpp)
target_include_directories(test_protocol_data PRIVATE ${IB_SRC} ${IB_WHEELRPM})
target_link_libraries(test_protocol_data PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(test_protocol_data)

# 3. test_protocol_validator ───────────────────────────────────────────────────
add_executable(test_protocol_validator
    test_protocol_validator.cpp
    ${IB_SRC}/Core/ProtocolValidator.cpp
)
target_include_directories(test_protocol_validator PRIVATE ${IB_SRC} ${IB_JSON})
target_link_libraries(test_protocol_validator PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(test_protocol_validator)

# 4. test_undo_redo ────────────────────────────────────────────────────────────
add_executable(test_undo_redo
    test_undo_redo.cpp
    ${IB_SRC}/Core/UndoRedo.cpp
)
target_include_directories(test_undo_redo PRIVATE ${IB_SRC})
target_link_libraries(test_undo_redo PRIVATE GTest::gtest_main)
gtest_discover_tests(test_undo_redo)

# 5. test_backup_manager ───────────────────────────────────────────────────────
add_executable(test_backup_manager
    test_backup_manager.cpp
    ${IB_SRC}/Core/BackupManager.cpp
)
target_include_directories(test_backup_manager PRIVATE ${IB_SRC})
target_link_libraries(test_backup_manager PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(test_backup_manager)

# 6. test_dualsense_trigger ────────────────────────────────────────────────────
add_executable(test_dualsense_trigger
    test_dualsense_trigger.cpp
    ${IB_SRC}/Haptics/DualSenseTriggerEffectGenerator.cpp
)
target_include_directories(test_dualsense_trigger PRIVATE ${IB_SRC})
target_link_libraries(test_dualsense_trigger PRIVATE GTest::gtest_main)
gtest_discover_tests(test_dualsense_trigger)

# 7. test_preferences ─────────────────────────────────────────────────────────
add_executable(test_preferences
    test_preferences.cpp
    ${IB_SRC}/Preferences/Preferences.cpp
)
target_include_directories(test_preferences PRIVATE ${IB_SRC})
target_link_libraries(test_preferences PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(test_preferences)

# 8. test_haptic_parser ────────────────────────────────────────────────────────
add_executable(test_haptic_parser
    test_haptic_parser.cpp
    mocks/output_mapper_stub.cpp
    ${IB_SRC}/Network/HapticParser.cpp
)
target_include_directories(test_haptic_parser PRIVATE
    ${IB_SRC}
    ${IB_WHEELRPM}
    ${IB_JSON}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_haptic_parser PRIVATE
    GTest::gtest_main
    SDL3::SDL3
    Threads::Threads
)
gtest_discover_tests(test_haptic_parser DISCOVERY_MODE PRE_TEST)

# 9. test_osc_subchannel ───────────────────────────────────────────────────────
# Tests ParseSubchannelPath() in isolation - no liblo dependency required.
# This covers the path-parsing logic that OSCServer::haptic_subchannel_handler
# delegates to, verifying all four effect types, valid/invalid paths, slot
# extraction, and the Resonite multi-slot scenario.
add_executable(test_osc_subchannel
    test_osc_subchannel.cpp
    ${IB_SRC}/Network/OSCSubchannel.cpp
)
target_include_directories(test_osc_subchannel PRIVATE ${IB_SRC})
target_link_libraries(test_osc_subchannel PRIVATE GTest::gtest_main)
gtest_discover_tests(test_osc_subchannel)

# 10. test_sensor_state ────────────────────────────────────────────────────────
# Tests the GyroState, AccelState, and TouchState value-type structs defined in
# Devices/SensorState.h.  No SDL calls are made; the file is header-only, so no
# extra .cpp sources are needed.  Covers default construction, scale constants,
# all TouchState helpers (primaryX/Y/Pressure, centered remapping), and second-
# finger independence.
add_executable(test_sensor_state
    test_sensor_state.cpp
)
target_include_directories(test_sensor_state PRIVATE ${IB_SRC})
target_link_libraries(test_sensor_state PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(test_sensor_state)

# 11. test_input_source ────────────────────────────────────────────────────────
# Tests InputMapper::InputSource default construction, the SensorChannel enum
# ordering relied on by the serialisation code and the UI range-checks, and the
# output-range / deadzone / invert post-processing pipeline shared by
# ProcessSensor() and ProcessAxis().  The pipeline is replicated as a pure free
# function so there is no SDL or DeviceManager dependency.  Also covers
# ButtonToAnalogMapping and ButtonToDigitalMapping defaults.
add_executable(test_input_source
    test_input_source.cpp
)
target_include_directories(test_input_source PRIVATE ${IB_SRC} ${IB_WHEELRPM})
target_link_libraries(test_input_source PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(test_input_source)

# 12. test_wiimote_decoder ─────────────────────────────────────────────────────
# Tests the pure byte->struct decode functions in Devices/Wiimote/WiimoteDecoder.*
# (buttons, accelerometer LSB packing, IR basic-mode bit packing, extension-ID
# classification, Nunchuk/Classic Controller/Balance Board payload decoding,
# and Balance Board calibration + weight interpolation). No SDL_hid_* I/O is
# exercised here - WiimoteDevice/WiimoteManager (the raw-HID layer) are
# intentionally left untested until an injectable HID transport exists for
# them (see Devices/Wiimote/README.md's gap list).
add_executable(test_wiimote_decoder
    test_wiimote_decoder.cpp
    ${IB_SRC}/Devices/Wiimote/WiimoteDecoder.cpp
)
target_include_directories(test_wiimote_decoder PRIVATE ${IB_SRC})
target_link_libraries(test_wiimote_decoder PRIVATE GTest::gtest_main)
gtest_discover_tests(test_wiimote_decoder)

# 13. test_wiimote_adpcm ────────────────────────────────────────────────────────
# Tests the pure PCM16<->nibble encode/decode functions in
# Devices/Wiimote/WiimoteADPCM.* (packing shape, incremental-vs-one-shot
# equivalence, round-trip error bounds, Reset() semantics). No SDL_hid_* I/O
# or WiimoteDevice - same rationale as test_wiimote_decoder above.
add_executable(test_wiimote_adpcm
    test_wiimote_adpcm.cpp
    ${IB_SRC}/Devices/Wiimote/WiimoteADPCM.cpp
)
target_include_directories(test_wiimote_adpcm PRIVATE ${IB_SRC})
target_link_libraries(test_wiimote_adpcm PRIVATE GTest::gtest_main)
gtest_discover_tests(test_wiimote_adpcm)