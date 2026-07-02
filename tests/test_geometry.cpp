/// AmbiTap: target-independent ambisonics library
/// Tests for convex hull, VBAP speaker layouts, presets, and T-designs.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/math/core/indexing.h"
#include "ambitap/math/geometry/convex_hull.h"
#include "ambitap/math/geometry/layouts.h"
#include "ambitap/math/geometry/speaker_layout.h"
#include "ambitap/math/geometry/tdesigns.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <stdexcept>

using namespace ambitap;

TEST(ConvexHull, RandomSpherePointsSatisfyEuler) {
    // Points in generic position (no coplanar faces): a closed triangulated
    // hull over V on-hull vertices has exactly F = 2V - 4 faces.
    std::mt19937                    rng(42);
    std::normal_distribution<float> dist;
    std::vector<Eigen::Vector3f>    pts;
    for (int i = 0; i < 40; ++i) {
        pts.push_back(Eigen::Vector3f(dist(rng), dist(rng), dist(rng)).normalized());
    }

    const auto tris = convex_hull_3d(pts);
    EXPECT_EQ(tris.size(), 2 * pts.size() - 4);
}

TEST(ConvexHull, CubeCoversAllVertices) {
    // Coplanar quad faces make the exact triangle count epsilon-sensitive
    // (12 for a clean diagonal split, more when overlapping splits occur).
    // That is benign for VBAP, which only needs *an* enclosing triangle per
    // direction — so assert coverage, not an exact count.
    std::vector<Eigen::Vector3f> pts;
    for (float x : {-1.0f, 1.0f})
        for (float y : {-1.0f, 1.0f})
            for (float z : {-1.0f, 1.0f}) pts.push_back(Eigen::Vector3f(x, y, z).normalized());

    const auto tris = convex_hull_3d(pts);
    EXPECT_GE(tris.size(), 12u);

    std::vector<bool> seen(pts.size(), false);
    for (const auto& t : tris) seen[t.a] = seen[t.b] = seen[t.c] = true;
    for (size_t i = 0; i < seen.size(); ++i) {
        EXPECT_TRUE(seen[i]) << "cube corner " << i << " missing from hull";
    }
}

TEST(LayoutPresets, SpeakerCounts) {
    EXPECT_EQ(layouts::stereo().size(), 2u);
    EXPECT_EQ(layouts::quad().size(), 4u);
    EXPECT_EQ(layouts::surround_5_1().size(), 5u);
    EXPECT_EQ(layouts::hexagon().size(), 6u);
    EXPECT_EQ(layouts::surround_7_1().size(), 7u);
    EXPECT_EQ(layouts::cube().size(), 8u);
    EXPECT_EQ(layouts::octagon().size(), 8u);
    EXPECT_EQ(layouts::surround_7_1_4().size(), 11u);
}

TEST(SpeakerLayout, VbapAtSpeakerIsThatSpeaker) {
    speaker_layout layout(layouts::cube());
    const auto&    speakers = layout.speakers();

    for (size_t i = 0; i < speakers.size(); ++i) {
        const auto gains = layout.vbap_gains(speakers[i]);
        ASSERT_EQ(gains.size(), speakers.size());
        EXPECT_NEAR(gains[i], 1.0f, 1e-4f) << "speaker " << i;
        for (size_t j = 0; j < gains.size(); ++j) {
            if (j != i) {
                EXPECT_NEAR(gains[j], 0.0f, 1e-4f) << "speaker " << i << " leak " << j;
            }
        }
    }
}

TEST(SpeakerLayout, VbapGainsAreEnergyNormalized) {
    speaker_layout layout(layouts::cube());

    for (float az : {0.1f, 1.2f, -2.3f, 3.0f}) {
        for (float el : {-0.9f, 0.0f, 0.4f, 1.1f}) {
            const auto gains  = layout.vbap_gains({az, el});
            float      energy = 0.0f;
            int        active = 0;
            for (float g : gains) {
                EXPECT_GE(g, 0.0f);
                energy += g * g;
                if (g > 1e-6f) ++active;
            }
            EXPECT_NEAR(energy, 1.0f, 1e-4f) << "az=" << az << " el=" << el;
            EXPECT_LE(active, 3) << "VBAP uses at most one triangle";
        }
    }
}

TEST(SpeakerLayout, DistanceCompensation) {
    const std::vector<float> distances = {1.0f, 2.0f, 4.0f};
    const auto               comp      = speaker_layout::distance_compensation(distances);

    // Farthest speaker: no delay, unity gain.
    EXPECT_FLOAT_EQ(comp.delays[2], 0.0f);
    EXPECT_FLOAT_EQ(comp.gains[2], 1.0f);
    // Nearest speaker: delayed by the path difference, attenuated by the ratio.
    EXPECT_NEAR(comp.delays[0], 3.0f / 343.0f, 1e-6f);
    EXPECT_FLOAT_EQ(comp.gains[0], 0.25f);
}

// Audit finding C1: coplanar inputs used to produce a garbage "hull" with
// duplicate-vertex, zero-volume triangles that silently broke VBAP (and thus
// ALLRAD) on every 2D preset. Degenerate inputs must yield an empty hull.
TEST(ConvexHull, DegenerateInputsReturnEmpty) {
    auto ring = [](size_t n) {
        std::vector<Eigen::Vector3f> pts;
        for (size_t i = 0; i < n; ++i) {
            const float a = 2.0f * k_pi * static_cast<float>(i) / static_cast<float>(n);
            pts.push_back({std::cos(a), std::sin(a), 0.0f});
        }
        return pts;
    };

    EXPECT_TRUE(convex_hull_3d(ring(8)).empty()) << "coplanar ring";
    EXPECT_TRUE(convex_hull_3d({{1, 0, 0}, {-1, 0, 0}, {0.5f, 0, 0}, {-0.5f, 0, 0}}).empty())
        << "collinear";
    EXPECT_TRUE(convex_hull_3d({{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}}).empty())
        << "coincident";
    EXPECT_TRUE(convex_hull_3d({{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}).empty()) << "< 4 points";
}

// 2D pairwise VBAP on planar layouts (the mode all horizontal presets use).
TEST(SpeakerLayout, PairwisePanningOn5_1) {
    speaker_layout layout(layouts::surround_5_1()); // L, R, C, LS, RS
    ASSERT_TRUE(layout.is_pairwise());
    ASSERT_EQ(layout.num_triangles(), 0u);

    const float deg = k_pi / 180.0f;

    // A source between C (0°) and L (+30°) activates exactly that pair, with
    // the 2D VBAP (tangent-law) split and unit energy.
    {
        const auto g = layout.vbap_gains({20.0f * deg, 0.0f});
        EXPECT_GT(g[0], g[2]) << "20° is closer to L(30°) than C(0°)";
        EXPECT_GT(g[2], 0.1f);
        EXPECT_FLOAT_EQ(g[1], 0.0f);
        EXPECT_FLOAT_EQ(g[3], 0.0f);
        EXPECT_FLOAT_EQ(g[4], 0.0f);
        float e = 0.f;
        for (float v : g) e += v * v;
        EXPECT_NEAR(e, 1.0f, 1e-5f);

        // Exact 2D VBAP solution for speakers at 0°/30°, source at 20°.
        const float gl = std::sin(20.0f * deg) / std::sin(30.0f * deg);
        const float gc = std::cos(20.0f * deg) - gl * std::cos(30.0f * deg);
        const float n  = std::sqrt(gl * gl + gc * gc);
        EXPECT_NEAR(g[0], gl / n, 1e-4f);
        EXPECT_NEAR(g[2], gc / n, 1e-4f);
    }

    // A source exactly at a speaker is that speaker alone.
    for (size_t i = 0; i < layout.num_speakers(); ++i) {
        const auto g = layout.vbap_gains(layout.speakers()[i]);
        EXPECT_NEAR(g[i], 1.0f, 1e-4f) << "speaker " << i;
    }

    // The rear gap (180°) pans between LS (+110°) and RS (-110°).
    {
        const auto g = layout.vbap_gains({180.0f * deg, 0.0f});
        EXPECT_NEAR(g[3], g[4], 1e-4f);
        EXPECT_GT(g[3], 0.5f);
        EXPECT_FLOAT_EQ(g[0], 0.0f);
        EXPECT_FLOAT_EQ(g[1], 0.0f);
        EXPECT_FLOAT_EQ(g[2], 0.0f);
    }

    // An elevated source pans by azimuth (projection onto the ring plane).
    {
        const auto g = layout.vbap_gains({0.0f, 45.0f * deg});
        EXPECT_GT(g[2], 0.9f);
    }
}

TEST(SpeakerLayout, PairwisePanningOnStereo) {
    speaker_layout layout(layouts::stereo()); // L(+30°), R(-30°)
    ASSERT_TRUE(layout.is_pairwise());

    const auto center = layout.vbap_gains({0.0f, 0.0f});
    EXPECT_NEAR(center[0], center[1], 1e-4f);
    EXPECT_NEAR(center[0], 1.0f / std::sqrt(2.0f), 1e-3f);

    const auto left = layout.vbap_gains(layout.speakers()[0]);
    EXPECT_NEAR(left[0], 1.0f, 1e-4f);
    EXPECT_NEAR(left[1], 0.0f, 1e-4f);
}

TEST(SpeakerLayout, PairwiseGainsAreEnergyNormalizedOnOctagon) {
    speaker_layout layout(layouts::octagon());
    ASSERT_TRUE(layout.is_pairwise());

    for (float az = -3.1f; az <= 3.1f; az += 0.13f) {
        const auto g      = layout.vbap_gains({az, 0.0f});
        float      e      = 0.f;
        int        active = 0;
        for (float v : g) {
            EXPECT_GE(v, 0.0f);
            e += v * v;
            if (v > 1e-6f) ++active;
        }
        EXPECT_NEAR(e, 1.0f, 1e-4f) << "az=" << az;
        EXPECT_LE(active, 2) << "pairwise panning uses at most one pair";
    }
}

TEST(SpeakerLayout, EmptyLayoutThrows) {
    EXPECT_THROW(speaker_layout(std::vector<spherical_coord> {}), std::invalid_argument);
}

TEST(Tdesigns, PointsAreOnUnitSphere) {
    for (int order = 1; order <= max_order; ++order) {
        size_t count         = 0;
        const float(*pts)[3] = tdesign_for_order(order, count);
        ASSERT_NE(pts, nullptr) << "order " << order;
        // ALLRAD needs t >= 2N+1, which needs at least (N+1)^2 points.
        EXPECT_GE(count, channel_count(order)) << "order " << order;
        for (size_t i = 0; i < count; ++i) {
            const float norm =
                std::sqrt(pts[i][0] * pts[i][0] + pts[i][1] * pts[i][1] + pts[i][2] * pts[i][2]);
            EXPECT_NEAR(norm, 1.0f, 1e-4f) << "order " << order << " point " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// Hull convexity and the VBAP velocity-vector invariant. Regression tests for
// the coplanar-face bug found by cross-library comparison (spaudiopy, see
// notebooks/library_comparison.ipynb): the incremental hull mis-stitched the
// cube's square faces, folding triangles through the interior, and ~20% of
// directions snapped to a single speaker up to 52 degrees away.
// ---------------------------------------------------------------------------

namespace {

    // No input point may lie strictly outside any hull face. Tolerance covers
    // the deterministic radius lift used to break coplanar ties (<= 1e-4).
    void expect_convex(const speaker_layout& layout, const char* name) {
        for (const auto& t : layout.triangles()) {
            const Eigen::Vector3f n = (layout.cartesian()[t.b] - layout.cartesian()[t.a])
                                          .cross(layout.cartesian()[t.c] - layout.cartesian()[t.a])
                                          .normalized();
            for (size_t i = 0; i < layout.cartesian().size(); ++i) {
                EXPECT_LE((layout.cartesian()[i] - layout.cartesian()[t.a]).dot(n), 5e-4f)
                    << name << ": point " << i << " outside a hull face";
            }
        }
    }

    // For sources inside the coverage, the VBAP velocity vector must point at
    // the source exactly (Pulkki's defining property).
    void expect_velocity_exact(const speaker_layout& layout, float el_lo, float el_hi,
                               const char* name) {
        std::mt19937                          rng(29);
        std::uniform_real_distribution<float> ua(-k_pi, k_pi);
        std::uniform_real_distribution<float> ue(el_lo, el_hi);
        for (int k = 0; k < 500; ++k) {
            const float     az = ua(rng);
            const float     el = ue(rng);
            const auto      g  = layout.vbap_gains({az, el});
            Eigen::Vector3d v  = Eigen::Vector3d::Zero();
            for (size_t s = 0; s < g.size(); ++s) {
                v += static_cast<double>(g[s]) * layout.cartesian()[s].cast<double>();
            }
            ASSERT_GT(v.norm(), 1e-6) << name;
            const double dot = v.normalized().dot(spherical_to_cartesian(az, el).cast<double>());
            // 0.1 degree: far above float32 solve noise (~0.02 deg observed),
            // far below the 50-degree failures of the coplanar-face bug.
            EXPECT_GT(dot, std::cos(0.1 * 3.14159265358979 / 180.0))
                << name << ": velocity vector off at az=" << az << " el=" << el;
        }
    }

} // namespace

TEST(SpeakerLayout, HullsAreConvexOnAllPresets) {
    expect_convex(speaker_layout(layouts::cube()), "cube");
    expect_convex(speaker_layout(layouts::surround_7_1_4()), "7.1.4");
    auto oct_z = layouts::octagon();
    oct_z.push_back({0.f, k_pi / 2});
    oct_z.push_back({0.f, -k_pi / 2});
    expect_convex(speaker_layout(oct_z), "octagon+zenith+nadir");
}

TEST(SpeakerLayout, VbapVelocityVectorIsExactInsideCoverage) {
    // The cube covers the whole sphere; its square faces are the coplanar
    // worst case for the hull.
    expect_velocity_exact(speaker_layout(layouts::cube()), -1.4f, 1.4f, "cube");
    // 7.1.4 has no bottom speakers: test the covered band only.
    expect_velocity_exact(speaker_layout(layouts::surround_7_1_4()), 0.05f, 0.45f, "7.1.4");
}
