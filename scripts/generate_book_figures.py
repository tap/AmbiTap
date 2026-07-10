#!/usr/bin/env python3
"""Generate the computed figures for the book (book/src/img/*.svg).

Every plot of computed data in "Hearing in Three Dimensions" is produced by
this script, which drives the actual C++ library through the C ABI
(tools/capi/, via notebooks/ambitap_py.py) — the same code path the shipping
Max/Pd objects run. Purely schematic diagrams (box-and-arrow SVGs with no
data in them) are hand-authored and not regenerated here.

Figures:
  itd-ild.svg           Interaural time/level differences vs. azimuth,
                        measured from the embedded KEMAR HRTF set (ch. 1).
  pan-law.svg           Equal-power stereo pan law + where the phantom image
                        can and cannot go (ch. 1; the pan law is analytic).
  order-patterns.svg    max-rE virtual-microphone polar patterns at orders
                        1/3/5 (ch. 3).
  b-format.svg          The order-1 basis as microphone pickup patterns:
                        W omni + Y/Z/X figure-8s (ch. 6).
  order-blur.svg        Beamwidth vs. order, and the quadratic channel-count
                        price (ch. 7).
  reflectogram.svg      Image-source early reflections of a shoebox room,
                        from the room model (ch. 11).
  decoder-comparison.svg  Energy and rE concentration around the circle for
                        mode-matching / ALLRAD / EPAD on 5.1 (ch. 12).
  ls-vs-magls.svg       Reconstructed HRTF magnitude at both ears, LS vs
                        MagLS at order 3 (ch. 13).
  heatmap.svg           analysis::soundfield_grid image of a two-source
                        scene (ch. 14).

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
# Fixed hashsalt (plus the stripped Date metadata below): regenerating an
# unchanged figure produces a byte-identical SVG, so `git status` stays
# honest about which figures actually changed.
matplotlib.rcParams["svg.hashsalt"] = "ambitap-book"
import matplotlib.pyplot as plt  # noqa: E402

OUT = REPO / "book" / "src" / "img"
PALETTE = at.PALETTE

# Strip the volatile creation-date metadata (paired with the fixed
# svg.hashsalt above) so unchanged figures regenerate byte-identically.
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


def fig_b_format() -> None:
    """The four order-1 basis functions drawn as microphone pickup patterns.
    Solid = positive lobe, dashed = negative — the classic polar-pattern
    convention. W/Y/X on the horizontal plane; Z on a vertical slice."""
    theta = np.linspace(-np.pi, np.pi, 721)
    horiz = np.array([at.evaluate_sh(1, float(t), 0.0) for t in theta])  # (721, 4)
    vert = np.array([at.evaluate_sh(1, 0.0, float(t)) for t in theta])

    # The mic-pattern claims of ch. 6, checked: W is direction-blind; Y peaks
    # left (+pi/2), X peaks front, Z peaks up, each with a mirror-image
    # negative lobe.
    assert np.allclose(horiz[:, 0], horiz[0, 0]), "W must be omnidirectional"
    assert theta[np.argmax(horiz[:, 1])] > 0, "Y must peak to the left"
    assert abs(theta[np.argmax(horiz[:, 3])]) < 0.01, "X must peak at front"
    assert abs(theta[np.argmax(vert[:, 2])] - np.pi / 2) < 0.01, "Z must peak at zenith"

    panels = [
        ("W — omni (ACN 0)", horiz[:, 0], "front up"),
        ("Y — left/right (ACN 1)", horiz[:, 1], "front up"),
        ("Z — up/down (ACN 2)", vert[:, 2], "vertical slice, up = up"),
        ("X — front/back (ACN 3)", horiz[:, 3], "front up"),
    ]
    fig, axes = plt.subplots(1, 4, figsize=(10.0, 2.9), subplot_kw={"projection": "polar"})
    for ax, (title, g, note), color in zip(axes, panels, PALETTE):
        pos, neg = np.where(g >= 0, g, np.nan), np.where(g < 0, -g, np.nan)
        ax.plot(theta, pos, color=color, lw=1.8)
        ax.plot(theta, neg, color=color, lw=1.4, ls="--")
        ax.set_title(f"{title}\n{note}", fontsize=8)
        ax.set_theta_zero_location("N")
        ax.set_theta_direction(1)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_ylim(0, 1.1)
    fig.tight_layout()
    fig.savefig(OUT / "b-format.svg", **SAVE)
    plt.close(fig)


def _beamwidth(order: int) -> float:
    theta = np.linspace(-np.pi, np.pi, 1441)
    w = at.max_re_weights(order)
    acn = np.arange((order + 1) ** 2)
    per_channel = w[np.floor(np.sqrt(acn)).astype(int)]
    y0 = at.evaluate_sh(order, 0.0, 0.0)
    g = np.array([np.dot(per_channel * y0, at.evaluate_sh(order, float(t), 0.0))
                  for t in theta])
    g = np.abs(g) / np.max(np.abs(g))
    above = theta[g >= np.sqrt(0.5)]
    return float(np.degrees(above.max() - above.min()))


def fig_order_blur() -> None:
    orders = np.arange(1, 6)
    widths = np.array([_beamwidth(int(o)) for o in orders])
    channels = (orders + 1) ** 2
    assert np.all(np.diff(widths) < 0), "beamwidth must shrink monotonically"

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.0, 3.2))
    ax1.plot(orders, widths, "o-", color=PALETTE[0], lw=1.8)
    for o, wdeg in zip(orders, widths):
        ax1.annotate(f"{wdeg:.0f}°", (o, wdeg), textcoords="offset points",
                     xytext=(8, 4), fontsize=8)
    ax1.set_xlabel("ambisonic order")
    ax1.set_ylabel("−3 dB beamwidth (°)")
    ax1.set_title("What order buys: sharpness")
    ax1.set_xticks(orders)
    ax1.set_ylim(0, 180)

    ax2.bar(orders, channels, color=PALETTE[1], width=0.6)
    for o, c in zip(orders, channels):
        ax2.annotate(str(c), (o, c), ha="center", va="bottom", fontsize=9)
    ax2.set_xlabel("ambisonic order")
    ax2.set_ylabel("channels: (order+1)²")
    ax2.set_title("What order costs: channel count")
    ax2.set_xticks(orders)

    for ax in (ax1, ax2):
        ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "order-blur.svg", **SAVE)
    plt.close(fig)


def fig_reflectogram() -> None:
    """Image-source early reflections of a shoebox room, straight from the
    library's room model (the same enumeration ambitap.room~ renders)."""
    lib = at._LIB
    f32p = np.ctypeslib.ndpointer(dtype=np.float32, flags="C")
    import ctypes
    lib.ambitap_room_image_sources.argtypes = [f32p, f32p, f32p, f32p,
                                               ctypes.c_float, ctypes.c_int,
                                               f32p, f32p, f32p,
                                               np.ctypeslib.ndpointer(dtype=np.int32, flags="C")]
    lib.ambitap_room_image_sources.restype = ctypes.c_int

    dims = np.array([8.0, 6.0, 3.5], dtype=np.float32)          # a rehearsal room
    source = np.array([2.0, 1.5, 1.6], dtype=np.float32)
    listener = np.array([5.5, 3.5, 1.6], dtype=np.float32)
    beta = np.full(6, 0.85, dtype=np.float32)                   # reflection coeffs
    cap = 4096
    t = np.empty(cap, dtype=np.float32)
    amp = np.empty(cap, dtype=np.float32)
    direction = np.empty(3 * cap, dtype=np.float32)
    refl = np.empty(cap, dtype=np.int32)
    n = lib.ambitap_room_image_sources(dims, source, listener, beta, 0.08, cap,
                                       t, amp, direction, refl)
    assert n > 10, "a shoebox must produce early reflections"
    t, amp, refl = t[:n] * 1e3, amp[:n], refl[:n]
    direct = refl == 0
    assert direct.sum() == 1 and t[direct][0] == t.min(), "direct path arrives first"

    db = 20 * np.log10(np.maximum(np.abs(amp), 1e-9) / np.abs(amp[direct][0]))
    fig, ax = plt.subplots(figsize=(8.0, 3.2))
    for order_n, color, label in ((0, PALETTE[2], "direct"),
                                  (1, PALETTE[0], "1st reflection"),
                                  (2, PALETTE[1], "2nd"),
                                  (3, PALETTE[4], "3rd+")):
        sel = (refl == order_n) if order_n < 3 else (refl >= 3)
        m, s, b = ax.stem(t[sel], db[sel], basefmt=" ")
        plt.setp(s, color=color, linewidth=1.2)
        plt.setp(m, color=color, markersize=3.5, label=label)
    ax.set_xlabel("time after emission (ms)")
    ax.set_ylabel("level re. direct path (dB)")
    ax.set_title("Early reflections of an 8 × 6 × 3.5 m room (image-source model)")
    ax.legend(fontsize=8, frameon=False)
    ax.set_ylim(-40, 3)
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "reflectogram.svg", **SAVE)
    plt.close(fig)


def fig_decoder_comparison() -> None:
    """Energy and rE concentration around the horizontal circle for the three
    decoder constructions on 5.1 (order 3, max-rE on): the actual matrices
    the decode~ object builds."""
    spk_az, spk_el = at.layout("5.1")
    order = 3
    theta = np.linspace(-np.pi, np.pi, 361)
    Y = np.stack([at.evaluate_sh(order, float(a), 0.0) for a in theta])  # (T, C)
    u = np.stack([np.cos(spk_az) * np.cos(spk_el),                       # unit vectors
                  np.sin(spk_az) * np.cos(spk_el),
                  np.sin(spk_el)], axis=1)                               # (L, 3)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.0, 3.4))
    results = {}
    for alg, color in (("mode_match", PALETTE[0]), ("allrad", PALETTE[1]),
                       ("epad", PALETTE[2])):
        D = at.decoder_matrix(alg, order, spk_az, spk_el, use_max_re=True)
        G = Y @ D.T                                # (T, L) speaker gains per direction
        E = np.sum(G**2, axis=1)                   # energy
        rE = np.linalg.norm((G**2) @ u, axis=1) / np.maximum(E, 1e-12)
        results[alg] = (E, rE)
        ax1.plot(np.degrees(theta), 10 * np.log10(E / E.mean()), color=color,
                 lw=1.6, label=alg)
        ax2.plot(np.degrees(theta), rE, color=color, lw=1.6, label=alg)

    # The chapter's claims, checked: on an irregular, gappy layout like 5.1
    # ALLRAD holds loudness flattest around the circle by a wide margin (its
    # design goal); and every construction focuses rE harder inside the
    # frontal stage, where the speakers actually are, than in the rear gap.
    spread = {a: np.ptp(10 * np.log10(E / E.mean())) for a, (E, _) in results.items()}
    assert spread["allrad"] <= min(spread.values()) + 1e-6, "ALLRAD must be flattest"
    assert spread["allrad"] < 0.5 * min(spread["mode_match"], spread["epad"]), \
        "the ALLRAD advantage on 5.1 should be decisive, not marginal"
    for alg, (_, rE) in results.items():
        front = np.abs(np.degrees(theta)) <= 30
        back = np.abs(np.degrees(theta)) >= 150
        assert rE[front].mean() > rE[back].mean(), f"{alg}: front must beat the gap"

    for spk in np.degrees(spk_az):
        for ax in (ax1, ax2):
            ax.axvline(spk, color="0.88", lw=0.8, zorder=0)
    ax1.set_ylabel("energy re. mean (dB)")
    ax1.set_title("Loudness around the circle")
    ax2.set_ylabel("|rE| (concentration, 0–1)")
    ax2.set_title("Image focus around the circle")
    for ax in (ax1, ax2):
        ax.set_xlabel("source azimuth (°) — vertical lines: the five speakers")
        ax.set_xticks([-180, -110, -30, 0, 30, 110, 180])
        ax.legend(fontsize=8, frameon=False)
        ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "decoder-comparison.svg", **SAVE)
    plt.close(fig)


def fig_ls_vs_magls() -> None:
    """Reconstructed HRTF magnitude at both ears for a hard-left source,
    order 3: LS loses high-frequency energy at the far ear; MagLS holds the
    magnitude. Same data paths ambitap.binaural~ convolves."""
    info = at.builtin_hrtf_info()
    rate = info["sample_rate"]
    az = np.deg2rad(90.0)  # hard left: left ear near, right ear shadowed
    fig, axes = plt.subplots(1, 2, figsize=(9.0, 3.4))
    curves = {}
    for magls, label, ls in ((False, "LS, order 3", "--"), (True, "MagLS, order 3", "-")):
        left, right = at.builtin_hrtf_hrir(az, 0.0, order=3, magls=magls)
        freqs = np.fft.rfftfreq(len(left), 1.0 / rate)
        for ax, ear, sig in ((axes[0], "near (left)", left), (axes[1], "far (right)", right)):
            mag = 20 * np.log10(np.maximum(np.abs(np.fft.rfft(sig)), 1e-9))
            color = PALETTE[0] if not magls else PALETTE[2]
            ax.semilogx(freqs[1:], mag[1:], ls, color=color, lw=1.5, label=label)
            curves[(magls, ear)] = (freqs, mag)
            ax.set_title(f"{ear} ear — source hard left")

    # The chapter's claim, checked: above the order-3 aliasing region the LS
    # reconstruction sheds energy at the shadowed ear relative to MagLS.
    f, ls_mag = curves[(False, "far (right)")]
    _, magls_mag = curves[(True, "far (right)")]
    hi = (f >= 6000) & (f <= 16000)
    assert magls_mag[hi].mean() > ls_mag[hi].mean(), "MagLS must hold HF magnitude"

    for ax in axes:
        ax.set_xlabel("frequency (Hz)")
        ax.set_ylabel("magnitude (dB)")
        ax.set_xlim(100, rate / 2)
        ax.legend(fontsize=8, frameon=False)
        ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "ls-vs-magls.svg", **SAVE)
    plt.close(fig)


def fig_heatmap() -> None:
    """analysis::soundfield_grid — the image behind ambitap.grid~ and the
    heatmap widget — for a scene with two sources of different levels."""
    rng = np.random.default_rng(7)
    order, n = 3, 48000
    hoa = np.zeros((at.channel_count(order), n), dtype=np.float32)
    for src_az, src_el, gain in ((np.deg2rad(40), np.deg2rad(15), 1.0),
                                 (np.deg2rad(-120), np.deg2rad(-10), 0.4)):
        coeffs = at.evaluate_sh(order, float(src_az), float(src_el))
        hoa += np.outer(coeffs, gain * rng.standard_normal(n).astype(np.float32))
    img, peak_db = at.soundfield_grid(hoa, order=order, az_steps=128,
                                      smoothing_ms=500.0)

    # The louder source must be the image's global maximum, near its true
    # direction (az 40°, el 15°).
    el_steps, az_steps = img.shape
    iy, ix = np.unravel_index(np.argmax(img), img.shape)
    az_found = -180.0 + 360.0 * (ix + 0.5) / az_steps
    el_found = 90.0 - 180.0 * (iy + 0.5) / el_steps
    assert abs(az_found - 40) < 15 and abs(el_found - 15) < 15, \
        f"peak at ({az_found:.0f}, {el_found:.0f}), expected near (40, 15)"

    fig, ax = plt.subplots(figsize=(8.0, 3.4))
    im = ax.imshow(img, extent=[-180, 180, -90, 90], aspect="auto",
                   origin="upper", cmap="viridis", vmin=0, vmax=1)
    ax.plot(40, 15, "o", ms=12, mfc="none", mec="white", mew=1.5)
    ax.plot(-120, -10, "o", ms=12, mfc="none", mec="white", mew=1.2, ls=":")
    ax.annotate("source, 0 dB", (40, 15), xytext=(52, 32), color="white", fontsize=8)
    ax.annotate("source, −8 dB", (-120, -10), xytext=(-108, -36), color="white", fontsize=8)
    ax.set_xlabel("azimuth (°) — 0 = front, +90 = left")
    ax.set_ylabel("elevation (°)")
    ax.set_title(f"Soundfield heatmap of a two-source scene (peak {peak_db:.0f} dB)")
    fig.colorbar(im, ax=ax, label="normalized energy")
    fig.tight_layout()
    fig.savefig(OUT / "heatmap.svg", **SAVE)
    plt.close(fig)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    fig_itd_ild()
    fig_pan_law()
    fig_order_patterns()
    fig_b_format()
    fig_order_blur()
    fig_reflectogram()
    fig_decoder_comparison()
    fig_ls_vs_magls()
    fig_heatmap()
    print(f"wrote {len(list(OUT.glob('*.svg')))} SVGs to {OUT}")


if __name__ == "__main__":
    main()
