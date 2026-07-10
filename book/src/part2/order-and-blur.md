# Order and blur

"Which order do I need?" is the first question every newcomer asks and the
question most answers dodge. This chapter answers it with numbers computed
from the library, then adds the perceptual caveats that make the honest
answer more interesting than the numeric one.

## The exchange rate

An ambisonic scene at order N resolves direction about as finely as a
beam this wide — here is the −3 dB width of the sharpest well-behaved
(max-rE) beam the scene can express, against the channel count you pay
for it:

![Beamwidth versus order, and the quadratic channel-count
price](../img/order-blur.svg)

Read the two panels together and the economics of the format fall out:

- **Order 1** (4 channels): a beam ~157° wide. "Leftish." Genuinely
  enveloping, genuinely vague. This is what a first-order microphone
  records and what YouTube 360 plays back.
- **Order 3** (16 channels): ~75°. Sources have places, not regions.
  The workhorse order — sharp enough to compose with, cheap enough to
  run many of.
- **Order 5** (36 channels): ~51°. Noticeably focused; also nine times
  first-order's channel count, in CPU, disk, and patch-cord width.

Sharpness improves roughly like 1/order, but cost grows like order². Each
step up buys less blur reduction than the last and costs more channels
than the last — *that* is why the answer to "which order?" is a judgment
call rather than "the biggest number you can afford."

## What blur actually sounds like

"Beamwidth" is a proxy. What you hear, order by order, is a bundle of
effects — worth knowing individually, because different projects care
about different ones:

- **Source focus.** At low order a point source sounds *wide* — pleasant
  for ambience and pads, wrong for a fly buzzing past an ear.
- **Separation.** Two sources 30° apart are one wide source at order 1,
  and two events at order 3+. If your material is dense and positional
  (dialogue scenes, counterpoint spatialization), order buys audible
  polyphony.
- **Sweet-spot size.** On loudspeakers, higher order holds the image
  together over a larger listening area — the low-order image collapses
  toward the nearest speaker sooner as you move off-center. For
  installations where people wander, this is often the *main* reason to
  pay for order.
- **Rear/height solidity on sparse arrays.** Where speakers are far
  apart, low order leans harder on the decoder's interpolation; images
  between speakers get phasey sooner.

## The honest complications

The clean curve above comes with three riders that practitioners learn by
expensive experience, offered here at book price.

**1 — Your renderer caps what order can deliver.** Binaural rendering with
a *non-individual* HRTF (Chapter 3's mannequin ears) blurs elevation and
front/back on its own; beyond roughly order 3, extra scene sharpness gets
laundered through those borrowed ears and much of it is lost. On
headphones with the stock KEMAR set, order 3 versus order 5 is a subtle
A/B; on a good 30-speaker dome it is not subtle at all. Match spend to
renderer.

**2 — Microphones lag encoders.** A synthetic scene can be order 5 at the
cost of CPU. A *recorded* scene is bounded by hardware: the ambisonic
microphones you can buy run from order 1 (most) through order 4
(Eigenmike em32-class instruments) — Chapter 19 surveys the market. Plan
hybrid: recorded first-order bed + encoded higher-order foreground is a
respectable, common design.

**3 — Order interacts with frequency.** The scene reconstructs the field
accurately only up to a frequency that rises with order (and shrinks with
listening-area radius). Above it, reproduction degrades gracefully from
"physically correct" to "psychoacoustically plausible" — which is why
decoders apply the max-rE weighting you met as an attribute in Chapter 5:
it optimizes the plausible regime that most of the audio band actually
lives in.

> **For the curious.** The reconstruction limit is the "kr rule": accurate
> holography holds roughly while N ≥ kr, with k the wavenumber and r the
> head/area radius. For a head (r ≈ 8.75 cm) that's about 700 Hz per
> order — order 3 reconstructs to ~2 kHz, and everything above relies on
> energy-vector psychoacoustics (hence max-rE, which maximizes exactly
> that vector). Zotter & Frank ch. 2 (Appendix D) derives all of it. The
> beamwidth figure and this chapter's numbers regenerate from the
> library via `scripts/generate_book_figures.py`, with the monotonic
> sharpening asserted at build time.

## The planning table

Rules of thumb, not laws — each row assumes the renderer can keep up:

| Order | Ch. | Reach for it when |
|---|---|---|
| 1 | 4 | Recorded ambience beds; YouTube/360 delivery; maximum compatibility; "spacious" beats "precise" |
| 2 | 9 | Tight channel budgets (games, mobile) that still want believable movement |
| **3** | **16** | **The default.** Composition, installation, VR foreground, binaural work with stock HRTFs |
| 4–5 | 25–36 | Large speaker arrays and domes; wandering audiences; research; personalized-HRTF binaural |
| 6+ | 49+ | Specialist arrays and papers. AmbiTap computes to order 10; your ears in a normal room plateau far earlier |

And a rule of thumb for changing your mind: you can always **truncate**
an ambisonic scene (drop the higher-order channels of an order-5 mix and
you have a legitimate order-3, then order-1, mix — the format nests). You
can never *add* order to a recording after the fact. When in doubt,
produce one order higher than you plan to deliver.

Order chosen, channels understood — one hazard remains before the craft
chapters, and it's the one that bites hardest in the wild: two files can
both say "Ambisonics" and disagree about what the channels *are*.
