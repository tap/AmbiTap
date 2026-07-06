/// AmbiTap: target-independent ambisonics library
/// ACN (Ambisonic Channel Number) indexing and channel count utilities.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_INDEXING_H
#define AMBITAP_MATH_INDEXING_H

#include <cstddef>

namespace ambitap {

    /// Number of ambisonics channels for a given order: (N+1)^2.
    /// @param order  Ambisonics order (0 = omnidirectional, 1 = first-order, etc.)
    /// @return Total channel count.
    constexpr size_t channel_count(int order) {
        return static_cast<size_t>((order + 1) * (order + 1));
    }

    /// Maximum supported ambisonics order.
    constexpr int k_max_order = 10;

    /// Maximum channel count (for order 10): 121.
    constexpr size_t k_max_channel_count = channel_count(k_max_order);

    /// Compute the ACN (Ambisonic Channel Number) index from order and degree.
    ///
    /// ACN = n^2 + n + m, where n is the order (>= 0) and m is the degree (-n <= m <= n).
    constexpr size_t acn_index(int order, int degree) {
        return static_cast<size_t>(order * order + order + degree);
    }

    /// Extract the spherical harmonic order from an ACN index.
    /// @return Order n such that n^2 <= acn < (n+1)^2.
    constexpr int acn_order(size_t acn) {
        int n = 0;
        while (static_cast<size_t>((n + 1) * (n + 1)) <= acn)
            ++n;
        return n;
    }

    /// Extract the spherical harmonic degree from an ACN index.
    /// @return Degree m where -n <= m <= n.
    constexpr int acn_degree(size_t acn) {
        int n = acn_order(acn);
        return static_cast<int>(acn) - n * n - n;
    }

    /// Alias for acn_order — returns the ambisonics order for a given ACN channel.
    constexpr int order_of(size_t acn) {
        return acn_order(acn);
    }

} // namespace ambitap

#endif // AMBITAP_MATH_INDEXING_H
