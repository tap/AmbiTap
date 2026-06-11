/// AmbiTap: target-independent ambisonics library
/// Mode-matching ambisonics decoder matrix construction.
/// Timothy Place
/// Copyright 2025 Timothy Place.

#ifndef AMBITAP_MATH_MODE_MATCHING_H
#define AMBITAP_MATH_MODE_MATCHING_H

#include "../core/coords.h"
#include "../core/indexing.h"
#include "../core/spherical_harmonics.h"
#include "max_re.h"

#include <Eigen/Dense>
#include <vector>

namespace ambitap {

    /// Compute a mode-matching (pseudoinverse) decoder matrix for a given ambisonics
    /// order and speaker layout.
    ///
    /// Algorithm:
    ///   1. Build the re-encoding matrix Y (L x C) where Y[i][j] = SH_j at speaker_i.
    ///   2. Take the Moore-Penrose pseudoinverse of Y via JacobiSVD.
    ///   3. Transpose to put it in (L x C) shape where speaker_signals = D * hoa.
    ///   4. Optionally apply per-order max-rE weighting.
    ///
    /// Works for any (L, C) shape — under-, square-, and over-determined cases all
    /// use the same SVD-based pseudoinverse.
    ///
    /// @param order       Ambisonics order (>= 0).
    /// @param speakers    Real speaker directions on the unit sphere.
    /// @param use_max_re  If true, apply max-rE per-order weighting.
    /// @return Decoder matrix of shape (num_speakers x (order+1)^2). Each row is
    ///         the per-channel weights for one speaker; speaker_signals = D * hoa.
    inline Eigen::MatrixXf
    compute_mode_matching_decoder(int                                 order,
                                  const std::vector<spherical_coord>& speakers,
                                  bool                                use_max_re = false) {
        const auto L = static_cast<Eigen::Index>(speakers.size());
        const auto C = static_cast<Eigen::Index>(channel_count(order));

        // Re-encoding matrix Y (L x C): row i is the SH basis evaluated at speaker i.
        Eigen::MatrixXf Y(L, C);
        float           sh_buf[max_channel_count];
        for (Eigen::Index i = 0; i < L; ++i) {
            evaluate_sh(order, speakers[static_cast<size_t>(i)], sh_buf);
            for (Eigen::Index j = 0; j < C; ++j) {
                Y(i, j) = sh_buf[j];
            }
        }

        // pinv(Y) via thin-SVD. JacobiSVD::solve returns the least-squares solution
        // to Y * X = I_LxL, i.e. pinv(Y) of shape (C x L); transpose for (L x C).
        Eigen::JacobiSVD<Eigen::MatrixXf> svd(Y, Eigen::ComputeThinU | Eigen::ComputeThinV);
        Eigen::MatrixXf                   pinv_Y = svd.solve(Eigen::MatrixXf::Identity(L, L));
        Eigen::MatrixXf                   D      = pinv_Y.transpose();

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

#endif // AMBITAP_MATH_MODE_MATCHING_H
