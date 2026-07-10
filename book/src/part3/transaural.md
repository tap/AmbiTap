# Speakers pretending to be headphones

Chapter 13 ended with binaural rendering as your most portable renderer
— with one string attached: headphones. The moment those signals play
from two *loudspeakers*, physics vandalizes them: the left speaker
reaches your **right** ear too (and vice versa), a few hundred
microseconds late, filtered by your head. That's **crosstalk**, and it
erases exactly the interaural differences the render worked to forge.

This chapter is about fighting back — **crosstalk cancellation** (XTC,
also *transaural* audio): pre-processing the two speaker feeds so that,
at one listener's ears, the crosstalk arrives pre-cancelled and the
binaural illusion survives open air. It's the most conditional trick in
the book, which is why it gets the book's most honest chapter.

Companion patch: **`patchers/booklet/11-transaural.maxpat`**.

## The idea in one paragraph

The path from two speakers to two ears is four filters (each speaker to
each ear — measurable head acoustics, the same KEMAR data as Chapter
13). Those four filters form a 2×2 system; invert it, and you get four
*correction* filters that make the acoustic journey undo itself:
feed the corrected signals to the speakers and what lands at your ears
is (approximately) the original binaural pair. Each speaker emits a
precisely timed, filtered anti-copy of the other's crosstalk; the air
does the subtraction at your head.

The catch is the word *your*: the inversion assumes a specific head at
a specific spot. Move, and the cancellation unravels.

## The object

```text
binaural stereo (e.g. [ambitap.binaural~ 3] or a binaural file)
    |         |
[ambitap.xtc~]           ← geometry: span, distance
    |         |
two LOUDSPEAKERS         (never headphones — the "correction" would
                          itself be the artifact)
```

You tell it the geometry it must invert: **`span`** (the full angle
between the speakers as seen from the listening position, degrees) and
**`distance`** (listener to speakers, meters). Change either and the
filters are redesigned from the KEMAR model — off the audio thread,
crossfaded in, like every rebuild in the package. `regularization`
(0–1) trades cancellation depth against filter aggressiveness — lower
digs deeper but rings harder and breaks more brittly off-center; the
default 0.5 is a sane perch.

Two built-in honesties to plan around: the object adds **512 samples of
latency** (~11 ms at 48 kHz), and its output sits about **12 dB below
bypass** — headroom the aggressive inverse filters require. That's what
the ramped `bypass` attribute is for: it level-compensates *comparison*
poorly if you just flick it, so the companion patch A/Bs through a
+12 dB trim on the processed path, per the library's own listening
protocol. (The filter design itself is gated by measured tests in the
library — in-band cancellation depth among them — so what you're
tuning is geometry and taste, not whether the math works.)

## What it's actually for

Where XTC earns its keep — the honest list:

- **The desktop.** One person, a meter from a stereo pair, head
  naturally steady: the near-ideal case. Personal near-field
  spatial audio without headphone fatigue. (Narrow spans work; the
  designer accepts 5–120°, and research systems favor *very* narrow
  "stereo dipole" spans for exactly this seat.)
- **The demo chair / sweet-spot installation.** A gallery piece with
  one seat; a listening-bar setup; the client preview chair. Staged
  deliberately — a marked seat is part of the piece — it's magical.
- **Curiosity and craft.** Hearing a fly circle your head from two
  visible speakers rewires your respect for interaural cues faster
  than any diagram.

And the disqualifying conditions, equally honest: **more than one
simultaneous listener** (the correction for seat A is garbage at seat
B), **audiences that move**, **reverberant rooms** (the room's
reflections re-introduce uncorrected paths — dry rooms and near-field
setups survive best), and anything where 11 ms of extra latency hurts.
For those, decode to speakers (Chapter 12) like a sensible person; XTC
is a scalpel, not a PA.

Note also what the signal path implies: XTC renders *binaural*
material. Your ambisonic scene reaches it through `binaural~` — so the
full chain is scene → binaural render → crosstalk-cancel → two
speakers, and everything you know about the binaural stage (MagLS,
SOFA ears, head-tracking-less front/back flips) still applies at this
one's input.

## The experiment

The companion patch: an orbiting scene through `binaural~` into
`xtc~`, the trim-matched A/B, and geometry controls. Sit accurately
(tape measure; enter the *true* span and distance — self-reporting
flatters), then:

1. Orbit with `bypass 1` (plain stereo playback of a binaural render):
   the image lives between the speakers, Chapter 1's narrow window.
2. Engage. The orbit should *leave* the speakers — sides opening
   beyond the span, rear content plausibly behind. Depth of the effect
   varies with your head-vs-KEMAR similarity and the room's dryness.
3. Now lean half a meter left. Watch the sphere collapse back into
   two speakers. That collapse *is* the chapter: you've heard both the
   power and the contract.
4. `patchers/ambitap.xtcdesigner.maxpat` shows the filters themselves
   redesigning as you drag the geometry — worth two minutes to see
   what you're listening through.

## Checkpoint

XTC inverts the speaker-to-ear acoustics for one head at one spot:
binaural in, two speakers out, geometry told truthfully,
regularization to taste, 512 samples and −12 dB as the cost of doing
business — a one-listener scalpel that's the wrong tool for every
audience larger than one. Last craft chapter next, and it points the
other direction entirely: not rendering scenes out, but folding the
channel-based world *in*.
