include(FetchContent)
# Fetched by git clone at a commit pin, which is the form every sibling *Tap repo
# uses (DspTap/MuTap/RatioTap/SampleRateTap). This repo previously declared a
# release *tarball* instead; that is the one mechanism an HTTPS-proxied build
# environment blocks, so AmbiTap was the only library in the family whose tests
# could not be configured behind a proxy. Note the family currently runs two
# GTest versions -- this repo on v1.15.2, the other four on v1.14.0. Unifying
# them is a deliberate call for whoever needs it, not a side effect of this
# mechanism change.
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    # Commit pin, not the movable tag: tags can be re-pointed upstream.
    GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59 # v1.15.2
    # Prefer an already-installed GTest (offline builds, distro packages).
    FIND_PACKAGE_ARGS NAMES GTest
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ambitap_tests
    test_core.cpp
    test_dsp_encoder.cpp
    test_dsp_transforms.cpp
    test_dsp_stateful.cpp
    test_nfc.cpp
    test_plate.cpp
    test_room.cpp
    test_dsp_matrix.cpp
    test_dsp_binaural.cpp
    test_analysis_soundfield.cpp
    test_rotation.cpp
    test_geometry.cpp
    test_decoding.cpp
    test_binaural.cpp
    test_hardening.cpp
    test_dsp_threads.cpp
    test_rt_safety.cpp
    test_embedded_core.cpp
    test_sofa.cpp
    test_xtc.cpp
)
target_link_libraries(ambitap_tests PRIVATE
    AmbiTap::ambitap ambitap_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ambitap_tests
    DISCOVERY_TIMEOUT 120
    PROPERTIES TIMEOUT 300)
