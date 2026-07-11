/// @file plate.h
/// @brief N-in/M-out plate reverb — Dattorro's plate-class tank (Griesinger style),
///        generalized from the stereo figure-8 to a K-branch ring.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../math/core/validate.h"
#include "util/smoothing.h"

namespace ambitap::dsp {

    /// Maximum input channel count accepted by dsp::plate.
    inline constexpr int k_plate_max_inputs = 64;

    /// Maximum output channel count accepted by dsp::plate.
    inline constexpr int k_plate_max_outputs = 64;

    /// Allowed tank-branch range for dsp::plate. Two branches is Dattorro's
    /// original figure-8; more branches add tail density and decorrelation.
    inline constexpr int k_plate_min_branches = 2;
    inline constexpr int k_plate_max_branches = 8;

    /// A multichannel plate-class reverb after Dattorro (1997), "Effect Design
    /// Part 1: Reverberator and Other Filters" — the plate reverb "in the
    /// Griesinger style", generalized for N inputs and M outputs.
    ///
    /// Topology, and how it maps to the paper:
    ///   - Per input: predelay, then the one-pole "bandwidth" low-pass.
    ///   - An N x K signed injection matrix (rotated Sylvester-Hadamard signs,
    ///     scaled sqrt(2/K)) distributes the inputs across the K tank branches.
    ///     For N = 1, K = 2 this is the paper's unit-gain feed to both halves.
    ///   - Per branch: the four series input-diffusion allpasses (coefficients
    ///     0.750/0.750/0.625/0.625, delays 142/107/379/277 at the reference
    ///     rate).
    ///   - The tank: K branches in a ring, each the paper's chain — modulated
    ///     allpass ("decay diffusion 1") -> delay -> damping low-pass -> decay
    ///     gain -> allpass ("decay diffusion 2") -> delay -> decay gain -> next
    ///     branch. K = 2 with the paper's two delay sets reproduces the
    ///     figure-8; branch pairs beyond the first reuse the two sets with
    ///     lengths stretched ~6% per pair so no two branches share periods.
    ///   - Outputs: each output sums 7 signed taps (gain 0.6 each, the paper's
    ///     tap count and gain) taken across the branches' delay and allpass
    ///     lines; branch walk, sign pattern, and golden-ratio tap positions are
    ///     deterministic in the output index, so every output is a distinct,
    ///     mutually decorrelated view of the same tank. The kernel is 100% wet;
    ///     dry/wet belongs to the caller.
    ///
    /// All magic constants are Dattorro's, specified at his 29761 Hz reference
    /// rate and rescaled to the prepared sample rate. Deviations from the
    /// paper are noted inline where they occur.
    ///
    /// Lifecycle: construct with (inputs, outputs, branches), then prepare()
    /// with the sample rate (allocates all rings; any block size may be used).
    /// Until prepare() is called, process() emits silence.
    ///
    /// Threading: setters run on one control thread, process on one audio
    /// thread. Every setter is wait-free (atomic store into a smoother); the
    /// process path never allocates, locks, or makes syscalls. There is no
    /// worker thread — the embedded profile gets the full processor.
    class plate {
      public:
        /// Taps summed per output channel (the paper's yL/yR recipe count).
        static constexpr size_t k_taps = 7;

        /// @param inputs    Input channel count in [1, k_plate_max_inputs].
        /// @param outputs   Output channel count in [1, k_plate_max_outputs].
        /// @param branches  Tank branches in [k_plate_min_branches, k_plate_max_branches].
        /// @throws std::invalid_argument on out-of-range counts (embedded
        ///         profile: asserts in debug, clamps in release).
        explicit plate(int inputs, int outputs, int branches = 4)
            : m_inputs(static_cast<size_t>(validated_count(inputs, 1, k_plate_max_inputs, "inputs")))
            , m_outputs(static_cast<size_t>(validated_count(outputs, 1, k_plate_max_outputs, "outputs")))
            , m_branches(static_cast<size_t>(
                  validated_count(branches, k_plate_min_branches, k_plate_max_branches, "branches"))) {
            build_injection();
            for (size_t b = 0; b < m_branches; ++b) {
                // Slightly incommensurate LFO rates per branch so the branch
                // modulations never phase-lock (factors in [1, 1.5)).
                m_lfo_factor[b] = 1.0f + 0.5f * fractional(0.6180339887f * static_cast<float>(b + 1));
            }
        }

        size_t inputs() const { return m_inputs; }
        size_t outputs() const { return m_outputs; }
        size_t branches() const { return m_branches; }
        bool   is_prepared() const { return !m_rings.empty(); }

        /// Set the sample rate, rescale all line lengths from the 29761 Hz
        /// reference values, and (re)allocate the rings. Not RT-safe. Any
        /// process() block size may be used afterwards.
        void prepare(float sample_rate);

        /// Tail decay per tank pass, [0, 1]. The paper's "decay"; applied twice
        /// per branch. 1 = freeze (lossless ring; damping still bleeds energy).
        /// RT-safe, click-free.
        void  set_decay(float value) { m_decay.set(std::clamp(value, 0.0f, 1.0f)); }
        float decay() const { return m_decay.target(); }

        /// In-loop high-frequency damping, [0, 1). 0 = no damping. RT-safe.
        void  set_damping(float value) { m_damping.set(std::clamp(value, 0.0f, 0.9999f)); }
        float damping() const { return m_damping.target(); }

        /// Input band-limiting, (0, 1]. 1 = full bandwidth. RT-safe.
        void  set_bandwidth(float value) { m_bandwidth.set(std::clamp(value, 0.0001f, 1.0f)); }
        float bandwidth() const { return m_bandwidth.target(); }

        /// The four diffusion coefficients, each [0, 1). Defaults are the
        /// paper's. RT-safe.
        void  set_input_diffusion_1(float value) { m_input_diffusion_1.set(std::clamp(value, 0.0f, 0.9999f)); }
        float input_diffusion_1() const { return m_input_diffusion_1.target(); }
        void  set_input_diffusion_2(float value) { m_input_diffusion_2.set(std::clamp(value, 0.0f, 0.9999f)); }
        float input_diffusion_2() const { return m_input_diffusion_2.target(); }
        void  set_decay_diffusion_1(float value) { m_decay_diffusion_1.set(std::clamp(value, 0.0f, 0.9999f)); }
        float decay_diffusion_1() const { return m_decay_diffusion_1.target(); }
        void  set_decay_diffusion_2(float value) { m_decay_diffusion_2.set(std::clamp(value, 0.0f, 0.9999f)); }
        float decay_diffusion_2() const { return m_decay_diffusion_2.target(); }

        /// Pre-tank delay in seconds, [0, k_max_predelay_seconds]. RT-safe;
        /// changes glide over the smoothing window (a brief pitch shift, like
        /// any moving delay).
        void  set_predelay_seconds(float value) { m_predelay.set(std::clamp(value, 0.0f, k_max_predelay_seconds)); }
        float predelay_seconds() const { return m_predelay.target(); }

        /// Tank-allpass modulation depth in milliseconds, [0, k_max_mod_depth_ms].
        /// The paper's 16-sample excursion at 29761 Hz is ~0.54 ms. RT-safe.
        void  set_mod_depth_ms(float value) { m_mod_depth.set(std::clamp(value, 0.0f, k_max_mod_depth_ms)); }
        float mod_depth_ms() const { return m_mod_depth.target(); }

        /// Tank-allpass modulation rate in Hz, [0, 10]. RT-safe.
        void set_mod_rate_hz(float value) {
            m_mod_rate.store(std::clamp(value, 0.0f, 10.0f), std::memory_order_relaxed);
        }
        float mod_rate_hz() const { return m_mod_rate.load(std::memory_order_relaxed); }

        /// Make every smoothed parameter jump to its target instead of ramping
        /// (offline rendering / tests).
        void snap_parameters() {
            m_decay.snap();
            m_damping.snap();
            m_bandwidth.snap();
            m_input_diffusion_1.snap();
            m_input_diffusion_2.snap();
            m_decay_diffusion_1.snap();
            m_decay_diffusion_2.snap();
            m_predelay.snap();
            m_mod_depth.snap();
        }

        /// Clear all audio state (rings, filter memories, LFO phases); keep
        /// the allocation and the parameters. Control thread.
        void reset() {
            std::fill(m_rings.begin(), m_rings.end(), 0.0f);
            m_bandwidth_state.fill(0.0f);
            m_damping_state.fill(0.0f);
            m_t = 0;
            for (size_t b = 0; b < m_branches; ++b) {
                m_lfo_phase[b] = static_cast<float>(b) / static_cast<float>(m_branches);
            }
        }

        /// Process one frame: inputs() samples in, outputs() samples out.
        /// Output may alias input. Silence until prepare() has been called.
        void process_frame(const float* in, float* out) noexcept {
            if (m_rings.empty()) {
                for (size_t m = 0; m < m_outputs; ++m) {
                    out[m] = 0.0f;
                }
                return;
            }
            std::array<const float*, k_plate_max_inputs> ins;
            std::array<float*, k_plate_max_outputs>      outs;
            for (size_t n = 0; n < m_inputs; ++n) {
                ins[n] = in + n;
            }
            for (size_t m = 0; m < m_outputs; ++m) {
                outs[m] = out + m;
            }
            tick_sample(ins.data(), outs.data(), 0);
        }

        /// Process a block of planar channel buffers: inputs() pointers in,
        /// outputs() pointers out. Output may alias input. Silence until
        /// prepare() has been called.
        void process(const float* const* in, float* const* out, size_t frame_count) noexcept {
            if (m_rings.empty()) {
                for (size_t m = 0; m < m_outputs; ++m) {
                    for (size_t i = 0; i < frame_count; ++i) {
                        out[m][i] = 0.0f;
                    }
                }
                return;
            }
            for (size_t i = 0; i < frame_count; ++i) {
                tick_sample(in, out, i);
            }
        }

        /// Longest predelay reservable via set_predelay_seconds().
        static constexpr float k_max_predelay_seconds = 0.25f;

        /// Deepest tank modulation reservable via set_mod_depth_ms().
        static constexpr float k_max_mod_depth_ms = 2.0f;

      private:
        /// One ring in the shared pool: power-of-two capacity, so a position
        /// is (sample counter - delay) & mask. Delays are relative to the
        /// shared counter m_t; every ring is written once per sample.
        struct line {
            size_t offset{0};
            size_t mask{0};
            size_t length{0};
        };

        /// One output tap: a delayed, signed read of a tank line.
        struct tap {
            size_t line{0}; ///< index into m_tap_lines
            size_t delay{0};
            float  gain{0.0f};
        };

        /// Dattorro's reference sample rate; all k_* delays below are in
        /// samples at this rate and rescaled in prepare().
        static constexpr float k_reference_rate = 29761.0f;

        /// Input-diffusion allpass delays (series, per branch).
        static constexpr std::array<size_t, 4> k_diffusion_delays{142, 107, 379, 277};

        /// Tank delay sets {AP1, D1, AP2, D2}: even branches take the paper's
        /// left half, odd branches the right half.
        static constexpr std::array<std::array<size_t, 4>, 2> k_tank_delays{{{672, 4453, 1800, 3720},   //
                                                                             {908, 4217, 2656, 3163}}}; //

        /// Length stretch per branch pair beyond the first — keeps added
        /// branches' periods incommensurate with the paper pair's. (Deviation
        /// from the paper: it has only the one pair.)
        static constexpr float k_branch_deviation = 0.0618f;

        /// Per-tap output gain (the paper's 0.6).
        static constexpr float k_tap_gain = 0.6f;

        /// Which line each of the 7 taps reads: 0 = D1, 1 = AP2, 2 = D2 —
        /// the paper's component sequence for yL/yR.
        static constexpr std::array<size_t, k_taps> k_tap_components{0, 0, 1, 2, 0, 1, 2};

        /// Branch offset walked by each tap (mod branches). For 2 branches
        /// this is the paper's split: four taps from the opposite branch,
        /// three from the output's own.
        static constexpr std::array<size_t, k_taps> k_tap_steps{1, 3, 5, 7, 0, 2, 4};

        /// The paper's tap sign pattern for one output.
        static constexpr std::array<float, k_taps> k_tap_signs{1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f};

        static int validated_count(int value, int lowest, int highest, const char* what) {
            if (value < lowest || value > highest) {
#if AMBITAP_HAS_EXCEPTIONS
                throw std::invalid_argument(std::string("ambitap::dsp::plate: ") + what + " " + std::to_string(value)
                                            + " outside supported range [" + std::to_string(lowest) + ", "
                                            + std::to_string(highest) + "]");
#else
                assert(false && "ambitap::dsp::plate: count outside supported range");
                (void)what;
                return std::clamp(value, lowest, highest);
#endif
            }
            return value;
        }

        static float fractional(float x) { return x - std::floor(x); }

        /// Signed injection matrix: rotated Sylvester-Hadamard sign rows
        /// scaled by sqrt(2/K), so N = 1 / K = 2 reduces to the paper's
        /// unit-gain feed into both tank halves, and every input reaches every
        /// branch with a distinct sign pattern.
        void build_injection() {
            const size_t width = std::bit_ceil(m_branches);
            const float  scale = std::sqrt(2.0f / static_cast<float>(m_branches));
            for (size_t n = 0; n < m_inputs; ++n) {
                const size_t row      = n % width;
                const size_t rotation = n / width;
                for (size_t b = 0; b < m_branches; ++b) {
                    const size_t col = (b + rotation) % width;
                    m_inject[n][b]   = (std::popcount(row & col) % 2 != 0) ? -scale : scale;
                }
            }
        }

        float ring_read(const line& l, size_t delay) const noexcept {
            return m_rings[l.offset + ((m_t - delay) & l.mask)];
        }

        /// Linear-interpolated read at a fractional delay (modulated allpass,
        /// gliding predelay).
        float ring_read_frac(const line& l, float delay) const noexcept {
            const size_t whole = static_cast<size_t>(delay);
            const float  frac  = delay - static_cast<float>(whole);
            const float  a     = m_rings[l.offset + ((m_t - whole) & l.mask)];
            const float  b     = m_rings[l.offset + ((m_t - whole - 1) & l.mask)];
            return a + (b - a) * frac;
        }

        void ring_write(const line& l, float value) noexcept { m_rings[l.offset + (m_t & l.mask)] = value; }

        /// Schroeder allpass around ring l at its nominal length:
        /// H(z) = (z^-L - g) / (1 - g z^-L).
        float allpass(const line& l, float g, float x) noexcept {
            const float z = ring_read(l, l.length);
            const float v = x + g * z;
            ring_write(l, v);
            return z - g * v;
        }

        /// Allpass with a modulated (fractional) delay — the tank's "decay
        /// diffusion 1" stage.
        float allpass_mod(const line& l, float g, float delay, float x) noexcept {
            const float z = ring_read_frac(l, delay);
            const float v = x + g * z;
            ring_write(l, v);
            return z - g * v;
        }

        /// Parabolic pseudo-sine on phase [0, 1) — smooth enough for a
        /// sub-audio chorus LFO, no libm call in the audio path.
        static float lfo_shape(float phase) {
            if (phase < 0.5f) {
                const float d = phase - 0.25f;
                return 1.0f - 16.0f * d * d;
            }
            const float d = phase - 0.75f;
            return 16.0f * d * d - 1.0f;
        }

        void tick_sample(const float* const* in, float* const* out, size_t i) noexcept;

        size_t m_inputs;
        size_t m_outputs;
        size_t m_branches;
        float  m_fs{0.0f};

        // Control-thread parameters, smoothed on the audio thread. Defaults
        // are the paper's table 1 values (decay 0.5, damping 0.0005,
        // bandwidth 0.9995), with the 16-sample excursion expressed in ms.
        smoothed_scalar    m_decay{0.5f};
        smoothed_scalar    m_damping{0.0005f};
        smoothed_scalar    m_bandwidth{0.9995f};
        smoothed_scalar    m_input_diffusion_1{0.750f};
        smoothed_scalar    m_input_diffusion_2{0.625f};
        smoothed_scalar    m_decay_diffusion_1{0.70f};
        smoothed_scalar    m_decay_diffusion_2{0.50f};
        smoothed_scalar    m_predelay{0.0f};
        smoothed_scalar    m_mod_depth{0.5376f};
        std::atomic<float> m_mod_rate{1.0f};

        std::array<std::array<float, k_plate_max_branches>, k_plate_max_inputs> m_inject{};
        std::array<float, k_plate_max_branches>                                 m_lfo_factor{};

        // ---- Audio-thread state (allocated in prepare(); owned by the one
        // audio thread thereafter).
        std::vector<float> m_rings;
        size_t             m_t{0};

        std::array<line, k_plate_max_inputs>                     m_predelay_line{};
        std::array<std::array<line, 4>, k_plate_max_branches>    m_diffusion_line{};
        std::array<line, k_plate_max_branches>                   m_ap1_line{};
        std::array<line, k_plate_max_branches>                   m_delay1_line{};
        std::array<line, k_plate_max_branches>                   m_ap2_line{};
        std::array<line, k_plate_max_branches>                   m_delay2_line{};
        std::array<line, 3 * k_plate_max_branches>               m_tap_lines{}; ///< [branch][D1, AP2, D2]
        std::array<std::array<tap, k_taps>, k_plate_max_outputs> m_taps{};

        std::array<float, k_plate_max_inputs>   m_bandwidth_state{};
        std::array<float, k_plate_max_branches> m_damping_state{};
        std::array<float, k_plate_max_branches> m_lfo_phase{};
        float                                   m_max_excursion{0.0f};
    };

    inline void plate::prepare(float sample_rate) {
        m_fs = sample_rate;

        const float rate_scale = sample_rate / k_reference_rate;
        m_max_excursion        = k_max_mod_depth_ms * 0.001f * sample_rate;

        // Rescale a reference-rate delay for branch b: sample-rate scaling
        // plus the per-pair stretch, nudged odd so line periods stay mutually
        // incommensurate.
        auto scaled = [rate_scale](size_t reference, size_t branch) -> size_t {
            const float stretch = 1.0f + k_branch_deviation * static_cast<float>(branch / 2);
            auto        length = static_cast<size_t>(std::lround(static_cast<float>(reference) * stretch * rate_scale));
            length |= 1u;
            return std::max<size_t>(length, 2);
        };

        size_t total   = 0;
        auto   reserve = [&total](line& l, size_t length, size_t headroom) {
            l.length          = length;
            const size_t need = std::bit_ceil(length + headroom);
            l.offset          = total;
            l.mask            = need - 1;
            total += need;
        };

        const auto predelay_span = static_cast<size_t>(k_max_predelay_seconds * sample_rate) + 4;
        for (size_t n = 0; n < m_inputs; ++n) {
            reserve(m_predelay_line[n], predelay_span, 2);
        }
        const auto excursion_headroom = static_cast<size_t>(m_max_excursion) + 3;
        for (size_t b = 0; b < m_branches; ++b) {
            for (size_t a = 0; a < 4; ++a) {
                reserve(m_diffusion_line[b][a], scaled(k_diffusion_delays[a], b), 2);
            }
            const auto& set = k_tank_delays[b % 2];
            reserve(m_ap1_line[b], scaled(set[0], b), excursion_headroom);
            reserve(m_delay1_line[b], scaled(set[1], b), 2);
            reserve(m_ap2_line[b], scaled(set[2], b), 2);
            reserve(m_delay2_line[b], scaled(set[3], b), 2);

            m_tap_lines[b * 3 + 0] = m_delay1_line[b];
            m_tap_lines[b * 3 + 1] = m_ap2_line[b];
            m_tap_lines[b * 3 + 2] = m_delay2_line[b];
        }

        // Output taps: deterministic in (output index, tap index) — a branch
        // walk, the paper's component/sign sequences, and golden-ratio
        // low-discrepancy positions, so every output reads a distinct tap set.
        constexpr float k_phi_frac = 0.6180339887f;
        for (size_t m = 0; m < m_outputs; ++m) {
            for (size_t j = 0; j < k_taps; ++j) {
                const size_t branch    = (m + k_tap_steps[j]) % m_branches;
                const size_t component = k_tap_components[j];
                const line&  l         = m_tap_lines[branch * 3 + component];
                const float  u         = fractional(k_phi_frac * static_cast<float>(m * k_taps + j + 1));
                m_taps[m][j].line      = branch * 3 + component;
                m_taps[m][j].delay     = 1 + static_cast<size_t>(u * static_cast<float>(l.length - 2));
                m_taps[m][j].gain      = k_tap_gain * k_tap_signs[j];
            }
        }

        m_rings.assign(total, 0.0f);
        reset();
    }

    inline void plate::tick_sample(const float* const* in, float* const* out, size_t i) noexcept {
        const float decay     = m_decay.tick();
        const float damping   = m_damping.tick();
        const float bandwidth = m_bandwidth.tick();
        const float id1       = m_input_diffusion_1.tick();
        const float id2       = m_input_diffusion_2.tick();
        const float dd1       = m_decay_diffusion_1.tick();
        const float dd2       = m_decay_diffusion_2.tick();
        const float predelay  = m_predelay.tick() * m_fs;
        const float depth     = std::min(m_mod_depth.tick() * 0.001f * m_fs, m_max_excursion);
        const float rate      = m_mod_rate.load(std::memory_order_relaxed);

        // Input stage: predelay -> bandwidth low-pass (paper form
        // y = bw * x + (1 - bw) * y1).
        std::array<float, k_plate_max_inputs> pre;
        for (size_t n = 0; n < m_inputs; ++n) {
            ring_write(m_predelay_line[n], in[n][i]);
            const float delayed  = ring_read_frac(m_predelay_line[n], predelay);
            m_bandwidth_state[n] = bandwidth * delayed + (1.0f - bandwidth) * m_bandwidth_state[n];
            pre[n]               = m_bandwidth_state[n];
        }

        // Injection + per-branch input diffusion (two allpasses at each of
        // the paper's two coefficients).
        std::array<float, k_plate_max_branches> branch_in;
        for (size_t b = 0; b < m_branches; ++b) {
            float s = 0.0f;
            for (size_t n = 0; n < m_inputs; ++n) {
                s += m_inject[n][b] * pre[n];
            }
            s            = allpass(m_diffusion_line[b][0], id1, s);
            s            = allpass(m_diffusion_line[b][1], id1, s);
            s            = allpass(m_diffusion_line[b][2], id2, s);
            s            = allpass(m_diffusion_line[b][3], id2, s);
            branch_in[b] = s;
        }

        // The tank ring. Feedback reads land >= 1 sample in the past, so
        // branch evaluation order does not matter. The tank allpasses take
        // negated coefficients — fig. 1 reverses the decay diffusors' signs
        // relative to the input diffusors.
        for (size_t b = 0; b < m_branches; ++b) {
            const size_t prev = (b + m_branches - 1) % m_branches;
            const float  fed  = branch_in[b] + decay * ring_read(m_delay2_line[prev], m_delay2_line[prev].length);

            m_lfo_phase[b] += rate * m_lfo_factor[b] / m_fs;
            m_lfo_phase[b] -= std::floor(m_lfo_phase[b]);
            const float excursion = depth * lfo_shape(m_lfo_phase[b]);
            const float modulated = std::max(1.0f, static_cast<float>(m_ap1_line[b].length) + excursion);

            float v = allpass_mod(m_ap1_line[b], -dd1, modulated, fed);

            const float tapped = ring_read(m_delay1_line[b], m_delay1_line[b].length);
            ring_write(m_delay1_line[b], v);

            m_damping_state[b] = (1.0f - damping) * tapped + damping * m_damping_state[b];
            v                  = m_damping_state[b] * decay;
            v                  = allpass(m_ap2_line[b], -dd2, v);
            ring_write(m_delay2_line[b], v);
        }

        for (size_t m = 0; m < m_outputs; ++m) {
            float acc = 0.0f;
            for (const tap& t : m_taps[m]) {
                acc += t.gain * ring_read(m_tap_lines[t.line], t.delay);
            }
            out[m][i] = acc;
        }

        ++m_t;
    }

} // namespace ambitap::dsp
