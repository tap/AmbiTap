/// AmbiTap: target-independent ambisonics library
/// Tests for max-rE weights and the three decoder constructions.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/math/core/spherical_harmonics.h"
#include "ambitap/math/decoding/allrad.h"
#include "ambitap/math/decoding/epad.h"
#include "ambitap/math/decoding/max_re.h"
#include "ambitap/math/decoding/mode_matching.h"
#include "ambitap/math/geometry/layouts.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>
#include <cmath>

using namespace ambitap;

namespace {

Eigen::MatrixXf reencoding_matrix(int order, const std::vector<spherical_coord>& speakers) {
    const auto      L = static_cast<Eigen::Index>(speakers.size());
    const auto      C = static_cast<Eigen::Index>(channel_count(order));
    Eigen::MatrixXf Y(L, C);
    float           sh[max_channel_count];
    for (Eigen::Index i = 0; i < L; ++i) {
        evaluate_sh(order, speakers[static_cast<size_t>(i)], sh);
        for (Eigen::Index j = 0; j < C; ++j) Y(i, j) = sh[j];
    }
    return Y;
}

Eigen::VectorXf encode(int order, spherical_coord dir) {
    float sh[max_channel_count];
    evaluate_sh(order, dir, sh);
    Eigen::VectorXf v(static_cast<Eigen::Index>(channel_count(order)));
    for (Eigen::Index i = 0; i < v.size(); ++i) v(i) = sh[i];
    return v;
}

// Index of the loudest speaker when decoding a source at `dir`.
Eigen::Index loudest_speaker(const Eigen::MatrixXf& D, int order, spherical_coord dir) {
    Eigen::VectorXf s = D * encode(order, dir);
    Eigen::Index    idx = 0;
    s.cwiseAbs().maxCoeff(&idx);
    return idx;
}

} // namespace

TEST(MaxRe, WeightsDecreaseFromUnity) {
    for (int order : {1, 3, 5}) {
        const auto w = max_re_weights(order);
        ASSERT_EQ(w.size(), static_cast<size_t>(order) + 1);
        EXPECT_FLOAT_EQ(w[0], 1.0f);
        for (size_t n = 1; n < w.size(); ++n) {
            EXPECT_LT(w[n], w[n - 1]) << "order " << order << " n=" << n;
            EXPECT_GT(w[n], 0.0f) << "order " << order << " n=" << n;
        }
    }
}

TEST(ModeMatching, ReencodeIsIdentityWhenOverdetermined) {
    // Cube (8 speakers) at order 1 (4 channels): decode-then-re-encode must be
    // the identity, since D == pinv(Y)^T and rank(Y) == C.
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto D        = compute_mode_matching_decoder(order, speakers);
    const auto Y        = reencoding_matrix(order, speakers);

    ASSERT_EQ(D.rows(), 8);
    ASSERT_EQ(D.cols(), 4);
    Eigen::MatrixXf YtD = Y.transpose() * D; // (C x C)
    EXPECT_TRUE(YtD.isApprox(Eigen::MatrixXf::Identity(4, 4), 1e-4f))
        << "Y^T * D =\n" << YtD;
}

TEST(ModeMatching, LocalizesToNearestSpeaker) {
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto D        = compute_mode_matching_decoder(order, speakers);

    for (size_t i = 0; i < speakers.size(); ++i) {
        EXPECT_EQ(loudest_speaker(D, order, speakers[i]), static_cast<Eigen::Index>(i));
    }
}

TEST(Epad, ColumnsAreUnitary) {
    // D = U V^T from the thin SVD, so D^T D == I when L >= C.
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto D        = compute_epad_decoder(order, speakers);

    ASSERT_EQ(D.rows(), 8);
    ASSERT_EQ(D.cols(), 4);
    Eigen::MatrixXf DtD = D.transpose() * D;
    EXPECT_TRUE(DtD.isApprox(Eigen::MatrixXf::Identity(4, 4), 1e-4f))
        << "D^T * D =\n" << DtD;
}

TEST(Epad, LocalizesToNearestSpeaker) {
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto D        = compute_epad_decoder(order, speakers);

    for (size_t i = 0; i < speakers.size(); ++i) {
        EXPECT_EQ(loudest_speaker(D, order, speakers[i]), static_cast<Eigen::Index>(i));
    }
}

TEST(Allrad, ShapeAndLocalization) {
    const int  order    = 3;
    const auto speakers = layouts::cube();
    const auto D        = compute_allrad_decoder(order, speakers);

    ASSERT_EQ(D.rows(), 8);
    ASSERT_EQ(D.cols(), 16);
    EXPECT_TRUE(D.allFinite());

    for (size_t i = 0; i < speakers.size(); ++i) {
        EXPECT_EQ(loudest_speaker(D, order, speakers[i]), static_cast<Eigen::Index>(i));
    }
}

TEST(Allrad, EnergyRoughlyUniformAcrossDirections) {
    // ALLRAD + max-rE on a regular layout should keep decoded energy roughly
    // direction-independent. Allow a generous band; this guards against gross
    // construction errors, not psychoacoustic optimality.
    const int  order    = 3;
    const auto speakers = layouts::cube();
    const auto D        = compute_allrad_decoder(order, speakers);

    float min_e = 1e30f, max_e = 0.0f;
    for (float az = -3.0f; az <= 3.0f; az += 0.5f) {
        for (float el = -1.2f; el <= 1.2f; el += 0.4f) {
            const float e = (D * encode(order, {az, el})).squaredNorm();
            min_e         = std::min(min_e, e);
            max_e         = std::max(max_e, e);
        }
    }
    EXPECT_GT(min_e, 0.0f);
    EXPECT_LT(max_e / min_e, 2.0f) << "min=" << min_e << " max=" << max_e;
}
