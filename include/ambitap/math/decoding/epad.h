/// AmbiTap: target-independent ambisonics library
/// EPAD (Energy-Preserving Ambisonic Decoder) matrix construction.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_EPAD_H
#define AMBITAP_MATH_EPAD_H

#include "../core/coords.h"
#include "../core/indexing.h"
#include "../core/spherical_harmonics.h"
#include "max_re.h"

#include <Eigen/Dense>
#include <vector>

namespace ambitap {

    /// Compute an EPAD (Energy-Preserving Ambisonic Decoder) matrix for a given
    /// ambisonics order and speaker layout.
    ///
    /// Algorithm:
    ///   1. Build re-encoding matrix Y (L x C), Y[i][j] = SH_j(speaker_i).
    ///   2. Thin SVD: Y = U S V^T with U (L x r), V (C x r), r = min(L, C).
    ///   3. Replace the singular values with 1 (drop the S factor):
    ///        D_EPAD = U V^T   — shape (L, C).
    ///   The columns of D_EPAD are unitary (energy-preserving) regardless of
    ///   layout regularity, which is the property that distinguishes EPAD from
    ///   mode-matching for irregular speaker arrays.
    ///
    /// Reference: Zotter & Frank (2019), "Ambisonics: A Practical 3D Audio Theory
    /// for Recording, Studio Production, Sound Reinforcement, and Virtual Reality",
    /// Springer Open. See also Zotter et al. (2012) on All-Round Ambisonic Decoding.
    ///
    /// @param order       Ambisonics order (>= 0).
    /// @param speakers    Real speaker directions on the unit sphere.
    /// @param use_max_re  If true, apply max-rE per-order column weighting.
    /// @return Decoder matrix (num_speakers x (order+1)^2).
    inline Eigen::MatrixXf
    compute_epad_decoder(int                                 order,
                         const std::vector<spherical_coord>& speakers,
                         bool                                use_max_re = false) {
        const auto L = static_cast<Eigen::Index>(speakers.size());
        const auto C = static_cast<Eigen::Index>(channel_count(order));

        Eigen::MatrixXf Y(L, C);
        float           sh_buf[max_channel_count];
        for (Eigen::Index i = 0; i < L; ++i) {
            evaluate_sh(order, speakers[static_cast<size_t>(i)], sh_buf);
            for (Eigen::Index j = 0; j < C; ++j) {
                Y(i, j) = sh_buf[j];
            }
        }

        Eigen::JacobiSVD<Eigen::MatrixXf> svd(Y, Eigen::ComputeThinU | Eigen::ComputeThinV);
        const Eigen::MatrixXf&            U = svd.matrixU(); // (L, r)
        const Eigen::MatrixXf&            V = svd.matrixV(); // (C, r)

        Eigen::MatrixXf D = U * V.transpose(); // (L, C)

        if (use_max_re) {
            auto weights = max_re_weights(order);
            for (Eigen::Index j = 0; j < C; ++j) {
                int n = order_of(static_cast<size_t>(j));
                D.col(j) *= weights[static_cast<size_t>(n)];
            }
        }

        return D;
    }

} // namespace ambitap

#endif // AMBITAP_MATH_EPAD_H
