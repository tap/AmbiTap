# Appendix B: Conventions cheat sheet

The tables you'll actually consult mid-patch. Everything here restates
the library's own `docs/CONCEPTS.md`, which is authoritative.

## Angles

- **Radians everywhere** in AmbiTap objects. Degrees → radians:
  `expr $f1 * 3.14159265 / 180.`
- **Azimuth**: 0 = front, +π/2 (90°) = **left**, ±π (180°) = behind,
  −π/2 (270°/−90°) = right. Counterclockwise seen from above.
- **Elevation**: 0 = horizon, +π/2 = zenith, −π/2 = nadir.
- **Rotation**: intrinsic Z-Y′-X″ Euler — yaw (about +Z, up) applied
  first, then pitch (about +Y, left), then roll (about +X, front).
  Right-hand rule: **+yaw turns the scene left; +pitch tips the front
  down; +roll lifts the left side.** `rotate~` turns the *world*;
  `binaural~`'s yaw/pitch/roll turn the *head* (equal values cancel).

## The bus

- Channels: **(order+1)²** — 4 / 9 / 16 / 25 / 36 for orders 1–5.
- Ordering: **ACN** (`acn = n(n+1) + m`): W=0, then Y=1, Z=2, X=3, …
- Scaling: **SN3D** — W of a unit source = 1.0; no channel exceeds W
  for a single source. W alone = correct mono mix.
- Orders **nest**: dropping channels above (M+1)² yields a valid
  order-M scene. Truncation is legal; extrapolation is not.
- Mixing = summing buses (join the cords). Same order everywhere
  unless deliberately blurring.

## Conversions

| From | To | How |
|---|---|---|
| FuMa (W,X,Y,Z; W −3 dB) | AmbiX | `ambitap.format~ N` `direction fuma_to_ambix` (orders ≤ 3) |
| AmbiX | FuMa | same object, `ambix_to_fuma` |
| N3D | SN3D | per-order gain ÷√(2n+1) (per-channel `mc.*~` gains) |
| Degrees | radians | × π/180 ≈ 0.0174533 |
| Meters | propagation delay | × ~2.9 ms |

## Speaker layouts (decode~ / bed2hoa~ presets)

Channel order is the layout's listed order; angles are (azimuth°,
elevation°), azimuth positive = left.

| Preset | Ch | Order of channels |
|---|---|---|
| `stereo` | 2 | L (+30), R (−30) |
| `quad` | 4 | FL (+45), BL (+135), BR (−135), FR (−45) |
| `hexagon` | 6 | 60°-spaced ring |
| `octagon` | 8 | 45°-spaced ring |
| `surround_5_1` | 5 | L (+30), R (−30), C (0), LS (+110), RS (−110) — **no LFE** |
| `surround_7_1` | 7 | L, R, C (as 5.1), LS (±90), LB (±135) — no LFE |
| `surround_7_1_4` | 11 | 7.1 + four heights at 45° elevation — no LFE |
| `cube` | 8 | lower square + upper square (full 3D) |

LFE policy: the bus never carries it — peel it before `bed2hoa~`,
derive it (low-passed W) after `decode~`. (Ch. 17)

## Object quick reference (creation args → key controls)

| Object | Args | The controls you'll touch |
|---|---|---|
| `encode~` | order | `azimuth` `elevation` `gain` |
| `rotate~` | order | `yaw` `pitch` `roll` |
| `decode~` | order, layout | `decoder_type` (`mode_match`/`allrad`/`epad`), `max_re` |
| `binaural~` | order (≤5) | `volume`, `hrtf_dataset` (`ls`/`magls`), `sofa` (Max), `yaw`/`pitch`/`roll` |
| `panbin~` | — | `azimuth` `elevation` `gain` |
| `distance~` | order | `distance`, `reference_distance`, `attenuation`, `air_absorption`, `doppler`/`nfc` |
| `doppler~` | order | `distance`, `speed_of_sound`, `max_distance` |
| `room~` | order (≤3) | `dim_x/y/z`, `source_/listener_x/y/z`, `rt60`, `direct`/`er`/`tail`, `absorption fir/iir` |
| `vmic~` | order | `azimuth` `elevation` `max_re` |
| `directional~` | order | `azimuth` `elevation` `gain` |
| `mirror~` | order | `flip_lr` `flip_fb` `flip_ud` |
| `compress~` | order | `threshold` `ratio` `attack` `release` `makeup_gain` (W-keyed) |
| `format~` | order (≤3) | `direction` |
| `bed2hoa~` | order, layout | (static) |
| `energyvec~` | — | `smoothing_time`; outputs x/y/z signals |
| `grid~` (Max) | order | bang → `grid` list; `azimuth_steps`, `smoothing_time`, `dynamic_range` |
| `xtc~` | — | `span` `distance` `regularization` `bypass` (512-sample latency, ~−12 dB) |

## Numbers worth memorizing

- Max ITD ≈ **0.7 ms**; ILD up to ~**20 dB** up high. (Ch. 1)
- Order-3 max-rE beam ≈ **75°**; order 1 ≈ 157°; order 5 ≈ 51°. (Ch. 7)
- Propagation: **~343 m/s** → 2.9 ms/m.
- 1/r law: **−6 dB per doubling** (free field).
- `room~` latency ≈ **53 ms** @ 48 kHz; `xtc~` = **512 samples**.
- Head-tracking budget: total motion-to-sound < **~50 ms**. (Ch. 13)
- Parameter ramps ≈ 128 samples; matrix crossfades ≈ 256. (Library
  contract — why nothing clicks.)
