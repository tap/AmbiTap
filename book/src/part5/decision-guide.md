# The decision guide

Everything in this book converges here. You have a project; the field
has a dozen tools; the promise of a field guide is that these two facts
meet in minutes, not weeks. This chapter delivers the promise twice:
first as questions, then as worked scenarios.

## The four questions

Nearly every spatial audio decision resolves from four axes:

**1. What's the deliverable — a scene, feeds, or a platform master?**
If the venue varies or the listener steers: scene (Ambisonics). If one
known rig defines the piece: channel feeds. If a commercial platform
names the spec: that platform's paradigm, full stop (Chapter 22).

**2. Who's listening, on what?** One tracked head (VR/binaural), many
untracked heads on headphones (Chapter 13's caveats), one sweet seat
(XTC territory), or a room of wanderers (arrays; order and decoder
choices per Chapters 7 and 12)?

**3. Is the material discrete, diffuse, or both?** A few precise
foreground sources lean object-wise (`panbin~`, engine objects, Atmos
objects). Fields, rooms, and weather lean scene-wise. "Both" is the
professional's default answer, and the bed + foreground split (Chapters
9, 21, 22) is the standing solution.

**4. What are the budgets?** Channel count and CPU (order economics,
Chapter 7), latency (`room~`'s 53 ms, `xtc~`'s 512 samples),
licensing (open bus vs. platform), and the scarcest one — setup time in
the venue (Chapter 5's monitor-binaurally-decode-on-site discipline).

Answer four questions, and the scenarios below mostly write themselves.

## Worked scenarios

**Live electronics set (club/venue, rig varies per gig).**
Compose order-3 in Max; master bus per Chapter 15; monitor binaurally
in rehearsal. Per venue: `decode~` to the house layout — `allrad`,
`max_re 1` for whatever gappy array exists (Ch. 12) — with Chapter 5's
tape-measure hour. Keep a stereo decode on a fader as the soundcheck
insurance policy. *Why not objects/Atmos:* no renderer at the club;
the scene decodes anywhere.

**Gallery installation, speaker dome, three months unattended.**
Author in Max with the widgets; exhibit per Chapter 18: Pd headless on
a small computer, same objects, decoded to the dome; order 3–4 if the
dome is dense (Ch. 7's wandering-audience case for higher order).
LFE/subs via W-derived send (Ch. 17). *The trap to avoid:* composing
on the dome. Compose binaurally, calibrate on site.

**VR title / 360 video.**
Foreground as engine objects; world as ambisonic beds authored on
your Max production line and delivered as AmbiX WAV (order 1–3 per
platform); HMD rotation does the head tracking (Ch. 21). For plain
360 video: order-1 AmbiX + head-locked stereo, injected metadata
(Ch. 20). *Why not pure scene:* foreground sharpness; *why not pure
objects:* the world would cost per-raindrop.

**Binaural fiction / podcast drama (headphones, no tracking).**
Order-3 scene; `magls`; room per scene from Chapter 11 (the
externalization stack); featured close voices via `panbin~` (Ch. 9's
fork); print per Chapter 20. Favor motion and staging that survive
front/back ambiguity (Ch. 13). *Why not Atmos:* Dolby's binaural is a
platform render; here you *are* the renderer and can voice it.

**Commercial music release "in Atmos."**
The spec answers question 1: author beds + objects against Dolby's
renderer (Ch. 22). Your kit still earns its keep upstream — spatial
sketching, ambience design decoded into the bed, `bed2hoa~` for
monitoring object stems binaurally without renderer seats. Keep
stems + trajectories as the archival truth; print Atmos and scene
masters as siblings.

**Field recording / documentary sound.**
First-order mic per Chapter 19 (craft section verbatim); deliver
scene masters + binaural prints; `vmic~` to derive spot mics in post;
hybrid with encoded foreground when the story needs focus (Ch. 7's
recorded-bed pattern).

**Concert hall / acousmatic diffusion.**
The historical home turf. Scene-based composition; venue decodes
(often the festival provides one — ship AmbiX + README per Ch. 8 and
you are the easiest guest they've had); rehearse spatial gestures
with `rotate~`/`directional~` automation rather than fader-diffusion
alone. Order 3 travels; order 5 if the hall's rig genuinely resolves
it.

**A client demo next Tuesday.**
Chapter 3's patch, your material, twenty minutes, headphones across
the desk. Nothing in professional audio demos better per unit effort
— which is, after all, how this book got you here.

## When the answer is "not Ambisonics"

The guide is only trustworthy if this list is real. Reach elsewhere
when: the deliverable is a **named platform spec** (author in that
ecosystem); it's **stereo/mono content with stereo ambitions** (a
great stereo mix beats a reluctant spatial one); the piece is **a few
point sources, headphones-only, no future on speakers** (direct
binaural per source — even then your `panbin~` does it in-family);
the rig is **wildly irregular and the material channel-conceived**
(a 40-speaker sculpture where each speaker is a *voice* is
channel-based art — compose it as channels); or **latency is king**
(sub-5 ms monitoring paths shouldn't detour through convolution
renderers).

## The last checkpoint

Four questions — deliverable, listener, material, budgets — then the
scenario table; and the honest "elsewhere" list keeps the whole guide
credible. You came to this book not knowing what Ambisonics was.
You leave with a bus in your patch cords, instruments on it, renders
out of it, and — the actual goal — *judgment* about when it's the
right tool. The appendices hold the reference tables and the road
onward. Go make something three-dimensional.
