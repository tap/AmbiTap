cmake_minimum_required(VERSION 3.21)

project(AmbiTap VERSION 0.1.0 LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

option(AMBITAP_ENABLE_SOFA
    "Enable SOFA-format HRTF loading via libmysofa (configure-time network fetch)" OFF)

if(PROJECT_IS_TOP_LEVEL)
    set(_ambitap_tests_default ON)
else()
    set(_ambitap_tests_default OFF)
endif()
option(AMBITAP_BUILD_TESTS "Build AmbiTap unit tests" ${_ambitap_tests_default})

# ------------------------------------------------------------------------------
# Eigen
#
# Resolution order:
#   1. An Eigen3::Eigen target already defined by the consuming project
#      (e.g. a consuming project may point one at its own bundled Eigen).
#   2. An installed/system Eigen via find_package.
#   3. FetchContent of a pinned release tarball (headers only — Eigen's own
#      CMake is not added to the build).
# ------------------------------------------------------------------------------

if(NOT TARGET Eigen3::Eigen)
    find_package(Eigen3 3.3 QUIET NO_MODULE)
endif()
if(NOT TARGET Eigen3::Eigen)
    include(FetchContent)
    FetchContent_Declare(eigen
        URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
        URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        FetchContent_Populate(eigen)
    endif()
    add_library(ambitap_eigen INTERFACE)
    add_library(Eigen3::Eigen ALIAS ambitap_eigen)
    target_include_directories(ambitap_eigen SYSTEM INTERFACE "${eigen_SOURCE_DIR}")
endif()

# ------------------------------------------------------------------------------
# Ooura FFT (vendored) — provides the extern "C" rdft/cdft used by
# include/ambitap/math/binaural/ooura_fft.h. PIC so MODULE/SHARED consumers
# (host applications, Max externals, Pd externals) can link it.
# ------------------------------------------------------------------------------

add_library(ambitap_fft STATIC third_party/ooura/fftsg.c)
add_library(AmbiTap::fft ALIAS ambitap_fft)
set_target_properties(ambitap_fft PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# The library proper (header-only)
# ------------------------------------------------------------------------------

add_library(ambitap INTERFACE)
add_library(AmbiTap::ambitap ALIAS ambitap)
target_include_directories(ambitap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(ambitap INTERFACE cxx_std_17)
target_link_libraries(ambitap INTERFACE Eigen3::Eigen ambitap_fft)

# ------------------------------------------------------------------------------
# Optional SOFA support (libmysofa)
# ------------------------------------------------------------------------------

if(AMBITAP_ENABLE_SOFA)
    include(FetchContent)
    FetchContent_Declare(libmysofa
        GIT_REPOSITORY https://github.com/hoene/libmysofa.git
        GIT_TAG        v1.3.4
    )
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libmysofa)

    target_compile_definitions(ambitap INTERFACE AMBITAP_HAS_SOFA)
    target_link_libraries(ambitap INTERFACE mysofa-static)
    # libmysofa generates mysofa_export.h into its build dir but doesn't add
    # the build dir to mysofa-static's INTERFACE_INCLUDE_DIRECTORIES.
    target_include_directories(ambitap SYSTEM INTERFACE "${libmysofa_BINARY_DIR}/src")
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(AMBITAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
