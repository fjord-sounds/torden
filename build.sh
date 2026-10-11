#!/usr/bin/env bash
# Builds Torden (AU + VST3 + Standalone) without the Xcode IDE, installs it and validates the AU.
set -euo pipefail
cd "$(dirname "$0")"
CONFIG="${1:-Release}"
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build build --config "$CONFIG" -j"$(sysctl -n hw.ncpu)"
# Do NOT restart the macOS audio plugin service by default (it can trigger an audio-device prompt in Logic).
if [[ "${REFRESH_AU:-0}" == "1" ]]; then
  killall -9 AudioComponentRegistrar 2>/dev/null || true
fi
if [[ "${SKIP_AUVAL:-0}" != "1" ]]; then
  auval -v aumu Trdn Fjrd | tail -n 5
fi
echo "Standalone: build/Torden_artefacts/$CONFIG/Standalone/Torden.app"
