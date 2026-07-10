# Appendix C: Max ↔ Pd object reference

One table: every object, both environments, and the dialect
differences that matter. (Chapter 18 is the prose version.)

Both wrappers sit on the identical library core — same DSP, same
conventions (AmbiX, radians), same real-time contract. Pd needs
`[declare -lib ambitap]` and Pd ≥ 0.54; Max needs the package in
`Packages/` and Max 9.

| Object | Max | Pd | Notes |
|---|---|---|---|
| `ambitap.encode~` | ✓ | ✓ | identical |
| `ambitap.rotate~` | ✓ | ✓ | identical |
| `ambitap.decode~` | ✓ | ✓ | identical (same layout names; Pd also accepts `5.1`-style aliases) |
| `ambitap.binaural~` | ✓ | ✓ | **Max only:** `sofa` (custom HRTF file). Both: KEMAR built-in, `ls`/`magls`, head yaw/pitch/roll |
| `ambitap.panbin~` | ✓ | ✓ | identical |
| `ambitap.distance~` | ✓ | ✓ | identical |
| `ambitap.doppler~` | ✓ | ✓ | identical |
| `ambitap.room~` | ✓ | ✓ | identical (orders ≤ 3; power-of-two blocks in Pd) |
| `ambitap.vmic~` | ✓ | ✓ | identical |
| `ambitap.directional~` | ✓ | ✓ | identical |
| `ambitap.mirror~` | ✓ | ✓ | identical |
| `ambitap.compress~` | ✓ | ✓ | identical |
| `ambitap.format~` | ✓ | ✓ | identical (orders ≤ 3) |
| `ambitap.bed2hoa~` | ✓ | ✓ | identical |
| `ambitap.energyvec~` | ✓ | ✓ | identical (x/y/z signal outlets) |
| `ambitap.xtc~` | ✓ | ✓ | Pd: stereo program on **two signal inlets**; both output two *loudspeaker* feeds |
| `ambitap.grid~` | ✓ | — | Max only (feeds the `v8ui` heatmap widget) |
| UI widgets (panner, heatmap, DOA, meters, rotation ball, designers) | ✓ (`v8ui`) | — | Max 9 / browser (`ui/` layer); Pd: patch your own from `energyvec~` |
| Designer patchers (`roomdesigner`, `xtcdesigner`, `ui-tour`) | ✓ | — | in `patchers/` |
| Book companion patches | `patchers/booklet/01–12` | `booklet/01-first-sounds.pd` | numbered to the hands-on chapters |

## Dialect crib

| | Max | Pd |
|---|---|---|
| Parameter set | attribute **or** message: `azimuth 1.57` | message only: `[azimuth 1.57(` |
| Saved initial values | attributes persist in the patcher | `[loadbang]` → message boxes |
| Bus cord | `mc.` patch cord | multichannel cord (Pd ≥ 0.54) |
| Split/merge bus channels | `mc.unpack~` / `mc.pack~` / `mc.combine~` | Pd channel objects (`snake~` family) |
| Bus metering | `mc.meter~`, `mc.scope~` | per-channel `env~`s (patch it) |
| Audio out | `ezdac~`, `mc.dac~ 1 2 3 4` | `dac~ 1 2 3 4` via channel split |
| Load the library | automatic (package) | `[declare -lib ambitap]` |

Units are identical everywhere: **radians** for all angles (both
environments — a degrees-labeled dial belongs in front of the same
`expr` conversion), meters for distances, seconds for RT60, dB where
a control says dB.
