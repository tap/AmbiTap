# Introduction

> Space is the place.
>
> — Sun Ra

Close your eyes anywhere — a kitchen, a train platform, a forest — and you
still know where everything is. The kettle is behind you and to the left. The
announcement echoes from above. Somebody's footsteps approach from the right
and pass behind your head. You perform this feat constantly, effortlessly,
with two ears and no visible antennas, and almost every recording you have
ever made throws that entire dimension away and hands your listener a flat
pane of glass: left, right, and a line between them.

This book is about getting the dimension back. It is a field guide to
**spatial audio** — the family of techniques for capturing, composing, and
delivering sound that has direction and space — written for people who make
sound and have never touched any of it. Its center of gravity is
**Ambisonics**: a sixty-year-old idea, born in British hi-fi research and
resurrected by VR, that has quietly become the common language of
full-sphere audio. If you have seen the word and nodded past it, or been
told "just use fourth order, obviously" by someone who didn't explain, this
book is for you.

## Who this is for

You are comfortable in Max — you can build a patch, you know what `dac~`
does, `mc.` cables don't scare you (and if they do, they will stop within a
chapter). You own a pair of headphones. That is the whole entry requirement.

You do **not** need: a loudspeaker array, an ambisonic microphone, a degree
involving spherical trigonometry, or any C++. There is real mathematics
underneath this field, and the book will always tell you where it lives, but
the main text works in pictures, listening experiments, and patches. The
formulas appear in clearly marked *for the curious* sidebars you can skip
without losing the thread, and the appendix points to the open-access
literature when you want the full derivations.

## What a "field guide" means

Spatial audio is a territory, not a technique. It contains at least three
competing paradigms (channel-based, object-based, scene-based), a zoo of
formats (5.1, 7.1.4, Atmos, AmbiX, FuMa, binaural…), and toolchains that
span DAWs, visual programming, game engines, and dedicated hardware. People
get lost here not because any one idea is hard but because nobody handed
them a map.

So this book behaves like a field guide: it teaches you to *identify* what
you're looking at, and it is opinionated about *which tool to reach for in
which situation* — including the situations where the honest answer is "not
Ambisonics." Part V distills that judgment into an explicit decision guide;
everything before it earns the distillation.

## The playground

You learn a territory by walking it. Our vehicle is
[**AmbiTap**](https://github.com/tap/AmbiTap), an open-source
higher-order-ambisonics library, through its
[**Max package**](https://github.com/tap/AmbiTap-Max): seventeen `ambitap.*~`
objects that encode, rotate, decode, binauralize, add distance and rooms,
and let you *watch* a soundfield while you listen to it. Every hands-on
chapter is built around a patch you can open and hear in minutes.

The vehicle is not the territory, though. The same objects exist for
[**Pure Data**](https://github.com/tap/AmbiTap-Pd), and Part IV walks the
wider world: ambisonic microphones, the Reaper plugin ecosystem (IEM,
SPARTA, ATK, Envelop), game engines and XR middleware, and how all of this
relates to Dolby Atmos. The concepts you learn in Max transfer whole; only
the object names change.

## How this book stays honest

Tutorials rot. Prose describes a patch that no longer matches the software;
a hand-drawn diagram flatters a decoder that never behaved that way. This
book borrows three mechanical commitments from its sibling (the
[SampleRateTap book](https://github.com/tap/SampleRateTap)) to resist that:

1. **Every hands-on chapter opens a real patch.** The companion patches ship
   inside the AmbiTap-Max package, under `patchers/booklet/`. The book never
   describes a patch that doesn't exist in the repository next to the
   objects it uses.
2. **Every plot of computed data is generated, not drawn.** The figures in
   `book/src/img/` are produced by `scripts/generate_book_figures.py` in the
   AmbiTap repository, which drives the *actual C++ library* through its C
   ABI — the same code path the shipping objects run. The ITD curves in
   Chapter 1 come from the same embedded KEMAR head your `binaural~` object
   uses. (Purely schematic diagrams — box-and-arrow pictures — are
   hand-authored SVG and contain no data to lie about.)
3. **Ecosystem claims carry dates.** Chapters about plugins, engines, and
   delivery formats describe a moving landscape; they name versions and say
   "as of 2026" out loud, so you know exactly what to re-verify later.

## The route

- **Part 0 — The flat picture.** How your ears actually localize sound, why
  stereo is a narrow trick, and the three paradigms of spatial audio. No
  tools yet; this is the map legend.
- **Part I — First sounds.** Headphones on: a source orbits your head within
  fifteen minutes, you rotate the world, and you send a scene to real
  loudspeakers. Experience before theory.
- **Part II — What's in the bus.** What those `(order+1)²` channels are, why
  order controls sharpness, and the format paperwork (AmbiX vs FuMa) that
  makes files from different decades interoperable.
- **Part III — The craft, task by task.** Each chapter is a thing you want
  to do: place sources, add distance, build rooms, decode to speaker
  arrays, go deep on binaural, monitor a scene you can't see, mix inside
  it, cancel crosstalk, and fold channel-based beds in.
- **Part IV — The wider world.** Pd, microphones, DAWs, game engines, Atmos.
- **Part V — Which tool, when.** The decision guide.

## What you need

- **Max 9** (the objects need Max's multichannel `mc.` signals; the optional
  UI widgets use `v8ui`). The AmbiTap-Max package, built or downloaded —
  Chapter 3 covers installation.
- **Headphones.** Closed or open, cheap or fancy — but headphones, not
  laptop speakers, for every binaural experiment.
- **Optional, later:** four or more loudspeakers (Chapter 5 onward), Pure
  Data ≥ 0.54, Reaper (Part IV).

## Status of this draft

This is an early, in-progress draft. The Introduction and Parts 0–I are
written; the remaining chapters exist as the outline you can see in the
table of contents, and are being written to the same contract: every claim
demonstrable, every patch shipped, every figure generated.
