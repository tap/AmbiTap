/// AmbiTap: target-independent ambisonics library
/// Spherical harmonic rotation matrices from 3x3 Cartesian rotations.
/// Timothy Place
/// Copyright 2025 Timothy Place.

#ifndef AMBITAP_MATH_ROTATION_H
#define AMBITAP_MATH_ROTATION_H

#include "indexing.h"
#include "spherical_harmonics.h"

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ambitap {

    /// Real-SH rotation matrix for a given 3x3 Cartesian rotation.
    ///
    /// For each order l, the (2l+1) x (2l+1) sub-block R_l satisfies
    ///   Y_l(R * d) = R_l * Y_l(d)
    /// for any direction d. R_l is computed by evaluating SH at many linearly
    /// independent test directions and solving the overdetermined system.
    ///
    /// More robust than the Ivanic-Ruedenberg recurrence at the cost of a slightly
    /// more expensive construction.
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
            R = Eigen::AngleAxisf(yaw, Eigen::Vector3f::UnitZ())
                * Eigen::AngleAxisf(pitch, Eigen::Vector3f::UnitY())
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
            m_matrix     = Eigen::MatrixXf::Zero(C, C);

            // Order 0 is invariant under rotation.
            m_matrix(0, 0) = 1.0f;
            if (m_order == 0) return;

            // Test directions: golden-angle spiral for uniform sphere coverage.
            const int                    num_dirs = std::max(50, 4 * (2 * m_order + 1));
            std::vector<Eigen::Vector3f> test_dirs;
            test_dirs.reserve(static_cast<size_t>(num_dirs));
            for (int i = 0; i < num_dirs; ++i) {
                float t  = (static_cast<float>(i) + 0.5f) / static_cast<float>(num_dirs);
                float el = std::asin(2.0f * t - 1.0f);
                float az = static_cast<float>(i) * 2.3999632f; // golden angle
                test_dirs.push_back({std::cos(el) * std::cos(az),
                                     std::cos(el) * std::sin(az),
                                     std::sin(el)});
            }

            float sh_orig[max_channel_count];
            float sh_rot[max_channel_count];

            for (int l = 1; l <= m_order; ++l) {
                const int  block_size = 2 * l + 1;
                const auto B          = static_cast<Eigen::Index>(block_size);
                const auto D          = static_cast<Eigen::Index>(num_dirs);

                Eigen::MatrixXf Y_orig(D, B);
                Eigen::MatrixXf Y_rot(D, B);

                for (int i = 0; i < num_dirs; ++i) {
                    const auto& d  = test_dirs[static_cast<size_t>(i)];
                    float       az = std::atan2(d.y(), d.x());
                    float       el = std::atan2(d.z(), std::sqrt(d.x() * d.x() + d.y() * d.y()));
                    evaluate_sh(l, az, el, sh_orig);

                    Eigen::Vector3f rd  = R * d;
                    float           raz = std::atan2(rd.y(), rd.x());
                    float rel = std::atan2(rd.z(), std::sqrt(rd.x() * rd.x() + rd.y() * rd.y()));
                    evaluate_sh(l, raz, rel, sh_rot);

                    for (int j = 0; j < block_size; ++j) {
                        size_t acn                                  = acn_index(l, j - l);
                        Y_orig(i, static_cast<Eigen::Index>(j))     = sh_orig[acn];
                        Y_rot(i, static_cast<Eigen::Index>(j))      = sh_rot[acn];
                    }
                }

                // Least squares: Y_rot = Y_orig * R_l^T
                Eigen::MatrixXf R_l_T = (Y_orig.transpose() * Y_orig)
                                            .ldlt()
                                            .solve(Y_orig.transpose() * Y_rot);
                Eigen::MatrixXf R_l = R_l_T.transpose();

                for (int m = -l; m <= l; ++m) {
                    for (int mp = -l; mp <= l; ++mp) {
                        auto row          = static_cast<Eigen::Index>(acn_index(l, m));
                        auto col          = static_cast<Eigen::Index>(acn_index(l, mp));
                        m_matrix(row, col) = R_l(m + l, mp + l);
                    }
                }
            }
        }
    };

} // namespace ambitap

#endif // AMBITAP_MATH_ROTATION_H
