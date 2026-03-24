cmake_minimum_required(VERSION 3.20)
project(Snap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SNAP_ENABLE_ZEROCOPY  "Enable MSG_ZEROCOPY send paths (Linux >= 4.14)" OFF)
option(SNAP_ENABLE_HUGEPAGES "Enable transparent hugepage-backed SHM and pool" OFF)
option(SNAP_BUILD_EXAMPLES   "Build all example programs" ON)
option(SNAP_BUILD_TESTS      "Build all test/bench programs" ON)

add_compile_options(
    -O3
    -march=native
    -mtune=native
    -fno-exceptions
    -fno-rtti
    -ffast-math
    -funroll-loops
    -fno-plt
    -Wall
    -Wextra
    -Wpedantic
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-flto=auto -fuse-linker-plugin -DNDEBUG)
    add_link_options(-flto=auto)
endif()

add_library(snap INTERFACE)
target_include_directories(snap INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(SNAP_ENABLE_ZEROCOPY)
    target_compile_definitions(snap INTERFACE SNAP_ENABLE_ZEROCOPY)
endif()
if(SNAP_ENABLE_HUGEPAGES)
    target_compile_definitions(snap INTERFACE SNAP_ENABLE_HUGEPAGES)
endif()

find_library(NUMA_LIB numa)
if(NUMA_LIB)
    set(SNAP_LINK_LIBS snap pthread rt ${NUMA_LIB})
    message(STATUS "libnuma found: ${NUMA_LIB}")
else()
    set(SNAP_LINK_LIBS snap pthread rt)
    message(STATUS "libnuma not found, NUMA features disabled")
endif()


if(SNAP_BUILD_EXAMPLES)
    foreach(ex 01_inproc 02_shm 03_udp 04_tcp 05_pubsub 06_pipeline 07_multicast 08_pool 09_batch)
        add_executable(snap_${ex} examples/${ex}.cpp)
        target_link_libraries(snap_${ex} PRIVATE ${SNAP_LINK_LIBS})
    endforeach()
endif()

if(SNAP_BUILD_TESTS)
    add_executable(snap_bench         test/bench.cpp)
    add_executable(snap_spsc_test     test/spsc_test.cpp)
    add_executable(snap_mpmc_test     test/mpmc_test.cpp)
    add_executable(snap_pool_test     test/pool_test.cpp)
    add_executable(snap_dispatch_test test/dispatch_test.cpp)

    foreach(target snap_bench snap_spsc_test snap_mpmc_test snap_pool_test snap_dispatch_test)
        target_link_libraries(${target} PRIVATE ${SNAP_LINK_LIBS})
    endforeach()
endif()

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/snap
    FILES_MATCHING PATTERN "*.hpp"
)