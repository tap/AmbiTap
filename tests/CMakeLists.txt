include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ambitap_tests
    test_core.cpp
    test_dsp_encoder.cpp
    test_dsp_transforms.cpp
    test_dsp_stateful.cpp
    test_dsp_matrix.cpp
    test_dsp_binaural.cpp
    test_analysis_soundfield.cpp
    test_rotation.cpp
    test_geometry.cpp
    test_decoding.cpp
    test_binaural.cpp
)
target_link_libraries(ambitap_tests PRIVATE AmbiTap::ambitap GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ambitap_tests)
