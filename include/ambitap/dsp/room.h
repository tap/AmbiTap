/// AmbiTap: target-independent ambisonics library
/// Shoebox room simulation for an HOA bus: image-source early reflections
/// plus a 16-line SH-domain FDN late tail (mono in, (order+1)^2 SH out).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_ROOM_H
#define AMBITAP_DSP_ROOM_H

#include "../math/binaural/ooura_fft.h"
#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/core/validate.h"
#include "room_data.h"
#include "util/async_rebuilder.h"
#include "util/smoothing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace ambitap::dsp {

    /// Shoebox room reverberation in the spherical-harmonic domain: a mono
    /// source at a stated position in a stated room, rendered as direct sound,
    /// image-source early reflections, and a statistically diffuse late tail
    /// on an order-N ambisonics bus (AmbiX: ACN/SN3D, (order+1)^2 channels).
    ///
    /// This is the real-time realization of the offline prototype verified in
    /// notebooks/room_verification.ipynb against the R1-R10 gates of
    /// docs/PERCEPTUAL-VERIFICATION.md (notebooks/room_model.py documents the
    /// architecture iteration; the velvet-noise decorrelator variant evaluated
    /// there was REJECTED and is not implemented here). The composition
    /// mirrors the prototype exactly so the gate functions evaluate this
    /// object's rendered IR unchanged:
    ///
    ///   direct + early reflections (t < 30 ms) — Allen-Berkley shoebox image
    ///   sources, each one nearest-sample tap off a shared mono delay line
    ///   with per-channel gains amplitude * Y_acn(direction), where amplitude
    ///   is prod(wall reflection coefficients) / distance. Exactly checkable
    ///   against the closed-form image-source math (gates R1-R3).
    ///
    ///   late tail (t >= 30 ms) — 16 feedback delay lines with mutually-prime
    ///   lengths 431..3989 samples; a signed-Hadamard (orthogonal, lossless)
    ///   feedback matrix; per-line 255-tap linear-phase absorption FIRs inside
    ///   the loop, fitted per octave band to 10^(-3 (L_i + D) / (fs T60(f)))
    ///   with the FIR group delay D counted as loop length (gate R4); per-line
    ///   ~53 ms unit-energy noise input FIRs (Jot style), octave-band shaped
    ///   to the parameterized decay and injection-aligned so every line first
    ///   fires on the same sample; and a second independent signed-Hadamard
    ///   distributing the 16 line outputs onto the SH channels with SN3D
    ///   diffuse-field gains 1/sqrt(2n+1) per order (gate R7). The tail's omni
    ///   energy is calibrated so it continues the same image-source model that
    ///   produced the early reflections (no energy knee at the junction, gate
    ///   R6): the target is the closed-form image sum beyond the cutoff, and
    ///   the realized unscaled tail energy is measured by an offline
    ///   simulation of the very same FDN on the rebuild thread.
    ///
    /// Determinism (gate R10): the prototype draws its mixing-matrix signs and
    /// input-burst noise from numpy's PCG64 at the committed seed 11 — the
    /// seed is part of the parameterization. Reproducing numpy's PCG64 stream
    /// and ziggurat Gaussian sampler in C++ is not reasonable, so those raw
    /// draws are baked as generated data in room_data.h (the hrtf_data.h
    /// approach; regenerate with scripts/generate_room_data.py). Everything
    /// derived from them — octave-band splitting, decay envelopes,
    /// normalization, absorption-FIR fitting, calibration — is computed here
    /// from the runtime parameters, in a fixed arithmetic order, so identical
    /// parameters render byte-identical output.
    ///
    /// Latency: the injection alignment requires every line's input FIR to
    /// end (burst start + line delay) at the same instant, which puts the
    /// earliest injection max(3989, n0) - 3989 samples BEFORE the tail onset
    /// n0 = round(0.030 * fs). Rendering that causally costs a fixed
    /// latency_samples() = max(3989 - n0, 0) on every path (direct sound
    /// included) — about 53 ms at 48 kHz, 0 at rates >= 133 kHz. Hosts that
    /// mix this object's output with undelayed signals should compensate.
    ///
    /// Sample rates: the FDN delay set, burst length, and absorption FIR
    /// length are defined in SAMPLES at the verified 48 kHz reference (their
    /// physical times scale with the host rate), but the absorption fit, the
    /// burst band split/envelopes, and the image-source delays all use the
    /// actual sample rate — so RT60(f), the ER geometry, and the calibration
    /// stay correct at any rate; only the tail's modal density scales.
    ///
    /// Numerical care: unlike dsp::nfc (whose shelving poles sit within ~1e-4
    /// of the unit circle and need double recurrence state), the FDN loop is
    /// strongly contractive — the per-traversal gain never exceeds
    /// 10^(-3*558/(fs*T60max)) ≈ 0.92 at the shortest loop and the
    /// parameterized ceiling, and the feedback matrix is orthogonal — so
    /// float32 line state is sufficient: per-sample rounding is injected some
    /// 150 dB below signal and decays at the same T60 as the signal itself.
    /// All design math (FIR fitting, band splitting, calibration) runs in
    /// double on the rebuild thread; the injection convolution uses double
    /// internals like the desktop partitioned_convolver.
    ///
    /// Lifecycle: construct with the ambisonics order (0..3; 16 delay lines
    /// cover at most the 16 channels of order 3), then prepare() with the
    /// processing block size (power of two >= 4) and sample rate before audio
    /// starts. prepare() allocates the audio state and computes the burst
    /// band-split cache (a few hundred ms of one-time work), then submits the
    /// first model build.
    ///
    /// Threading: setters run on one control thread; they snapshot the
    /// configuration and wake a worker (async_rebuilder) that rebuilds the
    /// image-source tap set, the absorption/input FIRs, and the calibration
    /// (~0.2-0.4 s, coalesced across rapid changes) and publishes wait-free.
    /// The audio thread adopts a published model with a short crossfade of
    /// the ER taps and output gains; the FIR tables swap in place against the
    /// persistent recirculation state, so parameter changes are click-free.
    /// process() is wait-free (no allocation, locks, or syscalls) and emits
    /// silence until prepare() and the first build have completed. The
    /// direct/early/tail enables ramp on the audio thread without a rebuild.
    /// Call snap_parameters() for offline/exact rendering.
    class room {
      public:
        /// Highest supported ambisonics order: the 16 FDN lines feed at most
        /// the 16 SH channels of order 3 (the prototype-verified topology).
        static constexpr int k_max_room_order = 3;
        /// FDN delay-line count (also the Hadamard mixing size).
        static constexpr size_t k_lines = room_data_lines;
        /// Walls in +/-x, +/-y, +/-z order: (x0, x1, y0, y1, z0, z1).
        static constexpr size_t k_walls = 6;
        /// Parameterized RT60 octave bands.
        static constexpr size_t                           k_rt60_bands = 5;
        static constexpr std::array<double, k_rt60_bands> k_rt60_centers_hz {250.0, 500.0, 1000.0,
                                                                             2000.0, 4000.0};
        static constexpr float k_speed_of_sound = 343.0f; ///< m/s (prototype constant)
        /// Image sources render below this time; the FDN tail carries t >= it.
        static constexpr float k_er_cutoff_seconds = 0.030f;
        /// Per-line linear-phase absorption FIR length (group delay 127).
        static constexpr size_t k_absorption_taps = 255;
        /// Per-line input-burst FIR length (~53 ms at 48 kHz).
        static constexpr size_t k_burst_length = room_data_burst_length;
        /// Mutually-prime FDN delay lengths in samples (9..83 ms at 48 kHz;
        /// the short end bridges the early-recirculation gap — see the
        /// prototype's docstring for the delay-set iteration).
        static constexpr std::array<size_t, k_lines> k_delays {431,  541,  677,  839,  1039, 1201,
                                                               1451, 1693, 1949, 2243, 2531, 2857,
                                                               3163, 3467, 3697, 3989};

        static constexpr float k_min_dimension = 1.0f;  ///< m; bounds image counts
        static constexpr float k_max_dimension = 50.0f; ///< m
        static constexpr float k_min_rt60      = 0.1f;  ///< s
        static constexpr float k_max_rt60      = 10.0f; ///< s

      private:
        /// Octave bands of the input-burst shaping (31.25 Hz .. 16 kHz
        /// centers; first band reaches DC, last reaches Nyquist).
        static constexpr size_t k_noise_bands = 10;
        /// Internal processing chunk: the FDN recurrence is valid for chunks
        /// up to the shortest delay (431); 256 is the largest power of two
        /// under it and the injection-convolution partition size cap.
        static constexpr size_t k_max_chunk = 256;
        static_assert(k_max_chunk <= 431, "chunk must not exceed the shortest FDN delay");
        /// Crossfade applied when a rebuilt model is adopted (ER taps and
        /// output gains; the FIR tables swap against persistent state).
        static constexpr size_t k_fade_samples = 256;
        /// The tail-calibration simulation measures the unscaled tail's omni
        /// energy over the prototype's IR length.
        static constexpr float k_calibration_seconds = 2.0f;
        /// Tail-level target: the image sum is enumerated to this horizon and
        /// exponentially extrapolated beyond it (prototype tail_energy_target).
        static constexpr double k_target_enum_seconds = 0.25;
        static constexpr double k_pi                  = 3.14159265358979323846;

        /// One image-source arrival: a delay-line tap with per-channel gains.
        struct er_tap {
            size_t                     delay;  ///< input-ring lag (latency included)
            bool                       direct; ///< the zero-reflection arrival
            std::array<float, k_lines> gains;  ///< amplitude * Y_acn(direction)
        };

        /// Product built on the worker thread and published to the audio
        /// thread. `prev` fields carry the previously published values so the
        /// audio thread can crossfade without holding two products.
        struct model {
            std::vector<er_tap>        taps;
            std::vector<er_tap>        prev_taps;
            std::vector<float>         absorption;     ///< [line][k_absorption_taps]
            std::vector<float>         absorption_rev; ///< absorption, per-line reversed (audio path)
            std::vector<double>        inject_spectra; ///< [line][partition][fft], Ooura packing
            size_t                     partitions {0};
            size_t                     chunk {0};   ///< partition/block size the spectra assume
            std::array<float, k_lines> out_gain {}; ///< per-channel sn3d * calibration
            std::array<float, k_lines> prev_out_gain {};
        };

        // ---- Immutable per-instance configuration --------------------------
        int    m_order;
        size_t m_channels;
        /// Signed-Hadamard mixing matrices from the baked seed-11 sign draws:
        /// m_feedback[i][j] = rows[i] * H16[i][j] * cols[j] / 4 (orthogonal).
        std::array<std::array<float, k_lines>, k_lines> m_feedback {};
        std::array<std::array<float, k_lines>, k_lines> m_outmix {};
        std::array<float, k_lines>                      m_sn3d {};

        // ---- Configuration snapshot (guarded by m_config_mtx; read by the
        //      worker's build callback) --------------------------------------
        mutable std::mutex              m_config_mtx;
        std::array<float, 3>            m_dims {7.10f, 5.30f, 3.10f};
        std::array<float, 3>            m_source {3.674f, 1.137f, 1.977f};
        std::array<float, 3>            m_listener {1.746f, 1.711f, 0.668f};
        std::array<float, k_walls>      m_beta {0.90f, 0.92f, 0.91f, 0.93f, 0.89f, 0.94f};
        std::array<float, k_rt60_bands> m_rt60 {0.90f, 0.84f, 0.76f, 0.66f, 0.54f};
        float                           m_fs {0.0f};
        size_t                          m_block {0};
        /// Burst noise split into octave bands at the prepared sample rate
        /// ([line][band][sample]); RT60-independent, so computed once per
        /// prepare() and only re-shaped on rebuilds. Swapped as a shared_ptr
        /// so the worker snapshots it without copying.
        std::shared_ptr<const std::vector<float>> m_burst_bands;

        // ---- Audio-thread state (allocated in prepare(); owned by the one
        //      audio thread) -------------------------------------------------
        size_t m_n0 {0};          ///< tail onset, round(k_er_cutoff_seconds * fs)
        size_t m_base {0};        ///< injection alignment instant, max(3989, n0)
        size_t m_audio_block {0}; ///< prepared block size (audio-side copy)
        size_t m_chunk {0};       ///< min(block, k_max_chunk)
        size_t m_fft_size {0};    ///< 2 * m_chunk
        size_t m_partitions {0};

        std::vector<float> m_input_ring; ///< mono input history (power-of-two)
        size_t             m_input_mask {0};
        std::vector<float> m_line_rings; ///< k_lines * m_line_stride
        size_t             m_line_stride {0};
        size_t             m_line_mask {0};
        size_t             m_write {0};

        std::vector<real_fft> m_fft;       ///< exactly one; vector for deferred init
        std::vector<double>   m_conv_time; ///< [prev|curr] input, m_fft_size
        std::vector<double>   m_spec_ring; ///< [partition][m_fft_size] input spectra
        std::vector<double>   m_accum;     ///< m_fft_size
        std::vector<double>   m_conv_out;  ///< m_fft_size
        std::vector<float>    m_inject;    ///< [line][m_chunk]
        size_t                m_spec_pos {0};

        smoothed_scalar   m_direct_gain {1.0f};
        smoothed_scalar   m_early_gain {1.0f};
        smoothed_scalar   m_tail_gain {1.0f};
        std::atomic<bool> m_snap {false};
        const model*      m_last_model {nullptr};
        size_t            m_fade_pos {k_fade_samples};

        // Declared after everything the build callback reads (async_rebuilder
        // joins its worker before earlier members are destroyed).
        async_rebuilder<const model> m_rebuilder;

      public:
        /// @param order  Ambisonics order in [0, k_max_room_order]; channel
        ///               count is (order+1)^2.
        /// @throws std::invalid_argument on out-of-range order.
        explicit room(int order)
            : m_order(validated_order(order, 0, k_max_room_order, "dsp::room"))
            , m_channels(channel_count(m_order))
            , m_rebuilder([this] { return build(); }) {
            for (size_t i = 0; i < k_lines; ++i) {
                for (size_t j = 0; j < k_lines; ++j) {
                    // Sylvester Hadamard: H16[i][j] = (-1)^popcount(i & j).
                    const float h    = (std::popcount(i & j) % 2 != 0) ? -0.25f : 0.25f;
                    m_feedback[i][j] = room_feedback_sign_rows[i] * h * room_feedback_sign_cols[j];
                    m_outmix[i][j]   = room_output_sign_rows[i] * h * room_output_sign_cols[j];
                }
            }
            for (size_t ch = 0; ch < k_lines; ++ch) {
                m_sn3d[ch] = 1.0f / std::sqrt(2.0f * static_cast<float>(acn_order(ch)) + 1.0f);
            }
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        /// Build the audio-path state for the processing block size (power of
        /// two >= 4 — the injection convolution is partitioned FFT) and host
        /// sample rate, compute the burst band-split cache, and submit the
        /// first model build. An invalid block size leaves the object
        /// unprepared (process() emits silence). Control thread; call before
        /// audio starts. NOT RT-safe (allocates; a few hundred ms of work).
        void prepare(size_t block_size, float sample_rate) {
            const bool valid =
                block_size >= 4 && (block_size & (block_size - 1)) == 0 && sample_rate > 0.0f;
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_block = valid ? block_size : 0;
                if (!valid) {
                    m_fs          = sample_rate;
                    m_audio_block = 0;
                    m_chunk       = 0;
                    return;
                }
                if (m_fs != sample_rate || !m_burst_bands) {
                    m_fs          = sample_rate;
                    m_burst_bands = split_burst_bands(static_cast<double>(sample_rate));
                }
            }

            const double fs = static_cast<double>(sample_rate);
            m_n0 = static_cast<size_t>(std::llround(static_cast<double>(k_er_cutoff_seconds) * fs));
            m_base        = std::max(k_delays.back(), m_n0);
            m_audio_block = block_size;
            m_chunk       = std::min(block_size, k_max_chunk);
            m_fft_size    = 2 * m_chunk;
            m_partitions  = (inject_taps() + m_chunk - 1) / m_chunk;

            m_input_ring.assign(next_pow2(m_base + m_chunk + 1), 0.0f);
            m_input_mask  = m_input_ring.size() - 1;
            m_line_stride = next_pow2(k_delays.back() + k_absorption_taps + m_chunk + 1);
            m_line_mask   = m_line_stride - 1;
            m_line_rings.assign(k_lines * m_line_stride, 0.0f);

            m_fft.clear();
            m_fft.emplace_back(m_fft_size);
            m_conv_time.assign(m_fft_size, 0.0);
            m_spec_ring.assign(m_partitions * m_fft_size, 0.0);
            m_accum.assign(m_fft_size, 0.0);
            m_conv_out.assign(m_fft_size, 0.0);
            m_inject.assign(k_lines * m_chunk, 0.0f);
            m_spec_pos   = 0;
            m_write      = 0;
            m_last_model = nullptr;
            m_fade_pos   = k_fade_samples;

            m_rebuilder.submit();
        }

        bool   is_prepared() const { return block_size() != 0; }
        size_t block_size() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_block;
        }
        float sample_rate() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_fs;
        }

        /// Fixed processing latency on every path (direct sound included):
        /// max(3989 - round(0.030 * fs), 0) samples — the causality cost of
        /// the FDN's injection alignment (see the class comment). Valid after
        /// prepare().
        size_t latency_samples() const { return m_base - m_n0; }

        /// Inside dimensions of the shoebox in meters (x front, y left, z
        /// up), each clamped to [k_min_dimension, k_max_dimension]. Control
        /// thread; triggers an async rebuild.
        void set_room_dimensions(float x, float y, float z) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_dims = {clamp_dim(x), clamp_dim(y), clamp_dim(z)};
            }
            m_rebuilder.submit();
        }
        std::array<float, 3> room_dimensions() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_dims;
        }

        /// Source position in meters from the room's (0,0,0) corner. Not
        /// clamped into the room: the image-source math stays finite for any
        /// position, and clamping would fight dimension edits. Control
        /// thread; triggers an async rebuild.
        void set_source_position(float x, float y, float z) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_source = {x, y, z};
            }
            m_rebuilder.submit();
        }
        std::array<float, 3> source_position() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_source;
        }

        /// Listener position in meters from the room's (0,0,0) corner.
        /// Control thread; triggers an async rebuild.
        void set_listener_position(float x, float y, float z) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_listener = {x, y, z};
            }
            m_rebuilder.submit();
        }
        std::array<float, 3> listener_position() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_listener;
        }

        /// Per-wall amplitude reflection coefficients in (x0, x1, y0, y1, z0,
        /// z1) order, each clamped to [0, 1] (frequency-independent, the
        /// prototype's v1 model — they shape the ER levels and the tail's
        /// calibration target; RT60(f) is parameterized separately). Control
        /// thread; triggers an async rebuild.
        void set_wall_reflections(const std::array<float, k_walls>& coefficients) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                for (size_t w = 0; w < k_walls; ++w) {
                    m_beta[w] = std::clamp(coefficients[w], 0.0f, 1.0f);
                }
            }
            m_rebuilder.submit();
        }
        std::array<float, k_walls> wall_reflections() const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return m_beta;
        }

        /// RT60 of one parameterized octave band (k_rt60_centers_hz), seconds,
        /// clamped to [k_min_rt60, k_max_rt60]. Out-of-range band indices are
        /// ignored. Control thread; triggers an async rebuild.
        void set_rt60_band(size_t band, float seconds) {
            if (band >= k_rt60_bands) return;
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                m_rt60[band] = std::clamp(seconds, k_min_rt60, k_max_rt60);
            }
            m_rebuilder.submit();
        }
        float rt60_band(size_t band) const {
            std::lock_guard<std::mutex> lock(m_config_mtx);
            return band < k_rt60_bands ? m_rt60[band] : 0.0f;
        }

        /// Broadband RT60 control: scales every band so the 1 kHz band lands
        /// on `seconds`, preserving the current spectral tilt (each band still
        /// clamped to [k_min_rt60, k_max_rt60]). Control thread; triggers an
        /// async rebuild.
        void set_rt60(float seconds) {
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                const float                 target = std::clamp(seconds, k_min_rt60, k_max_rt60);
                const float                 ratio  = target / m_rt60[2]; // 1 kHz band
                for (auto& v : m_rt60) v = std::clamp(v * ratio, k_min_rt60, k_max_rt60);
            }
            m_rebuilder.submit();
        }
        /// The 1 kHz band's RT60 (what set_rt60() pins), seconds.
        float rt60() const { return rt60_band(2); }

        /// Enable/disable the direct-sound tap, the early reflections, and
        /// the late tail independently (each ramps over k_smoothing_samples
        /// on the audio thread — no rebuild). Control thread.
        void set_direct_enabled(bool enabled) { m_direct_gain.set(enabled ? 1.0f : 0.0f); }
        void set_early_enabled(bool enabled) { m_early_gain.set(enabled ? 1.0f : 0.0f); }
        void set_tail_enabled(bool enabled) { m_tail_gain.set(enabled ? 1.0f : 0.0f); }
        bool direct_enabled() const { return m_direct_gain.target() != 0.0f; }
        bool early_enabled() const { return m_early_gain.target() != 0.0f; }
        bool tail_enabled() const { return m_tail_gain.target() != 0.0f; }

        /// Block until pending rebuilds have been published (tests / offline).
        void wait_for_settling() { m_rebuilder.wait_for_settling(); }

        /// Skip the enable ramps and the next model-adoption crossfade: the
        /// audio thread jumps straight to the latest targets. Offline
        /// rendering / tests.
        void snap_parameters() {
            m_direct_gain.snap();
            m_early_gain.snap();
            m_tail_gain.snap();
            m_snap.store(true, std::memory_order_release);
        }

        /// Clear the audio history (input ring, FDN state, injection
        /// convolution); keep the published model and allocations.
        void reset() {
            std::fill(m_input_ring.begin(), m_input_ring.end(), 0.0f);
            std::fill(m_line_rings.begin(), m_line_rings.end(), 0.0f);
            std::fill(m_conv_time.begin(), m_conv_time.end(), 0.0);
            std::fill(m_spec_ring.begin(), m_spec_ring.end(), 0.0);
            std::fill(m_inject.begin(), m_inject.end(), 0.0f);
            m_spec_pos = 0;
            m_write    = 0;
        }

        /// Process one block: mono input -> channels() planar SH outputs,
        /// frame_count samples each (must equal the prepared block size;
        /// outputs are zeroed otherwise). Output may not alias the input.
        /// Silence until prepare() and the first model build have completed.
        /// Audio thread; wait-free.
        void process(const float* in, float* const* out, size_t frame_count) noexcept {
            for (size_t ch = 0; ch < m_channels; ++ch) {
                std::memset(out[ch], 0, frame_count * sizeof(float));
            }
            if (m_chunk == 0 || frame_count != m_audio_block) return;

            auto         guard = m_rebuilder.read_lock();
            const model* m     = guard.get();
            if (!m || m->chunk != m_chunk || m->partitions != m_partitions) return;

            if (m != m_last_model) {
                m_last_model = m;
                m_fade_pos   = 0;
            }
            if (m_snap.exchange(false, std::memory_order_acq_rel)) {
                m_fade_pos = k_fade_samples;
            }

            for (size_t offset = 0; offset < frame_count; offset += m_chunk) {
                process_chunk(m, in + offset, out, offset);
            }
        }

      private:
        static float clamp_dim(float v) { return std::clamp(v, k_min_dimension, k_max_dimension); }

        static size_t next_pow2(size_t n) {
            size_t p = 1;
            while (p < n) p <<= 1;
            return p;
        }

        /// Common injection-FIR length: alignment delay of the shortest line
        /// plus the burst (all lines' FIRs are zero-padded to it).
        size_t inject_taps() const { return m_base - k_delays.front() + k_burst_length; }

        /// sum_{k} a[k] b[k] with eight independent accumulators: a fixed,
        /// data-independent reduction tree (so the result is deterministic and
        /// block-size-independent) that the compiler can lift into SIMD lanes.
        static float dot_fir(const float* a, const float* b, size_t n) noexcept {
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
            size_t k = 0;
            for (; k + 8 <= n; k += 8) {
                a0 += a[k] * b[k];
                a1 += a[k + 1] * b[k + 1];
                a2 += a[k + 2] * b[k + 2];
                a3 += a[k + 3] * b[k + 3];
                a4 += a[k + 4] * b[k + 4];
                a5 += a[k + 5] * b[k + 5];
                a6 += a[k + 6] * b[k + 6];
                a7 += a[k + 7] * b[k + 7];
            }
            float acc = ((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7));
            for (; k < n; ++k)
                acc += a[k] * b[k];
            return acc;
        }

        // ---- Audio path ----------------------------------------------------

        void process_chunk(const model* m, const float* in, float* const* out,
                           size_t offset) noexcept {
            const size_t B = m_chunk;
            const size_t F = m_fft_size;

            // Mono input into the shared ER delay ring.
            for (size_t i = 0; i < B; ++i) {
                m_input_ring[(m_write + i) & m_input_mask] = in[i];
            }

            // Injection convolution: one forward FFT of the raw input chunk,
            // per-line frequency-domain MAC across partitions, one inverse
            // per line (shared-spectrum partitioned overlap-save; the FIR
            // spectra live in the published model, the input history here —
            // so a model swap keeps the history and stays click-free).
            for (size_t i = 0; i < B; ++i) m_conv_time[i] = m_conv_time[B + i];
            for (size_t i = 0; i < B; ++i) m_conv_time[B + i] = static_cast<double>(in[i]);
            double* slot = m_spec_ring.data() + m_spec_pos * F;
            std::copy(m_conv_time.begin(), m_conv_time.end(), slot);
            m_fft[0].forward_inplace(slot);
            const double inv_scale = 2.0 / static_cast<double>(F);
            for (size_t line = 0; line < k_lines; ++line) {
                std::fill(m_accum.begin(), m_accum.end(), 0.0);
                for (size_t p = 0; p < m_partitions; ++p) {
                    const size_t  s = (m_spec_pos + m_partitions - p) % m_partitions;
                    const double* X = m_spec_ring.data() + s * F;
                    const double* H = m->inject_spectra.data() + (line * m_partitions + p) * F;
                    m_accum[0] += X[0] * H[0]; // DC
                    m_accum[1] += X[1] * H[1]; // Nyquist
                    for (size_t k = 1; k < F / 2; ++k) {
                        const double xr = X[2 * k];
                        const double xi = X[2 * k + 1];
                        m_accum[2 * k] += xr * H[2 * k] - xi * H[2 * k + 1];
                        m_accum[2 * k + 1] += xr * H[2 * k + 1] + xi * H[2 * k];
                    }
                }
                std::copy(m_accum.begin(), m_accum.end(), m_conv_out.begin());
                m_fft[0].inverse_inplace(m_conv_out.data());
                float* inj = m_inject.data() + line * B;
                for (size_t i = 0; i < B; ++i) {
                    inj[i] = static_cast<float>(m_conv_out[B + i] * inv_scale);
                }
            }
            m_spec_pos = (m_spec_pos + 1) % m_partitions;

            // Per-sample: ER taps, then one FDN step.
            for (size_t i = 0; i < B; ++i) {
                const size_t w  = m_write + i;
                const float  dg = m_direct_gain.tick();
                const float  eg = m_early_gain.tick();
                const float  tg = m_tail_gain.tick();

                float fade_new = 1.0f;
                float fade_old = 0.0f;
                if (m_fade_pos < k_fade_samples) {
                    fade_new = (static_cast<float>(m_fade_pos) + 1.0f)
                               / static_cast<float>(k_fade_samples);
                    fade_old = 1.0f - fade_new;
                    ++m_fade_pos;
                }

                mix_er_taps(m->taps, w, i, offset, dg * fade_new, eg * fade_new, out);
                if (fade_old > 0.0f) {
                    mix_er_taps(m->prev_taps, w, i, offset, dg * fade_old, eg * fade_old, out);
                }

                // Absorption FIRs over each line's loop output o_j(t) =
                // x_j(t - d_j): read the line ring at lag d_j + k. The window
                // ring[lag0 .. lag0-254] is contiguous unless it straddles the
                // ring wrap; in the common contiguous case run it as an
                // ascending dot with the reversed coefficients (identical terms
                // to the masked descending form, but the fixed forward stride
                // and independent accumulators let the compiler vectorize —
                // this loop is ~70% of the object's cost). The rare wrapping
                // sample falls back to the exact masked form.
                float filtered[k_lines];
                for (size_t j = 0; j < k_lines; ++j) {
                    const float* ring = m_line_rings.data() + j * m_line_stride;
                    const size_t lag0 = w - k_delays[j];
                    const size_t p    = lag0 & m_line_mask;    // newest window index
                    if (p >= k_absorption_taps - 1) {
                        const float* hr   = m->absorption_rev.data() + j * k_absorption_taps;
                        const float* base = ring + (p - (k_absorption_taps - 1));
                        filtered[j]       = dot_fir(hr, base, k_absorption_taps);
                    }
                    else {
                        const float* h   = m->absorption.data() + j * k_absorption_taps;
                        float        acc = 0.0f;
                        for (size_t k = 0; k < k_absorption_taps; ++k) {
                            acc += h[k] * ring[(lag0 - k) & m_line_mask];
                        }
                        filtered[j] = acc;
                    }
                }

                // x_i(t) = inject_i(t) + sum_j feedback[i][j] filtered_j(t).
                float line_out[k_lines];
                for (size_t li = 0; li < k_lines; ++li) {
                    float acc = m_inject[li * B + i];
                    for (size_t j = 0; j < k_lines; ++j) {
                        acc += m_feedback[li][j] * filtered[j];
                    }
                    float* ring           = m_line_rings.data() + li * m_line_stride;
                    ring[w & m_line_mask] = acc;
                    line_out[li]          = ring[(w - k_delays[li]) & m_line_mask];
                }

                // Line outputs onto the SH bus through the output Hadamard,
                // scaled by the published sn3d * calibration gains.
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    float acc = 0.0f;
                    for (size_t j = 0; j < k_lines; ++j) {
                        acc += m_outmix[ch][j] * line_out[j];
                    }
                    const float g = fade_new * m->out_gain[ch] + fade_old * m->prev_out_gain[ch];
                    out[ch][offset + i] += tg * g * acc;
                }
            }
            m_write += B;
        }

        void mix_er_taps(const std::vector<er_tap>& taps, size_t w, size_t i, size_t offset,
                         float direct_gain, float early_gain, float* const* out) const noexcept {
            for (const auto& tap : taps) {
                const float v = m_input_ring[(w - tap.delay) & m_input_mask]
                                * (tap.direct ? direct_gain : early_gain);
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    out[ch][offset + i] += v * tap.gains[ch];
                }
            }
        }

        // ---- Shared parameterized-decay math (double precision; worker /
        //      control thread only) -----------------------------------------

        /// T60(f): log-frequency interpolation across the parameterized
        /// octave bands, extrapolated beyond the outer bands with the
        /// boundary octave slope, floored at k_min_rt60 (the prototype's
        /// rt60_of_freq — the extrapolation keeps each parameterized band
        /// center representative of its measured band average, gate R4).
        static double rt60_of_freq(double f, const std::array<double, k_rt60_bands>& rt60) {
            const double                     lf = std::log(std::max(f, 1.0));
            std::array<double, k_rt60_bands> lc {};
            for (size_t b = 0; b < k_rt60_bands; ++b) lc[b] = std::log(k_rt60_centers_hz[b]);
            double v;
            if (lf <= lc.front()) {
                const double slope = (rt60[1] - rt60[0]) / (lc[1] - lc[0]);
                v                  = rt60[0] + slope * (lf - lc[0]);
            }
            else if (lf >= lc.back()) {
                const size_t n     = k_rt60_bands;
                const double slope = (rt60[n - 1] - rt60[n - 2]) / (lc[n - 1] - lc[n - 2]);
                v                  = rt60[n - 1] + slope * (lf - lc[n - 1]);
            }
            else {
                size_t b = 0;
                while (lf > lc[b + 1]) ++b;
                const double t = (lf - lc[b]) / (lc[b + 1] - lc[b]);
                v              = rt60[b] + t * (rt60[b + 1] - rt60[b]);
            }
            return std::max(v, static_cast<double>(k_min_rt60));
        }

        /// Enumerate every shoebox image source arriving before t_max
        /// (Allen-Berkley: image = (1-2p)*src + 2rL per axis, amplitude
        /// prod(beta^exponent) / distance) and call
        /// fn(t_seconds, amplitude, unit_direction, reflection_count).
        template <typename Fn>
        static void for_each_image(const std::array<float, 3>&       dims,
                                   const std::array<float, 3>&       source,
                                   const std::array<float, 3>&       listener,
                                   const std::array<float, k_walls>& beta, double t_max, Fn&& fn) {
            const double L[3]  = {dims[0], dims[1], dims[2]};
            const double s[3]  = {source[0], source[1], source[2]};
            const double m[3]  = {listener[0], listener[1], listener[2]};
            const double d_max = t_max * static_cast<double>(k_speed_of_sound);
            int          n_range[3];
            for (size_t a = 0; a < 3; ++a) {
                n_range[a] = static_cast<int>(std::ceil(d_max / (2.0 * L[a]))) + 1;
            }
            for (int px = 0; px <= 1; ++px) {
                for (int py = 0; py <= 1; ++py) {
                    for (int pz = 0; pz <= 1; ++pz) {
                        const int p[3] = {px, py, pz};
                        for (int rx = -n_range[0]; rx <= n_range[0]; ++rx) {
                            for (int ry = -n_range[1]; ry <= n_range[1]; ++ry) {
                                for (int rz = -n_range[2]; rz <= n_range[2]; ++rz) {
                                    const int r[3] = {rx, ry, rz};
                                    double    v[3];
                                    for (size_t a = 0; a < 3; ++a) {
                                        v[a] = (1.0 - 2.0 * p[a]) * s[a] + 2.0 * r[a] * L[a] - m[a];
                                    }
                                    const double dist =
                                        std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                                    const double t = dist / static_cast<double>(k_speed_of_sound);
                                    if (t > t_max || dist < 1e-6) continue;
                                    int    refl = 0;
                                    double amp  = 1.0;
                                    for (size_t a = 0; a < 3; ++a) {
                                        const int e0 = std::abs(r[a] - p[a]);
                                        const int e1 = std::abs(r[a]);
                                        refl += e0 + e1;
                                        amp *= std::pow(static_cast<double>(beta[2 * a]), e0)
                                               * std::pow(static_cast<double>(beta[2 * a + 1]), e1);
                                    }
                                    amp /= dist;
                                    const double u[3] = {v[0] / dist, v[1] / dist, v[2] / dist};
                                    fn(t, amp, u, refl);
                                }
                            }
                        }
                    }
                }
            }
        }

        /// Omni (W) energy the tail must carry from the ER cutoff onward so
        /// it continues the same image-source model that produced the early
        /// reflections: the image sum enumerated to k_target_enum_seconds,
        /// plus the remainder extrapolated at the parameterized mid-band
        /// exponential rate (the prototype's tail_energy_target; feeds the
        /// R6 clarity contract).
        static double tail_energy_target(const std::array<float, 3>&             dims,
                                         const std::array<float, 3>&             source,
                                         const std::array<float, 3>&             listener,
                                         const std::array<float, k_walls>&       beta,
                                         const std::array<double, k_rt60_bands>& rt60) {
            const double t_enum = k_target_enum_seconds;
            const double t_fit  = t_enum - 0.05;
            const double t_cut  = static_cast<double>(k_er_cutoff_seconds);
            double       e_mid  = 0.0;
            double       e_fit  = 0.0;
            for_each_image(dims, source, listener, beta, t_enum,
                           [&](double t, double amp, const double*, int) {
                               const double e = amp * amp;
                               if (t >= t_cut && t < t_enum) e_mid += e;
                               if (t >= t_fit && t < t_enum) e_fit += e;
                           });
            const double rate = 13.8 / rt60_of_freq(1000.0, rt60);
            return e_mid + e_fit / (std::exp(rate * (t_enum - t_fit)) - 1.0);
        }

        // ---- Worker-side model construction ---------------------------------

        /// Split the baked seed-11 noise into brick-wall octave bands at the
        /// prepared sample rate: exact FFT masks, like the prototype (IIR
        /// octave skirts leak slower-decaying neighbors into faster bands and
        /// bias T20). The burst length (2560) is not a power of two, so this
        /// uses a direct table-driven real DFT — a few hundred ms of double
        /// math, control thread, once per prepare(). Returns
        /// [line][band][sample].
        static std::shared_ptr<const std::vector<float>> split_burst_bands(double fs) {
            constexpr size_t N    = k_burst_length;
            constexpr size_t half = N / 2;
            auto out = std::make_shared<std::vector<float>>(k_lines * k_noise_bands * N, 0.0f);

            std::vector<double> cs(N), sn(N);
            for (size_t i = 0; i < N; ++i) {
                const double a = 2.0 * k_pi * static_cast<double>(i) / static_cast<double>(N);
                cs[i]          = std::cos(a);
                sn[i]          = std::sin(a);
            }
            // Band of each rfft bin: half-open octaves around 31.25 * 2^b,
            // first reaching DC, last reaching Nyquist.
            std::vector<size_t> band_of(half + 1, k_noise_bands - 1);
            for (size_t k = 0; k <= half; ++k) {
                const double f = static_cast<double>(k) * fs / static_cast<double>(N);
                for (size_t b = 0; b < k_noise_bands; ++b) {
                    const double c  = 31.25 * std::pow(2.0, static_cast<double>(b));
                    const double lo = (b == 0) ? 0.0 : c / std::sqrt(2.0);
                    const bool   hi = (b == k_noise_bands - 1) || f < c * std::sqrt(2.0);
                    if (f >= lo && hi) {
                        band_of[k] = b;
                        break;
                    }
                }
            }

            std::vector<double> re(half + 1), im(half + 1), acc(N);
            for (size_t line = 0; line < k_lines; ++line) {
                const float* x = room_noise[line];
                for (size_t k = 0; k <= half; ++k) {
                    double sr = 0.0, si = 0.0;
                    size_t idx = 0;
                    for (size_t n = 0; n < N; ++n) {
                        const double v = static_cast<double>(x[n]);
                        sr += v * cs[idx];
                        si -= v * sn[idx];
                        idx += k;
                        if (idx >= N) idx -= N;
                    }
                    re[k] = sr;
                    im[k] = si;
                }
                for (size_t b = 0; b < k_noise_bands; ++b) {
                    std::fill(acc.begin(), acc.end(), 0.0);
                    for (size_t k = 0; k <= half; ++k) {
                        if (band_of[k] != b) continue;
                        const double weight = (k == 0 || k == half) ? 1.0 : 2.0;
                        const double wr     = weight * re[k];
                        const double wi     = weight * im[k];
                        size_t       idx    = 0;
                        for (size_t n = 0; n < N; ++n) {
                            acc[n] += wr * cs[idx] - wi * sn[idx];
                            idx += k;
                            if (idx >= N) idx -= N;
                        }
                    }
                    float* dst = out->data() + (line * k_noise_bands + b) * N;
                    for (size_t n = 0; n < N; ++n) {
                        dst[n] = static_cast<float>(acc[n] / static_cast<double>(N));
                    }
                }
            }
            return out;
        }

        /// Linear-phase absorption FIR realizing per-frequency loop gain
        /// 10^(-3 (loop_delay + 127) / (fs T60(f))): a faithful replica of
        /// scipy.signal.firwin2 (frequency sampling on a 257-point grid,
        /// modeling-delay phase, 512-point inverse rFFT, symmetric Hamming) so
        /// the realized decay matches the verified prototype's.
        static void design_absorption_fir(size_t loop_delay, double fs,
                                          const std::array<double, k_rt60_bands>& rt60,
                                          real_fft& fft512, float* taps_out) {
            constexpr size_t taps        = k_absorption_taps;
            constexpr size_t nfreqs      = 257; // 1 + 2^ceil(log2(255))
            constexpr size_t G           = 2 * (nfreqs - 1);
            constexpr size_t grid        = 384; // [0] + 383 geometrically spaced points
            const double     group_delay = static_cast<double>(taps - 1) / 2.0;
            const double     l_eff       = static_cast<double>(loop_delay) + group_delay;
            const double     nyq         = fs / 2.0;

            // Design grid: [0] + geomspace(20, nyq, 383), last snapped to nyq.
            std::array<double, grid> f {}, g {};
            f[0] = 0.0;
            for (size_t i = 0; i + 1 < grid; ++i) {
                f[i + 1] = 20.0 * std::pow(nyq / 20.0, static_cast<double>(i) / 382.0);
            }
            f[grid - 1] = nyq;
            for (size_t i = 0; i < grid; ++i) {
                g[i] = std::pow(10.0, -3.0 * l_eff / (fs * rt60_of_freq(f[i], rt60)));
            }

            // Interpolate onto the uniform mesh and apply the linear-phase
            // shift exp(-j pi 127 x). Ooura's rdft stores +sin imaginary
            // parts (the conjugate of numpy's convention), so the packed
            // imaginary component flips sign.
            std::array<double, G> spec {};
            size_t                seg = 0;
            for (size_t k = 0; k < nfreqs; ++k) {
                const double x  = static_cast<double>(k) / static_cast<double>(nfreqs - 1);
                const double fx = x * nyq;
                while (seg + 2 < grid && f[seg + 1] < fx) ++seg;
                const double t   = (fx - f[seg]) / (f[seg + 1] - f[seg]);
                const double amp = g[seg] + std::clamp(t, 0.0, 1.0) * (g[seg + 1] - g[seg]);
                const double ph  = k_pi * group_delay * x;
                if (k == 0) {
                    spec[0] = amp;
                }
                else if (k == nfreqs - 1) {
                    spec[1] = amp * std::cos(ph);
                }
                else {
                    spec[2 * k]     = amp * std::cos(ph);
                    spec[2 * k + 1] = amp * std::sin(ph);
                }
            }
            fft512.inverse_inplace(spec.data());
            const double scale = 2.0 / static_cast<double>(G);
            for (size_t t = 0; t < taps; ++t) {
                const double w = 0.54
                                 - 0.46
                                       * std::cos(2.0 * k_pi * static_cast<double>(t)
                                                  / static_cast<double>(taps - 1));
                taps_out[t] = static_cast<float>(spec[t] * scale * w);
            }
        }

        /// Measure the unscaled tail's omni (W) energy by running the exact
        /// FDN offline: bursts injected at their alignment offsets, the
        /// published absorption FIRs in the loop, energy accumulated over the
        /// prototype's calibration window. Double arithmetic on the worker
        /// thread (~0.2 s); its ratio against tail_energy_target() is the
        /// calibration scale.
        double simulate_tail_energy(const model&                                   m,
                                    const std::array<std::vector<float>, k_lines>& bursts,
                                    size_t base, size_t n_tail) const {
            const size_t                     t_int = base + n_tail;
            std::vector<std::vector<double>> x(k_lines, std::vector<double>(t_int, 0.0));
            double                           energy = 0.0;
            for (size_t t = 0; t < t_int; ++t) {
                double filtered[k_lines];
                for (size_t j = 0; j < k_lines; ++j) {
                    const float* h   = m.absorption.data() + j * k_absorption_taps;
                    const auto&  xj  = x[j];
                    double       acc = 0.0;
                    if (t >= k_delays[j]) {
                        const size_t lag0 = t - k_delays[j];
                        const size_t kmax = std::min(k_absorption_taps - 1, lag0);
                        for (size_t k = 0; k <= kmax; ++k) {
                            acc += static_cast<double>(h[k]) * xj[lag0 - k];
                        }
                    }
                    filtered[j] = acc;
                }
                double w = 0.0;
                for (size_t li = 0; li < k_lines; ++li) {
                    double acc = 0.0;
                    for (size_t j = 0; j < k_lines; ++j) {
                        acc += static_cast<double>(m_feedback[li][j]) * filtered[j];
                    }
                    const size_t start = base - k_delays[li];
                    if (t >= start && t < start + k_burst_length) {
                        acc += static_cast<double>(bursts[li][t - start]);
                    }
                    x[li][t] = acc;
                    if (t >= base && t >= k_delays[li]) {
                        w += static_cast<double>(m_outmix[0][li]) * x[li][t - k_delays[li]];
                    }
                }
                if (t >= base) energy += w * w;
            }
            return energy;
        }

        /// Build a complete model from the current configuration snapshot.
        /// Worker thread; allocates freely; returns nullptr (keep the last
        /// model) until prepare() has run.
        std::shared_ptr<const model> build() const {
            std::array<float, 3>                      dims, source, listener;
            std::array<float, k_walls>                beta;
            std::array<double, k_rt60_bands>          rt60;
            double                                    fs;
            size_t                                    block;
            std::shared_ptr<const std::vector<float>> bands;
            {
                std::lock_guard<std::mutex> lock(m_config_mtx);
                dims     = m_dims;
                source   = m_source;
                listener = m_listener;
                beta     = m_beta;
                for (size_t b = 0; b < k_rt60_bands; ++b) {
                    rt60[b] = static_cast<double>(m_rt60[b]);
                }
                fs    = static_cast<double>(m_fs);
                block = m_block;
                bands = m_burst_bands;
            }
            if (block == 0 || !bands) return nullptr;

            const size_t n0 =
                static_cast<size_t>(std::llround(static_cast<double>(k_er_cutoff_seconds) * fs));
            const size_t base    = std::max(k_delays.back(), n0);
            const size_t chunk   = std::min(block, k_max_chunk);
            const size_t latency = base - n0;

            auto fresh   = std::make_shared<model>();
            fresh->chunk = chunk;

            // Early reflections: one nearest-sample tap per image source
            // below the cutoff, SH-encoded through the library's own
            // evaluate_sh at the image direction.
            for_each_image(dims, source, listener, beta, static_cast<double>(k_er_cutoff_seconds),
                           [&](double t, double amp, const double* u, int refl) {
                               er_tap tap {};
                               tap.delay      = latency + static_cast<size_t>(std::llround(t * fs));
                               tap.direct     = (refl == 0);
                               const float az = static_cast<float>(std::atan2(u[1], u[0]));
                               const float el =
                                   static_cast<float>(std::atan2(u[2], std::hypot(u[0], u[1])));
                               float sh[k_lines];
                               evaluate_sh(m_order, az, el, sh);
                               for (size_t ch = 0; ch < m_channels; ++ch) {
                                   tap.gains[ch] =
                                       static_cast<float>(amp * static_cast<double>(sh[ch]));
                               }
                               fresh->taps.push_back(tap);
                           });

            // Per-line absorption FIRs fitted to the parameterized decay.
            fresh->absorption.assign(k_lines * k_absorption_taps, 0.0f);
            real_fft fft512(512);
            for (size_t line = 0; line < k_lines; ++line) {
                design_absorption_fir(k_delays[line], fs, rt60, fft512,
                                      fresh->absorption.data() + line * k_absorption_taps);
            }
            // Per-line reversed copy for the audio path's ascending dot product.
            fresh->absorption_rev.assign(k_lines * k_absorption_taps, 0.0f);
            for (size_t line = 0; line < k_lines; ++line) {
                const float* src = fresh->absorption.data() + line * k_absorption_taps;
                float*       dst = fresh->absorption_rev.data() + line * k_absorption_taps;
                for (size_t k = 0; k < k_absorption_taps; ++k) {
                    dst[k] = src[k_absorption_taps - 1 - k];
                }
            }

            // Per-line input bursts: octave bands re-shaped by the
            // parameterized decay envelopes, then unit-energy normalized.
            std::array<std::vector<float>, k_lines> bursts;
            std::array<double, k_noise_bands>       band_rate {};
            for (size_t b = 0; b < k_noise_bands; ++b) {
                const double c = 31.25 * std::pow(2.0, static_cast<double>(b));
                band_rate[b]   = 6.91 / (fs * rt60_of_freq(c, rt60));
            }
            for (size_t line = 0; line < k_lines; ++line) {
                std::vector<double> shaped(k_burst_length, 0.0);
                for (size_t b = 0; b < k_noise_bands; ++b) {
                    const float* src = bands->data() + (line * k_noise_bands + b) * k_burst_length;
                    for (size_t n = 0; n < k_burst_length; ++n) {
                        shaped[n] += static_cast<double>(src[n])
                                     * std::exp(-band_rate[b] * static_cast<double>(n));
                    }
                }
                double e = 0.0;
                for (double v : shaped) e += v * v;
                const double norm = 1.0 / std::sqrt(e);
                bursts[line].resize(k_burst_length);
                for (size_t n = 0; n < k_burst_length; ++n) {
                    bursts[line][n] = static_cast<float>(shaped[n] * norm);
                }
            }

            // Injection FIR spectra: each line's burst behind its alignment
            // delay (base - d_i), partitioned at the audio chunk size.
            const size_t taps_total = base - k_delays.front() + k_burst_length;
            const size_t partitions = (taps_total + chunk - 1) / chunk;
            const size_t fft_size   = 2 * chunk;
            fresh->partitions       = partitions;
            fresh->inject_spectra.assign(k_lines * partitions * fft_size, 0.0);
            real_fft            fft(fft_size);
            std::vector<double> seg(fft_size);
            for (size_t line = 0; line < k_lines; ++line) {
                const size_t delay = base - k_delays[line];
                for (size_t p = 0; p < partitions; ++p) {
                    std::fill(seg.begin(), seg.end(), 0.0);
                    const size_t p0 = p * chunk;
                    for (size_t i = 0; i < chunk; ++i) {
                        const size_t t = p0 + i;
                        if (t >= delay && t < delay + k_burst_length) {
                            seg[i] = static_cast<double>(bursts[line][t - delay]);
                        }
                    }
                    fft.forward_inplace(seg.data());
                    std::copy(
                        seg.begin(), seg.end(),
                        fresh->inject_spectra.begin()
                            + static_cast<std::ptrdiff_t>((line * partitions + p) * fft_size));
                }
            }

            // Calibrate the tail's omni energy onto the image-source target.
            const size_t n_tail =
                static_cast<size_t>(std::llround(static_cast<double>(k_calibration_seconds) * fs))
                - n0;
            const double realized = simulate_tail_energy(*fresh, bursts, base, n_tail);
            const double target   = tail_energy_target(dims, source, listener, beta, rt60);
            const double scale    = std::sqrt(target / std::max(realized, 1e-30));
            for (size_t ch = 0; ch < m_channels; ++ch) {
                fresh->out_gain[ch] = static_cast<float>(static_cast<double>(m_sn3d[ch]) * scale);
            }

            // Crossfade sources: the previously published model when its
            // audio-format-dependent shape matches, else silence.
            const auto last = m_rebuilder.peek(); // worker thread; safe to lock
            if (last && last->chunk == chunk && last->partitions == partitions) {
                fresh->prev_taps     = last->taps;
                fresh->prev_out_gain = last->out_gain;
            }
            return fresh;
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_ROOM_H
