#!/bin/sh
# Render the hicolor PNG app icons from one SVG.
# Usage: render-icons.sh <icon.svg> <out-dir> [icon-name]
#   writes <out-dir>/<size>x<size>/apps/<icon-name>.png for 16 22 24 32 48 64 128 256 512
#   and <out-dir>/scalable/apps/<icon-name>.svg (a copy of the input).
# Uses rsvg-convert (librsvg), falling back to ImageMagick (magick / convert).
# SPDX-License-Identifier: LGPL-2.1-or-later
set -eu
svg=$1
out=$2
name=${3:-io.github.maxdtm.FabriCAD}
sizes="16 22 24 32 48 64 128 256 512"

render() { # size dst
  if command -v rsvg-convert >/dev/null 2>&1; then
    rsvg-convert -w "$1" -h "$1" -o "$2" "$svg"
  elif command -v magick >/dev/null 2>&1; then
    magick -background none -density 384 "$svg" -resize "$1x$1" "$2"
  elif command -v convert >/dev/null 2>&1; then
    convert -background none -density 384 "$svg" -resize "$1x$1" "$2"
  else
    echo "render-icons.sh: need rsvg-convert or ImageMagick" >&2
    exit 1
  fi
}

mkdir -p "$out/scalable/apps"
cp "$svg" "$out/scalable/apps/$name.svg"
for s in $sizes; do
  mkdir -p "$out/${s}x${s}/apps"
  render "$s" "$out/${s}x${s}/apps/$name.png"
done
