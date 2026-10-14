# Contributing to Torden

Thanks for your interest! Torden is a focused kick designer – ideas that make kicks better are very welcome.

By contributing you agree that your work is licensed under the [AGPL-3.0](LICENSE), like the rest of the project.

## Build

```bash
git clone https://github.com/fjord-sounds/fjord-base.git      # shared code, must sit next to torden
git clone https://github.com/fjord-sounds/torden.git && cd torden
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTORDEN_COPY_PLUGIN=OFF
cmake --build build -j8
cmake --build build --target TordenRenderTest -j8
./build/TordenRenderTest_artefacts/Release/TordenRenderTest build/renders      # must print ALL PASSED
```

Torden is built on [fjord-base](https://github.com/fjord-sounds/fjord-base) (shared UI, effects, build helpers): clone it next to this
repository, otherwise CMake downloads the pinned version (`FJORD_BASE_TAG`). JUCE 8 comes through fjord-base. Changes to the
effects, header, preset browser or controls belong in fjord-base ([docs](https://github.com/fjord-sounds/fjord-base/tree/main/docs)).
`./build.sh` is the macOS shortcut that also installs the plugins and runs `auval`.

## Rules that keep old projects and presets working

- All parameters are listed once in `Source/Parameters.h`. IDs never change; new parameters go to the **end** of the
  list (their position defines the modulation target index) and need a neutral default.
- Choice lists are append-only. The effect order (`fxOrder`) stores effect ids; the effect parameters (`fx_*`) belong to fjord-base.
- Envelopes are stored by name (`body_amp`, `pitch`, …); the step pattern as text. Missing ones fall back to defaults.
- If a change needs old project state to be converted, raise `kStateVersion` and add the step to `migrateState()`
  in `Source/PluginProcessor.cpp`.
- The audio thread never allocates or locks.

## Presets

Factory presets live in `presets/Factory/<Category>/*.torden` (one idea each). New presets should be loudness matched
to the others (about −15 dB RMS over the first 400 ms).

## Pull requests

- Keep them focused. Say what changes and why.
- UI changes: attach screenshots in the light **and** the dark theme (`TordenRenderTest --screenshot …`).
- Update the README and [CHANGELOG.md](CHANGELOG.md) (section "Unreleased"), and refresh the README images
  (`tools/screenshots/shoot.sh`) when the UI or the presets change.
- Everything in the repository is English.
