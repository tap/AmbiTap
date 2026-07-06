/// AmbiTap: target-independent ambisonics library
/// 3D convex hull computation for speaker layout triangulation.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_CONVEX_HULL_H
#define AMBITAP_MATH_CONVEX_HULL_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <Eigen/Dense>

namespace ambitap {

    /// Triangular face of a convex hull, storing indices into the point array.
    struct triangle {
        size_t a, b, c;
    };

    /// 3D convex hull of a set of points. Returns triangular faces with outward-facing
    /// normals. Incremental algorithm suitable for small point sets (< ~1000 points),
    /// which is typical for speaker layouts and T-designs.
    ///
    /// Degenerate inputs — fewer than four points, or point sets that are
    /// (affinely) coincident, collinear, or coplanar, such as a horizontal
    /// speaker ring — have no 3D hull and return an empty vector. Callers must
    /// check for this; speaker_layout falls back to 2D pairwise panning.
    ///
    /// Reference: de Berg et al., "Computational Geometry", Ch. 11.
    inline std::vector<triangle> convex_hull_3d(const std::vector<Eigen::Vector3f>& raw_points) {
        const size_t n = raw_points.size();
        if (n < 4) return {};

        // Degeneracy thresholds for the seed-tetrahedron searches below. Points
        // are speaker/T-design directions on (or near) the unit sphere, so an
        // absolute scale is meaningful.
        constexpr float k_degenerate_sq   = 1e-10f; // squared distances
        constexpr float k_degenerate_dist = 1e-5f;  // plane distance

        // Symbolic-perturbation guard for exactly coplanar faces (a cube's
        // square faces are the canonical case): four coplanar corners make the
        // incremental visibility test ambiguous, and either epsilon choice
        // mis-stitches SOME insertion order — strict visibility leaves a quad
        // face half-covered with the stitched triangles folded through the
        // hull interior. Deciding the TOPOLOGY on deterministically
        // radius-lifted copies removes every exact tie; the returned indices
        // are then used with the caller's original (unit) vectors, which VBAP
        // uses purely directionally. The lift (<= 1e-4 relative) bends
        // formerly-coplanar quad pairs by an equally negligible angle.
        std::vector<Eigen::Vector3f> points(n);
        for (size_t i = 0; i < n; ++i) {
            const auto  h    = static_cast<uint32_t>(i) * 2654435761u;
            const float lift = 1e-4f * (static_cast<float>(h % 1024u) + 1.f) / 1024.f;
            points[i]        = raw_points[i] * (1.f + lift);
        }

        // Planarity must still be judged on the ORIGINAL points — the lift
        // would otherwise turn a flat ring into a fake 3D shell.
        {
            Eigen::Vector3f mean = Eigen::Vector3f::Zero();
            for (const auto& p : raw_points)
                mean += p;
            mean /= static_cast<float>(n);
            Eigen::Matrix3f cov = Eigen::Matrix3f::Zero();
            for (const auto& p : raw_points) {
                const Eigen::Vector3f q = p - mean;
                cov += q * q.transpose();
            }
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> es(cov);
            if (es.eigenvalues()(0) <= k_degenerate_dist * k_degenerate_dist * static_cast<float>(n)) {
                return {}; // coincident, collinear, or coplanar input
            }
        }

        // Initial tetrahedron: pick four non-coplanar seed points.
        size_t p0 = 0, p1 = 1, p2 = 0, p3 = 0;

        float max_dist = 0.0f;
        for (size_t i = 1; i < n; ++i) {
            float d = (points[i] - points[p0]).squaredNorm();
            if (d > max_dist) {
                max_dist = d;
                p1       = i;
            }
        }
        if (max_dist <= k_degenerate_sq) return {}; // all points coincident

        Eigen::Vector3f dir01 = (points[p1] - points[p0]).normalized();
        max_dist              = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (i == p0 || i == p1) continue;
            Eigen::Vector3f v = points[i] - points[p0];
            float           d = (v - v.dot(dir01) * dir01).squaredNorm();
            if (d > max_dist) {
                max_dist = d;
                p2       = i;
            }
        }
        if (max_dist <= k_degenerate_sq) return {}; // all points collinear

        Eigen::Vector3f normal = (points[p1] - points[p0]).cross(points[p2] - points[p0]);
        normal.normalize();
        max_dist = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (i == p0 || i == p1 || i == p2) continue;
            float d = std::abs((points[i] - points[p0]).dot(normal));
            if (d > max_dist) {
                max_dist = d;
                p3       = i;
            }
        }
        if (max_dist <= k_degenerate_dist) return {}; // all points coplanar

        float sign = (points[p3] - points[p0]).dot(normal);

        struct face {
            size_t          v[3];
            Eigen::Vector3f normal;
            bool            alive = true;
        };

        std::vector<face> faces;
        auto              add_face = [&](size_t a, size_t b, size_t c) {
            face f;
            f.v[0]                   = a;
            f.v[1]                   = b;
            f.v[2]                   = c;
            const Eigen::Vector3f cr = (points[b] - points[a]).cross(points[c] - points[a]);
            // Collinear vertices produce a zero-area sliver (possible when a
            // horizon edge is collinear with the point being added on a
            // coplanar face); drop it instead of normalizing a zero vector.
            f.alive  = cr.squaredNorm() > 1e-12f;
            f.normal = f.alive ? cr.normalized() : Eigen::Vector3f::Zero();
            faces.push_back(f);
        };

        // Initial tetrahedron with consistent outward normals.
        if (sign > 0) {
            add_face(p0, p2, p1);
            add_face(p0, p1, p3);
            add_face(p1, p2, p3);
            add_face(p0, p3, p2);
        }
        else {
            add_face(p0, p1, p2);
            add_face(p0, p3, p1);
            add_face(p1, p3, p2);
            add_face(p0, p2, p3);
        }

        // Verify outward orientation against the tetrahedron centroid.
        Eigen::Vector3f centroid = (points[p0] + points[p1] + points[p2] + points[p3]) / 4.0f;
        for (auto& f : faces) {
            if ((centroid - points[f.v[0]]).dot(f.normal) > 0) {
                std::swap(f.v[1], f.v[2]);
                f.normal = -f.normal;
            }
        }

        std::vector<bool> used(n, false);
        used[p0] = used[p1] = used[p2] = used[p3] = true;

        struct edge {
            size_t a, b;
        };

        for (size_t i = 0; i < n; ++i) {
            if (used[i]) continue;

            const Eigen::Vector3f& pt = points[i];

            // A direction coincident with an existing hull vertex adds nothing
            // and would only create zero-area faces; skip it.
            bool duplicate = false;
            for (size_t j = 0; j < n && !duplicate; ++j) {
                if (used[j] && (pt - points[j]).squaredNorm() <= k_degenerate_sq) {
                    duplicate = true;
                }
            }
            if (duplicate) continue;

            // Pass 1: mark every live face visible from the new point. Visibility
            // must be decided for all faces before any face is removed — an edge
            // is on the horizon iff its neighbor across the edge is NOT visible,
            // and that test needs the neighbor's visibility regardless of the
            // order faces are processed in. The radius lift above guarantees no
            // point is ever exactly coplanar with a face, so this strict test
            // is unambiguous.
            std::vector<bool> visible(faces.size(), false);
            bool              any_visible = false;
            for (size_t fi = 0; fi < faces.size(); ++fi) {
                if (!faces[fi].alive) continue;
                float d = (pt - points[faces[fi].v[0]]).dot(faces[fi].normal);
                if (d > 1e-7f) {
                    visible[fi] = true;
                    any_visible = true;
                }
            }

            if (!any_visible) continue; // Point lies inside the current hull.
            used[i] = true;

            // Pass 2: collect horizon edges of the visible region.
            std::vector<edge> horizon;
            for (size_t fi = 0; fi < faces.size(); ++fi) {
                if (!visible[fi]) continue;
                for (int e = 0; e < 3; ++e) {
                    size_t ea = faces[fi].v[e];
                    size_t eb = faces[fi].v[(e + 1) % 3];

                    bool on_horizon = true;
                    for (size_t fj = 0; fj < faces.size(); ++fj) {
                        if (fj == fi || !faces[fj].alive) continue;
                        for (int e2 = 0; e2 < 3; ++e2) {
                            if (faces[fj].v[e2] == eb && faces[fj].v[(e2 + 1) % 3] == ea) {
                                if (visible[fj]) {
                                    on_horizon = false;
                                }
                                goto edge_done;
                            }
                        }
                    }
                edge_done:
                    if (on_horizon) {
                        horizon.push_back({ea, eb});
                    }
                }
            }

            // Pass 3: remove the visible faces, then stitch the horizon to the point.
            for (size_t fi = 0; fi < faces.size(); ++fi) {
                if (visible[fi]) faces[fi].alive = false;
            }

            for (const auto& e : horizon) {
                add_face(e.a, e.b, i);
                // The new face should point away from origin (points lie on a sphere).
                face&           f           = faces.back();
                Eigen::Vector3f face_center = (points[f.v[0]] + points[f.v[1]] + points[f.v[2]]) / 3.0f;
                if (face_center.dot(f.normal) < 0) {
                    std::swap(f.v[1], f.v[2]);
                    f.normal = -f.normal;
                }
            }
        }

        std::vector<triangle> result;
        for (const auto& f : faces) {
            if (f.alive) {
                result.push_back({f.v[0], f.v[1], f.v[2]});
            }
        }
        return result;
    }

} // namespace ambitap

#endif // AMBITAP_MATH_CONVEX_HULL_H
