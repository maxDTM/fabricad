#!/usr/bin/env bash
# FabriCAD UI test harness: a private rootful Xwayland display per tester,
# driven only with real X input events (xdotool) and observed via screenshots.
# See tools/uitest/README.md.
#
#   ui.sh start <N> [bin|-] [WxH] [scale]   start display :N (WxH, default 1600x1000) if needed and launch
#                                           FabriCAD on it with QT_SCALE_FACTOR=scale (default 1)
#   ui.sh prefs <N> key=value ...           write QSettings for display :N before `start`
#                                           (e.g. ui/theme=light ui/cardPlacement=sidebar)
#   ui.sh reset <N>                         forget display :N's settings and data
#   ui.sh shot  <N> <file>                  screenshot of display :N
#   ui.sh geom  <N> <file>                  widget geometry JSON of the running app (for compare.py)
#   ui.sh compare <N> <mockup.svg> [dir]    shot + geom + compare.py (report in dir)
#   ui.sh cvd   <in.png> <out.png> prot|deut|trit|all   colour-vision simulation (cvd.py)
#   ui.sh stop  <N>                         kill FabriCAD and the Xwayland server for display :N
#   ui.sh win   <N>                         print the FabriCAD main window id
#
# Each display has its own settings and data: XDG_CONFIG_HOME=$UI_LOGDIR/cfg-N and
# XDG_DATA_HOME=$UI_LOGDIR/data-N (UI_LOGDIR defaults to /tmp/fabricad-uitest). The binary defaults to
# $UI_BIN or the main checkout's build/bin/fabricad.
#
# Input: use xdotool with DISPLAY=:N, e.g.
#   DISPLAY=:N xdotool mousemove 640 400 click 1
#   DISPLAY=:N xdotool mousemove 900 400 mousedown 2 mousemove 950 420 mouseup 2
#   DISPLAY=:N xdotool key ctrl+z        DISPLAY=:N xdotool type "100"
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cmd=${1:?usage: ui.sh start|prefs|reset|shot|geom|compare|cvd|stop|win ...}

if [ "$cmd" = cvd ]; then
  exec python3 "$here/cvd.py" "${2:?in.png}" "${3:?out.png}" "${4:?prot|deut|trit|all}"
fi

n=${2:?display number}
export DISPLAY=:$n
logdir=${UI_LOGDIR:-/tmp/fabricad-uitest}
cfg="$logdir/cfg-$n"
data="$logdir/data-$n"
geo="$logdir/geo-$n.json"
mkdir -p "$logdir" "$cfg" "$data"

case $cmd in
  start)
    bin=${3:--}
    [ "$bin" = - ] && bin=${UI_BIN:-/home/user/Documents/projects/fabricad/build/bin/fabricad}
    size=${4:-1600x1000}
    scale=${5:-1}
    if ! xdotool getdisplaygeometry >/dev/null 2>&1; then
      setsid Xwayland ":$n" -geometry "$size" >"$logdir/xwayland-$n.log" 2>&1 &
      for _ in $(seq 50); do xdotool getdisplaygeometry >/dev/null 2>&1 && break; sleep 0.2; done
    fi
    pkill -f "fabricad-ui-$n" 2>/dev/null || true
    rm -f "$geo"
    XDG_CONFIG_HOME="$cfg" XDG_DATA_HOME="$data" QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR="$scale" \
      FABRICAD_GEOMETRY_DUMP="$geo" \
      setsid bash -c "exec -a fabricad-ui-$n '$bin'" >"$logdir/fabricad-$n.log" 2>&1 &
    w=
    for _ in $(seq 60); do
      w=$(xdotool search --name FabriCAD 2>/dev/null | head -1 || true)
      [ -n "$w" ] && break; sleep 0.25
    done
    read -r W H < <(xdotool getdisplaygeometry)
    # only our own window on our private display is moved or resized
    if [ -n "$w" ]; then xdotool windowmove "$w" 0 0 windowsize "$w" "$W" "$H" || true; fi
    sleep 1.5
    echo "display :$n ${W}x${H} scale $scale window $w config $cfg"
    [ "${W}x${H}" = "$size" ] || echo "note: display :$n is ${W}x${H}, not $size (a running display keeps its size: ui.sh stop $n first)"
    ;;
  prefs)
    shift 2
    python3 - "$cfg/FabriCAD/FabriCAD.conf" "$@" <<'PY'
import configparser, os, sys
path, pairs = sys.argv[1], sys.argv[2:]
os.makedirs(os.path.dirname(path), exist_ok=True)
cp = configparser.RawConfigParser(strict=False, interpolation=None)
cp.optionxform = str
if os.path.exists(path):
    cp.read(path, encoding="utf-8")
for kv in pairs:
    if "=" not in kv:
        sys.exit("prefs: expected key=value, got %r" % kv)
    key, value = kv.split("=", 1)
    group, _, name = key.rpartition("/")
    group = group or "General"
    if not cp.has_section(group):
        cp.add_section(group)
    cp.set(group, name, value)
with open(path, "w", encoding="utf-8") as f:
    cp.write(f, space_around_delimiters=False)
print(path)
PY
    ;;
  reset) rm -rf "$cfg" "$data" "$geo"; echo "reset display :$n settings and data" ;;
  shot)  import -window root "${3:?file}" ;;
  geom)
    for _ in $(seq 20); do [ -s "$geo" ] && break; sleep 0.25; done
    cp "$geo" "${3:?file}"
    ;;
  compare)
    svg=${3:?mockup.svg}; out=${4:-$logdir/compare-$n}
    mkdir -p "$out"
    import -window root "$out/shot.png"
    sleep 1.1  # the app rewrites the geometry every second
    cp "$geo" "$out/geo.json"
    python3 "$here/compare.py" "$out/shot.png" "$svg" --geometry "$out/geo.json" --report "$out"
    ;;
  win)   xdotool search --name FabriCAD | head -1 ;;
  stop)
    pkill -f "fabricad-ui-$n" 2>/dev/null || true
    pkill -f "Xwayland :$n " 2>/dev/null || true
    ;;
  *) echo "unknown command $cmd" >&2; exit 2 ;;
esac
