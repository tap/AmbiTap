/// AmbiTap: target-independent ambisonics library
/// Triangulated speaker layout with VBAP gain computation and distance compensation.
/// Timothy Place
/// Copyright 2025 Timothy Place.

#ifndef AMBITAP_MATH_SPEAKER_LAYOUT_H
#define AMBITAP_MATH_SPEAKER_LAYOUT_H

#include "../core/coords.h"
#include "convex_hull.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
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
    class speaker_layout {
        std::vector<spherical_coord> m_speakers;
        std::vector<Eigen::Vector3f> m_cart;
        std::vector<triangle>        m_triangles;
        std::vector<Eigen::Matrix3f> m_inv_matrices;

      public:
        explicit speaker_layout(const std::vector<spherical_coord>& speakers)
            : m_speakers(speakers) {
            m_cart.reserve(speakers.size());
            for (const auto& s : speakers) {
                m_cart.push_back(spherical_to_cartesian(s));
            }

            m_triangles = convex_hull_3d(m_cart);

            m_inv_matrices.reserve(m_triangles.size());
            for (const auto& tri : m_triangles) {
                Eigen::Matrix3f M;
                M.col(0) = m_cart[tri.a];
                M.col(1) = m_cart[tri.b];
                M.col(2) = m_cart[tri.c];
                m_inv_matrices.push_back(M.inverse());
            }
        }

        size_t                              num_speakers() const { return m_speakers.size(); }
        size_t                              num_triangles() const { return m_triangles.size(); }
        const std::vector<spherical_coord>& speakers() const { return m_speakers; }
        const std::vector<Eigen::Vector3f>& cartesian() const { return m_cart; }
        const std::vector<triangle>&        triangles() const { return m_triangles; }

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
    };

} // namespace ambitap

#endif // AMBITAP_MATH_SPEAKER_LAYOUT_H
