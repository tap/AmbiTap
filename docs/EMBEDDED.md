# AmbiTap on embedded targets

Target profile for running AmbiTap's real-time paths on **Cortex-M55**
(bare-metal / RTOS, Helium MVE, single-precision FPU) and **Hexagon**
(Qualcomm AudioReach / AudioLite modules, v68+ cores with HVX float).

The short version: the per-sample math fits. What the embedded profile
enforces is *architecture* — no exceptions, no threads, no Eigen, no
hardware doubles in any audio path — and CI proves it stays that way.

## The RT profile

`tools/embedded/rt_core_check.cpp` is the machine-checked definition: CI
cross-compiles it for Cortex-M55 with
`-fno-exceptions -fno-rtti -Wdouble-promotion -Werror` and links it against
newlib-nano + nosys. Everything it instantiates is embedded-clean:

| In the profile (audio side) | Stays on the host/control side |
|---|---|
| `dsp::encoder`, `mirror`, `virtual_mic`, `directional_loudness` | decode-matrix construction (Eigen SVD) — precompute and ship |
| `dsp::spatial_compressor` (fast_math, no per-sample libm) | rotation-matrix construction (`sh_rotation`, Eigen LSQ) |
| `dsp::doppler`, `dsp::format_converter` | `async_rebuilder` / worker threads (`std::thread`) |
| `dsp::matrix_applier` — crossfading application of *precomputed* rotation/decode matrices | `binaural_renderer`'s resampling + orientation layer |
| `dsp::binaural_core` — float32 HRTF convolver bank + volume | `analysis::soundfield_grid` (O(C²·D) per block; a UI feature) |
| `analysis::energy_vector` | SOFA reader |

Ground rules, same as the desktop RT contract plus two:

- **Allocation only at construction / prepare time** (newlib heap is fine at
  init; every `process()` is allocation-free — enforced on the host by
  `tests/test_rt_safety.cpp`).
- **No exceptions**: with `-fno-exceptions` (or `-DAMBITAP_NO_EXCEPTIONS`),
  `validated_order` asserts in debug and clamps in release
  (`math/core/validate.h`).
- **Float32 everywhere on the audio path**: the binaural engine uses
  `partitioned_convolver32` / `real_fft32` (fftsg_float.c) — on these FPUs a
  double is a software-library call. `-Wdouble-promotion` in the CI gate
  keeps accidental doubles out.

The control-side splits follow the same pattern everywhere: the heavy
construction (Eigen, resampling, dataset selection) produces plain float
arrays, and a dumb freestanding applier consumes them. On a desktop the
construction runs on AmbiTap's worker threads; on M55 it runs offline or on
the control core; on Hexagon it runs in the module's command handler.

## Cortex-M55 budget (analytical — validate on an FVP before trusting)

Assumptions: 48 kHz, block B = 64 (FFT N = 128), FIRs resampled offline to
48 kHz (≈139 taps → 3 partitions), ~1.3 cycles/flop scalar, ~3k cycles per
128-point real FFT scalar. Helium column assumes a CMSIS-DSP-class f32 FFT
and vectorized MACs (~3–4× on these kernels).

| Workload | order 3 (16 ch) | order 5 (36 ch) |
|---|---|---|
| encode + matrix_applier decode (8 spk) | ≈ 11 MHz scalar | ≈ 30 MHz scalar |
| head-track rotation (dense C×C) | ≈ 16 MHz scalar | ≈ 80 MHz scalar |
| binaural, **current** per-ear convolvers (4C FFTs/block) | ≈ 180 MHz scalar | ≈ 405 MHz scalar — **does not fit** |
| binaural, **shared-spectrum bank** (C+2 FFTs/block) | ≈ 77 MHz scalar / ~25 MHz Helium | ≈ 167 MHz scalar / ~60 MHz Helium |

Memory (order 5 binaural): ≈ 330 KB RAM as-is, ≈ 170 KB with the shared
bank; HRTF tables are 72 KiB of flash (both ears, LS + MagLS is 144 KiB;
ship one projection, or an order-trimmed table, on smaller parts). The CI
gate's order-3 check binary — every profile processor including binaural —
is **80 KB flash / 10 KB static RAM** total.

Conclusions:

- **Order-3 binaural fits an M55 today**, scalar, without optimization.
- **Order-5 binaural on M55 requires the shared-spectrum convolver bank**
  (one forward FFT per channel + one inverse per ear, accumulating in the
  frequency domain — instead of a full FFT round-trip per channel *per
  ear*). That is a ~2–4× win and the single highest-value optimization in
  the library; it helps every platform, desktop included.
- After that, the FFT is the hot spot: `real_fft32` is the seam where a
  Helium-optimized FFT (CMSIS-DSP) drops in per-target.
- The dense rotation applier at order 5 is worth a block-diagonal variant
  (SH rotation only couples within an order: Σ(2n+1)² = 286 vs 1296
  MACs/sample — 4.5×).

## Hexagon (AudioReach / AudioLite)

- v68+ cores have **HVX float**; the scalar core alone covers the budgets
  above with a wide margin at aDSP clocks. Start scalar, `-mhvx` autovec
  later if profiling asks for it.
- Build the RT profile with `-fno-exceptions` (the `AMBITAP_NO_EXCEPTIONS`
  path) inside the CAPI module; Hexagon clang handles C++20.
- Threading: do **not** use `async_rebuilder` in a module. Run matrix
  builds in the module's command/control handler (they are milliseconds)
  and hand results to `dsp::matrix_applier` / `binaural_core` — both adopt
  new data click-free on the audio path.
- CI covers Hexagon only at the portability level (the M55 gate's
  no-exceptions/no-doubles/no-threads constraints are a superset of what
  Hexagon needs); cycle numbers need the Qualcomm SDK simulator, which
  cannot run in this repo's CI.

## Known gaps / next steps

1. **Shared-spectrum convolver bank** — required for order-5 binaural on
   M55; see budget above.
2. **On-device rotation construction without Eigen** (Ivanic–Ruedenberg
   recurrence) — needed for *head-tracked* binaural on bare-metal M55,
   where there is no host to compute the C×C matrix. Until then, rotation
   matrices come from the control side.
3. **Measured numbers** — run the profile on the Corstone-300 FVP and
   replace the analytical table with cycle counts.
4. **HRTF table trimming** — a generator option for order-limited /
   single-projection tables on flash-constrained parts.
