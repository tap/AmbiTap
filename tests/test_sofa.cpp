/// @file test_sofa.cpp
/// @brief SOFA reader hardening tests. Built only when AMBITAP_ENABLE_SOFA is on
///        (the reader needs libmysofa); a no-op translation unit otherwise.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#ifdef AMBITAP_HAS_SOFA

#include <cstdio>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/math/binaural/sofa_reader.h"

using namespace ambitap;

namespace {

    std::string write_temp(const std::vector<unsigned char>& bytes, const char* tag) {
        const std::string path = std::string("/tmp/ambitap_sofa_test_") + tag + ".sofa";
        std::FILE*        f    = std::fopen(path.c_str(), "wb");
        if (f) {
            if (!bytes.empty())
                std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
        }
        return path;
    }

} // namespace

// The reader is the library's only untrusted-file surface. Malformed inputs
// must be rejected with an exception, never crash or read out of bounds
// (the whole path is fuzzed by tests/fuzz/fuzz_sofa_reader.cpp under
// ASan+UBSan; these are the deterministic regression cases).
TEST(SofaReader, RejectsMalformedFiles) {
    // Empty file.
    EXPECT_THROW(load_sofa(write_temp({}, "empty")), std::runtime_error);

    // Not an HDF5/SOFA container at all.
    const std::string junk = "this is definitely not a SOFA file";
    EXPECT_THROW(load_sofa(write_temp({junk.begin(), junk.end()}, "junk")), std::runtime_error);

    // Valid HDF5 magic, then garbage — parses far enough to matter, then fails.
    std::vector<unsigned char> hdf5_magic = {0x89, 'H', 'D', 'F', '\r', '\n', 0x1a, '\n'};
    hdf5_magic.resize(128, 0);
    EXPECT_THROW(load_sofa(write_temp(hdf5_magic, "hdf5magic")), std::runtime_error);
}

TEST(SofaReader, RejectsNonexistentPath) {
    EXPECT_THROW(load_sofa("/nonexistent/path/does/not/exist.sofa"), std::runtime_error);
}

#endif // AMBITAP_HAS_SOFA
