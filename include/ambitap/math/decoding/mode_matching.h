/// AmbiTap: target-independent ambisonics library
/// Mode-matching ambisonics decoder matrix construction.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_MODE_MATCHING_H
#define AMBITAP_MATH_MODE_MATCHING_H

#include "../core/coords.h"
#include "../core/indexing.h"
#include "../core/normalization.h"
#include "../core/spherical_harmonics.h"
#include "../core/validate.h"
#include "max_re.h"

#include <Eigen/Dense>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace ambitap {

    /// Compute a mode-matching (pseudoinverse) decoder matrix for a given ambisonics
    /// order and speaker layout.
    ///
    /// Mode matching is defined on the orthonormal (N3D) basis; since the library's
    /// signals are SN3D, the construction converts to N3D, inverts there, and folds
    /// the conversion back into the decoder:
    ///   1. Build the re-encoding matrix Y (L x C), Y[i][j] = SH_j at speaker_i,
    ///      and scale column j by sqrt(2n_j + 1) (SN3D -> N3D).
    ///   2. Take the Moore-Penrose pseudoinverse via JacobiSVD, with a relative
    ///      singular-value threshold of 1e-4 to keep panning gains bounded on
    ///      irregular layouts.
    ///   3. Transpose to (L x C) with speaker_signals = D * hoa, and re-apply the
    ///      sqrt(2n_j + 1) column scaling so D accepts SN3D input directly.
    ///   4. Optionally apply energy-normalized per-order max-rE weighting.
    ///
    /// For square/overdetermined full-rank layouts the result is identical to the
    /// plain SN3D pseudoinverse; the N3D weighting matters in the least-squares
    /// (L < C) regime, where it keeps the per-order residuals correctly weighted.
    ///
    /// @param order       Ambisonics order in [0, max_order].
    /// @param speakers    Real speaker directions on the unit sphere (non-empty).
    /// @param use_max_re  If true, apply max-rE per-order weighting.
    /// @return Decoder matrix of shape (num_speakers x (order+1)^2). Each row is
    ///         the per-channel weights for one speaker; speaker_signals = D * hoa.
    /// @throws std::invalid_argument on out-of-range order or empty speaker list.
    inline Eigen::MatrixXf
    compute_mode_matching_decoder(int                                 order,
                                  const std::vector<spherical_coord>& speakers,
                                  bool                                use_max_re = false) {
        validated_order(order, "compute_mode_matching_decoder");
        if (speakers.empty()) {
            throw std::invalid_argument("ambitap::compute_mode_matching_decoder: empty speaker list");
        }

        const auto L = static_cast<Eigen::Index>(speakers.size());
        const auto C = static_cast<Eigen::Index>(channel_count(order));

        // Re-encoding matrix in N3D: row i is the SH basis evaluated at speaker i,
        // column j scaled by the SN3D -> N3D factor sqrt(2n_j + 1).
        Eigen::MatrixXf Y(L, C);
        float           sh_buf[max_channel_count];
        for (Eigen::Index i = 0; i < L; ++i) {
            evaluate_sh(order, speakers[static_cast<size_t>(i)], sh_buf);
            for (Eigen::Index j = 0; j < C; ++j) {
                Y(i, j) = sh_buf[j];
            }
        }
        Eigen::VectorXf n3d(C);
        for (Eigen::Index j = 0; j < C; ++j) {
            n3d(j) = std::sqrt(static_cast<float>(2 * order_of(static_cast<size_t>(j)) + 1));
        }
        Y = Y * n3d.asDiagonal();

        // pinv(Y) via thin-SVD. JacobiSVD::solve returns the least-squares solution
        // to Y * X = I_LxL, i.e. pinv(Y) of shape (C x L); transpose for (L x C).
        Eigen::JacobiSVD<Eigen::MatrixXf> svd(Y, Eigen::ComputeThinU | Eigen::ComputeThinV);
        svd.setThreshold(1e-4f);
        Eigen::MatrixXf pinv_Y = svd.solve(Eigen::MatrixXf::Identity(L, L));
        Eigen::MatrixXf D      = pinv_Y.transpose() * n3d.asDiagonal();

        if (use_max_re) {
            auto weights = max_re_weights_energy_normalized(order);
            for (Eigen::Index j = 0; j < C; ++j) {
                int n = order_of(static_cast<size_t>(j));
                D.col(j) *= weights[static_cast<size_t>(n)];
            }
        }

        return D;
    }

} // namespace ambitap

#endif // AMBITAP_MATH_MODE_MATCHING_H
