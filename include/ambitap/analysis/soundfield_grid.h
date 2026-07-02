/// AmbiTap: target-independent ambisonics library
/// Per-direction soundfield energy analysis on an equirectangular grid.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_ANALYSIS_SOUNDFIELD_GRID_H
#define AMBITAP_ANALYSIS_SOUNDFIELD_GRID_H

#include "../dsp/util/rt_published.h"
#include "../math/core/indexing.h"
#include "../math/core/validate.h"
#include "../math/core/spherical_harmonics.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace ambitap::analysis {

    /// Project a HOA bus onto a dense equirectangular grid of directions and
    /// accumulate smoothed per-direction energy — the data behind a "where is
    /// the soundfield concentrating" heatmap.
    ///
    /// Per direction d, the soundfield value (the "virtual microphone signal")
    /// is the SH dot product
    ///     v_d[t] = sum_c Y_c(az_d, el_d) * hoa_c[t]
    /// and the per-block energy is its mean square — computed as the quadratic
    /// form Y_d^T R Y_d in the per-block channel covariance R, which is exact
    /// and ~N/C times cheaper than the naive per-direction, per-sample dot
    /// product. A one-pole smoother averages block energies over
    /// smoothing_time_ms before display.
    ///
    /// Grid layout: azimuth_steps columns x (azimuth_steps / 2) rows on an
    /// equirectangular projection. Row 0 = +pi/2 (zenith), bottom row = -pi/2;
    /// column 0 = -pi (back-left wrap), centre column = 0 (front). Edge-sampled
    /// so cardinal angles land on grid cells whenever azimuth_steps divides 4.
    ///
    /// Threading: process() is real-time safe (wait-free; never locks,
    /// allocates, or frees) and runs on exactly one audio thread.
    /// set_azimuth_steps() rebuilds the SH table synchronously on the calling
    /// (control) thread and publishes it wait-free for the audio thread; it
    /// may block for roughly one audio callback while the old table is
    /// retired. snapshot() may be called from any non-real-time thread.
    class soundfield_grid {
      public:
        /// Display-ready snapshot: row-major rows x cols values in [0, 1],
        /// normalized over a dynamic range below the current peak.
        struct image {
            int                rows {0};
            int                cols {0};
            std::vector<float> data;
            float              peak_db {0.f}; ///< absolute peak, for annotation
        };

      private:
        struct grid {
            int                             az_steps;
            int                             el_steps; // == az_steps / 2
            int                             directions;
            std::vector<float> Y; ///< (D x C) row-major SH table
            /// Length D, smoothed. Written by the audio thread through the
            /// published-const product, hence mutable.
            mutable std::vector<std::atomic<float>> energy;

            grid(int az, size_t channels)
                : az_steps(az)
                , el_steps(az / 2)
                , directions(az * (az / 2))
                , Y(static_cast<size_t>(directions) * channels, 0.f)
                , energy(static_cast<size_t>(directions)) {
                for (auto& e : energy) e.store(0.f, std::memory_order_relaxed);
            }
        };

        int    m_order;
        size_t m_channels;
        std::atomic<float> m_fs {48000.f};
        std::atomic<float> m_smoothing_ms {200.f};

        dsp::rt_published<const grid> m_grid; // wait-free for the audio thread

        // Per-block C x C covariance scratch. Heap-allocated member (not an
        // audio-thread stack array) — order 10 is 121 x 121 floats (~58 KB).
        std::vector<float> m_cov;

      public:
        /// @param order          Ambisonics order in [0, max_order].
        /// @param azimuth_steps  Initial grid azimuth resolution (columns).
        /// @throws std::invalid_argument on out-of-range order.
        explicit soundfield_grid(int order, int azimuth_steps = 32)
            : m_order(validated_order(order, "analysis::soundfield_grid"))
            , m_channels(channel_count(order))
            , m_cov(channel_count(order) * channel_count(order), 0.f) {
            set_azimuth_steps(azimuth_steps);
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        /// Set the sample rate (drives the smoothing coefficient).
        void prepare(float sample_rate) { m_fs.store(sample_rate, std::memory_order_relaxed); }

        /// One-pole smoothing time constant for per-direction energy, in ms.
        void set_smoothing_time_ms(float ms) {
            m_smoothing_ms.store(ms, std::memory_order_relaxed);
        }
        float smoothing_time_ms() const {
            return m_smoothing_ms.load(std::memory_order_relaxed);
        }

        /// Rebuild the SH table at a new resolution (elevation resolution is
        /// azimuth_steps / 2) and publish atomically. Resets energies. Cheap
        /// enough to run synchronously on a control thread.
        void set_azimuth_steps(int azimuth_steps) {
            auto fresh = std::make_shared<grid>(azimuth_steps, m_channels);
            for (int row = 0; row < fresh->el_steps; ++row) {
                const float el = elevation_of_row(row, fresh->el_steps);
                for (int col = 0; col < fresh->az_steps; ++col) {
                    const float az = azimuth_of_column(col, fresh->az_steps);
                    const int   d  = row * fresh->az_steps + col;
                    evaluate_sh(m_order, az, el,
                                fresh->Y.data() + static_cast<size_t>(d) * m_channels);
                }
            }
            m_grid.publish(std::move(fresh));
        }

        int azimuth_steps() const {
            auto g = m_grid.peek();
            return g ? g->az_steps : 0;
        }
        int elevation_steps() const {
            auto g = m_grid.peek();
            return g ? g->el_steps : 0;
        }

        /// Accumulate one block of planar HOA channel buffers into the smoothed
        /// per-direction energies. Real-time safe.
        void process(const float* const* in, size_t frame_count) noexcept {
            auto        guard = m_grid.read_lock();
            const grid* g     = guard.get();
            if (!g || g->directions == 0 || frame_count == 0) return;

            // One-pole alpha per block: alpha_block = 1 - exp(-dt / tau).
            const float fs     = m_fs.load(std::memory_order_relaxed);
            const float dt_ms  = (static_cast<float>(frame_count) / fs) * 1000.f;
            const float tau_ms = std::max(m_smoothing_ms.load(std::memory_order_relaxed), 0.01f);
            const float alpha  = 1.f - std::exp(-dt_ms / tau_ms);
            const float inv_n  = 1.f / static_cast<float>(frame_count);

            // Per-block channel covariance R (symmetric).
            float* R = m_cov.data();
            for (size_t c = 0; c < m_channels; ++c) {
                for (size_t c2 = c; c2 < m_channels; ++c2) {
                    float acc = 0.f;
                    for (size_t i = 0; i < frame_count; ++i) acc += in[c][i] * in[c2][i];
                    acc *= inv_n;
                    R[c * m_channels + c2] = acc;
                    R[c2 * m_channels + c] = acc;
                }
            }

            // Per-direction energy as the quadratic form Y_d^T R Y_d.
            const float* Y = g->Y.data();
            for (int d = 0; d < g->directions; ++d) {
                const float* Yd           = Y + static_cast<size_t>(d) * m_channels;
                float        block_energy = 0.f;
                for (size_t c = 0; c < m_channels; ++c) {
                    float ry = 0.f;
                    for (size_t c2 = 0; c2 < m_channels; ++c2) {
                        ry += R[c * m_channels + c2] * Yd[c2];
                    }
                    block_energy += Yd[c] * ry;
                }
                const auto   idx  = static_cast<size_t>(d);
                const float  prev = g->energy[idx].load(std::memory_order_relaxed);
                const float  next = prev + alpha * (block_energy - prev);
                g->energy[idx].store(next, std::memory_order_relaxed);
            }
        }

        /// Snapshot the current energies, convert to dB, and normalize to
        /// [0, 1] over `dynamic_range_db` below the peak. Any thread.
        image snapshot(float dynamic_range_db) const {
            image out;
            auto  g = m_grid.peek();
            if (!g || g->directions == 0) return out;

            out.rows = g->el_steps;
            out.cols = g->az_steps;
            out.data.resize(static_cast<size_t>(g->directions));

            std::vector<float> db(static_cast<size_t>(g->directions));
            float              peak_db = -1e9f;
            for (int d = 0; d < g->directions; ++d) {
                const auto  idx = static_cast<size_t>(d);
                const float e   = g->energy[idx].load(std::memory_order_relaxed);
                db[idx]         = 10.f * std::log10(std::max(e, 1e-12f));
                if (db[idx] > peak_db) peak_db = db[idx];
            }
            out.peak_db = peak_db;

            for (size_t d = 0; d < out.data.size(); ++d) {
                float n = (db[d] - peak_db + dynamic_range_db) / dynamic_range_db;
                if (n < 0.f) n = 0.f;
                if (n > 1.f) n = 1.f;
                out.data[d] = n;
            }
            return out;
        }

        /// Direction of a grid cell. Edge-sampled: column 0 = -pi (back wrap),
        /// row 0 = +pi/2 (zenith).
        static float azimuth_of_column(int col, int az_steps) {
            return -static_cast<float>(M_PI)
                 + static_cast<float>(col) * static_cast<float>(2.0 * M_PI)
                       / static_cast<float>(az_steps);
        }
        static float elevation_of_row(int row, int el_steps) {
            return static_cast<float>(M_PI / 2.0)
                 - static_cast<float>(row) * static_cast<float>(M_PI)
                       / static_cast<float>(el_steps);
        }
    };

} // namespace ambitap::analysis

#endif // AMBITAP_ANALYSIS_SOUNDFIELD_GRID_H
