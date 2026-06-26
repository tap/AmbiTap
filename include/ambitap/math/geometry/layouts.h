/// AmbiTap: target-independent ambisonics library
/// Standard speaker layout presets (stereo, 5.1, 7.1, 7.1.4, cube, etc.).
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_LAYOUTS_H
#define AMBITAP_MATH_LAYOUTS_H

#include "../core/coords.h"

#include <cmath>
#include <vector>

namespace ambitap {

    /// Standard speaker layout presets. Azimuth: 0 = front, pi/2 = left. Elevation: 0 = horizon.
    namespace layouts {

        /// Stereo: L (+30), R (-30).
        inline std::vector<spherical_coord> stereo() {
            constexpr float a = 30.0f * static_cast<float>(M_PI) / 180.0f;
            return {{a, 0.0f}, {-a, 0.0f}};
        }

        /// Quadraphonic: FL, BL, BR, FR.
        inline std::vector<spherical_coord> quad() {
            constexpr float a = 45.0f * static_cast<float>(M_PI) / 180.0f;
            return {{a, 0.0f},
                    {static_cast<float>(M_PI) - a, 0.0f},
                    {-(static_cast<float>(M_PI) - a), 0.0f},
                    {-a, 0.0f}};
        }

        /// ITU 5.1 (no LFE): L, R, C, LS, RS.
        inline std::vector<spherical_coord> surround_5_1() {
            constexpr float deg = static_cast<float>(M_PI) / 180.0f;
            return {
                { 30.0f * deg, 0.0f}, // L
                {-30.0f * deg, 0.0f}, // R
                {  0.0f,       0.0f}, // C
                { 110.0f * deg, 0.0f}, // LS
                {-110.0f * deg, 0.0f}, // RS
            };
        }

        /// ITU 7.1 (no LFE): L, R, C, LS, RS, LB, RB.
        inline std::vector<spherical_coord> surround_7_1() {
            constexpr float deg = static_cast<float>(M_PI) / 180.0f;
            return {
                {  30.0f * deg, 0.0f}, // L
                { -30.0f * deg, 0.0f}, // R
                {   0.0f,       0.0f}, // C
                {  90.0f * deg, 0.0f}, // LS
                { -90.0f * deg, 0.0f}, // RS
                { 135.0f * deg, 0.0f}, // LB
                {-135.0f * deg, 0.0f}, // RB
            };
        }

        /// 7.1.4 (Dolby Atmos bed, no LFE): 7.1 + 4 height speakers at 45 deg elevation.
        inline std::vector<spherical_coord> surround_7_1_4() {
            constexpr float deg = static_cast<float>(M_PI) / 180.0f;
            constexpr float h   = 45.0f * deg;
            return {
                // Ear level
                {  30.0f * deg, 0.0f}, // L
                { -30.0f * deg, 0.0f}, // R
                {   0.0f,       0.0f}, // C
                {  90.0f * deg, 0.0f}, // LS
                { -90.0f * deg, 0.0f}, // RS
                { 135.0f * deg, 0.0f}, // LB
                {-135.0f * deg, 0.0f}, // RB
                // Height
                {  45.0f * deg, h},    // TFL
                { -45.0f * deg, h},    // TFR
                { 135.0f * deg, h},    // TBL
                {-135.0f * deg, h},    // TBR
            };
        }

        /// Cube: 8 speakers at the corners of a cube.
        inline std::vector<spherical_coord> cube() {
            constexpr float deg = static_cast<float>(M_PI) / 180.0f;
            constexpr float az  = 45.0f * deg;
            constexpr float el  = 35.2644f * deg; // atan(1/sqrt(2))
            return {
                {az, el},
                {-az, el},
                {180.0f * deg - az, el},
                {-(180.0f * deg - az), el},
                {az, -el},
                {-az, -el},
                {180.0f * deg - az, -el},
                {-(180.0f * deg - az), -el},
            };
        }

        /// 6 speakers in the horizontal plane at 60-degree intervals.
        inline std::vector<spherical_coord> hexagon() {
            constexpr float deg = static_cast<float>(M_PI) / 180.0f;
            return {
                {   0.0f,       0.0f},
                {  60.0f * deg, 0.0f},
                { 120.0f * deg, 0.0f},
                { 180.0f * deg, 0.0f},
                {-120.0f * deg, 0.0f},
                { -60.0f * deg, 0.0f},
            };
        }

        /// 8 speakers in the horizontal plane at 45-degree intervals.
        inline std::vector<spherical_coord> octagon() {
            constexpr float deg = static_cast<float>(M_PI) / 180.0f;
            std::vector<spherical_coord> layout;
            for (int i = 0; i < 8; ++i) {
                layout.push_back({static_cast<float>(i) * 45.0f * deg, 0.0f});
            }
            return layout;
        }

    } // namespace layouts

} // namespace ambitap

#endif // AMBITAP_MATH_LAYOUTS_H
