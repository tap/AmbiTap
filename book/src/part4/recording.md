# Recording a scene

Everything so far *synthesized* scenes. But Chapter 2 flagged the
scene-based paradigm's exclusive: it's the one you can point a
microphone at. A single instrument on a stand captures the actual
soundfield of a forest, a cathedral, a protest, a rehearsal — behind,
above, all of it — as a bus your whole toolkit already speaks.

This chapter is the field guide's field chapter: how the microphones
work, what the market looks like, and the craft that makes location
B-format worth keeping. (Ecosystem facts here are stated as of 2026;
models drift, principles don't.)

## A-format and B-format

Chapter 6's ideal — an omni and three figure-8s occupying *one point* —
cannot be built; microphones have bodies. The 1970s solution (Gerzon
and Craven's, still the standard design) is the next best thing:
**four cardioid capsules on the faces of a tiny tetrahedron**, as
close to coincident as manufacturing allows.

The raw four-capsule output is called **A-format** — four aimed
cardioids, useless directly. But within the coincidence approximation,
sums and differences of the four reconstruct the ideal set exactly:
all four capsules summed ≈ the omni W; front pair minus back pair ≈
the X figure-8; and likewise Y and Z. That matrixing (plus filters
correcting the "tiny but not zero" spacing) is the **A-to-B
conversion**, and it ships with every mic as a plugin or is done
on-board. The output is **B-format** — and after Part II you know
precisely what that means, down to asking the professional's three
questions (order? channel order? normalization?). Modern mics emit
AmbiX; vintage material and the classic SoundField lineage speak FuMa;
`format~` (Chapter 8) stands at the border.

Higher orders need more capsules — the (N+1)² economics again, now in
hardware: order 2 wants ~8+ capsules, order 4 wants ~32 arranged on a
rigid sphere, and the conversion mathematics gets correspondingly
heavier. This is Chapter 7's "microphones lag encoders," explained.

## The market, in tiers (2026)

- **Recorder-integrated, first order.** The Zoom H3-VR class: four
  capsules, A-to-B conversion, level-metering and an SD card in one
  handheld box. The field-recording workhorse tier — point at the
  world, get AmbiX WAV.
- **Studio-grade first order.** The Sennheiser AMBEO VR, Rode NT-SF1,
  and the SoundField heritage line (now under RØDE/Freedman): better
  capsules, XLR outputs, conversion in a plugin where you can choose
  the output convention. The tier for material that will be mixed
  hard.
- **Higher-order arrays.** Spherical rigid arrays: Zylia's ZM-1 class
  (order ~3 from 19 capsules) and the research-and-broadcast Eigenmike
  em32/em64 (orders 4–6). Costs jump an order of magnitude; so does
  the post-processing. Rent before buying; verify your whole chain
  handles the channel counts.
- **Adjacent but not ambisonic:** binaural head rigs record two-channel
  ear signals (lovely, but baked — Chapter 13's format, not a scene),
  and spaced multichannel trees capture enveloping *channel-based*
  material (fold it in with `bed2hoa~` if you must, remembering what
  it is).

## Field craft

The handful of practices that separate keeper B-format from expensive
regret:

- **Orientation is sacred.** The mic's marked front is your scene's
  0° forever after. Photograph the setup; note "front = stage" in
  the slate. A mounted-sideways surprise costs an afternoon of
  `rotate~` forensics (and an upside-down one is why `flip_ud`
  exists — Chapter 15's rescue list, written from experience).
- **The mic is the listener's head.** Where you stand it is where
  every future listener stands. Height matters (ear height reads
  natural; 3 m up reads *crane shot*). Proximity matters doubly:
  nearby sources dominate a coincident array fast — the "step back"
  your recording teacher preached, now in 360°.
- **Wind and handling travel in W.** Standard protections (blimps,
  suspensions) apply — fourfold. Any thump is omnipresent by
  definition.
- **Monitor binaurally on location**: mic → `binaural~ 1` is a
  two-object patch (or the recorder's own binaural fold-down), and
  it catches the orientation and proximity mistakes while they're
  still fixable.
- **Slate the paperwork**: order, convention (the recorder's manual
  knows), sample rate, and orientation notes, in a text file beside
  the WAVs. Chapter 8 predicted this file; be the collaborator who
  ships it.

## Recorded scenes in the toolkit

Back home, a recorded scene is just a bus with weather in it — every
Part III tool applies. The idioms that come up constantly:

- **Re-aim in post**: `rotate~` turns the whole location; the take
  where the action drifted 40° left is salvageable.
- **Interview the field**: `vmic~` beams extract usable mono spot
  "mics" from a scene recorded with none (order-limited, but
  astonishing the first time).
- **Recorded bed + synthetic foreground** — the hybrid Chapter 7
  recommended: first-order forest as the world, order-3 encoded
  sources as the story. Sum the buses (pad the first-order scene's
  4 channels into the order-3 cord) and the seam is inaudible.
- **Stabilize handheld moves** the same way VR does: record the mic's
  orientation (IMU or careful notes), counter-rotate in post —
  Chapter 4's head-tracking machinery, pointed backwards in time.

## Checkpoint

Tetrahedra of cardioids → A-format → matrixed to B-format; first order
is a handheld commodity, order 4+ is a spherical-array specialty;
orientation, placement, and paperwork are the craft; and a recording
is just a bus you didn't have to patch. One question remains before
the DAW chapter poses it everywhere: your scenes — synthetic or
recorded — eventually have to *ship*. Onward to the toolchains the
rest of the industry mixes in.
