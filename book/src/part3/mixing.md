# Mixing inside the scene

A stereo engineer reaches into a mix constantly: solo this, duck that,
flip the guitars, glue the bus. Chapter 2 warned that a scene-based
format resists reaching in — the sources have already dissolved into the
field. True, and yet the field itself can be operated on *by direction*,
which turns out to cover most of what a mixer actually wants. Four
objects, four moves.

Companion patch: **`patchers/booklet/10-mixing.maxpat`**.

## Solo a direction: `ambitap.vmic~`

A **virtual microphone**: aim it into the scene (`azimuth`,
`elevation`) and it extracts a mono signal of what's sounding from
there — the max-rE beam from Chapter 3's figure, pointed by you
(`max_re 1` for the clean pattern; off for the sharper, lobier one).

Uses, in ascending sneakiness: **solo-in-place** for the ear ("what *is*
that at 140°?" — aim, listen, know); **stems from a finished scene**
(aim four vmics at a recorded soundfield and you've derived a quad of
spot mics that never existed); **sidechain taps** (the beam's output can
key a compressor — duck the music when the narrator's *direction*
speaks). Its resolution is the scene's order — at order 3 the beam is
~75° wide, a section mic, not a lavalier. It pairs naturally with the
heatmap: find the blob on the map, aim the beam, listen.

## Turn a direction up (or down): `ambitap.directional~`

The complement: instead of extracting a direction, **reweight** it, in
place, on the bus. `azimuth`/`elevation` aim it, `gain` sets what
happens there — below 1 to duck a region, above 1 to feature it — and
the rest of the sphere passes untouched (MC in, MC out; the transition
region is as wide as the order allows, so think broad tonal shaping of
the sphere, not surgical notches).

This is the scene's version of a mixer's most common move. The audience
side of the room is too loud in the installation recording? Duck 30°
of it 4 dB and leave the piece alone. The soloist needs 2 dB at
stage-left? Feature the direction, not the stem you no longer have.

## Flip the stage: `ambitap.mirror~`

Three toggles — `flip_lr`, `flip_fb`, `flip_ud` — each reflecting the
entire scene across a plane, instantly and losslessly (sign flips on
the appropriate channels; nothing is re-rendered, nothing blurs).

The workhorse is `flip_lr`: the *mix-translation* of a stereo
engineer's L/R swap. Check whether your staging survives mirroring
(balanced mixes mostly should); adapt a piece to a venue whose
geometry argues with your left-right choices; A/B suspicion about your
own ear's bias ("is the mix left-heavy or am I?" — flip it; if it
sounds *right*-heavy now, it was you). `flip_fb` earns its keep fixing
front/back-inverted recordings (a mic mounted backwards — it happens
more than anyone admits) and `flip_ud` fixes an inverted mic mount in
one click.

## Glue without smear: `ambitap.compress~`

Compressing a scene channel-by-channel with sixteen ordinary
compressors would be a disaster: each channel's gain would pump
independently, and since *direction* lives in the gain ratios between
channels (Chapter 6), independent pumping literally modulates where
things are — sources lurch toward wherever the compression bites least.

`ambitap.compress~` is built around the fix: it meters **W only** — the
omni channel, the scene's honest mono level — computes *one* gain
signal, and applies that same gain to every channel. Loudness breathes;
geometry is mathematically untouched (the channel ratios are preserved
exactly, which is why it's called image-preserving). The controls are
the familiar five — `threshold`, `ratio`, `attack`, `release`,
`makeup_gain` — behaving like every compressor you've ever set, just
aimed at a sphere. (Its static curve and attack/release clocks are
measured, not vibes — the library's behavior notebook plots them.)

Use it where you'd use bus compression in stereo: gluing a full scene,
taming a live input's swings before the encoder chain, limiting an
installation's output. What it deliberately *cannot* do is
multiband/multi-*direction* compression — squashing only the loud half
of the room — because that would be `directional~`'s aim with a
detector, and yes: `vmic~` (detector) driving `directional~` (gain) is
exactly how you'd patch that, all three objects consenting adults on
one bus.

## The order of operations

A scene channel-strip, assembled from the four plus earlier chapters —
a sensible master-bus order when you need everything at once:

```text
sources & rooms → [mirror~] → [directional~ …] → [compress~] → [rotate~] → renderer
     (create)      (fix)         (reweight)         (glue)      (stage)
```

Corrections before creative reweighting; dynamics after the tonal
balance they should respond to; rotation last so everything upstream is
defined in *scene* coordinates, not performance coordinates. `vmic~`
hangs off the bus wherever you need an ear or a key signal — it's a
listener, not a link in the chain.

The companion patch builds a three-source scene and puts all four
objects on switches, with the Chapter 14 instruments watching. The
five-minute lesson: flip `flip_lr` while watching the heatmap (the map
mirrors, the compass's y negates — geometry moved, nothing else);
then drive the compressor hard and watch the map *not* move while the
level breathes.

## Checkpoint

Reaching into a scene means operating by direction: extract one
(`vmic~`), reweight one (`directional~`), reflect them all (`mirror~`),
and control dynamics through one W-keyed gain so the image never
smears (`compress~`). Your mixes are now maintainable, not just
buildable. Two special-purpose renderers remain before the wider world
— first, the strange art of making two loudspeakers whisper binaural
signals into your ears.
