/// AmbiTap: target-independent ambisonics library
/// Minimal C ABI over the header-only C++ core, for language bindings and the
/// verification notebooks (notebooks/ drive it via ctypes).
///
/// Conventions: plain C types only; matrices are row-major float arrays sized
/// by the caller; functions return 0 on success and -1 on any error (bad
/// argument, unknown name, internal exception). No global state.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_CAPI_H
#define AMBITAP_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define AMBITAP_API __declspec(dllexport)
#else
#define AMBITAP_API __attribute__((visibility("default")))
#endif

/// (order+1)^2, or -1 for an out-of-range order.
AMBITAP_API int ambitap_channel_count(int order);

/// Real SH (ACN/SN3D) at a direction into out[(order+1)^2].
AMBITAP_API int ambitap_evaluate_sh(int order, float azimuth, float elevation, float* out);

/// max-rE per-order weights into out[order+1]; energy_normalized selects the
/// energy-preserving variant used by the decoders.
AMBITAP_API int ambitap_max_re_weights(int order, int energy_normalized, float* out);

/// SH rotation matrix for intrinsic Z-Y'-X'' Euler angles into
/// out[C*C] (row-major, C = (order+1)^2).
AMBITAP_API int ambitap_sh_rotation_matrix(int order, float yaw, float pitch, float roll,
                                           float* out);

/// Speaker layout preset by name: "stereo", "quad", "5.1", "hexagon", "7.1",
/// "cube", "octagon", "7.1.4". Writes azimuth/elevation (radians) into
/// az[cap], el[cap]. Returns the speaker count, or -1 (unknown name /
/// insufficient cap).
AMBITAP_API int ambitap_layout_preset(const char* name, float* az, float* el, int cap);

/// VBAP gains (3D triangle or 2D pairwise, chosen by the layout) for a source
/// direction into out[n_speakers].
AMBITAP_API int ambitap_vbap_gains(int n_speakers, const float* az, const float* el,
                                   float src_azimuth, float src_elevation, float* out);

/// Decoder matrix into out[n_speakers * (order+1)^2] (row-major;
/// speaker_signals = D * hoa). algorithm: "mode_match", "allrad", "epad".
AMBITAP_API int ambitap_decoder_matrix(const char* algorithm, int order, int n_speakers,
                                       const float* az, const float* el, int use_max_re,
                                       float* out);

/// Built-in HRTF dataset metadata.
AMBITAP_API int ambitap_builtin_hrtf_info(int* order, int* channels, int* length,
                                          float* sample_rate);

/// One SH-domain HRTF FIR into out[length]: magls selects the dataset,
/// ear 0 = left / 1 = right, channel in [0, channels).
AMBITAP_API int ambitap_builtin_hrtf_fir(int magls, int ear, int channel, float* out);

/// Windowed-sinc FIR resampling into out[out_cap]. Returns the output length,
/// or -1 when out_cap is too small.
AMBITAP_API int ambitap_resample_fir(const float* in, int in_len, float in_rate, float out_rate,
                                     float* out, int out_cap);

/// Stream x[n] through the partitioned overlap-save convolver (block_size a
/// power of two >= 4) against ir[ir_len], writing n samples to out. For
/// verifying the convolver against a direct reference.
AMBITAP_API int ambitap_convolve(const float* x, int n, const float* ir, int ir_len, int block_size,
                                 float* out);

/// Offline binaural render of a mono source moving along a per-sample
/// direction trajectory (az/el arrays of length n_frames, radians), through
/// encoder -> head rotation -> HRTF convolution at the given host sample rate
/// (built-in FIRs are resampled to it). magls selects the dataset. Writes
/// n_frames samples to left/right.
AMBITAP_API int ambitap_binaural_render(int order, float sample_rate, int magls, const float* mono,
                                        int n_frames, const float* az, const float* el,
                                        float head_yaw, float head_pitch, float head_roll,
                                        float* left, float* right);

/// Capture the dsp::encoder's parameter-smoothing ramp: encode a constant 1.0
/// input at (az0, el0), retarget to (az1, el1) at sample change_at, and write
/// the per-channel output channel-major into out[C * n_frames]
/// (C = (order+1)^2). The output IS the ramped coefficient trajectory.
AMBITAP_API int ambitap_encoder_ramp(int order, float az0, float el0, float az1, float el1,
                                     int change_at, int n_frames, float* out);

/// Capture the dsp::decoder's matrix crossfade: build the decoder for
/// from_algorithm on the given layout, settle it and run past the initial
/// fade-in, switch to to_algorithm, settle, then record n_frames of
/// per-speaker output for the constant HOA input frame hoa[(order+1)^2].
/// Frame 0 is the first sample of the crossfade (length: 256 samples).
/// Speaker-major out[n_speakers * n_frames]. Algorithms: "mode_match",
/// "allrad", "epad".
AMBITAP_API int ambitap_decoder_crossfade(int order, int n_speakers, const float* az,
                                          const float* el, const char* from_algorithm,
                                          const char* to_algorithm, int use_max_re,
                                          const float* hoa, int n_frames, float* out);

/// Run a mono signal through dsp::doppler (as the W channel of an order-1
/// bus) with a per-sample source-distance trajectory dist[n_frames] (meters).
/// max_distance sizes the delay line and must cover max(dist). Writes
/// n_frames delayed samples to out — distance modulation produces the
/// slew-smoothed Doppler pitch shift.
AMBITAP_API int ambitap_doppler_process(float sample_rate, float max_distance, const float* mono,
                                        const float* dist, int n_frames, float* out);

/// Per-sample linear gain computed by dsp::spatial_compressor's envelope
/// follower for the W signal w[n_frames], given threshold (dB), ratio,
/// attack/release (seconds), and makeup gain (dB). Writes n_frames gains to
/// out; multiply any HOA channel by it to reproduce the processor exactly.
AMBITAP_API int ambitap_compressor_gain(float sample_rate, float threshold_db, float ratio,
                                        float attack_s, float release_s, float makeup_db,
                                        const float* w, int n_frames, float* out);

/// Stream channel-major planar HOA hoa[(order+1)^2 * n_frames] through
/// analysis::soundfield_grid in blocks of block_size, then snapshot: writes
/// the normalized [0, 1] heatmap row-major into out[(az_steps/2) * az_steps]
/// (row 0 = zenith, column 0 = azimuth -pi) and the absolute peak level into
/// *peak_db. smoothing_ms is the per-direction energy smoothing constant.
AMBITAP_API int ambitap_soundfield_grid(int order, int az_steps, float sample_rate,
                                        float smoothing_ms, const float* hoa, int n_frames,
                                        int block_size, float dynamic_range_db, float* out,
                                        float* peak_db);

/// Smoothed active-intensity (energy) vector trajectory from channel-major
/// planar HOA hoa[(order+1)^2 * n_frames] (first-order channels are used;
/// order >= 1). Writes x/y/z channel-major into out[3 * n_frames]
/// (x = front, y = left, z = up). smoothing_s is the one-pole time constant.
AMBITAP_API int ambitap_energy_vector(int order, float sample_rate, float smoothing_s,
                                      const float* hoa, int n_frames, float* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AMBITAP_CAPI_H
