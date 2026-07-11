# AmbiTap

[![CI](https://github.com/tap/AmbiTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/AmbiTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

A target-independent C++20 library for higher-order ambisonics (HOA), using the
AmbiX convention throughout (ACN channel ordering, SN3D normalization).

AmbiTap is the shared core for three wrapper targets:

- **Native C++ applications** — link the library directly
- **Max/MSP** — multichannel (`mc.`) externals via min-api *(planned)*
- **Pure Data** — multichannel externals, Pd ≥ 0.54 *(planned)*

## What's here

```
include/ambitap/
├── ambitap.h            umbrella header
├── math/
│   ├── core/            spherical harmonics, ACN indexing, SN3D/N3D, SH rotation
│   ├── geometry/        3D convex hull, 3D + 2D pairwise VBAP, presets, T-designs
│   ├── decoding/        mode-matching, ALLRAD, EPAD decoder construction, max-rE
│   └── binaural/        Ooura real-FFT wrapper, partitioned overlap-save convolver,
│                        embedded SH-domain MIT KEMAR HRTF (order 5, LS + MagLS),
│                        FIR resampling, optional SOFA reader
├── dsp/                 runtime-sized processors: encoder, rotator, decoder,
│                        binaural renderer, mirror, virtual mic, doppler,
│                        directional loudness, spatial compressor,
│                        room reverb (image-source + SH FDN), plate reverb
│                        (N-in/M-out Dattorro tank), near-field compensation,
│                        crosstalk cancellation, FuMa <-> AmbiX format converter
│   └── util/            wait-free publication (rt_published), async matrix
│                        rebuild worker, parameter smoothing
└── analysis/            UI-feeding analysis: energy vector, soundfield heatmap
```

The library is header-only apart from one tiny static target (`AmbiTap::fft`,
the vendored Ooura `fftsg.c`).

**The real-time contract** (machine-checked in `tests/test_rt_safety.cpp` and
`tests/test_dsp_threads.cpp`, under TSan in CI): every `process()` path is
wait-free — it never locks, never allocates or frees, and never blocks on the
worker threads that rebuild decode/rotation matrices. Parameter changes are
click-free: coefficient tables ramp, matrices crossfade in, and delay changes
glide. Setters are safe to call from one control thread while audio runs; for
offline/exact rendering call `snap_parameters()` / `wait_for_settling()`.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: CMake ≥ 3.24, a C++20 compiler. Eigen is found via an existing
`Eigen3::Eigen` target, an installed Eigen, or a pinned FetchContent download —
in that order. GoogleTest is fetched automatically when tests are enabled (an
installed GTest is preferred when present).

Example programs live in `examples/` (`encode_rotate_decode`,
`binaural_render` — writes a WAV of a source orbiting the head) and a
dependency-free micro-benchmark in `bench/` (`-DAMBITAP_BUILD_BENCH=ON`).

For a visual tour and executable verification of the algorithms, see the
Jupyter notebooks in `notebooks/` — they drive the actual C++ implementation
through the C ABI (`tools/capi/`, `-DAMBITAP_BUILD_CAPI=ON`) via ctypes, and
their `assert` cells re-check the audit's key properties on every run:

- [`notebooks/ambitap_demo.ipynb`](notebooks/ambitap_demo.ipynb) — SH basis
  maps (cross-checked against SciPy), rotation, 3D + 2D pairwise VBAP, and an
  audible binaural orbit.
- [`notebooks/decoder_analysis.ipynb`](notebooks/decoder_analysis.ipynb) —
  energy / rE-vector / angular-error maps for mode-matching vs ALLRAD vs EPAD
  across layouts, absolute-gain and rank-truncation gates, and a NumPy
  pseudoinverse cross-check.
- [`notebooks/hrtf_analysis.ipynb`](notebooks/hrtf_analysis.ipynb) — LS vs
  MagLS causality (the audit's B6 picture), ILD/ITD against the Woodworth
  model, the HRTF resampler's response, and the partitioned convolver vs
  direct convolution.
- [`notebooks/dsp_behavior.ipynb`](notebooks/dsp_behavior.ipynb) — the
  real-time contract made visible: the encoder's 128-sample parameter ramps,
  the decoder's 256-sample matrix crossfade, the Doppler shift measured
  against 1 ± v/c (and the delay-slew pitch glide on distance jumps), and the
  spatial compressor's static curve and attack/release clocks.
- [`notebooks/soundfield_analysis.ipynb`](notebooks/soundfield_analysis.ipynb)
  — the `analysis/` layer against ground truth (soundfield-heatmap source
  positions and levels, energy-vector DOA tracking of a moving source) and a
  "which order do I need?" study: max-rE beamwidth and decoder |rE| across
  orders 1–5.
- [`notebooks/library_comparison.ipynb`](notebooks/library_comparison.ipynb)
  — cross-library verification against spaudiopy and pyshtools: SH basis,
  rotation, max-rE, VBAP, and decoder matrices agree to float precision
  through exactly-derived convention maps (needs
  `pip install -r notebooks/requirements-comparison.txt`). Narrative +
  feature/performance comparison in [`docs/COMPARISON.md`](docs/COMPARISON.md).

Python needs `numpy`, `scipy`, and `matplotlib`
(`pip install -r notebooks/requirements.txt`); the first cell builds the
shared library if missing.

## Consuming

```cmake
add_subdirectory(path/to/AmbiTap)   # or FetchContent
target_link_libraries(my_target PRIVATE AmbiTap::ambitap)
```

or install it and use the exported package (CI smoke-tests this path):

```bash
cmake -B build && cmake --install build --prefix /some/prefix
```

```cmake
find_package(AmbiTap CONFIG REQUIRED)   # needs an installed Eigen3
target_link_libraries(my_target PRIVATE AmbiTap::ambitap)
```

Options:

| Option | Default | Effect |
|---|---|---|
| `AMBITAP_ENABLE_SOFA` | `OFF` | FetchContent libmysofa and define `AMBITAP_HAS_SOFA`, enabling `ambitap/math/binaural/sofa_reader.h` (build-tree consumers only; not exported) |
| `AMBITAP_BUILD_TESTS` | `ON` when top-level | Build the GTest suite |
| `AMBITAP_BUILD_EXAMPLES` | `ON` when top-level | Build the example programs |
| `AMBITAP_BUILD_BENCH` | `OFF` | Build the micro-benchmarks |
| `AMBITAP_WERROR` | `OFF` | Warnings as errors (used by CI) |
| `AMBITAP_INSTALL` | `ON` when top-level | Generate install/export rules |

## Conventions

- **Channel ordering:** ACN. **Normalization:** SN3D (AmbiX). FuMa conversion
  (orders 0–3) is provided by `dsp::format_converter`.
- **Angles:** radians; azimuth 0 = front, +π/2 = left; elevation 0 = horizon,
  +π/2 = zenith. Rotations compose as yaw (about +Z) then pitch (about +Y)
  then roll (about +X); positive pitch tilts the front axis downward
  (right-hand rule about +Y).
- Decoder constructors return Eigen matrices shaped `(speakers × channels)`
  with `speaker_signals = D * hoa`. All three constructions (mode-matching,
  ALLRAD, EPAD) share one absolute-gain convention and are built in the
  orthonormal (N3D) basis internally.
- **Binaural sample rate:** the embedded KEMAR HRTFs are 44.1 kHz data; pass
  the host rate to `binaural_renderer::prepare(block_size, sample_rate)` and
  the FIRs are resampled to match.

## Third-party

- **Ooura FFT** (`third_party/ooura/`) — Takuya Ooura's split-radix FFT package;
  freely usable with attribution (see `third_party/ooura/readme.txt`).
- **Eigen** — MPL2, header-only, not vendored.
- **MIT KEMAR HRTF** — `math/binaural/hrtf_data.h` is a spherical-harmonic
  projection of the MIT KEMAR (normal pinna) measurements of W. G. Gardner and
  K. D. Martin (*HRTF Measurements of a KEMAR Dummy-Head Microphone*, MIT Media
  Lab Tech. Report #280, 1994), generated via `scripts/generate_hrtf.py`. The
  KEMAR data is distributed free by the MIT Media Lab on the condition that the
  authors are credited.
- **libmysofa** — BSD-3, optional, fetched only with `AMBITAP_ENABLE_SOFA=ON`.
- **Hardin–Sloane spherical t-designs** — `math/geometry/tdesigns.h` is
  generated by `scripts/generate_tdesigns.py` from the original, freely
  distributed tables of R. H. Hardin and N. J. A. Sloane (*McLaren's Improved
  Snub Cube and Other New Spherical Designs in Three Dimensions*, Discrete &
  Computational Geometry 15 (1996), 429–441), hosted at
  <http://neilsloane.com/sphdesigns/>. The point coordinates are mathematical
  facts taken from that catalogue rather than from any redistribution-licensed
  repackaging.

## Embedded targets

The real-time paths run on embedded processors (Cortex-M55, Hexagon
AudioReach): the RT profile — every `process()` path, including a float32
shared-spectrum binaural engine (`dsp::binaural_core`), on-device rotation
construction (`compute_sh_rotation`, Ivanic–Ruedenberg), and click-free
matrix application (`dsp::matrix_applier` / `sh_block_applier`) — builds
with no exceptions, no threads, no Eigen, and no hardware doubles. On every
push, CI cross-compiles it for bare-metal Cortex-M55 **and runs its
per-processor self-checks on QEMU's Cortex-M55 machine**, for both the full
HRTF tables and a `tools/hrtf_trim` order-trimmed build. Profile
definition, per-order cycle/memory budgets, and AudioReach integration
notes live in [`docs/EMBEDDED.md`](docs/EMBEDDED.md).

## Documentation

- [**Hearing in Three Dimensions**](book/) — a field guide to Ambisonics
  and spatial audio for newcomers, built around the Max package as its
  playground (mdBook sources in `book/`, published to GitHub Pages under
  `/book/`; figures generated by `scripts/generate_book_figures.py` from
  the library itself).
- [`docs/CONCEPTS.md`](docs/CONCEPTS.md) — conventions, the real-time
  contract, and the processor lifecycle: read this first.
- Generated API reference — `doxygen docs/Doxyfile` (published to GitHub
  Pages from `main`).
- [`docs/AUDIT.md`](docs/AUDIT.md) — the full correctness/quality audit and
  its remediation ledger.
- [`docs/COMPARISON.md`](docs/COMPARISON.md) — cross-library comparison:
  correctness cross-checks (spaudiopy, pyshtools), a measured C++
  head-to-head vs libspatialaudio and SAF (`bench/compare/`), and a
  feature/platform matrix across the ecosystem.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — the product-surface roadmap: the
  Max/MSP + Pd wrapper plan and the composable-object line built on the
  verified core.
- [`docs/EMBEDDED.md`](docs/EMBEDDED.md) — the embedded real-time profile,
  budgets, and AudioReach notes.

## License

MIT — © 2025–2026 Timothy Place. See [`LICENSE`](LICENSE). The bundled and
fetched third-party components above keep their own permissive licenses; their
notices and attribution requirements are collected in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
