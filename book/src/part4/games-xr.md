# Games and XR

Interactive audio is where every thread of this book was already
braided together decades ago — object rendering, scene beds, binaural
output, head tracking — because games never had a choice: the listener
*steers*. This chapter maps that world's architecture onto your
vocabulary, so when you walk into a game-audio pipeline (or ship a VR
piece) you recognize everything. Names as of 2026.

## The standing architecture

Every serious engine pipeline converges on the same three-layer
design — which Chapter 9 already taught you in miniature:

1. **Foreground: objects.** Each emitting thing (footstep, voice,
   engine) is a mono source + position, rendered per frame relative
   to the listener — the object paradigm, panned to speakers or
   convolved per-source to binaural (`panbin~` was this, one object
   at a time). Sharp, interactive, per-source cost.
2. **Background: an ambisonic bed.** The forest, the city, the room
   tone — authored or recorded as B-format (Chapters 3–19!), carried
   as one bus, and **counter-rotated with the listener's head** each
   frame. One matrix multiply for the whole world's ambience —
   Chapter 4's economics, deployed at industrial scale. This is
   *the* reason VR standardized on Ambisonics.
3. **Output: binaural (or the room's speakers).** On headsets, both
   layers render through HRTFs; head tracking comes free from the
   HMD at far better than Chapter 13's 50 ms budget.

Middleware names for the same picture: **Wwise** ships ambisonic
busses (to 5th order) alongside its object pipeline; **FMOD** routes
ambisonic assets through spatializer plugins; engine-native audio in
**Unity**/**Unreal** accepts first-order AmbiX ambience assets and
delegates rendering to a pluggable spatializer. The spatializer SDKs
you'll meet by name: **Steam Audio** (Valve — notable for tracing
actual geometry to drive occlusion and reflections), **Meta XR
Audio** (the Quest lineage), and **Resonance Audio** (Google's
open-source engine — ambisonics-based internally, order 3, now
dormant but instructive and still deployed). Under every one of those
logos: encode, rotate, decode binaurally — your Part I patch, shipped
at 90 fps.

## What translates, and what's new

Carrying your skill set in:

- **Order economics** (Chapter 7) reappear as *performance budgets*:
  engine beds run orders 1–3 because the rotation and decode happen
  per frame on consumer hardware, next to the game. Your instinct
  for "what does order 1 blur sound like" is directly a shipping
  decision.
- **The bed/foreground split** (Chapter 9's fork) is now enforced by
  architecture: precious sources go objectward, diffuse world goes
  busward. You've already practiced the judgment.
- **Rooms** (Chapter 11) become *acoustics systems*: image-source
  thinking survives, but engines trace real level geometry (Steam
  Audio bakes or ray-traces reflection paths). `room~`'s mental
  model — direct/early/tail, geometry drives the earlies — is
  exactly the right preparation.
- **Authoring is where AmbiTap re-enters**: engines consume
  first-order (sometimes order-2/3) AmbiX WAV as ambience assets.
  Your Max rig — encode, room, mix, record the bus, truncate per
  Chapter 7 — is an ambience-asset production line. (And a Pd patch
  via libpd, Chapter 18, is a legitimate prototyping spatializer.)

Genuinely new, worth respecting: **occlusion and propagation**
(walls muffle, sound bends through doorways — driven by game
geometry, no Part III equivalent), **interactive mixing** (snapshots
and side-chains driven by gameplay state), and **voice budgets**
(the renderer is sharing a CPU with a *game*; everything is
priority-managed). Game audio is its own deep craft — this chapter's
claim is only that its spatial layer is your Part I–III knowledge
wearing a hard hat.

## Shipping a small VR piece — the shape of it

For the common near-term case — a 360 video or a modest headset
experience — the pipeline in five lines:

1. Compose the world as an order-3 scene in Max (everything you know).
2. Print: full scene for archive; order-1 truncation for the video
   platforms (Chapter 20's delivery table).
3. For real-time pieces: import the bed as AmbiX into
   Unity/Unreal + a spatializer SDK; author foreground as objects.
4. Let the HMD drive rotation (never hand-roll what the runtime
   provides).
5. Test on the actual headset early — Chapter 13's strangers'-heads
   caveats apply to every player, and loudness targets differ from
   music norms.

## Checkpoint

Games render foreground as objects and world as head-tracked
ambisonic beds through binaural output; the middleware names change,
the architecture doesn't, and your order/bed/room instincts are the
transferable core. One name has been conspicuously deferred through
this whole part — the one on every client brief. Atmos, next.
