# Fuzzing

libFuzzer harnesses for AmbiTap's untrusted-input surfaces. Currently one:

- **`fuzz_sofa_reader.cpp`** — the SOFA reader (`load_sofa` +
  `decompose_sh`), the library's only consumer of arbitrary external files.
  Exercises the whole path a host would drive: parse (libmysofa's HDF5
  reader), validate, and project onto the SH basis.

## Build & run

Requires Clang (for `-fsanitize=fuzzer`) and the SOFA reader; ASan+UBSan are
layered on so memory/UB bugs surface, not just crashes.

```bash
cmake -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DAMBITAP_ENABLE_SOFA=ON -DAMBITAP_BUILD_FUZZERS=ON \
      -DAMBITAP_BUILD_TESTS=OFF
cmake --build build --target fuzz_sofa_reader

# Deep campaign (hours): mutate from the seed corpus, saving new coverage.
ASAN_OPTIONS=detect_leaks=0 \
  ./build/tests/fuzz/fuzz_sofa_reader -rss_limit_mb=4096 tests/fuzz/corpus
```

CI runs a bounded version of this on every push (the `fuzz` job) as a
regression gate: it replays the seed corpus and does a few minutes of
exploratory fuzzing. It is not a substitute for a periodic deep run.

## Corpus

`corpus/` is a deliberately small seed set: one structurally valid SOFA file
to mutate from, plus hand-authored malformed cases (empty, non-HDF5,
HDF5-magic-only, truncated). libFuzzer expands coverage from these at run
time; new inputs are not checked in (they regenerate, and would only bloat
the repo).

## Findings

The first run found a real bug: `load_sofa` dereferenced `Data.IR`,
`SourcePosition`, and `Data.SamplingRate` through the declared dimensions
without checking the arrays were present and sized, so a truncated or
hostile file that parsed structurally read off a null/short buffer. Fixed in
`sofa_reader.h` (up-front array validation); regression-tested in
`tests/test_sofa.cpp`.
