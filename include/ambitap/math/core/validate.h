/// AmbiTap: target-independent ambisonics library
/// Constructor-argument validation helpers.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_VALIDATE_H
#define AMBITAP_MATH_VALIDATE_H

#include "indexing.h"

// The embedded real-time profile builds with -fno-exceptions (detected via
// __cpp_exceptions, or forced with AMBITAP_NO_EXCEPTIONS): validation then
// asserts in debug and clamps in release instead of throwing, and the
// <stdexcept>/<string> dependencies drop out entirely.
#if defined(AMBITAP_NO_EXCEPTIONS) || !defined(__cpp_exceptions)
#define AMBITAP_HAS_EXCEPTIONS 0
#include <algorithm>
#include <cassert>
#else
#define AMBITAP_HAS_EXCEPTIONS 1
#include <stdexcept>
#include <string>
#endif

namespace ambitap {

    /// Validate an ambisonics order against an allowed range, returning it
    /// unchanged so the call can sit in a constructor's member-init list.
    ///
    /// With exceptions enabled: @throws std::invalid_argument when order is
    /// outside [lowest, highest]. Without exceptions (embedded profile): an
    /// out-of-range order asserts in debug builds and is clamped into range
    /// in release builds.
    inline int validated_order(int order, int lowest, int highest, const char* context) {
        if (order < lowest || order > highest) {
#if AMBITAP_HAS_EXCEPTIONS
            throw std::invalid_argument(std::string("ambitap::") + context + ": order "
                                        + std::to_string(order) + " outside supported range ["
                                        + std::to_string(lowest) + ", " + std::to_string(highest)
                                        + "]");
#else
            assert(false && "ambitap: order outside supported range");
            (void)context;
            return std::clamp(order, lowest, highest);
#endif
        }
        return order;
    }

    /// Overload with the library-wide default range [0, max_order].
    inline int validated_order(int order, const char* context) {
        return validated_order(order, 0, max_order, context);
    }

} // namespace ambitap

#endif // AMBITAP_MATH_VALIDATE_H
