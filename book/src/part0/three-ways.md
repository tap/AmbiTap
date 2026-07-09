# Three ways to put sound in space

Chapter 1 ended on a question: how do you *store* a soundfield? Not "how do
you play one back" — store one, mix one, send one to a stranger whose
playback system you know nothing about?

Every spatial audio format ever shipped answers that question in one of
three ways. Learn the three answers and the entire landscape — Atmos,
AmbiX, 7.1.4, VBAP, binaural stems, game-engine audio — snaps into a tidy
map. This chapter is that map. It contains no patches and no math; it is
the most important chapter in the book.

## Answer 1: store the speaker feeds (channel-based)

The oldest answer: decide the loudspeaker layout *first*, then store one
audio channel per loudspeaker. Stereo works this way. So do 5.1, 7.1, and
7.1.4: the ".1" file layouts where each channel is, by definition, *what
comes out of that speaker*.

The mix **is** the render. When you pan a sound "half into the left
surround," you are writing signal onto the left-surround channel, and that
decision is baked at mix time, forever.

- **Strengths.** Dead simple. Zero playback intelligence needed — wire
  channel 3 to the center speaker and you're done. Decades of tooling,
  room standards (ITU-R BS.775), and engineering craft. Total artistic
  control over exactly what each speaker emits.
- **Costs.** The mix assumes *that* layout. Played on anything else, it
  must be re-rendered by ad-hoc up/downmix rules ("fold the surrounds into
  the fronts at −3 dB…"), which are lossy and nobody's favorite. There is
  no listener rotation — the sound is nailed to the speakers. And channel
  count scales linearly with spatial resolution: wanting height means
  shipping four more channels.

**Names you'll hear:** stereo, quad, 5.1, 7.1, 7.1.4, 22.2, "beds" (a term
from Atmos workflows for a channel-based base layer — Chapter 17 folds
these into ambisonic scenes with `ambitap.bed2hoa~`).

## Answer 2: store the sounds and where they go (object-based)

The second answer refuses to commit to speakers at all. Store each sound as
a mono (or stereo) *object* — the dry audio — plus a metadata track:
"at t=12.3 s, this object is at azimuth 40°, elevation 10°, two meters
out." At playback time, a *renderer* reads the metadata and computes, for
whatever loudspeakers or headphones actually exist, how to place each
object there.

This is how **Dolby Atmos** delivers height without shipping a channel per
speaker, and it is how every **game engine** has worked since the 1990s: a
game can't know your speaker setup, and its sounds move because the world
moves, so rendering *must* happen at playback, per listener, per frame.

- **Strengths.** Layout-independent by construction — the same master plays
  on a soundbar, 7.1.4, or headphones, each rendered natively. Individual
  sources stay discrete and editable to the end: a renderer can place one
  helicopter with pinpoint precision on whatever speakers are closest.
  Interactivity falls out for free — move the metadata, the sound moves.
- **Costs.** The renderer is a black box you don't control; your mix's
  spatial character is partly *its* aesthetic decision (ask anyone who has
  compared the same Atmos master across renderers). Complexity lives in the
  pipeline: authoring tools, metadata formats, licensing. Object count is a
  budget — each one is a live audio stream plus math at playback. And a
  *diffuse* thing (rain, a crowd, reverb everywhere) is an awkward fit for
  a format whose atom is "a sound at a point."

**Names you'll hear:** Dolby Atmos, MPEG-H, ADM/BW64, game-engine "emitters,"
VBAP (vector-base amplitude panning — the workhorse algorithm object
renderers use to place a point source on the nearest speakers; AmbiTap's
decoder uses it internally, and Chapter 12 will point at it).

## Answer 3: store the field itself (scene-based — this is Ambisonics)

The third answer is the strange one, and the one this book is about.

Don't store speaker feeds. Don't store a list of sources either. Instead,
describe **the acoustic field at one point in space** — the point where the
listener's head goes — as a set of signals that together capture *sound
arriving from every direction at once*. Not "channel = speaker," not
"track = source," but "channel = a component of the directional field,"
the way an image file's channels are color components rather than a list
of the objects photographed.

That set of signals is called **B-format**, and the scheme is
**Ambisonics** (Michael Gerzon and colleagues, early 1970s). The first
channel (called **W**) is what an omnidirectional microphone at the
listening point would hear — sound from everywhere, no direction. The next
three (**Y, Z, X**) are what three figure-8 microphones at the *same point*
would hear, aimed left–right, up–down, and front–back. Four channels, and
the full sphere — behind, above, below — is already represented. That's
**first-order** Ambisonics. Want sharper directional detail? Add channels
that capture finer directional patterns: 9 channels for second order, 16
for third, 25 for fourth — **higher-order Ambisonics (HOA)**, and the blur
shrinks with each step. (Part II makes this precise; for now, "more
channels = sharper picture" is exactly the right intuition.)

The consequences are the point:

- **The scene is finished, yet the speakers are not chosen.** A *decoder*
  — a small matrix, computed once for your actual layout — turns the same
  B-format scene into feeds for a quad rig, a 30-speaker dome, stereo, or
  (via virtual speakers and HRTFs) headphones. Mix once, decode anywhere.
- **The whole scene rotates for the cost of a matrix.** Because the
  channels form a mathematically tidy directional basis, rotating
  *everything you hear* — a hundred sources, the reverb, the recorded
  crowd — is one small matrix multiply, identical in cost for one source
  or a thousand. This is why VR standardized on Ambisonics for ambience:
  head tracking must rotate the world sixty times a second, cheaply.
  (Chapter 4 puts this under your fingers.)
- **Diffuse and discrete coexist.** A field description doesn't care
  whether the energy came from one trumpet or rain on a roof. The awkward
  case for object formats is the natural case here.
- **It's how you *record* space.** An ambisonic microphone (Chapter 19)
  captures B-format directly. There is no such thing as an "object-based
  microphone."
- **The costs are real too.** Sources blur together into the field — you
  can't reach in afterward and grab one trumpet the way an object format
  can (Chapter 15's `vmic~` gets you partway, at the field's resolution).
  Sharpness costs channel count quadratically. And low orders are
  genuinely soft-focus: first-order sounds *spacious*, not *pinpoint*.

**Names you'll hear:** Ambisonics, B-format, HOA, AmbiX and FuMa (two
conventions for channel order/scaling — the "paperwork" of Chapter 8),
ACN/SN3D (the AmbiX conventions AmbiTap uses throughout), "360 audio"
(YouTube's spatial audio is first-order AmbiX).

## One table

| | **Channel-based** | **Object-based** | **Scene-based (Ambisonics)** |
|---|---|---|---|
| What travels | one signal per speaker | dry sources + position metadata | directional field components |
| Spatial decisions made | at mix time | at playback (renderer) | at mix time (scene), decoded at playback |
| Plays on other layouts | via lossy up/downmix | natively, per renderer | natively, via decoder matrix |
| Rotate the whole scene | no | re-render every object | one matrix multiply |
| Discrete source precision | high (on the reference layout) | highest | limited by order |
| Diffuse material (rain, reverb, crowds) | fine | awkward | natural |
| Capture with a microphone | per-layout arrays | — | directly (ambisonic mics) |
| Cost axis | channels = speakers | objects × renderer math | channels = (order+1)² |
| Flag-bearers | 5.1 / 7.1.4 | Atmos, MPEG-H, game engines | AmbiX, VR/360, research & art |

A photography analogy that will carry us surprisingly far: channel-based is
a **print**, sized for one wall; object-based is the **layered project
file**, re-composited for every screen; scene-based is a **panoramic
negative** — everything that arrived at the lens, developable for any
display, but you can no longer un-photograph one pedestrian from the crowd.

## So when is Ambisonics the right tool?

The full decision guide is Part V, after you've actually used everything.
But the shape of the answer fits in four lines, and you should carry it
from the start:

**Reach for Ambisonics when** the *scene* is the deliverable: immersive
recording, VR/360 ambience, head-tracked anything, music and installations
for speaker arrays that vary venue to venue, diffuse and enveloping
material, or whenever "mix once, decode anywhere" describes your problem.

**Reach for something else when** the *speakers* are the deliverable (a
club PA, a fixed cinema stem — channel-based), when a *commercial platform*
is the deliverable (a Dolby Atmos release — object-based, because the spec
says so), or when you have a *handful of point sources on headphones only*
and maximum sharpness matters — direct binaural panning of each source
(Chapter 9's `panbin~`) beats routing them through a blurring field.

Mixed answers are common and respectable: game engines routinely render
foreground objects directly and carry the ambience bed in Ambisonics.

Enough cartography. You now know what Ambisonics *is* — a stored soundfield
— and roughly when to want it. Time to hear one. Headphones on; the next
chapter is a patch.
