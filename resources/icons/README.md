# FabriCAD icon set

Own artwork, LGPL-2.1-or-later. Fusion-like flat line icons. Every SVG here except
`fabricad.svg` (the application logo) is **generated** by `generate_icons.py`; edit the
drawings there, not the SVGs, then run

```sh
python3 resources/icons/generate_icons.py   # rewrites the SVGs and the icons/ block of resources/fabricad.qrc
```

The script is deterministic (same script, same bytes) and removes SVGs it no longer generates.

## Style rules (enforced by `src/app/tests/test_icons.cpp`)

* Root element exactly: `width="24" height="24" viewBox="0 0 24 24" fill="none"
  stroke="#3C4148" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"`.
* One stroke width: 1.5. Dots and solid marks are filled shapes with `stroke="none"`.
* Elements: `g path circle ellipse rect line polyline polygon` only. No `<text>` (fonts differ
  between machines and are unreadable at 16 px), no gradients, images, `style`/`class`, `url()`.
* Colours `#RRGGBB` from the palette below only; translucency via `fill-opacity` (Qt reads
  `#RRGGBBAA` as `#AARRGGBB`, so 8-digit colours are not allowed).
* Must render at 16, 24 and 32 px with at least 4 % of pixels at alpha >= 128 and at most
  80 % covered (line icons stay open).

| Token | Colour | Use |
|---|---|---|
| ink | `#3C4148` | outlines (default stroke) |
| grey | `#9AA0A8` | secondary, hidden and reference lines |
| white | `#FFFFFF` | paper, knock-out fills |
| body | `#C9CDD2` | solid body fill |
| sketch | `#1F74D1` | sketch geometry and sketch tools |
| solid | `#E8811A` | solid feature accent (arrows, affected edges/faces) |
| assembly | `#2E9E4F` | components, joints, assembly tools, OK |
| construction | `#E5A100` | planes, axes, points, construction lines, warnings |
| inspect | `#8E44AD` | measure, interference |
| view / app | `#0696D7` | navigation, display, selection, file and shell commands |
| drawing | `#00838F` | drawing workspace tools |
| red | `#D93A3A` | error, delete, trim, cut |

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

## Contact sheet

`test_icons` renders all tool, feature and shell icons (32 px and 16 px) into
`build/tests/icons-contact-sheet.png` and writes `build/tests/icons-inventory.txt` (sorted list
of every tool id / feature type and the file it resolves to). Refresh the committed sheet with

```sh
QT_QPA_PLATFORM=offscreen FABRICAD_ICON_SHEET=$PWD/docs/screenshots/icons.png build/tests/fabricad_app_test_icons
```
