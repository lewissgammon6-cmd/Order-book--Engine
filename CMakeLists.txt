cmake_minimum_required(VERSION 3.16)
project(orderbook_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
add_compile_options(-Wall -Wextra)

option(OBE_BUILD_TESTS "Build GoogleTest unit tests" ON)
option(OBE_BUILD_BENCHMARKS "Build Google Benchmark suite" ON)
option(OBE_NATIVE_ARCH "Compile with -march=native" OFF)

if(OBE_NATIVE_ARCH)
    add_compile_options(-march=native)
endif()

# ---- Core library ----------------------------------------------------
add_library(orderbook_core
    src/order_book.cpp
)
target_include_directories(orderbook_core PUBLIC include)

# ---- Demo executable (no external deps) -------------------------------
add_executable(smoke_test examples/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE orderbook_core)

add_executable(latency_probe examples/latency_probe.cpp)
target_link_libraries(latency_probe PRIVATE orderbook_core)

# ---- GoogleTest (fetched from source; requires network on first build) -
if(OBE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    # Prevent GoogleTest from overriding the parent project's compiler/linker settings.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(unit_tests tests/test_order_book.cpp)
    target_link_libraries(unit_tests PRIVATE orderbook_core gtest gtest_main)
    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()

# ---- Google Benchmark (fetched from source; requires network on first build) -
if(OBE_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(bench_order_book benchmarks/bench_order_book.cpp)
    target_link_libraries(bench_order_book PRIVATE orderbook_core benchmark::benchmark)
endif()
