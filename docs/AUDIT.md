# AmbiTap — Complete Audit & Review

> **Remediation status (updated 2026-07-02, same branch):** all P0 items from
> the plan in §6 are now fixed on this branch and covered by regression tests —
> C1 (degenerate hulls detected; planar layouts get true 2D pairwise VBAP),
> C2 (EPAD singular-value truncation, N3D basis, gain matched to
> mode-matching), C4 (directional_loudness beam calibration), C5/C6
> (constructor validation everywhere + `tests/test_hardening.cpp`), C7
> (head tracking counter-rotates via the inverse orientation), B1 (ALLRAD
> 4π scale removed; shared absolute-gain convention), B2/B3 (mode-matching
> and EPAD built in N3D), B5 (energy-normalized max-rE variant used by the
> decoders), B8 (SOFA receiver/delay validation, rank-revealing SH solve,
> leak-safe load). B6: the **generator** bug is fixed
> (`scripts/generate_hrtf.py`, single phase-continued projection per bin),
> but the embedded MagLS dataset still needs regenerating against the MIT
> KEMAR source SOFA, which is not reachable from this environment — run the
> script per its docstring and re-enable `HrtfData.DISABLED_MaglsDatasetIsCausal`.
> Suite: 87 tests green, ASan+UBSan clean. C3 (wait-free publishing) and the
> remaining P1/P2 items are still open.
>
> **P1 remediation (same day, same branch):** C3 is fixed — publishing now goes
> through `dsp/util/rt_published.h` (single-audio-reader RCU: raw atomic
> pointer + epoch grace period; the audio thread never locks, allocates, or
> frees — the worker does all freeing and absorbs the grace wait). B9/B10 are
> fixed via `dsp/util/smoothing.h`: element-atomic target tables with
> audio-side linear ramps (`k_smoothing_samples`), atomic scalars elsewhere,
> and an atomic UI snapshot in `energy_vector`. B12 is fixed: rotator/decoder
> matrices crossfade in over `k_fade_samples` (products carry their fade-from
> matrix), encoder/mirror/virtual-mic/directional-loudness coefficients ramp,
> doppler's delay glides (a real Doppler pitch glide), and binaural volume
> ramps per block; `snap_parameters()` covers offline/exact use. B15's
> `async_rebuilder` doc/API contradictions are resolved (`read_lock()` for the
> audio path, `peek()` for UI/tests; `wait_for_settling` now also waits for
> `on_publish`). Q2/Q5/Q13 addressed in passing (pitch/roll doc, denormal
> guard in the compressor envelope, `uint64_t` sequence counters). Evidence:
> `tests/test_dsp_threads.cpp` (setter-hammer and destroy-while-building
> stress, torn/UAF product check) and `tests/test_rt_safety.cpp` (replaced
> global `operator new`/`delete` proving the process paths allocation- and
> free-free), plus ASan+UBSan and TSan legs in CI. 97 tests green under all
> three sanitizers.
>
> **P2 remediation (same day, same branch):** install/export rules +
> `AmbiTapConfig.cmake` make the README's `find_package(AmbiTap)` claim true
> (both the fetched-Eigen and system-Eigen paths are smoke-tested by a CI job
> that installs to a prefix and builds a downstream consumer). CI grew to a
> GCC/Clang/AppleClang/MSVC matrix with `AMBITAP_WERROR=ON`, the sanitizer
> legs, a SOFA build leg, and example smoke runs; the tests/examples/bench
> build against a strict warnings target (`-Wall -Wextra -Wpedantic
> -Wconversion -Wshadow`, `/W4 /permissive-`) warning-free. B7 is fixed:
> `binaural_renderer::prepare(block, sample_rate)` resamples the built-in
> 44.1 kHz FIRs to the host rate (windowed-sinc, `math/binaural/resample.h`),
> with a centroid-scaling regression test. New `examples/`
> (encode→rotate→decode; binaural WAV render) and dependency-free `bench/`
> (µs/block per processor per order + rebuild latency). Hygiene: the stale
> foreign `.clang-format` was replaced with a config matching the house style
> (plus `.clang-format-ignore` for generated tables — a one-time reformat
> commit is needed before CI can enforce it); the `BUILD_TESTS` cache clobber
> is gone; GTest prefers a system install (`FIND_PACKAGE_ARGS`); test
> discovery/run timeouts added; the deprecated Eigen `FetchContent_Populate`
> path is version-gated away on CMake ≥ 3.28; the HRTF tables are `inline
> constexpr` (one copy per binary, generator updated to match); `M_PI` is gone
> from library headers (`k_pi` in coords.h, plus a shared `to_spherical`
> helper); README contradictions (B14) fixed and the RT contract documented.
> 99 tests green under plain, ASan+UBSan, and TSan.
>
> **B6 follow-up:** an end-to-end dry run of the generator against a synthetic
> KEMAR-like SOFA file exposed a second, deeper cause of the time-aliasing:
> reusing the previous bin's phase (even single-projection) freezes the phase
> trend, collapsing group delay to zero above the cutoff and aliasing the
> measurements' ~30-tap bulk delay into pre-onset energy (67% on the synthetic
> set). The generator now continues the phase trend by linear extrapolation
> from the previous two bins (exact for a pure delay; 67% → 2% pre-onset with
> identical magnitude accuracy on the dry run), refuses to write any dataset
> with > 5% pre-onset energy, embeds the source file's SHA-256 for provenance,
> ships a step-by-step regeneration procedure in its docstring plus
> `scripts/requirements.txt`, and fails with instructions instead of a
> traceback when the SOFA file or dependencies are missing.
>
> **B6 closed:** the author regenerated the dataset against the real MIT
> KEMAR SOFA (SHA-256 e70359…, embedded in the header) with the corrected
> generator — MagLS pre-onset energy is now 0.49% (was 35.8%), verified
> independently from the embedded tables, with unchanged ILD/ITD signatures
> (±9.6 dB / correct ear ordering at ±90°). `HrtfData.MaglsDatasetIsCausal`
> is enabled and green; no disabled tests remain.
>
> **Notebooks parity (follow-up):** the last SampleRateTap parity gap is
> closed — `tools/capi/` (a minimal C ABI shared library, ctypes-loadable)
> plus three executed verification notebooks in `notebooks/` whose assert
> cells re-run the audit's key checks against the real C++ implementation:
> SH-vs-SciPy, rotation property, VBAP energy normalization, decoder
> rE/energy/error gates, absolute-gain and EPAD-rank gates, a NumPy
> pseudoinverse cross-check, HRTF causality/ILD/ITD, resampler response, and
> convolver-vs-direct equivalence. Two further notebooks extend coverage to
> the remaining layers: `dsp_behavior.ipynb` verifies the real-time contract
> end-to-end (128-sample encoder ramps, 256-sample decoder matrix crossfade,
> Doppler shift within 1% of 1 ± v/c plus the delay-slew glide on distance
> jumps, compressor static-curve slopes exactly 1 and 1/ratio with configured
> attack/release clocks), and `soundfield_analysis.ipynb` verifies the
> `analysis/` layer against ground truth (heatmap peaks within one grid cell
> at the correct relative level; energy-vector DOA median error 1.4° on a
> moving source) and adds an order-1–5 sharpness study (max-rE beamwidth
> 224° → 74°, ALLRAD |rE| monotone to > 0.9).
>
> **Embedded profile (follow-up):** the RT paths now build for bare-metal
> Cortex-M55 and Hexagon AudioReach targets, machine-checked by a CI
> cross-compile gate (`tools/embedded/rt_core_check.cpp`; -fno-exceptions,
> -fno-rtti, -Wdouble-promotion, newlib-nano link). Enabling work: the
> crossfading matrix application was extracted from rotator/decoder into
> freestanding `dsp::matrix_applier`; the binaural convolver bank + volume
> ramp into freestanding `dsp::binaural_core` on a new float32 convolver
> path (`partitioned_convolver32` / `real_fft32` / fftsg_float.c — the
> double path is software floating point on these FPUs); `validated_order`
> gained an assert/clamp no-exceptions mode; and the spatial compressor's
> per-sample `log10f`/`powf` were replaced with polynomial `fast_log2` /
> `fast_exp2` (measured error < 1e-4 dB, verified against the exact libm
> formula in tests) plus a compare-only below-threshold fast path. Budgets
> and the remaining embedded gaps are tracked in docs/EMBEDDED.md.
> Both headline gaps are now closed: the **shared-spectrum convolver bank**
> (`convolver_bank`, one forward FFT per channel + one inverse per ear)
> replaced the per-ear convolver arrangement inside `binaural_core` —
> measured 2.7× (order-5 binaural 67.9 → 25.0 µs/block) and the enabler
> for order-5 binaural on M55 — and **`compute_sh_rotation`**
> (`math/core/rotation_recurrence.h`, Ivanic–Ruedenberg with erratum,
> freestanding) builds rotation matrices on-device. The recurrence also
> replaced the sampling/least-squares backend of the desktop `sh_rotation`
> — exact up to roundoff, dependency-free, faster to build — **closing
> deferred item Q1**; it was validated against the LSQ construction
> (≤ 1.2e-6 disagreement through order 10) before the swap, and the
> in-tree tests gate on the defining property Y(R·d) = R·Y(d) directly.
> 113 tests green.
>
> **Cross-library comparison (follow-up):** docs/COMPARISON.md +
> notebooks/library_comparison.ipynb verify AmbiTap against spaudiopy and
> pyshtools through exactly-derived convention maps: SH basis (three
> independent references, <= 1.4e-6), rotation (Ivanic-Ruedenberg vs
> Wigner-D, 9.6e-7), max-rE weights (identical), VBAP (same gains to
> 2.9e-7 where triangulation is unambiguous; Pulkki invariants exact for
> both), mode-matching and EPAD decoders (matrix-identical up to a
> +1.63 dB convention constant, residual < 5e-7). The comparison also
> CAUGHT A REAL BUG: the incremental convex hull mis-triangulated exactly
> coplanar quad faces (cube faces), folding triangles through the interior
> so ~20% of horizon directions on the cube snapped to a single speaker up
> to 52 degrees away. Fixed by deciding hull topology on deterministically
> radius-lifted points plus a least-violating-triangle VBAP fallback;
> regression tests now enforce hull convexity and the velocity-vector
> invariant on the pathological layouts. All notebooks re-executed against
> the fixed hull; 116 tests green.
>
> **All remediation items closed:** libmysofa is pinned to the v1.3.4 commit
> SHA (resolved by the author); the one-time `clang-format` reformat landed
> with a config verified idempotent against the whole tree, and CI now
> enforces formatting. Deferred with rationale: P3 polish items not
> otherwise picked up along the way. (Q1 — rotation via least-squares fit —
> was deferred here originally and has since been closed by the
> Ivanic–Ruedenberg recurrence; see the embedded-profile note above.)

**Date:** 2026-07-02
**Scope:** every header under `include/ambitap/`, all tests, scripts, build system, and CI — reviewed line-by-line, with SampleRateTap as the quality/completeness bar.
**Method:** five parallel subsystem reviews (math core, decoder design + geometry, DSP processors, binaural/FFT stack, tests/infrastructure), followed by an independent verification round: every critical finding below was reproduced with small programs compiled against these exact headers, or with AddressSanitizer. Findings that were not independently re-reproduced are marked *(reported)*; everything else is **(verified)**.

**Build ground truth:** Release build clean; full suite **68/68 tests pass**, and passes again under ASan+UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`) with zero findings. Library headers compile warning-clean under SampleRateTap's full warning set (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`); the only warnings in the whole tree are two in tests (`-Wdangling-else` at `tests/test_geometry.cpp:73`, `-Wconversion` at `tests/test_core.cpp:50`).

---

## Executive summary

The foundations are genuinely good: the spherical-harmonics core is **numerically correct** (verified against an independent double-precision reference through order 10, including poles and orthonormality under exact quadrature), the t-design tables satisfy their quadrature property exactly, the Ooura FFT wrapper and the partitioned overlap-save convolver are correct (verified against direct convolution to ≤5e-7), the FuMa conversion factors are right, and the code is warning- and sanitizer-clean.

The problems are concentrated one layer up, and several are serious:

1. **Six of the eight shipped speaker presets are silently broken for VBAP/ALLRAD** — the 3D convex hull degenerates on coplanar (2D) layouts and every pan collapses to nearest-speaker snapping, with no error.
2. **The headline "wait-free real-time process()" claim is false** — the audio path takes a mutex (`std::atomic_load` on `shared_ptr` is lock-based on all mainstream standard libraries) and can free heap memory on the audio thread.
3. **Decoder absolute gains are mutually inconsistent** (ALLRAD ~+22 dB vs mode-matching) and EPAD emits energy on channels a layout cannot reproduce.
4. **Head-tracking rotates the scene the wrong way**, and the shipped MagLS HRTF dataset is time-aliased (36% of its energy lands before the acoustic onset).
5. **No input validation anywhere** — several one-liner misuses (`encoder(11)`, `binaural_renderer(6)`) are stack/global buffer overflows, ASan-provable.

The current test suite (fast, property-based, all passing) cannot see any of this: it never leaves the order-1 cube layout, never asserts an absolute gain, never checks a physical direction convention, and has zero concurrency, hardening, or sanitizer coverage. That evidence layer — quantitative quality gates, hardening tests, thread-stress tests, sanitizer/multi-compiler CI, benchmarks, docs, examples — is exactly what SampleRateTap has and AmbiTap lacks.

---

## 1. Critical findings

### C1 (verified) — Convex hull degenerates on coplanar layouts; VBAP/ALLRAD silently broken on 2D presets
`include/ambitap/math/geometry/convex_hull.h:33,59-68`

When all points are coplanar, no candidate `p3` has nonzero plane distance, so `p3` keeps its initialization value `0` (duplicating `p0`) and the "initial tetrahedron" contains a repeated vertex. Reproduced on `layouts::surround_5_1()`:

```
5.1 hull: 4 triangles: (0,4,3) (0,0,4) (4,0,3) (0,3,0)   ← duplicate indices, zero volume
5.1 vbap az=20° gains: 1.000 0.000 0.000 0.000 0.000      ← should split C/L; snaps to L
```

`speaker_layout` (`speaker_layout.h:55`) then inverts singular basis matrices (inf/NaN stored) and **every** `vbap_gains()` call falls through to the nearest-speaker fallback. Consequently `compute_allrad_decoder` on any 2D layout is a nearest-neighbor snap decoder with grossly asymmetric matrices. Stereo/3-speaker layouts get zero triangles (`convex_hull.h:30`) with the same silent snapping. Six of the eight presets in `layouts.h` (stereo, quad, 5.1, hexagon, octagon, 7.1) are 2D. No error is reported and no 2D pairwise-VBAP fallback exists.

**Fix:** detect rank < 3 point sets and either implement pairwise (2D) VBAP or fail loudly; reject <4-point inputs explicitly.

### C2 (verified) — EPAD lacks singular-value truncation; rank-deficient layouts decode garbage
`include/ambitap/math/decoding/epad.h:55-59`

`D = U·Vᵀ` uses all singular pairs. For σ=0 the corresponding U/V columns are arbitrary orthonormal completions, yet they enter `D` with unit weight. Reproduced: octagon at order 1 — the SH matrix's Z column is exactly zero (the layout cannot reproduce height), but:

```
EPAD octagon o1 col norms W,Y,Z,X = 1.000 1.000 1.000 1.000   ← Z must be 0.000
```

Height content in the input is redistributed to a numerically arbitrary direction. Published EPAD (Zotter & Frank) explicitly truncates to significant singular values. Affects every 2D layout at any order ≥ 1 and any rank-deficient case. The existing test `Epad.ColumnsAreUnitary` passes *even with* this bug, since U·Vᵀ is unitary-columned regardless.

**Fix:** drop singular pairs below a relative threshold (e.g. σᵢ < ε·σ₀).

### C3 (verified) — "Wait-free real-time process()" is actually lock-based, and the audio thread can free memory
`include/ambitap/dsp/util/async_rebuilder.h:78-80,116`; consumers: `dsp/decoder.h:120,138`, `dsp/rotator.h:82,99`, `dsp/binaural_renderer.h:168`, `analysis/soundfield_grid.h:133`

- `load()` uses `std::atomic_load_explicit(&m_active, …)` — the `shared_ptr` atomic free functions are **not lock-free** on any mainstream stdlib. Verified on this toolchain: `std::atomic_is_lock_free(&sp) == 0`. libstdc++ implements them via a global mutex pool — the *same* mutex the worker's `atomic_store_explicit` takes when publishing. If the worker is preempted holding it, the audio thread blocks in a futex: classic unbounded priority inversion. In `decoder::process_frame` this happens **per frame**.
- Additionally, `load()` returns a `shared_ptr` copy; when the worker publishes a replacement while the audio thread holds the old one, the audio thread drops the **last** reference at block end and runs `operator delete` (of an Eigen matrix, or the soundfield grid's table) on the audio thread. *(reported; mechanism confirmed by code inspection)*

The README (`README.md:29`) and doc comments (`async_rebuilder.h:73`, `decoder.h:37`, `rotator.h:31`, `encoder.h:22`) all state wait-free/allocation-free claims that this violates.

**Fix:** replace the atomic-`shared_ptr` publish with a genuinely wait-free scheme (pre-allocated double buffer + generation counter/seqlock, or hazard-pointer/retire-list so frees always happen on the worker).

### C4 (verified) — `directional_loudness` gain is miscalibrated by (order+1); attenuation actually inverts and boosts
`include/ambitap/dsp/directional_loudness.h:68-90`

The projection `out = in + (g−1)·(Yᵀx)·Y` omits the 1/(YᵀY) = 1/(order+1) normalizer. Achieved gain at the look direction is `1 + (g−1)(order+1)`. Reproduced at order 3, source exactly at look direction:

| requested | achieved |
|---|---|
| 0.0 | **−3.0** (inverted, +9.5 dB louder) |
| 0.5 | **−1.0** (inverted) |
| 2.0 | **+5.0** |

The doc ("gain < 1: attenuate", "Linear gain at the look direction") is wrong on both counts, and the existing test re-implements the same broken formula (circular). **Fix:** `delta = (m_gain − 1.f) * extracted / float(m_order + 1)`.

### C5 (verified) — `binaural_renderer` out-of-bounds reads for order > 5 and for malformed custom HRTFs
`include/ambitap/dsp/binaural_renderer.h:80-83,141-152,262-270`

The constructor accepts any order; `hrtf_fir` indexes the 36-channel built-in tables unchecked. `binaural_renderer bin(6); bin.prepare(64);` → ASan `global-buffer-overflow … past 'builtin_hrtf_left' (size 18432)` via `convolution.h:74 ← rebuild_convolvers ← prepare` (reproduced). Related *(reported)*: `set_custom_hrtf` never validates per-ear FIR counts/lengths, and `hrtf_fir` returns the **left**[0] length even for the right ear — undersized custom sets are the same OOB class.

**Fix:** validate order ∈ [1, builtin_hrtf_order] when no custom HRTF is set (throw, matching SampleRateTap's constructor-validation convention); validate custom-HRTF shapes.

### C6 (verified) — No precondition validation anywhere; `evaluate_sh` overflows the stack for order > 10
`include/ambitap/math/core/spherical_harmonics.h:32-40`; reachable from `dsp/encoder.h:34`, `mirror.h`, `virtual_mic.h`, `directional_loudness.h`; analogous OOB in `format_converter.h:112-137` for order > 3

`plm` is a fixed 121-float stack array with loops bounded by caller-supplied `order`; `evaluate_sh(11, …)` writes out of bounds (stack corruption — UB, not a reliable crash), and every DSP constructor passes its order argument straight through. `format_converter(4)` indexes `k_fuma_to_acn[9..24]` OOB. There is not a single `throw` or even `assert` on any constructor input in the library. SampleRateTap's bar: explicit `std::invalid_argument` validation plus a hardening regression suite.

### C7 (verified) — Head tracking rotates the scene *by* the head orientation instead of its inverse
`include/ambitap/dsp/binaural_renderer.h:129-134` → `rotator.h:20-25`

`set_head_orientation(yaw,…)` forwards angles unmodified to the (correct) forward scene rotator. Reproduced: source encoded at front, `set_head_orientation(+π/2, 0, 0)` (head turned left) →

```
E_L=1.733  E_R=0.474      ← the front source must land at the RIGHT ear
```

Compensating for head rotation requires the inverse rotation — and for combined yaw/pitch/roll, negating angles is insufficient; the Euler composition must be reversed (transpose the matrix). **Fix:** invert in the renderer, or rename/redocument the API as a scene rotation.

---

## 2. Bugs

### Decoder design

- **B1 (verified) — ALLRAD is ~4π (+22 dB) hotter than mode-matching.** `allrad.h:72` uses `weight = 4π/V`; with the library's SN3D basis plus the (correct) per-order `(2n+1)` fix-up, the true sampling decoder is `(1/V)·Y·diag(2n+1)`. Reproduced: decoding a unit omni field on the cube gives summed pressure **17.28 via ALLRAD vs 1.00 via mode-matching**; elementwise the decoder equals `4π · pinv` *(reported: ratio exactly 4π = 12.566)*. The doc's "closed-form pseudoinverse" claim (`allrad.h:54-56`) holds only for physics-orthonormal SH, not the ambisonic convention where Y₀₀ = 1. The library's decoders are mutually inconsistent in absolute gain; no test asserts absolute gain anywhere.
- **B2 *(reported)* — Mode-matching pseudoinverse computed in SN3D is wrong in the least-squares regime.** `mode_matching.h:44-56`. For L ≥ C full-rank layouts the result is normalization-invariant, but for L < C (e.g. 5.1 at order ≥ 2 — the common "few speakers, high order" case) the SN3D residual under-weights order-n error by (2n+1). Measured 18.7% relative Frobenius deviation from the proper N3D-weighted decoder on 5.1/order-2. Canonical mode matching is defined on the orthonormal (N3D) basis; there is no conversion or column weighting in the file.
- **B3 *(reported)* — EPAD computed in SN3D deviates from canonical EPAD even for full-rank layouts.** `epad.h:46-59`. The polar factor U·Vᵀ is basis-dependent; measured 67% relative deviation from the canonical N3D construction on 7.1.4/order-2, under-weighting high orders ≈ 1/√(2n+1) — collapsing decodes toward omni.
- **B4 *(reported)* — ALLRAD has no imaginary-speaker support.** Layouts with coverage holes (domes, 5.1, 7.1.4) send below-horizon virtual sources through near-singular ear-level triangles or nearest-speaker snaps instead of inserting an imaginary speaker at the hole and discarding its row (standard ALLRAD practice).
- **B5 *(reported)* — max-rE weighting has no energy-preserving normalization.** `max_re.h`, `epad.h:61-67`. The per-order weights themselves are correct (verified against exact Legendre-root values), but enabling `use_max_re` simply attenuates (~3 dB at order 1) and destroys EPAD's defining DᵀD = I property.

### Binaural

- **B6 (verified) — The shipped MagLS dataset is time-aliased.** `scripts/generate_hrtf.py:154-169` (`MAGLS_ITERS = 50`), data in `hrtf_data.h`. Measured from the embedded tables: pre-onset energy **35.8%** for MagLS vs 0.0% for LS; last-8-tap energy 17.4% vs 0.13%; MagLS W-channel peaks at sample 3 vs 46 for LS — circular wrap-around from an IFFT of a spectrum inconsistent with a compact causal 128-tap IR. Cause: 50 alternating-projection iterations per bin converge to an arbitrary phase fixed point, defeating the previous-bin phase-continuation that Schörkhuber/Habets MagLS (one projection per bin) relies on. Audible consequence: pre-echo/smeared transients above the ~3.1 kHz cutoff. **Fix:** single projection per bin (`n_iter=0` beyond the phase carry), or design on a longer FFT and window to 128 taps.
- **B7 *(reported)* — No sample-rate handling in the binaural path.** The built-in HRTFs are 44.1 kHz (`builtin_hrtf_sample_rate`) but `prepare(block_size)` never learns the host rate: at 48 kHz all spectral cues shift up ~8.8% and ITDs shrink. Not resampled, not documented at the renderer level.
- **B8 *(reported)* — `load_sofa` trusts its input's shape.** `sofa_reader.h:131-136` hard-codes 2 receivers without checking `hrtf->R` (misindexing/OOB for R≠2 files); `Data.Delay` is ignored (separately-stored TOA → broken ITD); no sample-rate check. Also `sofa_reader.h:69-71`: `decompose_sh` solves unguarded normal equations via LDLT — for insufficient measurement grids YᵀY is singular and the result is silent NaN/garbage (the Python generator correctly uses SVD `pinv`); `Yᵀ·Identity(M,M)` also materializes a pointless M×M product.

### Concurrency & real-time behavior

- **B9 *(reported)* — Unsynchronized setter/process data races on every non-async processor.** `encoder.h:44-56,87`, `mirror.h:47-49`, `virtual_mic.h:46-58`, `directional_loudness.h:48-58`, `format_converter.h:81-84,112-137` (a torn permutation table misroutes channels), `doppler.h:59`, `spatial_compressor.h:57-73`. The async-rebuilt processors got proper publication; these siblings have no atomics, no double-buffering, and no documented single-thread contract — under the library's own threading model this is UB and audibly a half-updated SH table.
- **B10 *(reported)* — `energy_vector::value()` returns a reference to state written per-sample by the audio thread** (`energy_vector.h:86`) — the explicitly UI-facing read is a torn-read data race. (`soundfield_grid` does this correctly with atomics.)
- **B11 *(reported)* — Live reconfiguration of `binaural_renderer` is use-after-free, not a "glitch".** `binaural_renderer.h:36-38` promises "a short glitch when switching datasets live", but `rebuild_convolvers()` destroys the `unique_ptr<partitioned_convolver>` vector that `process()` may be iterating.
- **B12 *(reported)* — No click-free parameter transitions anywhere.** Rotator/decoder matrices hard-swap at block boundaries (head-tracking yaw becomes a staircase → zipper noise); encoder/mirror/virtual-mic coefficient tables jump instantaneously; `doppler` computes `delay_samples` once per block, so the promised per-sample Doppler shift (`doppler.h:20-22`) is actually block-rate read-pointer jumps (clicks), with no distance ramp.

### Analysis & documentation contradictions

- **B13 *(reported)* — `soundfield_grid` elevation rows contradict the doc:** bottom row is −π/2+π/el_steps, nadir never sampled, zenith duplicated az_steps times (`soundfield_grid.h:210-214` vs doc at :36-37). Divide by `el_steps − 1` or use cell centers.
- **B14 (verified) — README contradictions.** `README.md:46` advertises `find_package(AmbiTap)` but the build has **no `install()`/`export()`/config-file rules at all** — the claim is false as shipped. `README.md:59-60` says "FuMa conversion lives in the wrapper targets, not here" — contradicted by `dsp/format_converter.h`. The "What's here" tree omits `dsp/` and `analysis/`, which the prose calls the main product.
- **B15 *(reported)* — `async_rebuilder` doc bugs:** `rebuild_synchronously` claims to build inline but actually submits to the worker and waits (`async_rebuilder.h:93-97`); `wait_for_settling` can return before the publish callback has run (`:122-128`) — the tests work around this with a sleep loop (`test_dsp_matrix.cpp:130-135`).

---

## 3. Quality issues

| # | Where | Issue |
|---|---|---|
| Q1 | `rotation.h:27,62-126` | SH rotation is a float least-squares fit over sampled directions (LDLT of YᵀY), not the exact Ivanic–Ruedenberg recurrence; accuracy is fine (~1e-6 at order 10, verified) but the doc claims it's "more robust" than IR, which is backwards, and construction redundantly re-evaluates lower bands (~order× wasted work). |
| Q2 | `rotation.h:41-49` | Yaw/pitch/roll convention undocumented; measured behavior is intrinsic Z-Y′-X″ with **positive pitch = front-down** — opposite of most ambisonics tools (IEM). Nothing in docs or tests pins it. |
| Q3 | `mode_matching.h:55-56` | No regularization/singular-value floor — irregular layouts (5.1's 140° rear gap) produce unbounded panning gains. |
| Q4 | `convex_hull.h:131,174-180` | Visibility epsilon 1e-7f is float-noise level (tests openly waive overlapping-split defects on the cube); new-face orientation test vs the origin is decided by noise for faces through the origin (dome layouts) — currently works by luck. |
| Q5 | `spatial_compressor.h:81`, `energy_vector.h:57-59` | Denormals unmitigated in envelope tails — CPU spikes on x86 without FTZ/DAZ. |
| Q6 | `hrtf_data.h:34,687,1340,1993` | Big tables are `constexpr` (internal linkage) — every TU gets its own 73.7 KB copy; should be `inline constexpr`. |
| Q7 | `doppler.h:118`, `soundfield_grid.h:206`, `max_re.h:24` | `std::fill` without `<algorithm>`; non-standard `M_PI` (breaks MSVC without `_USE_MATH_DEFINES`). |
| Q8 | `ooura_fft.h:30-34` | Packing doc omits that Ooura uses the e^{+iωt} convention — `forward()` output is `conj(numpy.rfft)` (verified); harmless internally, will bite users computing phase. Size checks are assert-only. |
| Q9 | `coords.h` | No spherical↔cartesian helpers; the conversion is hand-duplicated (consistently, verified) in three places. |
| Q10 | `.clang-format` | A stale multi-language config from an unrelated project (Java import groups, "AOEM Audio Group"); the code doesn't follow it (40 violations across 3 sampled files). |
| Q11 | `CMakeLists.txt:79,81,40-43` | libmysofa pinned to a movable tag (Eigen/GTest are properly SHA-pinned); `set(BUILD_TESTS OFF CACHE BOOL "" FORCE)` clobbers a generic consumer variable; deprecated `FetchContent_Populate` pattern (breaks ≥ CMake 3.30 eventually). Tests' GTest fetch has no `FIND_PACKAGE_ARGS` fallback to a system install. |
| Q12 | `scripts/` | Generators are well-documented but not reproducible: no dependency pins, no input checksums, t-designs fetched over plain `http`, and no CI freshness check that `hrtf_data.h`/`tdesigns.h` match their generators. `generate_tdesigns.py` never verifies the quadrature property of downloaded data. `generate_hrtf.py` uses unweighted, unregularized LS on the strongly non-uniform KEMAR grid (dense horizon, empty bottom cap) — standard practice is area-weighted + Tikhonov. |
| Q13 | `async_rebuilder.h` | `m_pending_seq` is `int` (theoretical overflow); otherwise the worker lifecycle (join-in-destructor, acquire/release pairing, coalescing) is sound. |
| Q14 | `virtual_mic.h`, `decoder.h` | Look-direction gain convention (order+1, changes with max-rE) undocumented; in-place aliasing rules documented on some processors but not decoder/binaural. |

**Verified correct along the way** (no action needed, recorded for confidence): SH values/normalization through order 10 incl. poles (max err 1e-6 vs double reference); SN3D↔N3D factors exact; ACN indexing exact; rotation matrices orthogonal & satisfy Y(Rd)=R·Y(d) to 1e-6; all ten t-design tables satisfy their quadrature identity (≤2.6e-6) with correct selection logic; layout preset angles match ITU/Dolby; VBAP Σg²=1 convention; Ooura wrapper packing/scaling/sizing contract; partitioned convolver vs direct convolution ≤5e-7 across block/IR-size matrix incl. ring wraps and mid-stream `set_ir`; FuMa↔SN3D factors (cross-checked against two external references); mirror parity logic; envelope attack/release coefficient math; doppler buffer sizing/clamping; `process()` paths of convolver/renderer/compressor genuinely allocation-free after prepare; HRTF table structure/metadata consistent with correct ear/azimuth assignment (note: right ear is an exact mirror of left — the source data was symmetrized).

---

## 4. Test-suite assessment

The suite is fast (0.24 s), all-green, and property-based in style — but it is **calibrated to miss every finding above**:

- **Decoding is only ever tested on the order-1/order-3 cube** — a 3D, near-uniform layout. No test touches any 2D preset through hull/VBAP/decoders (C1, C2 reachable with a single 5.1 or octagon test).
- **No absolute-gain assertion anywhere** (B1's 22 dB is invisible; `Allrad.EnergyRoughlyUniformAcrossDirections` is scale-invariant). No decoder comparison against published reference matrices, and no mode-matching test in the L < C regime (B2), no EPAD test on irregular/rank-deficient layouts (B3/C2).
- **No physical-convention pinning:** rotation tests build their reference with the same `Rz·Ry·Rx` composition as the implementation (a sign flip passes); no "yaw +90° moves front source left" test; no head-tracking direction test (C7); quaternion constructor and orders > 5 untested.
- **Circular tests:** `DirectionalLoudness` (re-implements the broken formula — C4 unreachable), `DspEncoder.CoefficientsMatchEvaluateSh` and `DspDecoder.MatchesDirectConstruction` (compare components against themselves; acceptable as wiring checks only).
- **Zero concurrency coverage** for `async_rebuilder`/decoder/rotator/soundfield_grid — every test serializes via `wait_for_settling()`. No TSan anywhere (C3/B9 would light up immediately).
- **Zero hardening coverage:** no invalid-order rejection, NaN/Inf inputs, zero-length blocks, degenerate layouts, custom-HRTF shape validation, SOFA path (never even compiled in CI), unprepared-use beyond happy-path.
- **No quantitative quality gates:** binaural tests assert "output changed" (`diff > 1e-4`) rather than "output is correct to X dB"; no MagLS causality/latency bound (B6 detectable with a one-line pre-onset-energy assertion); no Ooura sign/packing test (a conjugation error cancels in the round-trip test); no renderer-vs-direct-convolution SNR above order 1.
- **Good tests worth keeping:** SH orthonormality under t-design quadrature, Y(R·d)=R_sh·Y(d), VBAP energy normalization, mirror property tests, and especially `PartitionedConvolver.MatchesDirectConvolution` (200-tap IR, partial partitions, ring wraps) — this is the right kind of test; the suite needs more of exactly this.

---

## 5. Parity gaps vs SampleRateTap

| Area | SampleRateTap | AmbiTap |
|---|---|---|
| CI | 542-line matrix: GCC/Clang/AppleClang/MSVC, ASan+UBSan+TSan, WERROR, QEMU cross-targets, icount ratchet, clang-format gate, SHA-pinned actions; plus scheduled arm64 TSan, compare, book-pages workflows | 26 lines: ubuntu+macos, default compilers, Release only, nothing else |
| Warnings | `srt_warnings` INTERFACE target + `SRT_WERROR` option, enforced in CI | No flags at all (code already passes SRT's set — two 1-line test fixes needed) |
| Hardening tests | 267-line `test_hardening.cpp`, constructor validation, each test tagged to an audit finding | None; no validation to test |
| Concurrency tests | Dedicated two-thread 10M-element stress + TSan legs | None |
| Quality gates | Measured SNR thresholds in tests, published numbers in README | Qualitative "output changed" assertions |
| RT-safety evidence | noexcept push/pull, structural allocation-free tests | Claims in comments only; `process()` not even `noexcept` |
| Packaging | README advertises only what exists | README advertises `find_package` that doesn't exist |
| Docs | README + docs/ (COMPARISON, PERFORMANCE, HARDWARE_TESTING, Doxyfile) + full mdBook + 3 notebooks + 4 examples | README only (header doc-comments are good, though) |
| Benchmarks | bench/ + baselines.json + icount ratchet in CI + head-to-head vs libsamplerate/soxr | None |
| Repo hygiene | Clean 9-line .clang-format, CI-enforced; badges | Stale foreign .clang-format, no badges. (AmbiTap's THIRD_PARTY_NOTICES.md is exemplary — better than SRT needs.) |

---

## 6. Prioritized remediation plan

**P0 — correctness (do before anyone uses the outputs):**
1. Fix coplanar/degenerate convex-hull handling or add 2D VBAP; fail loudly on degenerate layouts (C1).
2. Add singular-value truncation to EPAD (C2); move mode-matching/EPAD to N3D-weighted construction (B2, B3); fix ALLRAD's 4π scale so all decoders share one absolute-gain convention, and add an omni-field absolute-gain test (B1).
3. Fix `directional_loudness` normalization (C4) and the head-tracking inversion (C7).
4. Constructor validation everywhere (order ranges, HRTF shapes, SOFA R/delay checks) + a `test_hardening.cpp` in the SampleRateTap style (C5, C6, B8).
5. Regenerate the MagLS dataset with corrected phase handling and add a pre-onset-energy test (B6).

**P1 — the real-time contract:**
6. Replace atomic-`shared_ptr` publishing with a wait-free, worker-frees-everything scheme (C3); make `process()` paths `noexcept`; add an allocation-free assertion test.
7. Give the non-async processors a real setter/process contract: atomics or double-buffered tables (B9), atomic UI reads in `energy_vector` (B10), and guard or redocument live `binaural_renderer` reconfiguration (B11).
8. Parameter smoothing: matrix crossfade in rotator/decoder, coefficient ramps in encoder-family, per-sample doppler delay ramp (B12).
9. Thread-stress tests (setters hammering against `process()`; destroy-while-building) + TSan CI leg.

**P2 — evidence layer (SampleRateTap parity):**
10. CI matrix: GCC/Clang/MSVC × Debug/Release, ASan+UBSan+TSan legs, `AMBITAP_WERROR` + warnings target, SOFA-enabled leg, clang-format gate (after replacing the stale config), SHA-pinned actions, timeouts.
11. Quantitative quality gates: binaural-vs-direct-convolution SNR in dB, decoder matrices vs published references, rotation convention pinned to physical directions; publish measured numbers in the README like SRT's quality table.
12. Either implement `install()`/export + `AmbiTapConfig.cmake` (handling the `ambitap_fft` static target and Eigen dependency) or delete the README claim (B14); fix the other README contradictions.
13. Sample-rate handling for the binaural path (resample HRTFs in `prepare`, or document loudly) (B7).
14. docs/ + Doxyfile over the existing header comments; an examples/ dir (encode→rotate→decode; binaural); bench/ with per-processor block costs across orders and a baselines file; generator reproducibility (pins, checksums, freshness check in CI).

**P3 — polish:** Q5–Q14 as listed (denormals, `inline constexpr` tables, `M_PI`/includes portability, coords helper, epsilon review, .gitignore, badges, small CMake fixes).

---

*Reproduction notes: all verification programs were compiled with `g++ -std=c++20 -O2 -I include -I build/_deps/eigen-src` against unmodified headers at this commit; ASan repro: `binaural_renderer bin(6); bin.prepare(64);`. Test-suite ground truth: 68/68 pass in Release and under ASan+UBSan.*
