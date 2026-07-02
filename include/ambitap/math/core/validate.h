/// AmbiTap: target-independent ambisonics library
/// Constructor-argument validation helpers.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_VALIDATE_H
#define AMBITAP_MATH_VALIDATE_H

#include "indexing.h"

#include <stdexcept>
#include <string>

namespace ambitap {

    /// Validate an ambisonics order against an allowed range, returning it
    /// unchanged so the call can sit in a constructor's member-init list.
    ///
    /// @throws std::invalid_argument when order is outside [lowest, highest].
    inline int validated_order(int order, int lowest, int highest, const char* context) {
        if (order < lowest || order > highest) {
            throw std::invalid_argument(std::string("ambitap::") + context + ": order "
                                        + std::to_string(order) + " outside supported range ["
                                        + std::to_string(lowest) + ", " + std::to_string(highest)
                                        + "]");
        }
        return order;
    }

    /// Overload with the library-wide default range [0, max_order].
    inline int validated_order(int order, const char* context) {
        return validated_order(order, 0, max_order, context);
    }

} // namespace ambitap

#endif // AMBITAP_MATH_VALIDATE_H
