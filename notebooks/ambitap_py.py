"""ctypes bridge to the AmbiTap C ABI, shared by the verification notebooks.

Loads build/tools/capi/libambitap_capi.{so,dylib,dll} relative to the
repository root, building it first if missing (requires cmake in PATH):

    cmake -B build -DAMBITAP_BUILD_CAPI=ON
    cmake --build build --target ambitap_capi

Every wrapper returns NumPy arrays and raises RuntimeError on a C-side error.

Copyright 2026 Timothy Place. MIT License.
"""

from __future__ import annotations

import ctypes
import pathlib
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent

# Categorical palette for the notebooks (colorblind-safe, fixed assignment
# order — never cycled). Sequential maps use viridis; diverging use RdBu_r.
PALETTE = ["#4269d0", "#efb118", "#ff725c", "#6cc5b0", "#3ca951", "#ff8ab7", "#a463f2"]


def _lib_path() -> pathlib.Path:
    stem = "ambitap_capi"
    names = {
        "linux": f"lib{stem}.so",
        "darwin": f"lib{stem}.dylib",
        "win32": f"{stem}.dll",
    }
    name = next(v for k, v in names.items() if sys.platform.startswith(k))
    return REPO / "build" / "tools" / "capi" / name


def _build_lib() -> None:
    subprocess.run(
        ["cmake", "-B", str(REPO / "build"), "-DAMBITAP_BUILD_CAPI=ON"],
        cwd=REPO, check=True, capture_output=True,
    )
    subprocess.run(
        ["cmake", "--build", str(REPO / "build"), "--target", "ambitap_capi", "--parallel"],
        cwd=REPO, check=True, capture_output=True,
    )


def load() -> ctypes.CDLL:
    path = _lib_path()
    if not path.exists():
        print("building ambitap_capi ...")
        _build_lib()
    lib = ctypes.CDLL(str(path))
    f32p = ctypes.POINTER(ctypes.c_float)
    i32p = ctypes.POINTER(ctypes.c_int)
    sigs = {
        "ambitap_channel_count": ([ctypes.c_int], ctypes.c_int),
        "ambitap_evaluate_sh": ([ctypes.c_int, ctypes.c_float, ctypes.c_float, f32p], ctypes.c_int),
        "ambitap_max_re_weights": ([ctypes.c_int, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_sh_rotation_matrix": (
            [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, f32p], ctypes.c_int),
        "ambitap_layout_preset": ([ctypes.c_char_p, f32p, f32p, ctypes.c_int], ctypes.c_int),
        "ambitap_vbap_gains": (
            [ctypes.c_int, f32p, f32p, ctypes.c_float, ctypes.c_float, f32p], ctypes.c_int),
        "ambitap_decoder_matrix": (
            [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, f32p, f32p, ctypes.c_int, f32p],
            ctypes.c_int),
        "ambitap_builtin_hrtf_info": ([i32p, i32p, i32p, f32p], ctypes.c_int),
        "ambitap_builtin_hrtf_fir": ([ctypes.c_int, ctypes.c_int, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_resample_fir": (
            [f32p, ctypes.c_int, ctypes.c_float, ctypes.c_float, f32p, ctypes.c_int], ctypes.c_int),
        "ambitap_convolve": (
            [f32p, ctypes.c_int, f32p, ctypes.c_int, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_binaural_render": (
            [ctypes.c_int, ctypes.c_float, ctypes.c_int, f32p, ctypes.c_int, f32p, f32p,
             ctypes.c_float, ctypes.c_float, ctypes.c_float, f32p, f32p], ctypes.c_int),
        "ambitap_encoder_ramp": (
            [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float,
             ctypes.c_int, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_decoder_crossfade": (
            [ctypes.c_int, ctypes.c_int, f32p, f32p, ctypes.c_char_p, ctypes.c_char_p,
             ctypes.c_int, f32p, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_doppler_process": (
            [ctypes.c_float, ctypes.c_float, f32p, f32p, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_compressor_gain": (
            [ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float,
             ctypes.c_float, f32p, ctypes.c_int, f32p], ctypes.c_int),
        "ambitap_soundfield_grid": (
            [ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_float, f32p, ctypes.c_int,
             ctypes.c_int, ctypes.c_float, f32p, f32p], ctypes.c_int),
        "ambitap_energy_vector": (
            [ctypes.c_int, ctypes.c_float, ctypes.c_float, f32p, ctypes.c_int, f32p],
            ctypes.c_int),
    }
    for name, (argtypes, restype) in sigs.items():
        fn = getattr(lib, name)
        fn.argtypes = argtypes
        fn.restype = restype
    return lib


_LIB = load()


def _f32(a) -> np.ndarray:
    return np.ascontiguousarray(a, dtype=np.float32)


def _ptr(a: np.ndarray):
    return a.ctypes.data_as(ctypes.POINTER(ctypes.c_float))


def _check(code: int, who: str) -> None:
    if code < 0:
        raise RuntimeError(f"{who} failed")


def channel_count(order: int) -> int:
    n = _LIB.ambitap_channel_count(order)
    _check(n, "channel_count")
    return n


def evaluate_sh(order: int, azimuth: float, elevation: float) -> np.ndarray:
    out = np.empty(channel_count(order), dtype=np.float32)
    _check(_LIB.ambitap_evaluate_sh(order, azimuth, elevation, _ptr(out)), "evaluate_sh")
    return out


def max_re_weights(order: int, energy_normalized: bool = False) -> np.ndarray:
    out = np.empty(order + 1, dtype=np.float32)
    _check(_LIB.ambitap_max_re_weights(order, int(energy_normalized), _ptr(out)),
           "max_re_weights")
    return out


def sh_rotation_matrix(order: int, yaw: float, pitch: float, roll: float) -> np.ndarray:
    c = channel_count(order)
    out = np.empty(c * c, dtype=np.float32)
    _check(_LIB.ambitap_sh_rotation_matrix(order, yaw, pitch, roll, _ptr(out)),
           "sh_rotation_matrix")
    return out.reshape(c, c)


def layout(name: str) -> tuple[np.ndarray, np.ndarray]:
    cap = 64
    az = np.empty(cap, dtype=np.float32)
    el = np.empty(cap, dtype=np.float32)
    n = _LIB.ambitap_layout_preset(name.encode(), _ptr(az), _ptr(el), cap)
    _check(n, f"layout({name})")
    return az[:n].copy(), el[:n].copy()


def vbap_gains(az: np.ndarray, el: np.ndarray, src_az: float, src_el: float) -> np.ndarray:
    az, el = _f32(az), _f32(el)
    out = np.empty(len(az), dtype=np.float32)
    _check(_LIB.ambitap_vbap_gains(len(az), _ptr(az), _ptr(el),
                                   float(src_az), float(src_el), _ptr(out)), "vbap_gains")
    return out


def decoder_matrix(algorithm: str, order: int, az: np.ndarray, el: np.ndarray,
                   use_max_re: bool = False) -> np.ndarray:
    az, el = _f32(az), _f32(el)
    L, C = len(az), channel_count(order)
    out = np.empty(L * C, dtype=np.float32)
    _check(_LIB.ambitap_decoder_matrix(algorithm.encode(), order, L, _ptr(az), _ptr(el),
                                       int(use_max_re), _ptr(out)), f"decoder({algorithm})")
    return out.reshape(L, C)


def builtin_hrtf_info() -> dict:
    order = ctypes.c_int()
    channels = ctypes.c_int()
    length = ctypes.c_int()
    fs = ctypes.c_float()
    _LIB.ambitap_builtin_hrtf_info(ctypes.byref(order), ctypes.byref(channels),
                                   ctypes.byref(length), ctypes.byref(fs))
    return {"order": order.value, "channels": channels.value,
            "length": length.value, "sample_rate": fs.value}


def builtin_hrtf(magls: bool = False) -> tuple[np.ndarray, np.ndarray]:
    """Both ears' SH-domain FIRs as (channels, length) arrays: (left, right)."""
    info = builtin_hrtf_info()
    ears = []
    for ear in (0, 1):
        firs = np.empty((info["channels"], info["length"]), dtype=np.float32)
        for ch in range(info["channels"]):
            _check(_LIB.ambitap_builtin_hrtf_fir(int(magls), ear, ch, _ptr(firs[ch])),
                   "builtin_hrtf_fir")
        ears.append(firs)
    return ears[0], ears[1]


def resample_fir(x: np.ndarray, in_rate: float, out_rate: float) -> np.ndarray:
    x = _f32(x)
    cap = int(np.ceil(len(x) * out_rate / in_rate)) + 8
    out = np.empty(cap, dtype=np.float32)
    n = _LIB.ambitap_resample_fir(_ptr(x), len(x), float(in_rate), float(out_rate),
                                  _ptr(out), cap)
    _check(n, "resample_fir")
    return out[:n].copy()


def convolve(x: np.ndarray, ir: np.ndarray, block_size: int = 64) -> np.ndarray:
    x, ir = _f32(x), _f32(ir)
    out = np.empty(len(x), dtype=np.float32)
    _check(_LIB.ambitap_convolve(_ptr(x), len(x), _ptr(ir), len(ir), block_size, _ptr(out)),
           "convolve")
    return out


def binaural_render(mono: np.ndarray, az: np.ndarray, el: np.ndarray, *, order: int = 3,
                    sample_rate: float = 48000.0, magls: bool = False,
                    head=(0.0, 0.0, 0.0)) -> tuple[np.ndarray, np.ndarray]:
    mono, az, el = _f32(mono), _f32(az), _f32(el)
    assert len(az) == len(mono) and len(el) == len(mono)
    left = np.empty(len(mono), dtype=np.float32)
    right = np.empty(len(mono), dtype=np.float32)
    _check(_LIB.ambitap_binaural_render(order, float(sample_rate), int(magls), _ptr(mono),
                                        len(mono), _ptr(az), _ptr(el), float(head[0]),
                                        float(head[1]), float(head[2]), _ptr(left),
                                        _ptr(right)), "binaural_render")
    return left, right


def encoder_ramp(order: int, az0: float, el0: float, az1: float, el1: float,
                 change_at: int, n_frames: int) -> np.ndarray:
    """dsp::encoder output (C, n_frames) for a DC-1 input with a direction
    change at sample change_at — the parameter-smoothing ramp, verbatim."""
    c = channel_count(order)
    out = np.empty(c * n_frames, dtype=np.float32)
    _check(_LIB.ambitap_encoder_ramp(order, float(az0), float(el0), float(az1), float(el1),
                                     change_at, n_frames, _ptr(out)), "encoder_ramp")
    return out.reshape(c, n_frames)


def decoder_crossfade(order: int, az: np.ndarray, el: np.ndarray, from_alg: str, to_alg: str,
                      hoa: np.ndarray, n_frames: int, use_max_re: bool = False) -> np.ndarray:
    """dsp::decoder per-speaker output (L, n_frames) for a constant HOA frame,
    starting at the first sample of the from_alg -> to_alg matrix crossfade."""
    az, el, hoa = _f32(az), _f32(el), _f32(hoa)
    L = len(az)
    assert len(hoa) == channel_count(order)
    out = np.empty(L * n_frames, dtype=np.float32)
    _check(_LIB.ambitap_decoder_crossfade(order, L, _ptr(az), _ptr(el), from_alg.encode(),
                                          to_alg.encode(), int(use_max_re), _ptr(hoa),
                                          n_frames, _ptr(out)), "decoder_crossfade")
    return out.reshape(L, n_frames)


def doppler_process(mono: np.ndarray, dist: np.ndarray, *, sample_rate: float,
                    max_distance: float) -> np.ndarray:
    """dsp::doppler applied to a mono signal along a per-sample distance
    trajectory (meters); returns the delayed signal (Doppler pitch shift)."""
    mono, dist = _f32(mono), _f32(dist)
    assert len(dist) == len(mono)
    out = np.empty(len(mono), dtype=np.float32)
    _check(_LIB.ambitap_doppler_process(float(sample_rate), float(max_distance), _ptr(mono),
                                        _ptr(dist), len(mono), _ptr(out)), "doppler_process")
    return out


def compressor_gain(w: np.ndarray, *, sample_rate: float, threshold_db: float, ratio: float,
                    attack_s: float = 0.005, release_s: float = 0.1,
                    makeup_db: float = 0.0) -> np.ndarray:
    """dsp::spatial_compressor per-sample linear gain for the W signal."""
    w = _f32(w)
    out = np.empty(len(w), dtype=np.float32)
    _check(_LIB.ambitap_compressor_gain(float(sample_rate), float(threshold_db), float(ratio),
                                        float(attack_s), float(release_s), float(makeup_db),
                                        _ptr(w), len(w), _ptr(out)), "compressor_gain")
    return out


def soundfield_grid(hoa: np.ndarray, *, order: int, az_steps: int = 64,
                    sample_rate: float = 48000.0, smoothing_ms: float = 200.0,
                    block_size: int = 256,
                    dynamic_range_db: float = 30.0) -> tuple[np.ndarray, float]:
    """analysis::soundfield_grid heatmap for planar HOA (C, n_frames): the
    normalized (el_steps, az_steps) image (row 0 = zenith, col 0 = az -pi)
    and the absolute peak in dB."""
    hoa = _f32(hoa)
    c, n = hoa.shape
    assert c == channel_count(order)
    out = np.empty((az_steps // 2) * az_steps, dtype=np.float32)
    peak = ctypes.c_float()
    _check(_LIB.ambitap_soundfield_grid(order, az_steps, float(sample_rate),
                                        float(smoothing_ms), _ptr(hoa), n, block_size,
                                        float(dynamic_range_db), _ptr(out),
                                        ctypes.byref(peak)), "soundfield_grid")
    return out.reshape(az_steps // 2, az_steps), peak.value


def energy_vector(hoa: np.ndarray, *, order: int, sample_rate: float = 48000.0,
                  smoothing_s: float = 0.01) -> np.ndarray:
    """analysis::energy_vector trajectory (3, n_frames) — smoothed active
    intensity x/y/z (x = front, y = left, z = up) — for planar HOA (C, n)."""
    hoa = _f32(hoa)
    c, n = hoa.shape
    assert c == channel_count(order)
    out = np.empty(3 * n, dtype=np.float32)
    _check(_LIB.ambitap_energy_vector(order, float(sample_rate), float(smoothing_s),
                                      _ptr(hoa), n, _ptr(out)), "energy_vector")
    return out.reshape(3, n)


def direction_grid(n_az: int = 121, n_el: int = 61):
    """Equirectangular direction grid: (az[n_az], el[n_el], AZ, EL meshes)."""
    az = np.linspace(-np.pi, np.pi, n_az)
    el = np.linspace(-np.pi / 2, np.pi / 2, n_el)
    AZ, EL = np.meshgrid(az, el)
    return az, el, AZ, EL


def sh_matrix(order: int, az_flat: np.ndarray, el_flat: np.ndarray) -> np.ndarray:
    """(N, C) SH basis evaluated by the C++ core at N directions."""
    return np.stack([evaluate_sh(order, float(a), float(e))
                     for a, e in zip(az_flat, el_flat)])
