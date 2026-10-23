# FabriCAD icon set

Own artwork, LGPL-2.1-or-later. Fusion-like flat line icons in two themes: `dark/` and `light/` hold
the same names with the palette of that theme. Every SVG except
`fabricad.svg` (the application logo) is **generated** by `generate_icons.py`; edit the
drawings there, not the SVGs, then run

```sh
python3 resources/icons/generate_icons.py   # rewrites dark/*.svg, light/*.svg and the icons/ block of resources/fabricad.qrc
```

The script is deterministic (same script, same bytes) and removes SVGs it no longer generates.

**Style.** The committed set is the `mono` style (v6 mockup): ink line icons, neutral body fill, at
most one accent per icon (violet `icon.badge.violet` for a feature's action mark, red for
remove/cut/error, green for OK checks, amber for the warning triangle). The category colours in the
table below apply to the `colour` style, which restores the earlier set byte for byte:
`python3 resources/icons/generate_icons.py --style colour` (or `FABRICAD_ICON_STYLE=colour`).
Contact sheets: `docs/uiux/ui2/icons/`. The application icon is the hand-made
`app/fabricad-app.svg` (`app/render_app_icons.sh` renders hicolor PNGs from it).

## Style rules (enforced by `src/app/tests/test_icons.cpp`)

* Root element exactly: `width="24" height="24" viewBox="0 0 24 24" fill="none"
  stroke="<icon.ink of the theme>" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"`.
* One stroke width: 1.5. Dots and solid marks are filled shapes with `stroke="none"`.
* Elements: `g path circle ellipse rect line polyline polygon` only. No `<text>` (fonts differ
  between machines and are unreadable at 16 px), no gradients, images, `style`/`class`, `url()`.
* Colours `#RRGGBB` from the palette below only; translucency via `fill-opacity` (Qt reads
  `#RRGGBBAA` as `#AARRGGBB`, so 8-digit colours are not allowed).
* Must render at 16, 24 and 32 px with at least 4 % of pixels at alpha >= 128 and at most
  80 % covered (line icons stay open).

Colours are the `icon.*` tokens of `resources/theme/tokens.json` (via `resources/theme/icon-palette.json`,
written by `tools/theme/gen_tokens.py`); the generator never holds hex values of its own. Every mark
colour must reach 3:1 against the header, panel and raised surfaces (ctest `theme_tokens` and
`test_icons`). Paper and body are fills that are always outlined and are exempt.

| Name in the script | Token | Dark | Light | Use |
|---|---|---|---|---|
| INK | `icon.ink` | `#C3C9D2` | `#3C4148` | outlines (default stroke) |
| GREY | `icon.grey` | `#8A93A0` | `#6F7886` | secondary, hidden and reference lines |
| WH | `icon.paper` | `#262B33` | `#FFFFFF` | paper, knock-out fills |
| BODY | `icon.body` | `#5C6470` | `#C9CDD2` | solid body fill |
| SK | `icon.badge.blue` | `#6F9DFF` | `#1E5FC0` | sketch geometry and sketch tools |
| OR | `icon.orange` | `#FF9A3D` | `#B8560A` | solid feature accent (arrows, affected edges/faces) |
| AS | `icon.badge.green` | `#4CC27A` | `#1E7F45` | components, joints, assembly tools, OK |
| CO | `icon.badge.amber` | `#D9A441` | `#8A5A00` | planes, axes, points, construction lines, warnings |
| IN | `icon.badge.violet` | `#B49CFF` | `#7A3FB8` | measure, interference |
| UI | `icon.badge.cyan` | `#3CC8F0` | `#0A6F91` | navigation, display, selection, file and shell commands |
| DR | `icon.badge.teal` | `#3CC8C0` | `#00737C` | drawing workspace tools |
| RED | `icon.badge.red` | `#FF7A8F` | `#B0293A` | error, delete, trim, cut |

## Runtime

Code refers to the logical path `:/icons/<name>.svg` (`ToolInfo::icon` is unchanged). `Icons.cpp`
returns icons backed by a `ThemedSvgIconEngine` that resolves `:/icons/<dark|light>/<name>.svg` from
`theme::current()` at paint time, so a theme switch needs no `setIcon` calls. `fabricad.svg` (the
logo) is theme independent and stays at the `:/icons` root.

## Naming and lookup (`src/app/Icons.cpp`)

* Tools: `<tool id with '.' replaced by '-'>.svg` (`part.extrude` -> `part-extrude.svg`,
  `sketch.constraint.fix` -> `sketch-constraint-fix.svg`). Fallbacks: `ToolInfo::icon`, then the id
  suffix (`app.undo` -> `undo.svg`). Every registered tool must resolve to its own artwork (no two
  tools share identical bytes).
* Feature types (browser / timeline): `Feature::iconName()` first (`sketch`, `component`,
  `joint-<motion>`), then `feature-<type lowercase>.svg` (`feature-offsetplane.svg`).
* Shell icons (browser, timeline, nav bar, app bar, dialogs) use plain names (`eye`, `folder`,
  `timeline-first`, ...); `kUiIcons` in the test lists them.
* A tool without artwork (a plug-in) still gets the generated initials badge; the test fails for
  any tool or feature type registered by FabriCAD's own modules that would fall back to it.

## Contact sheets

`test_icons` renders all tool, feature and shell icons (32 px and 16 px) for each theme into
`icons-dark.png` and `icons-light.png` (next to the test binary, or `$FABRICAD_ICON_SHEET_DIR`) and writes
`icons-inventory.txt` (sorted list of every tool id / feature type and the file it resolves to). Refresh
the committed sheets with

```sh
QT_QPA_PLATFORM=offscreen FABRICAD_ICON_SHEET_DIR=$PWD/docs/screenshots build/tests/fabricad_app_test_icons
```
