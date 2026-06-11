# AmbiTap

A target-independent C++17 library for higher-order ambisonics (HOA), using the
AmbiX convention throughout (ACN channel ordering, SN3D normalization).

AmbiTap is the shared core for three wrapper targets:

- **Native C++ applications** — link the library directly
- **Max/MSP** — multichannel (`mc.`) externals via min-api *(planned)*
- **Pure Data** — multichannel externals, Pd ≥ 0.54 *(planned)*

## What's here

```
include/ambitap/
├── ambitap.h            umbrella header
└── math/
    ├── core/            spherical harmonics, ACN indexing, SN3D/N3D, SH rotation
    ├── geometry/        3D convex hull, VBAP speaker layouts, presets, T-designs
    ├── decoding/        mode-matching, ALLRAD, EPAD decoder construction, max-rE
    └── binaural/        Ooura real-FFT wrapper, partitioned overlap-save convolver,
                         embedded SH-domain MIT KEMAR HRTF (order 5, LS + MagLS),
                         optional SOFA reader
```

The library is header-only apart from one tiny static target (`AmbiTap::fft`,
the vendored Ooura `fftsg.c`). A stateful DSP "processor" layer (`ambitap::dsp`)
and UI-feeding analysis (`ambitap::analysis`) build on the math layer; the
processors are runtime-sized with a wait-free real-time `process()` path.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: CMake ≥ 3.21, a C++17 compiler. Eigen is found via an existing
`Eigen3::Eigen` target, an installed Eigen, or a pinned FetchContent download —
in that order. GoogleTest is fetched automatically when tests are enabled.

## Consuming

```cmake
add_subdirectory(path/to/AmbiTap)   # or find_package(AmbiTap) once installed
target_link_libraries(my_target PRIVATE AmbiTap::ambitap)
```

Options:

| Option | Default | Effect |
|---|---|---|
| `AMBITAP_ENABLE_SOFA` | `OFF` | FetchContent libmysofa v1.3.4 and define `AMBITAP_HAS_SOFA`, enabling `ambitap/math/binaural/sofa_reader.h` |
| `AMBITAP_BUILD_TESTS` | `ON` when top-level | Build the GTest suite |

## Conventions

- **Channel ordering:** ACN. **Normalization:** SN3D (AmbiX). FuMa conversion
  lives in the wrapper targets, not here.
- **Angles:** radians; azimuth 0 = front, +π/2 = left; elevation 0 = horizon,
  +π/2 = zenith.
- Decoder constructors return Eigen matrices shaped `(speakers × channels)`
  with `speaker_signals = D * hoa`.

## Third-party

- **Ooura FFT** (`third_party/ooura/`) — Takuya Ooura's split-radix FFT package;
  freely usable with attribution (see `third_party/ooura/readme.txt`).
- **Eigen** — MPL2, header-only, not vendored.
- **MIT KEMAR HRTF** — `math/binaural/hrtf_data.h` is generated from the MIT
  KEMAR (normal pinna) measurements via `scripts/generate_hrtf.py`.
- **libmysofa** — BSD-3, optional, fetched only with `AMBITAP_ENABLE_SOFA=ON`.

## License

Not yet licensed for redistribution — © 2025–2026 Timothy Place; a license
decision is pending. Do not distribute binaries or source
until this section is replaced with a real license.
