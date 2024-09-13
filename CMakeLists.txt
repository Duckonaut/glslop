cmake_minimum_required(VERSION 3.16)

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

# Declare the project
project(glslop VERSION 0.1.0 LANGUAGES CXX)

# Set the name of the executable
set(EXECUTABLE_NAME ${PROJECT_NAME})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)
link_libraries("-static")

cmake_policy(SET CMP0135 NEW)

# use C11 and C++17
target_compile_features(${EXECUTABLE_NAME} PRIVATE cxx_std_17)

target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(${EXECUTABLE_NAME} PUBLIC PROJECT_NAME="${PROJECT_NAME}")
target_compile_definitions(${EXECUTABLE_NAME} PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")
execute_process(COMMAND git rev-parse HEAD OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(${EXECUTABLE_NAME} PUBLIC PROJECT_COMMIT_HASH="${GIT_COMMIT_HASH}")

include(FetchContent)

set(ENABLE_HLSL OFF CACHE BOOL "")
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "")

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(glslang)

add_executable(${EXECUTABLE_NAME}
    src/main.cpp
)

target_link_libraries(${EXECUTABLE_NAME} PRIVATE glslang)
