/// AmbiTap: target-independent ambisonics library
/// Hardening tests: input validation and degenerate-input behavior.
/// Each test is tagged with the audit finding it guards against.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/analysis/soundfield_grid.h"
#include "ambitap/dsp/binaural_renderer.h"
#include "ambitap/dsp/decoder.h"
#include "ambitap/dsp/directional_loudness.h"
#include "ambitap/dsp/doppler.h"
#include "ambitap/dsp/encoder.h"
#include "ambitap/dsp/format_converter.h"
#include "ambitap/dsp/mirror.h"
#include "ambitap/dsp/rotator.h"
#include "ambitap/dsp/spatial_compressor.h"
#include "ambitap/dsp/virtual_mic.h"
#include "ambitap/math/decoding/allrad.h"
#include "ambitap/math/decoding/epad.h"
#include "ambitap/math/decoding/mode_matching.h"
#include "ambitap/math/geometry/layouts.h"
#include "ambitap/math/geometry/speaker_layout.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace ambitap;

// Audit finding C6: orders beyond max_order used to overflow evaluate_sh's
// fixed-size Legendre table (stack corruption) via any processor constructor.
// Every constructor taking an order must validate it.
TEST(Hardening, ProcessorConstructorsRejectOutOfRangeOrders) {
    for (int bad : {-1, max_order + 1, 100}) {
        EXPECT_THROW(dsp::encoder e(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::mirror m(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::virtual_mic v(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::directional_loudness d(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::doppler dp(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::spatial_compressor c(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::rotator r(bad), std::invalid_argument) << bad;
        EXPECT_THROW(dsp::decoder dec(bad), std::invalid_argument) << bad;
        EXPECT_THROW(analysis::soundfield_grid g(bad), std::invalid_argument) << bad;
    }
    // Boundary orders construct fine.
    EXPECT_NO_THROW(dsp::encoder e(0));
    EXPECT_NO_THROW(dsp::encoder e(max_order));
}

// Audit finding C6 (format_converter variant): FuMa is only defined through
// order 3; order 4 used to index off the end of the 16-entry FuMa tables.
TEST(Hardening, FormatConverterRejectsOrdersAboveThree) {
    EXPECT_THROW(dsp::format_converter f(4), std::invalid_argument);
    EXPECT_THROW(dsp::format_converter f(-1), std::invalid_argument);
    EXPECT_NO_THROW(dsp::format_converter f(0));
    EXPECT_NO_THROW(dsp::format_converter f(3));
}

// Audit finding C5: binaural_renderer(6).prepare() used to read past the end
// of the embedded order-5 HRTF tables (ASan-verified global-buffer-overflow).
TEST(Hardening, BinauralRendererRejectsOrdersBeyondBuiltinHrtf) {
    EXPECT_THROW(dsp::binaural_renderer b(0), std::invalid_argument);
    EXPECT_THROW(dsp::binaural_renderer b(max_order + 1), std::invalid_argument);

    // Order within [1, max_order] but above the built-in dataset: construction
    // is fine (a custom HRTF may follow), prepare() without one is not.
    dsp::binaural_renderer bin(builtin_hrtf_order + 1);
    EXPECT_THROW(bin.prepare(64), std::invalid_argument);
    EXPECT_THROW(bin.probe_response(0.f, 0.f), std::invalid_argument);

    dsp::binaural_renderer ok(builtin_hrtf_order);
    EXPECT_NO_THROW(ok.prepare(64));
}

// Audit finding C5 (custom-HRTF variant): undersized or ragged custom HRTF
// sets used to be accepted and read out of bounds during convolver rebuild.
TEST(Hardening, BinauralRendererValidatesCustomHrtfShape) {
    dsp::binaural_renderer bin(1); // channels() == 4

    using firs = std::vector<std::vector<float>>;
    const firs good(4, std::vector<float>(32, 0.f));

    EXPECT_THROW(bin.set_custom_hrtf(firs(3, std::vector<float>(32, 0.f)), good),
                 std::invalid_argument)
        << "too few left FIRs";
    EXPECT_THROW(bin.set_custom_hrtf(good, firs(5, std::vector<float>(32, 0.f))),
                 std::invalid_argument)
        << "too many right FIRs";

    firs ragged = good;
    ragged[2].resize(16);
    EXPECT_THROW(bin.set_custom_hrtf(good, ragged), std::invalid_argument) << "ragged lengths";

    firs empty = good;
    empty[0].clear();
    EXPECT_THROW(bin.set_custom_hrtf(empty, good), std::invalid_argument) << "empty FIR";

    EXPECT_NO_THROW(bin.set_custom_hrtf(good, good));
    EXPECT_NO_THROW(bin.prepare(64));
}

// Decoder constructors must reject inputs the math cannot support instead of
// silently producing garbage (audit findings C6, C1).
TEST(Hardening, DecoderConstructionRejectsBadInput) {
    const auto cube = layouts::cube();
    EXPECT_THROW(compute_mode_matching_decoder(-1, cube), std::invalid_argument);
    EXPECT_THROW(compute_mode_matching_decoder(max_order + 1, cube), std::invalid_argument);
    EXPECT_THROW(compute_epad_decoder(max_order + 1, cube), std::invalid_argument);
    EXPECT_THROW(compute_allrad_decoder(max_order + 1, cube), std::invalid_argument);

    const std::vector<spherical_coord> none;
    EXPECT_THROW(compute_mode_matching_decoder(1, none), std::invalid_argument);
    EXPECT_THROW(compute_epad_decoder(1, none), std::invalid_argument);
    EXPECT_THROW(compute_allrad_decoder(1, none), std::invalid_argument);
}

// Audit finding C1: every decoder on every shipped preset must produce finite
// matrices — 2D presets used to go through a degenerate hull into singular
// matrix inversions (inf/NaN) and nearest-speaker snapping.
TEST(Hardening, AllDecodersFiniteOnAllPresets) {
    const std::vector<std::pair<const char*, std::vector<spherical_coord>>> presets = {
        {"stereo", layouts::stereo()},    {"quad", layouts::quad()},
        {"5.1", layouts::surround_5_1()}, {"hexagon", layouts::hexagon()},
        {"7.1", layouts::surround_7_1()}, {"cube", layouts::cube()},
        {"octagon", layouts::octagon()},  {"7.1.4", layouts::surround_7_1_4()},
    };

    for (const auto& [name, speakers] : presets) {
        for (int order : {1, 3}) {
            const auto Dmm = compute_mode_matching_decoder(order, speakers);
            const auto Dep = compute_epad_decoder(order, speakers);
            const auto Dar = compute_allrad_decoder(order, speakers);
            EXPECT_TRUE(Dmm.allFinite()) << name << " mode-matching order " << order;
            EXPECT_TRUE(Dep.allFinite()) << name << " EPAD order " << order;
            EXPECT_TRUE(Dar.allFinite()) << name << " ALLRAD order " << order;

            // Bounded panning gains even on irregular layouts (5.1's 140°
            // rear gap): the SVD threshold keeps the pseudoinverse tame.
            EXPECT_LT(Dmm.cwiseAbs().maxCoeff(), 25.0f) << name << " order " << order;
        }
    }
}

// Zero-length blocks are legal no-ops on the block APIs.
TEST(Hardening, ZeroLengthBlocksAreNoOps) {
    constexpr size_t channels = 16;

    std::vector<std::vector<float>> in_bufs(channels, std::vector<float>(4, 0.f));
    std::vector<std::vector<float>> out_bufs(channels, std::vector<float>(4, 0.f));
    std::vector<const float*>       in_ptrs;
    std::vector<float*>             out_ptrs;
    for (size_t ch = 0; ch < channels; ++ch) {
        in_ptrs.push_back(in_bufs[ch].data());
        out_ptrs.push_back(out_bufs[ch].data());
    }

    dsp::encoder enc(3);
    EXPECT_NO_THROW(enc.process(in_bufs[0].data(), out_ptrs.data(), 0));

    dsp::rotator rot(3);
    EXPECT_NO_THROW(rot.process(in_ptrs.data(), out_ptrs.data(), 0));

    dsp::mirror mir(3);
    EXPECT_NO_THROW(mir.process(in_ptrs.data(), out_ptrs.data(), 0));

    dsp::directional_loudness dl(3);
    EXPECT_NO_THROW(dl.process(in_ptrs.data(), out_ptrs.data(), 0));
}
