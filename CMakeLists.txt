cmake_minimum_required(VERSION 3.15)
project(qcborpp
    DESCRIPTION "C++ Header-Only Wrapper for QCBOR (RFC 8949 CBOR encoder/decoder)"
    LANGUAGES CXX
    VERSION 1.0.0
)

# ── qcborpp is a header-only INTERFACE library ──
add_library(qcborpp INTERFACE)

target_include_directories(qcborpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(qcborpp INTERFACE cxx_std_17)

# ── QCBOR dependency ──
# qcborpp does NOT build or fetch QCBOR itself.
# The user must provide QCBOR either via:
#   1. find_package(QCBOR)
#   2. An already-available qcbor target
#   3. Setting QCBOR_INCLUDE_DIR and QCBOR_LIBRARY manually
#
# We try find_package first; if not found, look for a qcbor target.
find_package(QCBOR QUIET)
if(QCBOR_FOUND)
    target_link_libraries(qcborpp INTERFACE QCBOR::qcbor)
elseif(TARGET qcbor)
    target_link_libraries(qcborpp INTERFACE qcbor)
else()
    # Let the user set these variables
    if(DEFINED QCBOR_INCLUDE_DIR)
        target_include_directories(qcborpp INTERFACE ${QCBOR_INCLUDE_DIR})
    endif()
    if(DEFINED QCBOR_LIBRARY)
        target_link_libraries(qcborpp INTERFACE ${QCBOR_LIBRARY})
    endif()
    if(NOT DEFINED QCBOR_INCLUDE_DIR AND NOT DEFINED QCBOR_LIBRARY)
        # Provide a clear error message
        message(WARNING "QCBOR not found. To use qcborpp, you must provide QCBOR. "
                        "Options:\n"
                        "  1. Install QCBOR and use find_package(QCBOR)\n"
                        "  2. Add qcbor target before this CMakeLists.txt\n"
                        "  3. Set QCBOR_INCLUDE_DIR and QCBOR_LIBRARY")
    endif()
endif()

# ── Tests (self-contained, auto-fetch QCBOR) ──
option(QCBORPP_BUILD_TESTS "Build qcborpp unit tests" ON)

if(QCBORPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
