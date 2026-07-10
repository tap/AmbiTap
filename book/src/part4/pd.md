# The same patch in Pd

Everything you've learned rides a bus, not a brand. This chapter proves
it the quick way: Chapter 3's first-sounds patch, rebuilt in Pure Data,
plus the complete phrasebook for moving any patch in this book between
the two environments.

If you don't use Pd, skim the phrasebook table and move on — the real
lesson is how *little* changes. If you do: the companion patch is
**`booklet/01-first-sounds.pd`** in the AmbiTap-Pd repository.

## Setup

AmbiTap-Pd ships the same DSP as the Max package — the identical
library underneath, wrapped as **one Pd library** named `ambitap`
containing all the `ambitap.*~` classes. Requirements: **Pd ≥ 0.54**
(the first release with multichannel signal cords — the whole
architecture depends on them) and a build of the library
(`cmake -B build && cmake --build build` in the repo; the result lands
in `externals/`). Then, in any patch that uses the objects:

```text
[declare -lib ambitap]
```

— the one line of ceremony Max's package system did for you.

## First sounds, translated

```text
[noise~]
|
[ambitap.encode~ 3]        ← same name, same creation arg, same
|                             (order+1)^2-channel multichannel cord
[ambitap.binaural~ 3]
|         \
[dac~]
```

Structurally identical. The differences are all *dialect*:

- **Messages instead of attributes.** Pd has no attribute system, so
  parameters arrive as messages on the left inlet — same names, same
  units: `[azimuth $1(`, `[elevation $1(`, `[yaw $1(`. Radians here
  too; the `[expr $f1 * 3.14159265 / 180]` idiom survives unchanged.
- **Thick cords, same meaning.** Pd ≥ 0.54 draws multichannel
  connections like any signal cord; `[snake~]`/channel tools apply if
  you need to split. Summing buses by joining cords works exactly as
  in Max.
- **No `@attr args` in the object box.** Creation args carry the
  order/layout (`[ambitap.decode~ 3 quad]` — identical); everything
  else is a message after creation (a `[loadbang]`-driven message box
  replaces Max's saved attribute values).

## The phrasebook

| Concept | Max | Pd |
|---|---|---|
| Load the objects | package in `Packages/` | `[declare -lib ambitap]` |
| Set a parameter | `azimuth 1.57` message or `@azimuth` attribute | `[azimuth 1.57(` message |
| Initialize a parameter | saved attribute | `[loadbang]` → message |
| The HOA bus | thick `mc.` cord | thick multichannel cord (Pd ≥ 0.54) |
| Watch the bus | `mc.scope~` / `mc.meter~` | per-channel via channel-splitting + `[env~]`s (no mc scope built in) |
| Multichannel output | `mc.dac~ 1 2 3 4` | `[dac~ 1 2 3 4]` fed via channel-split |
| Audio on | `ezdac~` / Audio On | DSP toggle (`Media → DSP On`) |

Object-for-object, the Pd library carries **sixteen of the seventeen**
Max externals — everything except `ambitap.grid~` (the heatmap
analysis feed; its consumer is the Max `v8ui` widget layer, which has
no Pd counterpart yet). The whole of Parts I–III therefore translates,
with three practical footnotes from the Pd wrappers' own docs: the
convolution-based objects (`binaural~`'s SOFA loading is Max-only —
Pd's `binaural~` uses the built-in KEMAR set with `ls`/`magls` intact;
`panbin~`, `xtc~`, `room~`) (re)allocate when the DSP graph compiles
and want power-of-two block sizes; `xtc~` takes its stereo program on
two ordinary signal inlets; and the UI story is patch-it-yourself —
Chapter 14's *instruments* exist (`energyvec~` outputs its x/y/z
signals identically) but the *gauges* don't.

## Why bother?

Because the two environments have different superpowers, and scenes
travel between them losslessly (it's the same AmbiX bus; record it to
a multichannel file in one, open it in the other):

- **Pd is deployable.** It runs headless on a Raspberry Pi bolted
  inside an installation plinth, boots from a script, and costs
  nothing to license on ten machines. The economical pattern:
  *compose* in Max with the widgets and comforts, *exhibit* in Pd —
  same objects, same rendering, order-3 binaural comfortably within a
  Pi's budget.
- **Pd is embeddable.** libpd puts a Pd patch inside an app or game
  prototype; your spatializer rides along.
- **And the reverse:** if you live in Pd and borrowed this book for
  the concepts — everything in Parts 0–III holds verbatim; only the
  screenshots were in the other church.

## Checkpoint

Same library, same bus, same names, same radians; messages for
attributes, `declare -lib` for the package, sixteen of seventeen
objects, no widget layer. Skills, it turns out, were never
Max-shaped. The next three chapters push that further — into
microphones, DAWs, and game engines — where even the object names stop
being familiar but the ideas keep translating.
