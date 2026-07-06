/// @file decoder.h
/// @brief HOA decoder: HOA bus -> loudspeaker layout, with async matrix rebuild.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "../math/core/coords.h"
#include "../math/core/indexing.h"
#include "../math/core/validate.h"
#include "../math/decoding/allrad.h"
#include "../math/decoding/epad.h"
#include "../math/decoding/mode_matching.h"
#include "util/async_rebuilder.h"
#include "util/matrix_applier.h"

namespace ambitap::dsp {

    /// Decoder construction algorithm.
    enum class decoder_algorithm {
        mode_match, ///< SVD-based pseudoinverse (Moore-Penrose)
        allrad,     ///< T-design virtual rig + VBAP panning
        epad,       ///< energy-preserving (truncated polar factor)
    };

    /// Decode a higher-order ambisonics signal to a loudspeaker layout.
    ///
    /// The decode matrix (speakers × channels, row-major; speaker_signals =
    /// D * hoa) is constructed on a worker thread — SVD / T-design + VBAP are
    /// not realtime-safe — and published wait-free (rt_published: the audio
    /// thread never locks, allocates, or frees). Until the first build
    /// completes, process() emits silence. Exactly one audio thread may call
    /// the process methods.
    ///
    /// Click suppression: a newly adopted matrix crossfades in over
    /// k_fade_samples against the previous one when the dimensions match;
    /// when the speaker count changes it fades in from silence.
    ///
    /// Setting an empty speaker list is ignored (the previous matrix is kept).
    class decoder {
      public:
        /// Crossfade length applied when a new decode matrix is adopted.
        static constexpr size_t k_fade_samples = matrix_applier::k_fade_samples;

        /// Published product: the matrix, its dimensions, and the matrix to
        /// fade from (zeros after a dimension change or before the first build).
        struct matrix {
            size_t             speakers;
            size_t             channels;
            std::vector<float> data; ///< row-major (speakers × channels)
            std::vector<float> prev; ///< same shape as data
        };

      private:
        int    m_order;
        size_t m_in_channels;

        // Configuration snapshot, guarded by m_config_mtx (speaker vectors
        // cannot be atomics). Read by the worker thread's build callback.
        mutable std::mutex           m_config_mtx;
        std::vector<spherical_coord> m_speakers;
        decoder_algorithm            m_algorithm{decoder_algorithm::mode_match};
        bool                         m_max_re{false};

        // Crossfading application; owned by the (single) audio thread.
        mutable matrix_applier m_applier;

        // Declared after everything the build callback reads (see async_rebuilder).
        async_rebuilder<const matrix> m_rebuilder;

      public:
        /// @param order       Ambisonics order in [1, k_max_order].
        /// @param on_publish  Optional callback run on the worker thread after
        ///                    each successful matrix publish (e.g. UI emission).
        /// @throws std::invalid_argument on out-of-range order.
        explicit decoder(int order, std::function<void()> on_publish = {})
            : m_order(validated_order(order, "dsp::decoder"))
            , m_in_channels(channel_count(order))
            , m_rebuilder([this] { return build(); }, std::move(on_publish)) {}

        int    order() const { return m_order; }
        size_t input_channels() const { return m_in_channels; }

        /// Set the loudspeaker directions. Empty lists are ignored at build time.
        void set_speakers(std::vector<spherical_coord> speakers) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_speakers = std::move(speakers);
            }
            m_rebuilder.submit();
        }

        void set_algorithm(decoder_algorithm algorithm) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_algorithm = algorithm;
            }
            m_rebuilder.submit();
        }

        void set_max_re(bool enabled) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_max_re = enabled;
            }
            m_rebuilder.submit();
        }

        decoder_algorithm algorithm() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_algorithm;
        }

        bool max_re() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_max_re;
        }

        /// Latest published decode matrix, or nullptr before the first build.
        /// Any thread EXCEPT the real-time path (UI/tests); the audio path
        /// uses process()/process_frame().
        std::shared_ptr<const matrix> load_matrix() const { return m_rebuilder.peek(); }

        /// Block until pending rebuilds have finished (tests / offline use).
        void wait_for_settling() { m_rebuilder.wait_for_settling(); }

        /// Decode one frame: in has input_channels() samples, out has one sample
        /// per speaker. Silence until the first matrix is published. Audio
        /// thread only; wait-free.
        void process_frame(const float* in, float* out, size_t out_channels) const noexcept {
            auto guard = m_rebuilder.read_lock();
            apply_frames(guard.get(), &in, &out, out_channels, 1, true);
        }

        /// Decode a block of planar buffers: in = input_channels() pointers,
        /// out = out_channels pointers. Silence until the first matrix. Audio
        /// thread only; wait-free.
        void process(const float* const* in, float* const* out, size_t out_channels,
                     size_t frame_count) const noexcept {
            auto guard = m_rebuilder.read_lock();
            apply_frames(guard.get(), in, out, out_channels, frame_count, false);
        }

      private:
        // frame_layout == true: in/out are single-frame channel arrays
        // (in[0][c]); false: planar buffers (in[c][i]).
        void apply_frames(const matrix* m, const float* const* in, float* const* out, size_t out_channels,
                          size_t frame_count, bool frame_layout) const noexcept {
            if (!m || m->speakers != out_channels) {
                if (frame_layout) {
                    for (size_t ch = 0; ch < out_channels; ++ch) {
                        out[0][ch] = 0.f;
                    }
                }
                else {
                    for (size_t ch = 0; ch < out_channels; ++ch) {
                        for (size_t i = 0; i < frame_count; ++i) {
                            out[ch][i] = 0.f;
                        }
                    }
                }
                return;
            }

            m_applier.apply(m, m->data.data(), m->prev.data(), m->speakers, m->channels, in, out, frame_count,
                            frame_layout);
        }

        std::shared_ptr<const matrix> build() const {
            std::vector<spherical_coord> speakers;
            decoder_algorithm            algorithm;
            bool                         mre;
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                speakers  = m_speakers;
                algorithm = m_algorithm;
                mre       = m_max_re;
            }
            if (speakers.empty()) {
                return nullptr; // keep previous matrix
            }

            Eigen::MatrixXf D;
            switch (algorithm) {
            case decoder_algorithm::allrad:
                D = compute_allrad_decoder(m_order, speakers, mre);
                break;
            case decoder_algorithm::epad:
                D = compute_epad_decoder(m_order, speakers, mre);
                break;
            case decoder_algorithm::mode_match:
                D = compute_mode_matching_decoder(m_order, speakers, mre);
                break;
            }

            auto fresh      = std::make_shared<matrix>();
            fresh->speakers = speakers.size();
            fresh->channels = m_in_channels;
            fresh->data.resize(fresh->speakers * fresh->channels);
            for (size_t r = 0; r < fresh->speakers; ++r) {
                for (size_t c = 0; c < fresh->channels; ++c) {
                    fresh->data[r * fresh->channels + c] =
                        D(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c));
                }
            }

            // Fade-from matrix: the previous build when shapes match, else
            // zeros (fade in from silence — also covers the first build).
            const auto last = m_rebuilder.peek(); // worker thread; safe to lock
            if (last && last->speakers == fresh->speakers && last->channels == fresh->channels) {
                fresh->prev = last->data;
            }
            else {
                fresh->prev.assign(fresh->data.size(), 0.f);
            }
            return fresh;
        }
    };

} // namespace ambitap::dsp
