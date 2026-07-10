# The DAW route

Not every spatial project wants a patcher. Long-form editing,
comping, automation lanes, video sync, stem management — the things
DAWs are *for* — don't stop mattering because the mix has a Z axis.
This chapter maps the DAW route to the same destination: which host,
which plugin suites, and how the concepts you own translate. (Names
and versions as of 2026; the shape of the route changes slowly.)

## The host question

An ambisonic mix needs tracks and buses that carry (N+1)² channels
through plugin chains without flinching. That single requirement
sorts the DAW market:

- **Reaper** is the community's default answer, and this chapter's:
  tracks up to 128 channels, per-track channel mapping, plugin pin
  routing, multichannel files as first-class citizens — order 7 rides
  an ordinary track. Cheap, scriptable, cross-platform.
- **Pro Tools, Nuendo, Logic** carry ambisonic bus formats natively
  but historically cap them low (commonly order 3, tied to their
  Atmos-centric workflows); fine for delivery work inside those
  ecosystems.
- **Ableton Live and the performance DAWs** have no native ambisonic
  bus; the working pattern is Max for Live devices (your Part I–III
  patches, literally, hosted in a set — see Envelop below) with
  multichannel routing tricks.

The rest of this chapter assumes Reaper; translate freely.

## The plugin suites

Four open-source suites cover the territory; all speak AmbiX; a
working mix borrows from several (they chain happily — it's all the
same bus):

- **IEM Plug-in Suite** (Graz — the institute whose research this
  book keeps citing): the reference set. Encoders, a SceneRotator,
  AllRAD-based decoding with a decoder-designer, binaural, EnergyVisualizer
  — plus the celebrated **RoomEncoder** (a positional room simulator
  in the spirit of `room~`, with hundreds of image-source
  reflections). Order 7. If this book had a DAW edition, it would be
  written over IEM.
- **SPARTA** (Aalto): the research bench — up to order 10, SOFA
  binaural with OSC head-tracking, spherical-array encoders (your
  Eigenmike's A-to-B stage lives here as `array2sh`), beamformers,
  powermaps (Chapter 14's instruments, DAW edition), and the
  parametric COMPASS processors.
- **ambiX plugins** (Kronlachner): small, ancient, indispensable —
  converters (the `format~` of the DAW world), rotators, and the
  ambiX binaural/decoder pair that half the field's tutorials assume.
- **ATK (Ambisonic Toolkit) for Reaper**: first-order-focused
  classics with a strong transform vocabulary (dominance, focus —
  `directional~`'s relatives) and deep documentation lineage.
- **Envelop for Live**: the Ableton answer — Max for Live devices
  (order 3) for source panning, rotation, and binaural monitoring,
  born from the Envelop venue's practice.

Concept-mapping is one row each: `encode~` → any suite's *encoder*
("panner"); `rotate~` → SceneRotator; `decode~` → the suite decoders
(IEM's designer covers the custom-array case Chapter 12 wished for);
`binaural~` → the binaural decoders; `grid~`/`energyvec~` → the
visualizers. Your Part III instincts — order choices, ALLRAD-for-gappy,
MagLS, W-keyed dynamics — transfer without edits.

## The session template

The load-bearing trick: **the master bus is an ambisonic bus.** In
Reaper: set the master (or a dedicated "SCENE" bus track) to 16
channels for order 3; every source track gets an encoder plugin as
its *panner* and sends multichannel to the scene; the scene's chain
ends in a *monitoring* section — a binaural decoder for headphone
work, a speaker decoder when the rig is attached — that you **bypass
at render time** when delivering the raw scene. AmbiTap fits this
picture at the borders: record your Max scene to a 16-channel file
(`mc.` recording into `sfrecord~`-style workflows) and it drops onto
the Reaper timeline as a finished element; conversely a Reaper-mixed
AmbiX render walks into any patch in this book.

Automation is where the DAW route earns its keep — encoder azimuths
drawn against picture, order-3 beds under comped dialogue — and the
one habitual mistake is putting *channel-ignorant* plugins on the
scene bus: a stereo compressor inserted on 16 channels processes two
and passes fourteen, smearing geometry Chapter 15 taught you to
protect. Scene buses take scene-aware processors only (IEM ships
multiband dynamics; the W-keyed trick is portable); source tracks,
pre-encoder, take anything.

## Delivery

The last mile, format by format:

- **Scene masters**: multichannel WAV, AmbiX, order stated — the
  Chapter 8 paperwork. This is the archival master everything else
  renders from.
- **YouTube 360 / VR video**: first-order AmbiX (4ch) muxed with the
  video, plus optionally a head-locked stereo track; Google's
  injection tooling stamps the metadata. Truncate your order-3
  master (Chapter 7's nesting — drop channels 4–15), check the
  result binaurally, done.
- **Binaural print**: render through your best binaural decoder
  (MagLS, personalized ears if you have them) to plain stereo. State
  "binaural — headphones" on the file; it's a *render*, not a scene
  (Chapter 13's caveats about strangers' heads apply).
- **Speaker prints**: decode per venue (Chapter 12), print the feeds,
  label the layout. For festival-circuit work, ship the *scene* plus
  a README instead and let each venue decode — the entire point of
  the format.
- **Atmos deliverables**: a different paradigm, not a different
  button — the next-but-one chapter untangles it.

## Checkpoint

Reaper (or any wide-bus host) + IEM/SPARTA/ambiX/ATK covers the DAW
route end to end; the session pattern is encoder-as-panner into a
scene-format master bus with monitoring you bypass at render;
delivery is AmbiX for scenes, truncation for YouTube, renders for
binaural and rigs. Same bus, longer timeline. Next: the toolchains
where the listener steers — games and XR.
