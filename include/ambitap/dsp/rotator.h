/// AmbiTap: target-independent ambisonics library
/// HOA rotator: Euler-angle SH rotation with async matrix rebuild.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_ROTATOR_H
#define AMBITAP_DSP_ROTATOR_H

#include "../math/core/indexing.h"
#include "../math/core/rotation.h"
#include "util/async_rebuilder.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace ambitap::dsp {

    /// Rotate a higher-order ambisonics signal by Euler angles (yaw, pitch, roll).
    ///
    /// Euler convention follows math/core/rotation.h:
    ///   yaw   — rotation about the +Z (up) axis, applied first
    ///   pitch — rotation about the +Y (left) axis, applied second
    ///   roll  — rotation about the +X (front) axis, applied last
    ///
    /// Threading model: angle setters submit a job to a worker thread that runs
    /// the sh_rotation least-squares solve; the resulting row-major (C × C)
    /// matrix is published via an atomic shared_ptr swap. Until the first setter
    /// fires, is_active() is false and process() is a passthrough — callers can
    /// use that for a fast path. The audio path is wait-free.
    class rotator {
        int    m_order;
        size_t m_channels;

        std::atomic<float> m_yaw {0.0f};
        std::atomic<float> m_pitch {0.0f};
        std::atomic<float> m_roll {0.0f};

        // Declared after everything the build callback reads (see async_rebuilder).
        async_rebuilder<const std::vector<float>> m_rebuilder;

      public:
        /// @param order  Ambisonics order in [0, max_order]. Order 0 is a passthrough.
        explicit rotator(int order)
            : m_order(order)
            , m_channels(channel_count(order))
            , m_rebuilder([this] { return build(); }) {}

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        void set_yaw(float radians) { m_yaw.store(radians); m_rebuilder.submit(); }
        void set_pitch(float radians) { m_pitch.store(radians); m_rebuilder.submit(); }
        void set_roll(float radians) { m_roll.store(radians); m_rebuilder.submit(); }

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
        /// while is_active() is false. Wait-free.
        std::shared_ptr<const std::vector<float>> matrix() const { return m_rebuilder.load(); }

        /// Block until pending rebuilds have finished (tests / offline use).
        void wait_for_settling() { m_rebuilder.wait_for_settling(); }

        /// Rotate one frame of channels() samples. Output must not alias input.
        void process_frame(const float* in, float* out) const {
            auto m = matrix();
            if (!m) {
                for (size_t ch = 0; ch < m_channels; ++ch) out[ch] = in[ch];
                return;
            }
            const float* data = m->data();
            for (size_t out_ch = 0; out_ch < m_channels; ++out_ch) {
                float acc = 0.f;
                for (size_t in_ch = 0; in_ch < m_channels; ++in_ch) {
                    acc += data[out_ch * m_channels + in_ch] * in[in_ch];
                }
                out[out_ch] = acc;
            }
        }

        /// Rotate a block of planar channel buffers. Output must not alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) const {
            auto m = matrix();
            if (!m) {
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    for (size_t i = 0; i < frame_count; ++i) out[ch][i] = in[ch][i];
                }
                return;
            }
            const float* data = m->data();
            for (size_t i = 0; i < frame_count; ++i) {
                for (size_t out_ch = 0; out_ch < m_channels; ++out_ch) {
                    float acc = 0.f;
                    for (size_t in_ch = 0; in_ch < m_channels; ++in_ch) {
                        acc += data[out_ch * m_channels + in_ch] * in[in_ch][i];
                    }
                    out[out_ch][i] = acc;
                }
            }
        }

      private:
        std::shared_ptr<const std::vector<float>> build() const {
            sh_rotation rot(m_order, m_yaw.load(), m_pitch.load(), m_roll.load());
            const auto& m = rot.matrix();

            auto fresh = std::make_shared<std::vector<float>>(m_channels * m_channels);
            for (size_t row = 0; row < m_channels; ++row) {
                for (size_t col = 0; col < m_channels; ++col) {
                    (*fresh)[row * m_channels + col] = m(static_cast<Eigen::Index>(row),
                                                         static_cast<Eigen::Index>(col));
                }
            }
            return fresh;
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_ROTATOR_H
