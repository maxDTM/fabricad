#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Re-measures FabriCAD and FreeCAD on the chair benchmark, same machine, interleaved runs.

usage: bench.py <fabricad binary> <FreeCAD squashfs-root> <scratch dir> [runs=3] > bench.json

Nothing is written into docs/comparison: FabriCAD runs in <scratch>/fab (relative out/ paths),
FreeCAD writes to CHAIR_OUT=<scratch>/fc/out with FREECAD_USER_HOME=<scratch>/fc/home.
"""
import json, os, re, shutil, statistics, subprocess, sys, time

fab, fcroot, scratch = sys.argv[1:4]
runs = int(sys.argv[4]) if len(sys.argv) > 4 else 3
here = os.path.dirname(os.path.abspath(__file__))
cmp_dir = os.path.dirname(here)
fab_dir = os.path.join(cmp_dir, "fabricad")
fc_dir = os.path.join(cmp_dir, "freecad")
fabw = os.path.join(scratch, "fab"); fcw = os.path.join(scratch, "fc")
for d in (fabw, fcw):
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
os.makedirs(os.path.join(fcw, "home"))
echo = os.path.join(fabw, "echo.fcs"); open(echo, "w").write("ECHO hi\n")
empty = os.path.join(fcw, "empty.py"); open(empty, "w").close()

fab_env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
fc_env = dict(os.environ, FREECAD_USER_HOME=os.path.join(fcw, "home"), CHAIR_OUT=os.path.join(fcw, "out"))
apprun = os.path.join(fcroot, "AppRun")
gui_env = dict(fc_env, PREFIX=os.path.join(fcroot, "usr"), PYTHONHOME=os.path.join(fcroot, "usr"),
               PATH_TO_FREECAD_LIBDIR=os.path.join(fcroot, "usr", "lib"),
               FONTCONFIG_FILE="/etc/fonts/fonts.conf", FONTCONFIG_PATH="/etc/fonts",
               SSL_CERT_FILE=os.path.join(fcroot, "usr", "ssl", "cacert.pem"), QT_QPA_PLATFORM="offscreen",
               GUI_READY_FILE=os.path.join(fcw, "gui_ready.txt"))
gui_env.pop("DISPLAY", None); gui_env.pop("WAYLAND_DISPLAY", None)


def timed(cmd, env, cwd):
    t = time.perf_counter()
    p = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, timeout=600)
    return time.perf_counter() - t, p


def fab_script(name):
    out = os.path.join(fabw, "out"); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
    log = os.path.join(fabw, name + ".log")
    dt, p = timed([fab, "--script", os.path.join(fab_dir, name), "--offscreen", "--quiet", "--log", log], fab_env, fabw)
    txt = open(log).read() if os.path.exists(log) else ""
    m = re.search(r"# (\d+) commands, (\d+) ok, (\d+) failed, ([\d.]+) ms total", txt)
    return {"wall_s": dt, "rc": p.returncode, "commands": int(m.group(1)) if m else None,
            "failed": int(m.group(3)) if m else None, "in_app_s": float(m.group(4)) / 1000 if m else None}


def fc_script():
    shutil.rmtree(os.path.join(fcw, "out"), ignore_errors=True)
    dt, p = timed([apprun, "freecadcmd", os.path.join(fc_dir, "chair_freecad.py")], fc_env, fcw)
    txt = p.stdout + p.stderr
    open(os.path.join(fcw, "last_run.log"), "w").write(txt)
    m = re.search(r"SCRIPT_WALL ([\d.]+)", txt)
    return {"wall_s": dt, "rc": p.returncode, "in_script_s": float(m.group(1)) if m else None}


def fc_gui():
    mk = gui_env["GUI_READY_FILE"]
    if os.path.exists(mk): os.remove(mk)
    dt, p = timed([os.path.join(fcroot, "usr", "bin", "freecad"), os.path.join(fc_dir, "gui_ready.py")], gui_env, fcw)
    ok = os.path.exists(mk) and "GUI_READY" in open(mk).read()
    return {"s": dt, "ready": ok, "rc": p.returncode}


res = {k: [] for k in ("fab_startup", "fc_cmd_startup", "fc_gui_startup", "fab_chair", "fab_chair_min", "fc_chair")}
res["load_before"] = os.getloadavg()
for i in range(runs):
    res["fab_startup"].append(timed([fab, "--script", echo, "--offscreen", "--quiet"], fab_env, fabw)[0])
    res["fc_cmd_startup"].append(timed([apprun, "freecadcmd", empty], fc_env, fcw)[0])
    res["fc_gui_startup"].append(fc_gui())
    res["fab_chair"].append(fab_script("chair.fcs"))
    res["fab_chair_min"].append(fab_script("chair_min.fcs"))
    res["fc_chair"].append(fc_script())
res["load_after"] = os.getloadavg()
med = lambda xs: statistics.median(xs)
res["median"] = {
    "fab_startup_s": med(res["fab_startup"]),
    "fc_cmd_startup_s": med(res["fc_cmd_startup"]),
    "fc_gui_startup_s": med([g["s"] for g in res["fc_gui_startup"] if g["ready"]]),
    "fab_chair_wall_s": med([r["wall_s"] for r in res["fab_chair"]]),
    "fab_chair_in_app_s": med([r["in_app_s"] for r in res["fab_chair"]]),
    "fab_chair_min_wall_s": med([r["wall_s"] for r in res["fab_chair_min"]]),
    "fab_chair_min_in_app_s": med([r["in_app_s"] for r in res["fab_chair_min"]]),
    "fc_chair_wall_s": med([r["wall_s"] for r in res["fc_chair"]]),
    "fc_chair_in_script_s": med([r["in_script_s"] for r in res["fc_chair"]]),
}
print(json.dumps(res, indent=2))
