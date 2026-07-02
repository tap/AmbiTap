/// AmbiTap: target-independent ambisonics library
/// Tests for SH rotation matrices.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/math/core/rotation.h"
#include "ambitap/math/core/spherical_harmonics.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <cmath>

using namespace ambitap;

namespace {

    Eigen::VectorXf sh_at(int order, const Eigen::Vector3f& d) {
        float       sh[max_channel_count];
        const float az = std::atan2(d.y(), d.x());
        const float el = std::atan2(d.z(), std::sqrt(d.x() * d.x() + d.y() * d.y()));
        evaluate_sh(order, az, el, sh);
        Eigen::VectorXf v(static_cast<Eigen::Index>(channel_count(order)));
        for (Eigen::Index i = 0; i < v.size(); ++i) v(i) = sh[i];
        return v;
    }

} // namespace

TEST(Rotation, IdentityIsIdentity) {
    constexpr int order = 3;
    sh_rotation   rot(order, Eigen::Matrix3f::Identity());
    EXPECT_TRUE(rot.matrix().isApprox(
        Eigen::MatrixXf::Identity(rot.matrix().rows(), rot.matrix().cols()), 1e-4f));
}

TEST(Rotation, FullTurnYawIsIdentity) {
    constexpr int order = 4;
    sh_rotation   rot(order, 2.0f * static_cast<float>(M_PI), 0.0f, 0.0f);
    EXPECT_TRUE(rot.matrix().isApprox(
        Eigen::MatrixXf::Identity(rot.matrix().rows(), rot.matrix().cols()), 1e-3f));
}

TEST(Rotation, OmniChannelInvariant) {
    sh_rotation rot(5, 0.8f, -0.4f, 1.1f);
    const auto& M = rot.matrix();
    EXPECT_NEAR(M(0, 0), 1.0f, 1e-5f);
    for (Eigen::Index j = 1; j < M.cols(); ++j) {
        EXPECT_NEAR(M(0, j), 0.0f, 1e-4f);
        EXPECT_NEAR(M(j, 0), 0.0f, 1e-4f);
    }
}

TEST(Rotation, MatchesDirectEvaluation) {
    // Defining property: Y(R * d) == R_sh * Y(d) for any direction d.
    constexpr int order = 3;

    Eigen::Matrix3f R;
    R = Eigen::AngleAxisf(0.7f, Eigen::Vector3f::UnitZ())
        * Eigen::AngleAxisf(-0.3f, Eigen::Vector3f::UnitY())
        * Eigen::AngleAxisf(0.5f, Eigen::Vector3f::UnitX());
    sh_rotation rot(order, R);

    const Eigen::Vector3f dirs[] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        Eigen::Vector3f(0.3f, -0.8f, 0.5f).normalized(),
        Eigen::Vector3f(-0.6f, 0.2f, -0.75f).normalized(),
    };

    for (const auto& d : dirs) {
        Eigen::VectorXf expected = sh_at(order, (R * d).eval());
        Eigen::VectorXf actual   = rot.matrix() * sh_at(order, d);
        EXPECT_TRUE(actual.isApprox(expected, 1e-3f))
            << "d=(" << d.transpose() << ")\nexpected " << expected.transpose() << "\nactual   "
            << actual.transpose();
    }
}

TEST(Rotation, OrderBandsAreOrthogonal) {
    // Each (2l+1)-sized diagonal block is a rotation in SH space, so it must
    // be orthogonal: B * B^T == I. (Real SH rotation matrices are orthogonal
    // for both N3D and SN3D within an order band, because the per-degree
    // normalization is uniform inside a band.)
    constexpr int order = 4;
    sh_rotation   rot(order, -1.3f, 0.6f, 0.2f);
    const auto&   M = rot.matrix();

    for (int l = 0; l <= order; ++l) {
        const auto      offset = static_cast<Eigen::Index>(acn_index(l, -l));
        const auto      size   = static_cast<Eigen::Index>(2 * l + 1);
        Eigen::MatrixXf B      = M.block(offset, offset, size, size);
        EXPECT_TRUE((B * B.transpose()).isApprox(Eigen::MatrixXf::Identity(size, size), 1e-3f))
            << "order band l=" << l;
    }
}
