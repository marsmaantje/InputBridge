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
target_link_libraries(test_protocol_validator PRIVATE GTest::gtest_main)
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
target_link_libraries(test_backup_manager PRIVATE GTest::gtest_main)
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
gtest_discover_tests(test_haptic_parser)
