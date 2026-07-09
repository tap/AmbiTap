# Turning your head

Chapter 1 left a debt unpaid. Time, level, and spectral cues still leave
ambiguities — front/back flips, vague elevation — and real ears settle them
by *moving*: turn your head three degrees and the way the cues change gives
the answer away. Chapter 3's orbiting noise couldn't offer you that;
however you turned, the scene turned with you, glued to your skull.

This chapter unglues it. Along the way you'll meet the operation that, more
than any other, is *why Ambisonics exists*.

## One new box

Open **`patchers/booklet/02-turning-your-head.maxpat`**, or splice one
object into Chapter 3's patch:

```text
[pink~]
   |
[ambitap.encode~ 3]
   |
[ambitap.rotate~ 3]        ← NEW: rotates the entire scene
   |
[ambitap.binaural~ 3]
   |    \
[dac~ 1 2]
```

`ambitap.rotate~` takes `yaw`, `pitch`, and `roll` messages — radians
again, so keep the degrees→radians `expr` idiom from Chapter 3 on a dial.

Park the encoder's source dead ahead (`azimuth 0`). Now sweep the
rotator's **yaw**: positive yaw swings the whole scene to your **left**
(counterclockwise from above, same handedness as azimuth). The source
orbits exactly as if you'd swept the encoder's azimuth — with one source,
you can't tell the difference. So add a second source, because the
difference is the entire point:

```text
[pink~]                [cycle~ 220]
   |                      |
[ambitap.encode~ 3]    [*~ 0.2]
   |                      |
   |                   [ambitap.encode~ 3]   ← azimuth 3.14159 (behind)
   |                      |
   +----------+-----------+
              |
      [ambitap.rotate~ 3]
              |
      [ambitap.binaural~ 3]
```

Two encoders, their mc cables joined into the rotator's inlet — patch
cords sum, and summing scenes *is* mixing in ambisonics; the bus doesn't
care how many sources wrote to it. Noise ahead, a quiet tone behind. Sweep
yaw: **both** move together, rigidly, keeping their 180° separation — the
world turns, not a source. Pitch and roll complete the set: `pitch` tips
the front of the world down (positive) or up, `roll` tilts it around your
nose axis.

![Yaw, pitch, and roll: the three rotation axes and their positive
directions](../img/axes.svg)

> **Order of operations (for the curious).** The three angles compose in a
> fixed order — yaw first, then pitch, then roll — as intrinsic Z-Y′-X″
> Euler rotations. It only matters when you use two or more at once:
> yaw-then-pitch and pitch-then-yaw end up in different places. If you ever
> port a head-tracker driver, this ordering (and the right-hand rule) is
> the whole game; the library pins it down in `docs/CONCEPTS.md`.

## Why this box justifies the format

Think about what just happened, in the terms of Chapter 2's three
paradigms.

**Channel-based:** rotating "the mix" is meaningless — the sound is bolted
to the speakers. **Object-based:** rotating the world means visiting every
object, every frame, and re-rendering each one; the cost scales with the
source count. **Scene-based:** the rotator never saw your sources. It saw
sixteen channels, multiplied them by one 16×16 matrix, and *everything in
the field* — two sources, or two hundred, or a recorded rainstorm with a
crowd in it — turned rigidly together. Same cost, always.

That trick is not a bonus feature of the format; it *is* the format. The
channels of an ambisonic scene are components in a directional basis
chosen precisely so that rotation is a small, exact, cheap matrix. This is
why, when VR needed head-tracked ambience at 60+ updates a second on a
phone strapped to a face, the industry reached past its beloved object
formats and standardized on Ambisonics for the job.

And it's why the box you just patched is the heart of every VR audio
pipeline you've ever heard: **head tracking is just yaw/pitch/roll,
counter-rotated.** If your head turns 30° left, rotate the scene 30° right
and the mountain stays put. Any source of orientation data — a phone's
gyroscope via OSC, a VR headset, a webcam head-tracker, an IMU taped to
your headphones — can drive those three messages. (Sweep the dial smoothly
and notice there's no zipper noise: the rotation matrix crossfades in over
a few milliseconds, a courtesy you'll learn to expect from every object in
the package.)

## Two rotators, hiding in plain sight

Here is a subtlety that bites everyone once, so let's get bitten in a
controlled setting. `ambitap.binaural~` *also* accepts `yaw`, `pitch`, and
`roll`. Same words — different meaning:

- **`rotate~` yaw turns the world.** Positive: the scene swings left past
  your face.
- **`binaural~` yaw turns *your head*.** Positive: your virtual head turns
  left — so a front source now lands at your **right** ear.

Same axis, opposite visible effect, and both conventions are correct for
what they name. Prove they're inverses with the patch: set `rotate~` yaw
and `binaural~` yaw to the same value — any value — and the sources snap
back to where they started. World turns left, head turns left with it,
nothing moves relative to your ears.

In practice the division of labor is: **scene rotation is composition**
(turning the stage, spinning an ambience, choreography — `rotate~`, mixed
*into* the scene, heard by every listener) and **head rotation is
monitoring** (tracking one listener's head — `binaural~`'s own attributes,
applied at the very end, personal to that render). Part III returns to
this when head tracking gets real hardware attached.

## Checkpoint

You can now: rotate an entire scene with one matrix, mix multiple encoded
sources onto one bus by joining patch cords, tell world-rotation from
head-rotation and cancel one with the other, and explain to a skeptical
friend why VR audio runs on Ambisonics. Your scenes still live entirely in
headphones, though. Time to put them in a room: loudspeakers next.
