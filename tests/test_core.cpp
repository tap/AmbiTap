/// AmbiTap: target-independent ambisonics library
/// Tests for ACN indexing, SN3D/N3D normalization, and SH evaluation.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/math/core/indexing.h"
#include "ambitap/math/core/normalization.h"
#include "ambitap/math/core/spherical_harmonics.h"
#include "ambitap/math/geometry/tdesigns.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace ambitap;

TEST(Indexing, ChannelCount) {
    EXPECT_EQ(channel_count(0), 1u);
    EXPECT_EQ(channel_count(1), 4u);
    EXPECT_EQ(channel_count(3), 16u);
    EXPECT_EQ(channel_count(5), 36u);
    EXPECT_EQ(channel_count(max_order), max_channel_count);
}

TEST(Indexing, AcnRoundTrip) {
    for (int n = 0; n <= max_order; ++n) {
        for (int m = -n; m <= n; ++m) {
            const size_t acn = acn_index(n, m);
            EXPECT_EQ(acn_order(acn), n) << "acn=" << acn;
            EXPECT_EQ(acn_degree(acn), m) << "acn=" << acn;
        }
    }
    // ACN indices are dense: 0 .. (N+1)^2 - 1.
    EXPECT_EQ(acn_index(max_order, max_order), max_channel_count - 1);
}

TEST(Normalization, Sn3dBasics) {
    // (n, 0) terms have epsilon == 1 and factorial ratio == 1.
    for (int n = 0; n <= max_order; ++n) {
        EXPECT_FLOAT_EQ(sn3d_factor(n, 0), 1.0f);
    }
    // First-order |m| == 1: sqrt(2 * 0!/2!) == 1.
    EXPECT_FLOAT_EQ(sn3d_factor(1, 1), 1.0f);
}

TEST(Normalization, N3dRelations) {
    for (int n = 0; n <= max_order; ++n) {
        for (int m = 0; m <= n; ++m) {
            EXPECT_NEAR(n3d_factor(n, m), sn3d_factor(n, m) * std::sqrt(2.0f * n + 1.0f), 1e-6f);
        }
        EXPECT_NEAR(sn3d_to_n3d(n) * n3d_to_sn3d(n), 1.0f, 1e-6f);
    }
}

TEST(SphericalHarmonics, OmniChannelIsUnity) {
    float sh[max_channel_count];
    for (float az : {0.0f, 0.7f, -2.1f, 3.0f}) {
        for (float el : {0.0f, 0.5f, -1.2f}) {
            evaluate_sh(5, az, el, sh);
            EXPECT_NEAR(sh[0], 1.0f, 1e-6f) << "az=" << az << " el=" << el;
        }
    }
}

TEST(SphericalHarmonics, FirstOrderClosedForm) {
    // ACN/SN3D order 1: Y = sin(az)cos(el), Z = sin(el), X = cos(az)cos(el).
    float sh[4];
    for (float az : {0.0f, 0.9f, -1.7f, 2.8f}) {
        for (float el : {0.0f, 0.6f, -0.8f}) {
            evaluate_sh(1, az, el, sh);
            EXPECT_NEAR(sh[1], std::sin(az) * std::cos(el), 1e-6f);
            EXPECT_NEAR(sh[2], std::sin(el), 1e-6f);
            EXPECT_NEAR(sh[3], std::cos(az) * std::cos(el), 1e-6f);
        }
    }
}

TEST(SphericalHarmonics, OrthonormalUnderTdesignQuadrature) {
    // A T-design with t >= 2N integrates products of two order-<=N harmonics
    // exactly. After SN3D -> N3D conversion the basis is orthonormal w.r.t.
    // the normalized sphere measure: (1/V) * sum_v Yi(v) Yj(v) == delta_ij.
    constexpr int order = 3;
    const size_t  C     = channel_count(order);

    size_t count            = 0;
    const float (*pts)[3]   = tdesign_for_order(order, count);
    ASSERT_GT(count, 0u);

    std::vector<std::vector<float>> Y(count, std::vector<float>(C));
    float                           sh[max_channel_count];
    for (size_t v = 0; v < count; ++v) {
        const float az = std::atan2(pts[v][1], pts[v][0]);
        const float el = std::atan2(pts[v][2],
                                    std::sqrt(pts[v][0] * pts[v][0] + pts[v][1] * pts[v][1]));
        evaluate_sh(order, az, el, sh);
        for (size_t c = 0; c < C; ++c) {
            Y[v][c] = sh[c] * sn3d_to_n3d(acn_order(c));
        }
    }

    for (size_t i = 0; i < C; ++i) {
        for (size_t j = 0; j < C; ++j) {
            double acc = 0.0;
            for (size_t v = 0; v < count; ++v) {
                acc += static_cast<double>(Y[v][i]) * static_cast<double>(Y[v][j]);
            }
            acc /= static_cast<double>(count);
            const double expected = (i == j) ? 1.0 : 0.0;
            EXPECT_NEAR(acc, expected, 1e-3) << "i=" << i << " j=" << j;
        }
    }
}
