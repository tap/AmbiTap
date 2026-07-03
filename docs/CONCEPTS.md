# AmbiTap concepts

The orientation guide to the library: the conventions everything obeys, the
real-time contract every processor honors, and the lifecycle a processor
moves through. Read this once and the per-class reference reads easily. The
API reference is generated from the header doc-comments; this page is the
map.

## Conventions

AmbiTap is **AmbiX throughout**: ACN channel ordering, SN3D normalization.

- **Channel ordering (ACN).** Channel index for degree *n*, order *m* is
  `acn = n(n+1) + m`. So W=0; Y,Z,X = 1,2,3; then the second-order five, and
  so on. `channel_count(order)` is `(order+1)²`.
- **Normalization (SN3D).** The W channel of a unit-amplitude source is 1.0.
  Decoder construction happens internally in the orthonormal **N3D** basis
  (AmbiTap's N3D is unit-mean-square: `YᵀY = L·I` on an *L*-point t-design,
  no 4π factor) and converts back; you never see N3D at the API surface.
  FuMa (orders 0–3) is available via `dsp::format_converter`.
- **Angles are radians.** Azimuth 0 = front, +π/2 = left. Elevation 0 =
  horizon, +π/2 = zenith.
- **Rotation.** Intrinsic Z-Y'-X'' Euler: yaw about +Z applied first, pitch
  about +Y second, roll about +X last (right-hand rule; positive pitch tilts
  the front axis *down*). Rotation matrices rotate the *soundfield*:
  `Y(R·d) = R_sh · Y(d)`. Built by the exact Ivanic–Ruedenberg recurrence
  (`math/core/rotation_recurrence.h`).
- **Decoder matrices** are shaped `(speakers × channels)`, row-major, with
  `speaker_signals = D · hoa`. Mode-matching, ALLRAD, and EPAD share one
  absolute-gain convention.
- **Binaural sample rate.** The embedded MIT KEMAR HRTFs are 44.1 kHz data;
  pass the host rate to `binaural_renderer::prepare(block, sample_rate)` and
  the FIRs are resampled to match.

These are cross-checked against spaudiopy, pyshtools, and SciPy in
[`COMPARISON.md`](COMPARISON.md).

## The real-time contract

Every processor's `process()` path is **wait-free**: it never locks, never
allocates or frees, and never blocks on a worker thread. This is not a
promise in prose — it is machine-checked in CI by
`tests/test_rt_safety.cpp` (which replaces global `operator new`/`delete` to
prove the process paths never allocate) and `tests/test_dsp_threads.cpp`
(setter-vs-process stress under ThreadSanitizer).

Two mechanisms make it work:

- **Wait-free publication (`dsp::rt_published`, `dsp::async_rebuilder`).**
  Matrix construction (decoder SVD, rotation) is not real-time-safe, so it
  runs on a worker thread and the result is published to the audio thread
  through a single-reader RCU handoff — the audio thread does two atomic
  loads and never waits; the worker does all freeing. Until the first build
  lands, the processor emits silence (decoder) or passes through (rotator).
- **Click-free parameter changes.** Coefficient tables ramp linearly over
  `k_smoothing_samples` (128); decode/rotation matrices crossfade over
  `k_fade_samples` (256); the doppler delay glides with a one-pole slew
  (producing a real Doppler pitch bend on distance jumps); binaural volume
  ramps per block. All of this is visualized in
  [`notebooks/dsp_behavior.ipynb`](../notebooks/dsp_behavior.ipynb).

**Threading model.** Setters run on one control thread; `process()` on
exactly one audio thread. Setters are safe to call while audio runs. For
offline/exact rendering where you want the change applied *now* with no
ramp, call `snap_parameters()` (or `wait_for_settling()` for the
worker-built matrices).

## Processor lifecycle

Every runtime processor follows the same shape:

1. **Construct** with the ambisonics order. This validates the order and
   allocates order-sized buffers. Out-of-range orders throw
   `std::invalid_argument` (or, in the embedded no-exceptions profile,
   assert in debug and clamp in release — see `validated_order`).
2. **`prepare(...)`** with the sample rate and/or block size where a
   processor needs it (binaural, doppler, compressor, the analysis layer).
   This is the last allocating call; do it before audio starts.
3. **Set parameters** from the control thread at will (`set_direction`,
   `set_speakers`, `set_head_orientation`, …). Matrix-building setters
   submit a worker job and return immediately.
4. **`process(...)`** on the audio thread, once per block. Wait-free.
5. For offline use, **`snap_parameters()` / `wait_for_settling()`** to
   collapse ramps and drain pending rebuilds so output is exact.

## The embedded real-time profile

A freestanding subset of the above runs on bare-metal Cortex-M55 and Hexagon
AudioReach: float32 throughout, no exceptions, no threads, no Eigen on the
audio path. Matrix *construction* moves to the control side
(`compute_sh_rotation` builds rotation matrices on-device; decode matrices
are precomputed) and `dsp::matrix_applier` / `dsp::binaural_core` apply them.
Full definition, budgets, and the CI gate: [`EMBEDDED.md`](EMBEDDED.md).

## Where to look next

- **Per-class API** — the generated reference (the modules/namespaces list).
- **Correctness evidence** — [`AUDIT.md`](AUDIT.md) and the notebooks in
  `notebooks/`.
- **Ecosystem comparison** — [`COMPARISON.md`](COMPARISON.md).
- **Embedded** — [`EMBEDDED.md`](EMBEDDED.md).
