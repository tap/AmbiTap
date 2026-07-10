# Appendix A: Glossary

Working definitions, tuned to how this book uses each term. Chapter
references point to the fuller treatment.

**A-format** — The raw four-capsule output of a tetrahedral ambisonic
microphone, before matrixing. Not usable directly. (Ch. 19)

**ACN** — Ambisonic Channel Number; the modern channel ordering,
`acn = n(n+1) + m`. W=0; Y,Z,X = 1,2,3. Half of AmbiX. (Chs. 6, 8)

**ALLRAD** — Decoder construction: sample to an ideal virtual layout,
then VBAP-pan the virtual speakers onto the real ones. The safe choice
for irregular/gappy arrays. (Ch. 12)

**AmbiX** — The modern ambisonic convention: ACN ordering + SN3D
scaling. What AmbiTap and everything current speaks. (Ch. 8)

**Ambisonics** — The scene-based spatial audio paradigm: store the
directional soundfield at a point as spherical-harmonic components;
decode at playback. (Ch. 2)

**Azimuth / elevation** — Direction angles: azimuth 0 = front,
positive counterclockwise from above (+90° = left); elevation 0 =
horizon, +90° = zenith. Radians in AmbiTap objects. (Ch. 3)

**B-format** — An ambisonic signal set; historically first-order FuMa,
loosely any ambisonic bus today. Ask order/ordering/normalization.
(Chs. 6, 8)

**Bed** — A channel-based base layer (5.1, 7.1.4) in an otherwise
object- or scene-based mix. Folded into scenes with `bed2hoa~`.
(Chs. 17, 22)

**Binaural** — Two-channel audio that reproduces the *ear-entrance*
signals — rendered through HRTFs, headphones assumed. (Chs. 1, 13)

**Channel-based** — The paradigm where the stored channels *are* the
speaker feeds (stereo, 5.1, 7.1.4). (Ch. 2)

**Cone of confusion** — The set of directions sharing (nearly) the
same ITD/ILD; broken by pinna spectra and head movement. (Ch. 1)

**Crosstalk cancellation (XTC / transaural)** — Pre-filtering two
speaker feeds so binaural signals survive the trip through open air
to one listener's ears. (Ch. 16)

**Decoder** — The matrix (and philosophy) turning scene channels into
speaker feeds: mode-matching, ALLRAD, EPAD. (Chs. 5, 12)

**Doppler** — Pitch shift from changing propagation delay; the
strongest "really moving" cue. (Ch. 10)

**Energy vector (rE)** — Loudness-weighted mean direction of a decode
or scene; its magnitude predicts image focus. The instrument
`energyvec~` tracks the scene's. (Chs. 12, 14)

**EPAD** — Energy-preserving decoder construction (SVD-based).
(Ch. 12)

**Externalization** — Hearing sound *out there* rather than inside
the head on headphones; helped most by rooms, MagLS, and tracking.
(Chs. 11, 13)

**FuMa** — The historical convention (Furse–Malham): W,X,Y,Z order,
W at −3 dB, defined to order 3. Convert at the border with
`format~`. (Ch. 8)

**HOA** — Higher-Order Ambisonics: order ≥ 2, (N+1)² channels.
(Chs. 2, 7)

**HRTF / HRIR** — Head-Related Transfer Function (frequency view) /
Impulse Response (time view): per-direction filters from source to
each eardrum; the raw material of binaural rendering. (Ch. 13)

**ILD / ITD** — Interaural Level / Time Difference: the two
lateral-localization cue families. (Ch. 1)

**KEMAR** — The standard measurement mannequin whose HRTF set is
embedded in AmbiTap (MIT dataset, SH-projected to order 5). (Ch. 13)

**LFE** — Low-Frequency Effects channel (the ".1"): a delivery
convention, not a direction; routed around the bus. (Ch. 17)

**MagLS** — Magnitude least-squares HRTF projection: sacrifices
high-frequency phase to preserve magnitude at low order; the modern
binaural default. (Ch. 13)

**max-rE** — Per-order weighting that maximizes the energy-vector
magnitude — cleaner concentration above the reconstruction limit.
A decoder attribute and a beam flavor. (Chs. 5, 7, 12)

**Mid-side (M/S)** — Stereo technique (omni-ish mid + sideways
figure-8); B-format's two-channel ancestor in spirit. (Ch. 6)

**Mode-matching** — Decoder construction by (pseudo)inverting the
re-encoding matrix; faithful on healthy layouts, strained on gappy
ones. (Ch. 12)

**N3D** — Orthonormal scaling cousin of SN3D (higher orders hotter by
√(2n+1)); appears in research tools. (Ch. 8)

**NFC** — Near-field compensation: correcting the bass boost of
wavefront curvature for close sources. (Ch. 10)

**Object-based** — The paradigm storing dry sources + position
metadata, rendered at playback (Atmos, game engines). (Chs. 2, 22)

**Order (N)** — The spherical-harmonic degree cap of a scene:
(N+1)² channels; sharpness ~1/N; this book defaults to 3. (Ch. 7)

**Pinna cues** — Direction-dependent spectral fingerprints from the
outer ear; carry elevation and front/back. Personal. (Ch. 1)

**Precedence effect** — The first-arriving wavefront dominates
localization; why off-center listeners hear the nearest speaker.
(Chs. 1, 5)

**rE** — see Energy vector.

**Scene-based** — The paradigm of this book; see Ambisonics. (Ch. 2)

**SN3D** — The AmbiX level scaling: no channel exceeds W for a single
source. (Chs. 6, 8)

**SOFA** — Standard file format for measured HRTF sets; accepted by
`binaural~` (Max) for personalized ears. (Ch. 13)

**Soundfield** — The pressure-and-direction state of sound at a
point; what a scene stores and an ambisonic mic records. Also the
historical microphone brand. (Chs. 2, 19)

**Spherical harmonics (SH)** — The basis functions on the sphere;
each ambisonic channel is one, readable as a mic pickup pattern.
(Ch. 6)

**Sweet spot** — The listening region where reproduction holds
together; grows with order and decoder care. (Chs. 5, 7)

**T-design** — A uniform point set on the sphere (used as ALLRAD's
virtual layout and in the library's internals). (Ch. 12)

**VBAP** — Vector-Base Amplitude Panning: pan a source onto the
nearest 2–3 speakers; the object-renderer workhorse, and ALLRAD's
second stage. (Chs. 2, 12)

**Virtual microphone** — A beam extracted from a scene in a chosen
direction (`vmic~`); resolution set by order. (Ch. 15)

**W** — Channel 0: the omni component; a correct mono mix of the
scene; the key signal for image-preserving dynamics. (Chs. 6, 15)
