#!/usr/bin/env python3
"""Generate the computed figures for the book (book/src/img/*.svg).

Every plot of computed data in "Hearing in Three Dimensions" is produced by
this script, which drives the actual C++ library through the C ABI
(tools/capi/, via notebooks/ambitap_py.py) — the same code path the shipping
Max/Pd objects run. Purely schematic diagrams (box-and-arrow SVGs with no
data in them) are hand-authored and not regenerated here.

Figures:
  itd-ild.svg         Interaural time/level differences vs. azimuth, measured
                      from the embedded KEMAR HRTF set (book ch. 1).
  pan-law.svg         Equal-power stereo pan law + where the phantom image
                      can and cannot go (book ch. 1; the pan law is analytic).
  order-patterns.svg  max-rE virtual-microphone polar patterns at orders
                      1/3/5 (book ch. 3).

Usage:  python3 scripts/generate_book_figures.py
Needs:  numpy, matplotlib; builds libambitap_capi via cmake if missing.

Copyright 2026 Timothy Place. MIT License.
"""

from __future__ import annotations

import pathlib
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "notebooks"))

import ambitap_py as at  # noqa: E402  (path set up above)

import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

OUT = REPO / "book" / "src" / "img"
PALETTE = at.PALETTE

# Strip the volatile creation-date metadata so regenerating an unchanged
# figure produces a byte-identical SVG (same discipline as the notebooks'
# committed artifacts).
SAVE = dict(format="svg", bbox_inches="tight", metadata={"Date": None})


def _fractional_delay(a: np.ndarray, b: np.ndarray, rate: float) -> float:
    """Delay of b relative to a in seconds (positive: b arrives later), via
    FFT cross-correlation with parabolic peak interpolation.

    ITD is a low-frequency cue (the duplex theory of book ch. 1), so both
    signals are brickwall low-passed at 1.5 kHz first; that also keeps the
    correlation peak unimodal where pinna reflections otherwise distract it.
    The search is restricted to physically possible head-sized lags."""
    n = len(a) + len(b) - 1
    nfft = 1 << (n - 1).bit_length()
    fa, fb = np.fft.rfft(a, nfft), np.fft.rfft(b, nfft)
    cut = np.fft.rfftfreq(nfft, 1.0 / rate) <= 1500.0
    corr = np.fft.irfft(fa * cut * np.conj(fb * cut), nfft)
    corr = np.roll(corr, nfft // 2)
    lags = np.arange(nfft) - nfft // 2
    window = np.abs(lags) <= int(1.2e-3 * rate)
    k = int(np.flatnonzero(window)[np.argmax(corr[window])])
    y0, y1, y2 = corr[k - 1], corr[k], corr[k + 1]
    frac = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2)
    # corr peaks at lag −d when b is a delayed by d, hence the negation.
    return -(k + frac - nfft // 2) / rate


def fig_itd_ild() -> None:
    info = at.builtin_hrtf_info()
    rate = info["sample_rate"]
    az_deg = np.arange(-180, 181, 5)
    itd_ms, ild_db, ild_hi_db = [], [], []
    for deg in az_deg:
        # LS dataset: it preserves the measurements' true interaural phase.
        left, right = at.builtin_hrtf_hrir(np.deg2rad(deg), 0.0, magls=False)
        itd_ms.append(1e3 * _fractional_delay(left, right, rate))
        spec_l = np.abs(np.fft.rfft(left))
        spec_r = np.abs(np.fft.rfft(right))
        freqs = np.fft.rfftfreq(len(left), 1.0 / rate)
        ild_db.append(10 * np.log10(np.sum(spec_l**2) / np.sum(spec_r**2)))
        hi = freqs >= 3000.0
        ild_hi_db.append(10 * np.log10(np.sum(spec_l[hi] ** 2) / np.sum(spec_r[hi] ** 2)))

    itd_ms, ild_db, ild_hi_db = map(np.asarray, (itd_ms, ild_db, ild_hi_db))

    # The claims the chapter makes, checked here at build time:
    assert itd_ms[az_deg == 90][0] > 0.45, "left source must lead the left ear"
    assert abs(itd_ms[az_deg == 0][0]) < 0.05, "front source must be ~centered"
    assert np.max(np.abs(itd_ms)) < 1.0, "head-sized ITDs stay under 1 ms"
    assert np.max(ild_hi_db) > 10.0, "head shadow must exceed 10 dB up high"

    # Woodworth's spherical-head approximation, mirrored for rear angles.
    a, c = 0.0875, 343.0
    phi = np.deg2rad(az_deg)
    phi_f = np.where(np.abs(phi) <= np.pi / 2, np.abs(phi), np.pi - np.abs(phi))
    woodworth = 1e3 * np.sign(phi) * (a / c) * (phi_f + np.sin(phi_f))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.0, 3.4))
    ax1.plot(az_deg, itd_ms, color=PALETTE[0], lw=1.8, label="KEMAR (measured)")
    ax1.plot(az_deg, woodworth, color=PALETTE[2], lw=1.2, ls="--",
             label="Woodworth spherical head")
    ax1.set_xlabel("azimuth (°)  —  0 = front, +90 = left")
    ax1.set_ylabel("ITD (ms), + = left ear leads")
    ax1.set_title("Interaural time difference")
    ax1.legend(fontsize=8, frameon=False)

    ax2.plot(az_deg, ild_db, color=PALETTE[0], lw=1.8, label="broadband")
    ax2.plot(az_deg, ild_hi_db, color=PALETTE[1], lw=1.8, label="3 kHz and up")
    ax2.set_xlabel("azimuth (°)")
    ax2.set_ylabel("ILD (dB), + = left ear louder")
    ax2.set_title("Interaural level difference")
    ax2.legend(fontsize=8, frameon=False)

    for ax in (ax1, ax2):
        ax.axhline(0.0, color="0.8", lw=0.6, zorder=0)
        ax.axvline(0.0, color="0.8", lw=0.6, zorder=0)
        ax.set_xticks([-180, -90, 0, 90, 180])
        ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "itd-ild.svg", **SAVE)
    plt.close(fig)


def fig_pan_law() -> None:
    fig, (ax1, ax2) = plt.subplots(
        1, 2, figsize=(9.0, 3.4), gridspec_kw={"width_ratios": [1.15, 1.0]}
    )

    p = np.linspace(0.0, 1.0, 256)
    g_l, g_r = np.cos(p * np.pi / 2), np.sin(p * np.pi / 2)
    ax1.plot(p, g_l, color=PALETTE[0], lw=1.8, label="left gain")
    ax1.plot(p, g_r, color=PALETTE[1], lw=1.8, label="right gain")
    ax1.plot(p, g_l**2 + g_r**2, color=PALETTE[2], lw=1.2, ls="--",
             label="summed power (constant)")
    ax1.set_xlabel("pan position  (0 = left, 1 = right)")
    ax1.set_ylabel("gain (linear)")
    ax1.set_title("Equal-power pan law")
    ax1.set_ylim(0, 1.15)
    ax1.legend(fontsize=8, frameon=False, loc="center")
    ax1.spines[["top", "right"]].set_visible(False)

    # Where the phantom image lives: a ±30° arc, and nothing else.
    ax2.set_aspect("equal")
    ax2.axis("off")
    ax2.set_title("Where the image can go")
    theta = np.linspace(0, 2 * np.pi, 256)
    ax2.plot(np.cos(theta), np.sin(theta), color="0.85", lw=1.0, ls=":")
    arc = np.linspace(np.deg2rad(60), np.deg2rad(120), 64)  # ±30° about front
    ax2.plot(np.cos(arc), np.sin(arc), color=PALETTE[0], lw=3.0,
             solid_capstyle="round")
    # Viewed from above, front up: the left speaker is drawn on the left.
    for sgn, lab in ((-1, "L"), (1, "R")):
        x, y = sgn * np.sin(np.deg2rad(30)), np.cos(np.deg2rad(30))
        ax2.plot(x, y, "s", ms=11, color=PALETTE[3])
        ax2.annotate(lab, (x, y), ha="center", va="center", fontsize=7)
    head = plt.Circle((0, 0), 0.09, color="0.4")
    ax2.add_patch(head)
    ax2.annotate("phantom images:\nthis arc only", (0, 1.02),
                 xytext=(0.0, 0.55), ha="center", fontsize=8,
                 arrowprops=dict(arrowstyle="-", color="0.5", lw=0.8))
    ax2.annotate("no knob\nreaches here", (0, -1.0), xytext=(0, -0.55),
                 ha="center", fontsize=8, color="0.45",
                 arrowprops=dict(arrowstyle="-", color="0.7", lw=0.8))
    ax2.set_xlim(-1.45, 1.45)
    ax2.set_ylim(-1.3, 1.3)

    fig.tight_layout()
    fig.savefig(OUT / "pan-law.svg", **SAVE)
    plt.close(fig)


def fig_order_patterns() -> None:
    theta = np.linspace(-np.pi, np.pi, 721)
    fig, axes = plt.subplots(
        1, 3, figsize=(9.0, 3.2), subplot_kw={"projection": "polar"}
    )
    widths = []
    for ax, order, color in zip(axes, (1, 3, 5), (PALETTE[0], PALETTE[1], PALETTE[2])):
        w = at.max_re_weights(order)
        acn = np.arange((order + 1) ** 2)
        per_channel = w[np.floor(np.sqrt(acn)).astype(int)]
        y0 = at.evaluate_sh(order, 0.0, 0.0)  # the source direction: front
        g = np.array([
            np.dot(per_channel * y0, at.evaluate_sh(order, float(t), 0.0))
            for t in theta
        ])
        g = np.abs(g) / np.max(np.abs(g))

        # Half-power beamwidth, printed in the title so the figure carries
        # its own numbers.
        above = theta[g >= np.sqrt(0.5)]
        bw = np.degrees(above.max() - above.min())
        widths.append(bw)

        ax.plot(theta, g, color=color, lw=1.8)
        ax.fill(theta, g, color=color, alpha=0.15)
        ax.set_title(f"order {order} — {(order + 1) ** 2} ch\n−3 dB width ≈ {bw:.0f}°",
                     fontsize=9)
        ax.set_theta_zero_location("N")  # front points up
        ax.set_theta_direction(1)        # positive azimuth = left = CCW
        ax.set_xticks(np.deg2rad([0, 90, 180, 270]))
        ax.set_xticklabels(["front", "left", "back", "right"], fontsize=7)
        ax.set_yticks([])
    # The claim the chapter makes: sharpness improves with order.
    assert widths[0] > widths[1] > widths[2] > 0, "beamwidth must shrink with order"
    fig.tight_layout()
    fig.savefig(OUT / "order-patterns.svg", **SAVE)
    plt.close(fig)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    fig_itd_ild()
    fig_pan_law()
    fig_order_patterns()
    print(f"wrote {len(list(OUT.glob('*.svg')))} SVGs to {OUT}")


if __name__ == "__main__":
    main()
