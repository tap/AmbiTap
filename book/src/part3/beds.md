# Beds and stems

The last craft chapter faces the direction we've ignored since Chapter 2:
backwards, toward the channel-based world. Your collaborators live there.
Sessions arrive as 5.1 stems; sample libraries ship quad ambiences; the
sound designer's handoff is a 7.1.4 print. None of it is ambisonic, all
of it is useful, and one object annexes it.

Companion patch: **`patchers/booklet/12-beds.maxpat`**.

## The move

A channel-based **bed** is speaker feeds for a known layout — which
means every channel has a *defined direction* (L = +30°, RS = −110°,
…). So encode each channel at its canonical direction and sum:

```text
[your 5-channel stem]              (one mc cord, L R C LS RS order)
    |
[ambitap.bed2hoa~ 3 surround_5_1]
    |
   bus — an ordinary ambisonic scene now
```

`ambitap.bed2hoa~` takes the same `<order> <layout>` arguments and the
same layout list as `decode~` — it is, conceptually, that object's
mirror image (decode: scene → feeds; bed2hoa: feeds → scene). The
matrix is static (directions don't move), so it's among the cheapest
objects in the package. Once through it, the material *is* scene:
rotatable, mirrorable, re-decodable to any rig, binauralizable —
the whole of Part III applies.

## What the move is worth

Concretely, with the 5.1 stem session that just landed:

- **Monitor it on headphones, properly.** `bed2hoa~ 3 surround_5_1` →
  `binaural~ 3` is an instant, correct virtual 5.1 room — each stem
  channel rendered from its true angle. For anyone who's mixed
  surround on a laptop, this alone justifies the object.
- **Re-deliver it anywhere.** The 5.1 print, through the scene, decodes
  to your quad rig, an octagon, or a dome (Chapter 12 rules apply —
  `allrad` for the gappy targets). This is up/down/cross-mixing done
  by geometry instead of by folklore fold-down coefficients.
- **Use it as a bed** in the Atmos sense: channel-based ambience layer
  underneath object-like foreground — here, encoded foreground
  sources (Chapter 9) summed onto the *same bus* as the imported bed.
  One cord, both paradigms, everything rotates together.
- **Rescue legacy work.** Quad tape-music realizations, 5.1-era
  installations — re-staged into scenes, they stop being hostage to
  one speaker count.

And the disclaimer, since this object is where wishful thinking
congregates: **this is not upmixing magic.** A bed encodes as five (or
eleven) *virtual speakers* on the sphere — phantom images between them
stay phantom (a source panned L↔C in the original is two correlated
virtual speakers, not a true 15° source), and the scene inherits the
bed's spatial resolution forever. `bed2hoa~` relocates a channel-based
mix *faithfully*; it cannot un-bake the panning decisions inside it.

## The LFE, finally

Every layout in this package has been "no LFE" since Chapter 5, with
promises. Here's the whole policy in one place.

The **LFE** channel (the ".1") is a *delivery* convention — a separate
low-frequency effects track for a subwoofer — not a direction. An
ambisonic scene describes directional sound, and sub-bass direction is
barely perceptible (which is why one subwoofer suffices in the first
place). So the bus simply does not carry it, and the routing is:

- **Importing** a 5.1-with-LFE stem: peel channel 4 (the LFE) off
  *before* `bed2hoa~` — `mc.unpack~`, feed the five mains to the
  object, and route the LFE directly to your subwoofer output (or, on
  a sub-less monitor rig, mix it into the render at −∞ to −10 dB per
  taste; it's effects seasoning by definition).
- **Delivering** to an LFE-expecting format: decode the scene to the
  mains (Chapter 12), and derive the LFE as a low-passed mono sum (W,
  low-passed, is the classical choice) — or better, keep true LFE-type
  material (the explosion sweetener) on its own mono track through the
  whole project, outside the bus, and print it directly.
- **Bass management** (rerouting mains' low end to the sub) is the
  monitor controller's job or a crossover after the decoder — playback
  plumbing, not scene content. Keep it downstream and out of the
  master.

The companion patch builds a synthetic 5.1 bed (five distinct sources
`mc.combine`d in canonical order — front trio, rear pair), folds it in,
and renders binaurally, with the Chapter 14 heatmap showing five tidy
blobs at the canonical angles: the bed, visibly staged on the sphere.
Rotate it — five blobs turn as one — and the point of the whole
exercise lands.

## Checkpoint — and the end of the craft

Beds fold in by encoding each channel at its canonical angle
(`bed2hoa~`, `decode~`'s mirror); the result is honest scene material
with the bed's resolution baked in; the LFE routes *around* the bus in
both directions.

Take stock: sources, distance, rooms, decoding, binaural, instruments,
scene-mixing, XTC, beds. That is the complete AmbiTap toolkit and — by
this book's argument — a complete spatial audio education in one
package. Part IV takes exactly this skill set and shows you the same
ideas wearing other uniforms: Pd, microphones, DAWs, game engines, and
the A-word.
