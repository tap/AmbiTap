/// @file rotation.h
/// @brief Spherical harmonic rotation matrices from 3x3 Cartesian rotations.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <vector>

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include "indexing.h"
#include "rotation_recurrence.h"

namespace ambitap {

    /// Real-SH rotation matrix for a given 3x3 Cartesian rotation.
    ///
    /// For each order l, the (2l+1) x (2l+1) sub-block R_l satisfies
    ///   Y_l(R * d) = R_l * Y_l(d)
    /// for any direction d. Computed by the Ivanic-Ruedenberg recurrence
    /// (math/core/rotation_recurrence.h) — exact up to float roundoff; this
    /// class is an Eigen-typed convenience wrapper around it. (It replaced a
    /// sampling/least-squares construction that agreed with the recurrence
    /// to ~1e-6 through order 10 but was approximate and slower to build.)
    ///
    /// Euler convention (angle constructor): intrinsic Z-Y'-X'' — yaw about
    /// +Z first, pitch about +Y second, roll about +X last, right-hand rule
    /// (positive pitch tilts the front axis DOWN).
    class sh_rotation {
        int             m_order;
        size_t          m_num_channels;
        Eigen::MatrixXf m_matrix;

      public:
        sh_rotation(int order, const Eigen::Matrix3f& R)
            : m_order(order)
            , m_num_channels(channel_count(order)) {
            compute(R);
        }

        sh_rotation(int order, float yaw, float pitch, float roll)
            : m_order(order)
            , m_num_channels(channel_count(order)) {
            Eigen::Matrix3f R;
            R = Eigen::AngleAxisf(yaw, Eigen::Vector3f::UnitZ()) * Eigen::AngleAxisf(pitch, Eigen::Vector3f::UnitY())
                * Eigen::AngleAxisf(roll, Eigen::Vector3f::UnitX());
            compute(R);
        }

        sh_rotation(int order, const Eigen::Quaternionf& q)
            : m_order(order)
            , m_num_channels(channel_count(order)) {
            compute(q.toRotationMatrix());
        }

        int                    order() const { return m_order; }
        size_t                 num_channels() const { return m_num_channels; }
        const Eigen::MatrixXf& matrix() const { return m_matrix; }

      private:
        void compute(const Eigen::Matrix3f& R) {
            const auto C = static_cast<Eigen::Index>(m_num_channels);

            const float R9[9] = {R(0, 0), R(0, 1), R(0, 2), R(1, 0), R(1, 1), R(1, 2), R(2, 0), R(2, 1), R(2, 2)};
            std::vector<float> m(m_num_channels * m_num_channels);
            compute_sh_rotation_from_matrix(m_order, R9, m.data());

            m_matrix.resize(C, C);
            for (Eigen::Index r = 0; r < C; ++r) {
                for (Eigen::Index c = 0; c < C; ++c) {
                    m_matrix(r, c) = m[static_cast<size_t>(r) * m_num_channels + static_cast<size_t>(c)];
                }
            }
        }
    };

} // namespace ambitap
