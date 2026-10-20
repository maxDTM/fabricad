#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Runs stlcheck on every STL of both apps (against the app's own chair.step and, as a cross-check,
against the other app's), merges the timing results and writes docs/comparison/metrics.json.

usage: collect_metrics.py <stlcheck binary> <bench.json from bench.py> <fab_stages.json> <freecad run log>
Raw per-file results go to docs/comparison/measurements/."""
import json, os, re, subprocess, sys

stlcheck, bench_p, stages_p, fclog_p = sys.argv[1:5]
here = os.path.dirname(os.path.abspath(__file__))
cmp_dir = os.path.dirname(here)
meas = os.path.join(cmp_dir, "measurements"); os.makedirs(os.path.join(meas, "stl"), exist_ok=True)
APPS = ["fabricad", "freecad"]
FILES = ["chair", "seat", "leg_left", "leg_right", "chair_default"]
REF_VOL = {"Seat": 8565975.690, "LegLeft": 1718632.000, "LegRight": 1716808.259}
REF_VOL["chair"] = sum(REF_VOL.values())
REF_BOX = {"Seat": [-210, -198.478, 430.000, 210, 235.734, 840.619],
           "LegLeft": [-205, -180, 0, -15, 180, 430], "LegRight": [15, -180, 0, 205, 180, 430]}
REF_BOX["chair"] = [-210, -198.478, 0, 210, 235.734, 840.619]
PART = {"chair": "chair", "chair_default": "chair", "seat": "Seat", "leg_left": "LegLeft", "leg_right": "LegRight"}
SETTINGS = {"fabricad": {"matched": "linear 0.05 mm absolute, angular 0.2 rad, relative off, binary",
                         "default": "linear 0.05 mm absolute, angular 20 deg (0.349 rad), relative off, binary"},
            "freecad": {"matched": "linear 0.05 mm absolute, angular 0.2 rad, relative off, binary",
                        "default": "linear 0.1 mm absolute, angular 0.1 rad, relative off, binary"}}


def run(stl, step):
    out = subprocess.run([stlcheck, stl, step, "4", "6"], capture_output=True, text=True, check=True).stdout
    d = json.loads(out)
    d["file"] = os.path.relpath(stl, cmp_dir); d["reference"] = os.path.relpath(step, cmp_dir)
    return d


stl = {}
for app in APPS:
    other = APPS[1 - APPS.index(app)]
    for f in FILES:
        p = os.path.join(cmp_dir, app, "out", f + ".stl")
        own = run(p, os.path.join(cmp_dir, app, "out", "chair.step"))
        cross = run(p, os.path.join(cmp_dir, other, "out", "chair.step"))
        json.dump(own, open(os.path.join(meas, "stl", f"{app}_{f}.json"), "w"), indent=1)
        json.dump(cross, open(os.path.join(meas, "stl", f"{app}_{f}_vs_{other}_step.json"), "w"), indent=1)
        part = PART[f]
        box_dev = max(abs(a - b) for a, b in zip(own["bbox"], REF_BOX[part]))
        stl.setdefault(app, {})[f] = {
            "setting": SETTINGS[app]["default" if f == "chair_default" else "matched"],
            "binary": own["binary"], "bytes": own["bytes"], "triangles": own["triangles"], "vertices": own["vertices"],
            "shells": len(own["shells"]), "genus_per_shell": [s["genus"] for s in own["shells"]],
            "watertight": own["watertight"], "open_edges": own["open_edges"], "nonmanifold_edges": own["nonmanifold_edges"],
            "inconsistent_winding_edges": own["inconsistent_winding_edges"],
            "normals_outward": all(s["outward"] for s in own["shells"]),
            "stored_normals_flipped": own["stored_normals_flipped"], "degenerate_triangles": own["degenerate"],
            "volume_mm3": round(own["volume"], 3), "reference_volume_mm3": REF_VOL[part],
            "volume_dev_pct": round(100 * (own["volume"] - REF_VOL[part]) / REF_VOL[part], 5),
            "bbox": [round(x, 4) for x in own["bbox"]], "bbox_max_dev_vs_spec_mm": round(box_dev, 4),
            "chordal_dev_max_mm": round(own["deviation"]["max"], 5),
            "chordal_dev_mean_mm": round(own["deviation"]["mean"], 5),
            "chordal_dev_rms_mm": round(own["deviation"]["rms"], 5),
            "chordal_dev_max_surface": own["deviation"]["max_surface"],
            "chordal_dev_max_by_surface_mm": {k: round(v, 5) for k, v in own["deviation"]["max_by_surface"].items()},
            "vertex_dev_max_mm": own["deviation"]["max_vertex"],
            "chordal_dev_max_vs_other_app_step_mm": round(cross["deviation"]["max"], 5),
            "samples": own["deviation"]["samples"],
        }

bench = json.load(open(bench_p))
stages = json.load(open(stages_p))
fc_stage = json.loads(re.search(r"STAGE_TIMES (\{.*\})", open(fclog_p).read()).group(1).replace("'", '"'))
json.dump(bench, open(os.path.join(meas, "bench.json"), "w"), indent=1)
json.dump(stages, open(os.path.join(meas, "fabricad_stages.json"), "w"), indent=1)
json.dump(fc_stage, open(os.path.join(meas, "freecad_stages.json"), "w"), indent=1)

r3 = lambda x: round(x, 3)
metrics = {
    "spec": "docs/comparison/CHAIR_SPEC.md",
    "date": "2026-10-20",
    "machine": "Arch Linux, 8 threads, 7.7 GB RAM + zram, other agents running (load avg %.2f before / %.2f after)" % (bench["load_before"][0], bench["load_after"][0]),
    "versions": {"fabricad": "main @ b246294 (Release build, binary 2026-10-20 12:54), OCCT 7.9.3",
                 "freecad": "1.1.4 AppImage (extracted, AppRun), OCCT 7.8.1"},
    "speed": {
        "method": "tools/bench.py, %d interleaved runs per item, median; FabriCAD --offscreen in a scratch dir, FreeCAD freecadcmd with FREECAD_USER_HOME and CHAIR_OUT in a scratch dir" % len(bench["fab_chair"]),
        "fabricad": {
            "startup_shutdown_s": r3(bench["median"]["fab_startup_s"]), "startup_runs_s": [r3(x) for x in bench["fab_startup"]],
            "startup_note": "script mode always builds and shows the full MainWindow (no 3D view offscreen); there is no separate non-GUI runner",
            "chair_fcs_wall_s": r3(bench["median"]["fab_chair_wall_s"]), "chair_fcs_runs_s": [r3(r["wall_s"]) for r in bench["fab_chair"]],
            "chair_fcs_in_app_s": r3(bench["median"]["fab_chair_in_app_s"]),
            "chair_min_wall_s": r3(bench["median"]["fab_chair_min_wall_s"]), "chair_min_runs_s": [r3(r["wall_s"]) for r in bench["fab_chair_min"]],
            "chair_min_in_app_s": r3(bench["median"]["fab_chair_min_in_app_s"]),
            "failed_commands": sum(r["failed"] for r in bench["fab_chair"] + bench["fab_chair_min"]),
            "stages_ms_chair_fcs_last_run": {k: v["modelling_ms"] for k, v in stages.items()},
        },
        "freecad": {
            "freecadcmd_startup_s": r3(bench["median"]["fc_cmd_startup_s"]), "freecadcmd_startup_runs_s": [r3(x) for x in bench["fc_cmd_startup"]],
            "gui_startup_offscreen_s": r3(bench["median"]["fc_gui_startup_s"]), "gui_startup_runs_s": [r3(g["s"]) for g in bench["fc_gui_startup"]],
            "chair_script_wall_s": r3(bench["median"]["fc_chair_wall_s"]), "chair_script_runs_s": [r3(r["wall_s"]) for r in bench["fc_chair"]],
            "chair_in_script_s": r3(bench["median"]["fc_chair_in_script_s"]),
            "stages_ms_last_run": {k: round(v * 1000, 1) for k, v in fc_stage.items()},
            "module_imports_s": r3(bench["fc_chair"][-1]["in_script_s"] - sum(fc_stage.values())),
        },
    },
    "stl": stl,
}
metrics["ease"] = json.load(open(os.path.join(meas, "ease.json")))
json.dump(metrics, open(os.path.join(cmp_dir, "metrics.json"), "w"), indent=1)
print("ok")
