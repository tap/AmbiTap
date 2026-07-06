/// AmbiTap: target-independent ambisonics library
/// Tests for max-rE weights and the three decoder constructions.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include <cmath>

#include <Eigen/Dense>
#include <gtest/gtest.h>

#include "ambitap/math/core/spherical_harmonics.h"
#include "ambitap/math/decoding/allrad.h"
#include "ambitap/math/decoding/epad.h"
#include "ambitap/math/decoding/max_re.h"
#include "ambitap/math/decoding/mode_matching.h"
#include "ambitap/math/geometry/layouts.h"

using namespace ambitap;

namespace {

    Eigen::MatrixXf reencoding_matrix(int order, const std::vector<spherical_coord>& speakers) {
        const auto      L = static_cast<Eigen::Index>(speakers.size());
        const auto      C = static_cast<Eigen::Index>(channel_count(order));
        Eigen::MatrixXf Y(L, C);
        float           sh[max_channel_count];
        for (Eigen::Index i = 0; i < L; ++i) {
            evaluate_sh(order, speakers[static_cast<size_t>(i)], sh);
            for (Eigen::Index j = 0; j < C; ++j)
                Y(i, j) = sh[j];
        }
        return Y;
    }

    Eigen::VectorXf encode(int order, spherical_coord dir) {
        float sh[max_channel_count];
        evaluate_sh(order, dir, sh);
        Eigen::VectorXf v(static_cast<Eigen::Index>(channel_count(order)));
        for (Eigen::Index i = 0; i < v.size(); ++i)
            v(i) = sh[i];
        return v;
    }

    // Index of the loudest speaker when decoding a source at `dir`.
    Eigen::Index loudest_speaker(const Eigen::MatrixXf& D, int order, spherical_coord dir) {
        Eigen::VectorXf s   = D * encode(order, dir);
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
    EXPECT_TRUE(YtD.isApprox(Eigen::MatrixXf::Identity(4, 4), 1e-4f)) << "Y^T * D =\n" << YtD;
}

TEST(ModeMatching, LocalizesToNearestSpeaker) {
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto D        = compute_mode_matching_decoder(order, speakers);

    for (size_t i = 0; i < speakers.size(); ++i) {
        EXPECT_EQ(loudest_speaker(D, order, speakers[i]), static_cast<Eigen::Index>(i));
    }
}

TEST(Epad, EnergyPreservingInN3dUpToScale) {
    // The polar factor has orthonormal columns; with the 1/sqrt(L) scale and
    // the SN3D column weighting folded in, D^T D == (1/L) * diag(2n+1) on a
    // full-rank layout.
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto D        = compute_epad_decoder(order, speakers);

    ASSERT_EQ(D.rows(), 8);
    ASSERT_EQ(D.cols(), 4);
    Eigen::MatrixXf expected = Eigen::MatrixXf::Zero(4, 4);
    for (Eigen::Index j = 0; j < 4; ++j) {
        const float f  = static_cast<float>(2 * order_of(static_cast<size_t>(j)) + 1);
        expected(j, j) = f / 8.0f;
    }
    Eigen::MatrixXf DtD = D.transpose() * D;
    EXPECT_TRUE(DtD.isApprox(expected, 1e-4f)) << "D^T * D =\n" << DtD;
}

TEST(Epad, MatchesModeMatchingOnTDesignLayout) {
    // The cube is a spherical 3-design, so at order 1 EPAD and mode matching
    // must coincide exactly (all singular values equal sqrt(L)). This pins the
    // absolute gain convention shared by the decoders.
    const int  order = 1;
    const auto Dep   = compute_epad_decoder(order, layouts::cube());
    const auto Dmm   = compute_mode_matching_decoder(order, layouts::cube());
    EXPECT_TRUE(Dep.isApprox(Dmm, 1e-4f)) << "EPAD =\n" << Dep << "\nmode-matching =\n" << Dmm;
}

// Audit finding C2: without singular-value truncation, EPAD emitted
// unit-energy output on SH channels a layout cannot reproduce (the octagon
// has no height information; its Z column must be zero, not an arbitrary
// unitary completion).
TEST(Epad, DropsChannelsTheLayoutCannotReproduce) {
    const int  order = 1;
    const auto D     = compute_epad_decoder(order, layouts::octagon());

    ASSERT_EQ(D.cols(), 4);
    EXPECT_NEAR(D.col(2).norm(), 0.0f, 1e-5f) << "Z column (ACN 2) must be dropped";
    EXPECT_GT(D.col(0).norm(), 0.1f);
    EXPECT_GT(D.col(1).norm(), 0.1f);
    EXPECT_GT(D.col(3).norm(), 0.1f);
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

// Audit finding B1: ALLRAD used a 4*pi/V virtual-decoder weight, making it
// ~22 dB hotter than the other decoders. All decoders now share one absolute
// gain convention: decoding a unit omni field yields energy 1/L on a uniform
// layout, exactly for mode-matching/EPAD and within coherent-VBAP-spread
// bounds for ALLRAD.
TEST(DecoderAbsoluteGain, ConsistentAcrossDecoders) {
    const int  order    = 1;
    const auto speakers = layouts::cube();
    const auto Dmm      = compute_mode_matching_decoder(order, speakers);
    const auto Dar      = compute_allrad_decoder(order, speakers, false);
    const auto Dep      = compute_epad_decoder(order, speakers);

    const float e_mm = Dmm.col(0).squaredNorm();
    const float e_ar = Dar.col(0).squaredNorm();
    const float e_ep = Dep.col(0).squaredNorm();

    EXPECT_NEAR(e_mm, 1.0f / 8.0f, 1e-5f);
    EXPECT_NEAR(e_ep, 1.0f / 8.0f, 1e-5f);
    // ALLRAD's VBAP stage spreads each virtual source over up to 3 speakers
    // coherently, so its omni energy sits a little above 1/L — but nowhere
    // near the (4*pi)^2 ~ 158x of the old bug.
    EXPECT_GT(e_ar, 0.9f * e_mm);
    EXPECT_LT(e_ar, 4.0f * e_mm);

    // Point sources: decoded energy of ALLRAD tracks mode-matching within a
    // small band across directions.
    for (float az = -3.0f; az <= 3.0f; az += 0.61f) {
        for (float el = -1.2f; el <= 1.2f; el += 0.57f) {
            const auto  v     = encode(order, {az, el});
            const float ratio = (Dar * v).squaredNorm() / (Dmm * v).squaredNorm();
            EXPECT_GT(ratio, 0.5f) << "az=" << az << " el=" << el;
            EXPECT_LT(ratio, 2.5f) << "az=" << az << " el=" << el;
        }
    }
}

// Audit finding B2: the mode-matching pseudoinverse must be computed in the
// orthonormal (N3D) basis. In the least-squares regime (L < C: 5.1 at order
// 2) the SN3D and N3D constructions genuinely differ; this pins the N3D one
// by its defining property — for any speaker-feed vector g, decoding the
// re-encoded field D * (Y_n3d^T g) reproduces the component of g the layout
// observes, i.e. D is pinv(Y_n3d^T) with bounded gains.
TEST(ModeMatching, UnderdeterminedRegimeUsesN3dBasis) {
    const int  order    = 2;
    const auto speakers = layouts::surround_5_1(); // L=5 < C=9
    const auto D        = compute_mode_matching_decoder(order, speakers);

    ASSERT_EQ(D.rows(), 5);
    ASSERT_EQ(D.cols(), 9);
    EXPECT_TRUE(D.allFinite());

    // N3D re-encoding matrix.
    Eigen::MatrixXf Y = reencoding_matrix(order, speakers);
    for (Eigen::Index j = 0; j < Y.cols(); ++j) {
        Y.col(j) *= std::sqrt(static_cast<float>(2 * order_of(static_cast<size_t>(j)) + 1));
    }
    // D in the N3D basis: a_sn3d = W^-1 a_n3d, D_n3d = D * W^-1 ... folding the
    // SN3D weighting out again gives the polar-free pinv(Y_n3d^T).
    Eigen::MatrixXf Dn3d = D;
    for (Eigen::Index j = 0; j < Dn3d.cols(); ++j) {
        Dn3d.col(j) /= std::sqrt(static_cast<float>(2 * order_of(static_cast<size_t>(j)) + 1));
    }
    // Defining property of the pseudoinverse with L < C, full row rank:
    // pinv(Y^T) * Y^T == I_L (decode of any re-encoded speaker feed is exact).
    Eigen::MatrixXf DY = Dn3d * Y.transpose();
    EXPECT_TRUE(DY.isApprox(Eigen::MatrixXf::Identity(5, 5), 1e-3f)) << "D * Y_n3d^T =\n" << DY;

    // Regression guard: the (wrong) SN3D-basis pseudoinverse fails the same
    // identity by a large margin, so this test genuinely discriminates.
    Eigen::MatrixXf                   Ysn = reencoding_matrix(order, speakers);
    Eigen::JacobiSVD<Eigen::MatrixXf> svd(Ysn, Eigen::ComputeThinU | Eigen::ComputeThinV);
    Eigen::MatrixXf                   pinv_sn = svd.solve(Eigen::MatrixXf::Identity(5, 5)).transpose();
    Eigen::MatrixXf                   DYsn    = pinv_sn * Y.transpose();
    EXPECT_FALSE(DYsn.isApprox(Eigen::MatrixXf::Identity(5, 5), 1e-3f));
}

TEST(MaxRe, EnergyNormalizedWeightsPreserveTotalEnergy) {
    for (int order : {1, 2, 3, 5}) {
        const auto w   = max_re_weights_energy_normalized(order);
        float      num = 0.f, den = 0.f;
        for (int n = 0; n <= order; ++n) {
            const float g = static_cast<float>(2 * n + 1);
            num += g;
            den += g * w[static_cast<size_t>(n)] * w[static_cast<size_t>(n)];
        }
        EXPECT_NEAR(den, num, 1e-3f * num) << "order " << order;
        // Still max-rE-shaped: monotonically decreasing across orders.
        for (size_t n = 1; n < w.size(); ++n)
            EXPECT_LT(w[n], w[n - 1]);
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
