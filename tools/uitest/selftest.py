#!/usr/bin/env python3
"""Self-test of the stdlib tools in tools/uitest (ctest uitest_tools): PNG round trip, CVD simulation
against tools/theme/colormath.py, and compare.py on the token mockup against itself.
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "theme"))

import colormath  # noqa: E402
import compare  # noqa: E402
import cvd  # noqa: E402
import pngio  # noqa: E402


def check(cond, msg):
    if not cond:
        print("FAIL: " + msg)
        sys.exit(1)


def main():
    tmp = tempfile.mkdtemp()
    colours = [(245, 183, 0), (242, 84, 45), (255, 92, 138), (139, 92, 246), (16, 18, 23), (255, 255, 255)]
    img = pngio.Image(len(colours), 2, [list(colours), list(reversed(colours))])
    path = os.path.join(tmp, "t.png")
    pngio.write_png(path, img)
    back = pngio.read_png(path)
    check(back.rows == img.rows, "PNG round trip")
    for kind in ("protan", "deutan", "tritan"):
        sim = cvd.simulate_image(back, kind)
        for p, q in zip(back.rows[0], sim.rows[0]):
            ref = colormath.simulate(tuple(v / 255.0 for v in p) + (1.0,), kind)
            check(all(abs(round(ref[i] * 255) - q[i]) <= 1 for i in range(3)), "cvd %s %s" % (kind, p))
    # the mockup PNG is the mockup SVG's raster: SSIM 1 and dE 0 in every region
    svg = os.path.join(ROOT, "docs", "mockups", "custom-ui-extrude-v6-tokens-dark.svg")
    out = os.path.join(tmp, "r.json")
    code = compare.main([os.path.splitext(svg)[0] + ".png", svg, "--only", "AppBar,OkButton,StatusBar", "--json", out])
    check(code == 0, "compare exit status")
    import json
    with open(out) as f:
        regions = json.load(f)["regions"]
    check(len(regions) == 3, "three regions")
    for r in regions:
        check(r["colour"]["dE00"] == 0 and r["ssim"] > 0.999, "self-compare " + r["id"])
    print("uitest tools ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
