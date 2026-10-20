# Round 2: icon set (branch `feat/icons`)

The two-letter badges are replaced by a generated, consistent SVG line-icon set:
one icon for every registered tool id (166 today), every feature type (29, plus the 7 joint
motion types) and every shell icon (browser, timeline, nav bar, app bar, dialogs): 233 SVGs.
Style rules, palette and naming are in `resources/icons/README.md`; contact sheet in
`docs/screenshots/icons.png`.

## Files

| File | Change |
|---|---|
| `resources/icons/*.svg` | all regenerated (old ones restyled to the 1.5 px / palette rules), ~180 new |
| `resources/icons/generate_icons.py` | new: the single source of the artwork; rewrites SVGs + qrc icon list |
| `resources/icons/README.md` | new: style rules, palette, naming, how to regenerate / refresh the sheet |
| `resources/fabricad.qrc` | **shared**: the `icons/` block is regenerated (sorted); `theme-light.qss` kept |
| `src/app/Icons.h/.cpp` | **shared (ui)**: new `resourceIconPath`, `toolIconPath`, `featureIconPath`; lookup order changed (below) |
| `src/app/tests/test_icons.cpp` | new test (picked up by the existing glob, no CMake edit) |
| `docs/screenshots/icons.png` | new contact sheet |

No CMake, MainWindow, registration or module files were touched.

## Behaviour changes

* `toolIcon()` now tries `:/icons/<id with '.' -> '-'>.svg` **before** `ToolInfo::icon`, then the
  id suffix, then the badge. Modules no longer need to set `ToolInfo::icon`; existing values are
  harmless (they match the id-based names or are only a fallback).
* `featureIcon()` tries `Feature::iconName()`, then `feature-<type lowercase>.svg`, then the old
  folder fallbacks. This also fixes `PlaneThreePoints` showing the point icon (old substring
  match on "point").
* Badge fallback is unchanged for unknown/plug-in tools.

## Test shape (`fabricad_app_test_icons`)

Fixed inputs: the registry/factory exactly as `MainWindow` builds them (sketch, part, assembly,
drawing, app tools), joint types `0..kJointTypeCount-1`, the fixed `kUiIcons` list, literal
`resourceIcon("...")` names in `src/app/*.cpp`, and every SVG in `resources/icons` and `:/icons`.
Everything is sorted before checking; each check reports the complete sorted list of offenders.
Checks: every tool / feature type resolves to real artwork; no two tools share identical artwork;
qrc == source directory; SVG lint (viewBox, 1.5 stroke, palette, allowed elements); rendering at
16/24/32 px within ink/coverage thresholds and pixel-identical on a second render; contact sheet
rendered twice must be identical. Outputs (`build/tests/icons-contact-sheet.png`,
`build/tests/icons-inventory.txt`) are byte-identical across runs on the same machine
(verified: same md5 across two processes). No QSettings, documents or timing involved.

## Expect / follow-ups for the integrator

* **Any new tool id added by another round-2 branch makes `test_icons` fail** with
  "tools without an icon: <id> (expected resources/icons/<file>.svg)". Fix: add a drawing for
  that name in `generate_icons.py`, run it, re-run the test, and refresh the sheet (command in
  `resources/icons/README.md`). Same for new feature types (`feature-<type>.svg`) and for new
  `resourceIcon("name")` literals in `src/app`.
* A new tool that reuses an existing icon (e.g. setting `ToolInfo::icon = "select"`) fails the
  distinctness check; give it its own drawing.
* `resources/fabricad.qrc` conflicts: if another branch adds files, keep their non-icon lines and
  re-run `generate_icons.py` (it preserves non-`icons/` entries and rewrites the icon block). A
  hand-added SVG that the generator does not know is **deleted** by the script — add new art to
  the script (or to its `KEEP` set) instead.
* `Icons.cpp`: if another branch edits the same functions, keep the new lookup order above.
* `resources/README.md` (ui-owned) still says "reference icons from ToolInfo::icon"; it could
  point to `resources/icons/README.md` instead (left untouched to avoid conflicts).
* Possible follow-ups: sketch browser icon states (under-defined / fully constrained / error per
  ux.md), suppressed-feature greyed variants (`QIcon::Disabled` already greys via Qt), a dark
  theme palette (the generator makes a second set easy).
