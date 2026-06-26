# Third-Party Notices

AmbiTap itself is licensed under the MIT License (see `LICENSE`), © 2025–2026
Timothy Place. It bundles or fetches the third-party components listed below,
each of which remains under its own license. Nothing here is GPL- or
LGPL-encumbered: the only copyleft dependency (Eigen, MPL-2.0) is fetched at
build time and never redistributed by this repository.

If you redistribute binaries built from AmbiTap, you are responsible for
carrying forward the notices of whichever components you link in (notably the
Ooura FFT, which compiles into every consumer via `AmbiTap::fft`, and — when
`AMBITAP_ENABLE_SOFA=ON` — libmysofa).

---

## Vendored (committed into this repository)

### Ooura FFT — `third_party/ooura/fftsg.c`
Takuya Ooura's General Purpose FFT Package (split-radix "Fast Version III").
Only the single file `fftsg.c` is bundled; `third_party/ooura/readme.txt` is the
upstream package description.

> Copyright(C) 1996-2001 Takuya OOURA
> (email: ooura@mmm.t.u-tokyo.ac.jp,
> download: http://momonga.t.u-tokyo.ac.jp/~ooura/fft.html)
> You may use, copy, modify this code for any purpose and without fee.
> You may distribute this ORIGINAL package.

This is a permissive grant, compatible with redistribution inside an
MIT-licensed project. The copyright/permission notice is retained at the top of
`fftsg.c`.

### MIT KEMAR HRTF data — `include/ambitap/math/binaural/hrtf_data.h`
A spherical-harmonic projection (order 5, LS + MagLS) of the MIT KEMAR
(normal-pinna) head-related transfer function measurements.

> W. G. Gardner and K. D. Martin, "HRTF Measurements of a KEMAR Dummy-Head
> Microphone," MIT Media Lab Perceptual Computing Technical Report #280, 1994.

The KEMAR dataset is distributed free of charge by the MIT Media Laboratory on
the condition that the authors are credited in any research or commercial use.
The committed coefficients are a derived work of that data; the credit
requirement is carried by this notice and by the header comment in
`hrtf_data.h`. Original distribution:
<https://sound.media.mit.edu/resources/KEMAR.html>.

### Hardin–Sloane spherical t-designs — `include/ambitap/math/geometry/tdesigns.h`
Point coordinates of putatively optimal spherical t-designs.

> R. H. Hardin and N. J. A. Sloane, "McLaren's Improved Snub Cube and Other New
> Spherical Designs in Three Dimensions," Discrete and Computational Geometry,
> 15 (1996), pp. 429–441. Tables: <http://neilsloane.com/sphdesigns/>.

These coordinates are mathematical facts taken from the authors' freely
distributed tables (not from any redistribution-licensed repackaging). Generated
by `scripts/generate_tdesigns.py`.

---

## Fetched at build time (not committed, not redistributed by AmbiTap)

| Component | License | When fetched | Notes |
|---|---|---|---|
| **Eigen** 3.4.0 | MPL-2.0 (with some BSD/LGPL files in-tree) | Always, unless an `Eigen3::Eigen` target or a system Eigen is already present | Headers only; Eigen's own CMake is not added. MPL-2.0 is file-level copyleft, but because AmbiTap does not commit or ship Eigen sources, no MPL notice obligation attaches to this repo. A downstream project that *redistributes* Eigen headers inherits that obligation. |
| **GoogleTest** 1.15.2 | BSD-3-Clause | Only when `AMBITAP_BUILD_TESTS=ON` | Test-only; `INSTALL_GTEST=OFF`. Never linked into a distributed AmbiTap artifact. |
| **libmysofa** 1.3.4 | BSD-3-Clause (bundles zlib, also permissive) | Only when `AMBITAP_ENABLE_SOFA=ON` (default OFF) | Linked into the consumer when SOFA support is enabled; its BSD-3 + zlib notices then become the consumer's redistribution obligation. |

---

## Algorithm / formula references (cited, not code dependencies)

These works are cited in source comments because AmbiTap implements published
algorithms or formulas from them. Algorithms and mathematical formulas are not
copyrightable; no code is taken from these sources.

- F. Zotter (2009), *Analysis and Synthesis of Sound-Radiation with Spherical
  Arrays* — spherical-harmonic conventions (`core/spherical_harmonics.h`).
- A. Politis, *Spherical-Harmonic-Transform* library (github.com/polarch,
  BSD-3) — used to cross-check the SH evaluation in
  `core/spherical_harmonics.h`. If any expression is ever copied from it rather
  than independently implemented from the formulas, its BSD-3 copyright notice
  must be reproduced here.
- F. Zotter and M. Frank (2012), "All-Round Ambisonic Panning and Decoding,"
  JAES 60(10) — ALLRAD and max-rE (`decoding/allrad.h`, `decoding/max_re.h`).
- F. Zotter and M. Frank (2019), *Ambisonics* (Springer Open) — EPAD decoder
  (`decoding/epad.h`).
- V. Pulkki (1997), "Virtual Sound Source Positioning Using Vector Base
  Amplitude Panning," JAES 45(6) — VBAP (`geometry/speaker_layout.h`).
- J. Daniel / Furse-Malham — FuMa ⇄ ACN/SN3D conversion factors
  (`dsp/format_converter.h`); these are standard published constants.
- C. Schörkhuber and R. Habets (2019) — magnitude least-squares HRTF projection
  (`scripts/generate_hrtf.py`).
