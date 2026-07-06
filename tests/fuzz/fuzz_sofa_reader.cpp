/// AmbiTap: target-independent ambisonics library
/// libFuzzer harness for the SOFA reader load + SH-decompose path.
///
/// The SOFA reader (math/binaural/sofa_reader.h) is the library's only
/// consumer of untrusted external files: users load arbitrary HRTF SOFA
/// files, which are HDF5 containers parsed by libmysofa. This fuzzes the
/// whole path a host would drive — parse, validate, and project onto the SH
/// basis — against adversarial bytes, so a malformed or hostile file fails
/// cleanly (exception or rejection) rather than crashing, reading out of
/// bounds, or looping.
///
/// libmysofa parses the HDF5/NetCDF container; that third-party surface is
/// in scope here too (AmbiTap ships it as an optional dependency and is
/// responsible for feeding it safely).
///
/// Build: -DAMBITAP_BUILD_FUZZERS=ON with a Clang toolchain (needs
/// -fsanitize=fuzzer). See tests/fuzz/CMakeLists.txt.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

#include "ambitap/math/binaural/sofa_reader.h"

namespace {

    // libmysofa only takes a file path, so each input is materialized to a
    // temp file, named from a monotonic counter (no timestamp — reproducible
    // across a replay of the same corpus).
    std::string write_temp(const uint8_t* data, size_t size) {
        static unsigned long counter = 0;
        std::string          path    = "/tmp/ambitap_fuzz_" + std::to_string(counter++) + ".sofa";
        std::FILE*           f       = std::fopen(path.c_str(), "wb");
        if (!f)
            return {};
        if (size > 0)
            std::fwrite(data, 1, size, f);
        std::fclose(f);
        return path;
    }

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Cap input size: HDF5 parsing of huge inputs is slow and adds no
    // coverage past a point; keep iterations fast.
    if (size > (1u << 20))
        return 0;

    const std::string path = write_temp(data, size);
    if (path.empty())
        return 0;

    try {
        const ambitap::hrtf_data hrtf = ambitap::load_sofa(path);

        // Exercise the downstream math too: project onto a low order (cheap;
        // higher orders just rank-reject on sparse grids). Only meaningful
        // when the grid could plausibly support it.
        if (hrtf.num_measurements >= 4 && hrtf.hrir_length > 0) {
            std::vector<std::vector<float>> left;
            std::vector<std::vector<float>> right;
            hrtf.decompose_sh(1, hrtf.hrir_length, left, right);
        }
    }
    catch (const std::exception&) {
        // Expected: malformed/unsupported files must throw, not crash.
    }

    std::remove(path.c_str());
    return 0;
}
