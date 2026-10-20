#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Per-stage FabriCAD time from a chair.fcs log: sums the per-command times between the
'# ---------- <stage> ----------' markers of chair.fcs, with and without the CHECK/INFO/LIST/OPTIONS lines.
usage: fab_stages.py chair.fcs chair.log"""
import json, re, sys
fcs, log = sys.argv[1:3]
bounds = []
for i, l in enumerate(open(fcs), 1):
    m = re.match(r"# -+ (.*?) -+\s*$", l)
    if m: bounds.append((i, m.group(1)))
def stage(n):
    s = None
    for i, name in bounds:
        if n >= i: s = name
    return s
out = {}
for l in open(log):
    m = re.match(r"\s*(\d+)\s+(ok|FAIL\w*)\s+(\S+).*?([\d.]+) ms$", l)
    if not m: continue
    st = stage(int(m.group(1)))
    d = out.setdefault(st, {"commands": 0, "ms": 0.0, "modelling_commands": 0, "modelling_ms": 0.0})
    ms = float(m.group(4)); d["commands"] += 1; d["ms"] += ms
    if m.group(3).upper() not in ("CHECK", "INFO", "LIST", "OPTIONS", "ECHO"):
        d["modelling_commands"] += 1; d["modelling_ms"] += ms
for d in out.values(): d["ms"] = round(d["ms"], 1); d["modelling_ms"] = round(d["modelling_ms"], 1)
print(json.dumps(out, indent=1))
