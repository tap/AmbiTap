# Placing sources

Part III is organized by job, and the first job is the daily one: take
some mono material — synths, samples, stems, live inputs — and place each
piece somewhere deliberate. You already own the mechanics (`encode~`,
azimuth, elevation, summed buses); this chapter is about doing it *well*,
at mix scale, and about the one architectural fork the job hides.

Companion patch: **`patchers/booklet/04-placing-sources.maxpat`**.

## The bus discipline

A workable spatial mix in Max is one habit applied consistently: **one
`mc.` cord is the master scene**, every source gets its own
`ambitap.encode~ N`, and everything sums into that cord before the
renderer. The companion patch lays out three sources this way — noise, a
tone, a click pattern — each with its own azimuth/elevation controls, so
the mix reads like a mixer: channel strips into a bus.

Practical notes that save real time:

- **Give every encoder the same order.** One order-1 encoder summed into
  an order-3 bus doesn't crash — its 4 channels land on the first 4 of 16
  — and the result is even *legitimate* (a deliberately blurrier source,
  the nesting property from Chapter 7). But do it by decision, not by
  typo; a source that refuses to sharpen is the classic symptom.
- **Set levels before the encoder** (a `*~` per source, or the encoder's
  `gain` attribute — same thing). The bus is a sum; balance is easiest
  while each voice is still mono.
- **Automate angles like any parameter.** The `azimuth`/`elevation`
  setters ramp click-free over a few milliseconds (every AmbiTap
  parameter does — the library's "click-free contract"), so `line`,
  `function`, LFOs, and live dials are all fair game. For *fast* circular
  movement prefer driving azimuth from a phasor (Chapter 3's idiom): it
  wraps cleanly at ±180° where a naive `line` ramp would spin the long
  way round.
- **Park a monitor on the bus.** An `mc.meter~` tells you instantly which
  source just clipped the scene; Chapter 14 upgrades this to real
  soundfield instruments.

## Elevation earns its keep

New spatializers overuse azimuth (the party trick) and underuse elevation
(the depth of field). Two placements that transform dense mixes:

- **Tilt the pads up.** Ambient beds at +30–45° elevation stop competing
  with foreground material at ear level. The mix gains a vertical layer
  cake structure that stereo literally cannot express.
- **Keep transients near the horizon.** Elevation perception (Chapter 1)
  is spectral and fragile; percussive, broadband material reads its
  height cues best, but *everything* localizes most stably at ear level.
  The horizon is your focal plane.

## The fork: through the bus, or around it?

Now the architectural decision this chapter exists to teach. For
headphone delivery, there are two ways to render a placed mono source,
and the package ships both:

**Through the scene:** `encode~ 3` → bus → `binaural~ 3`. The source
becomes part of the field, subject to the field's order-3 blur (~75°
beam, Chapter 7), rotatable with the world, mixable with everything else.
Cost: one convolver bank *total*, however many sources — the renderer
binauralizes the whole bus at once.

**Around the scene:** `ambitap.panbin~` — mono in, `azimuth`/`elevation`
messages, stereo out. It convolves *this one source* directly with a
per-direction HRTF (the full order-5 resolution of the embedded KEMAR
set), skipping the bus entirely. No order-limited blur: this is as sharp
as the dataset gets. Direction changes crossfade click-free. Cost: one
convolution pair *per source*, and the result is a stereo signal — it
can't be rotated with the scene, `vmic~`'d, or decoded to a dome; it's
already rendered.

The trade in one line: **the bus scales, the direct path sharpens.**

| | `encode~` → bus → `binaural~` | `panbin~` per source |
|---|---|---|
| Sharpness | order-limited (order 3 ≈ 75° beam) | full HRTF resolution |
| CPU | ~flat with source count | linear per source |
| Rotates with scene / head-trackable via bus | yes | no |
| Decodable to speakers later | yes | no — headphones only |
| Best for | scenes, beds, many sources, anything with a future on speakers | a few precious foreground sources, headphone-final work |

The companion patch wires the same source both ways behind an A/B switch.
Listen at 90° azimuth: through the order-3 bus the source is a presence
to your left; through `panbin~` it is a *point*. Then imagine forty of
them and check the CPU meter. Both instincts are correct; that's why both
objects exist. (Game engines reached the same design decades ago —
Chapter 21 — rendering foreground objects directly and carrying the
ambience in a bus. You are allowed to mix strategies within one patch:
`panbin~` the soloist, bus the orchestra, sum the stereo outputs.)

## Checkpoint

You can place and automate any number of sources on a disciplined bus,
you use elevation as a mixing dimension, and you can argue both sides of
bus-versus-direct rendering and pick per source. Placed sources still
float in an airless void, though: every source is *somewhere*, but no
source is *far away*. Distance is its own set of cues, and the next
chapter manufactures them.
