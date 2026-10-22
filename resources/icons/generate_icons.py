#!/usr/bin/env python3
# FabriCAD icon set generator (own artwork, LGPL-2.1-or-later).
#
# Every SVG in resources/icons except fabricad.svg (the application logo) is written by this
# script, so the whole set shares one header, one stroke width and one palette. Edit the
# drawings below and run
#     python3 resources/icons/generate_icons.py
# It rewrites the SVGs and the icons/ entries of resources/fabricad.qrc, and deletes SVGs it
# no longer generates (except the logo). Output is deterministic: same script, same bytes.
# The rules (checked by src/app/tests/test_icons.cpp) are documented in README.md here.
# SPDX-License-Identifier: LGPL-2.1-or-later
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
QRC = os.path.join(HERE, "..", "fabricad.qrc")
KEEP = {"fabricad.svg"}  # hand-made, not generated

# --- palette (README.md) ----------------------------------------------------------------
INK = "#3C4148"    # outlines
GREY = "#9AA0A8"   # secondary / hidden / reference lines
WH = "#FFFFFF"     # paper, knock-outs
BODY = "#C9CDD2"   # solid body fill
SK = "#1F74D1"     # sketch
OR = "#E8811A"     # solid feature accent
AS = "#2E9E4F"     # assembly
CO = "#E5A100"     # construction geometry
IN = "#8E44AD"     # inspect
UI = "#0696D7"     # view / application accent
DR = "#00838F"     # drawing
RED = "#D93A3A"    # error / remove / cut

HEADER = ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" '
          'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' % INK)

ATTR = {"s": "stroke", "f": "fill", "fo": "fill-opacity", "da": "stroke-dasharray", "so": "stroke-opacity"}


def _attrs(kw):
    out = []
    for k, v in kw.items():
        out.append('%s="%s"' % (ATTR.get(k, k.replace("_", "-")), v))
    return (" " + " ".join(out)) if out else ""


def P(d, **kw):
    return '<path d="%s"%s/>' % (d, _attrs(kw))


def C(cx, cy, r, **kw):
    return '<circle cx="%g" cy="%g" r="%g"%s/>' % (cx, cy, r, _attrs(kw))


def E(cx, cy, rx, ry, **kw):
    return '<ellipse cx="%g" cy="%g" rx="%g" ry="%g"%s/>' % (cx, cy, rx, ry, _attrs(kw))


def R(x, y, w, h, rx=0, **kw):
    r = ' rx="%g"' % rx if rx else ""
    return '<rect x="%g" y="%g" width="%g" height="%g"%s%s/>' % (x, y, w, h, r, _attrs(kw))


def dot(x, y, c=INK, r=1.75):
    return C(x, y, r, f=c, s="none")


DASH = "2 2.5"
DASHDOT = "5 2 1 2"

# --- motifs -------------------------------------------------------------------------------


def cube(cx=12, cy=12, s=1.0, fill=BODY, stroke=INK, inner=None):
    """Isometric cube, `s` = 1 spans 14 x 15 px around (cx, cy)."""
    def pt(x, y):
        return "%g %g" % (round(cx + x * s, 2), round(cy + y * s, 2))
    inner = inner or stroke
    out = P("M%s L%s L%s L%s L%s L%s Z" % (pt(-7, -4), pt(0, -7.5), pt(7, -4), pt(7, 4), pt(0, 7.5), pt(-7, 4)),
            f=fill, s=stroke)
    out += P("M%s L%s L%s M%s L%s" % (pt(-7, -4), pt(0, -0.5), pt(7, -4), pt(0, -0.5), pt(0, 7.5)), s=inner)
    return out


def cube_pts(cx=12, cy=12, s=1.0):
    def pt(x, y):
        return (round(cx + x * s, 2), round(cy + y * s, 2))
    return {"tl": pt(-7, -4), "tb": pt(0, -7.5), "tr": pt(7, -4), "br": pt(7, 4), "bf": pt(0, 7.5),
            "bl": pt(-7, 4), "tf": pt(0, -0.5), "bb": pt(0, 0.5)}


def poly(*pts):
    return "M" + " L".join("%g %g" % p for p in pts) + " Z"


def pencil(tx=13, ty=21):
    """Pencil with its tip at (tx, ty), pointing down-left."""
    return (P("M%g %g l1 -3.5 l7 -7 l2.5 2.5 l-7 7 z" % (tx, ty), f=WH) +
            P("M%g %g l2.5 2.5" % (tx + 6.5, ty - 9.5)))


def plus(x, y, c=INK, r=3):
    return P("M%g %g h%g M%g %g v%g" % (x - r, y, 2 * r, x, y - r, 2 * r), s=c)


def cross(x, y, c=RED, r=2.5):
    return P("M%g %g l%g %g M%g %g l%g %g" % (x - r, y - r, 2 * r, 2 * r, x + r, y - r, -2 * r, 2 * r), s=c)


def arrow_head(x, y, direction, c, size=2.5):
    """Open chevron with its tip at (x, y); direction one of up/down/left/right."""
    k = size
    d = {"up": "M%g %g L%g %g L%g %g" % (x - k, y + k, x, y, x + k, y + k),
         "down": "M%g %g L%g %g L%g %g" % (x - k, y - k, x, y, x + k, y - k),
         "left": "M%g %g L%g %g L%g %g" % (x + k, y - k, x, y, x + k, y + k),
         "right": "M%g %g L%g %g L%g %g" % (x - k, y - k, x, y, x - k, y + k)}[direction]
    return P(d, s=c)


def doc(x=4, y=3, w=12, h=18, fill=WH):
    """Page with a folded corner."""
    f = 4
    return (P("M%g %g H%g L%g %g V%g H%g Z" % (x, y, x + w - f, x + w, y + f, y + h, x), f=fill) +
            P("M%g %g V%g H%g" % (x + w - f, y, y + f, x + w)))


def export_doc(glyph, c):
    """Document with a glyph and an outgoing arrow (exports)."""
    return doc(3, 3, 12, 18) + glyph + P("M13 17 H21.5", s=c) + arrow_head(21.5, 17, "right", c)


def import_doc(glyph, c):
    """Document with a glyph and an incoming arrow (imports)."""
    return doc(9, 3, 12, 18) + glyph + P("M2.5 17 H11", s=c) + arrow_head(11, 17, "right", c)


def sheet(fill=WH):
    """Drawing sheet with a title block."""
    return R(2.5, 4.5, 19, 15, 1, f=fill) + P("M14 19.5 V16 H21.5", s=GREY)


def sketch_plane(stroke=GREY, fill=WH):
    return P("M2.5 18 L7.5 10 H21.5 L16.5 18 Z", f=fill, s=stroke)


def sketch_profile():
    return P("M7 15.5 L9.5 12 H14 A2 2 0 0 1 15.8 14.5 L15.2 15.5 Z", s=SK)


def component(cx=12, cy=12, s=1.0):
    """Assembly component: green outlined cube."""
    return cube(cx, cy, s, fill=WH, stroke=AS)


def four_way(c, r=10, cx=12, cy=12, inner=4):
    out = P("M%g %g V%g M%g %g V%g M%g %g H%g M%g %g H%g" % (
        cx, cy - r, cy - inner, cx, cy + inner, cy + r, cx - r, cy, cx - inner, cx + inner, cy, cx + r), s=c)
    out += arrow_head(cx, cy - r, "up", c, 2) + arrow_head(cx, cy + r, "down", c, 2)
    out += arrow_head(cx - r, cy, "left", c, 2) + arrow_head(cx + r, cy, "right", c, 2)
    return out


def view_cube(face):
    """Iso cube with one of its six faces highlighted (view.front ... view.bottom)."""
    p = cube_pts(12, 12.5, 1.0)
    faces = {
        "top": [p["tl"], p["tb"], p["tr"], p["tf"]],
        "front": [p["tl"], p["tf"], p["bf"], p["bl"]],
        "right": [p["tf"], p["tr"], p["br"], p["bf"]],
        "back": [p["tb"], p["tr"], p["br"], p["bb"]],
        "left": [p["tl"], p["tb"], p["bb"], p["bl"]],
        "bottom": [p["bl"], p["bb"], p["br"], p["bf"]],
    }
    hidden = face in ("back", "left", "bottom")
    out = cube(12, 12.5, 1.0, fill=WH)
    out += P("M%g %g L%g %g M%g %g L%g %g M%g %g L%g %g" % (
        p["bb"] + p["bl"] + p["bb"] + p["br"] + p["bb"] + p["tb"]), s=GREY, da="1.5 2")
    if hidden:  # seen through the cube: translucent fill, dashed outline
        out += P(poly(*faces[face]), f=UI, fo="0.4", s=UI, da="1.5 2")
    else:
        out += P(poly(*faces[face]), f=UI, fo="0.45", s=UI)
    return out


def orbit(direction):
    if direction in ("left", "right"):
        out = E(12, 12, 9, 4, s=GREY, da=DASH) + dot(12, 12, UI, 2)
        if direction == "left":
            out += P("M19 15.2 A9 4 0 0 1 3 12", s=UI) + arrow_head(3, 12, "up", UI, 2.2)
        else:
            out += P("M5 15.2 A9 4 0 0 0 21 12", s=UI) + arrow_head(21, 12, "up", UI, 2.2)
    else:
        out = E(12, 12, 4, 9, s=GREY, da=DASH) + dot(12, 12, UI, 2)
        if direction == "up":
            out += P("M15.2 19 A4 9 0 0 0 12 3", s=UI) + arrow_head(12, 3, "left", UI, 2.2)
        else:
            out += P("M15.2 5 A4 9 0 0 1 12 21", s=UI) + arrow_head(12, 21, "left", UI, 2.2)
    return out


def eye(cx=12, cy=12, iris=UI):
    return (P("M%g %g s4 -6.5 10 -6.5 10 6.5 10 6.5 -4 6.5 -10 6.5 S%g %g %g %g z" % (
        cx - 10, cy, cx - 10, cy, cx - 10, cy)) + C(cx, cy, 3, s=iris))


def cursor(c=INK, fill=WH, x=0, y=0):
    return P("M%g %g V%g L%g %g L%g %g L%g %g L%g %g H%g Z" % (
        6 + x, 3.5 + y, 18 + y, 9.5 + x, 14.8 + y, 12 + x, 20 + y, 14.2 + x, 19 + y, 11.8 + x, 13.8 + y,
        16.5 + x), f=fill, s=c)


def floppy(fill=WH):
    return (P("M4 4 H16.5 L20 7.5 V20 H4 Z", f=fill) + P("M8 4 V9 H15 V4") + R(7.5, 13, 9, 7, s=UI))


def table(x, y, w, h, rows, cols, c, header=True):
    out = R(x, y, w, h, 0.5, f=WH)
    if header:
        out += R(x, y, w, h / rows, f=c, fo="0.3", s="none")
    d = ""
    for i in range(1, rows):
        d += "M%g %g H%g " % (x, round(y + h * i / rows, 2), x + w)
    for j in range(1, cols):
        d += "M%g %g V%g " % (round(x + w * j / cols, 2), y, y + h)
    return out + P(d.strip(), s=c)


def dimension(c, geom=INK):
    return (P("M4 19 H20", s=geom) + P("M4 19 V6 M20 19 V6", s=GREY) + P("M4.5 9.5 H19.5", s=c) +
            arrow_head(4.5, 9.5, "left", c, 2) + arrow_head(19.5, 9.5, "right", c, 2))


def balloon(cx, cy, r, c=DR):
    return C(cx, cy, r, s=c, f=WH)


def joint_ring(cx=12, cy=12, r=4):
    return C(cx, cy, r, s=AS, f=WH) + dot(cx, cy, AS, 1.6)


def L_view(c=DR, dx=0, dy=0, s=1.0):
    """Front view of an L-shaped part (drawing views), 11 x 11 at scale 1."""
    pts = [(0, 11), (0, 0), (5, 0), (5, 6), (11, 6), (11, 11)]
    return P(poly(*[(round(dx + x * s, 2), round(dy + y * s, 2)) for x, y in pts]), s=c, f=WH)


# --- the icons -----------------------------------------------------------------------------
I = {}

# ===== sketch (blue) =========================================================================
I["sketch-line"] = P("M5 18.5 L19 5.5", s=SK) + dot(5, 18.5) + dot(19, 5.5)
I["sketch-rectangle"] = R(4, 7, 16, 11, s=SK) + dot(4, 18) + dot(20, 7)
I["sketch-rectangle-3point"] = P("M3.5 12.5 L10 6 L18 14 L11.5 20.5 Z", s=SK) + dot(3.5, 12.5) + dot(10, 6) + dot(18, 14)
I["sketch-rectangle-center"] = (R(4, 6, 16, 12, s=SK) + P("M12 12 L20 6", s=GREY, da=DASH) + dot(12, 12) +
                                dot(20, 6))
I["sketch-circle"] = C(12, 12, 8, s=SK) + P("M12 12 L17.66 6.34", s=GREY) + dot(12, 12) + dot(17.66, 6.34)
I["sketch-circle-2point"] = C(12, 12, 8, s=SK) + P("M12 4 V20", s=GREY, da=DASH) + dot(12, 4) + dot(12, 20)
I["sketch-circle-3point"] = C(12, 12, 8, s=SK) + dot(4, 12) + dot(16, 5.07) + dot(16, 18.93)
I["sketch-arc-3point"] = P("M4 17 A8 8 0 0 1 20 17", s=SK) + dot(4, 17) + dot(12, 9) + dot(20, 17)
I["sketch-arc-center"] = (P("M7 18 H19 M7 18 V6", s=GREY, da=DASH) + P("M19 18 A12 12 0 0 0 7 6", s=SK) +
                          dot(7, 18) + dot(19, 18) + dot(7, 6))
I["sketch-arc-tangent"] = (P("M3 19 H11", s=GREY) + P("M11 19 A6.5 6.5 0 0 0 11 6", s=SK) + dot(11, 19) +
                           dot(11, 6))
I["sketch-polygon"] = (C(12, 12, 6.93, s=GREY, da=DASH) +
                       P("M20 12 L16 18.93 L8 18.93 L4 12 L8 5.07 L16 5.07 Z", s=SK) + dot(12, 12, INK, 1.4))
I["sketch-polygon-inscribed"] = (C(12, 12, 8.5, s=GREY, da=DASH) +
                                 P("M12 3.5 L19.36 7.75 L19.36 16.25 L12 20.5 L4.64 16.25 L4.64 7.75 Z", s=SK) +
                                 dot(12, 12, INK, 1.4))
I["sketch-slot"] = (P("M8 7.5 H16 A4.5 4.5 0 0 1 16 16.5 H8 A4.5 4.5 0 0 1 8 7.5 Z", s=SK) +
                    P("M8 12 H16", s=GREY, da=DASH) + dot(8, 12) + dot(16, 12))
I["sketch-point"] = P("M12 4 V8 M12 16 V20 M4 12 H8 M16 12 H20", s=GREY) + C(12, 12, 2.75, s=SK, f=WH)
I["sketch-project"] = (cube(12, 7.5, 0.6, fill=BODY) + P("M12 13 V17.5", s=INK) + arrow_head(12, 17.5, "down", INK, 2) +
                       P("M3 21 H21", s=SK) + dot(3, 21, SK, 1.2) + dot(21, 21, SK, 1.2))
I["sketch-mirror"] = (P("M12 3 V21", s=GREY, da=DASHDOT) + P("M3.5 18 L9.5 6.5 V18 Z", s=SK) +
                      P("M20.5 18 L14.5 6.5 V18 Z", s=SK))
I["sketch-offset"] = (P("M4 20 V11 A7 7 0 0 1 11 4 H20", s=GREY) + P("M8.5 20 V12 A3.5 3.5 0 0 1 12 8.5 H20", s=SK) +
                      P("M4.5 4.5 L8 8", s=INK) + P("M8 4.8 V8 H4.8", s=INK))
I["sketch-trim"] = (P("M7 4 V20 M17 4 V20 M3 12 H7 M17 12 H21", s=SK) + P("M7 12 H17", s=RED, da=DASH) +
                    cross(12, 7.5, RED, 2))
I["sketch-extend"] = (P("M19 4 V20", s=SK) + P("M3.5 12 H11", s=SK) + P("M11 12 H18", s=SK, da=DASH) +
                      arrow_head(18, 12, "right", INK, 2.5) + dot(3.5, 12))
I["sketch-break"] = (P("M3.5 12 H10 M14 12 H20.5", s=SK) + dot(10, 12) + dot(14, 12) +
                     P("M13.5 5 L10.5 8.5 H13.5 L10.5 12", s=RED))
I["sketch-fillet"] = (P("M5 20 V5 H20", s=GREY, da=DASH) + P("M5 20 V13 A8 8 0 0 1 13 5 H20", s=SK) +
                      dot(5, 13) + dot(13, 5))
I["sketch-construction"] = (P("M4 19 L20 5", s=CO, da="3 2.5") + C(7, 7, 3.5, s=CO, da="2 2") + dot(4, 19, CO) +
                            dot(20, 5, CO))
I["sketch-dimension"] = dimension(SK, SK)
I["sketch-dimension-edit"] = (P("M3 17 H15", s=SK) + P("M3 17 V6 M15 17 V6", s=GREY) + P("M3.5 9.5 H14.5", s=SK) +
                              arrow_head(3.5, 9.5, "left", SK, 2) + arrow_head(14.5, 9.5, "right", SK, 2) +
                              pencil(13, 22))
I["sketch"] = sketch_plane() + sketch_profile()
I["sketch-create"] = sketch_plane() + sketch_profile() + plus(5.5, 5, SK, 3)
I["sketch-edit"] = sketch_plane() + sketch_profile() + pencil(13, 14.5)
I["sketch-finish"] = sketch_plane() + P("M7 7 L10.5 10.5 L18 3", s=AS)
I["finish-sketch"] = R(3.5, 3.5, 17, 17, 3, s=AS, f=WH) + P("M7.5 12 L10.5 15 L16.5 9", s=AS)
I["sketch-look-at"] = (R(6, 3, 15, 13, 1, f=WH, s=GREY) + P("M10 12 L12 7 H17 L15 12 Z", s=SK) +
                       P("M2.5 21 L7 16.5", s=INK) + P("M3.5 16.5 H7 V20", s=INK))
I["sketch-select"] = cursor(SK, WH)
I["sketch-toggle-constraints"] = (R(3.5, 3.5, 17, 17, 3, s=GREY) + P("M7.5 16.5 H16.5 M12 16.5 V7.5", s=SK) +
                                  P("M12 13.5 H15 V16.5", s=SK))
I["sketch-toggle-dimensions"] = (P("M4 7 V17 M20 7 V17", s=GREY) + P("M4.5 12 H19.5", s=SK) +
                                 arrow_head(4.5, 12, "left", SK, 2.2) + arrow_head(19.5, 12, "right", SK, 2.2))
I["sketch-toggle-grid"] = R(3.5, 3.5, 17, 17, 1, s=GREY) + P("M9.17 3.5 V20.5 M14.83 3.5 V20.5 M3.5 9.17 H20.5 M3.5 14.83 H20.5", s=SK)
I["sketch-toggle-points"] = (R(5, 5, 14, 14, s=GREY, da=DASH) + dot(5, 5, SK, 2.2) + dot(19, 5, SK, 2.2) +
                             dot(5, 19, SK, 2.2) + dot(19, 19, SK, 2.2))
I["sketch-toggle-profiles"] = P("M5 17 C3 11 7 4 13 5 C19 6 22 11 18 17 C15 21 7 21 5 17 Z", s=SK, f=SK, fo="0.25")
I["sketch-toggle-snap"] = (dot(6, 6, GREY, 1.2) + dot(12, 6, GREY, 1.2) + dot(18, 6, GREY, 1.2) +
                           dot(6, 12, GREY, 1.2) + dot(18, 12, GREY, 1.2) +
                           dot(6, 18, GREY, 1.2) + dot(12, 18, GREY, 1.2) + dot(18, 18, GREY, 1.2) +
                           C(12, 12, 3, s=SK) + P("M12 7.5 V9 M12 15 V16.5 M7.5 12 H9 M15 12 H16.5", s=SK))
# constraints (sketch glyphs)
I["sketch-constraint-horizontal-vertical"] = P("M4 18 H17", s=SK) + P("M20 4 V17", s=SK) + dot(4, 18) + dot(17, 18) + dot(20, 4) + dot(20, 17)
I["sketch-constraint-coincident"] = P("M3.5 19 L12 12 L20.5 16", s=SK) + C(12, 12, 3, s=INK, f=WH)
I["sketch-constraint-tangent"] = C(12, 14.5, 6, s=SK) + P("M3 8.5 H21", s=SK) + dot(12, 8.5)
I["sketch-constraint-equal"] = P("M5 9 H19 M5 15 H19", s=SK) + dot(5, 9) + dot(19, 9) + dot(5, 15) + dot(19, 15)
I["sketch-constraint-parallel"] = P("M5 19.5 L12 4.5 M12 19.5 L19 4.5", s=SK)
I["sketch-constraint-perpendicular"] = P("M4 19 H20 M12 19 V4.5", s=SK) + P("M12 15 H16 V19", s=INK)
I["sketch-constraint-fix"] = (P("M8.5 11 V8 A3.5 3.5 0 0 1 15.5 8 V11", s=AS) + R(5.5, 11, 13, 9.5, 1.5, s=AS, f=WH) +
                              P("M12 14.5 V17", s=AS))
I["sketch-constraint-midpoint"] = P("M3.5 18 L20.5 6", s=SK) + P("M12 8 L15.5 14 H8.5 Z", s=INK, f=WH)
I["sketch-constraint-concentric"] = C(12, 12, 8.5, s=SK) + C(12, 12, 4.5, s=SK) + dot(12, 12, INK, 1.4)
I["sketch-constraint-collinear"] = P("M3 18 L9 14 M15 10 L21 6", s=SK) + P("M9 14 L15 10", s=GREY, da="1.5 2") + dot(9, 14) + dot(15, 10)
I["sketch-constraint-symmetric"] = (P("M12 3 V21", s=GREY, da=DASHDOT) + P("M4 7.5 L9 12 L4 16.5", s=SK) +
                                    P("M20 7.5 L15 12 L20 16.5", s=SK))

# ===== solid features (grey bodies, orange accent) ===========================================
EXTRUDE = (P("M3 20 V12.5 L7.5 8.5 H17 V16 L13 20 Z", f=BODY) + P("M3 12.5 H13 L17 8.5 M13 12.5 V20") +
           P("M3 20 H13 L17 16", s=SK) + P("M20.5 18 V4", s=OR) + arrow_head(20.5, 4, "up", OR))
I["part-extrude"] = EXTRUDE
REVOLVE = (P("M6.5 7 V16.5 A5.5 2.2 0 0 0 17.5 16.5 V7", f=BODY) + E(12, 7, 5.5, 2.2, f=WH) +
           P("M12 2 V22", s=GREY, da=DASHDOT) + P("M3.2 12.8 A9 3.4 0 1 0 20.6 11.2", s=OR) +
           arrow_head(20.6, 11.2, "up", OR, 2.2))
I["part-revolve"] = REVOLVE
SWEEP = (P("M3 13.5 C8.5 13.5 9 5.5 16 5.5 H20 M3 21 C11.5 21 12 12.5 16 12.5 H20", s=INK) +
         P("M3 17.25 C10 17.25 10.5 9 16 9 H20", s=OR, da=DASH) + E(3.5, 17.25, 1.75, 3.75, f=BODY) +
         E(20, 9, 1.75, 3.5, f=WH))
I["part-sweep"] = SWEEP
LOFT = (P("M3 19 L8 15.5 H20.5 L15.5 19 Z", f=BODY) + E(12, 5.5, 4.5, 2, f=WH) + P("M3 19 L7.5 5.5 M15.5 19 L16.5 5.5 M20.5 15.5 L16.5 5.5", s=INK) +
        P("M12 15.5 V9", s=OR) + arrow_head(12, 9, "up", OR, 2))
I["part-loft"] = LOFT
HOLE = (cube(12, 12.5, 1.05, fill=BODY) + E(12, 8.6, 3.6, 1.8, f=WH, s=OR) +
        P("M8.4 8.6 V12.6 M15.6 8.6 V12.6", s=OR, da="1.5 1.5"))
I["part-hole"] = HOLE
FILLET = (P("M4 20 V11 A7 7 0 0 1 11 4 H20 V20 Z", f=BODY) + P("M4 11 V4 H11", s=GREY, da=DASH) +
          P("M4 11 A7 7 0 0 1 11 4", s=OR))
I["part-fillet"] = FILLET
CHAMFER = P("M4 20 V10 L10 4 H20 V20 Z", f=BODY) + P("M4 10 V4 H10", s=GREY, da=DASH) + P("M4 10 L10 4", s=OR)
I["part-chamfer"] = CHAMFER
SHELL = (cube(12, 12.5, 1.05) + P("M7.2 8.3 L12 5.8 L16.8 8.3 L12 10.9 Z", f=WH, s=OR) +
         P("M7.2 8.3 V10.5 M16.8 8.3 V10.5", s=OR))
I["part-shell"] = SHELL
DRAFT = P("M7 4 H17 L20.5 20 H3.5 Z", f=BODY) + P("M17 4 V20", s=GREY, da=DASH) + P("M17 4 L20.5 20", s=OR) + P("M17 14 A6 6 0 0 0 19.2 13.9", s=OR)
I["part-draft"] = DRAFT
I["part-press-pull"] = (P("M3 15 L11 11 L21 15 L13 19.5 Z", f=BODY) + P("M3 15 V18 L13 22.5 L21 18 V15 M13 19.5 V22.5") +
                        P("M3 15 L11 11 L21 15 L13 19.5 Z", f=OR, fo="0.35", s=OR) + P("M12 15 V2.5", s=OR) +
                        arrow_head(12, 2.5, "up", OR) + P("M8.5 7.5 L12 11 L15.5 7.5", s=OR))
OFFSET_FACE = (cube(10, 12.5, 1.0) +
               P("M10 12 L17 8.5 V16.5 L10 20 Z", f=OR, fo="0.35", s=OR) + P("M15.5 13 L21 10.2", s=OR) +
               arrow_head(21, 10.2, "right", OR, 2))
I["part-offset-face"] = OFFSET_FACE
COMBINE = (P("M3 3 H14 V10 H21 V21 H10 V14 H3 Z", f=BODY, s=OR) + P("M10 10 H14 V14 H10 Z", s=GREY, da="1.5 1.5"))
I["part-combine"] = COMBINE
SPLIT = (P("M3 7 H11.5 L8 20 H3 Z", f=BODY) + P("M14.5 7 H21 V20 H11 Z", f=BODY) +
         P("M14 2.5 L9.5 21.5", s=OR, da=DASH))
I["part-split-body"] = SPLIT
MOVE_BODY = cube(12, 12, 0.5) + four_way(OR, 10, 12, 12, 5.5)
I["part-move"] = MOVE_BODY
MIRROR = (P("M3 18.5 L5.5 7 H10 V18.5 Z", f=BODY) + P("M21 18.5 L18.5 7 H14 V18.5 Z", f=BODY, s=GREY) +
          P("M12 2.5 V21.5", s=OR, da=DASHDOT))
I["part-mirror"] = MIRROR
RECT_PATTERN = (R(3.5, 3.5, 6.5, 6.5, 1, f=BODY) + R(14, 3.5, 6.5, 6.5, 1, f=BODY, s=OR) +
                R(3.5, 14, 6.5, 6.5, 1, f=BODY, s=OR) + R(14, 14, 6.5, 6.5, 1, f=BODY, s=OR))
I["part-rectangular-pattern"] = RECT_PATTERN
CIRC_PATTERN = C(12, 12, 7, s=GREY, da=DASH) + dot(12, 12, INK, 1.2) + "".join(
    C(round(12 + 7 * x, 2), round(12 + 7 * y, 2), 2.4, f=BODY, s=(INK if i == 0 else OR))
    for i, (x, y) in enumerate([(0, -1), (0.866, -0.5), (0.866, 0.5), (0, 1), (-0.866, 0.5), (-0.866, -0.5)]))
I["part-circular-pattern"] = CIRC_PATTERN
I["part-edit-feature"] = cube(10, 10.5, 0.85) + pencil(13, 22)
I["feature"] = cube(10, 10.5, 0.85, fill=WH) + pencil(13, 22)
I["part-export-step"] = export_doc(cube(9, 12.5, 0.55), OR)
I["part-export-stl"] = export_doc(P("M5.5 16 L9 8.5 L12.5 16 Z M7.25 12.25 H10.75 L9 16 Z", s=OR), OR)
IMPORT = import_doc(cube(15, 12, 0.55), OR)
I["part-import"] = IMPORT
I["part-import-file"] = (P("M2.5 6 H8.5 L10.5 8 H20 V19 H2.5 Z", f=WH) + P("M11.5 1.5 V12", s=OR) +
                         arrow_head(11.5, 12, "down", OR) + P("M2.5 10.5 H20", s=GREY))

# ===== construction (orange-yellow) ==========================================================
OFFSET_PLANE = (P("M2.5 19 L7 14.5 H21 L16.5 19 Z", s=GREY, f=WH) + P("M3 10 L7.5 5.5 H21.5 L17 10 Z", s=CO, f=CO, fo="0.25") +
                P("M12 16.75 V11.5", s=INK) + arrow_head(12, 11.5, "up", INK, 2))
I["construct-offset-plane"] = OFFSET_PLANE
PLANE_ANGLE = (P("M2.5 20 L6.5 16 H21.5 L17.5 20 Z", s=GREY, f=WH) + P("M6.5 16 L9.5 4 H21.5 L17.5 16", s=CO, f=CO, fo="0.25") +
               P("M2 16 H22", s=INK) + P("M13 16 A4.5 4.5 0 0 0 13.9 13.2", s=INK))
I["construct-plane-at-angle"] = PLANE_ANGLE
TANGENT_PLANE = C(9, 13, 6.5, f=BODY) + P("M15.5 3.5 L19 1.5 V18.5 L15.5 22.5 Z", s=CO, f=CO, fo="0.25") + dot(15.5, 13, INK, 1.5)
I["construct-tangent-plane"] = TANGENT_PLANE
MIDPLANE = (R(2.5, 6, 3.5, 14, f=BODY) + R(18, 6, 3.5, 14, f=BODY) +
            P("M10 5.5 L14 3 V18.5 L10 21 Z", s=CO, f=CO, fo="0.25") + P("M6.5 13 H9 M15 13 H17.5", s=GREY, da="1.5 1.5"))
I["construct-midplane"] = MIDPLANE
PLANE_3P = P("M2.5 18 L8 6 H21.5 L16 18 Z", s=CO, f=CO, fo="0.25") + dot(5.5, 16) + dot(9.5, 8) + dot(17.5, 12)
I["construct-plane-three-points"] = PLANE_3P
AXIS = (E(12, 7, 5, 2, s=GREY) + P("M7 7 V17 A5 2 0 0 0 17 17 V7", s=GREY) + P("M12 1.5 V22.5", s=CO, da=DASHDOT))
I["construct-axis"] = AXIS
CPOINT = P("M3.5 18 L9 13.5 H21 M9 13.5 V3", s=GREY) + C(15, 8, 2.75, s=CO, f=CO, fo="0.35") + P("M15 3 V5 M15 11 V13 M10 8 H12 M18 8 H20", s=CO)
I["construct-point"] = CPOINT
# origin / browser construction items
I["plane"] = P("M2.5 17 L8 7 H21.5 L16 17 Z", s=CO, f=CO, fo="0.25")
I["axis"] = P("M3 21 L21 3", s=CO, da=DASHDOT) + dot(12, 12, CO, 2)
I["point"] = C(12, 12, 3.5, s=CO, f=CO, fo="0.35") + P("M12 3 V7 M12 17 V21 M3 12 H7 M17 12 H21", s=CO)
I["origin"] = (P("M5 19 V4", s=GREY) + arrow_head(5, 4, "up", GREY, 2) + P("M5 19 H20", s=GREY) + arrow_head(20, 19, "right", GREY, 2) +
               P("M5 19 L14.5 9.5", s=GREY) + C(5, 19, 2.2, s=CO, f=WH))

# ===== assembly (green) ======================================================================
I["component"] = component(12, 12, 1.1)
I["assembly-new-component"] = component(13.5, 13.5, 0.95) + plus(5, 5, INK, 3)
I["assembly-components-from-bodies"] = (cube(6.5, 8, 0.6) + component(17.5, 16.5, 0.6) + P("M7 15.5 Q7.5 19 12 19", s=INK) +
                                         arrow_head(12, 19, "right", INK, 2))
I["assembly-activate"] = C(12, 12, 8.5, s=AS, f=WH) + C(12, 12, 4, f=AS, s="none")
I["assembly-copy"] = component(9, 9.5, 0.75) + component(15, 14.5, 0.75)
I["assembly-paste"] = (R(4, 4, 16, 18, 1.5, f=WH) + R(8.5, 2.5, 7, 3.5, 1, f=WH) + component(12, 14, 0.6))
I["assembly-delete-component"] = component(10, 11, 0.9) + cross(18.5, 18.5, RED, 3)
I["assembly-ground"] = component(12, 9, 0.8) + P("M4 18.5 H20 M7 21.5 H17", s=AS) + P("M12 15 V18.5", s=AS)
I["assembly-unground"] = (component(12, 8, 0.8) + P("M4 20 H20", s=AS, da="2 1.5") + P("M12 14 V18.5", s=AS) +
                          arrow_head(12, 14, "up", AS, 2))
JOINT = R(2, 8.5, 6, 7, 1, f=BODY) + R(16, 8.5, 6, 7, 1, f=BODY) + P("M8 12 H16", s=GREY) + joint_ring(12, 12, 4)
I["assembly-joint"] = JOINT
I["joint"] = C(12, 12, 7.5, s=AS) + joint_ring(12, 12, 3) + P("M12 2 V4.5 M12 19.5 V22 M2 12 H4.5 M19.5 12 H22", s=AS)
I["assembly-as-built-joint"] = R(3, 6, 9, 12, 1, f=BODY) + R(12, 6, 9, 12, 1, f=BODY) + joint_ring(12, 12, 3.5) + P("M5 3 H19", s=AS) + arrow_head(5, 3, "left", AS, 1.5) + arrow_head(19, 3, "right", AS, 1.5)
RIGID = (R(3, 3, 6, 6, 1, f=BODY) + R(15, 3, 6, 6, 1, f=BODY) + R(9, 15, 6, 6, 1, f=BODY) +
         P("M9 6 H15 M7.5 9 L10.5 15 M16.5 9 L13.5 15", s=AS))
I["assembly-rigid-group"] = RIGID
I["assembly-drive-joint"] = (joint_ring(12, 12, 4) + P("M12 3 A9 9 0 1 1 3.4 9.2", s=AS) + arrow_head(12, 3, "right", AS, 2.2) +
                             P("M3 14 V20 L8 17 Z", s=INK, f=INK))
I["assembly-move"] = component(12, 12, 0.5) + four_way(AS, 10, 12, 12, 5.5)
INSERT_DESIGN = component(12, 15, 0.8) + P("M12 1.5 V8", s=INK) + arrow_head(12, 8, "down", INK, 2.5)
I["assembly-insert-design"] = INSERT_DESIGN
I["assembly-export-step"] = export_doc(component(7, 9.5, 0.4) + component(11, 14, 0.4), AS)
I["assembly-export-bom"] = export_doc(P("M5.5 9 H12.5 M5.5 12.5 H12.5 M5.5 16 H10", s=AS), AS)
# joint motion types (browser / timeline)
I["joint-rigid"] = C(12, 12, 8, s=AS, f=WH) + R(9, 9, 6, 6, 1, f=AS, s="none")
I["joint-revolute"] = (C(12, 12, 3, s=AS, f=AS) + P("M12 4 A8 8 0 1 1 4.1 10.6", s=INK) + arrow_head(12, 4, "right", INK, 2.2))
I["joint-slider"] = (P("M2.5 16 H21.5", s=GREY) + R(7.5, 8, 9, 6, 1, f=WH, s=AS) + P("M3 20 H21", s=INK) +
                     arrow_head(3, 20, "left", INK, 2) + arrow_head(21, 20, "right", INK, 2))
I["joint-cylindrical"] = (P("M8 5 V19 M16 5 V19", s=AS) + E(12, 5, 4, 1.6, s=AS, f=WH) + E(12, 19, 4, 1.6, s=AS) +
                          P("M3.5 11.5 A9 3 0 0 0 20.5 11.5", s=INK) + arrow_head(20.5, 11.5, "up", INK, 1.8) +
                          P("M21 2.5 V8.5", s=INK) + arrow_head(21, 2.5, "up", INK, 1.6))
I["joint-pin-slot"] = (P("M6 8 H18 A4 4 0 0 1 18 16 H6 A4 4 0 0 1 6 8 Z", s=GREY, f=WH) + C(8, 12, 2.5, s=AS, f=AS) +
                       P("M12 12 H18.5", s=INK) + arrow_head(18.5, 12, "right", INK, 2))
I["joint-planar"] = (P("M2.5 18 L8 9 H21.5 L16 18 Z", s=AS, f=AS, fo="0.25") + P("M7 13.5 H17", s=INK) +
                     arrow_head(17, 13.5, "right", INK, 1.8) + P("M12 4 V13.5", s=GREY, da="1.5 1.5") +
                     P("M9 6.5 A3.5 1.4 0 1 0 15 6.5", s=INK))
I["joint-ball"] = (C(12, 12, 8, s=AS, f=WH) + E(12, 12, 8, 3, s=AS) + P("M12 4 A3 8 0 0 0 12 20", s=AS) +
                   dot(12, 12, AS, 1.6))

# ===== inspect (purple) ======================================================================
I["inspect-measure"] = (R(2, 8, 20, 8, 1.5, s=IN, f=WH) + P("M6 8 V11 M10 8 V12.5 M14 8 V11 M18 8 V12.5", s=IN))
I["assembly-interference"] = (R(3, 4, 11, 11, 1, s=IN, f=WH) + R(10, 9, 11, 11, 1, s=IN, f=WH) +
                              R(10, 9, 4, 6, f=RED, fo="0.6", s="none") + P("M3 4 H14 V15 H3 Z", s=IN))

# ===== drawing (teal) ========================================================================
I["workspace-drawing"] = sheet() + R(5.5, 7.5, 5, 5, s=DR) + P("M13 7.5 H18.5 M13 10.5 H17", s=DR)
I["drawing-create"] = sheet() + cube(8.5, 11.5, 0.5, fill=WH, stroke=DR) + plus(17.5, 9.5, INK, 2.5)
I["drawing-new-sheet"] = (R(5.5, 2.5, 16, 13, 1, s=GREY, f=WH) + R(2.5, 6, 16, 13, 1, f=WH) + P("M12 19 V16 H18.5", s=GREY) +
                          plus(18.5, 18.5, DR, 3))
I["drawing-delete-sheet"] = sheet() + P("M6 8 H12 M6 11 H10", s=GREY) + cross(18.5, 9, RED, 3)
I["drawing-edit-sheet"] = sheet() + P("M6 8 H12 M6 11 H10", s=DR) + pencil(13, 22)
I["drawing-edit-title-block"] = (R(2.5, 4.5, 19, 15, 1, f=WH) + R(12, 13, 9.5, 6.5, f=DR, fo="0.3", s="none") +
                                 P("M12 19.5 V13 H21.5 M12 16.25 H21.5 M16 16.25 V19.5", s=DR) + pencil(7, 17))
I["drawing-base-view"] = R(2.5, 3.5, 19, 17, s=GREY, da=DASH) + L_view(DR, 6, 6.5, 1.0)
I["drawing-projected-view"] = (L_view(DR, 2.5, 3, 0.7) + P("M5 16 H8.5 V21 H5 Z", s=DR, f=WH) +
                               P("M13 16 H20.5 V21 H13 Z M17 16 V21", s=DR, f=WH) +
                               P("M11 7 H17", s=INK) + arrow_head(17, 7, "right", INK, 2))
I["drawing-section-view"] = (R(4.5, 5.5, 15, 13, f=WH, s=DR) +
                             P("M4.5 10 L9 5.5 M4.5 14.5 L13.5 5.5 M4.5 18.5 L17.5 5.5 M8.5 18.5 L19.5 7.5 M13 18.5 L19.5 12 M17.5 18.5 L19.5 16.5", s=DR) +
                             P("M2 2.5 H6 M18 2.5 H22 M2 2.5 V7 M22 2.5 V7", s=INK))
I["drawing-detail-view"] = (P("M3 13 V5 H12", s=GREY) + C(10, 11, 6.5, s=DR, f=WH) + P("M6 16 V8.5 H14", s=DR) +
                            P("M15 16 L20.5 21.5", s=INK))
I["drawing-edit-view"] = R(2.5, 3, 16, 14, s=GREY, da=DASH) + L_view(DR, 5, 5.5, 0.85) + pencil(13, 22)
I["drawing-move"] = R(7.5, 7.5, 9, 9, f=WH, s=DR) + four_way(DR, 10, 12, 12, 6)
I["drawing-delete"] = R(3, 4, 13, 12, s=DR, da=DASH) + P("M6 13 V7 H10", s=DR) + cross(18, 18, RED, 3.5)
I["drawing-dimension"] = dimension(DR)
I["drawing-edit-dimension"] = (P("M3 17 H15", s=INK) + P("M3 17 V6 M15 17 V6", s=GREY) + P("M3.5 9.5 H14.5", s=DR) +
                               arrow_head(3.5, 9.5, "left", DR, 2) + arrow_head(14.5, 9.5, "right", DR, 2) + pencil(13, 22))
I["drawing-center-mark"] = C(12, 12, 7, s=INK) + P("M12 2 V22 M2 12 H22", s=DR, da=DASHDOT) + P("M10 12 H14 M12 10 V14", s=DR)
I["drawing-auto-center-marks"] = "".join(C(x, y, 3.5, s=INK) + P("M%g %g H%g M%g %g V%g" % (x - 5, y, x + 5, x, y - 5, y + 5), s=DR)
                                         for x, y in [(6.5, 6.5), (17.5, 6.5), (6.5, 17.5), (17.5, 17.5)])
I["drawing-centerline"] = P("M3 6 H21 M3 18 H21", s=INK) + P("M1.5 12 H22.5", s=DR, da=DASHDOT)
I["drawing-text"] = P("M5.5 5.5 H18.5 M12 5.5 V19 M9 19 H15", s=DR) + P("M5.5 5.5 V7.5 M18.5 5.5 V7.5", s=DR)
I["drawing-leader"] = (P("M4 20 L11 11 H21", s=DR) + P("M4 20 L4.6 16.4 M4 20 L7.6 19.2", s=DR) +
                       P("M12 7 H21 M12 4 H18", s=GREY))
I["drawing-balloon"] = P("M11 13 L4 20", s=DR) + dot(4, 20, DR, 1.5) + balloon(15, 9, 6) + P("M15 6.5 V11.5 M13.6 7.6 L15 6.5", s=DR)
I["drawing-balloon-all"] = (balloon(6.5, 6.5, 4) + balloon(17.5, 6.5, 4) + balloon(12, 17.5, 4) +
                            dot(6.5, 6.5, DR, 1.2) + dot(17.5, 6.5, DR, 1.2) + dot(12, 17.5, DR, 1.2))
I["drawing-parts-list"] = table(2.5, 4.5, 19, 15, 4, 3, DR)
I["drawing-toggle-hidden-lines"] = R(3.5, 5, 17, 14, f=WH) + P("M3.5 12 H20.5 M9 5 V19", s=DR, da="2 2")
I["drawing-toggle-tangent-edges"] = (P("M3.5 19.5 V12 A7.5 7.5 0 0 1 11 4.5 H20.5", s=INK) +
                                     P("M3.5 12 H8 M11 4.5 V9", s=DR, da="1.5 1.5") + dot(3.5, 12, DR, 1.4) + dot(11, 4.5, DR, 1.4))
I["drawing-export-pdf"] = export_doc(R(5.5, 8, 7, 6, s=DR) + P("M5.5 16.5 H9", s=RED), DR)
I["drawing-export-svg"] = export_doc(P("M5.5 15.5 C5.5 9 12.5 13 12.5 7.5", s=DR) + P("M5.5 15.5 L8.5 12.5 M12.5 7.5 L9.5 10.5", s=GREY) +
                                     dot(8.5, 12.5, DR, 1.1) + dot(9.5, 10.5, DR, 1.1), DR)
I["drawing-export-dxf"] = export_doc(P("M5.5 15.5 L9.5 8", s=DR) + C(10.5, 13.5, 2.2, s=DR), DR)
I["drawing-export-csv"] = export_doc(P("M5.5 8.5 H12.5 M5.5 12 H12.5 M5.5 15.5 H12.5 M9 8.5 V15.5", s=DR), DR)

# ===== app / file =============================================================================
I["document"] = doc(4.5, 2.5, 15, 19) + P("M8 11 H16 M8 14.5 H16 M8 18 H13", s=GREY)
I["new"] = doc(4.5, 2.5, 15, 19) + plus(12, 13.5, UI, 3.5)
I["open"] = P("M2.5 5.5 H8.5 L10.5 7.5 H18.5 V10", f=WH) + P("M2.5 5.5 V19 H17.5 L21.5 10 H6.5 L2.5 19", f=WH) + P("M6.5 10 L2.5 19", s=UI)
I["folder"] = P("M2.5 6 H9 L11 8 H21.5 V19 H2.5 Z", f=WH) + P("M2.5 10.5 H21.5", s=GREY)
I["save"] = floppy()
I["save-as"] = (P("M4 4 H14.5 L17 6.5 V11", f=WH) + P("M4 4 V20 H11", f="none") + P("M8 4 V8.5 H13.5 V4") +
                pencil(13, 22) + P("M7.5 20 V13.5 H14", s=UI))
I["undo"] = P("M9 14.5 L4 9.5 L9 4.5", s=INK) + P("M4 9.5 H15 A5 5 0 0 1 15 19.5 H10", s=UI)
I["redo"] = P("M15 14.5 L20 9.5 L15 4.5", s=INK) + P("M20 9.5 H9 A5 5 0 0 0 9 19.5 H14", s=UI)
I["app-redo-alt"] = P("M19.5 12 A7.5 7.5 0 1 1 17.3 6.7", s=UI) + P("M19.5 3 V8 H14.5", s=INK) + dot(12, 12, UI, 1.4)
I["delete"] = (P("M4 6.5 H20 M9.5 6.5 V4 H14.5 V6.5", s=INK) + P("M6 6.5 L7 20.5 H17 L18 6.5", f=WH) +
               P("M10 10 V17 M14 10 V17", s=RED))
I["app-edit-properties"] = (R(3, 3, 15, 18, 1.5, f=WH) + P("M6.5 8 H14.5 M6.5 12 H14.5 M6.5 16 H10", s=GREY) +
                            dot(6.5, 8, UI, 1.2) + pencil(13, 22))
I["parameters"] = (P("M4 6 H20 M4 12 H20 M4 18 H20", s=GREY) + C(9, 6, 2.2, s=UI, f=WH) + C(15, 12, 2.2, s=UI, f=WH) +
                   C(7, 18, 2.2, s=UI, f=WH))
I["compute"] = P("M20 12 A8 8 0 1 1 17.66 6.34", s=INK) + P("M20 3.5 V8.5 H15", s=UI) + P("M10 9 L15 12 L10 15 Z", s=UI)
I["command-line"] = R(2.5, 4, 19, 16, 1.5, f=WH) + P("M6 9 L9.5 12 L6 15", s=UI) + P("M11.5 15.5 H17", s=INK)
I["help"] = (C(12, 12, 9) + P("M9.2 9.3 A2.9 2.9 0 1 1 13.2 12 C12.4 12.4 12 13 12 14", s=UI) + dot(12, 17.3, UI, 1.2))
I["app-help"] = (R(2, 6, 20, 12, 2, f=WH) + "".join(R(x, 9, 2, 2, 0.4, s=GREY) for x in (5, 8.5, 12, 15.5)) +
                 P("M7 15 H17", s=UI))
I["search"] = C(10.5, 10.5, 6.5) + P("M15.3 15.3 L20.5 20.5", s=UI)
I["settings"] = (C(12, 12, 3, s=UI) +
                 P("M12 2.5 V5 M12 19 V21.5 M2.5 12 H5 M19 12 H21.5 M5.3 5.3 L7 7 M17 17 L18.7 18.7 M5.3 18.7 L7 17 M17 7 L18.7 5.3") +
                 C(12, 12, 7))
# G2: Units (ruler with ticks) and Appearance (paint drop over a body)
I["app-units"] = (R(2.5, 8, 19, 8, 1, f=WH) +
                  P("M6 8 V11.5 M9.5 8 V10.5 M13 8 V11.5 M16.5 8 V10.5 M20 8 V11.5", s=UI))
I["app-appearance"] = (cube(10, 13, 0.8, fill=BODY) +
                       P("M18 3 C18 3 21.5 7.5 21.5 9.8 A3.5 3.5 0 0 1 14.5 9.8 C14.5 7.5 18 3 18 3 Z", s=UI, f=UI, fo="0.85"))
I["file-menu"] = P("M4 6 H20 M4 12 H20 M4 18 H20")
I["workspace-design"] = cube(12, 12, 1.1, fill=WH, stroke=UI)
I["eye"] = eye()
I["eye-off"] = eye(iris=GREY) + P("M4 20 L20 4", s=INK)
I["visibility"] = eye() + P("M4 20 L20 4", s=UI)
I["app-toggle-visibility"] = P("M2 12 s4 -6.5 10 -6.5 C12 5.5 12 18.5 12 18.5 C6 18.5 2 12 2 12 z", f=WH) + P("M12 5.5 c6 0 10 6.5 10 6.5 s-4 6.5 -10 6.5", s=GREY, da="2 2") + P("M12 9 A3 3 0 0 0 12 15", s=UI)
I["app-show-all"] = eye(9, 13) + P("M3.5 4 H20.5 M12 7 H20.5", s=GREY) + plus(19, 18, UI, 2.5)
I["ok"] = C(12, 12, 9, s=AS, f=WH) + P("M7.5 12.5 L10.5 15.5 L16.5 9", s=AS)
I["cancel"] = P("M6 6 L18 18 M18 6 L6 18", s=RED)
I["error"] = C(12, 12, 9, s=RED, f=WH) + P("M8.5 8.5 L15.5 15.5 M15.5 8.5 L8.5 15.5", s=RED)
I["warning"] = P("M12 3 L21.5 20 H2.5 Z", s=CO, f=WH) + P("M12 9.5 V14", s=INK) + dot(12, 17, INK, 1.1)
I["body"] = cube(12, 12, 1.1)

# ===== view / navigation / display (accent cyan) =============================================
for f in ("front", "back", "top", "bottom", "left", "right"):
    I["view-" + f] = view_cube(f)
_iso = cube_pts(12, 12.5, 1.0)
I["view-iso"] = (cube(12, 12.5, 1.0, fill=WH) + P(poly(_iso["tl"], _iso["tb"], _iso["tr"], _iso["tf"]), f=UI, fo="0.25", s="none") +
                 P(poly(_iso["tl"], _iso["tf"], _iso["bf"], _iso["bl"]), f=UI, fo="0.45", s="none") +
                 P(poly(_iso["tf"], _iso["tr"], _iso["br"], _iso["bf"]), f=UI, fo="0.65", s="none") + dot(12, 12, INK, 1.4))
I["views"] = (R(3, 3, 8, 8, s=INK) + R(13, 3, 8, 8, s=INK) + R(3, 13, 8, 8, s=INK) + P("M13 17 L17 13 L21 17 L17 21 Z", s=UI, f=UI, fo="0.3"))
I["home"] = P("M3.5 11.5 L12 4 L20.5 11.5", s=INK) + P("M6 9.5 V20 H18 V9.5", f=WH) + P("M10 20 V14.5 H14 V20", s=UI)
I["fit"] = P("M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15") + R(8, 8, 8, 8, s=UI)
I["look-at"] = R(4, 4, 16, 16, 1, f=WH) + P("M8 12 H16 M12 8 V16", s=GREY) + C(12, 12, 2.5, s=UI, f=UI, fo="0.35")
I["view-look-at"] = (P("M10 3.5 L20.5 3.5 V14 L10 14 Z", f=UI, fo="0.3", s=UI) + P("M3 21 L11.5 12.5", s=INK) +
                     P("M7.5 12.5 H11.5 V16.5", s=INK))
I["orbit"] = E(12, 12, 9, 4) + P("M12 3 V21", s=UI) + arrow_head(21, 12, "down", INK, 2.5)
for d in ("left", "right", "up", "down"):
    I["view-orbit-" + d] = orbit(d)
I["pan"] = P("M12 3 V21 M3 12 H21") + P("M9 6 L12 3 L15 6 M9 18 L12 21 L15 18 M6 9 L3 12 L6 15 M18 9 L21 12 L18 15", s=UI)
I["zoom"] = C(10.5, 10.5, 6.5) + P("M15.3 15.3 L20.5 20.5") + P("M7.5 10.5 H13.5 M10.5 7.5 V13.5", s=UI)
I["view-orthographic"] = R(3.5, 8, 12, 12, f=WH) + P("M3.5 8 L8.5 3.5 H20.5 L15.5 8 M20.5 3.5 V15.5 L15.5 20", s=INK) + P("M3.5 22 H15.5 M22 8 V20", s=UI)
I["view-perspective"] = (P("M3.5 8 H15.5 V20 H3.5 Z", f=WH) + P("M3.5 8 L9 4.5 H17.5 L15.5 8 M17.5 4.5 V14.5 L15.5 20") +
                         P("M3.5 22 L9.5 18 M15.5 22 L9.5 18", s=UI))
I["display"] = cube(12, 12, 1.1, fill=BODY, stroke=INK, inner=UI)
_v = cube_pts(12, 12, 1.1)
I["view-shaded"] = (P(poly(_v["tl"], _v["tb"], _v["tr"], _v["tf"]), f=BODY, s=BODY) +
                    P(poly(_v["tl"], _v["tf"], _v["bf"], _v["bl"]), f=GREY, s=GREY) +
                    P(poly(_v["tf"], _v["tr"], _v["br"], _v["bf"]), f=INK, s=INK))
I["view-shaded-edges"] = cube(12, 12, 1.1, fill=BODY, stroke=INK) + P("M4.3 7.6 L12 3.75 L19.7 7.6 L12 11.45 Z", f=WH, fo="0.6", s="none")
_w = cube_pts(12, 12, 1.1)
I["view-wireframe"] = (cube(12, 12, 1.1, fill="none", stroke=UI) +
                       P("M%g %g L%g %g M%g %g L%g %g M%g %g L%g %g" % (_w["bb"] + _w["bl"] + _w["bb"] + _w["br"] + _w["bb"] + _w["tb"]), s=UI))
I["view-hidden-line"] = (cube(12, 12, 1.1, fill=WH, stroke=INK) +
                         P("M%g %g L%g %g M%g %g L%g %g M%g %g L%g %g" % (_w["bb"] + _w["bl"] + _w["bb"] + _w["br"] + _w["bb"] + _w["tb"]), s=UI, da="1.5 2"))
I["grid"] = R(3.5, 3.5, 17, 17, 1) + P("M9.17 3.5 V20.5 M14.83 3.5 V20.5 M3.5 9.17 H20.5 M3.5 14.83 H20.5", s=GREY)
I["view-grid"] = P("M2.5 19 L7.5 9 H21.5 L16.5 19 Z", f=WH) + P("M7.17 19 L11.17 9 M11.83 19 L15.83 9 M5 14 H19", s=UI)
I["view-snap"] = (P("M6 4 V12 A6 6 0 0 0 18 12 V4 H14.5 V12 A2.5 2.5 0 0 1 9.5 12 V4 Z", f=WH, s=UI) +
                  P("M6 7.5 H9.5 M14.5 7.5 H18", s=INK) + dot(12, 21, INK, 1.3))

# ===== selection filters =====================================================================
I["select"] = cursor(INK, WH)
_s = cube_pts(12, 12, 1.1)
I["select-body"] = cube(12, 12, 1.1, fill=UI, stroke=INK) + P(poly(_s["tl"], _s["tb"], _s["tr"], _s["br"], _s["bf"], _s["bl"]), f=WH, fo="0.6", s="none") + P("M%g %g L%g %g L%g %g M%g %g L%g %g" % (_s["tl"] + _s["tf"] + _s["tr"] + _s["tf"] + _s["bf"]))
I["select-component"] = cube(12, 12, 1.1, fill=WH, stroke=UI) + dot(12, 11.5, AS, 2)
I["select-edge"] = cube(12, 12, 1.1, fill=WH) + P("M%g %g L%g %g" % (_s["tf"] + _s["bf"]), s=UI) + dot(_s["tf"][0], _s["tf"][1], UI, 1.4) + dot(_s["bf"][0], _s["bf"][1], UI, 1.4)
I["select-face"] = cube(12, 12, 1.1, fill=WH) + P(poly(_s["tf"], _s["tr"], _s["br"], _s["bf"]), f=UI, fo="0.45", s=UI)
I["select-vertex"] = cube(12, 12, 1.1, fill=WH) + C(_s["tf"][0], _s["tf"][1], 2.6, f=UI, s="none")

# ===== timeline ==============================================================================
I["timeline-first"] = P("M6 5 V19", s=INK) + P("M18 5 L9 12 L18 19 Z", s=UI, f=UI, fo="0.3")
I["timeline-prev"] = P("M16 5 L7 12 L16 19 Z", s=UI, f=UI, fo="0.3")
I["timeline-next"] = P("M8 5 L17 12 L8 19 Z", s=UI, f=UI, fo="0.3")
I["timeline-last"] = P("M18 5 V19", s=INK) + P("M6 5 L15 12 L6 19 Z", s=UI, f=UI, fo="0.3")

# ===== round 2: sketch curves, patterns and transforms (blue) ================================
I["sketch-ellipse"] = E(12, 12, 8.5, 5.5, s=SK) + P("M12 12 H20.5", s=GREY) + dot(12, 12) + dot(20.5, 12) + dot(12, 6.5)
I["sketch-ellipse-arc"] = (P("M3.5 14 A8.5 5.5 0 0 1 20.5 14", s=SK) + P("M3.5 14 H20.5", s=GREY, da=DASH) +
                           dot(3.5, 14) + dot(20.5, 14) + dot(12, 8.5))
_spl = "M3 17 C7 3 10 21 14 10 C16 5 18.5 8 21 6"
I["sketch-spline-fit"] = P(_spl, s=SK) + dot(3, 17) + dot(9.2, 12.2) + dot(14, 10) + dot(21, 6)
I["sketch-spline-control"] = (P("M3 17 L7 3 L14 10 L21 6", s=GREY, da=DASH) + P(_spl, s=SK) +
                              R(1.75, 15.75, 2.5, 2.5, s=INK, f=WH) + R(5.75, 1.75, 2.5, 2.5, s=INK, f=WH) +
                              R(12.75, 8.75, 2.5, 2.5, s=INK, f=WH) + R(19.75, 4.75, 2.5, 2.5, s=INK, f=WH))
I["sketch-text"] = P("M5 19 L11 5 L17 19 M7.5 13.5 H14.5", s=SK) + P("M3 21.5 H19", s=GREY, da=DASH) + dot(3, 21.5)
I["sketch-pattern-rectangular"] = (C(6.5, 6.5, 2.6, s=INK) + C(17.5, 6.5, 2.6, s=SK) + C(6.5, 17.5, 2.6, s=SK) +
                                   C(17.5, 17.5, 2.6, s=SK) + P("M6.5 9.1 V14.9 M9.1 6.5 H14.9", s=GREY, da=DASH))
I["sketch-pattern-circular"] = C(12, 12, 7, s=GREY, da=DASH) + dot(12, 12, INK, 1.2) + "".join(
    C(round(12 + 7 * x, 2), round(12 + 7 * y, 2), 2.2, s=(INK if i == 0 else SK))
    for i, (x, y) in enumerate([(0, -1), (0.866, -0.5), (0.866, 0.5), (0, 1), (-0.866, 0.5), (-0.866, -0.5)]))
I["sketch-move"] = R(8.5, 8.5, 7, 7, s=SK) + four_way(SK, 10, 12, 12, 5.5)
I["sketch-rotate"] = (R(9, 9, 8, 8, s=GREY, da=DASH) + P("M5.5 9.5 L10 5 L14.5 9.5 L10 14 Z", s=SK) +
                      P("M18.5 5 A8 8 0 0 1 20 13", s=INK) + arrow_head(20, 13, "down", INK, 2) + dot(12, 12, INK, 1.2))
I["sketch-scale"] = (R(3.5, 12.5, 8, 8, s=SK) + R(3.5, 3.5, 17, 17, s=GREY, da=DASH) + P("M11.5 12.5 L19 5", s=INK) +
                     P("M14.5 5 H19 V9.5", s=INK))
I["sketch-chamfer"] = (P("M5 20 V5 H20", s=GREY, da=DASH) + P("M5 20 V11 L11 5 H20", s=SK) + dot(5, 11) + dot(11, 5))

# ===== round 2: primitives, thread, rib, thicken, scale, split face ==========================
BOX = cube(12, 11.5, 1.05, fill=BODY) + P("M3 21 H21", s=OR)
I["part-box"] = BOX
CYLINDER = P("M6 6.5 V17 A6 2.5 0 0 0 18 17 V6.5", f=BODY) + E(12, 6.5, 6, 2.5, f=WH) + P("M3 21 H21", s=OR)
I["part-cylinder"] = CYLINDER
SPHERE = C(12, 12, 8, f=BODY) + E(12, 12, 8, 3, s=GREY, da=DASH) + P("M12 4 A3 8 0 0 0 12 20", s=GREY, da=DASH)
I["part-sphere"] = SPHERE
TORUS = E(12, 12, 9, 5, f=BODY) + E(12, 12, 3.5, 1.8, f=WH) + P("M3 20.5 H21", s=OR)
I["part-torus"] = TORUS
COIL = (P("M12 2 V22", s=GREY, da=DASHDOT) +
        P("M6 4.5 H15 A3 2.25 0 0 1 15 9 H9 A3 2.25 0 0 0 9 13.5 H15 A3 2.25 0 0 1 15 18 H9 A3 2.25 0 0 0 9 20.5", s=INK) +
        P("M6 4.5 L4 6.5", s=OR))
I["part-coil"] = COIL
THREAD = (P("M7 4 H17 V20 H7 Z", f=BODY) + P("M7 8 L17 6.5 M7 11.5 L17 10 M7 15 L17 13.5 M7 18.5 L17 17", s=OR) +
          P("M12 1.5 V3 M12 21 V22.5", s=GREY, da=DASHDOT))
I["part-thread"] = THREAD
RIB = (P("M3 20 H21 V15 H3 Z", f=BODY) + P("M10.5 15 V6 H13.5 V15", f=BODY, s=OR) + P("M5 6 H10.5", s=SK) + dot(5, 6, SK, 1.2))
I["part-rib"] = RIB
THICKEN = (P("M3 19 Q12 5 21 19 L21 15 Q12 1 3 15 Z", f=BODY) + P("M3 19 Q12 5 21 19", s=GREY, da=DASH) +
           P("M12 11 V7.5", s=OR) + arrow_head(12, 7.5, "up", OR, 2))
I["part-thicken"] = THICKEN
SCALE_BODY = (cube(8.5, 15.5, 0.5, fill=BODY) + cube(13, 11, 1.0, fill="none", stroke=GREY) + P("M11.5 12.5 L19 5", s=OR) +
              P("M14.5 5 H19 V9.5", s=OR))
I["part-scale"] = SCALE_BODY
_sf = cube_pts(12, 12, 1.1)
SPLIT_FACE = (cube(12, 12, 1.1, fill=BODY) +
              P(poly(_sf["tf"], _sf["tr"], _sf["br"], _sf["bf"]), f=OR, fo="0.25", s="none") +
              P("M%g %g L%g %g" % ((_sf["tf"][0] + _sf["tr"][0]) / 2, (_sf["tf"][1] + _sf["tr"][1]) / 2,
                                    (_sf["bf"][0] + _sf["br"][0]) / 2, (_sf["bf"][1] + _sf["br"][1]) / 2), s=OR, da=DASH))
I["part-split-face"] = SPLIT_FACE
I["assembly-paste-new"] = (R(4, 4, 16, 18, 1.5, f=WH) + R(8.5, 2.5, 7, 3.5, 1, f=WH) + component(11, 13, 0.55) +
                           plus(17, 18.5, AS, 2.5))

# ===== round 2: drawing annotations ==========================================================
I["drawing-hole-callout"] = (C(7.5, 16, 4.5, s=INK) + P("M10.7 12.8 L15 8.5 H21.5", s=DR) + dot(10.7, 12.8, DR, 1.3) +
                             C(16.5, 5.5, 2, s=DR) + P("M15 7.5 L18 3.5", s=DR))
I["drawing-thread-callout"] = (C(7.5, 16, 4.5, s=INK) + C(7.5, 16, 3, s=GREY, da=DASH) +
                               P("M10.7 12.8 L15 8.5 H21.5", s=DR) + dot(10.7, 12.8, DR, 1.3) +
                               P("M14.5 7 V2.5 L17 5.5 L19.5 2.5 V7", s=DR))
I["drawing-feature-control-frame"] = (R(2.5, 8, 19, 8, f=WH) + P("M8.5 8 V16 M15.5 8 V16", s=INK) +
                                      C(5.5, 12, 1.8, s=DR) + P("M5.5 9.5 V14.5 M3 12 H8", s=DR) +
                                      C(12, 12, 1.4, s=INK) + P("M10.6 13.4 L13.4 10.6", s=INK) +
                                      P("M17 14.5 L18.5 9.5 L20 14.5 M17.6 13 H19.4", s=INK))
I["drawing-datum"] = (R(9, 3, 6, 6, f=WH) + P("M10.5 7.5 L12 4.5 L13.5 7.5 M11 6.5 H13", s=DR) + P("M12 9 V16", s=INK) +
                      P("M9 20 L12 16 L15 20 Z", f=DR, s=DR) + P("M4 20 H20", s=INK))
I["drawing-surface-texture"] = (P("M5 11 L9.5 19.5 L16.5 5", s=DR) + P("M9.5 11 H20", s=DR) + P("M3 21.5 H21", s=INK) +
                                P("M3 21.5 L5 19 M7 21.5 L9 19 M11 21.5 L13 19 M15 21.5 L17 19 M19 21.5 L21 19", s=GREY))
I["drawing-dimension-ordinate"] = (P("M3 20 H21 M3 20 V4", s=INK) + dot(3, 20, DR, 1.6) +
                                   P("M8 18.5 V7 M14 18.5 V7 M20 18.5 V7", s=DR) + P("M6.5 5 H9.5 M12.5 5 H15.5 M18.5 5 H21.5", s=GREY))
I["drawing-dimension-baseline"] = (P("M3 21 H21", s=INK) + P("M3 21 V4 M9 21 V8 M15 21 V12 M21 21 V16", s=GREY) +
                                   P("M3.5 7 H20.5 M3.5 11 H14.5 M3.5 15 H8.5", s=DR) +
                                   arrow_head(20.5, 7, "right", DR, 1.5) + arrow_head(14.5, 11, "right", DR, 1.5) +
                                   arrow_head(8.5, 15, "right", DR, 1.5))
I["drawing-dimension-chain"] = (P("M3 21 H21", s=INK) + P("M3 21 V7 M9 21 V7 M15 21 V7 M21 21 V7", s=GREY) +
                                P("M3.5 10 H8.5 M9.5 10 H14.5 M15.5 10 H20.5", s=DR) +
                                arrow_head(8.5, 10, "right", DR, 1.5) + arrow_head(14.5, 10, "right", DR, 1.5) +
                                arrow_head(20.5, 10, "right", DR, 1.5) + arrow_head(3.5, 10, "left", DR, 1.5))
I["drawing-break-view"] = (P("M2.5 6 H9 L8 10 L10 14 L9 18 H2.5 Z", f=WH) + P("M21.5 6 H15 L14 10 L16 14 L15 18 H21.5 Z", f=WH) +
                           P("M9 6 L8 10 L10 14 L9 18 M15 6 L14 10 L16 14 L15 18", s=DR) + P("M2.5 12 H5.5 M18.5 12 H21.5", s=GREY))
_hatch = P("M6 17 L8 15 M6 14.5 L8 12.5 M8.5 17 L10 15.5", s=GREY)
I["drawing-section-view-stepped"] = (R(4.5, 5.5, 15, 13, f=WH) + P("M2 9 H11 V15 H22", s=DR, da=DASHDOT) +
                                     arrow_head(2, 9, "up", DR, 1.6) + arrow_head(22, 15, "down", DR, 1.6) + _hatch)
I["drawing-section-view-aligned"] = (R(4.5, 5.5, 15, 13, f=WH) + P("M2 19 L12 12 H22", s=DR, da=DASHDOT) +
                                     dot(12, 12, DR, 1.3) + arrow_head(22, 12, "down", DR, 1.6) + _hatch)
I["drawing-revision-table"] = table(2.5, 4, 19, 13, 3, 3, DR) + P("M9.5 22 L12 18 L14.5 22 Z", f=WH, s=DR)
I["drawing-revision"] = (P("M12 4 L21 20 H3 Z", f=WH, s=DR) + P("M10 17 L12 11 L14 17 M10.8 15 H13.2", s=INK))
I["drawing-edit-callout"] = (C(6, 7.5, 3.5, s=INK) + P("M8.5 9.9 L11.5 13 H17.5", s=DR) + dot(8.5, 9.9, DR, 1.2) + pencil(13, 21))

# ===== round 2: scripting shell verbs (UI accent) ============================================
I["app-ok"] = C(12, 12, 9, s=UI, f=WH) + P("M7.5 12.5 L10.5 15.5 L16.5 9", s=AS)
I["app-cancel"] = C(12, 12, 9, s=UI, f=WH) + cross(12, 12, RED, 3.5)
I["app-select"] = cursor(UI, WH, -2, -1) + R(14, 14, 7, 7, 1, s=INK, da=DASH)
I["app-pick"] = C(12, 12, 6, s=UI) + P("M12 2.5 V7 M12 17 V21.5 M2.5 12 H7 M17 12 H21.5", s=INK) + dot(12, 12, UI, 1.5)
I["app-input"] = R(2.5, 7, 19, 10, 1.5, f=WH) + P("M6 12 H13", s=UI) + P("M16 9.5 V14.5", s=INK)
I["app-options"] = (P("M4 7 H20 M4 12 H20 M4 17 H20", s=GREY) + R(7, 5, 3, 4, 0.8, s=UI, f=WH) + R(13, 10, 3, 4, 0.8, s=UI, f=WH) +
                    R(9, 15, 3, 4, 0.8, s=UI, f=WH))
I["app-info"] = C(12, 12, 9, s=UI) + P("M12 11 V17", s=INK) + dot(12, 7.5, INK, 1.3)
I["app-list"] = P("M8 6 H20 M8 12 H20 M8 18 H20", s=INK) + dot(4.5, 6, UI, 1.5) + dot(4.5, 12, UI, 1.5) + dot(4.5, 18, UI, 1.5)
I["app-check"] = R(4, 4, 16, 18, 1.5, f=WH) + R(8.5, 2.5, 7, 3.5, 1, f=WH) + P("M8 14 L11 17 L16.5 10.5", s=AS)
I["app-rename"] = P("M12 5 V19 M9.5 5 H14.5 M9.5 19 H14.5", s=UI) + P("M3 9 H8 M3 15 H8 M16 9 H21 M16 15 H21", s=GREY)
I["app-echo"] = P("M4 4 H20 V15 H11 L6.5 19.5 V15 H4 Z", f=WH) + P("M8 8 H16 M8 11.5 H13", s=UI)
I["app-wait"] = (P("M7 3.5 H17 V6.5 L12 12 L17 17.5 V20.5 H7 V17.5 L12 12 L7 6.5 Z", f=WH) +
                 P("M9.5 18.5 H14.5 L12 15.5 Z", f=UI, s="none"))
I["app-screenshot"] = (R(2.5, 7, 19, 13, 2, f=WH) + P("M8 7 L9.5 4.5 H14.5 L16 7") + C(12, 13.5, 4, s=UI, f=WH) +
                       dot(18.5, 10, INK, 1))
I["app-tab"] = (R(2.5, 8.5, 19, 12, 1, f=WH) + P("M4 8.5 V5.5 H10 V8.5", s=UI, f=UI, fo="0.3") +
                P("M11 8.5 V5.5 H17 V8.5", s=INK))
I["app-close"] = R(2.5, 8.5, 19, 12, 1, f=WH) + P("M4 8.5 V5.5 H10 V8.5", s=INK) + cross(12, 14.5, RED, 3)

# ===== feature types (browser / timeline items) ==============================================
FEATURES = {
    "extrude": EXTRUDE, "revolve": REVOLVE, "sweep": SWEEP, "loft": LOFT, "hole": HOLE, "fillet": FILLET,
    "chamfer": CHAMFER, "shell": SHELL, "draft": DRAFT, "offsetface": OFFSET_FACE, "combine": COMBINE,
    "splitbody": SPLIT, "movebody": MOVE_BODY, "mirror": MIRROR, "rectangularpattern": RECT_PATTERN,
    "circularpattern": CIRC_PATTERN, "import": IMPORT, "offsetplane": OFFSET_PLANE, "planeatangle": PLANE_ANGLE,
    "tangentplane": TANGENT_PLANE, "midplane": MIDPLANE, "planethreepoints": PLANE_3P, "constructionaxis": AXIS,
    "constructionpoint": CPOINT, "rigidgroup": RIGID, "insertdesign": INSERT_DESIGN, "newcomponent": I["component"],
    "box": BOX, "cylinder": CYLINDER, "sphere": SPHERE, "torus": TORUS, "coil": COIL, "thread": THREAD, "rib": RIB,
    "thicken": THICKEN, "scalebody": SCALE_BODY, "splitface": SPLIT_FACE,
}
for k, v in FEATURES.items():
    I["feature-" + k] = v


def main():
    bad = [n for n in I if not n.replace("-", "").isalnum() or n != n.lower()]
    if bad:
        sys.exit("bad icon names: %s" % bad)
    written = set()
    for name in sorted(I):
        fn = name + ".svg"
        with open(os.path.join(HERE, fn), "w", encoding="utf-8", newline="\n") as f:
            f.write(HEADER + I[name] + "</svg>\n")
        written.add(fn)
    for fn in sorted(os.listdir(HERE)):
        if fn.endswith(".svg") and fn not in written and fn not in KEEP:
            os.remove(os.path.join(HERE, fn))
            print("removed", fn)
    # qrc: keep non-icon entries, regenerate the icons/ block (sorted)
    with open(QRC, encoding="utf-8") as f:
        lines = f.read().splitlines()
    head = [l for l in lines if "<file>icons/" not in l and "</qresource>" not in l and "</RCC>" not in l]
    icons = ["  <file>icons/%s</file>" % fn for fn in sorted(written | KEEP)]
    with open(QRC, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(head + icons + ["</qresource>", "</RCC>"]) + "\n")
    print("%d icons written" % len(written))


if __name__ == "__main__":
    main()
