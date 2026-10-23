#!/bin/sh
# Render the FabriCAD application icon (fabricad-app.svg, next to this script) into a hicolor
# icon theme tree:  <out>/hicolor/<N>x<N>/apps/fabricad.png  for N in 16 22 24 32 48 64 128 256 512,
# plus  <out>/hicolor/scalable/apps/fabricad.svg  (a copy of the SVG).
#
# Usage: render_app_icons.sh [OUT_DIR]   (default: ./icons-out)
# Needs rsvg-convert (librsvg); falls back to ImageMagick (magick / convert) if it is missing.
# Not wired into CMake: packaging (install rules, .desktop file) references this script.
# SPDX-License-Identifier: LGPL-2.1-or-later
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/fabricad-app.svg"
OUT=${1:-./icons-out}
NAME=${FABRICAD_ICON_NAME:-fabricad}
SIZES="16 22 24 32 48 64 128 256 512"

[ -f "$SRC" ] || { echo "missing $SRC" >&2; exit 1; }

render() {  # size out.png
    if command -v rsvg-convert >/dev/null 2>&1; then
        rsvg-convert -w "$1" -h "$1" -o "$2" "$SRC"
    elif command -v magick >/dev/null 2>&1; then
        magick -background none -density 384 "$SRC" -resize "$1x$1" "$2"
    elif command -v convert >/dev/null 2>&1; then
        convert -background none -density 384 "$SRC" -resize "$1x$1" "$2"
    else
        echo "need rsvg-convert or ImageMagick" >&2; exit 1
    fi
}

for s in $SIZES; do
    d="$OUT/hicolor/${s}x${s}/apps"
    mkdir -p "$d"
    render "$s" "$d/$NAME.png"
    echo "$d/$NAME.png"
done
mkdir -p "$OUT/hicolor/scalable/apps"
cp "$SRC" "$OUT/hicolor/scalable/apps/$NAME.svg"
echo "$OUT/hicolor/scalable/apps/$NAME.svg"
