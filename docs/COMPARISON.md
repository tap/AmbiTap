# AmbiTap vs the ambisonics ecosystem

A comparison of AmbiTap against other open implementations on three axes:
**correctness** (do independent codebases compute the same numbers?),
**feature set**, and **performance** of the major algorithms.

Two kinds of evidence, kept separate:

- **Measured** — produced by
  [`notebooks/library_comparison.ipynb`](../notebooks/library_comparison.ipynb),
  which drives the compiled C++ through the C ABI against **spaudiopy 0.2.0**
  (independent Python implementation: SH, Wigner-D rotation, VBAP,
  ALLRAD/EPAD/mode-matching) and **pyshtools 4.14.1** (the
  geodesy community's SH reference), with SciPy as a third SH source. Every
  number below re-checks on each notebook run.
- **Documentation-based** — the feature matrix rows for C++ libraries that
  could not be built in this environment; accurate as of their public
  documentation, mid-2025. Treat as orientation, not measurement.

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

**Real-time processing** (AmbiTap C++, 64-frame blocks at 48 kHz, single
core, `bench/`): order-5 binaural 25.0 µs/block (1.9% of budget), order-5
rotation 14.3 µs, order-3 full encode–rotate–decode chain well under 1%.
No cross-library C++ runtime comparison was possible in this environment
(libspatialaudio/SAF could not be fetched); the absolute numbers and the
embedded results below are the anchor.

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

Measured column for AmbiTap and spaudiopy; other columns
documentation-based (mid-2025).

| | **AmbiTap** | spaudiopy | libspatialaudio | SAF (Spatial Audio Framework) | IEM Plug-in Suite | Resonance Audio |
|---|---|---|---|---|---|---|
| Language / form | C++20 header-only | Python | C++ library | C library | C++ (JUCE plugins) | C++ SDK |
| License | MIT | MIT | LGPL | ISC/GPL mix | GPL | Apache-2.0 |
| Max order | 10 | arbitrary | 3 | 7+ | 7 | 3 |
| Decoders | mode-match, ALLRAD, EPAD (+max-rE) | SAD/MAD, ALLRAD/ALLRAD2, EPAD, nearest | AllRAD-style presets | ALLRAD, EPAD, MMD, SPD | AllRAD (+IEM decoder files) | pre-computed |
| Binaural | SH-domain KEMAR, LS + MagLS, resampled to host rate | MagLS (SOFA input) | per-order HRTF convolution | LS/MagLS, SOFA | binaural decoder plugin | proprietary HRTFs |
| Rotation | Ivanic–Ruedenberg, exact; on-device capable | Wigner-D (SciPy-based) | yes | yes | SceneRotator | yes |
| Real-time contract | machine-checked: wait-free, allocation-free process paths (TSan + operator-new proof in CI) | none (offline/research) | informal | informal | plugin RT via JUCE | yes (game-audio) |
| Embedded | Cortex-M55 gate + QEMU execution in CI; Hexagon/AudioReach notes | no | no | partial (used in products) | no | mobile-focused |
| Verification artifacts | 116 C++ tests, 6 executed notebooks, cross-library checks, audit doc | unit tests | tests | extensive tests | listening-tested plugins | tests |
| Extras | doppler, spatial compressor, mirror, directional loudness, analysis layer, FuMa | sig/decoder analysis, plotting | AmbiX/FuMa, binauralizer | beamforming, SLDoA, etc. | full plugin chain | rooms/reflections |

Fair summary: **SAF** is the most feature-broad C library (beamforming,
localization); **IEM** is the reference for end-user tooling; **spaudiopy**
is the best research/prototyping companion (and pairs naturally with
AmbiTap's notebooks); **libspatialaudio** is a compact order-3 renderer;
**Resonance** targets game/mobile scenes. AmbiTap's distinct position is
the combination of a machine-checked real-time contract, embedded
deployability, and executable correctness evidence — at the cost of (so
far) having no room modeling and no shipped end-user tools (the Max/Pd
wrappers are the planned front ends).

## Reproducing

```bash
pip install -r notebooks/requirements-comparison.txt
jupyter execute notebooks/library_comparison.ipynb
```

The notebook `assert`s every agreement claim above; if a future spaudiopy
or pyshtools release changes a convention, the run fails loudly rather
than silently drifting.
