/// @file allrad.h
/// @brief ALLRAD (All-Round Ambisonic Decoder) matrix construction.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <cmath>
#include <vector>

#include <Eigen/Dense>

#include "../core/coords.h"
#include "../core/indexing.h"
#include "../core/spherical_harmonics.h"
#include "../core/validate.h"
#include "../geometry/speaker_layout.h"
#include "../geometry/tdesigns.h"
#include "max_re.h"

namespace tap::ambi {

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
    /// @param order       Ambisonics order in [0, k_max_order].
    /// @param speakers    Real speaker directions on the unit sphere (non-empty).
    /// @param use_max_re  If true, apply energy-normalized max-rE per-order weighting.
    /// @return Decoder matrix of shape (num_speakers x (order+1)^2). Each row is
    ///         the per-channel weights for one speaker; speaker_signals = D * hoa.
    ///         Absolute gain matches compute_mode_matching_decoder: decoding a
    ///         unit-pressure omni field yields unit summed pressure.
    /// @throws std::invalid_argument on out-of-range order or empty speaker list.
    inline Eigen::MatrixXf compute_allrad_decoder(int order, const std::vector<spherical_coord>& speakers,
                                                  bool use_max_re = true) {
        validated_order(order, "compute_allrad_decoder");
        // speaker_layout validates non-emptiness.
        const auto     C = static_cast<Eigen::Index>(channel_count(order));
        speaker_layout layout(speakers);

        // Step 1: T-design virtual layout.
        size_t tdesign_count         = 0;
        const float(*tdesign_pts)[3] = tdesign_for_order(order, tdesign_count);
        const auto V                 = static_cast<Eigen::Index>(tdesign_count);

        // Step 2: Sampling decoder for the T-design.
        // For a T-design with t >= 2N+1 in the ambisonic convention (Y_00 = 1),
        // the pseudoinverse simplifies to
        //   D_virtual = (1 / V) * Y * diag(2n+1)
        // where Y is the SN3D re-encoding matrix (the per-order factor is applied
        // below). The 4*pi factor familiar from the physics-orthonormal SH
        // convention (where the integral of Y^2 over the sphere is 1) does NOT
        // appear here; including it would make ALLRAD ~22 dB hotter than the
        // library's other decoders.
        Eigen::MatrixXf Y_virtual(V, C);
        float           sh_buf[k_max_channel_count];

        for (Eigen::Index v = 0; v < V; ++v) {
            const auto dir = to_spherical(tdesign_pts[v][0], tdesign_pts[v][1], tdesign_pts[v][2]);
            evaluate_sh(order, dir, sh_buf);
            for (Eigen::Index c = 0; c < C; ++c) {
                Y_virtual(v, c) = sh_buf[c];
            }
        }

        const float     weight    = 1.0f / static_cast<float>(V);
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
            auto weights = max_re_weights_energy_normalized(order);
            for (Eigen::Index c = 0; c < C; ++c) {
                int n = order_of(static_cast<size_t>(c));
                D_virtual.col(c) *= weights[static_cast<size_t>(n)];
            }
        }

        // Step 3: VBAP gains from each virtual speaker to the real speakers.
        const auto      L = static_cast<Eigen::Index>(speakers.size());
        Eigen::MatrixXf G(V, L);

        for (Eigen::Index v = 0; v < V; ++v) {
            const auto dir = to_spherical(tdesign_pts[v][0], tdesign_pts[v][1], tdesign_pts[v][2]);

            auto gains = layout.vbap_gains(dir);
            for (Eigen::Index l = 0; l < L; ++l) {
                G(v, l) = gains[static_cast<size_t>(l)];
            }
        }

        // Step 4: Combine. G^T is L x V, D_virtual is V x C, result is L x C.
        return G.transpose() * D_virtual;
    }

} // namespace tap::ambi
