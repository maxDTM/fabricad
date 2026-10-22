#!/usr/bin/env bash
# Regenerates the README screenshots (docs/screenshots/fabricad-*.png) in the dark and light themes.
#   readme-shots.sh <bin> [outdir=docs/screenshots] [display=93]
# The 3D view needs GL, so the shots run on a private Xwayland display (tools/uitest/ui.sh, its own
# settings and data); the window is grabbed at exactly 1920x1080 by FABRICAD_SCREENSHOT. Scenes:
#   fabricad-extrude   the bracket fixture (tests/e2e/bracket-v3.fcad) editing the boss extrude
#   fabricad-assembly  the chair benchmark (tests/scripts/chair.fcs)
#   fabricad-drawing   the chair as a drawing tab (white paper, black lines in both themes)
#   fabricad-empty     a new empty design
# Dark files have no suffix, light files end in -light.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
bin=$(realpath "${1:?bin}")
out=$(realpath -m "${2:-$root/docs/screenshots}")
n=${3:-93}
mkdir -p "$out"
tmp=$(mktemp -d)
export UI_LOGDIR="$tmp/ui"
trap '"$here/ui.sh" stop "$n" >/dev/null 2>&1 || true; rm -rf "$tmp"' EXIT

# the chair document (the benchmark script also writes its exports into the working directory)
(cd "$tmp" && "$bin" --batch "$root/tests/scripts/chair.fcs" -q >/dev/null 2>&1) || true
[ -f "$tmp/chair.fcad" ] || { echo "chair.fcs did not produce chair.fcad" >&2; exit 1; }

cat >"$tmp/extrude.fcs" <<'S'
ISO
FIT
SELECT feature:Extrude3
EDITFEATURE
S
cat >"$tmp/assembly.fcs" <<'S'
ISO
FIT
SELECT none
S
cat >"$tmp/drawing.fcs" <<'S'
DRAWING
OK
BASEVIEW front 1:5 85,150
OK
BASEVIEW right 1:5 185,150
OK
BASEVIEW iso 1:5 315,135
OK
SELECT none
S
cat >"$tmp/empty.fcs" <<'S'
ISO
S

shoot() {  # <name> <theme> <script> [file]
  local name=$1 theme=$2 script=$3 file=${4:-} suffix=""
  [ "$theme" = light ] && suffix="-light"
  local png="$out/$name$suffix.png"
  "$here/ui.sh" stop "$n" >/dev/null 2>&1 || true
  "$here/ui.sh" reset "$n" >/dev/null
  "$here/ui.sh" prefs "$n" ui/theme="$theme" >/dev/null
  rm -f "$png"
  FABRICAD_WINDOW_SIZE=1920x1080 FABRICAD_SCREENSHOT="$png" FABRICAD_SCREENSHOT_DELAY=${SHOT_DELAY:-9000} \
    UI_ARGS="--script $script --stay $file" "$here/ui.sh" start "$n" "$bin" 1920x1080 >/dev/null
  for _ in $(seq 120); do [ -s "$png" ] && break; sleep 0.25; done
  sleep 0.5
  echo "$png"
}

for theme in dark light; do
  shoot fabricad-extrude "$theme" "$tmp/extrude.fcs" "$root/tests/e2e/bracket-v3.fcad"
  shoot fabricad-assembly "$theme" "$tmp/assembly.fcs" "$tmp/chair.fcad"
  shoot fabricad-drawing "$theme" "$tmp/drawing.fcs" "$tmp/chair.fcad"
  shoot fabricad-empty "$theme" "$tmp/empty.fcs"
done
