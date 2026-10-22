#!/usr/bin/env bash
# FabriCAD UI test harness: a private rootful Xwayland display per tester,
# driven only with real X input events (xdotool) and observed via screenshots.
#
#   ui.sh start <N> [bin]   start display :N (if needed) and launch FabriCAD on it
#   ui.sh shot  <N> <file>  screenshot of display :N
#   ui.sh stop  <N>         kill FabriCAD and the Xwayland server for display :N
#   ui.sh win   <N>         print the FabriCAD main window id
#
# Input: use xdotool with DISPLAY=:N, e.g.
#   DISPLAY=:N xdotool mousemove 640 400 click 1
#   DISPLAY=:N xdotool mousemove 900 400 mousedown 2 mousemove 950 420 mouseup 2
#   DISPLAY=:N xdotool key ctrl+z        DISPLAY=:N xdotool type "100"
set -euo pipefail
cmd=${1:?usage}; n=${2:?display number}
bin=${3:-/home/user/Documents/projects/fabricad/build/bin/fabricad}
export DISPLAY=:$n
logdir=${UI_LOGDIR:-/tmp/fabricad-uitest}
mkdir -p "$logdir"

case $cmd in
  start)
    if ! xdotool getdisplaygeometry >/dev/null 2>&1; then
      setsid Xwayland ":$n" -geometry 1600x1000 >"$logdir/xwayland-$n.log" 2>&1 &
      for _ in $(seq 50); do xdotool getdisplaygeometry >/dev/null 2>&1 && break; sleep 0.2; done
    fi
    pkill -f "fabricad-ui-$n" 2>/dev/null || true
    QT_QPA_PLATFORM=xcb setsid bash -c "exec -a fabricad-ui-$n '$bin'" >"$logdir/fabricad-$n.log" 2>&1 &
    for _ in $(seq 60); do
      w=$(xdotool search --name FabriCAD 2>/dev/null | head -1 || true)
      [ -n "$w" ] && break; sleep 0.25
    done
    read -r W H < <(xdotool getdisplaygeometry)
    xdotool windowmove "$w" 0 0 windowsize "$w" "$W" "$H" || true
    sleep 1.5
    echo "display :$n ${W}x${H} window $w"
    ;;
  shot)  import -window root "${3:?file}" ;;
  win)   xdotool search --name FabriCAD | head -1 ;;
  stop)
    pkill -f "fabricad-ui-$n" 2>/dev/null || true
    pkill -f "Xwayland :$n " 2>/dev/null || true
    ;;
  *) echo "unknown command $cmd" >&2; exit 2 ;;
esac
