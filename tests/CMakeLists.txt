cmake_minimum_required(VERSION 3.15)

# ── Tests must self-contain their dependencies ──

# ── 1. Fetch Catch2 (header-only test framework) ──

# Catch2 v3.7 needs C++17 to compile its string_view support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_GetProperties(Catch2)
if(NOT Catch2_POPULATED)
    FetchContent_MakeAvailable(Catch2)
endif()
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# ── 2. Fetch and build QCBOR ──
FetchContent_Declare(
    qcbor_dep
    GIT_REPOSITORY https://github.com/laurencelundblade/QCBOR.git
    GIT_TAG        v1.2
)
# Override QCBOR's own CMake options to avoid building its tests
set(BUILD_QCBOR_TEST OFF CACHE BOOL "" FORCE)
set(QCBOR_OPT_DISABLE_FLOAT_HW_USE OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(qcbor_dep)
if(NOT qcbor_dep_POPULATED)
    FetchContent_MakeAvailable(qcbor_dep)
endif()

# ── 3. Test executable ──
add_executable(qcborpp_tests
    test_encoder_primitives.cpp
    test_encoder_compounds.cpp
    test_encoder_tagged.cpp
    test_decoder_primitives.cpp
    test_decoder_compounds.cpp
    test_error.cpp
    test_roundtrip.cpp
    test_coverage_gap.cpp
    test_coverage_gap2.cpp
    test_coverage_gap3.cpp
    test_item_proxy_type.cpp
    test_static_encoder.cpp
    test_cross_validation.cpp
    test_error_paths.cpp
    test_bench.cpp
    test_chrono.cpp
    test_init_list.cpp
    test_decoder_convenience.cpp
    test_decoder_tagged.cpp
    test_coverage_gap4.cpp
    test_main.cpp
)

target_link_libraries(qcborpp_tests PRIVATE
    qcborpp
    Catch2::Catch2
    qcbor      # QCBOR's own CMake target name (not the FetchContent declare name)
)

target_include_directories(qcborpp_tests PRIVATE
    "${qcbor_dep_SOURCE_DIR}/inc"
)

target_compile_features(qcborpp_tests PRIVATE cxx_std_17)

# ── 4. Register tests with CTest ──
include(Catch)
catch_discover_tests(qcborpp_tests)
