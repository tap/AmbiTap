/// AmbiTap: target-independent ambisonics library
/// ALLRAD (All-Round Ambisonic Decoder) matrix construction.
/// Timothy Place
/// Copyright 2025 Timothy Place.

#ifndef AMBITAP_MATH_ALLRAD_H
#define AMBITAP_MATH_ALLRAD_H

#include "../core/coords.h"
#include "../core/indexing.h"
#include "../core/spherical_harmonics.h"
#include "../geometry/speaker_layout.h"
#include "../geometry/tdesigns.h"
#include "max_re.h"

#include <Eigen/Dense>
#include <cmath>
#include <vector>

namespace ambitap {

    /// Compute an ALLRAD decoder matrix for a given ambisonics order and speaker layout.
    ///
    /// Combines a sampling decoder on a virtual T-design layout with VBAP panning to
    /// the real speaker layout. Works well for arbitrary speaker arrangements, including
    /// irregular layouts.
    ///
    /// Algorithm:
    ///   1. Pick a T-design virtual layout with t >= 2*N + 1 for order N.
    ///   2. Compute a sampling decoder for the virtual layout (closed form for T-designs).
    ///   3. For each virtual speaker, compute VBAP gains to the real speakers.
    ///   4. Combine: D_real = G_vbap^T * D_virtual.
    ///
    /// Reference: Zotter & Frank (2012), "All-Round Ambisonic Panning and Decoding",
    ///            J. Audio Eng. Soc. 60(10), pp. 807-820.
    ///
    /// @param order       Ambisonics order (>= 0).
    /// @param speakers    Real speaker directions on the unit sphere.
    /// @param use_max_re  If true, apply max-rE per-order weighting.
    /// @return Decoder matrix of shape (num_speakers x (order+1)^2). Each row is
    ///         the per-channel weights for one speaker; speaker_signals = D * hoa.
    inline Eigen::MatrixXf compute_allrad_decoder(int                                 order,
                                                  const std::vector<spherical_coord>& speakers,
                                                  bool                                use_max_re = true) {
        const auto     C = static_cast<Eigen::Index>(channel_count(order));
        speaker_layout layout(speakers);

        // Step 1: T-design virtual layout.
        size_t tdesign_count = 0;
        const float (*tdesign_pts)[3] = tdesign_for_order(order, tdesign_count);
        const auto V                  = static_cast<Eigen::Index>(tdesign_count);

        // Step 2: Sampling decoder for the T-design.
        // For a T-design with t >= 2N+1, the pseudoinverse simplifies to
        //   D_virtual = (4*pi / V) * Y
        // where Y is the re-encoding matrix.
        Eigen::MatrixXf Y_virtual(V, C);
        float           sh_buf[max_channel_count];

        for (Eigen::Index v = 0; v < V; ++v) {
            float x  = tdesign_pts[v][0];
            float y  = tdesign_pts[v][1];
            float z  = tdesign_pts[v][2];
            float az = std::atan2(y, x);
            float el = std::atan2(z, std::sqrt(x * x + y * y));
            evaluate_sh(order, az, el, sh_buf);
            for (Eigen::Index c = 0; c < C; ++c) {
                Y_virtual(v, c) = sh_buf[c];
            }
        }

        const float     weight    = static_cast<float>(4.0 * M_PI) / static_cast<float>(V);
        Eigen::MatrixXf D_virtual = weight * Y_virtual; // V x C

        // The closed-form sampling decoder requires an orthonormal (N3D) basis;
        // evaluate_sh() returns SN3D. Scale each order band by (2n+1) — one
        // sqrt(2n+1) for the basis at the virtual speaker and one for the input
        // coefficients. Without this the higher order bands are under-weighted
        // and the decode collapses toward omni.
        for (Eigen::Index c = 0; c < C; ++c) {
            int n = order_of(static_cast<size_t>(c));
            D_virtual.col(c) *= static_cast<float>(2 * n + 1);
        }

        if (use_max_re) {
            auto weights = max_re_weights(order);
            for (Eigen::Index c = 0; c < C; ++c) {
                int n = order_of(static_cast<size_t>(c));
                D_virtual.col(c) *= weights[static_cast<size_t>(n)];
            }
        }

        // Step 3: VBAP gains from each virtual speaker to the real speakers.
        const auto      L = static_cast<Eigen::Index>(speakers.size());
        Eigen::MatrixXf G(V, L);

        for (Eigen::Index v = 0; v < V; ++v) {
            float x  = tdesign_pts[v][0];
            float y  = tdesign_pts[v][1];
            float z  = tdesign_pts[v][2];
            float az = std::atan2(y, x);
            float el = std::atan2(z, std::sqrt(x * x + y * y));

            auto gains = layout.vbap_gains({az, el});
            for (Eigen::Index l = 0; l < L; ++l) {
                G(v, l) = gains[static_cast<size_t>(l)];
            }
        }

        // Step 4: Combine. G^T is L x V, D_virtual is V x C, result is L x C.
        return G.transpose() * D_virtual;
    }

} // namespace ambitap

#endif // AMBITAP_MATH_ALLRAD_H
