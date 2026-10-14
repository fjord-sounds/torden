#!/usr/bin/env bash
# README images: the shots listed in shots.conf in both themes, then the product image (hero.mjs).
# The work is done by fjord-base (next to this repository).
# Usage: tools/screenshots/shoot.sh      (first time: cd tools/screenshots && npm install)
set -euo pipefail
cd "$(dirname "$0")/../.."
BASE="${FJORD_BASE_DIR:-../fjord-base}"
exec "$BASE/tools/screenshots/shoot.sh" "$PWD"
