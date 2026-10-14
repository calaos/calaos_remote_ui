#!/bin/bash
# Rasterises main/images/src/climate_*.svg into main/images/climate_*.png (white on
# transparent: the widgets recolour them). Needs a chrome-headless-shell binary.
set -e
CHROME="${1:?usage: $0 /path/to/chrome-headless-shell}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for svg in "$ROOT"/main/images/src/climate_*.svg; do
    name="$(basename "$svg" .svg)"
    "$CHROME" --no-sandbox --hide-scrollbars --window-size=26,26 \
        --default-background-color=00000000 \
        --screenshot="$ROOT/main/images/$name.png" "file://$svg" > /dev/null 2>&1
done
