#!/usr/bin/env bash
# Builds Torden (AU + VST3 + Standalone) without the Xcode IDE, installs it and validates the AU.
# The work is done by fjord-base (next to this repository). SKIP_AUVAL=1 / REFRESH_AU=1 see there.
set -euo pipefail
cd "$(dirname "$0")"
BASE="${FJORD_BASE_DIR:-../fjord-base}"
[[ -x "$BASE/tools/build-plugin.sh" ]] || { echo "fjord-base not found – clone it next to this repository: git clone https://github.com/fjord-sounds/fjord-base ../fjord-base"; exit 1; }
"$BASE/tools/build-plugin.sh" "$PWD" Torden aumu Trdn "${1:-Release}"
