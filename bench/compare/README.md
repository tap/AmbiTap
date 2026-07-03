# Cross-library C++ benchmarks

Head-to-head runtime measurements of AmbiTap against **libspatialaudio**
and the **Spatial_Audio_Framework (SAF)**, feeding the "Measured C++
head-to-head" section of [`docs/COMPARISON.md`](../../docs/COMPARISON.md).

## What runs

| Harness | Library | Processors timed |
|---|---|---|
| `../../bench/` (in-tree) | AmbiTap | encoder, rotator, decoder (cube), binaural — orders 1/3/5 |
| `bench_libspatialaudio.cpp` | libspatialaudio | `AmbisonicEncoder`, `AmbisonicRotator`, `AmbisonicDecoder` (cube), `AmbisonicBinauralizer` (built-in MIT HRTF) — orders 1/2/3 |
| `bench_saf.c` | SAF | `ambi_enc`, `rotator`, `ambi_dec` (8-speaker preset), `ambi_bin` — orders 1/3/5 |

Shared methodology: 48 kHz, median of 9 runs × 400 blocks, 50 warm-up
blocks, single thread. AmbiTap and libspatialaudio process 64-sample
blocks directly. SAF's `ambi_dec`/`ambi_bin` process at their compiled
128-sample frame; the harness reports both raw µs/frame and the
normalized µs-per-64-samples used in the comparison tables.

## Known asymmetries (read before quoting numbers)

- SAF's decoder and binauralizer are **time–frequency (afSTFT)**
  processors — the filterbank cost buys frequency-dependent decoding
  that the broadband matrix decoders in AmbiTap and libspatialaudio do
  not attempt. Its time-domain encoder/rotator are directly comparable.
- libspatialaudio's bundled MIT HRTF binauralizer only configures at
  order 1 (`Configure` returns false at 2–3 without a SOFA file); the
  harness reports this rather than substituting a different path.
- SAF is built with its default `SAF_ENABLE_SIMD=OFF` and the OpenBLAS
  backend; enabling SIMD or MKL may improve its numbers.
- All three include their own parameter-smoothing/fade machinery in the
  timed paths; none was disabled.

## Requirements

- CMake ≥ 3.24, a C++20 compiler, git
- SAF backend: `apt install libopenblas-dev liblapacke-dev` (or point
  SAF at MKL / Accelerate per its docs)
- Eigen for the AmbiTap build (found or fetched automatically)

## Run

```bash
bench/compare/run.sh
```

Clones both libraries into `bench/compare/_work/` (git-ignored), builds
everything Release, and prints all three benchmark reports. Pinned
versions for the numbers in COMPARISON.md: libspatialaudio 0.4.1,
SAF v1.3.5 (master @ 2026-01), captured 2026-07-03 on a single-core
Intel Xeon @ 2.10 GHz, GCC 13.3, Ubuntu 24.04.
