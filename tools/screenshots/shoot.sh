#!/usr/bin/env bash
# README images: every page in the light and the dark theme, rendered offscreen by TordenRenderTest and compressed
# to WebP (cwebp), then the product image on top of the README (hero.mjs, Playwright).
# Usage: tools/screenshots/shoot.sh      (first time: cd tools/screenshots && npm install)
set -euo pipefail
cd "$(dirname "$0")/../.."
TOOL=build/TordenRenderTest_artefacts/Release/TordenRenderTest
OUT=docs/screenshots
RAW=build/screenshots/raw
mkdir -p "$OUT" "$RAW"

# id  page  preset  [fx index]
SHOTS=(
  "kick|kick|Thunder Hammer|"
  "layers|layers|Studio 22|"
  "fx|fx|Warehouse Rumble|4"
  "mod|mod|Glitch Hit|"
  "step|step|Glitch Hit|"
  "browser|browser|Velvet Verse|"
)

for shot in "${SHOTS[@]}"; do
  IFS='|' read -r id page preset fx <<< "$shot"
  for theme in light dark; do
    "$TOOL" --screenshot "$page" "$preset" "$theme" "$RAW/$id-$theme.png" ${fx:+"$fx"}
    cwebp -quiet -q 86 -resize 1650 0 "$RAW/$id-$theme.png" -o "$OUT/$id-$theme.webp"
  done
  echo "shot  $id"
done

node tools/screenshots/hero.mjs
