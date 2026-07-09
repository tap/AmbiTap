# Out of the headphones

Binaural rendering is a wonderful lie told to two ears. This chapter tells
the truth to a room instead: the same scene you built in Chapters 3–4,
decoded to actual loudspeakers, where the soundfield exists in the air and
listeners can walk around inside it.

If you don't own four loudspeakers, read on anyway — the chapter ends with
what to do about that, and the concepts here (layouts, decoders, channel
order) are load-bearing for the whole rest of the book.

## The last box

Open **`patchers/booklet/03-out-of-the-headphones.maxpat`**, or swap the
renderer at the end of Chapter 4's patch:

```text
[pink~]
   |
[ambitap.encode~ 3]
   |
[ambitap.rotate~ 3]
   |
[ambitap.decode~ 3 quad]      ← scene in, four speaker feeds out
   |
[mc.dac~ 1 2 3 4]
```

`ambitap.decode~` takes two creation arguments: the **order** (match the
bus, as always) and a **layout** name that fixes how many output channels
it produces and where it assumes the speakers are. The built-in layouts:

| Layout | Speakers | Where |
|---|---|---|
| `stereo` | 2 | ±30° front pair |
| `quad` | 4 | ±45°, ±135° — a square around you |
| `hexagon` | 6 | a 60°-spaced ring |
| `octagon` | 8 | a 45°-spaced ring |
| `surround_5_1` | 5 | ITU 5.1 angles (no LFE — see below) |
| `surround_7_1` | 7 | 7.1 angles (no LFE) |
| `surround_7_1_4` | 11 | 7.1 plus four height speakers |
| `cube` | 8 | lower + upper square: full 3D |

The decoder's output is one mc cable, **one channel per speaker, in the
layout's canonical order** — for `quad` that's front-left, back-left,
back-right, front-right. `mc.dac~ 1 2 3 4` then maps those onto your audio
interface's outputs in that order, so the only setup job is knowing which
interface output feeds which physical speaker (and passing `mc.dac~` the
right numbers if it isn't 1–4).

## Setting up four speakers honestly

The decode assumes a listener at the center of a square of speakers. You
don't need an anechoic chamber, but three disciplines repay you instantly:

1. **Equal distances.** The math assumes all speakers equidistant from the
   center. A tape measure is a spatial audio tool.
2. **Equal levels.** Play pink noise through each speaker in turn (sweep
   the encoder azimuth to 45°, 135°, −135°, −45° — at those exact angles,
   a quad decode hands one speaker most of the signal) and match by ear or
   SPL meter.
3. **Angles as advertised.** ±45° and ±135° from the listening position —
   the decoder is computing gains for *those* directions, not for wherever
   the furniture allowed. Close counts; 20° off doesn't.

Now run the orbit from Chapter 3. Walk around. Sit in the middle and sweep
the rotator's yaw. Notice what headphones couldn't give you: the field is
*in the room* — externalization for free, no HRTF required — and several
people can hear it at once. Notice also what got worse: away from the
center, images pull toward the nearest speaker (Chapter 1's precedence
effect, back for revenge), and a four-speaker ring has no idea what "above"
means — send `elevation 1.0` and the circle just… flattens. Height needs
height speakers (`cube`, `surround_7_1_4`) or headphones.

## What the decoder actually did

Chapter 2 promised: a decoder is a small matrix computed once for your
layout. Concretely, `ambitap.decode~ 3 quad` built a 4×16 matrix — four
speakers, sixteen scene channels — and every sample of your speaker feeds
is that matrix applied to the bus. All the intelligence lives in *choosing*
the matrix; there are competing philosophies, and the object exposes them:

- the `decoder_type` attribute selects the construction — `mode_match`
  (the default), `allrad`, or `epad`;
- the `max_re` attribute (off by default) applies a per-order weighting
  that trades a little theoretical sharpness for cleaner energy
  concentration — usually a good trade in real rooms.

Chapter 12 is an entire chapter about that choice, with figures computed
from the actual matrices, and a rule of thumb for which construction suits
which array. For a regular ring like `quad`, the honest summary is: the
defaults are fine, try `max_re 1`, and don't lose sleep.

One deliberate omission: **there is no LFE channel** anywhere in this. The
`surround_5_1` layout is five full-range speakers. An ambisonic scene
describes direction, and sub-bass direction is barely perceptible —
so LFE/bass management is a *playback* concern, handled downstream of the
decode (Chapter 17 shows the routing when we meet channel-based beds).

## No speakers? Two honest options

**Option one: stereo decode.** `ambitap.decode~ 3 stereo` produces feeds
for a normal ±30° speaker pair. It works — the frontal stage is stable and
CPU cost is nil — but understand what it is: a projection of your sphere
onto stereo's narrow window. Rear and height content doesn't vanish (the
decode folds it in at reduced level so energy isn't lost), but it no longer
sounds *behind* you. It's the right tool for "my ambisonic piece needs a
stereo bounce," not for monitoring spatial decisions.

**Option two — the recommended one: keep monitoring binaurally.** This
isn't a consolation prize. `ambitap.binaural~` *is* a decode — internally
it renders the scene as if through a fine array of virtual loudspeakers,
each convolved to your ears — and it's a truthful monitor for direction,
which is what you're composing with. Professionals mixing for domes they
visit twice a year work exactly this way: compose binaurally, decode on
site, spend the precious room time on level calibration instead of
composition. The companion patch is wired for the same discipline — the
quad decode and a binaural monitor side by side, one mc gain to switch
between them — so "check it on speakers" stays a five-second habit even
when the speakers are hypothetical.

## Checkpoint — and the end of the beginning

You can now take a scene from silence to sound three different ways:
binaural for headphones, a quad ring, a stereo fold-down — same scene, one
box swapped. That's the "mix once, decode anywhere" promise of Chapter 2,
demonstrated rather than asserted.

You also, quietly, now hold the entire ambisonic signal chain:
**encode → transform → decode**. Everything else in this book is a richer
version of one of those three stages — fancier sources into the bus (rooms,
recordings, beds), fancier transforms on it (mirrors, compressors, virtual
microphones), fancier renders out of it (arrays, personalized HRTFs,
crosstalk cancellation). Before the craft, though, one debt from Chapter 3
is still outstanding: *what are those sixteen channels, actually?* Part II
opens the bus.
