/// @file rotator.h
/// @brief HOA rotator: Euler-angle SH rotation with async matrix rebuild.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "../math/core/indexing.h"
#include "../math/core/rotation.h"
#include "../math/core/validate.h"
#include "util/async_rebuilder.h"
#include "util/sh_block_applier.h"

namespace tap::ambi::dsp {

    /// Rotate a higher-order ambisonics signal by Euler angles (yaw, pitch, roll).
    ///
    /// Euler convention follows math/core/rotation.h:
    ///   yaw   — rotation about the +Z (up) axis, applied first
    ///   pitch — rotation about the +Y (left) axis, applied second
    ///   roll  — rotation about the +X (front) axis, applied last
    ///
    /// Threading model: angle setters submit a job to a worker thread that runs
    /// the sh_rotation solve; the resulting row-major (C × C) matrix is
    /// published wait-free (rt_published — the audio thread never locks,
    /// allocates, or frees; the worker does all freeing). Until the first
    /// setter fires, is_active() is false and process() is a passthrough.
    /// Exactly one audio thread may call the process methods.
    ///
    /// Click suppression: each newly adopted matrix is crossfaded in over
    /// k_fade_samples against the previous one (the identity for the first),
    /// so continuous head-tracking updates do not zipper.
    class rotator {
      public:
        /// Crossfade length applied when a new rotation matrix is adopted.
        static constexpr size_t k_fade_samples = sh_block_applier::k_fade_samples;

        /// Published product: the new matrix plus the matrix to fade from.
        struct product {
            std::vector<float> m;    ///< row-major (C × C)
            std::vector<float> prev; ///< same shape; identity before any build
        };

      private:
        int    m_order;
        size_t m_channels;

        std::atomic<float> m_yaw{0.0f};
        std::atomic<float> m_pitch{0.0f};
        std::atomic<float> m_roll{0.0f};

        // Crossfading block-diagonal application (SH rotation never mixes
        // orders); owned by the (single) audio thread.
        mutable sh_block_applier m_applier;

        // Worker-only: copy of the last matrix this worker built, used as the
        // fade-from matrix of the next product.
        mutable std::vector<float> m_worker_prev;

        // Declared after everything the build callback reads (see async_rebuilder).
        async_rebuilder<const product> m_rebuilder;

      public:
        /// @param order  Ambisonics order in [0, k_max_order]. Order 0 is a passthrough.
        /// @throws std::invalid_argument on out-of-range order.
        explicit rotator(int order)
            : m_order(validated_order(order, "dsp::rotator"))
            , m_channels(channel_count(order))
            , m_rebuilder([this] { return build(); }) {}

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        void set_yaw(float radians) {
            m_yaw.store(radians);
            m_rebuilder.submit();
        }
        void set_pitch(float radians) {
            m_pitch.store(radians);
            m_rebuilder.submit();
        }
        void set_roll(float radians) {
            m_roll.store(radians);
            m_rebuilder.submit();
        }

        /// Set all three angles with a single rebuild.
        void set_rotation(float yaw_radians, float pitch_radians, float roll_radians) {
            m_yaw.store(yaw_radians);
            m_pitch.store(pitch_radians);
            m_roll.store(roll_radians);
            m_rebuilder.submit();
        }

        float yaw() const { return m_yaw.load(); }
        float pitch() const { return m_pitch.load(); }
        float roll() const { return m_roll.load(); }

        /// True once a rotation matrix has been published (i.e. any setter has
        /// fired and its rebuild finished). False = identity passthrough.
        bool is_active() const { return m_rebuilder.has_value(); }

        /// Latest row-major (channels × channels) rotation matrix, or nullptr
        /// while is_active() is false. Any thread EXCEPT the real-time path
        /// (UI/tests); the audio path uses process()/process_frame().
        std::shared_ptr<const std::vector<float>> matrix() const {
            auto p = m_rebuilder.peek();
            if (!p) {
                return nullptr;
            }
            return {p, &p->m}; // aliasing: shares ownership, points at the matrix
        }

        /// Block until pending rebuilds have finished (tests / offline use).
        void wait_for_settling() { m_rebuilder.wait_for_settling(); }

        /// Rotate one frame of channels() samples. Output must not alias input.
        /// Audio thread only; wait-free.
        void process_frame(const float* in, float* out) const noexcept {
            auto guard = m_rebuilder.read_lock();
            apply_frames(guard.get(), &in, &out, 1, true);
        }

        /// Rotate a block of planar channel buffers. Output must not alias
        /// input. Audio thread only; wait-free.
        void process(const float* const* in, float* const* out, size_t frame_count) const noexcept {
            auto guard = m_rebuilder.read_lock();
            apply_frames(guard.get(), in, out, frame_count, false);
        }

      private:
        // frame_layout == true: in/out are single-frame channel arrays
        // (in[0][ch]); false: planar buffers (in[ch][i]).
        void apply_frames(const product* p, const float* const* in, float* const* out, size_t frame_count,
                          bool frame_layout) const noexcept {
            if (!p) {
                if (frame_layout) {
                    for (size_t ch = 0; ch < m_channels; ++ch) {
                        out[0][ch] = in[0][ch];
                    }
                }
                else {
                    for (size_t ch = 0; ch < m_channels; ++ch) {
                        for (size_t i = 0; i < frame_count; ++i) {
                            out[ch][i] = in[ch][i];
                        }
                    }
                }
                return;
            }

            m_applier.apply(p, p->m.data(), p->prev.data(), m_order, in, out, frame_count, frame_layout);
        }

        std::shared_ptr<const product> build() const {
            sh_rotation rot(m_order, m_yaw.load(), m_pitch.load(), m_roll.load());
            const auto& m = rot.matrix();

            auto fresh = std::make_shared<product>();
            fresh->m.resize(m_channels * m_channels);
            for (size_t row = 0; row < m_channels; ++row) {
                for (size_t col = 0; col < m_channels; ++col) {
                    fresh->m[row * m_channels + col] =
                        m(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col));
                }
            }

            if (m_worker_prev.empty()) { // first build: fade in from identity
                m_worker_prev.assign(m_channels * m_channels, 0.f);
                for (size_t d = 0; d < m_channels; ++d) {
                    m_worker_prev[d * m_channels + d] = 1.f;
                }
            }
            fresh->prev   = m_worker_prev;
            m_worker_prev = fresh->m;
            return fresh;
        }
    };

} // namespace tap::ambi::dsp
