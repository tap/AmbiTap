/// AmbiTap: target-independent ambisonics library
/// max-rE weighting coefficients for ambisonics decoders.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_MAX_RE_H
#define AMBITAP_MATH_MAX_RE_H

#include <cmath>
#include <vector>

namespace ambitap {

    /// Per-order max-rE weights for a decoder of the given ambisonics order.
    ///
    /// Optimizes the energy vector magnitude, reducing side lobes when decoding
    /// to loudspeakers. Returns weights[n] for each order n in [0, order].
    ///
    ///   a_n = P_n(cos(137.9 deg / (N + 1.51)))
    /// where P_n is the Legendre polynomial of degree n.
    ///
    /// Reference: Zotter & Frank (2012), "All-Round Ambisonic Panning and Decoding".
    inline std::vector<float> max_re_weights(int order) {
        const float theta = (137.9f * static_cast<float>(M_PI) / 180.0f)
                            / (static_cast<float>(order) + 1.51f);
        const float x = std::cos(theta);

        std::vector<float> weights(order + 1);

        // Legendre polynomials via recurrence: P_0(x)=1, P_1(x)=x,
        // (n+1) P_{n+1}(x) = (2n+1) x P_n(x) - n P_{n-1}(x)
        float p_prev = 1.0f; // P_0
        float p_curr = x;    // P_1
        weights[0]   = p_prev;
        if (order >= 1) {
            weights[1] = p_curr;
        }

        for (int n = 1; n < order; ++n) {
            float p_next = (static_cast<float>(2 * n + 1) * x * p_curr
                            - static_cast<float>(n) * p_prev)
                           / static_cast<float>(n + 1);
            p_prev         = p_curr;
            p_curr         = p_next;
            weights[n + 1] = p_next;
        }

        return weights;
    }

} // namespace ambitap

#endif // AMBITAP_MATH_MAX_RE_H
