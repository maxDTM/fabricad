#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check the chair benchmark exports (docs/comparison/CHAIR_SPEC.md) written by tests/chair/chair.fcs.

For every expected file: it exists and is non-empty; the STEP names its three parts and the design
(no OCCT default product name, no leftover LegFrame); for every STL: binary, watertight (after welding
coincident vertices every undirected edge is used by exactly two triangles, once in each direction,
i.e. closed, manifold and consistently oriented), no degenerate triangles, the signed volume is within
0.5 % of the reference and the bounding box is within 0.05 mm of the spec (the mesh vertices lie on the
B-rep, so its box is the B-rep box up to the tessellation of curved extremal faces).

usage: check_stl.py <out-dir> [--weld <mm>]
Only the Python standard library is used. Exit status 0 = all checks passed.
"""
import math
import os
import re
import struct
import sys
from collections import Counter

SEAT, LEFT, RIGHT = 8565976.0, 1718632.0, 1716808.0
BOX = {
    "seat": ((-210, 210), (-198.478, 235.734), (430.0, 840.619)),
    "leg_left": ((-205, -15), (-180, 180), (0, 430)),
    "leg_right": ((15, 205), (-180, 180), (0, 430)),
}
BOX["chair"] = ((-210, 210), (-198.478, 235.734), (0, 840.619))
STLS = {  # file -> (reference volume, expected box, number of closed shells)
    "chair.stl": (SEAT + LEFT + RIGHT, BOX["chair"], 3),
    "chair_default.stl": (SEAT + LEFT + RIGHT, BOX["chair"], 3),
    "seat.stl": (SEAT, BOX["seat"], 1),
    "leg_left.stl": (LEFT, BOX["leg_left"], 1),
    "leg_right.stl": (RIGHT, BOX["leg_right"], 1),
}
OTHER = ["chair.step", "chair.fcad"]
# STEP: every part keeps its name as a PRODUCT (the root product is the design, named after the file),
# LegFrame was deleted, and no part fell back to OCCT's default product name
STEP_PRODUCTS = {"chair", "Seat", "LegLeft", "LegRight"}
STEP_FORBIDDEN = {"LegFrame", "Open CASCADE STEP translator"}
VOL_TOL = 0.005    # 0.5 %
BOX_TOL = 0.05     # mm, for the mesh box


def read_binary_stl(path):
    data = open(path, "rb").read()
    if len(data) < 84:
        raise ValueError("file too short for a binary STL")
    n = struct.unpack_from("<I", data, 80)[0]
    if len(data) != 84 + 50 * n:
        raise ValueError(f"not a binary STL: size {len(data)} != 84 + 50*{n}")
    tris = []
    for i in range(n):
        v = struct.unpack_from("<12f", data, 84 + 50 * i)
        tris.append((v[3:6], v[6:9], v[9:12]))
    return tris


def analyse(tris, weld):
    index = {}
    def vid(p):
        key = tuple(round(c / weld) for c in p) if weld > 0 else tuple(p)
        if key not in index:
            index[key] = len(index)
        return index[key]
    faces, degenerate = [], 0
    vol = 0.0
    lo, hi = [math.inf] * 3, [-math.inf] * 3
    for a, b, c in tris:
        ia, ib, ic = vid(a), vid(b), vid(c)
        if len({ia, ib, ic}) < 3:
            degenerate += 1
            continue
        faces.append((ia, ib, ic))
        vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
        for p in (a, b, c):
            for k in range(3):
                lo[k] = min(lo[k], p[k]); hi[k] = max(hi[k], p[k])
    directed = Counter()
    for ia, ib, ic in faces:
        for e in ((ia, ib), (ib, ic), (ic, ia)):
            directed[e] += 1
    bad = 0
    for (u, v), cnt in directed.items():
        if cnt != 1 or directed.get((v, u), 0) != 1:
            bad += 1
    # closed shells = connected components over shared vertices
    parent = list(range(len(index)))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x
    for ia, ib, ic in faces:
        for j in (ib, ic):
            ra, rj = find(ia), find(j)
            if ra != rj:
                parent[ra] = rj
    used = {i for f in faces for i in f}
    shells = len({find(i) for i in used})
    return dict(triangles=len(tris), vertices=len(used), degenerate=degenerate, bad_edges=bad, volume=vol,
                box=list(zip(lo, hi)), shells=shells)


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    out = argv[1]
    weld = float(argv[argv.index("--weld") + 1]) if "--weld" in argv else 0.0
    failures = 0
    def fail(msg):
        nonlocal failures
        failures += 1
        print("FAIL", msg)
    for name in OTHER + list(STLS):
        p = os.path.join(out, name)
        if not os.path.isfile(p) or os.path.getsize(p) == 0:
            fail(f"{name}: missing or empty")
    step = os.path.join(out, "chair.step")
    if os.path.isfile(step):
        text = open(step, "r", encoding="latin-1").read()
        products = set(re.findall(r"PRODUCT\('([^']*)'", text))
        nauo = re.findall(r"NEXT_ASSEMBLY_USAGE_OCCURRENCE\('[^']*','([^']*)'", text)
        print(f"chair.step: products {sorted(products)}, {len(nauo)} instance(s) {sorted(nauo)}")
        for want in STEP_PRODUCTS:
            if want not in products:
                fail(f"chair.step: no PRODUCT named '{want}'")
        for bad in STEP_FORBIDDEN:
            if any(bad in prod for prod in products):
                fail(f"chair.step: unexpected PRODUCT '{bad}'")
        if sorted(nauo) != ["LegLeft", "LegRight", "Seat"]:
            fail(f"chair.step: instances {sorted(nauo)} != ['LegLeft', 'LegRight', 'Seat']")
    for name, (ref, box, nshell) in STLS.items():
        p = os.path.join(out, name)
        if not os.path.isfile(p):
            continue
        try:
            r = analyse(read_binary_stl(p), weld)
        except ValueError as e:
            fail(f"{name}: {e}"); continue
        dv = (r["volume"] - ref) / ref
        print(f"{name}: {r['triangles']} triangles, {r['vertices']} vertices, {r['shells']} shell(s), "
              f"volume {r['volume']:.1f} (ref {ref:.0f}, {dv * 100:+.4f} %), open/non-manifold edges {r['bad_edges']}, "
              f"degenerate {r['degenerate']}, box " + " ".join(f"[{a:.3f}, {b:.3f}]" for a, b in r["box"]))
        if r["bad_edges"]:
            fail(f"{name}: not watertight ({r['bad_edges']} open, non-manifold or mis-oriented edges)")
        if r["degenerate"]:
            fail(f"{name}: {r['degenerate']} degenerate triangles")
        if r["shells"] != nshell:
            fail(f"{name}: {r['shells']} connected shells, expected {nshell}")
        if abs(dv) > VOL_TOL:
            fail(f"{name}: volume {r['volume']:.1f} off by {dv * 100:.3f} % (> {VOL_TOL * 100} %)")
        for k, ((a, b), (ea, eb)) in enumerate(zip(r["box"], box)):
            if abs(a - ea) > BOX_TOL or abs(b - eb) > BOX_TOL:
                fail(f"{name}: box {'XYZ'[k]} [{a:.4f}, {b:.4f}] != [{ea}, {eb}] (tol {BOX_TOL})")
    print("all export checks passed" if failures == 0 else f"{failures} check(s) failed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
