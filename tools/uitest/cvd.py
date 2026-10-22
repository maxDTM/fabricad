#!/usr/bin/env python3
"""Colour-vision deficiency simulation for screenshots (docs/plans/ui-redesign.md 4.7, gate G). Stdlib only.

  cvd.py <in.png> <out.png> prot|deut|trit [--severity 0..1]
  cvd.py <in.png> <outdir> all            writes <name>-normal/protan/deutan/tritan.png and a 2x2 sheet

Uses the same Machado, Oliveira and Fernandes (2009) matrices as the theme_tokens ctest
(tools/theme/colormath.py), applied in linear RGB, so a token pair that passes the ctest looks the same
here. Colours are cached per RGB value; a UI screenshot has few distinct colours, so a 1920x1080 shot takes
a few seconds.
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "theme"))

import colormath  # noqa: E402
import pngio  # noqa: E402

KINDS = {"prot": "protan", "protan": "protan", "protanopia": "protan",
         "deut": "deutan", "deutan": "deutan", "deuteranopia": "deutan",
         "trit": "tritan", "tritan": "tritan", "tritanopia": "tritan",
         "normal": "normal"}

_LIN = [colormath.to_linear(i / 255.0) for i in range(256)]


def matrix(kind, severity=1.0):
    if kind == "normal":
        return ((1, 0, 0), (0, 1, 0), (0, 0, 1))
    m = colormath.MACHADO[kind]
    ident = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
    return tuple(tuple(ident[r][c] + severity * (m[r][c] - ident[r][c]) for c in range(3)) for r in range(3))


def simulate_image(img, kind, severity=1.0):
    if kind == "normal":
        return pngio.Image(img.width, img.height, [list(r) for r in img.rows])
    m = matrix(kind, severity)
    cache = {}

    def sim(p):
        q = cache.get(p)
        if q is None:
            lin = (_LIN[p[0]], _LIN[p[1]], _LIN[p[2]])
            q = tuple(int(round(colormath.to_srgb(m[r][0] * lin[0] + m[r][1] * lin[1] + m[r][2] * lin[2]) * 255))
                      for r in range(3))
            cache[p] = q
        return q

    return pngio.Image(img.width, img.height, [[sim(p) for p in row] for row in img.rows])


def sheet(images, cols=2, gap=8, bg=(128, 128, 128)):
    """Tile images (same size) into a grid."""
    w, h = images[0].width, images[0].height
    rows_n = (len(images) + cols - 1) // cols
    W, H = cols * w + (cols + 1) * gap, rows_n * h + (rows_n + 1) * gap
    rows = [[bg] * W for _ in range(H)]
    for k, im in enumerate(images):
        ox = gap + (k % cols) * (w + gap)
        oy = gap + (k // cols) * (h + gap)
        for y in range(h):
            rows[oy + y][ox:ox + w] = im.rows[y]
    return pngio.Image(W, H, rows)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input")
    ap.add_argument("output", help="output PNG, or a directory with kind 'all'")
    ap.add_argument("kind", help="prot | deut | trit | normal | all")
    ap.add_argument("--severity", type=float, default=1.0, help="0..1 (default 1: full dichromacy)")
    a = ap.parse_args(argv)
    img = pngio.read_png(a.input)
    if a.kind == "all":
        os.makedirs(a.output, exist_ok=True)
        base = os.path.splitext(os.path.basename(a.input))[0]
        outs = []
        for kind in colormath.SIMS:
            sim = simulate_image(img, kind, a.severity)
            path = os.path.join(a.output, "%s-%s.png" % (base, kind))
            pngio.write_png(path, sim)
            outs.append(sim)
            print(path)
        path = os.path.join(a.output, "%s-cvd-sheet.png" % base)
        pngio.write_png(path, sheet(outs))
        print(path)
        return 0
    kind = KINDS.get(a.kind)
    if not kind:
        ap.error("unknown kind %r (prot, deut, trit, normal, all)" % a.kind)
    pngio.write_png(a.output, simulate_image(img, kind, a.severity))
    print(a.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
