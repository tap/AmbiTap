# Decoding to real speakers

Chapter 5 got you from bus to loudspeakers with defaults and discipline.
This chapter is the craft edition: what the decoder families actually
trade against each other, which to choose for which array, and what to do
when your room refuses to be a diagram. It contains this book's most
useful figure.

Companion patch: **`patchers/booklet/07-decoding.maxpat`**.

## The problem, honestly stated

A decoder turns C scene channels into L speaker feeds — a single L×C
matrix. If speakers surrounded you densely and uniformly, every sensible
construction would converge on the same matrix and this chapter would be
a footnote. Real arrays are sparse (four to a dozen speakers), irregular
(5.1's 80° front density vs. 140° rear gap), and incomplete (no floor
speakers, often no height). A matrix must now *approximate*, and the
decoder families are three philosophies of what to sacrifice.

- **`mode_match`** (the default): algebra-first. Invert the encoding
  process — find the feeds that would re-encode back into the original
  scene. Faithful where the layout supports it; where the layout is
  gappy, the inversion strains, and loudness can swing hard with
  direction.
- **`allrad`**: rendering-first. Decode to an ideal *virtual* array
  (mathematically perfect, exists only inside the matrix), then place
  each virtual speaker onto your *real* speakers with amplitude panning
  (VBAP — the object-renderer workhorse from Chapter 2, working inside
  your decoder). Never strains, because panning can't strain; instead it
  blurs where speakers are missing.
- **`epad`**: energy-first. A construction that keeps the decode's total
  energy transfer uniform (built from an orthogonalized re-encoding,
  discarding what the layout provably cannot render — e.g. height
  channels on a flat ring).

Words are cheap; here are the actual matrices measured. 5.1, order 3,
max-rE on — loudness and image focus for a source swept around the
circle:

![Energy and rE concentration around the circle for the three decoder
constructions on 5.1](../img/decoder-comparison.svg)

The figure *is* the guidance. On this gappy layout, `mode_match` and
`epad` swing 15–18 dB in loudness around the circle (listen to the left
panel: a source panned through the rear gap ducks, then blooms at the
surrounds); `allrad` holds loudness within ~4 dB everywhere. The right
panel shows the price: in the rear gap `allrad`'s focus (|rE|) falls to
~0.4 — wide, wallpaper-soft imaging — where the others hold more focus
at the cost of that loudness rollercoaster. Nothing wins; the layouts
choose.

## Choosing, as a table

| Your array | Reach for | Because |
|---|---|---|
| Regular ring or sphere (quad, hexagon, octagon, cube) at adequate order | `mode_match` (default) or `epad` | the inversion is healthy; you get maximum faithfulness, and the two nearly agree |
| Irregular / gappy (5.1, 7.1, 7.1.4, real venues) | `allrad` | even loudness beats sharp-but-lumpy on layouts with holes; this is what it was invented for (Zotter & Frank 2012) |
| Ring only, but the scene has height | `epad` or `allrad` | both handle the un-renderable height channels gracefully; naive inversion can misbehave |
| Undecided | A/B it — it's one message | `decoder_type allrad` etc.; rebuilds happen on a worker thread and **crossfade in click-free**, so switching mid-playback is a legitimate listening test |

And `max_re 1`, the attribute from Chapter 5, composes with all three:
it tapers the higher orders to concentrate energy (the psychoacoustic
optimum above the reconstruction limit — Chapter 7's sidebar). On real
speakers in real rooms it is almost always an improvement; the book's
default advice is simply *on*.

## The room strikes back

The matrix assumes equidistant, level-matched, correctly-angled
speakers. Chapter 5's tape-measure discipline covers the ideal case;
real rooms add three adjustments worth knowing:

- **Unequal distances** (the sofa is against the wall): delay the near
  speakers so wavefronts arrive together — `mc.delay~` on the decoder's
  output cord, ~2.9 ms per meter of shortfall, before level matching.
- **Unequal speakers** (the rears are smaller): match levels with pink
  noise per speaker (Chapter 5), and accept that timbre will shift with
  direction; `allrad`'s even energy makes mismatched speakers *less*
  conspicuous, one more point in its favor for found arrays.
- **A layout that isn't any preset:** the preset list (Chapter 5's
  table) covers the standard rigs. For a genuinely custom array —
  the gallery's seven ceiling speakers — the library computes decoders
  for arbitrary speaker lists (it's one function call; the Max object
  currently exposes the presets), so a custom rig is a feature request
  or a small C++ patch away rather than impossible. Until then, pick
  the nearest preset and correct angles physically — moving a speaker
  beats lying to the matrix.

One loose end from Chapter 10: the `nfc` stage there compensates the
*source's* proximity. The *speakers'* own proximity (a desktop-radius
rig curves wavefronts too) is a further refinement — NFC-HOA per array
radius — that AmbiTap doesn't currently expose; at typical listening
radii (≥ 1.5 m) its absence is minor. Know the term, don't lose sleep.

## The listening protocol

The companion patch wires the full A/B: an orbiting source, `decode~ 3
surround_5_1` with the three `decoder_type` messages and the `max_re`
toggle, plus a binaural monitor branch for the speakerless. The
protocol that teaches fastest, on speakers or on the binaural stand-in:

1. Orbit slowly with `mode_match`. Hear the loudness swing as the
   source crosses the rear gap — the left panel of the figure, live.
2. Switch to `allrad` mid-orbit (it crossfades). Loudness levels out;
   listen for what softened in the rear.
3. Toggle `max_re` both ways on each. Cleaner concentration versus a
   hair of sparkle.
4. Park the source at −110° (a surround speaker) and A/B again —
   differences nearly vanish *on* a speaker; the philosophies only
   disagree *between* speakers.

## Checkpoint

A decoder is one matrix and three philosophies: invert (`mode_match`),
re-pan (`allrad`), preserve energy (`epad`); gappy layouts favor
`allrad`, healthy ones favor inversion, `max_re` helps almost always,
and switching is a click-free message so your ears get the final vote.
Speakers handled — back to the other renderer, the one you carry in
your pocket. Binaural, properly, next.
