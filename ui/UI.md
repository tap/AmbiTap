# AmbiTap UI — visualization and interaction widgets

Status: written 2026-07-08, updated 2026-07-09. This is the authority for the
UI work, in the same spirit as `docs/ROADMAP.md` is for the object line.
**Build-sequence waves 1-3 are implemented**: the coordinate/arcball core,
the renderer seam, and the panner (wave 1); the WASM build of the C ABI,
the AudioWorklet host, and the heatmap / DOA / meters widgets in both hosts
(wave 2 — streaming handles were added to `tools/capi/` for it, and
`ambitap.grid~` to AmbiTap-Max for the Max heatmap feed); the rotation ball
and decoder-layout widgets, an SH-rotation streaming handle on the embedded
profile (`compute_sh_rotation` + `sh_block_applier`), and the
browser-as-remote-surface OSC path (wave 3). The browser dashboard is
verified end-to-end in headless Chromium — the WASM worklet runs
`dsp::encoder` → SH rotation → `analysis::soundfield_grid` +
`energy_vector` with an ALLRAD metering decode, and a yaw-ring drag both
counter-rotates the heatmap and lands as `/ambitap/orientation` OSC on a
UDP listener via `scripts/osc-bridge.mjs`. The v8ui bundles pass
simulated-mgraphics smoke runs and `ambitap.grid~` passes a full-header
type check, but — like the externals — the Max side still **needs in-Max
verification**. Wave 4+ (the designer widgets) remains planning.

The UI work lives in this top-level `ui/` directory of the AmbiTap library
repo — next to `tools/capi/`, which it depends on — because the shared widget
model serves every wrapper target (web, Max, later Pd), not just one of them.

## The strategic frame

The library core is done and verified; the Max object set is code complete
(see the AmbiTap-Max README and `docs/ROADMAP.md`). What is missing is the
layer users *see*: direction pickers, rotation controls, soundfield meters.
The analysis layer was built for exactly this — `analysis::energy_vector`
publishes an atomic `{x,y,z}` snapshot readable from any thread, and
`analysis::soundfield_grid::snapshot()` returns a display-ready normalized
equirectangular image — but nothing consumes them yet.

Two hosts matter, and the plan is one codebase for both:

1. **Web browsers** (HTML5 canvas) — demos, documentation, remote control
   surfaces, and standalone tools. Via **WASM builds of the C ABI**
   (`tools/capi/`), browser widgets run the *actual verified core* in an
   AudioWorklet — not a JavaScript reimplementation that would need its own
   correctness story.
2. **Max** via **`v8ui`** (the V8-engine JS UI object, Max 8.5+; not the
   legacy ES5 `jsui`) — drawing through `mgraphics`, fed by the
   `ambitap.*~` externals.

The trap this architecture avoids: `v8ui`'s `mgraphics` is a Cairo-style
immediate-mode API that is *shaped like* canvas 2D but is not canvas 2D. So
the widgets are written against a small renderer interface of our own, and
each host gets a thin backend that binds it — the widget logic (the part with
bugs, and the part worth testing) is 100% shared; only ~150 lines of drawing
glue differ per host. This mirrors the library's own philosophy: one verified
core, thin wrappers.

What the UI layer should NOT be: a plugin GUI framework (IEM/SPARTA own
that), a general-purpose Max UI toolkit, or anything that touches the audio
thread. Widgets are pure presentation + interaction over the existing
analysis/DSP surface.

## Design principles

- **One model, pluggable renderer.** Widget state, layout, hit-testing,
  coordinate mapping, and interaction logic live in framework-free
  TypeScript (`ui/core/`, `ui/widgets/`) with no rendering calls. Renderer
  backends (`ui/render/canvas.ts`, `ui/render/mgraphics.ts`) implement one
  small drawing interface (`moveTo / lineTo / arc / fill / stroke / text /
  setColor / …`). Widgets never call a host API directly.
- **The library's conventions are the widgets' conventions.** Azimuth 0 =
  front, +π/2 = left; elevation 0 = horizon, +π/2 = zenith; rotations
  compose yaw (+Z) → pitch (+Y) → roll (+X); ACN/SN3D everywhere. The
  az/el↔pixel mappings and arcball math in `ui/core/` are built against
  exactly these and unit-tested — this is where "why is left/right flipped"
  bugs live, and they must live in exactly one place.
- **Never paint per audio block.** The analysis objects publish atomic
  snapshots precisely so the UI can poll at display rate: `qmetro` → bang in
  Max, `requestAnimationFrame` in the browser (the worklet posts snapshots
  via SharedArrayBuffer or postMessage). Target 30–60 fps; degrade
  gracefully by dropping frames, never by blocking.
- **Preserve the real-time contract at the UI boundary.** Widgets read
  `value()` / `snapshot()` (any-thread by design) and write parameters
  through ordinary control-thread setters / Max attributes. No widget ever
  causes an allocation or a lock on an audio thread.
- **Use the native display path for images.** The soundfield heatmap is a
  `rows × cols` float image; in Max it goes into a `jit.matrix` shown by
  `jit.pwindow` (GPU-cheap; the roadmap already names this), in the browser
  it is a canvas `ImageData` blit. Hand-painting grid cells through the
  vector API is the wrong tool in both hosts.
- **Bidirectional where the data flows both ways.** The rotation widget both
  *drives* yaw/pitch/roll and *displays* incoming OSC head-tracking; the
  panner both sets and shows az/el. Every controller widget must render an
  externally-set value without fighting the user's drag (last-writer-wins
  with a short drag-priority window).
- **OSC/WebSocket symmetry.** In Max, widgets talk to objects via patch
  cords/attributes; in the browser, the same widget emits the same parameter
  stream over WebSocket/OSC so a browser tab can be a remote control surface
  for a Max patch or a native app.

## Architecture

```
ui/
├── UI.md               this document
├── core/               framework-free TS: coords (az/el↔pixel, equirect,
│                       arcball quaternions), hit-testing, drag state,
│                       value smoothing/throttling.  NO rendering. Unit-tested.
├── render/
│   ├── renderer.ts     the drawing interface all widgets target
│   ├── canvas.ts       CanvasRenderingContext2D backend (browser)
│   └── mgraphics.ts    mgraphics backend (Max v8ui)
├── widgets/            one module per widget (see catalog)
├── hosts/
│   ├── web/            demo pages, AudioWorklet + WASM loader, WebSocket/OSC
│   └── max/            v8ui entry scripts + helper patches (thin; the Max
│                       package itself stays in AmbiTap-Max)
└── test/               unit tests for core/ + golden-image tests for widgets
                        (render to canvas in headless Chromium)
```

Build: TypeScript compiled to (a) ES modules for the browser and (b) a
single-file bundle per widget for `v8ui` (V8 in Max loads modern JS but not
web module resolution). No framework, no runtime dependencies.

Data feeds:

| Host | Analysis data | Parameter writes |
|---|---|---|
| Browser | WASM build of `tools/capi/` in an AudioWorklet (`ambitap_energy_vector`, `ambitap_soundfield_grid`, `ambitap_decoder_matrix`, `ambitap_layout_preset`, `ambitap_evaluate_sh` already exist in the ABI) | direct into the worklet; optionally out over WebSocket/OSC |
| Max | outlets of `ambitap.energyvec~` etc., or a small `dict` / `jit.matrix` bridge from the externals | attribute messages to the `ambitap.*~` objects |

## Widget catalog

### Controllers

- **`panner`** — source direction picker: top-down dome + elevation ring
  (v1), arcball sphere (later). Drives `encode~` az/el; doubles for
  `vmic~`, `directional~`, `panbin~`. Optional radius axis for `distance~`.
  Multi-source: N draggable dots on one surface. **This is the first
  deliverable** — it is used everywhere and exercises the whole
  dual-renderer seam.
- **`rotation`** — yaw/pitch/roll arcball for `rotate~` / `binaural~` /
  `room~` listener orientation. Bidirectional (displays OSC head-tracking).
  Draws a head glyph + horizon ring so the sign conventions are visible at a
  glance.
- **`layout`** — decoder layout view: speakers on the sphere (presets
  stereo … 7.1.4 via `ambitap_layout_preset`, or custom), per-speaker gain
  read from the decode matrix. Ties to `decode~` / `bed2hoa~`.

### Visualizers

- **`heatmap`** — soundfield energy on the equirectangular grid, direct from
  `soundfield_grid::snapshot(dynamic_range_db)` (`image { rows, cols,
  data[0..1], peak_db }`; col 0 = −π back-wrap, centre col = front, row 0 =
  zenith). Overlays: speaker positions, the energy-vector arrow, `peak_db`
  readout. Max path: `jit.matrix` + `jit.pwindow`, widget draws overlays
  only.
- **`doa`** — energy-vector dot from `energy_vector::value()` on the same
  projection (or on the panner surface). Vector magnitude (‖rE‖) maps to
  radius/opacity so it reads localization *quality*, not just direction.
- **`meters`** — per-ACN-channel meter bridge (36 bars at order 5;
  diagnostic, high value for debugging bus wiring) and per-order energy
  bands. Later: polar directivity plot of the current `vmic~` max-rE beam
  (evaluate the beam pattern via `ambitap_evaluate_sh` /
  `ambitap_max_re_weights`).

### `roomdesigner` — the `room~` widget

`dsp::room` already exposes everything a designer needs, most of it without
any library change:

- **Geometry editing.** Plan view (x/y) + side view (x/z), or a small 3D
  isometric later: drag the source and listener dots, drag walls to resize
  `dim_x/y/z`. Writes `set_room_dimensions` / `set_source_position` /
  `set_listener_position` (all clamp-and-rebuild off-thread already —
  drag-friendly by construction).
- **Reflectogram (energy-time curve).** `room::for_each_image` is a public
  *static* enumeration — `(t_seconds, amplitude, unit_direction,
  reflection_count)` for every image source before `t_max` — pure math,
  callable on the UI thread or from WASM with no instance. Draw amplitude
  stems vs time, colored by reflection order, with the direct sound, the
  30 ms early/tail junction, and the fixed `latency_samples()` offset
  marked. Overlay the target RT60 decay slope per band so "does the tail
  match the knob" is visible.
- **Image-source cloud.** In the plan view, draw the mirrored rooms and
  image sources from the same enumeration (first 1–2 reflection orders) —
  the classic Allen–Berkley picture, and the best pedagogy for why the
  early pattern changes when a source approaches a wall.
- **Absorption + RT60 editors.** Six per-wall reflection-coefficient
  handles on the room faces (`set_wall_reflections`), and a 6-band RT60
  breakpoint curve (`set_rt60_band` / `rt60band` message). Show the
  `fir`/`iir` absorption trade as a mode toggle with its CPU note.
- **Live overlay.** Feed the widget's `heatmap`/`doa` layers from the
  `room~` *output* bus — the early reflections actually arriving in the HOA
  domain light up at the directions the image-source cloud predicts. This
  closes the loop between the model drawing and the verified DSP, in the
  same spirit as the notebooks.

Direction-convention note: room geometry is Cartesian (meters, walls at
x0/x1/y0/y1/z0/z1) while the rest of the UI is az/el — `ui/core/` owns that
conversion, tested against `for_each_image`'s unit directions.

### `xtcdesigner` — the `xtc~` widget

`dsp::xtc` exposes its geometry, its design constants, and — crucially — the
four shipped FIRs (`fir(speaker, input)`), plus `makeup_gain()` /
`design_gain_db()` / `latency_samples()`:

- **Geometry view.** Top-down head + two speakers at ±span/2 and the given
  distance; drag speakers to set `span` (clamped 5°–120°) / `distance`.
  Indicate the sweet spot and its fragility (cancellation is a
  head-position-sized fiction above ~6 kHz — say so in the UI rather than
  overselling).
- **Filter response plot.** FFT the four FIRs and plot |H_ij(f)| with the
  cancellation band (300 Hz – 6 kHz) shaded, the +12 dB gain ceiling
  (`k_gain_ceiling_db`) as a rule, and `design_gain_db` / makeup annotated.
  Redesign happens on the control thread on every setter, so the plot
  refreshes on parameter change, not per frame.
- **Predicted performance curve.** The X1-style rejection prediction
  (P = C·H off-diagonal vs frequency) needs the plant C, which is internal
  to `redesign()`. Either reconstruct C in the widget from the KEMAR probe
  (`ambitap_builtin_hrtf_*` exists in the ABI) or — cleaner — add a small
  accessor to `dsp::xtc` that exports the design-grid prediction it already
  computes. Prefer the accessor: the widget should display the *design's*
  numbers, not a reimplementation's.
- **Loudness-matched A/B.** The listening protocol's bypass rule: `xtc~`
  output sits ~12 dB below bypass by design, so a naive A/B is a loudness
  test, not an XTC test. The widget's bypass button drives the object's
  ramped `bypass` attribute *and* applies the documented level match, so
  casual A/B comparisons are honest by default.

Both designer widgets are parameter-heavy rather than frame-rate-heavy: they
redraw on parameter change and on a slow meter tick, not at animation rate.
They are also the two objects gated behind the measurement + listening pass
(`docs/PERCEPTUAL-VERIFICATION.md`) — the widgets should *serve* that
protocol (honest A/B, visible predictions vs gates), not bypass it.

## Developing

```bash
cd ui
npm install
npm test                  # tsc + node --test: core conventions, widget logic,
                          # and (when built) the WASM module end-to-end
./scripts/build-wasm.sh   # tools/capi -> dist/wasm/ambitap.wasm (needs emsdk
                          # and an Eigen checkout; see the script header)
npm run build             # dist/web/ (serve it and open index.html — ES
                          # modules and WASM need http) + dist/max/ ([v8ui])
```

Angles are radians on every interface (matching the library and the
`ambitap.*~` attributes); degrees appear only in display strings.

## Build sequence

1. **Seam first — DONE (needs in-Max verification).** `ui/core/` coordinate
   + arcball math with unit tests locked to the library conventions;
   `renderer.ts` interface; the `canvas` and `mgraphics` backends; the
   **`panner`** widget running in a browser page and in a `v8ui` script
   driving `ambitap.encode~` (`dist/max/ambitap.panner.js` emits
   `azimuth <rad>` / `elevation <rad>` messages for the encode~ inlet and
   accepts the same messages back, drag-gated).
   Exit criterion: one widget file, two hosts, no forked logic.
2. **Visualizers — DONE (Max side needs in-Max verification).** WASM build
   of `tools/capi/` (`scripts/build-wasm.sh`; streaming
   encoder/grid/vector handles added to the ABI, since the batch entries
   reset smoothing state per call) + AudioWorklet host running the verified
   core on the audio thread; `heatmap` (ImageData blit in the browser,
   Renderer-cell path in v8ui), `doa`, `meters`. Max feeds:
   `ambitap.grid~` (new external, list-on-bang) for the heatmap,
   `ambitap.energyvec~` + `snapshot~` for the DOA, `mc.peakamp~` for the
   meters. Exit criterion met in the browser: the notebooks' soundfield
   story, live — panner drags move the heatmap lobes and the DOA dot.
3. **`rotation` + `layout` — DONE (Max side needs in-Max verification).**
   The rotation ball (arcball tumble + yaw twist ring, head glyph, rotated
   horizon; bidirectional, so incoming OSC head-tracking displays and is
   drag-gated) and the decoder layout view (library presets cross-checked
   against `ambitap_layout_preset` by the test suite, click-to-select,
   live per-speaker levels). The worklet chain grew an SH-rotation stage —
   a new `ambitap_rotator_*` streaming handle on the embedded profile
   (`compute_sh_rotation` + `sh_block_applier`, no threads, click-free
   crossfade) — plus an ALLRAD decode matrix for per-speaker metering.
   Remote surface: widget gestures mirror as binary OSC over WebSocket
   (`core/osc.ts`, `hosts/web/remote.ts`, `?remote=ws://...`) through
   `scripts/osc-bridge.mjs` to UDP for `[udpreceive]` in Max
   (`/ambitap/source/<id>/direction`, `/ambitap/orientation`; radians).
   v8ui hosts: `ambitap.rotation.js` (drives `rotate~`/`binaural~`,
   accepts `yaw`/`pitch`/`roll`/`ypr` back), `ambitap.layout.js`
   (`preset <name>`, `speakers <az el ...>`, levels from `mc.peakamp~`,
   emits `select`).
4. **Designer widgets.** `roomdesigner`, then `xtcdesigner` (which waits on
   the small library accessor for the predicted-performance export, and on
   the perceptual-verification listening pass that gates `xtc~` itself).
5. **Pd** inherits the browser/WASM widgets as-is (Pd has no v8ui
   equivalent; a browser panel over OSC is the realistic UI story there).

## Open questions

- **C ABI additions for the designers.** `room::for_each_image` and
  `xtc::fir()` (+ a predicted-performance accessor) are not yet exported in
  `tools/capi/`. Small, but they touch the library repo — sequence them
  with wave 4.
- **v8ui bundle format.** Confirm what module/bundle shape Max 9's `v8ui`
  loads most happily (single-file IIFE vs ES module) before settling the
  build tooling.
- **Snapshot bridge in Max — RESOLVED (wave 2).** `ambitap.grid~` (in
  AmbiTap-Max) wraps `analysis::soundfield_grid` with an MC passthrough and
  emits the snapshot as a `grid <rows> <cols> <peak_db> <values...>` list on
  bang (drive from `qmetro`); the v8ui heatmap paints it via the
  Renderer-cell path — no Jitter dependency at `azimuth_steps <= 32`. A
  `jit.matrix` outlet remains the upgrade path if higher grid resolutions
  are ever wanted.
- **Golden-image testing.** Headless-Chromium canvas renders give the
  shared model pixel-level regression tests; decide tolerance strategy
  (perceptual diff) before the widget count grows.
