# AmbiTap roadmap — wrappers and the object line

Status: planning document, written 2026-07-03 for handoff to a fresh
working session. Nothing here is built yet; it captures the agreed
direction, the rationale, and a concrete sequence so the next session can
start executing without re-deriving the strategy.

This is a *product-surface* roadmap. The library core (encode, rotate,
decode, binaural, analysis, embedded profile) is mature and verified — see
`AUDIT.md`, `COMPARISON.md`, `EMBEDDED.md`. What is missing is the layer
that puts that core in front of users, plus a handful of new DSP capabilities
that are most valuable *unbundled into small objects* rather than assembled
into a monolithic product.

## Where AmbiTap fits (the strategic frame)

The VST plugin space for ambisonics is well served: IEM Plug-in Suite and
SPARTA (on SAF) own end-user production tooling; Nuendo has native HOA. None
of them reach **Max/MSP, Pure Data, installations, or embedded hardware** —
and Ableton Live has *no* native ambisonics at all, only Max-for-Live
toolkits. That gap — Max/Pd/M4L + embedded — is AmbiTap's lane.

Two consequences shape everything below:

1. **The wrappers are the product.** The library is the shared core for
   native / Max / Pd targets (README states this). The first deliverable is
   the Max `mc.` object set; Pd follows the same design.
2. **Max/Pd is a composable-object paradigm.** So capabilities that a
   consumer playback SDK would bundle into one renderer are, for this
   audience, more valuable as *individual objects* that each sit on the
   existing core. That reframes an ambitious "immersive renderer" from a
   product bet into an incremental object line — the second wave below.

What AmbiTap should NOT try to be: a GUI plugin suite (IEM/SPARTA/Nuendo own
that), an Atmos tool (channel+object bed format, different math and
interchange), or a consumer-device playback SDK with loudness/device voicing
(no fixed device in Max; the ecosystem already covers metering/EQ).

## Design principles for the wrappers

- **Mirror the mental model users already have from IEM.** Separate
  `encode` / `rotate` / `decode` / `binaural` objects, AmbiX-native
  (ACN/SN3D), plain `(azimuth, elevation, order)` controls. Do not invent a
  new vocabulary.
- **Multichannel-native.** Use Max's `mc.` wrapper so one cord carries the
  HOA bus; a patch does not manage 16 or 36 individual cords. Pd uses its
  own multichannel signals (Pd ≥ 0.54).
- **Preserve the real-time contract at the wrapper boundary.** The core's
  `process()` paths are wait-free/allocation-free; the externals must keep
  parameter changes on the control thread (setters), never allocate in the
  perform routine, and use `snap_parameters()` only for offline/bounce.
- **OSC-friendly rotation.** Head-tracking in this world arrives as OSC;
  the rotate/binaural objects should take yaw/pitch/roll as ordinary
  control inputs so `udpreceive`/`[o.route]` patches drive them directly.
- **Every new DSP module needs its own correctness story.** The matrix-math
  objects are unit-testable to ~1e-7 (see the C++ suite and notebooks). The
  new perceptual modules below (crosstalk cancellation, room/reverb) are
  *listening-dependent*: a wrong regularization or a bad early-reflection
  pattern sounds worse than doing nothing. Those need a measurement +
  listening protocol, not just an `assert`. Scope them accordingly.

## Wave 1 — core Max `mc.` objects (the un-served audience today)

Ships ambisonics into Max and, via Max for Live, into Ableton — where there
is currently nothing native. This is the foundation everything else builds
on and establishes the SDK plumbing.

Objects (names indicative):

| Object | Wraps | Notes |
|---|---|---|
| `ambitap.encode~` | `dsp::encoder` | mono → HOA at (az, el); order attribute; click-free ramps |
| `ambitap.rotate~` | `dsp::rotator` | HOA → HOA; yaw/pitch/roll inlets (OSC-driven); crossfaded matrices |
| `ambitap.decode~` | `dsp::decoder` | HOA → speakers; layout preset or custom; mode-match/ALLRAD/EPAD; max-rE toggle |
| `ambitap.binaural~` | `dsp::binaural_renderer` | HOA → stereo; built-in KEMAR; head-orientation inlets |

Deliverables:
- A shared external-build scaffold (min-devkit / min-api for Max; the C ABI
  in `tools/capi/` is the natural boundary — the externals can call it or
  link the header core directly).
- One reference Max patch per object + a combined
  "encode → rotate → binaural" demo patch (mirrors the notebooks' story in
  a patch).
- Packaging: a Max Package (with the mc. objects, help patches, and the
  overview) and a note on Max for Live device wrapping.
- CI: at minimum, the externals must *build* on the three desktop OSes;
  audio-correctness is already covered by the core's tests, so the wrapper
  CI is a build/load smoke test.

Exit criterion: a Max user can encode sources, rotate the field from OSC
head-tracking, and monitor in binaural or on a speaker array, entirely in
`mc.` patches, on macOS/Windows/Linux.

## Wave 2 — cheap high-value additions to the core objects

These fold into Wave-1 objects (mostly reuse), not new objects, and each
raises the objects above the built-in options in IEM.

1. **SOFA HRTF selection** — a `binaural~` attribute to load a user SOFA
   file. The reader exists (`sofa_reader.h`, now fuzz-hardened) and
   `decompose_sh` already projects arbitrary measured sets onto the SH
   domain. Table-stakes for people who want their own ears. Nearly free.
2. **Surround-bed → HOA** (`ambitap.bed2hoa~` or an `encode~` mode) —
   encode a 5.1/7.1 bed by encoding each speaker feed at its canonical
   direction. Trivial over the existing encoder; lets people bring
   channel-based surround into an ambisonic workflow.

## Wave 3 — the object line (new capability, still on the same core)

These are the individually shippable objects that, together, cover the
"immersive renderer" territory the plugin suites don't reach in Max — but as
composable pieces, each reusing the existing HRTF / encoder / convolver /
SOFA machinery. Ordered by value-to-effort.

1. **Direct binaural panner** (`ambitap.panbin~`) — `mono + (az,el) →
   stereo` with per-source HRTF, *without* an ambisonic bus. Distinct from
   encode→binaural: lower latency, no order-limited blur, per-source. Wins
   over the ambisonic round-trip at small source counts; complements it at
   large counts. ~80% reuse: `binaural_renderer::probe_response` already
   reconstructs the HRTF at an arbitrary direction from the SH set — the new
   work is per-source directional interpolation + a small partitioned
   convolver per source (already have `partitioned_convolver`).
2. **Near-field / distance** (`ambitap.distance~`) — bundle distance gain +
   air-absorption low-pass + Doppler (have `dsp::doppler`) + near-field
   compensation (NFC-HOA bass shelf for close sources — a real ambisonics
   gap AmbiTap does not yet implement). One object for object-placement
   distance cues. Moderate; the only genuinely new DSP is the NFC filter
   (per-order shelving, Daniel's formulation).
3. **Transaural / crosstalk cancellation** (`ambitap.xtc~`) — `stereo or
   binaural → 2 loudspeakers` with crosstalk cancellation for a known
   speaker geometry. The standout: a real gap in the free Max ecosystem,
   self-contained (does not touch the ambisonics core), directly serves
   installation / desktop-immersive users with two speakers and no dome.
   **Most new DSP** (regularized inverse of the 2×2 transfer matrix, or a
   fixed structural canceller) and the first object that is genuinely
   *listening-dependent* — gate it behind a measurement + listening pass.
4. **SH-domain room / reverb** (`ambitap.room~`) — early reflections +
   spatial reverb tail in the ambisonic domain (the IEM RoomEncoder gap).
   The single most-wanted capability, because room/reflections are what make
   binaural *externalize* instead of sitting inside the head. Biggest
   new-DSP lift; also the biggest differentiator. Ambitious; only if the
   product wants to close that gap. Design note: a shoebox image-source
   early-reflection model encoding reflections as HOA point sources, plus a
   feedback-delay-network or convolution tail rendered in the SH domain.

Explicitly out of scope for the object line (Max has no fixed device to tune
for; the ecosystem already covers these): loudness normalization, device EQ,
perceptual "voicing." These are consumer-playback-product concerns, not Max
objects.

## Pure Data

Pd (≥ 0.54, multichannel signals) mirrors the same object set and design.
Do it after the Max set stabilizes — the DSP is identical (same core); only
the external glue differs. Keep the object names and inlet semantics parallel
so patches translate.

## Dependencies, risks, open questions for the next session

- **Build boundary decision (do this first):** should the externals link
  the header-only core directly, or go through the C ABI in `tools/capi/`?
  The C ABI is stable and already CI-built warning-free on 4 platforms, and
  it decouples the external from Eigen at link time — likely the cleaner
  path for the mc. objects. Confirm before writing object code.
- **Max SDK choice:** min-api (C++, modern, matches the codebase idiom) vs
  the classic C SDK. min-api is the natural fit.
- **`mc.` channel-count negotiation** is the fiddly part of the Max side —
  the object must expose the HOA channel count as a function of the order
  attribute and handle order changes without glitching (the core's
  crossfade/ramp machinery helps, but the channel *count* changing is a
  patch-level event).
- **Verification for perceptual modules:** define the measurement +
  listening protocol for `xtc~` and `room~` *before* building them (e.g.
  measured crosstalk rejection vs frequency for XTC; a clarity / EDT /
  externalization check for the room). This is a different, harder
  correctness bar than the rest of the library and should not be
  hand-waved.
- **Embedded orthogonality:** the object line is desktop/Max-facing and does
  not affect the embedded RT profile. But if any new core DSP (NFC filter,
  XTC) is added to the library proper, keep it inside the freestanding
  constraints (no exceptions/threads/Eigen on the audio path) so the
  embedded gate stays green — see `EMBEDDED.md` for the profile definition.

## One-paragraph summary for the next session

The library core is done and verified; the work now is the product surface.
Build the Max `mc.` object set first (encode / rotate / decode / binaural) —
that serves a real un-served audience (Max, and Ableton via Max for Live) and
lays the SDK plumbing. Then fold in SOFA-HRTF selection and surround-bed
encoding (nearly free). Then grow the object line — direct binaural panner,
near-field/distance, transaural/XTC, and (if ambitious) an SH-domain room —
each a small object reusing the existing engine, together covering the
immersive-rendering territory the VST suites don't reach in Max. Keep every
new perceptual DSP module (XTC, room) behind a measurement + listening
correctness pass, and keep any new core DSP inside the embedded freestanding
constraints. Decide the external↔core build boundary (C ABI vs header link)
before writing object code.
