#!/usr/bin/env bash
# Offscreen grabs of the command card (redesign phase 3) at an exact window size, per placement
# and theme, with an isolated config (XDG_CONFIG_HOME). The 3D view is blank offscreen; the
# chrome and the card are real.
#   card-shots.sh <bin> <outdir> [WxH]
set -euo pipefail
bin=${1:?bin}; out=${2:?outdir}; size=${3:-1920x1080}
mkdir -p "$out"
tmp=$(mktemp -d)
cat >"$tmp/extrude.fcs" <<'S'
SKETCH XY
RECT
0,0
40,30
FINISH
SELECT profile:@20,15,0
EXTRUDE 25
S
for theme in dark light; do
  for p in floatRight floatLeft sidebar free; do
    cfg="$tmp/cfg-$theme-$p"; mkdir -p "$cfg/FabriCAD"
    {
      echo "[ui]"
      echo "theme=$theme"
      echo "cardPlacement=$p"
      echo "cardFreePos=floatRight;tl;0.3000;0.1500"
    } >"$cfg/FabriCAD/FabriCAD.conf"
    XDG_CONFIG_HOME="$cfg" QT_QPA_PLATFORM=offscreen FABRICAD_WINDOW_SIZE=$size \
      FABRICAD_SCREENSHOT="$out/offscreen-$theme-$size-$p.png" FABRICAD_SCREENSHOT_DELAY=4000 \
      "$bin" --script "$tmp/extrude.fcs" --stay -q >/dev/null 2>&1 || true
    echo "$out/offscreen-$theme-$size-$p.png"
  done
done
rm -rf "$tmp"
