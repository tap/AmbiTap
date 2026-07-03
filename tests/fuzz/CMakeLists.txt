# libFuzzer harnesses. Require Clang (for -fsanitize=fuzzer) and the SOFA
# reader (AMBITAP_ENABLE_SOFA, which brings in libmysofa). ASan+UBSan are
# layered on so memory and UB bugs surface during fuzzing, not just crashes.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "AMBITAP_BUILD_FUZZERS requires Clang (needs -fsanitize=fuzzer); "
        "current compiler is ${CMAKE_CXX_COMPILER_ID}")
endif()
if(NOT AMBITAP_ENABLE_SOFA)
    message(FATAL_ERROR
        "AMBITAP_BUILD_FUZZERS needs AMBITAP_ENABLE_SOFA=ON (the SOFA reader "
        "is the fuzz target)")
endif()

add_executable(fuzz_sofa_reader fuzz_sofa_reader.cpp)
target_link_libraries(fuzz_sofa_reader PRIVATE AmbiTap::ambitap)
target_compile_options(fuzz_sofa_reader PRIVATE
    -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
target_link_options(fuzz_sofa_reader PRIVATE
    -fsanitize=fuzzer,address,undefined)

# A short built-in run doubles as a CI smoke test: exercise the harness
# against the seed corpus for a bounded time and fail on any finding.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
    add_test(NAME fuzz_sofa_reader_smoke
        COMMAND fuzz_sofa_reader -runs=20000 -max_total_time=60
                ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
endif()
