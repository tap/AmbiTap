/// AmbiTap: target-independent ambisonics library
/// EPAD (Energy-Preserving Ambisonic Decoder) matrix construction.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_EPAD_H
#define AMBITAP_MATH_EPAD_H

#include <cmath>
#include <stdexcept>
#include <vector>

#include <Eigen/Dense>

#include "../core/coords.h"
#include "../core/indexing.h"
#include "../core/spherical_harmonics.h"
#include "../core/validate.h"
#include "max_re.h"

namespace ambitap {

    /// Compute an EPAD (Energy-Preserving Ambisonic Decoder) matrix for a given
    /// ambisonics order and speaker layout.
    ///
    /// EPAD is defined on the orthonormal (N3D) basis; the construction converts
    /// to N3D, builds the polar factor there, and folds the conversion back so
    /// the decoder accepts the library's SN3D signals directly:
    ///   1. Build the N3D re-encoding matrix Y (L x C): Y[i][j] = SH_j(speaker_i)
    ///      scaled by sqrt(2n_j + 1).
    ///   2. Thin SVD: Y = U S V^T. Truncate to the r singular values above
    ///      1e-3 * sigma_max — for rank-deficient layouts (e.g. a horizontal ring,
    ///      which cannot reproduce height) the discarded pairs correspond to SH
    ///      channels the layout cannot render; without truncation their U/V
    ///      columns are arbitrary and would inject garbage into the decode.
    ///   3. D = (1 / sqrt(L)) * U_r V_r^T, then re-apply the sqrt(2n_j + 1)
    ///      column scaling for SN3D input. The 1/sqrt(L) factor matches
    ///      mode-matching's absolute gain on uniform (t-design) layouts, where
    ///      the two constructions coincide exactly: the library's N3D basis is
    ///      unit-mean-square over the sphere, so a t-design has Y^T Y = L * I.
    ///
    /// The N3D-basis columns of the polar factor are orthogonal with equal norm
    /// (energy-preserving up to rank), which is the property that distinguishes
    /// EPAD from mode-matching for irregular speaker arrays.
    ///
    /// Reference: Zotter & Frank (2019), "Ambisonics: A Practical 3D Audio Theory
    /// for Recording, Studio Production, Sound Reinforcement, and Virtual Reality",
    /// Springer Open. See also Zotter et al. (2012) on All-Round Ambisonic Decoding.
    ///
    /// @param order       Ambisonics order in [0, max_order].
    /// @param speakers    Real speaker directions on the unit sphere (non-empty).
    /// @param use_max_re  If true, apply energy-normalized max-rE column weighting.
    /// @return Decoder matrix (num_speakers x (order+1)^2); speaker_signals = D * hoa.
    /// @throws std::invalid_argument on out-of-range order or empty speaker list.
    inline Eigen::MatrixXf compute_epad_decoder(int order, const std::vector<spherical_coord>& speakers,
                                                bool use_max_re = false) {
        validated_order(order, "compute_epad_decoder");
        if (speakers.empty()) {
            throw std::invalid_argument("ambitap::compute_epad_decoder: empty speaker list");
        }

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
        Eigen::VectorXf n3d(C);
        for (Eigen::Index j = 0; j < C; ++j) {
            n3d(j) = std::sqrt(static_cast<float>(2 * order_of(static_cast<size_t>(j)) + 1));
        }
        Y = Y * n3d.asDiagonal();

        Eigen::JacobiSVD<Eigen::MatrixXf> svd(Y, Eigen::ComputeThinU | Eigen::ComputeThinV);
        const Eigen::MatrixXf&            U = svd.matrixU(); // (L, min(L,C))
        const Eigen::MatrixXf&            V = svd.matrixV(); // (C, min(L,C))
        const Eigen::VectorXf&            S = svd.singularValues();

        // Truncate: keep only singular pairs the layout can actually reproduce.
        Eigen::Index r = 0;
        while (r < S.size() && S(r) > 1e-3f * S(0))
            ++r;

        const float     scale = 1.0f / std::sqrt(static_cast<float>(L));
        Eigen::MatrixXf D =
            scale * (U.leftCols(r) * V.leftCols(r).transpose()) * n3d.asDiagonal(); // (L, C), SN3D input

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

#endif // AMBITAP_MATH_EPAD_H
