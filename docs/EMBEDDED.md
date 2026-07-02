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
| `dsp::spatial_compressor` (fast_math, no per-sample libm) | `async_rebuilder` / worker threads (`std::thread`) |
| `dsp::doppler`, `dsp::format_converter` | `binaural_renderer`'s resampling + dataset-selection layer |
| `compute_sh_rotation` — on-device rotation-matrix construction (Ivanic–Ruedenberg recurrence, `math/core/rotation_recurrence.h`) | `analysis::soundfield_grid` (O(C²·D) per block; a UI feature) |
| `dsp::matrix_applier` — crossfading application of rotation/decode matrices | SOFA reader |
| `dsp::binaural_core` — float32 shared-spectrum HRTF convolver bank + volume | |
| `analysis::energy_vector` | |

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
| head-track rotation (`sh_block_applier`, Σ(2l+1)² MACs) | ≈ 5 MHz scalar | ≈ 18 MHz scalar |
| binaural (`binaural_core`: shared-spectrum bank, C+2 FFTs/block) | ≈ 77 MHz scalar / ~25 MHz Helium | ≈ 167 MHz scalar / ~60 MHz Helium |

(Rotation application is block-diagonal — SH rotation never mixes orders —
via `sh_block_applier`, which `dsp::rotator` also uses now; the desktop
bench measured 3.9× over the dense applier at order 5, 56.0 → 14.3
µs/block.)

(The bank shares each channel's input spectrum across ears; the earlier
per-ear-convolver arrangement cost 4C transforms per block — ≈ 405 MHz at
order 5, which did not fit. The switch measured **2.7× faster** on the
desktop bench: order-5 binaural 67.9 → 25.0 µs/block.)

Memory (order 5 binaural): ≈ 170 KB RAM. HRTF tables are 72 KiB of flash
for the full order-5 LS + MagLS set; `tools/hrtf_trim` emits drop-in
order-trimmed / single-projection replacements (values bit-exact slices of
the full set, verified in CI by running the trimmed build):

| Table build | flash (tables) | RT-profile check binary |
|---|---|---|
| full: order 5, LS + MagLS | 72 KiB | 83 KB |
| order 3, LS + MagLS | 32 KiB | — |
| order 3, LS only (`--order 3 --projection ls`) | 16 KiB | 63 KB |

The check binary numbers are the complete order-3 profile — every
processor including binaural and on-device rotation — linked against
newlib-nano.

Conclusions:

- **Order-3 binaural fits an M55 today**, scalar, with a wide margin —
  and the complete profile **runs, self-checked, on QEMU's Cortex-M55
  machine in CI** (mps3-an547, semihosting; startup + linker script in
  `tools/embedded/qemu/`). QEMU validates target-ISA behavior, not
  timing; the budget numbers stay analytical until an FVP/hardware run.
- **Order-5 binaural fits at ~400 MHz scalar** and comfortably with a
  Helium FFT: `real_fft32` is the seam where a CMSIS-DSP-class FFT drops
  in per-target — it is now the dominant cost.
- Head-tracking is fully on-device: `compute_sh_rotation`
  (Ivanic–Ruedenberg, exact, no Eigen) builds the C×C matrix on the
  control core and `sh_block_applier` crossfades it in block-diagonally.

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

1. **Measured cycle counts** — QEMU is functional, not cycle-accurate; run
   the profile on the Corstone-300 FVP (or hardware) and replace the
   analytical table. The QEMU build in `tools/embedded/qemu/` is the
   starting point — the FVP boots the same ELF.
2. **Helium FFT integration** — swap `real_fft32` for a CMSIS-DSP FFT on
   M55 builds once FVP profiling confirms it is the remaining hot spot.
   Needs the CMSIS-DSP dependency and hardware/FVP validation; the class
   is the deliberate seam.

Closed since first written: the shared-spectrum convolver bank
(`convolver_bank`, inside `binaural_core`; 2.7× measured), on-device
rotation construction (`compute_sh_rotation`, Ivanic–Ruedenberg; also now
the backend of the desktop `sh_rotation`, replacing the least-squares
approximation — audit item Q1), the block-diagonal `sh_block_applier`
(3.9× measured at order 5; `dsp::rotator` uses it too), HRTF table
trimming (`tools/hrtf_trim`), and functional execution on the target ISA
(QEMU mps3-an547 in CI, full-table and trimmed builds).
