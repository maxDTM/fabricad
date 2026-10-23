#!/usr/bin/env python3
# Before/after contact sheets of the toolbar icon set (colour style vs mono style).
#
#     python3 docs/uiux/ui2/icons/make_contact_sheets.py
#
# Evaluates resources/icons/generate_icons.py in memory for both styles and both themes (it does
# not write resources/icons), renders every icon with rsvg-convert at 16, 24 and 32 px and writes
#   all-<theme>.png      every icon: name, colour 16/24/32 | mono 16/24/32, at 1x on surface.header
#   toolbar-<theme>.png  the main toolbar / tool icons at 1x and 3x (nearest neighbour)
# next to this script. Needs rsvg-convert and Pillow.
# SPDX-License-Identifier: LGPL-2.1-or-later
import io
import json
import os
import runpy
import subprocess

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
GEN = os.path.join(ROOT, "resources", "icons", "generate_icons.py")
TOKENS = os.path.join(ROOT, "resources", "theme", "tokens.json")
FONT = os.path.join(ROOT, "resources", "fonts", "JetBrainsMono-Medium.ttf")
SIZES = (16, 24, 32)
STYLES = ("colour", "mono")

TOOLBAR = [
    "sketch-create", "sketch-project", "part-extrude", "part-revolve", "part-sweep", "part-loft", "part-hole",
    "part-fillet", "part-chamfer", "part-shell", "part-draft", "part-press-pull", "part-rectangular-pattern",
    "part-circular-pattern", "part-mirror", "part-combine", "part-split-body", "part-move", "part-thicken",
    "construct-offset-plane", "construct-axis", "construct-point", "assembly-new-component", "assembly-joint",
    "inspect-measure", "inspect-section", "sketch-line", "sketch-rectangle", "sketch-circle", "sketch-arc-3point",
    "sketch-trim", "sketch-dimension", "finish-sketch", "undo", "redo", "save", "search", "help", "delete",
    "view-iso", "fit", "orbit", "ok", "cancel", "warning", "error",
]


def icon_sets(theme):
    out = {}
    for style in STYLES:
        g = runpy.run_path(GEN, init_globals={"THEME": theme, "STYLE": style}, run_name="sheet_%s_%s" % (theme, style))
        out[style] = {n: g["HEADER"] + g["I"][n] + "</svg>\n" for n in g["I"]}
    return out


def render(svg, size):
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size)], input=svg.encode(),
                         capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def colours(theme):
    c = json.load(open(TOKENS, encoding="utf-8"))["color"]
    return {k: c[k][theme] for k in ("surface.header", "text.secondary", "border.strong", "text.primary")}


def cell_strip(sets, name, scale=1, gap=8):
    """colour 16/24/32 | mono 16/24/32 for one icon, `scale` x nearest neighbour."""
    imgs = []
    for style in STYLES:
        for s in SIZES:
            im = render(sets[style][name], s)
            if scale != 1:
                im = im.resize((s * scale, s * scale), Image.NEAREST)
            imgs.append(im)
    h = max(i.height for i in imgs)
    w = sum(i.width for i in imgs) + gap * (len(imgs) - 1) + gap * 2  # extra gap between the styles
    strip = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    x = 0
    for k, im in enumerate(imgs):
        if k == len(SIZES):
            x += gap * 2
        strip.alpha_composite(im, (x, (h - im.height) // 2))
        x += im.width + gap
    return strip


def sheet(theme, sets, names, scale, cols, title, path):
    col = colours(theme)
    font = ImageFont.truetype(FONT, 11)
    head = ImageFont.truetype(FONT, 13)
    strips = [(n, cell_strip(sets, n, scale)) for n in names]
    label_w = 190
    cw = label_w + max(s.width for _, s in strips) + 24
    ch = max(s.height for _, s in strips) + 14
    rows = (len(strips) + cols - 1) // cols
    top = 52
    img = Image.new("RGBA", (cols * cw + 16, top + rows * ch + 12), col["surface.header"])
    d = ImageDraw.Draw(img)
    d.text((12, 10), title, font=head, fill=col["text.primary"])
    d.text((12, 30), "per icon: colour (old) 16 24 32 px  |  mono (new) 16 24 32 px", font=font,
           fill=col["text.secondary"])
    for i, (n, s) in enumerate(strips):
        r, c = divmod(i, cols)
        x, y = 12 + c * cw, top + r * ch
        d.text((x, y + (s.height - 11) // 2), n[:26], font=font, fill=col["text.secondary"])
        img.alpha_composite(s, (x + label_w, y))
        sep = x + label_w + sum(sz * scale for sz in SIZES) + 8 * len(SIZES) + 3
        d.line([(sep, y), (sep, y + s.height)], fill=col["border.strong"])
    img.convert("RGB").save(path, optimize=True)
    print(path)


def main():
    for theme in ("dark", "light"):
        sets = icon_sets(theme)
        names = sorted(sets["mono"])
        sheet(theme, sets, names, 1, 5, "FabriCAD icons, %s theme: all %d icons, before / after" % (theme, len(names)),
              os.path.join(HERE, "all-%s.png" % theme))
        tb = [n for n in TOOLBAR if n in sets["mono"]]
        sheet(theme, sets, tb, 1, 4, "Toolbar and tool icons, %s theme, 1x" % theme,
              os.path.join(HERE, "toolbar-%s.png" % theme))
        sheet(theme, sets, tb, 3, 2, "Toolbar and tool icons, %s theme, 3x nearest neighbour" % theme,
              os.path.join(HERE, "toolbar-%s-3x.png" % theme))


if __name__ == "__main__":
    main()
