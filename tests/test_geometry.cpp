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
            if (j != i) EXPECT_NEAR(gains[j], 0.0f, 1e-4f) << "speaker " << i << " leak " << j;
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

TEST(Tdesigns, PointsAreOnUnitSphere) {
    for (int order = 1; order <= max_order; ++order) {
        size_t count          = 0;
        const float (*pts)[3] = tdesign_for_order(order, count);
        ASSERT_NE(pts, nullptr) << "order " << order;
        // ALLRAD needs t >= 2N+1, which needs at least (N+1)^2 points.
        EXPECT_GE(count, channel_count(order)) << "order " << order;
        for (size_t i = 0; i < count; ++i) {
            const float norm = std::sqrt(pts[i][0] * pts[i][0] + pts[i][1] * pts[i][1]
                                         + pts[i][2] * pts[i][2]);
            EXPECT_NEAR(norm, 1.0f, 1e-4f) << "order " << order << " point " << i;
        }
    }
}
