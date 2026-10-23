#!/usr/bin/env python3
"""Compare a FabriCAD screenshot with a token mockup, region by region (docs/plans/ui-redesign.md 8.3). Stdlib only.

  compare.py <shot.png> <mockup.svg> [--geometry geo.json] [--report DIR] [--tol 4] [--only ID,ID]

Regions are the <rect data-region="ID" x= y= width= height=> boxes in the mockup SVG (group id
"data-regions"). For every region:
  layout  with --geometry (FABRICAD_GEOMETRY_DUMP from the app, or `ui.sh geom`): the widget box for the
          region must match the mockup box within --tol px on every edge (gating: exit 1 on a miss).
          The command card's widget carries an 8 px bleed for its shadow; --inset ID=PX sets such insets.
  colour  dominant colour of the region in the shot vs the mockup PNG, as CIEDE2000 (reported; > 3 flagged).
          <circle data-sample="token.name" cx= cy=> points in the SVG, if any, are sampled too.
  ssim    mean SSIM of the luminance over 8x8 blocks (reported, never gating).
The mockup raster is <mockup>.png next to the SVG; it is current when mockup-png.sha256 lists the SVG's
content hash (else rsvg-convert renders a temporary one; --refresh-mockup re-renders it in place). A shot taken at
QT_SCALE_FACTOR s (device px = s x logical) is compared with the regions scaled by s; geometry stays logical.
--report DIR writes index.html with side-by-side crops of every region.

Widget lookup for a region ID: objectName == ID; "RibbonGroup:NAME" = a RibbonGroup whose toolbarGroup
or section is NAME; ViewToolColumn = property overlay; StatusBar = QStatusBar; Viewer = the ViewerWidget.
A rect may name another widget with data-widget="objectName" (FeatureList -> SidebarPanel_features: the
mockup box includes the panel header).
data-gate on a rect says which edges gate the layout check: "ltrb" (default, every edge), a subset such as
"ltb" (the right edge follows content: crumbs, tab count, card body rows), or "none" (reported only: the box
depends on the scene, such as the value box at the 3D handle, or has no widget). data-why says why.
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import argparse
import hashlib
import html
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "theme"))

import colormath  # noqa: E402
import pngio  # noqa: E402

# Widgets whose visible box is inset from the widget rect (CardFrame::kBleed: shadow and brackets).
INSETS = {"CommandDialog": 8}

RECT_RX = re.compile(r"<rect\b[^>]*\bdata-region=\"([^\"]+)\"[^>]*>")
SAMPLE_RX = re.compile(r"<circle\b[^>]*\bdata-sample=\"([^\"]+)\"[^>]*>")


def attr(tag, name):
    m = re.search(r"\b%s=\"([^\"]*)\"" % name, tag)
    return float(m.group(1)) if m else 0.0


def read_regions(svg_text):
    regions = []
    for m in RECT_RX.finditer(svg_text):
        tag = m.group(0)
        gate = re.search(r"\bdata-gate=\"([^\"]*)\"", tag)
        widget = re.search(r"\bdata-widget=\"([^\"]*)\"", tag)
        g = gate.group(1) if gate else "ltrb"
        regions.append({"id": m.group(1), "x": attr(tag, "x"), "y": attr(tag, "y"),
                        "w": attr(tag, "width"), "h": attr(tag, "height"),
                        "gate": "" if g == "none" else g, "widget": widget.group(1) if widget else None})
    samples = [{"token": m.group(1), "x": attr(m.group(0), "cx"), "y": attr(m.group(0), "cy")}
               for m in SAMPLE_RX.finditer(svg_text)]
    return regions, samples


HASH_INDEX = "mockup-png.sha256"  # next to the mockups: "<sha256 of the SVG>  <name>.svg" per current PNG


def svg_digest(svg_path):
    with open(svg_path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def read_hash_index(mockup_dir):
    idx = {}
    try:
        with open(os.path.join(mockup_dir, HASH_INDEX), encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) == 2 and not line.startswith("#"):
                    idx[parts[1]] = parts[0]
    except FileNotFoundError:
        pass
    return idx


def write_hash_index(mockup_dir, idx):
    with open(os.path.join(mockup_dir, HASH_INDEX), "w", encoding="utf-8") as f:
        f.write("# sha256 of each mockup SVG its committed PNG was rendered from (compare.py --refresh-mockup)\n")
        for name in sorted(idx):
            f.write(f"{idx[name]}  {name}\n")


def png_is_current(svg_path):
    """True when <mockup>.png was rendered from this exact SVG content. Judged by content hash
    (mockup-png.sha256), never by mtime: a checkout or merge rewrites mtimes in any order."""
    png = os.path.splitext(svg_path)[0] + ".png"
    if not os.path.exists(png):
        return False
    known = read_hash_index(os.path.dirname(os.path.abspath(svg_path))).get(os.path.basename(svg_path))
    return known is not None and known == svg_digest(svg_path)


def mockup_png(svg_path, refresh=False):
    png = os.path.splitext(svg_path)[0] + ".png"
    if not refresh and png_is_current(svg_path):
        return png
    if refresh:
        # re-render next to the SVG and record the SVG's hash
        subprocess.run(["rsvg-convert", "-o", png, svg_path], check=True)
        d = os.path.dirname(os.path.abspath(svg_path))
        idx = read_hash_index(d)
        idx[os.path.basename(svg_path)] = svg_digest(svg_path)
        write_hash_index(d, idx)
        return png
    out = os.path.join(os.environ.get("TMPDIR", "/tmp"), os.path.basename(png))
    print(f"note: {os.path.basename(png)} is not the raster of the current SVG (hash); rendering a temporary one. "
          "Run compare.py --refresh-mockup to update it.", file=sys.stderr)
    subprocess.run(["rsvg-convert", "-o", out, svg_path], check=True)
    return out


def match_widget(rid, region, widgets):
    if region.get("widget"):
        rid = region["widget"]

    def ok(w):
        if rid.startswith("RibbonGroup:"):
            name = rid.split(":", 1)[1].upper()
            return w.get("name") == "RibbonGroup" and name in (w.get("toolbarGroup", "").upper(),
                                                              w.get("section", "").upper())
        if rid == "ViewToolColumn":
            return w.get("overlay") == "ViewToolColumn" or w.get("name") == "ViewToolColumn"
        if rid == "StatusBar":
            return w.get("class") == "QStatusBar" or w.get("name") == "StatusBar"
        if rid == "Viewer":
            return w.get("class", "").endswith("ViewerWidget") or w.get("name") == "Viewer"
        return w.get("name") == rid

    cands = [w for w in widgets if ok(w) and w["w"] > 0 and w["h"] > 0]
    if not cands:
        return None
    cx, cy = region["x"] + region["w"] / 2, region["y"] + region["h"] / 2
    return min(cands, key=lambda w: (w["x"] + w["w"] / 2 - cx) ** 2 + (w["y"] + w["h"] / 2 - cy) ** 2)


def dominant(img):
    """Most frequent colour after 4-bit quantisation, refined to the mean of that bin."""
    bins = {}
    step = max(1, (img.width * img.height) // 40000)
    k = 0
    for row in img.rows:
        for p in row:
            k += 1
            if k % step:
                continue
            key = (p[0] >> 4, p[1] >> 4, p[2] >> 4)
            b = bins.get(key)
            if b is None:
                bins[key] = [1, p[0], p[1], p[2]]
            else:
                b[0] += 1
                b[1] += p[0]
                b[2] += p[1]
                b[3] += p[2]
    if not bins:
        return (0, 0, 0)
    n, r, g, b = max(bins.values(), key=lambda v: v[0])
    return (round(r / n), round(g / n), round(b / n))


def de(c1, c2):
    f = lambda c: (c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, 1.0)  # noqa: E731
    return colormath.de2000(f(c1), f(c2))


def luma(img):
    return [[0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] for p in row] for row in img.rows]


def ssim(a, b, block=8):
    """Mean SSIM over non-overlapping blocks of two equal-size luminance arrays."""
    h, w = min(len(a), len(b)), min(len(a[0]) if a else 0, len(b[0]) if b else 0)
    if h < block or w < block:
        return None
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    total, n = 0.0, 0
    stride = block if h * w <= 400000 else block * 2  # sample every other block on big regions
    for y in range(0, h - block + 1, stride):
        for x in range(0, w - block + 1, stride):
            xs = [a[y + j][x + i] for j in range(block) for i in range(block)]
            ys = [b[y + j][x + i] for j in range(block) for i in range(block)]
            m = float(len(xs))
            mx, my = sum(xs) / m, sum(ys) / m
            vx = sum((v - mx) ** 2 for v in xs) / m
            vy = sum((v - my) ** 2 for v in ys) / m
            cov = sum((xs[i] - mx) * (ys[i] - my) for i in range(len(xs))) / m
            total += ((2 * mx * my + c1) * (2 * cov + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))
            n += 1
    return total / n if n else None


def resize_nearest(img, w, h):
    if img.width == w and img.height == h:
        return img
    rows = []
    for y in range(h):
        src = img.rows[min(img.height - 1, y * img.height // h)]
        rows.append([src[min(img.width - 1, x * img.width // w)] for x in range(w)])
    return pngio.Image(w, h, rows)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("shot")
    ap.add_argument("mockup", help="token mockup SVG with data-region rects")
    ap.add_argument("--geometry", help="widget geometry JSON (FABRICAD_GEOMETRY_DUMP / ui.sh geom)")
    ap.add_argument("--report", help="directory for index.html and the region crops")
    ap.add_argument("--tol", type=float, default=4.0, help="layout tolerance per edge in logical px (default 4)")
    ap.add_argument("--only", help="comma-separated region ids")
    ap.add_argument("--no-ssim", action="store_true", help="skip SSIM (faster)")
    ap.add_argument("--json", help="write the results as JSON here")
    ap.add_argument("--inset", action="append", default=[], metavar="ID=PX",
                    help="visible box = widget rect inset by PX (default CommandDialog=8, the card's bleed)")
    ap.add_argument("--refresh-mockup", action="store_true",
                    help="re-render <mockup>.png from the SVG (rsvg-convert) and record its hash in mockup-png.sha256")
    a = ap.parse_args(argv)

    with open(a.mockup, encoding="utf-8") as f:
        svg = f.read()
    regions, samples = read_regions(svg)
    if a.only:
        keep = set(a.only.split(","))
        regions = [r for r in regions if r["id"] in keep]
    if not regions:
        print("no data-region rects in %s" % a.mockup, file=sys.stderr)
        return 2
    m = re.search(r"<svg\b[^>]*\bwidth=\"([\d.]+)\"[^>]*\bheight=\"([\d.]+)\"", svg)
    svg_w, svg_h = (float(m.group(1)), float(m.group(2))) if m else (1920.0, 1080.0)

    shot = pngio.read_png(a.shot)
    mock = pngio.read_png(mockup_png(a.mockup, a.refresh_mockup))
    sx, sy = shot.width / svg_w, shot.height / svg_h
    mx, my = mock.width / svg_w, mock.height / svg_h
    if abs(sx - sy) > 0.02:
        print("warning: shot %dx%d is not the mockup's aspect (%gx%g); regions are scaled per axis"
              % (shot.width, shot.height, svg_w, svg_h), file=sys.stderr)

    widgets, geo_scale = None, 1.0
    if a.geometry:
        with open(a.geometry, encoding="utf-8") as f:
            geo = json.load(f)
        widgets = geo.get("widgets", [])
        win = geo.get("window", {})
        if win and (abs(win.get("w", svg_w) - svg_w) > 1 or abs(win.get("h", svg_h) - svg_h) > 1):
            print("warning: window %sx%s differs from the mockup %gx%g: run at FABRICAD_WINDOW_SIZE=%dx%d"
                  % (win.get("w"), win.get("h"), svg_w, svg_h, svg_w, svg_h), file=sys.stderr)

    insets = dict(INSETS)
    for kv in a.inset:
        k, _, v = kv.partition("=")
        insets[k] = float(v or 0)
    if a.report:
        os.makedirs(a.report, exist_ok=True)
    results, layout_fail = [], 0
    for r in regions:
        res = {"id": r["id"], "mockup": [round(r[k], 1) for k in ("x", "y", "w", "h")]}
        if widgets is not None:
            w = match_widget(r["id"], r, widgets)
            if w is None:
                res["layout"] = "missing" if r["gate"] else "info"
                layout_fail += res["layout"] == "missing"
            else:
                i = insets.get(r["id"], 0)
                w = dict(w, x=w["x"] + i, y=w["y"] + i, w=w["w"] - 2 * i, h=w["h"] - 2 * i)
                d = [w["x"] - r["x"], w["y"] - r["y"], (w["x"] + w["w"]) - (r["x"] + r["w"]),
                     (w["y"] + w["h"]) - (r["y"] + r["h"])]
                res["widget"] = [w["x"], w["y"], w["w"], w["h"]]
                res["edges"] = [round(v, 1) for v in d]
                res["gate"] = r["gate"] or "none"
                gated = [v for v, e in zip(d, "ltrb") if e in r["gate"]]
                if not gated:
                    res["layout"] = "info"
                else:
                    res["layout"] = "ok" if max(abs(v) for v in gated) <= a.tol else "FAIL"
                layout_fail += res["layout"] == "FAIL"
        cs = shot.crop(r["x"] * sx, r["y"] * sy, r["w"] * sx, r["h"] * sy)
        cm = mock.crop(r["x"] * mx, r["y"] * my, r["w"] * mx, r["h"] * my)
        if cs.width and cm.width:
            ds, dm = dominant(cs), dominant(cm)
            res["colour"] = {"shot": "#%02X%02X%02X" % ds, "mockup": "#%02X%02X%02X" % dm, "dE00": round(de(ds, dm), 2)}
            if not a.no_ssim:
                cm2 = resize_nearest(cm, cs.width, cs.height)
                s = ssim(luma(cs), luma(cm2))
                res["ssim"] = None if s is None else round(s, 3)
            if a.report:
                safe = re.sub(r"[^A-Za-z0-9_-]", "_", r["id"])
                pngio.write_png(os.path.join(a.report, safe + "-shot.png"), cs)
                pngio.write_png(os.path.join(a.report, safe + "-mockup.png"), cm)
                res["crop"] = safe
        results.append(res)

    sample_res = []
    for p in samples:
        ps = shot.pixel(min(shot.width - 1, p["x"] * sx), min(shot.height - 1, p["y"] * sy))
        pm = mock.pixel(min(mock.width - 1, p["x"] * mx), min(mock.height - 1, p["y"] * my))
        sample_res.append({"token": p["token"], "at": [p["x"], p["y"]], "shot": "#%02X%02X%02X" % ps,
                           "mockup": "#%02X%02X%02X" % pm, "dE00": round(de(ps, pm), 2)})

    # text table
    print("%-24s %-7s %-5s %-26s %-17s %s" % ("REGION", "LAYOUT", "GATE", "EDGES l,t,r,b (px)", "COLOUR dE00", "SSIM"))
    for res in results:
        c = res.get("colour", {})
        flag = "!" if c.get("dE00", 0) > 3 else " "
        print("%-24s %-7s %-5s %-26s %-17s %s" % (
            res["id"], res.get("layout", "-"), res.get("gate", "-"),
            ",".join("%+g" % v for v in res.get("edges", [])) or "-",
            ("%5.1f%s %s" % (c["dE00"], flag, c["shot"])) if c else "-",
            res.get("ssim", "-")))
    for s in sample_res:
        print("sample %-20s %s vs %s  dE00 %.1f%s" % (s["token"], s["shot"], s["mockup"], s["dE00"],
                                                     " !" if s["dE00"] > 3 else ""))
    if widgets is not None:
        n = sum(1 for r in results if r.get("layout") in ("ok", "FAIL", "missing"))
        info = sum(1 for r in results if r.get("layout") == "info")
        print("layout: %d of %d gated regions within %g px on their gated edges (%d informative)"
              % (n - layout_fail, n, a.tol, info))

    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"regions": results, "samples": sample_res}, f, indent=1)
    if a.report:
        write_report(a, results, sample_res)
        print("report: " + os.path.join(a.report, "index.html"))
    return 1 if layout_fail else 0


def write_report(a, results, samples):
    e = html.escape
    rows = []
    for r in results:
        c = r.get("colour", {})
        bad = r.get("layout") in ("FAIL", "missing")
        crop = r.get("crop")
        imgs = ('<td><img src="%s-shot.png"></td><td><img src="%s-mockup.png"></td>' % (crop, crop)) if crop else "<td></td><td></td>"
        rows.append(
            "<tr class='%s'><th>%s</th><td>%s<br><small>%s</small></td><td>%s</td>"
            "<td><span class=sw style='background:%s'></span>%s<br><span class=sw style='background:%s'></span>%s<br>dE00 %s</td>"
            "<td>%s</td>%s</tr>" % (
                "bad" if bad else "", e(r["id"]), e(r.get("layout", "-")),
                e(",".join("%+g" % v for v in r.get("edges", []))), e(str(r["mockup"])),
                c.get("shot", "#000"), c.get("shot", ""), c.get("mockup", "#000"), c.get("mockup", ""), c.get("dE00", "-"),
                r.get("ssim", "-"), imgs))
    srows = "".join("<tr><th>%s</th><td>%s</td><td>%s</td><td>%s</td></tr>" % (e(s["token"]), s["shot"], s["mockup"], s["dE00"])
                    for s in samples)
    doc = """<!doctype html><html><head><meta charset=utf-8><title>FabriCAD mockup compare</title>
<style>body{font:13px monospace;background:#16181d;color:#e8eaee;margin:16px}table{border-collapse:collapse}
td,th{border:1px solid #333;padding:4px;vertical-align:top;text-align:left}tr.bad th{color:#ff5c8a}
img{max-width:520px;max-height:320px;image-rendering:pixelated;background:#000}.sw{display:inline-block;width:12px;height:12px;border:1px solid #888;margin-right:4px}</style></head><body>
<h1>%s vs %s</h1><p>layout tolerance %g px; colour is the dominant colour of each region; SSIM is informative only.</p>
<table><tr><th>region</th><th>layout / edges</th><th>mockup box</th><th>colour (shot, mockup)</th><th>SSIM</th><th>shot</th><th>mockup</th></tr>%s</table>
%s</body></html>""" % (e(a.shot), e(a.mockup), a.tol, "".join(rows),
                        ("<h2>samples</h2><table><tr><th>token</th><th>shot</th><th>mockup</th><th>dE00</th></tr>%s</table>" % srows) if samples else "")
    with open(os.path.join(a.report, "index.html"), "w", encoding="utf-8") as f:
        f.write(doc)


if __name__ == "__main__":
    sys.exit(main())
