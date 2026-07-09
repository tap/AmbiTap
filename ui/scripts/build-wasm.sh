#!/usr/bin/env bash
# Build the C ABI (tools/capi) to a standalone WebAssembly module for the
# browser AudioWorklet host (UI.md wave 2). Requires emscripten (emcc on
# PATH, or EMSDK set) and an Eigen checkout — by default the one FetchContent
# put in a sibling CMake build dir; override with AMBITAP_EIGEN_DIR.
#
#   cd ui && ./scripts/build-wasm.sh
#
# Output: dist/wasm/ambitap.wasm — plain standalone WASM, no JS glue; the
# loader (hosts/web/ambitap-wasm.ts) instantiates it with tiny WASI stubs.
set -euo pipefail

cd "$(dirname "$0")/.."
LIB_ROOT=".."

if ! command -v em++ >/dev/null 2>&1; then
    if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
    fi
fi
command -v em++ >/dev/null 2>&1 || { echo "em++ not found: install emsdk or set EMSDK" >&2; exit 1; }

EIGEN_DIR="${AMBITAP_EIGEN_DIR:-}"
if [ -z "$EIGEN_DIR" ]; then
    for candidate in "$LIB_ROOT"/build*/_deps/eigen-src /usr/include/eigen3; do
        if [ -f "$candidate/Eigen/Core" ]; then
            EIGEN_DIR="$candidate"
            break
        fi
    done
fi
[ -n "$EIGEN_DIR" ] && [ -f "$EIGEN_DIR/Eigen/Core" ] || {
    echo "Eigen not found: configure a CMake build of the library first" >&2
    echo "(cmake -B build -DAMBITAP_BUILD_CAPI=ON) or set AMBITAP_EIGEN_DIR" >&2
    exit 1
}

EXPORTS=_malloc,_free
EXPORTS+=,_ambitap_channel_count,_ambitap_evaluate_sh,_ambitap_max_re_weights
EXPORTS+=,_ambitap_encoder_create,_ambitap_encoder_destroy,_ambitap_encoder_set_direction
EXPORTS+=,_ambitap_encoder_set_gain,_ambitap_encoder_process
EXPORTS+=,_ambitap_grid_create,_ambitap_grid_destroy,_ambitap_grid_process,_ambitap_grid_snapshot
EXPORTS+=,_ambitap_vector_create,_ambitap_vector_destroy,_ambitap_vector_process,_ambitap_vector_value

mkdir -p dist/wasm
em++ "$LIB_ROOT/tools/capi/ambitap_capi.cpp" \
    -I "$LIB_ROOT/include" -isystem "$EIGEN_DIR" \
    -std=c++20 -O2 -fwasm-exceptions \
    --no-entry \
    -sEXPORTED_FUNCTIONS="$EXPORTS" \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=16MB \
    -o dist/wasm/ambitap.wasm

ls -la dist/wasm/ambitap.wasm
