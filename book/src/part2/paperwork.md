# The paperwork: formats and conventions

Sooner or later — usually the day a collaborator sends "the ambisonic
stems" — you will play a B-format file and hear something *almost* right:
levels off, the image smeared, front and left subtly traded. Nothing is
broken. You have met the field's history, encoded as channel order.

This chapter is short, practical, and worth its weight in un-smeared
mixes.

## Two dialects

Ambisonics predates its own standardization by four decades, so there are
two families of convention in the wild:

**FuMa** (Furse–Malham), the historical dialect. First-order channels in
the order **W, X, Y, Z**, and W recorded 3 dB down (a −3 dB factor,
1/√2, inherited from analog-era headroom practice, with further
per-channel factors at higher orders). Four decades of recordings,
`.amb` files, classic tools (and the SoundField microphone lineage) speak
FuMa. Defined cleanly only up to order 3.

**AmbiX** (2011), the modern standard and the only thing new systems
should emit. Channels in **ACN** order — W, then **Y, Z, X** (the
`n(n+1)+m` indexing from Chapter 6) — at **SN3D** scaling, no W
attenuation. Everything in AmbiTap, every current game engine, YouTube,
and essentially all software written this decade speaks AmbiX.

Same soundfield, two filing systems:

| | FuMa | AmbiX (ACN/SN3D) |
|---|---|---|
| First-order order | W **X Y Z** | W **Y Z X** |
| W level | −3 dB (×1/√2) | full scale |
| Higher orders | per-channel legacy factors ("maxN"), defined ≤ order 3 | one rule, any order |
| You'll meet it in | `.amb` files, older recordings & plugins, SoundField heritage | everything modern; assume it unless told otherwise |

## What mismatch sounds like

Feed FuMa into a system expecting AmbiX and two things happen at once.
The W attenuation reads as a **level/balance error** (the omni content
3 dB shy, so the scene sounds oddly hollow and over-directional). The
X↔Y swap reads as a **geometry error**: front-back content lands on the
left-right axis and vice versa — the image doesn't rotate so much as
*fold*. It is exactly weird enough to waste an afternoon, because it
still sounds "spatial."

The fix is one object:

```text
[ambitap.format~ 3]     attribute: direction — fuma_to_ambix / ambix_to_fuma
```

Multichannel in, multichannel out, orders 0–3 (a FuMa limitation, not an
AmbiTap one), exact published conversion factors (they're pinned by
exact-value tests in the library, against the AmbiX specification's own
tables). Put it at the border the moment anything historical crosses into
your patch, and forget it's there.

## The rest of the paperwork

Channel order and scaling are the big two; three smaller lines complete
the customs form.

**Angles and axes.** AmbiTap: radians; azimuth 0 = front, positive =
counterclockwise from above (+90° = left); elevation positive = up;
rotations yaw-then-pitch-then-roll (Chapter 4's figure). Other tools may
speak degrees, measure azimuth clockwise, or compose Euler angles in
another order. None of this corrupts a *file* — it corrupts *control
data*, which is why a head-tracker driver is where you'll meet it.

**"B-format" is ambiguous on its own.** Historically it implied FuMa;
today people say it for any ambisonic bus. When a collaborator offers
B-format, the professional reply is three questions: *what order, what
channel order (FuMa or ACN), what normalization (SN3D or N3D)?* — that
last one because some research tools emit N3D, a cousin scaling in which
higher-order channels run hotter than SN3D by fixed per-order factors
(√(2n+1); the fix is per-channel gains, and knowing the name is most of
the battle).

**Files.** There is no dedicated ambisonic file format in common use —
scenes travel as ordinary multichannel WAV (or WavPack/FLAC), and the
convention travels as *metadata at best, folklore at worst*. `.amb` is a
WAVE variant that reliably means FuMa. A 4- or 16-channel `.wav` means
whatever its maker meant; a `README` beats a filename. When *you* export:
AmbiX, state "AmbiX (ACN/SN3D), order N" in the delivery notes, and
you've done your part for civilization.

> **For the curious.** The AmbiX specification is Nachbar, Zotter,
> Deleflie & Sontacchi, *AMBIX — A Suggested Ambisonics Format* (Ambisonics
> Symposium 2011) — short and readable. The FuMa↔AmbiX factor tables it
> publishes are the ones `format~` implements; the library's
> `docs/COMPARISON.md` records the exact-value tests, and its ACN⇄FuMa
> index map is eight integers you can read yourself in
> `dsp/format_converter.h`.

## Checkpoint — and the end of the theory

Part II in three sentences: an ambisonic scene is a set of coincident
microphone patterns (order 1: one omni, three figure-8s). More orders mean
finer patterns, sharper images, quadratically more channels, and order 3
is the sensible default. Modern channels are filed ACN/SN3D — "AmbiX" —
and one object repairs history when it knocks.

That is all the theory this book makes you carry. From here on, every
chapter is a job: placing sources, adding distance, building rooms,
decoding to arrays, and the rest of the craft. Patch cords from here to
the end.
