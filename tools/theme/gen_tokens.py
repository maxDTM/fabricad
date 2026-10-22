#!/usr/bin/env python3
"""FabriCAD design-token generator (docs/plans/ui-redesign.md section 2.2). Stdlib only.

Reads resources/theme/tokens.json and writes the committed outputs:
  src/theme/ThemeTokens.inc         enums + constexpr tables for the fabricad_theme library
  docs/design/palette.md            swatches, contrast table and CVD table for review
  resources/theme/icon-palette.json icon ink and badge colours per theme (for generate_icons.py)
  resources/ui/{dark,light}/*.svg   arrows, checks and close glyphs from resources/theme/ui-svg/*.svg.tmpl
and validates resources/theme/app.qss.tmpl (placeholders resolve, no raw colour literals).

Usage:
  gen_tokens.py                 regenerate the outputs in the source tree
  gen_tokens.py --check         regenerate into a temp dir, diff with the committed files and run the
                                contrast and colour-vision checks (exit 1 on any failure; the ctest)
  gen_tokens.py --root DIR      source tree root (default: two levels above this script)
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import argparse
import difflib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import colormath as cm  # noqa: E402

THEMES = ("dark", "light")
KIND_MIN = {"text": 4.5, "ui": 3.0, "exempt": 0.0}
PLACEHOLDER = re.compile(r"@\{([A-Za-z0-9_.]+)(?:/(\d{1,3}))?\}")
RAW_COLOR = re.compile(r"#[0-9A-Fa-f]{3,8}\b|\brgba?\s*\(|\bhsla?\s*\(")


def ident(name, prefix=""):
    """'surface.panel' -> 'SurfacePanel'; 'label.sm' -> 'LabelSm'."""
    parts = re.split(r"[^A-Za-z0-9]+", name)
    return prefix + "".join(p[:1].upper() + p[1:] for p in parts if p)


def load(root):
    with open(os.path.join(root, "resources/theme/tokens.json"), encoding="utf-8") as f:
        t = json.load(f)
    colors = {k: v for k, v in t["color"].items() if not k.startswith("$")}
    for k, v in colors.items():
        for th in THEMES:
            cm.parse_hex(v[th])  # validates
    for a, target in t["alias"].items():
        if target not in colors:
            raise SystemExit("alias %s -> unknown colour %s" % (a, target))
        if a in colors:
            raise SystemExit("alias %s shadows a colour token" % a)
    return t, colors


def resolve(t, colors, name):
    if name in colors:
        return name
    if name in t["alias"]:
        return t["alias"][name]
    return None


def rgba_u32(h):
    r, g, b, a = (round(x * 255) for x in cm.parse_hex(h))
    return (r << 24) | (g << 16) | (b << 8) | a


def opaque(colors, name, theme, over=None):
    c = cm.parse_hex(colors[name][theme])
    if c[3] < 1.0:
        bg = cm.parse_hex(colors[over or "surface.panel"][theme])
        c = cm.composite(c, bg)
    return c


# ---------------------------------------------------------------------------- checks

def contrast_rows(t, colors):
    rows = []
    for fg, bg, kind in t["pairs"]:
        for name in (fg, bg):
            if name not in colors:
                raise SystemExit("pairs: unknown colour " + name)
        vals = {}
        for th in THEMES:
            bgc = opaque(colors, bg, th)
            fgc = cm.parse_hex(colors[fg][th])
            vals[th] = cm.contrast(fgc, bgc)
        ok = all(vals[th] >= KIND_MIN[kind] for th in THEMES)
        rows.append((fg, bg, kind, vals, ok))
    return rows


def cvd_rows(t, colors):
    rows = []
    for r in t["cvd"]["rows"]:
        a, b, mins, note = r[0], r[1], r[2:6], (r[6] if len(r) > 6 else "")
        vals = {}
        ok = True
        for th in THEMES:
            d = cm.cvd_deltas(opaque(colors, a, th), opaque(colors, b, th))
            vals[th] = d
            ok = ok and all(d[i] >= mins[i] for i in range(4))
        rows.append((a, b, mins, vals, ok, note))
    return rows


def validate_qss(t, colors, path):
    errors = []
    with open(path, encoding="utf-8") as f:
        text = f.read()
    # comments may mention anything; strip them before the literal check
    body = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for m in PLACEHOLDER.finditer(body):
        if m.group(1) == "theme" and not m.group(2):
            continue
        if resolve(t, colors, m.group(1)) is None:
            errors.append("unknown placeholder @{%s}" % m.group(1))
        if m.group(2) and int(m.group(2)) > 100:
            errors.append("alpha over 100 in @{%s/%s}" % (m.group(1), m.group(2)))
    stripped = PLACEHOLDER.sub("", body)
    for i, line in enumerate(stripped.splitlines(), 1):
        if RAW_COLOR.search(line):
            errors.append("line %d: raw colour literal: %s" % (i, line.strip()))
        if re.search(r"\bfont(-size|-family|-weight)?\s*:", line):
            errors.append("line %d: fonts are set in code, not QSS: %s" % (i, line.strip()))
    if "@{" in stripped:
        errors.append("malformed placeholder")
    return errors


# ---------------------------------------------------------------------------- outputs

def gen_inc(t, colors):
    names = list(colors.keys())
    styles = list(t["font"]["styles"].keys())
    out = []
    w = out.append
    w("// GENERATED by tools/theme/gen_tokens.py from resources/theme/tokens.json. Do not edit.")
    w("// Included by src/theme/Tokens.h inside namespace fabricad::theme.")
    w("// SPDX-License-Identifier: LGPL-2.1-or-later")
    w("")
    w("/// Colour tokens (docs/plans/ui-redesign.md section 4).")
    w("enum class Tok : std::uint16_t {")
    for n in names:
        w("  %s,  // %s" % (ident(n), n))
    w("  Count")
    w("};")
    w("")
    w("/// Text styles (section 3.2).")
    w("enum class TextStyle : std::uint8_t {")
    for s in styles:
        w("  %s,  // %s" % (ident(s), s))
    w("  Count")
    w("};")
    w("")
    w("namespace gen {")
    w("")
    w("inline constexpr const char* kTokName[] = {")
    for n in names:
        w('    "%s",' % n)
    w("};")
    for th in THEMES:
        w("")
        w("/// 0xRRGGBBAA, %s theme" % th)
        w("inline constexpr std::uint32_t k%s[] = {" % th.capitalize())
        for n in names:
            w("    0x%08Xu,  // %s" % (rgba_u32(colors[n][th]), n))
        w("};")
    w("")
    w("struct Alias {")
    w("  const char* name;")
    w("  Tok target;")
    w("};")
    w("inline constexpr Alias kAliases[] = {")
    for a, target in t["alias"].items():
        w('    {"%s", Tok::%s},' % (a, ident(target)))
    w("};")
    w("")
    w("struct FontSpec {")
    w("  const char* name;")
    w("  double px;")
    w("  int weight;")
    w("  double tracking;")
    w("  bool caps;")
    w("};")
    w("inline constexpr FontSpec kFonts[] = {")
    for s in styles:
        f = t["font"]["styles"][s]
        w('    {"%s", %s, %d, %s, %s},' % (s, repr(float(f["px"])), f["weight"], repr(float(f["tracking"])),
                                         "true" if f["caps"] else "false"))
    w("};")
    w("inline constexpr const char* kFontFamilies[] = {%s};" % ", ".join('"%s"' % x for x in t["font"]["families"]))
    w("inline constexpr const char* kFontFiles[] = {%s};" % ", ".join('"%s"' % x for x in t["font"]["files"]))
    w("")
    w("// sizes (logical px)")
    for k, v in t["size"].items():
        if isinstance(v, list):
            w("inline constexpr int k%s[] = {%s};" % (ident(k), ", ".join(str(x) for x in v)))
        else:
            w("inline constexpr int k%s = %d;" % (ident(k), v))
    w("")
    w("struct Pair {")
    w("  Tok fg, bg;")
    w("  double minRatio;")
    w("};")
    w("inline constexpr Pair kPairs[] = {")
    for fg, bg, kind in t["pairs"]:
        w("    {Tok::%s, Tok::%s, %s},  // %s" % (ident(fg), ident(bg), repr(KIND_MIN[kind]), kind))
    w("};")
    w("")
    w("struct CvdRow {")
    w("  Tok a, b;")
    w("  double minDeltaE[4];  // normal, protan, deutan, tritan")
    w("};")
    w("inline constexpr CvdRow kCvd[] = {")
    for r in t["cvd"]["rows"]:
        w("    {Tok::%s, Tok::%s, {%s}}," % (ident(r[0]), ident(r[1]), ", ".join(repr(float(x)) for x in r[2:6])))
    w("};")
    w("")
    w("}  // namespace gen")
    return "\n".join(out) + "\n"


def swatch(h):
    return "`%s`" % h


def gen_palette_md(t, colors, crow, vrow):
    out = []
    w = out.append
    w("# FabriCAD palette")
    w("")
    w("GENERATED by `tools/theme/gen_tokens.py` from `resources/theme/tokens.json`; do not edit. Contrast is the WCAG 2.x")
    w("ratio (alpha colours composited over the background, or over `surface.panel` when they are the background).")
    w("ΔE00 is CIEDE2000 on Machado 2009 simulations (severity 1). See `docs/plans/ui-redesign.md` section 4.")
    w("")
    w("## Colours")
    w("")
    w("| Token | Dark | Light |")
    w("|---|---|---|")
    for n, v in colors.items():
        w("| `%s` | %s | %s |" % (n, swatch(v["dark"]), swatch(v["light"])))
    w("")
    w("## Aliases")
    w("")
    w("| Alias | Token |")
    w("|---|---|")
    for a, target in t["alias"].items():
        w("| `%s` | `%s` |" % (a, target))
    w("")
    w("## Contrast (T = text ≥ 4.5, U = UI ≥ 3.0, X = exempt)")
    w("")
    w("| Foreground | Background | Kind | Dark | Light | |")
    w("|---|---|---|---|---|---|")
    for fg, bg, kind, vals, ok in crow:
        w("| `%s` | `%s` | %s | %.2f | %.2f | %s |" % (fg, bg, {"text": "T", "ui": "U", "exempt": "X"}[kind],
                                                    vals["dark"], vals["light"], "pass" if ok else "**FAIL**"))
    w("")
    w("## Colour vision (ΔE00 normal / protan / deutan / tritan; floor in brackets, 0 = not gated)")
    w("")
    w("| A | B | Dark | Light | Floor | | Note |")
    w("|---|---|---|---|---|---|---|")
    for a, b, mins, vals, ok, note in vrow:
        f = lambda d: " / ".join("%.1f" % x for x in d)  # noqa: E731
        w("| `%s` | `%s` | %s | %s | %s | %s | %s |" % (a, b, f(vals["dark"]), f(vals["light"]),
                                                      " / ".join("%g" % x for x in mins), "pass" if ok else "**FAIL**", note))
    w("")
    w("## Text styles")
    w("")
    w("Families: %s." % ", ".join(t["font"]["families"]))
    w("")
    w("| Style | px | Weight | Tracking | Case |")
    w("|---|---|---|---|---|")
    for s, f in t["font"]["styles"].items():
        w("| `%s` | %g | %d | %+g | %s |" % (s, f["px"], f["weight"], f["tracking"], "CAPS" if f["caps"] else "as typed"))
    return "\n".join(out) + "\n"


def gen_icon_palette(colors):
    d = {}
    for th in THEMES:
        d[th] = {n: colors[n][th] for n in colors if n.startswith("icon.") or n.startswith("accent.") and n in
                 ("accent.indicator", "accent.text") or n in ("live.base", "live.ink")}
    return json.dumps(d, indent=2, sort_keys=True) + "\n"


def render_svg(t, colors, text, theme):
    def sub(m):
        name = resolve(t, colors, m.group(1))
        if name is None:
            raise SystemExit("svg template: unknown placeholder @{%s}" % m.group(1))
        h = colors[name][theme]
        return h[:7]  # SVG gets the opaque colour; alpha goes into an opacity attribute when needed
    return PLACEHOLDER.sub(sub, text)


def outputs(root):
    """{relative path: fn(t, colors, contrast rows, cvd rows) -> text} for every generated file."""
    out = {
        "src/theme/ThemeTokens.inc": lambda t, c, cr, vr: gen_inc(t, c),
        "docs/design/palette.md": lambda t, c, cr, vr: gen_palette_md(t, c, cr, vr),
        "resources/theme/icon-palette.json": lambda t, c, cr, vr: gen_icon_palette(c),
    }
    tdir = os.path.join(root, "resources/theme/ui-svg")
    for name in sorted(os.listdir(tdir)):
        if not name.endswith(".svg.tmpl"):
            continue
        with open(os.path.join(tdir, name), encoding="utf-8") as f:
            text = f.read()
        for th in THEMES:
            out["resources/ui/%s/%s" % (th, name[:-5])] = (
                lambda t, c, cr, vr, text=text, th=th: render_svg(t, c, text, th))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    args = ap.parse_args()
    root = args.root
    t, colors = load(root)
    crow = contrast_rows(t, colors)
    vrow = cvd_rows(t, colors)
    failures = []
    for fg, bg, kind, vals, ok in crow:
        if not ok:
            failures.append("contrast %s on %s (%s): dark %.2f light %.2f, need %.1f"
                            % (fg, bg, kind, vals["dark"], vals["light"], KIND_MIN[kind]))
    for a, b, mins, vals, ok, _ in vrow:
        if not ok:
            failures.append("cvd %s vs %s: dark %s light %s, floor %s" % (
                a, b, ["%.1f" % x for x in vals["dark"]], ["%.1f" % x for x in vals["light"]], mins))
    qss_errors = validate_qss(t, colors, os.path.join(root, "resources/theme/app.qss.tmpl"))
    failures += ["app.qss.tmpl: " + e for e in qss_errors]

    gen = outputs(root)
    if args.check:
        for rel, fn in gen.items():
            want = fn(t, colors, crow, vrow)
            path = os.path.join(root, rel)
            try:
                with open(path, encoding="utf-8") as f:
                    have = f.read()
            except OSError:
                have = ""
            if have != want:
                diff = "".join(difflib.unified_diff(have.splitlines(True), want.splitlines(True), rel + " (committed)",
                                                    rel + " (generated)", n=1))
                failures.append("%s is out of date; run tools/theme/gen_tokens.py\n%s" % (rel, diff[:4000]))
        for f in failures:
            print("FAIL:", f)
        print("theme_tokens: %d colours, %d contrast pairs, %d cvd rows, %d failures"
              % (len(colors), len(crow), len(vrow), len(failures)))
        return 1 if failures else 0

    for rel, fn in gen.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            f.write(fn(t, colors, crow, vrow))
        print("wrote", rel)
    for f in failures:
        print("FAIL:", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
