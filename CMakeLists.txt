cmake_minimum_required(VERSION 3.20)
project(MiniRouter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warning flags
if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-unused-parameter)
endif()

# Sanitizer options
option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)
if (ENABLE_SANITIZERS AND NOT MSVC)
    message(STATUS "Enabling ASan and UBSan")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# MiniRouter Core Library
file(GLOB_RECURSE MINIROUTER_CORE_SOURCES
    "src/packet/*.cpp"
    "src/dataplane/*.cpp"
    "src/routing/*.cpp"
    "src/runtime/*.cpp"
)

add_library(minirouter_core STATIC ${MINIROUTER_CORE_SOURCES})
target_include_directories(minirouter_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# MiniRouter SAI C API Shared Library
add_library(minirouter_sai SHARED src/api/minirouter_sai.cpp)
target_link_libraries(minirouter_sai PRIVATE minirouter_core)
target_include_directories(minirouter_sai PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
set_target_properties(minirouter_sai PROPERTIES
    OUTPUT_NAME "minirouter_sai"
    POSITION_INDEPENDENT_CODE ON
)

# MiniRouter Daemon Executable
add_executable(minirouter src/main.cpp)
target_link_libraries(minirouter PRIVATE minirouter_core)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)
enable_testing()

# Unit & Integration Tests
file(GLOB_RECURSE TEST_SOURCES
    "tests/unit/*.cpp"
    "tests/integration/*.cpp"
)

add_executable(minirouter_tests ${TEST_SOURCES})
target_link_libraries(minirouter_tests PRIVATE
    minirouter_core
    gtest_main
)
target_include_directories(minirouter_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(minirouter_tests)

# Benchmarks
file(GLOB BENCHMARK_SOURCES "benchmarks/*.cpp")
if (BENCHMARK_SOURCES)
    add_executable(minirouter_benchmarks ${BENCHMARK_SOURCES})
    target_link_libraries(minirouter_benchmarks PRIVATE minirouter_core)
    target_include_directories(minirouter_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

# Examples
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/basic_forwarding/main.cpp")
    add_executable(example_basic_forwarding examples/basic_forwarding/main.cpp)
    target_link_libraries(example_basic_forwarding PRIVATE minirouter_core)
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/three_router_topology/main.cpp")
    add_executable(example_three_router_topology examples/three_router_topology/main.cpp)
    target_link_libraries(example_three_router_topology PRIVATE minirouter_core)
endif()
