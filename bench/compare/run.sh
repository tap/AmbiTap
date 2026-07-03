#!/usr/bin/env bash
# Cross-library C++ benchmark: AmbiTap vs libspatialaudio vs SAF.
# Clones/builds the other two libraries next to this script, compiles the
# harnesses, and runs all three benchmarks. Results feed docs/COMPARISON.md.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
WORK="$HERE/_work"
mkdir -p "$WORK"

# --- AmbiTap (this repo) ---------------------------------------------------
cmake -S "$ROOT" -B "$WORK/ambitap-build" -DCMAKE_BUILD_TYPE=Release \
      -DAMBITAP_BUILD_BENCH=ON -DAMBITAP_BUILD_TESTS=OFF -DAMBITAP_BUILD_EXAMPLES=OFF
cmake --build "$WORK/ambitap-build" -j"$(nproc)"

# --- libspatialaudio ---------------------------------------------------------
[ -d "$WORK/libspatialaudio" ] || git clone --depth 1 \
    https://github.com/videolabs/libspatialaudio.git "$WORK/libspatialaudio"
cmake -S "$WORK/libspatialaudio" -B "$WORK/libspatialaudio/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/libspatialaudio/build" -j"$(nproc)"
g++ -O3 -std=c++17 -o "$WORK/bench_lsa" "$HERE/bench_libspatialaudio.cpp" \
    -I "$WORK/libspatialaudio/include" -I "$WORK/libspatialaudio/build/include" \
    -L "$WORK/libspatialaudio/build" -lspatialaudio \
    -Wl,-rpath,"$WORK/libspatialaudio/build" -lm

# --- SAF (needs OpenBLAS + LAPACKE; e.g. apt install libopenblas-dev liblapacke-dev)
[ -d "$WORK/SAF" ] || git clone --depth 1 \
    https://github.com/leomccormack/Spatial_Audio_Framework.git "$WORK/SAF"
cmake -S "$WORK/SAF" -B "$WORK/SAF/build" -DCMAKE_BUILD_TYPE=Release \
      -DSAF_PERFORMANCE_LIB=SAF_USE_OPEN_BLAS_AND_LAPACKE
cmake --build "$WORK/SAF/build" -j"$(nproc)"
gcc -O3 -o "$WORK/bench_saf" "$HERE/bench_saf.c" \
    -I "$WORK/SAF/examples/include" -I "$WORK/SAF/framework/include" \
    "$WORK/SAF/build/examples/libsaf_example_ambi_enc.a" \
    "$WORK/SAF/build/examples/libsaf_example_ambi_dec.a" \
    "$WORK/SAF/build/examples/libsaf_example_ambi_bin.a" \
    "$WORK/SAF/build/examples/libsaf_example_rotator.a" \
    "$WORK/SAF/build/framework/libsaf.a" -lopenblas -llapacke -lm -lpthread

echo; echo "================ AmbiTap ================"
"$WORK/ambitap-build/bench/ambitap_bench"
echo; echo "============ libspatialaudio ============"
"$WORK/bench_lsa"
echo; echo "=================== SAF ================="
"$WORK/bench_saf"
