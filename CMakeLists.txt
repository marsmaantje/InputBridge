cmake_minimum_required(VERSION 3.15)
project(InputBridge VERSION 1.0)

# 1. Set C++20 standard (matches your CXXFLAGS)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. Recursively find all .cpp files in src/ (matches your wildcard/find logic)
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 3. Include directories (matches your INCLUDE and LIB variables)
include_directories(include)
link_directories(lib)

# 4. Define the executable and its output location
# This replaces your $(OUTPUT)/$(MAIN) logic
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
add_executable(InputBridge ${SOURCES})

# 5. Compiler Flags (matches -Wall -Wextra)
if(MSVC)
    target_compile_options(InputBridge PRIVATE /W4)
else()
    target_compile_options(InputBridge PRIVATE -Wall -Wextra)
endif()

# 6. Installation (Crucial for the AppImage/Workflow we discussed)
install(TARGETS InputBridge RUNTIME DESTINATION bin)