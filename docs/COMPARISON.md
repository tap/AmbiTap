# AmbiTap vs the ambisonics ecosystem

A comparison of AmbiTap against other open implementations on three axes:
**correctness** (do independent codebases compute the same numbers?),
**feature set**, and **performance** of the major algorithms.

Three kinds of evidence, kept separate:

- **Measured (Python cross-checks)** — produced by
  [`notebooks/library_comparison.ipynb`](../notebooks/library_comparison.ipynb),
  which drives the compiled C++ through the C ABI against **spaudiopy 0.2.0**
  (independent Python implementation: SH, Wigner-D rotation, VBAP,
  ALLRAD/EPAD/mode-matching) and **pyshtools 4.14.1** (the
  geodesy community's SH reference), with SciPy as a third SH source. Every
  number below re-checks on each notebook run.
- **Measured (C++ head-to-head)** — produced by the harnesses in
  [`bench/compare/`](../bench/compare/), which build **libspatialaudio 0.4.1**
  and **SAF v1.3.5** from source and time their real-time processors on the
  same machine, same compiler, same methodology as AmbiTap's own `bench/`.
- **Documentation-based** — feature-matrix cells for projects not built here
  (IEM, SPARTA, Resonance); verified against their public documentation and
  repositories, July 2026. Treat as orientation, not measurement.

## Correctness: measured cross-library agreement

Conventions differ between libraries by design; the notebook derives each
mapping exactly and compares through it.

| Algorithm | Reference | Convention mapping | Agreement |
|---|---|---|---|
| SH basis (orders 1–5) | spaudiopy | × √(2n+1)/√4π per order | 4.0e-7 |
| SH basis | pyshtools ("4π" real SH) | × √(2n+1) (equals AmbiTap N3D exactly) | 1.4e-6 |
| SH basis | SciPy (`ambitap_demo.ipynb`) | Legendre + SN3D, CS removed | < 2e-5 |
| SH rotation (orders 1–5, random angles) | spaudiopy Wigner-D | negate all three angles (field- vs frame-rotation sense) | 9.6e-7 |
| max-rE weights (orders 1–5) | spaudiopy | normalize leading coefficient | < 1e-5 |
| VBAP gains (7.1.4, in coverage) | spaudiopy | none needed | same triangle 200/200, gains to 2.9e-7 |
| VBAP invariants (cube, full sphere) | Pulkki's definition | — | unit power to 2.4e-7; velocity vector within 0.025° for **both** libraries |
| Mode-matching decoder (7.1.4, order 3) | spaudiopy `mad` | basis map + one global gain (+1.63 dB convention offset) | residual 2.7e-7 |
| EPAD decoder | spaudiopy `epad` | same | residual 4.8e-7 |
| FuMa ↔ AmbiX | published AmbiX-spec factors | — | exact-value C++ tests (`tests/test_dsp_transforms.cpp`) |
| Binaural ILD/ITD | Woodworth model (`hrtf_analysis.ipynb`) | — | ITD ≈ 660 µs at ±90°, correct ear ordering |

Notes:

- **Rotation** is the strongest of these results: AmbiTap's
  Ivanic–Ruedenberg recurrence and spaudiopy's Wigner-D construction share
  no lineage at all, and agree to float32 precision at every order.

- **Mode-matching and EPAD are matrix-identical** across the libraries —
  the +1.63 dB scalar is the two projects' absolute-level conventions,
  each internally consistent.

- **VBAP diagonal ties**: on square faces (the cube) the two libraries
  pick different — equally valid — face diagonals, so gains differ while
  both satisfy the defining invariants exactly. On layouts with
  unambiguous triangulation they agree to float precision.

- **ALLRAD is the one deliberate divergence.** It depends on a virtual
  optimal layout (AmbiTap: Hardin–Sloane t-designs; spaudiopy: its own
  kernel grid) and on imaginary-speaker policy. Measured on 7.1.4 /
  order 3 / max-rE, horizon:

  | | energy ripple (max/min) | mean \|rE\| |
  |---|---|---|
  | AmbiTap | 1.90 | **0.862** |
  | spaudiopy `allrad` | 2.26 | 0.787 |
  | spaudiopy `allrad2` | **1.66** | 0.732 |

  AmbiTap lands the sharpest imaging with energy flatness between
  spaudiopy's two variants — a defensible point on the trade-off curve,
  not a superset.

### A bug this comparison caught (and its fix)

The first VBAP cross-check exposed a real AmbiTap defect: the incremental
convex hull mis-triangulated **exactly coplanar quad faces** (a cube face is
the canonical case), folding two triangles through the hull interior and
leaving part of each such face uncovered — sources there snapped to a
single speaker up to 52° away (~20% of horizon directions on the cube).
Fixed in `convex_hull.h` by deciding hull topology on deterministically
radius-lifted points (breaking every exact coplanarity tie), plus a
least-violating-triangle fallback in `speaker_layout.h`. Regression tests
now enforce hull convexity and the VBAP velocity-vector invariant on the
pathological layouts (`tests/test_geometry.cpp`). This is precisely what
independent-implementation comparison is for.

## Performance

### Measured C++ head-to-head

All three libraries were built from source and timed on the **same
machine** (single-core Intel Xeon @ 2.10 GHz, Ubuntu 24.04, GCC 13,
`-O3`), 48 kHz, median of 9 runs × 400 blocks, warm caches. Sources and
build commands are in [`bench/compare/`](../bench/compare/). Absolute
numbers will differ on other hardware; the *ratios* on one machine are
the point.

- **AmbiTap** — `main`, its own `bench/`, 64-frame blocks.
- **libspatialaudio 0.4.1** — current `AmbisonicRotator`/`AmbisonicDecoder`/
  `AmbisonicBinauralizer` classes, 64-frame blocks, cube layout,
  built-in MIT HRTF, `lowCpuMode`.
- **SAF v1.3.5** — its `ambi_enc` / `rotator` / `ambi_dec` / `ambi_bin`
  example engines (the exact cores inside the SPARTA plug-ins), OpenBLAS
  backend, default `SAF_ENABLE_SIMD=OFF`. `ambi_dec`/`ambi_bin` run at
  their compiled 128-sample frame; values are normalized to µs per 64
  samples for comparison.

**µs per 64-sample block** (budget at 48 kHz = 1333 µs):

| Processor | Order | AmbiTap | libspatialaudio | SAF |
|---|---|---|---|---|
| encoder | 1 | 0.02 | 0.14 | 1.67 |
| | 3 | 0.08 | 0.56 | 2.01 |
| | 5 | 0.14 | n/a (max order 3) | 2.43 |
| rotator | 1 | 0.76 | 0.14 | 1.70 |
| | 3 | 5.08 | 1.08 | 2.11 |
| | 5 | 14.39 | n/a | 3.57 |
| decoder (8 spk) | 1 | 2.02 | 3.10 | 22.3 |
| | 3 | 5.87 | 10.96 | 38.0 |
| | 5 | 14.55 | n/a | 63.0 |
| binaural | 1 | 3.33 | 12.15 | 12.0 |
| | 3 | 11.20 | unsupported* | 29.7 |
| | 5 | 22.99 | unsupported* | 62.2 |

\* libspatialaudio's bundled MIT HRTF binauralizer configures only at
order 1; orders 2–3 require supplying a SOFA file.

Reading these numbers honestly:

- **SAF's decoder and binauralizer are time–frequency processors**
  (afSTFT filterbank), which is what buys its frequency-dependent
  decoding orders and Mag-LS options. The constant-ish filterbank
  overhead (~20 µs/64 even at order 1) is the price of that capability,
  and its 128-sample internal frame sets a latency floor. AmbiTap and
  libspatialaudio decode broadband with per-block matrices — cheaper,
  but a different (simpler) operation. SAF's *time-domain* modules
  (encoder, rotator) are directly comparable and land within a few µs.
- **libspatialaudio's encoder and rotator are the fastest here at
  orders 1–3** — lean per-degree processing with gain interpolation.
  Its decoder is ~2× AmbiTap's cost at matching orders, and it stops at
  order 3.
- **AmbiTap's rotator cost** buys exact Ivanic–Ruedenberg matrices with
  click-free crossfade machinery on every path; at order 5 it is the
  only one of the three that pairs that with a wait-free,
  allocation-free contract that is machine-checked in CI.
- **Binaural is where AmbiTap's design shows most**: the shared-spectrum
  SH-domain engine is 3.6× cheaper than libspatialaudio at order 1 and
  is the only embedded-HRTF path here that runs at orders up to 5
  without external files (12.0–62.2 µs for SAF's ambi_bin vs
  3.3–23.0 µs for AmbiTap across orders 1–5).

**Design-time construction** (measured, same machine; spaudiopy is NumPy
research code and makes no speed claims — this quantifies the workflow
gap, and ctypes overhead is charged to AmbiTap's side):

| Construction | AmbiTap | spaudiopy | ratio |
|---|---|---|---|
| mode-matching (order 3, 7.1.4) | 0.069 ms | 0.437 ms | 6× |
| EPAD (order 3, 7.1.4) | 0.072 ms | 0.390 ms | 5× |
| ALLRAD (order 3, 7.1.4) | 0.051 ms | 3.56 ms | 69× |
| rotation matrix (order 5) | 0.016 ms | 2.42 ms | 148× |

The rotation number is what makes per-block head-tracked matrix rebuilds
practical on device.

**Embedded** (see [`EMBEDDED.md`](EMBEDDED.md)): the full RT profile —
including order-3 binaural and on-device rotation — runs self-checked on
QEMU's Cortex-M55 in 80 KB of flash; order-5 binaural fits a 400 MHz M55
analytically. None of the compared libraries publishes an embedded story.

## Feature matrix

Measured columns for AmbiTap, spaudiopy, libspatialaudio, and SAF; IEM,
SPARTA, and Resonance columns documentation-based (verified July 2026).

| | **AmbiTap** | spaudiopy | libspatialaudio 0.4.1 | SAF v1.3.5 | IEM Plug-in Suite | SPARTA | Resonance Audio |
|---|---|---|---|---|---|---|---|
| Language / form | C++20 header-only | Python | C++ library | C library | C++ (JUCE plugins) | C++ (JUCE plugins on SAF) | C++ SDK |
| License | MIT | MIT | LGPL-2.1+ (commercial dual-license via VideoLabs) | ISC core; optional modules GPLv2 | GPLv3 | GPLv3 | Apache-2.0 |
| Max order | 10 | arbitrary | 3 | 10 | 7 | 10 | 3 |
| Decoders | mode-match, ALLRAD, EPAD (+max-rE) | SAD/MAD, ALLRAD/ALLRAD2, EPAD, nearest | AllRAD-style presets + custom layouts | ALLRAD, EPAD, MMD, SAD (freq-dependent) | AllRAD (+IEM decoder files) | ALLRAD, EPAD, MMD, SAD | pre-computed |
| Binaural | SH-domain KEMAR embedded, LS + MagLS, resampled to host rate, orders ≤5 | MagLS (SOFA input) | HRTF convolution; built-in MIT HRTF **order 1 only**, SOFA for higher | LS, SPR, TA, Mag-LS; SOFA | binaural decoder plugin | LS/SPR/TA/Mag-LS, SOFA, OSC head-tracking | proprietary HRTFs |
| Rotation | Ivanic–Ruedenberg, exact; on-device capable | Wigner-D (SciPy-based) | yes (`AmbisonicRotator`, faded) | yes (`rotator` example) | SceneRotator | Rotator (OSC head-tracking) | yes |
| Real-time contract | machine-checked: wait-free, allocation-free process paths (TSan + operator-new proof in CI) | none (offline/research) | informal (RT-oriented, gain-faded) | informal (RT-oriented; powers SPARTA) | plugin RT via JUCE | plugin RT via JUCE | yes (game-audio) |
| Embedded | Cortex-M55 gate + QEMU execution in CI; Hexagon/AudioReach notes | no | no | partial (used in products) | no | no | mobile-focused |
| External deps for core | Eigen (design-time only; RT profile has none) | NumPy/SciPy | none | BLAS/LAPACK backend required (MKL / OpenBLAS / Accelerate / FFTW) | JUCE, FFTW | JUCE + SAF | none notable |
| Verification artifacts | 116 C++ tests, 6 executed notebooks, cross-library checks, audit doc | unit tests | tests | extensive tests | listening-tested plugins | (inherits SAF tests) | tests |
| Extras | doppler, spatial compressor, mirror, virtual mic, directional loudness, analysis layer, FuMa | sig/decoder analysis, plotting | object panning, speaker rendering, ADM (BS.2127-1) / IAMF workflows, unified `Renderer`, AmbiX/FuMa | beamforming, SLDoA, powermap, roomsim, DRC, array2SH | full plugin chain incl. RoomEncoder (200+ reflections, Doppler), granular encoder, multiband compressor | visualisers, roomsim, array encoders, COMPASS/HO-DirAC parametric suites | room modeling / reflections, spectral reverb |
| Activity (July 2026) | active | active | active (commit July 2026); `AmbisonicProcessor` deprecated in favor of `AmbisonicRotator` | active (v1.3.5, Jan 2026) | active | active | dormant — community steering committee; several platform SDK repos archived |

Changes from the mid-2025 snapshot worth knowing about:

- **libspatialaudio has outgrown "compact order-3 renderer."** It now
  centers a unified `Renderer` for HOA + objects + speaker feeds +
  binaural, targeting ADM (ITU-R BS.2127-1) and IAMF rendering. HOA is
  still capped at order 3, and the built-in-HRTF binaural path is still
  order-1 only.
- **SAF's example engines go to order 10** (not "7+"), and they are the
  literal cores of the SPARTA plug-ins, so benchmarking them benchmarks
  SPARTA's DSP.
- **Resonance Audio is best treated as stable-but-dormant**: Apache-2.0
  and still a fine reference for its decode approach, but several of its
  platform SDKs are archived and activity is minimal.

## Platforms and architectures

| | AmbiTap | libspatialaudio | SAF | IEM | SPARTA | Resonance |
|---|---|---|---|---|---|---|
| Desktop OS | macOS / Linux / Windows (CMake) | macOS / Linux / Windows (CMake + Meson) | macOS / Linux / Windows | macOS / Linux / Windows | macOS 12+ / Linux x86_64 / Windows x86_64 | macOS / Linux / Windows |
| Mobile | via wrapper targets (planned) | yes (used in VLC) | possible | no | no | Android / iOS focus |
| Bare-metal / DSP | **Cortex-M55 (CI-gated, QEMU-executed), Hexagon AudioReach notes** | no | no | no | no | no |
| SIMD story | target-independent C++; Eigen vectorizes design-time paths | portable C++ | optional SSE3/AVX2/AVX512 (`SAF_ENABLE_SIMD`); perf lib does the heavy lifting | via JUCE/FFTW | via SAF | optimized DSP classes |
| Plugin formats | Max/MSP `mc.` + Pd externals (planned) | n/a (library) | n/a (library; SPARTA is the plugin face) | VST2/VST3/LV2/AU/AAX + standalone | VST/VST3/LV2/AU + standalone | Unity/Unreal/FMOD/Wwise/Web SDKs, VST monitor |

## Pros and cons, per library

**AmbiTap** — *Pros*: machine-checked wait-free RT contract; embedded
deployability with CI proof; embedded order-5 HRTF set (no files
needed); fastest measured binaural and encoder; executable correctness
evidence; MIT. *Cons*: no room modeling; no shipped end-user tools yet
(Max/Pd wrappers planned); young project, small community.

**libspatialaudio** — *Pros*: broadest *format* scope (HOA + objects +
speakers + ADM/IAMF) behind one `Renderer`; dependency-free; very lean
time-domain processors; production-proven (VLC lineage); LGPL usable in
closed apps with dynamic linking. *Cons*: HOA capped at order 3;
built-in binaural order-1 only; informal RT guarantees; LGPL obligations
where static linking or modification is needed.

**SAF** — *Pros*: widest algorithm coverage in C (beamforming,
localisation, parametric methods); frequency-dependent decoding; order
10; ISC core; powers a mature plugin suite. *Cons*: mandatory
BLAS/LAPACK backend complicates deployment; filterbank overhead and
128-sample frame floor; optional modules flip the license to GPLv2;
informal RT guarantees.

**IEM Plug-in Suite** — *Pros*: the reference for end-user Ambisonics
production; order 7; RoomEncoder is unmatched for externalization work;
every major plugin format. *Cons*: it's a plugin suite, not a library
API; GPLv3; JUCE + FFTW build.

**SPARTA** — *Pros*: SAF's algorithms with GUIs, SOFA, OSC
head-tracking; order 10; research-current (COMPASS, HO-DirAC). *Cons*:
GPLv3; plugin-only; channel-count quirks vary by plugin format.

**spaudiopy** — *Pros*: best prototyping companion; arbitrary order;
pairs directly with AmbiTap's notebooks. *Cons*: not real-time; NumPy
speeds (see design-time table).

**Resonance Audio** — *Pros*: Apache-2.0; proven game/mobile scene
model with rooms and reflections; broad engine plugin coverage.
*Cons*: order 3; dormant; HRTF set fixed; not a general HOA toolkit.

Fair summary: **SAF** is the most feature-broad C library (beamforming,
localization, parametric methods); **IEM** is the reference for end-user
tooling; **spaudiopy** is the best research/prototyping companion (and
pairs naturally with AmbiTap's notebooks); **libspatialaudio** has grown
into an ADM/IAMF-oriented renderer while remaining order-3 for HOA;
**Resonance** remains the game/mobile scene reference, now dormant.
AmbiTap's distinct position is the combination of a machine-checked
real-time contract, embedded deployability, and executable correctness
evidence — at the cost of (so far) having no room modeling and no
shipped end-user tools (the Max/Pd wrappers are the planned front ends).

## Reproducing

Python cross-checks:

```bash
pip install -r notebooks/requirements-comparison.txt
jupyter execute notebooks/library_comparison.ipynb
```

The notebook `assert`s every agreement claim above; if a future spaudiopy
or pyshtools release changes a convention, the run fails loudly rather
than silently drifting.

C++ head-to-head:

```bash
# see bench/compare/README.md — clones and builds libspatialaudio and
# SAF, compiles the two harnesses, and runs all three benchmarks
bench/compare/run.sh
```

The head-to-head numbers above were captured 2026-07-03 at
libspatialaudio 0.4.1 and SAF v1.3.5 (master); re-run on your hardware
before quoting absolute values.
