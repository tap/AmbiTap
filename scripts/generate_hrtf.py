#!/usr/bin/env python3
"""Generate the SH-domain HRTF C++ header for the binaural renderer.

Decomposes measured HRTFs from a SOFA file into spherical harmonic coefficients
using two different projection schemes:

  LS    - standard least-squares per-bin pseudoinverse (complex). Faithful
          reconstruction at low frequencies; phase artifacts above the SH
          aliasing limit produce the well-known "in-the-head" / collapsed-image
          effect at high orders.

  MagLS - magnitude least-squares (Schoerkhuber & Habets, 2019). Identical to
          LS below the SH aliasing frequency. Above it, the per-direction phase
          is allowed to drift while the magnitude is matched to the target,
          which dramatically improves high-frequency localization. Implemented
          as a single projection per bin with phase continuity carried from the
          previous bin, exactly as published. (Running further alternating
          projections per bin — as an earlier revision of this script did —
          converges each bin to an arbitrary phase fixed point, severing the
          bin-to-bin phase continuity; the resulting spectrum is inconsistent
          with a compact causal FIR and the IFFT time-aliases ~36% of the
          energy ahead of the acoustic onset. See docs/AUDIT.md finding B6.)

Both datasets are written into the same header so the runtime can switch
between them via a runtime parameter.

Usage:
    python3 scripts/generate_hrtf.py [sofa_file] [max_order] [output_path]

Defaults:
    sofa_file:   /tmp/mit_kemar.sofa
    max_order:   5
    output_path: include/ambitap/math/binaural/hrtf_data.h

The SOFA file is the MIT KEMAR (normal pinna) HRTF set of W. G. Gardner and
K. D. Martin (MIT Media Lab, 1994), distributed free of charge on the condition
that the authors are credited. A SOFA-format copy is available from the SOFA
project (e.g. https://www.sofacoustics.org/data/database/mit/), or convert the
original MIT distribution (https://sound.media.mit.edu/resources/KEMAR.html).
Record the exact source file you used here when you regenerate, so the embedded
coefficients stay reproducible. See THIRD_PARTY_NOTICES.md for the credit text.

Copyright 2025-2026 Timothy Place. Distributed under the MIT License.
"""

import os
import sys
import numpy as np
from math import factorial
from scipy.special import lpmv


SPEED_OF_SOUND = 343.0
HEAD_RADIUS_M  = 0.0875  # MIT KEMAR; close enough for any human-head HRTF

# Extra alternating-projection iterations per bin AFTER the phase-continuation
# projection. Keep this at 0: the published MagLS uses exactly one projection
# per bin with the phase seeded from the previous bin, which is what keeps the
# resulting FIRs compact and causal. Iterating further destroys the phase
# continuity and time-aliases the IRs (audit finding B6).
MAGLS_ITERS    = 0


def sn3d_factor(n, abs_m):
    epsilon = 1.0 if abs_m == 0 else 2.0
    return np.sqrt(epsilon * factorial(n - abs_m) / factorial(n + abs_m))


def evaluate_sh_at(order, azimuth, elevation):
    """Evaluate all SH up to order at a single direction. Returns (N+1)^2 values."""
    num_ch = (order + 1) ** 2
    result = np.zeros(num_ch)
    sin_el = np.sin(elevation)
    for n in range(order + 1):
        for m in range(-n, n + 1):
            abs_m = abs(m)
            # Associated Legendre without Condon-Shortley phase
            p = float(lpmv(abs_m, n, sin_el)) * ((-1) ** abs_m)
            norm = sn3d_factor(n, abs_m)
            if m > 0:
                y = norm * p * np.cos(m * azimuth)
            elif m < 0:
                y = norm * p * np.sin(abs_m * azimuth)
            else:
                y = norm * p
            result[n * n + n + m] = y
    return result


def load_sofa(path):
    import sofar
    sofa = sofar.read_sofa(path)
    positions = np.array(sofa.SourcePosition)
    az = np.radians(positions[:, 0])
    el = np.radians(positions[:, 1])
    data = np.array(sofa.Data_IR)  # (num_meas, 2, hrir_len)
    sample_rate = float(sofa.Data_SamplingRate)
    print(f"  Measurements: {data.shape[0]}")
    print(f"  HRIR length:  {data.shape[2]} taps")
    print(f"  Sample rate:  {sample_rate} Hz")
    return az, el, data, sample_rate


def build_sh_matrix(az, el, order):
    num_meas = len(az)
    num_ch = (order + 1) ** 2
    Y = np.zeros((num_meas, num_ch))
    for i in range(num_meas):
        Y[i] = evaluate_sh_at(order, az[i], el[i])
    return Y


def sh_aliasing_freq(order, head_radius_m=HEAD_RADIUS_M, c=SPEED_OF_SOUND):
    """The frequency above which the SH order is no longer sufficient to
    reproduce the per-direction HRTF exactly. Beyond this, MagLS gives up
    phase fidelity in exchange for magnitude accuracy."""
    return order * c / (2.0 * np.pi * head_radius_m)


def compute_sh_hrtf_ls(Y_pinv, hrir_data, fft_len):
    """Standard least-squares SH projection, per frequency bin.

    Mathematically equivalent to per-tap time-domain pinv (FFT is a linear
    operator that commutes with the projection); we do it in the frequency
    domain to share the loop with MagLS.
    """
    num_meas, num_ears, _ = hrir_data.shape
    H_freq = np.fft.rfft(hrir_data, n=fft_len, axis=2)  # (meas, ears, bins)
    num_bins = H_freq.shape[2]
    num_ch = Y_pinv.shape[0]
    sh_hrtf = np.zeros((num_ears, num_ch, fft_len), dtype=np.float64)
    for ear in range(num_ears):
        H_sh_freq = (Y_pinv @ H_freq[:, ear, :])  # (num_ch, num_bins)
        sh_hrtf[ear] = np.fft.irfft(H_sh_freq, n=fft_len, axis=1)
    return sh_hrtf


def compute_sh_hrtf_magls(Y, Y_pinv, hrir_data, fft_len, sample_rate,
                          order, n_iter=MAGLS_ITERS):
    """Magnitude least-squares SH projection.

    Below the SH aliasing frequency, falls back to standard LS. Above it,
    iteratively refines so that the SH-reconstructed per-direction magnitude
    matches the measurement while the phase is free to relax.
    """
    num_meas, num_ears, _ = hrir_data.shape
    H_freq = np.fft.rfft(hrir_data, n=fft_len, axis=2)  # (meas, ears, bins)
    num_bins = H_freq.shape[2]
    num_ch = Y_pinv.shape[0]

    f_alias = sh_aliasing_freq(order)
    bin_freqs = np.linspace(0.0, sample_rate / 2.0, num_bins)
    f_alias_bin = int(np.searchsorted(bin_freqs, f_alias))
    f_alias_bin = max(f_alias_bin, 1)  # always keep DC in LS branch
    print(f"  MagLS aliasing cutoff: {f_alias:.0f} Hz (bin {f_alias_bin} of {num_bins})")

    sh_hrtf = np.zeros((num_ears, num_ch, fft_len), dtype=np.float64)
    for ear in range(num_ears):
        H_sh_freq = np.zeros((num_ch, num_bins), dtype=np.complex128)

        # Below alias (including DC): plain LS.
        H_sh_freq[:, :f_alias_bin] = Y_pinv @ H_freq[:, ear, :f_alias_bin]

        # Above alias: alternating projection with phase continuity.
        for b in range(f_alias_bin, num_bins):
            target = H_freq[:, ear, b]
            target_mag = np.abs(target)

            # Seed with the previous bin's reconstructed phase to keep the
            # IRs causal-ish and avoid bin-to-bin phase jumps.
            recon = Y @ H_sh_freq[:, b - 1]
            phase = np.angle(recon)
            modified = target_mag * np.exp(1j * phase)
            H_sh_freq[:, b] = Y_pinv @ modified

            for _ in range(n_iter):
                recon = Y @ H_sh_freq[:, b]
                phase = np.angle(recon)
                modified = target_mag * np.exp(1j * phase)
                H_sh_freq[:, b] = Y_pinv @ modified

        # Force Nyquist bin to be real (rfft contract) if fft_len is even.
        if fft_len % 2 == 0:
            H_sh_freq[:, -1] = H_sh_freq[:, -1].real

        sh_hrtf[ear] = np.fft.irfft(H_sh_freq, n=fft_len, axis=1)
    return sh_hrtf


def emit_array(lines, name, data, num_ch, fft_len):
    lines.append(f"constexpr float {name}[{num_ch}][{fft_len}] = {{")
    for ch in range(num_ch):
        vals = data[ch]
        val_strs = [f"{v:.8e}f" for v in vals]
        lines.append(f"    {{ // ACN {ch}")
        for start in range(0, len(val_strs), 8):
            chunk = ", ".join(val_strs[start:start + 8])
            lines.append(f"        {chunk},")
        comma = "," if ch < num_ch - 1 else ""
        lines.append(f"    }}{comma}")
    lines.append("};")
    lines.append("")


def generate_header(sh_ls, sh_magls, sample_rate, order, output_path):
    """Write the LS + MagLS datasets as a single C++ header."""
    num_ears, num_ch, fft_len = sh_ls.shape

    lines = [
        "/// AmbiTap: target-independent ambisonics library",
        "/// Built-in SH-domain HRTF dataset for binaural rendering.",
        "/// Auto-generated from MIT KEMAR (normal pinna) via SOFA + scripts/generate_hrtf.py.",
        "///",
        "/// Source measurements: W. G. Gardner and K. D. Martin, \"HRTF Measurements of",
        "/// a KEMAR Dummy-Head Microphone,\" MIT Media Lab Perceptual Computing Technical",
        "/// Report #280 (1994). The KEMAR data is distributed free of charge by the MIT",
        "/// Media Laboratory on the condition that the authors are credited; this header",
        "/// is a spherical-harmonic projection of that data. See THIRD_PARTY_NOTICES.md.",
        "///",
        "/// Two parallel datasets are emitted:",
        "///   builtin_hrtf_{left,right}       - standard LS SH projection.",
        "///   builtin_hrtf_magls_{left,right} - magnitude least-squares projection",
        "///                                      for improved HF localization.",
        "/// The binaural renderer switches between them at runtime via the",
        "/// hrtf_dataset parameter.",
        "/// Timothy Place",
        "/// Copyright 2025-2026 Timothy Place.",
        "",
        "#ifndef AMBITAP_MATH_HRTF_DATA_H",
        "#define AMBITAP_MATH_HRTF_DATA_H",
        "",
        "#include <cstddef>",
        "",
        "namespace ambitap {",
        "",
        f"constexpr int    builtin_hrtf_order       = {order};",
        f"constexpr size_t builtin_hrtf_channels    = {num_ch};",
        f"constexpr size_t builtin_hrtf_length      = {fft_len};",
        f"constexpr float  builtin_hrtf_sample_rate = {sample_rate}f;",
        "",
    ]
    ear_names = ["left", "right"]
    for ear in range(num_ears):
        lines.append(f"/// LS SH-domain HRTF for the {ear_names[ear]} ear.")
        lines.append(f"/// Indexed as builtin_hrtf_{ear_names[ear]}[acn_channel][sample].")
        emit_array(lines, f"builtin_hrtf_{ear_names[ear]}", sh_ls[ear], num_ch, fft_len)
    for ear in range(num_ears):
        lines.append(f"/// MagLS SH-domain HRTF for the {ear_names[ear]} ear (better HF).")
        lines.append(f"/// Indexed as builtin_hrtf_magls_{ear_names[ear]}[acn_channel][sample].")
        emit_array(lines, f"builtin_hrtf_magls_{ear_names[ear]}", sh_magls[ear], num_ch, fft_len)

    lines.append("} // namespace ambitap")
    lines.append("")
    lines.append("#endif // AMBITAP_MATH_HRTF_DATA_H")
    lines.append("")

    with open(output_path, "w") as f:
        f.write("\n".join(lines))

    embedded_bytes = 2 * num_ears * num_ch * fft_len * 4
    print(f"  Wrote {output_path}")
    print(f"  Embedded data size: {embedded_bytes / 1024:.1f} KB (both datasets)")


def main():
    sofa_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mit_kemar.sofa"
    max_order = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_out = os.path.join(here, "include", "ambitap", "math", "binaural", "hrtf_data.h")
    output_path = sys.argv[3] if len(sys.argv) > 3 else default_out

    print(f"Loading SOFA: {sofa_path}")
    az, el, hrir_data, sr = load_sofa(sofa_path)

    fft_len = 128
    if hrir_data.shape[2] > fft_len:
        print(f"  Truncating HRIRs from {hrir_data.shape[2]} to {fft_len} taps")
        hrir_data = hrir_data[:, :, :fft_len]

    print(f"\nBuilding SH matrix (order {max_order}, {(max_order + 1) ** 2} channels)...")
    Y = build_sh_matrix(az, el, max_order)
    Y_pinv = np.linalg.pinv(Y)

    print("\nLS projection...")
    sh_ls = compute_sh_hrtf_ls(Y_pinv, hrir_data, fft_len)

    print("\nMagLS projection...")
    sh_magls = compute_sh_hrtf_magls(Y, Y_pinv, hrir_data, fft_len, sr, max_order)

    # Reality check: above-alias MagLS magnitude should be closer to the target
    # than LS in the per-direction sense. Quick sanity print.
    # hrir_data is (meas, ears, taps); rfft -> (meas, ears, bins).
    # einsum 'mc,ecn->emn' puts ears first; transpose to match.
    H_target = np.fft.rfft(hrir_data, n=fft_len, axis=2)
    H_ls_recon = np.fft.rfft(
        np.einsum('mc,ecn->emn', Y, sh_ls).transpose(1, 0, 2), n=fft_len, axis=2)
    H_magls_recon = np.fft.rfft(
        np.einsum('mc,ecn->emn', Y, sh_magls).transpose(1, 0, 2), n=fft_len, axis=2)
    f_alias_bin = int(np.searchsorted(
        np.linspace(0, sr / 2, H_target.shape[2]), sh_aliasing_freq(max_order)))
    mag_target = np.abs(H_target[:, :, f_alias_bin:])
    mag_err_ls = np.abs(np.abs(H_ls_recon[:, :, f_alias_bin:]) - mag_target).mean()
    mag_err_magls = np.abs(np.abs(H_magls_recon[:, :, f_alias_bin:]) - mag_target).mean()
    print(f"\nAbove-alias mean magnitude error:")
    print(f"  LS:    {mag_err_ls:.4e}")
    print(f"  MagLS: {mag_err_magls:.4e}   ({mag_err_magls/mag_err_ls*100:.1f}% of LS)")

    print(f"\nWriting header...")
    generate_header(sh_ls, sh_magls, sr, max_order, output_path)


if __name__ == "__main__":
    main()
