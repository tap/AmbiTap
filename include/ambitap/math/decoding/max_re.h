/// AmbiTap: target-independent ambisonics library
/// max-rE weighting coefficients for ambisonics decoders.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_MAX_RE_H
#define AMBITAP_MATH_MAX_RE_H

#include <cmath>
#include <cstddef>
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

    /// max-rE weights scaled to preserve decoded energy.
    ///
    /// The raw weights a_n only attenuate (a_0 = 1, decreasing), so switching
    /// max-rE on drops loudness by ~3 dB at order 1 and more at higher orders.
    /// This variant multiplies all weights by
    ///     alpha = sqrt( sum_n (2n+1) / sum_n (2n+1) a_n^2 )
    /// so that a decoded point source has the same total energy as with the
    /// basic (all-ones) weighting on an energy-preserving layout.
    ///
    /// Reference: Zotter & Frank (2012), sec. on decoder weighting normalization.
    inline std::vector<float> max_re_weights_energy_normalized(int order) {
        auto  weights = max_re_weights(order);
        float num     = 0.f;
        float den     = 0.f;
        for (int n = 0; n <= order; ++n) {
            const float g = static_cast<float>(2 * n + 1);
            num += g;
            den += g * weights[static_cast<size_t>(n)] * weights[static_cast<size_t>(n)];
        }
        const float alpha = std::sqrt(num / den);
        for (auto& w : weights) w *= alpha;
        return weights;
    }

} // namespace ambitap

#endif // AMBITAP_MATH_MAX_RE_H
