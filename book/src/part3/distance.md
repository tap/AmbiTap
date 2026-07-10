# Distance

Sweep an encoder's azimuth and the source moves convincingly. Now try to
make it *walk away*. There is no knob: azimuth and elevation are angles on
a sphere of fixed radius, and nothing in Part I or II ever said how far
away that sphere is. Distance isn't a direction — it's a bundle of
physical side effects, and to place a source at three meters you
manufacture the side effects of three meters.

Companion patch: **`patchers/booklet/05-distance.maxpat`**.

## What "far" sounds like

Four cues, in rough order of importance:

1. **Quieter.** Level falls as 1/distance (−6 dB per doubling) for a
   point source in free space — less steep indoors, where walls return
   energy.
2. **Duller.** Air absorbs high frequencies; a distant source is
   low-passed by the atmosphere itself. Subtle per meter, decisive per
   hundred meters.
3. **More room, less source.** At distance, the reverberant field
   swallows the direct sound; the direct-to-reverb ratio is arguably the
   strongest distance cue indoors. (This one needs a room to exist —
   next chapter's job.)
4. **Late — and shifting when moving.** Sound takes ~2.9 ms per meter to
   arrive. A *constant* delay is nearly inaudible on its own, but a
   *changing* distance turns propagation delay into pitch: the Doppler
   effect, the single most physical "it's really moving" cue there is.

And one anti-cue for close range: a **near** source (inside about a
meter) gets a bass boost from wavefront curvature — the same physics as
microphone proximity effect. Ambisonics has a name for compensating it
(**NFC**, near-field compensation), and it's the difference between "at
my shoulder" reading as intimate versus just loud.

## One object for the bundle

`ambitap.distance~` packages the whole chain for a bus — Doppler delay,
then 1/r gain, then air-absorption low-pass, then per-order NFC shelving
— driven by a single master parameter, in meters:

```text
[ambitap.encode~ 3]        ← direction, as always
    |
[ambitap.distance~ 3]      ← distance 3.5   (meters)
    |
   bus …
```

The controls that matter, in the order you'll reach for them:

- **`distance`** — the meters. Automate this and everything downstream
  follows. Distance *changes* glide rather than jump: the internal delay
  slews, which is what makes fast automation produce a genuine Doppler
  pitch bend instead of a click (the same slewing you can watch measured
  against 1 ± v/c in the library's `dsp_behavior` notebook).
- **`reference_distance`** — the "zero dB" range: the distance at which
  the object applies no gain change. Set it to where you balanced the
  source in Chapter 9's mixing pass (default 1 m), so adding distance
  processing doesn't re-balance your mix.
- **`attenuation`** — the 1/r exponent. 1.0 is free-field physics; real
  rooms behave shallower (0.5–0.8), and cinematic taste is often
  shallower still. This is a *style* control wearing a physics costume.
- **`air_absorption`** — depth of the high-frequency loss. Physical
  default; exaggerate for haze, zero it for clinical.
- **`doppler` / `nfc`** — toggles for the two ends of the chain: Doppler
  matters when things move fast, NFC when things come close. Both on by
  default; turn Doppler *off* for material where pitch is sacred (a
  distant choir shouldn't detune as it drifts).

Order the chain correctly by instinct: `distance~` sits **after** the
encoder (it operates on the source's bus representation), one per
source-with-a-range; sources that live at a fixed comfortable distance
don't need one at all.

There is also a lone **`ambitap.doppler~`** — just the variable
propagation delay, no gain/air/NFC — for when you want the pitch physics
à la carte (classic use: a synthetic fly-by where you're drawing the
level curve by hand anyway).

## The experiment

The companion patch puts a ticking source on the bus with a distance
dial spanning 0.3–40 m and each stage on its own toggle. Three things to
do with it, ears closed to theory:

1. **Sweep distance slowly with everything on.** Note how little of the
   effect is level once air absorption joins in past ~10 m.
2. **Automate a fast pass** (2 m → 30 m in two seconds). Toggle
   `doppler` off and on. Off: a fader move. On: something *goes by*.
3. **Creep inside 1 m and A/B `nfc`.** With it, the source leans into
   your face; without, it's merely near. (Binaural rendering makes this
   easiest to hear; on speakers, NFC's benefit depends on the array
   radius — the decode chapter returns to this.)

What you will *not* get, even with everything on: true "far away"
indoors. A source at the end of this chain in a dry scene sounds like a
quiet, dull, correct object in an anechoic void — because cue #3, the
room, is still missing. That's next.

## Checkpoint

Distance is manufactured, not dialed: 1/r, air, delay/Doppler, NFC — one
object chains them per source, with `reference_distance` protecting your
mix balance and taste controls (`attenuation`, `air_absorption`)
adjusting how much physics you want. The void problem remains, and it's
the best possible motivation for the next chapter: rooms.
