#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Timing harness for the FreeCAD chair benchmark (system python3, not FreeCAD's).

usage: bench.py <extracted squashfs-root> <AppImage> <scratch dir for FREECAD_USER_HOME>
"""
import os, re, statistics, subprocess, sys, time, json

root, appimage, scratch = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
apprun = os.path.join(root, "AppRun")
env = dict(os.environ, FREECAD_USER_HOME=os.path.join(scratch, "fchome"))
empty = os.path.join(scratch, "empty.py")
open(empty, "w").close()
res = {}


def timed(cmd, e=env, timeout=600, log=None):
    t = time.perf_counter()
    p = subprocess.run(cmd, env=e, cwd=here, capture_output=True, text=True, timeout=timeout)
    dt = time.perf_counter() - t
    if log:
        open(log, "w").write(p.stdout + p.stderr)
    return dt, p


runs = []
for i in range(1, 4):
    for f in os.listdir(os.path.join(here, "out")):
        os.remove(os.path.join(here, "out", f))
    dt, p = timed([apprun, "freecadcmd", os.path.join(here, "chair_freecad.py")],
                  log=os.path.join(here, "logs", f"run{i}.log"))
    m = re.search(r"SCRIPT_WALL ([\d.]+)", p.stdout + p.stderr)
    runs.append({"process_wall_s": dt, "in_script_s": float(m.group(1)) if m else None, "rc": p.returncode})
res["chair_runs"] = runs
res["chair_process_wall_median_s"] = statistics.median(r["process_wall_s"] for r in runs)
res["chair_in_script_median_s"] = statistics.median(r["in_script_s"] for r in runs)

st = [timed([apprun, "freecadcmd", empty])[0] for _ in range(5)]
res["freecadcmd_startup_extracted_s"] = st
res["freecadcmd_startup_extracted_median_s"] = statistics.median(st)
st2 = [timed([appimage, "freecadcmd", empty])[0] for _ in range(3)]
res["freecadcmd_startup_appimage_s"] = st2
res["freecadcmd_startup_appimage_median_s"] = statistics.median(st2)

# GUI startup, offscreen (AppRun forces QT_QPA_PLATFORM=xcb, so call usr/bin/freecad directly
# with AppRun's other environment variables).
genv = dict(env, PREFIX=os.path.join(root, "usr"), PYTHONHOME=os.path.join(root, "usr"),
            PATH_TO_FREECAD_LIBDIR=os.path.join(root, "usr", "lib"),
            FONTCONFIG_FILE="/etc/fonts/fonts.conf", FONTCONFIG_PATH="/etc/fonts",
            SSL_CERT_FILE=os.path.join(root, "usr", "ssl", "cacert.pem"),
            QT_QPA_PLATFORM="offscreen")
# without a display the offscreen plugin skips GLX (with DISPLAY set it aborts: "Could not initialize GLX")
genv.pop("DISPLAY", None)
genv.pop("WAYLAND_DISPLAY", None)
marker = os.path.join(scratch, "gui_ready.txt")
genv["GUI_READY_FILE"] = marker
gui = []
for i in range(3):
    if os.path.exists(marker):
        os.remove(marker)
    try:
        dt, p = timed([os.path.join(root, "usr", "bin", "freecad"), os.path.join(here, "gui_ready.py")],
                      e=genv, timeout=90, log=os.path.join(here, "logs", f"gui_startup{i+1}.log"))
        out = open(marker).read() if os.path.exists(marker) else ""
        gui.append({"s": dt, "rc": p.returncode, "ready": "GUI_READY" in out,
                    "marker": (re.findall(r"GUI_READY.*", out) or [""])[0]})
    except subprocess.TimeoutExpired:
        gui.append({"s": None, "rc": "timeout", "ready": False})
res["gui_startup_offscreen"] = gui
ok = [g["s"] for g in gui if g["ready"]]
res["gui_startup_offscreen_median_s"] = statistics.median(ok) if ok else None
json.dump(res, open(os.path.join(here, "logs", "bench.json"), "w"), indent=2)
print(json.dumps(res, indent=2))
