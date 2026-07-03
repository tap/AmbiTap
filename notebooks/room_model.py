"""Offline SH-domain room prototype for notebooks/room_verification.ipynb.

This is the Wave-3 `ambitap.room~` *architecture prototype* — pure Python /
NumPy, no real-time constraints — built to be evaluated against the R1-R10
gates in docs/PERCEPTUAL-VERIFICATION.md before any C++ DSP is written. It
reaches the library through the C ABI (ambitap_py) for everything that must
match the shipped math bit-for-bit: SH encoding (`evaluate_sh`), the built-in
KEMAR SH HRTF set, and the windowed-sinc FIR resampler.

Model structure (mirrors the ROADMAP item-4 design frame):

  direct + early reflections   shoebox image-source model (Allen & Berkley),
  (t < er_cutoff)              each image encoded as an HOA point source at
                               its exact direction with amplitude prod(beta)/r
                               placed at the nearest sample — exactly
                               checkable against closed-form ground truth
                               (gates R1-R3).

  late tail (t >= er_cutoff)   one of two interchangeable architectures:

    'fdn'   16-line feedback delay network in the SH domain.
            - mutually-prime delays 431..3989 samples (9..83 ms @ 48 kHz;
              the short end bridges the early-recirculation gap);
            - Hadamard feedback matrix with random +/-1 diagonal conjugation
              (orthogonal, lossless);
            - per-line linear-phase FIR absorption filters fitted to
              g_i(f) = 10^(-3 (L_i + D) / (fs * T60(f))) so every octave
              band decays at the parameterized RT60 (D = FIR group delay,
              counted as part of the loop);
            - per-line unit-energy noise input bursts (~53 ms, octave-band
              shaped to the parameterized decay), injection time-aligned per
              line so all lines first fire on the same sample: instant echo
              density, decorrelated line states, and an exponential energy
              envelope from the tail's first sample. The C++ equivalent is
              per-line input FIRs, standard Jot practice;
            - output taps: a second independent signed Hadamard mixing
              matrix distributes the 16 line outputs across the 16 order-3
              SH channels, each row scaled by the SN3D diffuse-field target
              1/sqrt(2n+1) for channel order n;
            - a per-channel velvet-noise decorrelation stage was tried and
              REJECTED (tail_kind 'fdn_velvet' keeps it measurable): it
              trims |rE| and broadband IACC but its short sparse FIRs are
              spectrally uneven near 500 Hz and break the per-band
              diffuse-coherence tracking (R9) on some seeds.

    'conv'  synthesized-convolution tail: per SH channel an independent
            white-noise sequence, split into octave bands with brick-wall
            FFT masks, each band enveloped by exp(-6.91 t / T60(band)),
            summed, scaled by the same per-order SN3D diffuse-field weights.
            Uncorrelated across channels by construction — the comparison
            point for the R7/R9 architecture decision.

  level calibration            the tail's omni (W) energy is scaled so it
                               continues the SAME image-source model that
                               produced the ERs (the image sum beyond
                               er_cutoff, enumerated then exponentially
                               extrapolated — tail_energy_target). A junction
                               energy knee would corrupt EDT and clarity; the
                               R6 gate checks the rendered C50/C80 against
                               this exact parameterization.

Every random draw goes through one np.random.default_rng(seed) per render,
so a fixed seed gives byte-identical output (gate R10).

Copyright 2026 Timothy Place. MIT License.
"""

from __future__ import annotations

import dataclasses

import numpy as np
import scipy.linalg
import scipy.signal

import ambitap_py as at

SPEED_OF_SOUND = 343.0  # m/s

# 16 mutually-prime FDN delay lengths, 9..83 ms at 48 kHz. The short end
# matters: early recirculation density has to bridge the gap between the
# injected first generation and the dense mixed field, or the tail's energy
# envelope dips right where EDT is measured.
FDN_DELAYS = np.array([431, 541, 677, 839, 1039, 1201, 1451, 1693,
                       1949, 2243, 2531, 2857, 3163, 3467, 3697, 3989])
FDN_ABSORPTION_TAPS = 255          # linear phase; group delay 127 samples
FDN_BURST_LEN = 2560               # per-line input noise burst, ~53 ms
VELVET_LEN = 960                   # per-channel decorrelator length, 20 ms
VELVET_TAPS = 24                   # sparse impulses per decorrelator


@dataclasses.dataclass(frozen=True)
class RoomParams:
    """Shoebox parameterization shared by the renderer and the gate math."""
    # Geometry chosen so the first ~22 image-source arrivals are >= 8 samples
    # apart at 48 kHz (isolable for the per-arrival R1-R3 measurements) and so
    # the whole 1.2/2.4/3.6 m distance sweep stays inside the room.
    dims: tuple[float, float, float] = (7.10, 5.30, 3.10)   # m; x front, y left, z up
    source: tuple[float, float, float] = (3.674, 1.137, 1.977)
    listener: tuple[float, float, float] = (1.746, 1.711, 0.668)
    # Wall amplitude reflection coefficients (x0, x1, y0, y1, z0, z1);
    # frequency-independent for v1, roughly consistent with rt60 via Sabine.
    beta: tuple[float, ...] = (0.90, 0.92, 0.91, 0.93, 0.89, 0.94)
    # Parameterized RT60 per octave band (the tail's contract, gates R4/R5).
    rt60: dict[int, float] = dataclasses.field(default_factory=lambda: {
        250: 0.90, 500: 0.84, 1000: 0.76, 2000: 0.66, 4000: 0.54})
    order: int = 3
    fs: int = 48000
    # Image sources rendered below er_cutoff, the statistical tail above it.
    # 30 ms sits just past this room's mixing time (~2*sqrt(V) ms ~ 21 ms) and
    # still contains the first 20+ arrivals the R1-R3 gates need. A longer ER
    # span measurably biases EDT and T20: the v1 image-source field is
    # frequency-flat (it decays at the Eyring rate the walls imply, ~0.73 s)
    # while the tail realizes the band-dependent parameterized RT60, so every
    # extra ER millisecond drags each band's early decay toward the flat rate.
    er_cutoff: float = 0.030
    length: float = 2.0            # s; rendered IR length
    # The committed seed is part of the parameterization (gate R10 renders it
    # twice, byte-compared). The notebook also reports an informational
    # multi-seed sweep: single-realization T20/EDT/IACC estimates in the
    # narrow low bands have statistical spread comparable to the R4/R5/R9
    # tolerances, so not every seed passes every band.
    seed: int = 11

    @property
    def channels(self) -> int:
        return (self.order + 1) ** 2

    @property
    def n_samples(self) -> int:
        return round(self.length * self.fs)

    @property
    def distance(self) -> float:
        return float(np.linalg.norm(np.subtract(self.source, self.listener)))


def channel_orders(order: int) -> np.ndarray:
    """SH order n of each ACN channel: [0, 1,1,1, 2,2,2,2,2, ...]."""
    return np.array([n for n in range(order + 1) for _ in range(2 * n + 1)])


def sn3d_diffuse_gains(order: int) -> np.ndarray:
    """Per-channel diffuse-field amplitude targets in SN3D: 1/sqrt(2n+1).

    An isotropic ensemble of SN3D-encoded plane waves has per-channel energy
    1/(2n+1) relative to W (addition theorem) — this is the shaping that
    makes the tail decode without order-dependent coloration (gate R7).
    """
    return 1.0 / np.sqrt(2.0 * channel_orders(order) + 1.0)


def direction_to_azel(v: np.ndarray) -> tuple[float, float]:
    """Cartesian (x front, y left, z up) -> (azimuth, elevation), radians."""
    az = float(np.arctan2(v[1], v[0]))
    el = float(np.arctan2(v[2], np.hypot(v[0], v[1])))
    return az, el


# ---------------------------------------------------------------------------
# Image-source early reflections (gates R1-R3)
# ---------------------------------------------------------------------------

def image_source_list(params: RoomParams, t_max: float | None = None) -> list[dict]:
    """All shoebox image sources arriving before t_max, sorted by time.

    Allen & Berkley enumeration: image position along each axis is
    (1-2p)*src + 2rL, with amplitude beta_wall0^|r-p| * beta_wall1^|r| per
    axis and 1/distance spreading. Returns closed-form ground truth: exact
    fractional arrival time (seconds and samples), direction, amplitude.
    """
    if t_max is None:
        t_max = params.length
    L = np.asarray(params.dims)
    s = np.asarray(params.source)
    m = np.asarray(params.listener)
    b = params.beta
    d_max = t_max * SPEED_OF_SOUND
    n_range = [int(np.ceil(d_max / (2 * L[a]))) + 1 for a in range(3)]

    out = []
    for px in (0, 1):
        for py in (0, 1):
            for pz in (0, 1):
                p = np.array([px, py, pz])
                for rx in range(-n_range[0], n_range[0] + 1):
                    for ry in range(-n_range[1], n_range[1] + 1):
                        for rz in range(-n_range[2], n_range[2] + 1):
                            r = np.array([rx, ry, rz])
                            img = (1 - 2 * p) * s + 2 * r * L
                            v = img - m
                            dist = float(np.linalg.norm(v))
                            t = dist / SPEED_OF_SOUND
                            if t > t_max or dist < 1e-6:
                                continue
                            refl = np.abs(r - p).sum() + np.abs(r).sum()
                            amp = (b[0] ** abs(rx - px) * b[1] ** abs(rx)
                                   * b[2] ** abs(ry - py) * b[3] ** abs(ry)
                                   * b[4] ** abs(rz - pz) * b[5] ** abs(rz)) / dist
                            az, el = direction_to_azel(v / dist)
                            out.append({
                                "time": t, "sample": t * params.fs,
                                "distance": dist, "amplitude": float(amp),
                                "azimuth": az, "elevation": el,
                                "reflections": int(refl),
                            })
    out.sort(key=lambda e: e["time"])
    return out


def render_early_reflections(params: RoomParams) -> tuple[np.ndarray, list[dict]]:
    """Direct sound + image sources with t < er_cutoff as an SH IR (C, T).

    Each arrival is one sample (nearest-sample placement, <= 0.5 sample
    timing error) encoded through the library's own evaluate_sh via the
    C ABI — the encoding is the shipped math, not a Python mirror.
    """
    images = image_source_list(params, t_max=params.er_cutoff)
    ir = np.zeros((params.channels, params.n_samples))
    for img in images:
        n = round(img["sample"])
        if n >= params.n_samples:
            continue
        sh = at.evaluate_sh(params.order, img["azimuth"], img["elevation"])
        ir[:, n] += img["amplitude"] * sh.astype(np.float64)
    return ir, images


# ---------------------------------------------------------------------------
# Parameterized decay model shared by the tails and the R6 analytic gate
# ---------------------------------------------------------------------------

def rt60_of_freq(params: RoomParams, f: np.ndarray) -> np.ndarray:
    """T60(f): log-frequency interpolation across the parameterized octave
    bands, extrapolated beyond the outer bands with the boundary octave
    slope (floored at 0.1 s).

    The extrapolation matters for the R4 gate semantics: T20 is measured
    per octave BAND but parameterized at the band CENTER. If T60(f) went
    flat above the top band, the 4 kHz octave would average slower-decaying
    content below its center against flat content above it and read several
    percent high. Continuing the slope keeps each parameterized center
    representative of its band average (and a HF roll-off is also the
    physical behavior of air absorption).
    """
    centers = np.array(sorted(params.rt60))
    values = np.array([params.rt60[c] for c in centers])
    f = np.maximum(np.asarray(f, dtype=np.float64), 1.0)
    lf = np.log(f)
    lc = np.log(centers)
    out = np.interp(lf, lc, values)
    slope_lo = (values[1] - values[0]) / (lc[1] - lc[0])
    slope_hi = (values[-1] - values[-2]) / (lc[-1] - lc[-2])
    out = np.where(lf < lc[0], values[0] + slope_lo * (lf - lc[0]), out)
    out = np.where(lf > lc[-1], values[-1] + slope_hi * (lf - lc[-1]), out)
    return np.maximum(out, 0.1)


def tail_energy_fraction(params: RoomParams, t: np.ndarray) -> np.ndarray:
    """Fraction of reverberant energy remaining after time t (re. the direct
    arrival) under the parameterized model: flat spectral density, each
    frequency decaying as exp(-13.8 (t - t_direct) / T60(f))."""
    f = np.linspace(20.0, params.fs / 2, 512)
    t60 = rt60_of_freq(params, f)
    t_direct = params.distance / SPEED_OF_SOUND
    dt = np.maximum(np.asarray(t, dtype=np.float64) - t_direct, 0.0)
    # E(>t) integrated per frequency: (T60/13.8) e^{-13.8 dt/T60}; normalize
    # by the same integral at dt=0.
    num = (t60[None, :] * np.exp(-13.8 * dt[..., None] / t60[None, :])).sum(axis=-1)
    den = t60.sum()
    return num / den


def sabine_absorption_area(params: RoomParams) -> float:
    """Equivalent absorption area from the mid-band parameterized RT60."""
    lx, ly, lz = params.dims
    volume = lx * ly * lz
    t60_mid = rt60_of_freq(params, np.array([1000.0]))[0]
    return 0.161 * volume / t60_mid


def tail_energy_target(params: RoomParams, t_enum: float = 0.25) -> float:
    """Omni (W) energy the tail should carry from er_cutoff onward.

    Self-consistent with the early reflections: the tail continues the SAME
    image-source model that produced the ERs. The image sum is enumerated to
    t_enum, and the (small) remainder beyond t_enum is extrapolated with the
    parameterized mid-band exponential decay — so the ER->tail junction has
    no energy knee (which would corrupt EDT and clarity). Deterministic,
    closed-form given the parameterization.
    """
    images = image_source_list(params, t_max=t_enum)
    e_mid = sum(e["amplitude"] ** 2 for e in images
                if params.er_cutoff <= e["time"] < t_enum)
    # extrapolate E(> t_enum) from the last 50 ms of the enumeration
    t_fit = t_enum - 0.05
    e_fit = sum(e["amplitude"] ** 2 for e in images if t_fit <= e["time"] < t_enum)
    rate = 13.8 / rt60_of_freq(params, np.array([1000.0]))[0]
    e_late = e_fit / (np.exp(rate * (t_enum - t_fit)) - 1.0)
    return float(e_mid + e_late)


# ---------------------------------------------------------------------------
# Tail architecture 1: SH-domain FDN
# ---------------------------------------------------------------------------

def _absorption_fir(loop_delay: int, params: RoomParams) -> np.ndarray:
    """Linear-phase FIR realizing per-frequency loop attenuation
    10^(-3 * loop_delay_eff / (fs * T60(f))); the FIR's own group delay is
    part of the effective loop length."""
    taps = FDN_ABSORPTION_TAPS
    group_delay = (taps - 1) // 2
    l_eff = loop_delay + group_delay
    nyq = params.fs / 2
    f = np.concatenate([[0.0], np.geomspace(20.0, nyq, 383)])
    f[-1] = nyq
    gain = 10.0 ** (-3.0 * l_eff / (params.fs * rt60_of_freq(params, f)))
    return scipy.signal.firwin2(taps, f / nyq, gain)


def _signed_hadamard(n: int, rng: np.random.Generator) -> np.ndarray:
    """Orthogonal mixing matrix: Hadamard conjugated by random +/-1 signs."""
    h = scipy.linalg.hadamard(n).astype(np.float64) / np.sqrt(n)
    s1 = rng.choice([-1.0, 1.0], size=n)
    s2 = rng.choice([-1.0, 1.0], size=n)
    return (s1[:, None] * h) * s2[None, :]


def _velvet_decorrelator(rng: np.random.Generator) -> np.ndarray:
    """Sparse unit-energy velvet-noise FIR (~20 ms): random-sign impulses,
    one per equal time slot, exponentially decaying taps. Convolving each SH
    channel with its own draw randomizes the residual inter-channel phase
    the FDN recirculation leaves behind, at ~zero spectral cost."""
    fir = np.zeros(VELVET_LEN)
    slot = VELVET_LEN // VELVET_TAPS
    positions = np.arange(VELVET_TAPS) * slot + rng.integers(0, slot, VELVET_TAPS)
    signs = rng.choice([-1.0, 1.0], size=VELVET_TAPS)
    decay = np.exp(-3.0 * np.arange(VELVET_TAPS) / VELVET_TAPS)
    fir[positions] = signs * decay
    return fir / np.linalg.norm(fir)


def render_fdn_tail(params: RoomParams, n_samples: int, rng: np.random.Generator,
                    decorrelate: bool = False) -> np.ndarray:
    """SH-domain FDN tail (C, n_samples); sample 0 is the tail onset.

    See the module docstring for the design. Block-processed: with block
    size <= min(delays), every sample a block needs is already history.
    decorrelate=True adds a per-channel velvet-noise stage — a rejected
    iteration kept for the notebook's R8/R9 architecture comparison: it
    lowers |rE| and broadband IACC slightly but its short sparse FIRs are
    spectrally uneven near 500 Hz, degrading the per-band diffuse-coherence
    tracking on some seeds.
    """
    delays = FDN_DELAYS
    n_lines = len(delays)
    l_max = int(delays.max())
    taps = FDN_ABSORPTION_TAPS

    firs = np.stack([_absorption_fir(int(d), params) for d in delays])
    feedback = _signed_hadamard(n_lines, rng)
    out_mix = _signed_hadamard(n_lines, rng)[: params.channels]

    # Per-line unit-energy noise bursts, injected so every line's first
    # output (its own burst after one traversal) lands at internal time
    # l_max: line i is injected at l_max - delays[i]. The bursts are long
    # enough (~53 ms) to bridge the gap until second-generation recirculation
    # arrives, and are octave-band shaped to the parameterized decay so the
    # tail's per-band energy envelope is exponential from its very first
    # sample (a spiky or frequency-flat onset corrupts the R4/R6 contracts).
    # In the C++ FDN these bursts are per-line input FIRs (Jot-style).
    bursts = np.stack([_band_shaped_noise(params, FDN_BURST_LEN, rng)
                       for _ in range(n_lines)])
    bursts /= np.linalg.norm(bursts, axis=1, keepdims=True)

    t_int = l_max + n_samples
    x = np.zeros((n_lines, t_int))          # line inputs (post feedback sum)
    o = np.zeros((n_lines, t_int))          # line outputs (delayed inputs)
    inject = np.zeros((n_lines, t_int))
    for i in range(n_lines):
        start = l_max - int(delays[i])
        inject[i, start:start + FDN_BURST_LEN] = bursts[i]

    block = 384
    assert block <= delays.min()
    for t0 in range(0, t_int, block):
        t1 = min(t0 + block, t_int)
        for i in range(n_lines):
            src0 = t0 - int(delays[i])
            seg = np.zeros(t1 - t0)
            lo = max(src0, 0)
            hi = t1 - int(delays[i])
            if hi > lo:
                seg[lo - src0:] = x[i, lo:hi]
            o[i, t0:t1] = seg
            # absorption FIR over the loop output, with 'taps' of history
            h0 = max(t0 - taps + 1, 0)
            filt = np.convolve(o[i, h0:t1], firs[i])
            x[i, t0:t1] = filt[t0 - h0:t0 - h0 + (t1 - t0)]
        x[:, t0:t1] = inject[:, t0:t1] + feedback @ x[:, t0:t1]

    lines = o[:, l_max:]
    tail = out_mix @ lines
    if decorrelate:
        for ch in range(params.channels):
            fir = _velvet_decorrelator(rng)
            tail[ch] = scipy.signal.fftconvolve(tail[ch], fir)[:n_samples]
    tail *= sn3d_diffuse_gains(params.order)[:, None]
    return tail


# ---------------------------------------------------------------------------
# Tail architecture 2: synthesized convolution (shaped decorrelated noise)
# ---------------------------------------------------------------------------

def _band_shaped_noise(params: RoomParams, n_samples: int,
                       rng: np.random.Generator) -> np.ndarray:
    """White noise realizing the parameterized decay exactly per octave band:
    the spectrum is split with brick-wall FFT masks at the octave edges
    (31.25 Hz .. 16 kHz centers; first band reaches DC, last reaches
    Nyquist) and each band is enveloped by exp(-6.91 t / T60(center)).

    Brick-wall splitting is the point: IIR octave filters have skirts
    shallow enough that a slower-decaying neighbor band eventually dominates
    inside a faster band, bending its Schroeder curve upward — measured as a
    several-percent T20 bias (gate R4). Offline (and in a C++ IR-synthesis
    step) exact masks are free of that leakage.
    """
    centers = 31.25 * 2.0 ** np.arange(10)
    t = np.arange(n_samples) / params.fs
    noise = rng.standard_normal(n_samples)
    spec = np.fft.rfft(noise)
    f = np.fft.rfftfreq(n_samples, 1.0 / params.fs)
    out = np.zeros(n_samples)
    for i, c in enumerate(centers):
        lo = 0.0 if i == 0 else c / np.sqrt(2.0)
        hi = np.inf if i == len(centers) - 1 else c * np.sqrt(2.0)
        band = np.fft.irfft(np.where((f >= lo) & (f < hi), spec, 0.0), n_samples)
        t60 = float(rt60_of_freq(params, np.array([c]))[0])
        out += band * np.exp(-6.91 * t / t60)
    return out


def render_conv_tail(params: RoomParams, n_samples: int,
                     rng: np.random.Generator) -> np.ndarray:
    """Synthesized-convolution tail (C, n_samples): independent band-shaped
    noise per SH channel (see _band_shaped_noise) with per-order SN3D
    diffuse gains. Uncorrelated across channels by construction."""
    tail = np.stack([_band_shaped_noise(params, n_samples, rng)
                     for _ in range(params.channels)])
    tail *= sn3d_diffuse_gains(params.order)[:, None]
    return tail


# ---------------------------------------------------------------------------
# Full room render
# ---------------------------------------------------------------------------

def render_room(params: RoomParams, tail_kind: str = "fdn") -> dict:
    """Render the full SH room IR: direct + image-source ERs below er_cutoff,
    the selected tail from er_cutoff on, calibrated to the diffuse-field
    level target. Deterministic for a fixed params.seed (gate R10)."""
    rng = np.random.default_rng(params.seed)
    er, images = render_early_reflections(params)

    n0 = round(params.er_cutoff * params.fs)
    n_tail = params.n_samples - n0
    if tail_kind == "fdn":
        tail = render_fdn_tail(params, n_tail, rng)
    elif tail_kind == "fdn_velvet":
        tail = render_fdn_tail(params, n_tail, rng, decorrelate=True)
    elif tail_kind == "conv":
        tail = render_conv_tail(params, n_tail, rng)
    else:
        raise ValueError(f"unknown tail_kind {tail_kind!r}")

    scale = np.sqrt(tail_energy_target(params) / np.sum(tail[0] ** 2))
    tail = tail * scale

    ir = er.copy()
    ir[:, n0:] += tail
    tail_full = np.zeros_like(er)
    tail_full[:, n0:] = tail
    return {"ir": ir, "er": er, "tail": tail_full, "images": images,
            "onset_sample": n0, "params": params}


# ---------------------------------------------------------------------------
# Binauralization (KEMAR SH set through the C ABI) and isotropic reference
# ---------------------------------------------------------------------------

_HRTF_CACHE: dict[tuple[int, float], tuple[np.ndarray, np.ndarray]] = {}


def hrtf_firs(order: int, fs: float) -> tuple[np.ndarray, np.ndarray]:
    """Built-in KEMAR SH FIRs truncated to the render order and resampled to
    fs through the library's own windowed-sinc resampler — exactly what
    dsp::binaural_renderer::prepare() does."""
    key = (order, float(fs))
    if key not in _HRTF_CACHE:
        info = at.builtin_hrtf_info()
        left, right = at.builtin_hrtf(magls=False)
        c = (order + 1) ** 2
        if fs != info["sample_rate"]:
            left = np.stack([at.resample_fir(f, info["sample_rate"], fs)
                             for f in left[:c]])
            right = np.stack([at.resample_fir(f, info["sample_rate"], fs)
                              for f in right[:c]])
        else:
            left, right = left[:c], right[:c]
        _HRTF_CACHE[key] = (left.astype(np.float64), right.astype(np.float64))
    return _HRTF_CACHE[key]


def binauralize(sh_ir: np.ndarray, fs: float) -> tuple[np.ndarray, np.ndarray]:
    """(C, T) SH signal -> (left, right): per-channel HRTF convolution and
    sum, the binaural_renderer signal path."""
    c = sh_ir.shape[0]
    order = int(np.sqrt(c)) - 1
    left_firs, right_firs = hrtf_firs(order, fs)
    left = sum(scipy.signal.fftconvolve(sh_ir[ch], left_firs[ch]) for ch in range(c))
    right = sum(scipy.signal.fftconvolve(sh_ir[ch], right_firs[ch]) for ch in range(c))
    return left, right


def fibonacci_sphere(n: int) -> tuple[np.ndarray, np.ndarray]:
    """Deterministic near-uniform directions: (azimuth[n], elevation[n])."""
    i = np.arange(n) + 0.5
    el = np.arcsin(1.0 - 2.0 * i / n)
    az = np.mod((np.pi * (1.0 + np.sqrt(5.0))) * i, 2 * np.pi) - np.pi
    return az, el


def isotropic_sh_noise(order: int, n_samples: int, seed: int,
                       n_dirs: int = 256) -> np.ndarray:
    """Reference diffuse field: independent white noise from n_dirs
    near-uniform directions, each encoded through the library's evaluate_sh.
    The R7/R9 gates use this as the 'what SHOULD a diffuse tail look like
    through this exact rendering chain' baseline."""
    az, el = fibonacci_sphere(n_dirs)
    y = np.stack([at.evaluate_sh(order, float(a), float(e))
                  for a, e in zip(az, el)]).astype(np.float64)  # (n_dirs, C)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal((n_dirs, n_samples)) / np.sqrt(n_dirs)
    return y.T @ noise
