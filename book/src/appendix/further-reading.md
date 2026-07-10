# Appendix D: Annotated further reading

A short shelf, ordered by what to reach for next. Open-access items
are marked ⊚.

## The one book after this book

**Zotter & Frank, *Ambisonics: A Practical 3D Audio Theory for
Recording, Studio Production, Sound Reinforcement, and Virtual
Reality* (Springer, 2019).** ⊚ Open access, and the field's standard
text. Everything this book handled with intuition and sidebars —
spherical-harmonic derivations, max-rE optimality, ALLRAD (Zotter &
Frank are its authors), decoder theory, the kr limit — is derived
properly here. Chapters 1–4 are the natural continuation of our Part
II; its treatment matches the conventions AmbiTap uses.

## Foundations

**Blauert, *Spatial Hearing* (MIT Press, rev. 1997).** The
encyclopedic psychoacoustics of Chapter 1 — ITD/ILD, pinna cues, the
cone of confusion, precedence — with the experimental record behind
every claim.

**Rayleigh, "On our perception of sound direction" (*Phil. Mag.*,
1907).** The duplex theory, from the source; a genuinely readable
Victorian paper.

**Gerzon, "Periphony: With-Height Sound Reproduction" (*JAES*,
1973).** Ambisonics' founding paper — B-format, the energy-vector
thinking, decades early. Historical but bracing: most of the field is
already here.

**Pulkki, "Virtual Sound Source Positioning Using Vector Base
Amplitude Panning" (*JAES*, 1997).** VBAP — Chapter 2's object
workhorse and ALLRAD's second stage.

## The specifics this book leaned on

**Nachbar, Zotter, Deleflie & Sontacchi, "AMBIX — A Suggested
Ambisonics Format" (Ambisonics Symposium, 2011).** ⊚ The AmbiX
specification: ACN, SN3D, and the FuMa conversion tables `format~`
implements. Short; read it once and Chapter 8 becomes obvious.

**Zotter & Frank, "All-Round Ambisonic Panning and Decoding"
(*JAES*, 2012).** ALLRAD — the construction behind Chapter 12's
flattest curve.

**Schörkhuber, Zaunschirm & Höldrich, "Binaural rendering of
Ambisonic signals via magnitude least squares" (DAGA, 2018).** ⊚
MagLS — why `hrtf_dataset magls` sounds the way Chapter 13's figure
shows.

**Allen & Berkley, "Image method for efficiently simulating
small-room acoustics" (*JASA*, 1979).** The image-source model behind
`room~`'s early reflections and Chapter 11's reflectogram.

**Gardner & Martin, "HRTF Measurements of a KEMAR Dummy-Head
Microphone" (MIT Media Lab TR-280, 1994).** ⊚ The measurements inside
`binaural~`.

**Majdak et al., "Spatially Oriented Format for Acoustics (SOFA)"
(AES convention, 2013; standardized as AES69).** The HRTF container
of Chapter 13; sofaconventions.org hosts the public databases
(HUTUBS, ARI, SADIE II) worth auditioning.

## Tools and communities

**The IEM Plug-in Suite documentation** (plugins.iem.at) ⊚ — besides
documenting Chapter 20's reference suite, the plugin manuals are a
compact course in production ambisonics by the group that wrote the
textbook above.

**The SPARTA papers and site** (leomccormack.github.io/sparta-site)
⊚ — Aalto's suite, with citations into the parametric-spatial-audio
research frontier (COMPASS, HO-DirAC) when you want to see past
linear ambisonics.

**AmbiTap's own documentation** ⊚ — this book deliberately didn't
duplicate it: `docs/CONCEPTS.md` (the conventions and the real-time
contract), `docs/COMPARISON.md` (the measured cross-library
verification — how we know the numbers in this book's figures match
independent implementations), and the executed notebooks in
`notebooks/` (every algorithm, visualized and asserted). The
figures in this book regenerate via `scripts/generate_book_figures.py`.

**Where the practitioners are** (2026): the Sursound mailing list
(venerable, active since the '90s), the IEM and McGill communities'
public materials, and the game-audio side's GDC audio talks ⊚ for
Chapter 21's world in practice.

## If you read only three things

1. Zotter & Frank (the book) — theory, properly.
2. The AmbiX paper — ten minutes, permanent immunity to Chapter 8
   problems.
3. Blauert — because every renderer in this book is ultimately an
   argument with your auditory system, and Blauert is its biography.
