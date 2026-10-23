# Icons: monochrome toolbar set and the application icon (ui2/icons)

Closes the open part of P5-MOCKUP-9/10 (`docs/uiux/redesign/p5-summary.md`): the v6 mockup
(`docs/mockups/custom-ui-extrude-v6-tokens-*.svg`) draws toolbar icons as line icons in `icon.ink`
with at most one accent.

## Monochrome style (default)

`resources/icons/generate_icons.py` has a style switch. `mono` is the default and is what
`resources/icons/dark` and `resources/icons/light` now hold. `colour` restores the earlier category
palette byte for byte (checked: `--style colour` regenerates the files committed on `main`).

```sh
python3 resources/icons/generate_icons.py                  # mono (default)
python3 resources/icons/generate_icons.py --style colour   # old colourful set (or FABRICAD_ICON_STYLE=colour)
```

What `mono` does:

* Outlines, sketch geometry, construction, assembly, drawing, view and app marks are all drawn in
  `icon.ink`. `icon.grey` stays for secondary, hidden and reference lines. Body fills stay
  `icon.body`, and translucent highlights (planes, faces) become ink tints.
* Each icon has at most one accent colour, and only where it carries meaning:
  * `icon.badge.violet` (the v6 accent, `#B49CFF` dark) for the action mark of a solid feature
    (extrude arrow, fillet edge, hole, mirror plane, pattern copies, combine overlap) and the
    profile of the sketch tools. Inspect icons are ink.
  * `icon.badge.red`: delete, cut, trim, cancel, error.
  * `icon.badge.green`: OK checks (`ok`, `app-ok`, `finish-sketch`, `sketch-finish`, `app-check`,
    `app-unsuppress`).
  * `icon.badge.amber`: the warning triangle.
  * Live gold (`live.base`) is never used in icons.
* Some icons only differed by hue. They get a small shape or accent change so every tool still has
  its own artwork: `sketch-select`, `sketch-dimension`, `sketch-dimension-edit`, `app-ok`, `display`
  and `workspace-design`. A script check found no new identical pairs and no icon with more than one
  accent colour in either theme.
* File names, the 24 px header, the 1.5 stroke and the palette rules of
  `resources/icons/README.md` are unchanged. Every colour is still an `icon.*` token.

Not run here: `ctest` (`test_icons`) and the committed `docs/screenshots/icons-*.png`. The
machine was busy with builds. Run the test on the next build.

## Contact sheets (before = colour, after = mono; 16 / 24 / 32 px at 1x)

| Sheet | Content |
|---|---|
| `toolbar-dark.png`, `toolbar-light.png` | main toolbar and tool icons, 1x |
| `toolbar-dark-3x.png`, `toolbar-light-3x.png` | the same, 3x nearest neighbour |
| `all-dark.png`, `all-light.png` | all 323 icons, 1x |

Regenerate them with `python3 docs/uiux/ui2/icons/make_contact_sheets.py`. It needs rsvg-convert and
Pillow, evaluates the generator in memory for both styles and writes nothing under `resources/`.

## Application icon

`resources/icons/app/fabricad-app.svg` is the chosen app icon (dark square, ISO axes with filled and
hollow boxes, solid gold circle), copied unchanged. It is in the qrc as
`:/icons/app/fabricad-app.svg`, is the window icon (`src/app/main.cpp`) and is the README logo.
`resources/icons/fabricad.svg` stays, because the icon tests reference it as the `:/icons` root logo.

`resources/icons/app/render_app_icons.sh [OUT]` writes `OUT/hicolor/{16,22,24,32,48,64,128,256,512}x…/apps/fabricad.png`
and `OUT/hicolor/scalable/apps/fabricad.svg` (rsvg-convert, with an ImageMagick fallback). It is not
wired into CMake. The packaging branch adds the install rules.
