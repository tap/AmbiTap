# Perceptual verification — `ambitap.xtc~` and `ambitap.room~`

Measurement + listening protocol for the two Wave-3 perceptual objects
(ROADMAP items 3–4). This document is a **prerequisite**: per the roadmap,
neither object gets built until this protocol exists, and neither ships
until it passes.

The premise, restated from the roadmap: these modules are
*listening-dependent*. The rest of the library is verifiable to ~1e-7
against closed-form references; these two are not. A crosstalk canceller
with wrong regularization colors the sound and smears transients; a bad
early-reflection pattern destroys localization instead of adding
externalization. In both cases **the failure mode sounds worse than doing
nothing** — so "the math ran without asserting" proves nothing, and the
correctness bar has two layers:

1. **Numeric gates** — objective metrics computed offline from the designed
   filters / rendered impulse responses. Deterministic, thresholded, run in
   CI exactly like the existing quality gates.
2. **A listening pass** — small, scripted, repeatable, recorded. A release
   checklist item, not CI.

**The bypass rule** (non-negotiable, from the roadmap framing): each module
is compared against BYPASS in the listening pass — plain stereo playback
for `xtc~`, anechoic encode→binaural for `room~`. If listeners do not
prefer the module over bypass on its headline question, **it does not
ship**, regardless of how green the numeric gates are.

Every threshold below is an **initial engineering target**: set from the
literature and first principles, expected to be revised (and the revision
recorded here) after the first real measurement run. Thresholds are
targets; the *metric definitions* and the bypass rule are the contract.

**Harness convention.** Same shape as the existing verification layer: an
executed notebook per module (`notebooks/xtc_verification.ipynb`,
`notebooks/room_verification.ipynb`) driving the real implementation
through the C ABI (`tools/capi/`, loaded via `notebooks/ambitap_py.py`),
with assert cells enforcing every numeric gate. Fixed seeds, fixed
geometry, byte-stable outputs — the notebooks are the plots+numbers
artifact; the asserts are mirrored as plain C++ tests
(`tests/test_xtc_metrics.cpp`, `tests/test_room_metrics.cpp`) so the gate
runs in the ordinary ctest suite without a Python leg. One capi addition is
needed up front: export the **time-domain** SH-reconstructed HRIR at a
direction (the inner loop of `binaural_renderer::probe_response`, which
currently returns magnitude only) — the XTC plant model and the room
binauralization both need the complex response.

---

## `ambitap.xtc~` — transaural / crosstalk cancellation

### Plant model and what is being measured

The object designs a 2×2 filter matrix **H**(f) for a stated geometry
(speaker span ±θ, distance d). The plant **C**(f) is the 2×2 matrix of
head-related transfer functions from each speaker to each ear —
reconstructed from the built-in KEMAR SH set at (±θ, 0) elevation, at the
design distance. Everything below is computed from the **performance
matrix P(f) = C(f)·H(f)** — no audio, no room, no listener needed, fully
deterministic. (An in-room *measured* plant is explicitly out of scope for
v1; see open questions.)

### Numeric gates

| # | Metric | Definition | Gate (initial target) |
|---|---|---|---|
| X1 | Crosstalk rejection spectrum | XTC(f) = 20·log10(\|P_ii\| / \|P_ij\|), worse ear | ≥ 20 dB mean and ≥ 15 dB minimum over **300 Hz – 6 kHz**, at spans ±10°, ±20°, ±30° |
| X2 | Robustness — translation | Recompute P with the head displaced; H fixed | ≥ 12 dB mean in-band XTC at ±2 cm lateral; XTC ≥ 0 dB (never worse than bypass) at ±5 cm |
| X3 | Robustness — rotation | Head yaw ±5°, ±10°; H fixed | ≥ 12 dB mean in-band at ±5°; ≥ 6 dB at ±10° |
| X4 | Coloration budget | \|P_ii(f)\|, 1/3-octave smoothed, re. its 300 Hz–6 kHz mean | within **±3 dB** over 200 Hz – 8 kHz at the design point; within ±6 dB under the X2/X3 offsets |
| X5 | Filter gain ceiling | max over f, over all four \|H_ij(f)\| | ≤ **+12 dB**; object applies matching static makeup attenuation so full-scale input cannot clip |
| X6 | Determinism | design twice at identical parameters | byte-identical filters |

Rationale, per gate:

- **X1 — why 300 Hz–6 kHz and not full band.** Below ~300 Hz the
  inter-ear path difference is a tiny fraction of a wavelength: C is
  nearly singular, and inverting it demands enormous filter gains for a
  cue (low-frequency ILD) that barely exists perceptually. Above ~6 kHz,
  cancellation depends on sub-centimeter head placement and on
  individual-pinna detail the KEMAR plant cannot predict — "rejection"
  there is fiction that evaporates the moment a real listener sits down.
  The regularization is *supposed* to give up outside the band; a design
  that claims full-band rejection is over-fit to the dummy head. 20 dB
  in-band is the level Choueiri's BACCH work treats as perceptually
  sufficient; 15 dB minimum keeps any single notch from collapsing the
  image.
- **X2/X3 — the sweet spot must fail gracefully.** The gate is
  two-sided: retain useful rejection at small offsets (a seated listener
  breathes and shifts by centimeters), and *never* dip below 0 dB XTC or
  blow the ±6 dB coloration budget at moderate offsets — an off-axis
  listener should hear approximately ordinary stereo, not a comb filter.
  The notebook additionally reports the contour of the ≥ 15 dB region
  (sweet-spot width in cm/degrees) as an informational plot, not a gate.
- **X4 — the regularization vs coloration tradeoff, measured.** This is
  the canonical failure Choueiri identified: frequency-independent
  regularization of the 2×2 inverse produces dB-scale peaks/notches at
  the ipsilateral ear. ±3 dB (1/3-octave) is the budget; if the design
  cannot meet X1 and X4 simultaneously, the regularization must become
  frequency-dependent (BACCH-style) — the gates force that decision
  early rather than after someone hears it.
- **X5 — stability/headroom.** The gain ceiling is what the
  regularization parameter buys; +12 dB is the budget for both dynamic
  range loss and speaker stress. The ceiling, not the rejection, is what
  the regularization knob trades away — report both together.

### Listening pass

Setup: two loudspeakers at a measured design span (start with ±15°,
desktop geometry), listening position marked, program loudness-matched
(±0.5 LU) between conditions. Minimum **3 listeners**, at least one not
involved in development. Stimuli: dry speech (one male, one female),
percussive transients (claps, rim shots), broadband sustained (pink-noise
bursts, string pad), plus one binaural scene rendered with
`ambitap.binaural~` containing hard-lateral (±90°) and rear sources.

| Question | Method | Pass |
|---|---|---|
| Image widening / lateralization | A/B vs bypass, binaural scene: report perceived azimuth of the ±90° sources | ≥ 80% of trials place them clearly outside the speaker span, each listener |
| Front/back | rear source in the scene, A/B vs bypass: "behind or in front?" | majority "behind" with xtc~ on, and strictly better than bypass's score |
| Coloration | ABX, mono frontal speech, xtc~ vs bypass, then preference | audible difference is expected (ABX will pass); *preference* must not favor bypass, per listener over ≥ 10 trials |
| Transient integrity | percussive stimulus: any pre-echo, chirp, ringing? | zero artifact reports |
| Graceful failure | listener deliberately moves ±10 cm / turns head | image collapses toward ordinary stereo; no comb/chirp artifacts reported |

Ship rule: the widening question is the headline — if `xtc~` does not
beat bypass there, or loses the coloration preference, it does not ship.

---

## `ambitap.room~` — SH-domain early reflections + spatial reverb

### What is being measured

The module has two parts with different verifiability. The **image-source
early reflections** for a shoebox are exactly computable — arrival times,
directions, and levels have closed-form ground truth, so they get
tight tolerances. The **tail** (FDN or convolution in the SH domain) is
statistical — it gets ISO 3382-style energy metrics plus SH- and
interaural-domain diffuseness checks. All metrics are computed from a
rendered SH impulse response (impulse in → SH IR out, fixed seed) and its
binauralization through `dsp::binaural_renderer`; the ISO 3382 machinery
(Schroeder integration, octave bands) lives in the notebook.

### Numeric gates

| # | Metric | Definition | Gate (initial target) |
|---|---|---|---|
| R1 | ER arrival times | first ~20 image-source arrivals in the SH IR vs analytic shoebox times | within **±1 sample** at 48 kHz |
| R2 | ER directions | `analysis::energy_vector` DOA on a window around each arrival | within **5°** of the analytic image direction (order ≥ 3 render) |
| R3 | ER levels | per-arrival energy vs 1/r · Π(wall reflection coefficients) | within **±0.5 dB** |
| R4 | Decay time | Schroeder T20 per octave band, 250 Hz – 4 kHz, on the omni (W) channel | within **±10%** of the parameterized RT60, every band |
| R5 | EDT | early decay time (0…−10 dB), same bands | within **±25%** of parameterized RT60; no double-slope unless parameterized |
| R6 | Clarity | C50 and C80 from the rendered IR vs the analytic prediction of the direct + ER + exponential-tail parameterization | within **±2 dB**, and strictly monotone decreasing in source distance across a 3-distance sweep |
| R7 | SH order balance | tail (t > 80 ms) energy per SH order n, normalized per channel of that order | flat within **±1.5 dB** across orders — no order-dependent coloration of the diffuse field |
| R8 | Tail isotropy | \|rE\| (energy-vector magnitude) of the late tail, 20 ms windows, averaged | ≤ **0.1** |
| R9 | Interaural coherence | IACC of the binauralized tail (t > 80 ms), per octave band and broadband | broadband within **0.15** of the same-order diffuse-field reference (*revised 2026-07 — see below*); per-band within **0.15** of the KEMAR diffuse-field coherence curve above 500 Hz (below 500 Hz diffuse coherence is naturally high — track the reference, don't chase 0) |
| R10 | Determinism | fixed seed, two renders | byte-identical SH IR |

Rationale: R1–R3 are the "checkable exactly" layer — if the image-source
model is wrong, no listening test is needed to reject it. R4/R5 verify
the tail *does what the parameter says* (a reverb whose T60 knob lies is
broken even if it sounds pleasant). R6 ties the direct/early/late balance
to the distance parameter — this is the objective shadow of the distance
listening question. R7 is the SH-specific trap: an FDN that feeds orders
unevenly (or a decoder-side max-rE weighting leaking into the tail)
produces a tail whose *timbre changes with playback order* — invisible in
any single stereo render, caught by construction here. R8/R9 are the
diffuseness of the tail in the two domains that matter: the SH field
itself, and at the ears — late IACC near the diffuse-field reference is
the strongest known objective correlate of externalization and perceived
envelopment.

Threshold revision, R9 broadband (2026-07, first measurement run): the
original absolute target (broadband IACC ≤ 0.3) is unattainable through an
order-3 rendering chain — the ideal isotropic reference itself measures
0.429 broadband, because order truncation reconstructs both ears from the
same smooth low-order field at high frequencies — so the broadband gate is
restated relative to the diffuse-field reference (|IACC − IACC_ref| ≤
0.15), the same tracking rule the per-band gate already applies.

### Listening pass

Headphone-based (the module's headline value is making binaural
externalize), binaural rendering via `ambitap.binaural~`, loudness-matched
(±0.5 LU). Minimum **3 listeners**, one non-developer. Stimuli: dry
speech, percussion, sustained broadband — all anechoic sources.

| Question | Method | Pass |
|---|---|---|
| Externalization | A/B: encode→room~→binaural vs encode→binaural (BYPASS = anechoic) — "which is more outside your head?" | ≥ 80% of trials choose room~, each listener |
| Localization preserved | source at ±45°: does adding the room smear or drag the direct-sound image? | reported azimuth within ±15° of the dry condition |
| Distance | 3 parameterized distances, random order, rank them | correct ranking ≥ 80% of trials |
| Tail naturalness | rating 1–5 after A/B vs bypass and vs one reference convolution reverb IR; explicit artifact checklist (metallic ringing, flutter, chirping, pumping) | mean ≥ 3.5 and zero artifact reports on percussion (percussion is where FDN metallicity lives) |

Ship rule: externalization is the headline — the roadmap's whole case for
`room~` is "room/reflections are what make binaural externalize". If it
does not beat anechoic bypass on that question, it does not ship. A tail
that externalizes but rings metallic on percussion also does not ship
(naturalness gate).

---

## Gating — what runs where

| Check | Kind | Runs |
|---|---|---|
| X1–X6, R1–R10 | numeric, deterministic | CI: `tests/test_xtc_metrics.cpp`, `tests/test_room_metrics.cpp` in the ordinary ctest suite; the executed notebooks re-run the same asserts with plots and are re-executed whenever the module DSP changes |
| Listening passes | human | Release checklist. Required before first ship, and re-run whenever the filter design / room model changes (not for wrapper-glue or packaging changes) |
| Threshold revisions | process | any threshold change lands as a diff to this file with one line of justification |

Artifacts: the executed notebooks live in `notebooks/` (same convention as
the existing verification notebooks); the listening results are recorded
in the table below — date, commit, listener count, and the headline
numbers, so "it passed" is always attributable to a build.

### Results log

| Date | Commit | Module | Numeric gates | Listening (n, headline result) | Verdict |
|---|---|---|---|---|---|
| 2026-07-03 | ada6191 | room~ (offline prototype, seed 11) | all 21 enforced R1–R10 checks PASS, both tail architectures — fdn: worst T20 +6.0%, EDT +21.1%, C50/C80 err 0.56 dB, order balance 0.16 dB, \|rE\| 0.053, worst IACC dev 0.079, broadband dev 0.020; conv: +9.6%, +12.9%, 0.58 dB, 0.19 dB, 0.037, 0.018, 0.034 (broadband gated re. reference per the R9 revision above) | — (numeric phase; listening pending first ship) | prototype accepted; **FDN tail selected** for the C++ `dsp::room` |

---

## Open questions (decide before or during first measurement)

1. **KEMAR plant vs real heads (xtc~).** The numeric gates use the KEMAR
   plant; real listeners' HRTFs differ, so measured-on-KEMAR 20 dB will be
   less on a human. The listening pass is the real gate for this; if the
   first session shows the KEMAR-designed filters underperforming
   audibly, the options are a structural (HRTF-free) canceller mode or
   per-listener plant loading via the existing SOFA path. Decide after
   the first session, not before.
2. **Free geometry vs presets (xtc~).** The protocol assumes a computed
   plant from (span, distance). Supporting arbitrary/measured in-room
   plants is a different verification problem (measurement error enters
   the gate) — proposed: v1 ships computed-plant presets only.
3. **Tail architecture (room~).** FDN vs SH-domain convolution changes
   which gates are tight vs statistical (R4–R9 are written to hold for
   either). The R7/R9 gates are the ones most likely to force the
   architecture choice — if an FDN can't hit diffuse-field IACC, the
   answer is convolution or a hybrid, and it's cheaper to learn that from
   the notebook than from listeners.
4. **Listener pool.** Three is the floor for a solo project; if a Wave-3
   release gathers external testers, raise the externalization and
   widening gates from "each listener ≥ 80%" to a binomial-test criterion
   (p < 0.05 vs chance) over the pooled trials.
