/// AmbiTap: target-independent ambisonics library
/// Fast float math for per-sample audio paths: polynomial log2/exp2 and the
/// dB conversions built on them.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_MATH_FAST_MATH_H
#define AMBITAP_MATH_FAST_MATH_H

#include <bit>
#include <cmath>
#include <cstdint>

namespace ambitap {

    /// log2(x) for positive finite x: exponent extraction plus a degree-6
    /// Chebyshev fit of log2(m) on the mantissa interval [1, 2).
    ///
    /// Maximum absolute error ~6e-6 (in log2 units, i.e. ~3.6e-5 dB) —
    /// measured in float32 arithmetic against a double reference. Inputs
    /// below the smallest normal float (including 0 and denormals) are
    /// clamped to it; the callers in the audio paths clamp harder anyway.
    ///
    /// Why this exists: std::log10/std::pow per sample are software-library
    /// calls costing hundreds of cycles on embedded FPUs (Cortex-M55,
    /// Hexagon). This is a handful of MACs and stays on the FPU everywhere.
    inline float fast_log2(float x) {
        constexpr float k_min_normal = 1.17549435e-38f;
        if (x < k_min_normal) x = k_min_normal;

        const auto  bits = std::bit_cast<std::uint32_t>(x);
        const float e    = static_cast<float>(static_cast<int>(bits >> 23) - 127);
        const float m    = std::bit_cast<float>((bits & 0x007fffffu) | 0x3f800000u);

        // Degree-6 Chebyshev fit of log2(m), m in [1, 2), Horner form.
        float p = -2.4825984e-02f;
        p       = p * m + 2.6686277e-01f;
        p       = p * m - 1.2342799e+00f;
        p       = p * m + 3.2188698e+00f;
        p       = p * m - 5.2641555e+00f;
        p       = p * m + 6.0658589e+00f;
        p       = p * m - 3.0283250e+00f;
        return e + p;
    }

    /// 2^x for x in roughly [-125, 127]: integer/fraction split with a
    /// degree-6 Chebyshev fit of 2^f on [0, 1], scaled by an exponent-field
    /// bit shift. Maximum relative error ~1e-7 (float32-measured). Inputs
    /// are clamped to the representable exponent range.
    inline float fast_exp2(float x) {
        if (x < -125.f) x = -125.f;
        if (x > 127.f) x = 127.f;

        const float xi = std::floor(x);
        const float f  = x - xi; // [0, 1)

        // Degree-6 Chebyshev fit of 2^f, f in [0, 1], Horner form.
        float p = 2.1871262e-04f;
        p       = p * f + 1.2382416e-03f;
        p       = p * f + 9.6861863e-03f;
        p       = p * f + 5.5478912e-02f;
        p       = p * f + 2.4023110e-01f;
        p       = p * f + 6.9314684e-01f;
        p       = p * f + 1.0000000e+00f;

        const auto scale =
            std::bit_cast<float>(static_cast<std::uint32_t>(static_cast<int>(xi) + 127) << 23);
        return p * scale;
    }

    /// 20 * log10(x): dB of a linear amplitude. 20/log2(10) = 6.0205999...
    inline float fast_db_from_linear(float linear) {
        return 6.0205999f * fast_log2(linear);
    }

    /// 10^(db/20): linear amplitude of a dB value. log2(10)/20 = 0.16609640...
    inline float fast_linear_from_db(float db) {
        return fast_exp2(db * 0.16609640f);
    }

} // namespace ambitap

#endif // AMBITAP_MATH_FAST_MATH_H
