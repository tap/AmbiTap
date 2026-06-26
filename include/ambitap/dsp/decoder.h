/// AmbiTap: target-independent ambisonics library
/// HOA decoder: HOA bus -> loudspeaker layout, with async matrix rebuild.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_DECODER_H
#define AMBITAP_DSP_DECODER_H

#include "../math/core/coords.h"
#include "../math/core/indexing.h"
#include "../math/decoding/allrad.h"
#include "../math/decoding/epad.h"
#include "../math/decoding/mode_matching.h"
#include "util/async_rebuilder.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace ambitap::dsp {

    /// Decoder construction algorithm.
    enum class decoder_algorithm {
        mode_match, ///< SVD-based pseudoinverse (Moore-Penrose)
        allrad,     ///< T-design virtual rig + VBAP panning
        epad,       ///< energy-preserving (D = U V^T from SVD of Y)
    };

    /// Decode a higher-order ambisonics signal to a loudspeaker layout.
    ///
    /// The decode matrix (speakers × channels, row-major; speaker_signals =
    /// D * hoa) is constructed on a worker thread — SVD / T-design + VBAP are
    /// not realtime-safe — and published via an atomic shared_ptr swap. Until
    /// the first build completes, process() emits silence. The audio path is
    /// wait-free.
    ///
    /// Setting an empty speaker list is ignored (the previous matrix is kept).
    class decoder {
      public:
        /// Published product: the matrix plus its dimensions.
        struct matrix {
            size_t             speakers;
            size_t             channels;
            std::vector<float> data; ///< row-major (speakers × channels)
        };

      private:
        int    m_order;
        size_t m_in_channels;

        // Configuration snapshot, guarded by m_config_mtx (speaker vectors
        // cannot be atomics). Read by the worker thread's build callback.
        mutable std::mutex           m_config_mtx;
        std::vector<spherical_coord> m_speakers;
        decoder_algorithm            m_algorithm {decoder_algorithm::mode_match};
        bool                         m_max_re {false};

        // Declared after everything the build callback reads (see async_rebuilder).
        async_rebuilder<const matrix> m_rebuilder;

      public:
        /// @param order       Ambisonics order in [1, max_order].
        /// @param on_publish  Optional callback run on the worker thread after
        ///                    each successful matrix publish (e.g. UI emission).
        explicit decoder(int order, std::function<void()> on_publish = {})
            : m_order(order)
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
        /// Wait-free; safe from the audio thread.
        std::shared_ptr<const matrix> load_matrix() const { return m_rebuilder.load(); }

        /// Block until pending rebuilds have finished (tests / offline use).
        void wait_for_settling() { m_rebuilder.wait_for_settling(); }

        /// Decode one frame: in has input_channels() samples, out has one sample
        /// per speaker. Silence until the first matrix is published.
        void process_frame(const float* in, float* out, size_t out_channels) const {
            auto m = load_matrix();
            if (!m || m->speakers != out_channels) {
                for (size_t ch = 0; ch < out_channels; ++ch) out[ch] = 0.f;
                return;
            }
            for (size_t s = 0; s < m->speakers; ++s) {
                float acc = 0.f;
                for (size_t c = 0; c < m->channels; ++c) {
                    acc += m->data[s * m->channels + c] * in[c];
                }
                out[s] = acc;
            }
        }

        /// Decode a block of planar buffers: in = input_channels() pointers,
        /// out = out_channels pointers. Silence until the first matrix.
        void process(const float* const* in, float* const* out, size_t out_channels,
                     size_t frame_count) const {
            auto m = load_matrix();
            if (!m || m->speakers != out_channels) {
                for (size_t ch = 0; ch < out_channels; ++ch) {
                    for (size_t i = 0; i < frame_count; ++i) out[ch][i] = 0.f;
                }
                return;
            }
            for (size_t i = 0; i < frame_count; ++i) {
                for (size_t s = 0; s < m->speakers; ++s) {
                    float acc = 0.f;
                    for (size_t c = 0; c < m->channels; ++c) {
                        acc += m->data[s * m->channels + c] * in[c][i];
                    }
                    out[s][i] = acc;
                }
            }
        }

      private:
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
            if (speakers.empty()) return nullptr; // keep previous matrix

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
            return fresh;
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_DECODER_H
