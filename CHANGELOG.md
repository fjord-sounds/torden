# Changelog

All notable changes to Torden. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] – 2026-10-14
- Built on [fjord-base](https://github.com/fjord-sounds/fjord-base), the code shared by all Fjord plugins: clone it next to
  this repository (or CMake downloads the pinned release). Header, preset bar, browser, controls, theme, the FX page,
  build scripts, CI workflows and the screenshot tooling now come from there.
- **Unified effects**: fifteen effects and the clipper in every Fjord plugin – Saturation, Lo-Fi, Transient, Compressor,
  Squash, EQ, Filter, Motion, Delay, Reverb, Width and the new Tremolo / Auto-Pan, Frequency Shifter, Exciter and
  Harmonizer – with at least 40 presets per effect and 35 effect chains (**FX CHAIN**, **Lock FX**). Existing effects were
  extended (e.g. filter LFO and per-note envelope, delay modes, reverb modes with freeze, EQ slopes, motion modes).
- **Locks**: the padlock in the preset bar keeps layers, kick settings, envelopes, modulation, macros, the step pattern and
  effects when switching presets; panel padlocks on the layer chips, Kick, Macros, Modulation and Step.
- *Window size* in the options menu (75–150 %).
- The FX page is the same in all plugins: live tiles in two rows, big graph, per-effect preset menu.
- Effect parameter ids are now `fx_<effect>_<param>` and the factory presets use them; the effect order is stored as ids.
- Torden's own effect code was replaced by the shared effects (crush became Lo-Fi); the factory presets were converted.
- The screenshot tool reads `tools/screenshots/shots.conf`.
- 100 new factory presets (160 in total, 16 per category), e.g. Amapiano Log, Rawstyle Venom, Tabla Baya, Earthquake.
- Product image at the top of the README.
- Two new categories with 30 presets each, **Pop** and **Tropical** (220 in total): soft, deep four-on-the-floor kicks
  for verses under soft synths, e.g. Velvet Verse, Pillow Talk, Palm Shade, Driftwood.
- The save dialog grows with the number of categories.
- README images refreshed (the browser shows the new Pop category); screenshots ignore user presets.
- The preset generator scripts are no longer part of the repository; the factory presets ship as `.torden` files.

## [0.1.0] – first version
- Kick synthesizer with four layers (body, click, sub, noise), seven multi-point envelopes edited on the waveform,
  fixed or tracked pitch, choke or overlap retrigger.
- Ten reorderable effects plus a clipper, four macros and an eight-slot modulation matrix.
- Step sequencer with velocity, pitch, ratchet, chance and decay per step, host sync and an internal clock.
- Visuals: storm, 3D spectrum waterfall, scope, live effect tiles, lightning level meter, phase scope.
- A/B comparison, mutate, undo / redo, drag-to-DAW WAV export.
- 60 factory presets in ten categories.
- VST3 (macOS, Windows, Linux), Audio Unit (macOS), LV2 (Linux) and a standalone app.
