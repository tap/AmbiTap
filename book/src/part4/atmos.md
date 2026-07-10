# Atmos and friends

Somebody is going to say it in a meeting: *"can we get this in
Atmos?"* This chapter equips you to answer — what Dolby Atmos actually
is, how it relates to the scenes you now build fluently, when each
paradigm genuinely wins, and the workflows where they cooperate. Tone
check before we start: this book is not against Atmos. It is against
fog. (Specifics as of 2026.)

## What Atmos actually is

Dolby Atmos is the **object-based** paradigm from Chapter 2, shipped
at industrial scale with a licensing program attached:

- A mix is **beds + objects**: channel-based base layers (typically
  7.1.2-shaped) plus up to ~118 mono/stereo objects, each with
  positional metadata authored against a standardized renderer.
- **The renderer is Dolby's.** At playback — cinema processor, AVR,
  soundbar, phone — Dolby's renderer lays objects onto whatever
  speakers exist, or into its binaural mode for headphones. Authoring
  happens through the Dolby Atmos Renderer application (or
  DAW-integrated equivalents in Pro Tools/Nuendo/Logic; Apple's
  ecosystem wraps the same delivery as "Spatial Audio").
- **The deliverable is a master file** (ADM BWF / IAB families), and
  distribution runs through platforms licensed to decode it.

Read that against Chapter 2's table and the trade is familiar:
maximum per-object discreteness and a consumer-delivery pipeline that
*works at retail scale*, in exchange for spatial decisions finalized
by a renderer you don't control, on speaker layouts you'll never see.

## Scene versus object, without the marketing

Where each paradigm genuinely wins — the honest scorecard your
meeting needs:

**Atmos wins** consumer delivery (the only spatial format with
end-to-end reach into living rooms, cars, and earbuds), discrete
foreground precision on dense speaker rigs, industry interop (dubbing
stages, streaming specs, loudness pipelines), and any brief where the
deliverable *is* "an Atmos master" — by definition.

**Ambisonics wins** rotatability (nothing head-tracks a finished
Atmos mix outside Dolby's own binaural path; a scene rotates for one
matrix — Chapters 4, 21), recordability (there is no Atmos
microphone; there are ambisonic ones — Chapter 19), diffuse material
(Atmos objects are points; a scene *is* a field), venue independence
outside the licensed ecosystem (your dome, your gallery, your
festival decode — Chapter 12), archival transparency (a math-defined
open bus versus a renderer-defined proprietary master), and cost
(zero licensing, this entire toolkit).

Neither list embarrasses the other. They're different answers to
Chapter 2's question, optimized for different economies.

## Cooperation workflows

The practical part: the two paradigms meet constantly, and the
crossings are well-trodden.

**Scene → Atmos** (the common direction: you composed spatially, the
label wants Atmos):

1. **Decode into the bed**: render your scene to 7.1.4 (Chapter 12 —
   `allrad`, since that's a gappy layout) and deliver it as the
   Atmos *bed*. The scene's spatial character survives at the bed's
   resolution. This is the standard route for ambience/texture-heavy
   material.
2. **Re-author the foreground as objects**: your handful of featured
   sources — whose positions you have as automation already — become
   Atmos objects with the same trajectories, riding above the
   decoded bed. Bed-for-world, objects-for-story: the game-audio
   split (Chapter 21), performed one final time at delivery.
3. What *doesn't* exist: a lossless "HOA in, Atmos out" button. The
   paradigm translation is real work; budget it.

**Atmos-adjacent material → scene**: a 7.1.4 print folds into your
bus via `bed2hoa~` (Chapter 17, built for this) for monitoring,
re-staging, or archival — with that chapter's honesty about baked
panning. Object *stems* (dry source + position notes) port even
better: re-encode them as Chapter 9 sources and the piece is native
scene again.

**Both-masters projects**: keep the mix in stems + position data as
long as possible (source-and-trajectory is the truly portable
representation — more portable than either delivery format); print
the scene master and the Atmos master as two *renders* of the same
decisions. Studios doing regular spatial work converge on exactly
this.

## The other friends

Same paradigm-mapping, quickly: **MPEG-H** — the broadcast-world
object/scene hybrid (notably: it can carry HOA natively — the one
mainstream delivery family where your scene ships *as itself*;
adoption is strongest in broadcast and parts of streaming). **AURO
3D** — channel-based height, a tall Chapter 2 column A. **IAMF**
(AOM's open "Eclipsa" format, backed by Google/Samsung) — a young,
royalty-free object/scene container to watch; ambisonic payloads are
in its spec. If any of these is on your brief, the Chapter 2 table
plus this chapter's scorecard method answers it.

## Checkpoint

Atmos = beds + objects + Dolby's renderer + licensed delivery: the
object paradigm productized. It beats scenes at retail reach and
discrete precision; scenes beat it at rotation, recording, diffusion,
open venues, and price. They cooperate via decode-to-bed,
objects-for-foreground, and `bed2hoa~` back — and "can we get this
in Atmos?" now has a costed, honest answer.

That completes the wider world. Every paradigm, toolchain, and
delivery route is now on your map — which means it's time for the
chapter this book was named for: given a *situation*, which tool?
