/// AmbiTap: target-independent ambisonics library
/// Triangulated speaker layout with VBAP gain computation and distance compensation.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_SPEAKER_LAYOUT_H
#define AMBITAP_MATH_SPEAKER_LAYOUT_H

#include "../core/coords.h"
#include "convex_hull.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace ambitap {

    /// Convert spherical coordinates to a Cartesian unit vector.
    inline Eigen::Vector3f spherical_to_cartesian(float azimuth, float elevation) {
        float cos_el = std::cos(elevation);
        return {cos_el * std::cos(azimuth),
                cos_el * std::sin(azimuth),
                std::sin(elevation)};
    }

    inline Eigen::Vector3f spherical_to_cartesian(spherical_coord c) {
        return spherical_to_cartesian(c.azimuth, c.elevation);
    }

    /// Triangulated speaker layout on the unit sphere. Provides VBAP gain computation
    /// for arbitrary source directions.
    ///
    /// Three panning modes, chosen automatically at construction:
    ///   - 3D VBAP over the convex-hull triangulation, when the layout spans 3D.
    ///   - 2D pairwise VBAP for planar layouts (stereo, quad, 5.1, rings, …):
    ///     speakers are ordered by angle in their common plane and a source pans
    ///     between the adjacent pair bracketing its direction.
    ///   - Nearest-speaker, for layouts too degenerate for either (e.g. one
    ///     speaker, or coincident positions).
    class speaker_layout {
        std::vector<spherical_coord> m_speakers;
        std::vector<Eigen::Vector3f> m_cart;
        std::vector<triangle>        m_triangles;
        std::vector<Eigen::Matrix3f> m_inv_matrices;

        // 2D pairwise mode (planar layouts): plane basis, per-speaker in-plane
        // angle, and speaker indices sorted by that angle.
        bool                         m_pairwise {false};
        Eigen::Vector3f              m_plane_u {Eigen::Vector3f::UnitX()};
        Eigen::Vector3f              m_plane_v {Eigen::Vector3f::UnitY()};
        std::vector<float>           m_ring_angle; // parallel to m_ring_index
        std::vector<size_t>          m_ring_index;

      public:
        /// @throws std::invalid_argument when speakers is empty.
        explicit speaker_layout(const std::vector<spherical_coord>& speakers)
            : m_speakers(speakers) {
            if (speakers.empty()) {
                throw std::invalid_argument("ambitap::speaker_layout: empty speaker list");
            }

            m_cart.reserve(speakers.size());
            for (const auto& s : speakers) {
                m_cart.push_back(spherical_to_cartesian(s));
            }

            m_triangles = convex_hull_3d(m_cart);

            if (!m_triangles.empty()) {
                m_inv_matrices.reserve(m_triangles.size());
                for (const auto& tri : m_triangles) {
                    Eigen::Matrix3f M;
                    M.col(0) = m_cart[tri.a];
                    M.col(1) = m_cart[tri.b];
                    M.col(2) = m_cart[tri.c];
                    m_inv_matrices.push_back(M.inverse());
                }
            }
            else {
                setup_pairwise(); // planar or < 4 speakers; may leave nearest-only
            }
        }

        size_t                              num_speakers() const { return m_speakers.size(); }
        size_t                              num_triangles() const { return m_triangles.size(); }
        const std::vector<spherical_coord>& speakers() const { return m_speakers; }
        const std::vector<Eigen::Vector3f>& cartesian() const { return m_cart; }
        const std::vector<triangle>&        triangles() const { return m_triangles; }

        /// True when the layout is planar and panning is 2D pairwise VBAP.
        bool is_pairwise() const { return m_pairwise; }

        /// VBAP gains for all speakers given a source direction.
        /// Returns a vector of length num_speakers() with non-negative gains, energy-
        /// normalized (sum of squares == 1).
        ///
        /// Reference: Pulkki, V. (1997). "Virtual Sound Source Positioning Using Vector
        /// Base Amplitude Panning."
        std::vector<float> vbap_gains(spherical_coord direction) const {
            std::vector<float> gains(m_speakers.size(), 0.0f);
            Eigen::Vector3f    d = spherical_to_cartesian(direction);

            // Enclosing triangle: barycentric coordinates all >= 0.
            for (size_t ti = 0; ti < m_triangles.size(); ++ti) {
                Eigen::Vector3f g = m_inv_matrices[ti] * d;

                if (g(0) >= -1e-6f && g(1) >= -1e-6f && g(2) >= -1e-6f) {
                    g = g.cwiseMax(0.0f);

                    float norm = g.norm();
                    if (norm > 1e-10f) {
                        g /= norm;
                    }

                    gains[m_triangles[ti].a] = g(0);
                    gains[m_triangles[ti].b] = g(1);
                    gains[m_triangles[ti].c] = g(2);
                    return gains;
                }
            }

            if (m_pairwise && pairwise_gains(d, gains)) {
                return gains;
            }

            // Fallback: nearest speaker by dot product.
            float  max_dot = -2.0f;
            size_t nearest = 0;
            for (size_t i = 0; i < m_cart.size(); ++i) {
                float dot = d.dot(m_cart[i]);
                if (dot > max_dot) {
                    max_dot = dot;
                    nearest = i;
                }
            }
            gains[nearest] = 1.0f;
            return gains;
        }

        /// Delay (seconds) and gain adjustments to align each speaker to the farthest one.
        struct compensation {
            std::vector<float> delays;
            std::vector<float> gains;
        };

        static compensation distance_compensation(const std::vector<float>& distances,
                                                  float                     speed_of_sound = 343.0f) {
            compensation comp;
            const size_t n = distances.size();
            comp.delays.resize(n);
            comp.gains.resize(n);

            float max_dist = *std::max_element(distances.begin(), distances.end());

            for (size_t i = 0; i < n; ++i) {
                comp.delays[i] = (max_dist - distances[i]) / speed_of_sound;
                comp.gains[i]  = distances[i] / max_dist;
            }
            return comp;
        }

      private:
        /// Configure 2D pairwise panning for planar layouts (no 3D hull).
        ///
        /// The common plane is found from the principal directions of the
        /// speaker vectors: for >= 3 speakers, the two dominant axes of the
        /// centered point cloud (which for an elevated ring is the ring plane);
        /// for 2 speakers, the plane they span with the origin. If even that is
        /// degenerate (single speaker, coincident pair), pairwise mode stays
        /// off and vbap_gains falls back to nearest-speaker.
        void setup_pairwise() {
            const size_t n = m_cart.size();
            if (n < 2) return;

            Eigen::MatrixXf P(3, static_cast<Eigen::Index>(n));
            for (size_t i = 0; i < n; ++i) P.col(static_cast<Eigen::Index>(i)) = m_cart[i];
            if (n >= 3) {
                const Eigen::Vector3f mean = P.rowwise().mean();
                P.colwise() -= mean;
            }

            Eigen::JacobiSVD<Eigen::MatrixXf> svd(P, Eigen::ComputeThinU);
            if (svd.singularValues()(1) <= 1e-5f * svd.singularValues()(0)
                || svd.singularValues()(0) <= 1e-6f) {
                return; // rank < 2: no plane to pan in
            }
            m_plane_u = svd.matrixU().col(0);
            m_plane_v = svd.matrixU().col(1);

            m_ring_index.resize(n);
            std::iota(m_ring_index.begin(), m_ring_index.end(), size_t {0});
            std::vector<float> angles(n);
            for (size_t i = 0; i < n; ++i) {
                angles[i] = std::atan2(m_cart[i].dot(m_plane_v), m_cart[i].dot(m_plane_u));
            }
            std::sort(m_ring_index.begin(), m_ring_index.end(),
                      [&](size_t a, size_t b) { return angles[a] < angles[b]; });
            m_ring_angle.resize(n);
            for (size_t i = 0; i < n; ++i) m_ring_angle[i] = angles[m_ring_index[i]];

            m_pairwise = true;
        }

        /// 2D pairwise VBAP: project the source onto the layout plane, pick the
        /// adjacent speaker pair bracketing its in-plane angle, and solve the
        /// 2x2 gain system (Pulkki 1997, 2D case). Elevated sources over a
        /// horizontal ring pan by azimuth. Returns false when the source has no
        /// usable in-plane component (e.g. the zenith over a horizontal ring),
        /// leaving the nearest-speaker fallback to decide.
        bool pairwise_gains(const Eigen::Vector3f& d, std::vector<float>& gains) const {
            const float du = d.dot(m_plane_u);
            const float dv = d.dot(m_plane_v);
            if (du * du + dv * dv < 1e-10f) return false;

            const float  theta = std::atan2(dv, du);
            const size_t n     = m_ring_index.size();

            // Find the sorted pair (i, i+1) bracketing theta; the wrap-around
            // pair (n-1, 0) covers the remaining arc through +/-pi.
            size_t hi = 0;
            while (hi < n && m_ring_angle[hi] < theta) ++hi;
            const size_t lo = (hi == 0 || hi == n) ? n - 1 : hi - 1;
            if (hi == n) hi = 0;

            const size_t a = m_ring_index[lo];
            const size_t b = m_ring_index[hi];

            Eigen::Matrix2f M;
            M(0, 0) = m_cart[a].dot(m_plane_u);
            M(1, 0) = m_cart[a].dot(m_plane_v);
            M(0, 1) = m_cart[b].dot(m_plane_u);
            M(1, 1) = m_cart[b].dot(m_plane_v);
            if (std::abs(M.determinant()) < 1e-8f) return false;

            Eigen::Vector2f g = M.inverse() * Eigen::Vector2f(du, dv);
            g                 = g.cwiseMax(0.0f); // outside the arc: clamp, keep direction
            const float norm  = g.norm();
            if (norm < 1e-10f) return false;
            g /= norm;

            gains[a] = g(0);
            gains[b] = g(1);
            return true;
        }
    };

} // namespace ambitap

#endif // AMBITAP_MATH_SPEAKER_LAYOUT_H
