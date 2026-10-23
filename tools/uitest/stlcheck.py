#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check one binary STL against reference numbers (docs/plans/tutorial.md 5.8, the bracket tour).

The mesh must be binary, watertight and consistently oriented (after welding coincident vertices every
undirected edge is used by exactly two triangles, once in each direction), free of degenerate triangles,
with the expected number of shells, a signed volume within --vol-tol of --volume and a bounding box within
--box-tol mm of --box. The STL reader and the analysis are the chair benchmark's (tests/chair/check_stl.py),
shared here so the tour runner and the headless twin use one implementation.

usage: stlcheck.py file.stl --volume 66901.359 --box -40,40,-25,25,0,50 [--shells 1] [--vol-tol 0.003]
                   [--box-tol 0.05] [--weld 0]
Only the Python standard library is used. Exit status 0 = all checks passed.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests", "chair"))
from check_stl import analyse, read_binary_stl  # noqa: E402


def check(path, volume, box, shells=1, vol_tol=0.003, box_tol=0.05, weld=0.0):
    """Returns (list of failure strings, analysis dict or None)."""
    try:
        r = analyse(read_binary_stl(path), weld)
    except (OSError, ValueError) as e:
        return ["%s: %s" % (path, e)], None
    fails = []
    if r["bad_edges"]:
        fails.append("not watertight (%d open, non-manifold or mis-oriented edges)" % r["bad_edges"])
    if r["degenerate"]:
        fails.append("%d degenerate triangles" % r["degenerate"])
    if r["shells"] != shells:
        fails.append("%d shells, expected %d" % (r["shells"], shells))
    dv = (r["volume"] - volume) / volume
    if abs(dv) > vol_tol:
        fails.append("volume %.3f off by %.3f %% (> %.2f %%)" % (r["volume"], dv * 100, vol_tol * 100))
    for k, ((a, b), (ea, eb)) in enumerate(zip(r["box"], box)):
        if abs(a - ea) > box_tol or abs(b - eb) > box_tol:
            fails.append("box %s [%.4f, %.4f] != [%g, %g] (tol %g)" % ("XYZ"[k], a, b, ea, eb, box_tol))
    return fails, r


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("stl")
    ap.add_argument("--volume", type=float, required=True)
    ap.add_argument("--box", required=True, help="xmin,xmax,ymin,ymax,zmin,zmax")
    ap.add_argument("--shells", type=int, default=1)
    ap.add_argument("--vol-tol", type=float, default=0.003)
    ap.add_argument("--box-tol", type=float, default=0.05)
    ap.add_argument("--weld", type=float, default=0.0)
    a = ap.parse_args()
    b = [float(x) for x in a.box.split(",")]
    box = [(b[0], b[1]), (b[2], b[3]), (b[4], b[5])]
    fails, r = check(a.stl, a.volume, box, a.shells, a.vol_tol, a.box_tol, a.weld)
    if r:
        print("%s: %d triangles, %d shell(s), volume %.3f (ref %.3f), box %s" % (
            os.path.basename(a.stl), r["triangles"], r["shells"], r["volume"], a.volume,
            " ".join("[%.3f, %.3f]" % ab for ab in r["box"])))
    for f in fails:
        print("FAIL", f)
    print("STL checks passed" if not fails else "%d STL check(s) failed" % len(fails))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
