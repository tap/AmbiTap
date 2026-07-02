cmake_minimum_required(VERSION 3.24)

project(AmbiTap VERSION 0.1.0 LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

option(AMBITAP_ENABLE_SOFA
    "Enable SOFA-format HRTF loading via libmysofa (configure-time network fetch)" OFF)
option(AMBITAP_WERROR "Treat warnings as errors (CI)" OFF)
option(AMBITAP_INSTALL "Generate install/export rules" ${PROJECT_IS_TOP_LEVEL})

if(PROJECT_IS_TOP_LEVEL)
    set(_ambitap_dev_default ON)
else()
    set(_ambitap_dev_default OFF)
endif()
option(AMBITAP_BUILD_TESTS "Build AmbiTap unit tests" ${_ambitap_dev_default})
option(AMBITAP_BUILD_EXAMPLES "Build AmbiTap example programs" ${_ambitap_dev_default})
option(AMBITAP_BUILD_BENCH "Build AmbiTap benchmarks" OFF)

# ------------------------------------------------------------------------------
# Warnings (applied to this project's own executables — tests, examples,
# benchmarks — never exported to consumers of the INTERFACE library).
# ------------------------------------------------------------------------------

add_library(ambitap_warnings INTERFACE)
if(MSVC)
    target_compile_options(ambitap_warnings INTERFACE /W4 /permissive-)
    if(AMBITAP_WERROR)
        target_compile_options(ambitap_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(ambitap_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(AMBITAP_WERROR)
        target_compile_options(ambitap_warnings INTERFACE -Werror)
    endif()
endif()

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
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        # A SOURCE_SUBDIR with no CMakeLists.txt makes MakeAvailable download
        # without add_subdirectory() — headers only, no Eigen build system.
        FetchContent_Declare(eigen
            URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
            URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR cmake_subdir_that_does_not_exist
        )
        FetchContent_MakeAvailable(eigen)
    else()
        # Pre-3.28 fallback: plain population (FetchContent_Populate's
        # single-argument form is only deprecated from CMake 3.30).
        FetchContent_Declare(eigen
            URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
            URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_GetProperties(eigen)
        if(NOT eigen_POPULATED)
            FetchContent_Populate(eigen)
        endif()
    endif()
    add_library(ambitap_eigen INTERFACE)
    add_library(Eigen3::Eigen ALIAS ambitap_eigen)
    target_include_directories(ambitap_eigen SYSTEM INTERFACE
        $<BUILD_INTERFACE:${eigen_SOURCE_DIR}>)
endif()

# ------------------------------------------------------------------------------
# Ooura FFT (vendored) — provides the extern "C" rdft/cdft used by
# include/ambitap/math/binaural/ooura_fft.h. PIC so MODULE/SHARED consumers
# (host applications, Max externals, Pd externals) can link it.
# ------------------------------------------------------------------------------

add_library(ambitap_fft STATIC third_party/ooura/fftsg.c)
add_library(AmbiTap::fft ALIAS ambitap_fft)
set_target_properties(ambitap_fft PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME ambitap_fft)

# ------------------------------------------------------------------------------
# The library proper (header-only)
# ------------------------------------------------------------------------------

add_library(ambitap INTERFACE)
add_library(AmbiTap::ambitap ALIAS ambitap)
target_include_directories(ambitap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(ambitap INTERFACE cxx_std_20)
# In the build tree, Eigen binds to whichever Eigen3::Eigen resolution above
# won. For the install export: when Eigen came from find_package the imported
# name Eigen3::Eigen is recorded and AmbiTapConfig.cmake's
# find_dependency(Eigen3) resolves it in the consumer; when it came from
# FetchContent, the shim (BUILD_INTERFACE-only usage requirements, installs
# empty) joins the export set and AmbiTapConfig.cmake forwards it to the
# consumer's real Eigen3::Eigen.
target_link_libraries(ambitap INTERFACE Eigen3::Eigen ambitap_fft)

# ------------------------------------------------------------------------------
# Optional SOFA support (libmysofa). Build-tree only: the installed/exported
# package does not carry libmysofa — consumers who need SOFA loading should
# vendor AmbiTap via add_subdirectory/FetchContent with AMBITAP_ENABLE_SOFA=ON.
# ------------------------------------------------------------------------------

if(AMBITAP_ENABLE_SOFA)
    include(FetchContent)
    FetchContent_Declare(libmysofa
        GIT_REPOSITORY https://github.com/hoene/libmysofa.git
        # Commit pin, not the movable v1.3.4 tag: tags can be re-pointed.
        GIT_TAG        7a0c07111a3d7230ec534e08925c24a7525f33c0 # v1.3.4
    )
    # libmysofa's CMake exposes a generic BUILD_TESTS option; scope the
    # override to its subtree instead of clobbering the consumer's cache.
    set(BUILD_TESTS OFF)
    FetchContent_MakeAvailable(libmysofa)
    unset(BUILD_TESTS)

    target_compile_definitions(ambitap INTERFACE $<BUILD_INTERFACE:AMBITAP_HAS_SOFA>)
    target_link_libraries(ambitap INTERFACE $<BUILD_INTERFACE:mysofa-static>)
    # libmysofa generates mysofa_export.h into its build dir but doesn't add
    # the build dir to mysofa-static's INTERFACE_INCLUDE_DIRECTORIES.
    target_include_directories(ambitap SYSTEM INTERFACE
        $<BUILD_INTERFACE:${libmysofa_BINARY_DIR}/src>)
endif()

# ------------------------------------------------------------------------------
# Install / export: makes find_package(AmbiTap) work.
#   - headers            -> <prefix>/include/ambitap
#   - ambitap, ambitap_fft targets + AmbiTapTargets.cmake
#   - AmbiTapConfig.cmake (find_dependency(Eigen3)) + version file
# ------------------------------------------------------------------------------

if(AMBITAP_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/ambitap
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS ambitap ambitap_fft
        EXPORT AmbiTapTargets)
    if(TARGET ambitap_eigen)
        # FetchContent Eigen shim: exports as an empty INTERFACE target (its
        # include dir is BUILD_INTERFACE-only); consumers get real Eigen from
        # AmbiTapConfig.cmake's find_dependency(Eigen3).
        install(TARGETS ambitap_eigen EXPORT AmbiTapTargets)
    endif()
    install(EXPORT AmbiTapTargets
        NAMESPACE AmbiTap::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AmbiTap)
    install(FILES third_party/ooura/readme.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ambitap
        RENAME OOURA_LICENSE.txt)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ambitap)

    configure_package_config_file(
        cmake/AmbiTapConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/AmbiTapConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AmbiTap)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/AmbiTapConfigVersion.cmake
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/AmbiTapConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/AmbiTapConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AmbiTap)
endif()

# ------------------------------------------------------------------------------
# Tests / examples / benchmarks
# ------------------------------------------------------------------------------

if(AMBITAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(AMBITAP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(AMBITAP_BUILD_BENCH)
    add_subdirectory(bench)
endif()
