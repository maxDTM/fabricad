# Plan: FabriCAD UI redesign ("v5/v6" dark + light, bold mono caps)

Status: planned, not started. No code yet. Open questions answered: see section 12 (decisions confirmed 2026-10-22).
Execution order with the other plans: `docs/plans/ROADMAP.md` (this plan is step 1). Owner: ui agent (`src/app`, `src/viewer`, `resources`), with requests to
the core, sketch, part and drawing owners through `docs/integration-notes/*.md` (see section 9).

Reference: `docs/mockups/custom-ui-extrude-v5.png/.svg` (1920 × 1080) and `custom-ui-extrude-v6.png/.svg` (dark
only, added 2026-10-22; it has no `data-region` ids, no light variant and no MORE ▾ section, and it uses Noto Sans
Mono rather than the bundled JetBrains Mono). The token-generated targets are `custom-ui-extrude-v6-tokens-dark.png/.svg` and `-light` (phase 0.4). The user's v6 decisions override v5 where they differ: all UI text
bold monospace ALL CAPS, straight toolbar dividers and straight section-caption dropdowns. Regenerating v6 from the
token file in both themes, with `data-region` ids, is task 0.4.

Related plans this one includes or must keep working:
- `docs/plans/feature-list-timeline.md`: the FEATURES panel (groups, suppress, rollback bar). Its data and command
  work is unchanged. This plan restyles its widget, sets its default and moves its setting to the Layout page.
- `docs/plans/tutorial.md`: the overlay's anchors (`widget`, `tool:`, `group:`, `tab:`, `prop:`, `timeline:`,
  `browser:`, model anchors) and its accent ring must resolve in every layout combination (section 8.2). That plan
  was reconciled with this one on 2026-10-22 (anchor mapping, text, presets, harness).

---

## 1. Goals and non-goals

| | |
|---|---|
| **Goals** | (1) Mockup-faithful dark theme and an equal light theme, both generated from one token file. (2) Seven live layout toggles, persisted in QSettings. (3) No hard-coded colours left in `src/` outside an allow-list. (4) Existing tests, objectNames and tutorial anchors keep working. (5) WCAG AA for every text and UI-component pair, checked in ctest. |
| **Non-goals** | New modelling features. Translation or i18n. Other usability changes the user did not ask for. A QML rewrite: the UI stays Qt Widgets plus QPainter overlays. |

---

## 2. Design-token architecture

### 2.1 Single source

`resources/theme/tokens.json` is the only place a colour, size or font is defined. Its sections:

| Section | Content | Example |
|---|---|---|
| `color` | `name: {dark, light}` hex (RGBA allowed: `#RRGGBBAA`) | `"surface.panel": {"dark": "#171A20", "light": "#F3F5F8"}` |
| `alias` | Semantic name pointing at a colour token (QSS and code use aliases) | `"button.primary.bg": "accent.solid"` |
| `size` | Spacing scale, radii, control heights, stroke widths | `"radius.control": 1`, `"space": [2,4,6,8,12,16,24]` |
| `font` | Families, pixel sizes, weights, letter-spacing per text style | `"caption": {"px": 9, "weight": 700, "tracking": 1.0}` |
| `pairs` | (fg, bg, kind) pairs that must pass contrast; kind is `text` (4.5), `ui` (3.0), `exempt` | `["text.secondary","surface.header","text"]` |
| `cvd` | Colour pairs that must stay apart under simulation (ΔE00 floor per simulation) | `["live.base","status.warning",10,10,10,6]` |

### 2.2 Generator and outputs (committed, checked by ctest)

`tools/theme/gen_tokens.py` (stdlib-only Python, like `generate_icons.py`) reads `tokens.json` and writes:

1. `src/theme/ThemeTokens.inc`: `enum class Tok : uint16_t {...}` plus two `constexpr` RGBA tables (dark, light),
   size and font tables. It is included by the new static library **`fabricad_theme`** (`src/theme/`), which depends
   on QtGui only, so both `fabricad_viewer` and `fabricad` can link it.
2. `resources/theme/app.qss.tmpl`: hand-written QSS that uses only `@{alias}` placeholders. It is not generated,
   but the generator validates it: every placeholder must exist, and there must be no raw `#hex` or `rgb(`.
3. `docs/design/palette.md`: swatches, the contrast table and the CVD table, regenerated for review.
4. `icons` palette dicts for `generate_icons.py` (section 6.1).

ctest `theme_tokens` (offscreen) does the following:
- regenerates the outputs into a temp dir and diffs them with the committed files;
- computes the WCAG contrast for every `pairs` row and the ΔE00 (Machado 2009 protan/deutan/tritan) for every
  `cvd` row, and fails below the threshold;
- loads both themes and checks that the QSS has no unresolved placeholders.

When Python is missing, the test skips the sync check only. The math checks also run in C++.

### 2.3 Runtime

| Class (new, `src/theme` or `src/app`) | Job |
|---|---|
| `theme::Tokens` | `QColor color(Tok, Theme)`, `std::array<float,4> rgba(Tok, Theme)`, sizes, `QFont font(TextStyle)` |
| `app::ThemeManager` (replaces `Theme.{h,cpp}`) | Holds the current theme (`Dark`, `Light`, `System`). `apply()` sets the Fusion style, the full `QPalette` from tokens, the fonts per class (section 3) and the rendered QSS. Emits `themeChanged(Theme)`. Follows `QStyleHints::colorSchemeChanged` in System mode. Re-applies once after the event loop starts (the qt6ct workaround in today's `Theme.cpp`). |
| `viewer::ViewerPalette` | A plain struct of every viewport colour (section 4.5), filled from tokens by the app. `ViewerWidget::setPalette(const ViewerPalette&)` re-styles OCCT: the background gradient, grid, highlight drawers, ViewCube, default face boundaries, and redisplay. |
| `core::ColorRole` (requested from core) | An enum on `PreviewStyle` (`role` field: `Live`, `Join`, `Cut`, `NewBody`, `SketchFree`, `Inference`, `Dimension`, ...). The viewer resolves the role through `ViewerPalette`. The RGBA field stays as a fallback for headless tests. |

Custom-painted widgets (timeline, marking menu, dial, value box, card frame) read `Tokens` at paint time and
`update()` on `themeChanged`. Nothing caches a `QColor` across a theme switch.

### 2.4 Audit of hard-coded colours (step 0.1)

Run `tools/theme/audit_colors.py` (it becomes a ctest in lint mode with an allow-list). It searches for these
patterns:
- `#[0-9A-Fa-f]{3,8}\b`, `QColor\(`, `Qt::(white|black|red|green|blue|gray|darkGray|lightGray|yellow)`;
- `Quantity_NOC_`, `Quantity_Color\(`, `rgba?\(`, `setStyleSheet\(`;
- float RGBA literals such as `\{0\.\d+f, 0\.\d+f, 0\.\d+f`.

The search covers `src/**` (except `third_party`), `resources/**/*.qss` and `.svg` outside `icons/`.

Today's count of colour literals in non-test files (grep, 2026-10-22):

| File | Hits | Owner | Action |
|---|---|---|---|
| `src/viewer/ViewerWidget.cpp` | 45 | ui | → `ViewerPalette` (bg, grid, highlight, cube, labels, rubber band, cube controls) |
| `src/app/MarkingMenu.cpp` | 11 | ui | → tokens |
| `src/app/TimelineWidget.cpp` | 10 | ui | → tokens (status, marker, drop line) |
| `src/sketch/SketchSession.h` (`colors::`) | 8 | sketch | → `ColorRole` (request) |
| `src/app/Theme.cpp` | 8 | ui | replaced by `ThemeManager` |
| `src/viewer/ManipulatorPrs.cpp` | 7 | ui | → `ViewerPalette.manip*` |
| `src/sketch/ValueInputOverlay.cpp` | 7 | sketch | QSS objectName `#fcValueBox` + tokens (request) |
| `src/viewer/occt/OcctQtTools.*` | 9 | ui | helpers only (allow-list) |
| `src/part/tools/PropertyPanel.cpp` | 4 | part | → QSS properties (request) |
| `src/drawing/DrawingCanvas.cpp`, `SheetRenderer.*`, `DrawingExport.cpp` | 9 | drawing | paper and ink stay fixed (allow-list); canvas surround and selection → tokens (request) |
| `src/app/{MainWindow,Icons,BrowserTree,ParametersDialog,CommandSearch,Ribbon,NavBar,DocumentTab,AppTools}.cpp` | 21 | ui | → tokens and QSS |
| `src/viewer/PrimitivePrs.*`, `occt/OcctView.cpp` | 5 | ui | → `ViewerPalette` |
| `src/assembly/StepExport.cpp`, `planegcs/GCS.cpp` | 3 | — | not UI colours (allow-list) |
| `src/core/ViewerContext.h` `PreviewStyle::join/cut/newBody/sketchLine` | 4 presets | core | → `ColorRole` (request) |
| `src/app/Icons.cpp` badge palette | 8 | ui | → `icon.badge.*` tokens |
| `resources/theme-light.qss` | ~60 literals | ui | deleted. Replaced by `app.qss.tmpl` |
| 10 files calling `setStyleSheet` | — | mixed | only objectName or dynamic-property hooks; no inline colours |

The theme work counts as done when the allow-list holds only the drawing paper and ink, export colours,
`OcctQtTools` helpers and third-party code.

---

## 3. Typography

### 3.1 Font

| Item | Decision |
|---|---|
| Family | **JetBrains Mono** (SIL OFL 1.1), static TTFs: Medium 500, Bold 700, ExtraBold 800 (about 270 KB each). The fallback chain matches the mockup: Noto Sans Mono, DejaVu Sans Mono, `monospace`. Alternatives: IBM Plex Mono or Noto Sans Mono (open question 4). |
| Bundling | `resources/fonts/*.ttf` + `OFL.txt` in `fabricad.qrc`. `QFontDatabase::addApplicationFont` runs in `main()` before the window exists (≈ 3 ms). Append a row to `THIRD_PARTY.md`, which is the allowed append. |
| ALL CAPS | Set by **`QFont::setCapitalization(QFont::AllUppercase)`** on the UI fonts, never by changing strings. Tool names, objectNames, test strings, tutorial bubble matching and scripting output stay mixed case. |
| Exceptions (no caps) | `QLineEdit`, `QAbstractSpinBox`, `QPlainTextEdit`/`QTextEdit`, `#CommandLine`, `#CommandHistory`, the value boxes (`#fcValueBox`), expression labels (`fx boss_height = 25 mm`), units text and status-bar value cells. They use the `value` style below. Implement them with a per-class font (`QApplication::setFont(f, "QLineEdit")`) plus a `textCase="as-is"` dynamic property for labels that show values. |
| Applied per class | As `Theme.cpp` already does for the platform-theme override, extended to the caps and value fonts. |

### 3.2 Text styles (logical px; Qt scales by the device pixel ratio)

| Style | Size | Weight | Tracking | Case | Used for |
|---|---|---|---|---|---|
| `caption` | 9 | 700 | +1.0 px | CAPS | toolbar section captions, step labels `01 PROFILE`, table keys (`TYPE`, `AREA`) |
| `label.sm` | 10.5 | 700 | +0.3 | CAPS | toolbar button labels, segmented labels, status cells |
| `body` | 12 | 700 | 0 | CAPS | tree rows, form labels, menus, buttons, tabs |
| `header` | 12 | 800 | +1.5 | CAPS | panel headers `FEATURES 13`, `MODEL`, `SELECTION` (one size above row text for clear hierarchy) |
| `title` | 13 | 800 | +0.6 | CAPS | card title, dialog titles |
| `value` | 12.5 | 500 | 0 | as typed | numbers and units, expressions, command line, value boxes, user-entered names (open question 3) |
| `keycap` | 9.5 | 700 | +0.5 | CAPS | `Ctrl K`, `Esc`, `Enter`, `E` chips |

### 3.3 Number formatting

- Use the document precision (`ParametersDialog`/`Units`). Trailing zeros are stripped in trees and kept in the
  readouts (`X 62.000`).
- A thin space (U+2009) groups thousands, as in `1 256.64 mm²`. The decimal point is always `.`.
- There is a space before the unit (`25 mm`) and none before `°`. Units are lowercase. `ø` marks diameters and `×`
  separates pairs.
- AZ/EL readouts are zero-padded to 3 digits: `AZ 045°  EL 35°`.
- Numeric columns are right-aligned. Monospace keeps them tabular.

### 3.4 Width budget and layout impact

Bold mono caps take about 0.60 em per glyph: 7.2 px at 12 px and 6.3 px at 10.5 px. That is about 25 % wider than
today's 9 pt sans.

| Place | Rule |
|---|---|
| Toolbar button | Width = max(48, label chars × 6.3 + 10). A shortened label comes from an app-side `shortLabel` map in `ToolbarModel` (`New comp` for New Component). `ToolInfo::name` is unchanged. |
| Toolbar overflow | When the row is too narrow, the right-most promoted tools fold into the section caption dropdown, then whole sections collapse to caption-only. There is no horizontal scroll, and no label is ever cut. |
| Sidebar (default 320 px, minimum 260) | Name column elides in the middle (`Extrude…3`), and the full name goes in the tooltip. The value column (≤ 10 chars) is never elided. |
| Card (360 px) | Form label column 96 px (≤ 12 chars at 12 px). A longer label wraps to 2 lines. |
| Status bar | The prompt elides on the right. Fixed cells use `minimumWidth` from their longest value string. |
| Test | `test_layout_text`, offscreen at 1280 × 800 and 1920 × 1080 in both toolbar orientations. It asserts that no `QToolButton`/`QLabel` in the toolbar or sidebar headers has `fontMetrics().horizontalAdvance(text) > contentsRect().width()`. |

---

## 4. Colour palette (both themes)

All ratios were computed with the WCAG 2.x formula. ΔE00 was computed on Machado 2009 (severity 1.0)
simulations. The script is reproduced by `tools/theme/gen_tokens.py`. **T** = text (needs ≥ 4.5), **U** = UI
component or graphic (needs ≥ 3.0), **X** = exempt (disabled, decorative).

### 4.1 Surfaces, borders, text

| Token | Dark | Light | Use |
|---|---|---|---|
| `bg.app` | `#101217` | `#E9ECF1` | title bar, status bar, window background |
| `surface.panel` | `#171A20` | `#F3F5F8` | toolbar, sidebar, panel bodies |
| `surface.header` | `#1D2128` | `#E9ECF1` | panel headers, card header |
| `surface.raised` | `#1D2128` | `#FFFFFF` | floating card, popovers, menus (elevation 1) |
| `surface.overlay` | `#252A32` | `#FFFFFF` | tooltips, dropdown lists, dial plate (elevation 2) |
| `surface.input` | `#101217` | `#FFFFFF` | line edits, combos, value box |
| `surface.hover` | `#252A32` | `#E4E8EE` | row and button hover |
| `surface.pressed` | `#2E343E` | `#D9DDE3` | pressed |
| `border.subtle` | `#2A2F37` | `#E1E4E8` | dividers, row separators (X, decorative) |
| `border.default` | `#343A44` | `#D3D7DD` | panel edges, toolbar section dividers (X) |
| `border.control` | `#6E7682` | `#7E8794` | input, checkbox and segmented outlines (U) |
| `text.primary` | `#F3F5F8` | `#171A20` | |
| `text.secondary` | `#A7AEB9` | `#4A515D` | captions, keys, units |
| `text.tertiary` | `#8E96A2` | `#5E6675` | counts, hints |
| `text.disabled` | `#6E7682` | `#8E96A2` | (X) |
| `shadow.card` | `#000000` 55 %, 0/8/24 | `#1A1D22` 18 %, 0/6/18 | card drop shadow |

Elevation comes from surface steps plus a 1 px `border.default` outline and a shadow on floating items only.

### 4.2 Purple (selection and UI state) and gold (live operation)

| Token | Dark | Light | Use |
|---|---|---|---|
| `accent.solid` | `#7C4DEB` | `#6D3FE0` | primary button fill, checked toggle, active toolbar tool |
| `accent.hover` | `#6F42E0` | `#5B2FCC` | |
| `accent.pressed` | `#6236CF` | `#4A22B0` | |
| `accent.onSolid` | `#FFFFFF` | `#FFFFFF` | text on solid |
| `accent.text` | `#B49CFF` | `#6D3FE0` | links, active tab text, selected row icon |
| `accent.indicator` | `#8B5CF6` | `#6D3FE0` | 3 px row bar, underline of the active filter, step bar, brackets |
| `accent.focus` | `#B49CFF` | `#6D3FE0` | 2 px focus ring, 1 px offset |
| `accent.selFill` | `#2D2547` | `#ECE6FF` | selected row, selected chip background |
| `live.base` | `#F5B700` | `#F5B700` | drag handle, preview edges (dark), value-box frame, active-command square. **Light exception:** gold is only 1.8:1 on white, so every 1 px `live.base` border or hairline in this plan (5.3 chip, value box, 5.5 live field, 7.1 card hairline, step bar) is drawn 2 px in light, and gold fills always carry `live.ink` text or an ink edge (v6-tokens mockup). |
| `live.bright` | `#FFD24D` | `#FFD24D` | hover and drag of a live handle, edited digits |
| `live.ink` | `#1A1400` | `#1A1400` | text and outline on gold, the two-tone stroke's dark half |
| `live.text` | `#FFD24D` | `#6B4A00` | gold text (`EDITING Extrude 3`: the caption is caps, the feature name keeps its case per decision 3; the active row) |
| `live.fill` | `#3A3018` | `#FFF3CC` | active feature row, editing breadcrumb chip |
| `live.outline` | `#F5B700` | `#6B4A00` | preview outline on the viewport background |

The mockup's `#8B5CF6` cannot be the button fill: white on it is only **4.23**. The fill darkens to `#7C4DEB` (5.10),
and `#8B5CF6` stays for indicators.

### 4.3 Status colours: error, warning, success, info

The warning colour is the risky one, so status colours never rely on hue alone. Each has a fixed **shape**:

| Token | Dark | Light | Fill (row/badge bg) dark / light | Shape cue |
|---|---|---|---|---|
| `status.error` | `#FF5C8A` (crimson-rose) | `#C4125E` | `#3A1A26` / `#FCE4EC` | filled octagon with `×`. Rows are struck red with a 3 px bar. |
| `status.warning` | `#F2542D` (orange-red) | `#B93815` | `#2B1812` / `#FBE6DF` | triangle with `!` |
| `status.success` | `#4CC27A` | `#1E7F45` | — | check mark |
| `status.info` | `#3CC8F0` | `#0A6F91` | — | circle with `i` |
| Live (gold) | `#F5B700` | `#F5B700` fill + `#1A1400`/`#6B4A00` ink | — | diamond handle, cone arrow, gold square, solid hairline. **Never a status icon.** |

Hue choice: a grid search over warning and error hues maximised the smallest ΔE00 to gold and to each other in
the normal, protan and deutan simulations. It pushed warning to orange-red (H ≈ 12°) and error to crimson-rose
(H ≈ 345°), away from gold (H ≈ 45°).

### 4.4 Contrast results (key pairs; all pass unless marked)

| Pair | Kind | Dark | Light |
|---|---|---|---|
| text.primary / surface.panel | T | 15.96 | 15.96 |
| text.primary / surface.raised | T | 14.79 | 17.43 |
| text.secondary / surface.header | T | 7.23 | 6.75 |
| text.tertiary / surface.header | T | 5.41 | 4.88 |
| text.disabled / surface.panel | X | 3.80 | 2.73 |
| border.control / surface.panel | U | 3.80 | 3.33 |
| border.control / surface.input | U | 4.08 | 3.63 |
| accent.onSolid / accent.solid | T | 5.10 | 6.11 |
| accent.onSolid / accent.hover · pressed | T | 5.95 · 7.12 | 7.71 · 9.78 |
| accent.solid / surface.panel | U | 3.42 | 5.59 |
| accent.text / surface.panel | T | 7.58 | 5.59 |
| accent.text / accent.selFill | T | 6.23 | 5.04 |
| text.primary / accent.selFill | T | 13.10 | 14.38 |
| accent.focus / surface.panel | U | 7.58 | 5.59 |
| live.ink / live.base | T | 10.19 | 10.19 |
| live.text / surface.panel | T | 12.10 | 7.38 |
| live.text / live.fill | T | 9.02 | 7.27 |
| live.outline / vp.bgTop · vp.bgBottom | U | 7.99 · 9.98 | 7.85 · 5.79 |
| status.error / surface.panel · raised | T | 5.93 · 5.50 | 5.35 · 5.84 |
| status.warning / surface.panel · raised | T | 5.05 · 4.68 | 5.28 · 5.77 |
| status.warning / warningFill | T | 4.90 | 4.80 |
| status.error / errorFill | T | 5.27 | 4.85 |
| status.success / surface.panel | T | 7.72 | 4.60 |
| status.info / surface.panel | T | 8.89 | 5.20 |

Fixed during the check (light hidden lines `#7E8794` also failed at 2.61 and became `#5E6675`):
- Light info `#0B7A9F` gave 4.47, so it became `#0A6F91`.
- Dark warning fill `#3A1F17` gave 4.38, so it became `#2B1812`.
- Light preselect `#9B7BF0` gave 2.32, so it became `#8257F0`.

### 4.5 Viewport, sketch and drawing tokens (`ViewerPalette`)

| Token | Dark | Light | Ratio vs viewport bg (top / bottom), dark · light | Notes |
|---|---|---|---|---|
| `vp.bgTop` → `vp.bgBottom` | `#262A31` → `#14171B` | `#FBFCFD` → `#D5DBE3` | — | vertical gradient. Mockup scanline texture is off by default (open question 10). |
| `vp.gridMinor` / `vp.gridMajor` | `#2B2F36` / `#3A404A` | `#E1E5EA` / `#BCC3CC` | X (decorative) | Major every 5. The sketch grid uses the same tokens. |
| `vp.originAxes` | axis colours at 70 % | same | — | |
| `axis.x` / `axis.y` / `axis.z` | `#F0525F` / `#4CC27A` / `#5B8CFF` | `#C42B38` / `#1B7A43` / `#2F5FD9` | dark 4.17–7.97 · light 3.85–5.45 (U) | Always with the letters **X Y Z** (dial, triad, readout ticks). They are never the only cue. |
| `axis.onAxis` | `#101217` | `#FFFFFF` | 5.42–8.30 · 5.37–5.60 (T) on the axis colours | letters on axis squares and tips (dial X/Y/Z). Added after the v6-tokens mockup. |
| `body.default` | `#9BA0A9` | `#BFC3C8` | 6.84/5.48 · 1.72/1.27 (S) | default body material: the colour of the best-lit face (`ViewerPalette` material gain ≈ 1), the mockup's top face. Per-body colours from the document override it. Phase 5 (p4-check M1): was `#B4BAC4` / `#C9CDD2`, lit to `#E3E7EB` in light (1.2:1 on the viewport). **Shape rule**: body vs both gradient ends ≥ 1.25:1 (`S` pairs in `theme_tokens`), so the silhouette reads without its edge lines. |
| `body.edge` | `#1A1D22` | `#1A1A1A` | 7.56 on the face | face boundaries |
| `body.hiddenLine` | `#6E7682` dashed | `#5E6675` dashed | 3.14/3.92 · 5.63/4.15 (U) | HLR and "shaded + hidden" modes |
| `body.ghost` | body at 25 % | body at 25 % | — | ghosted components, rolled-back preview |
| `sel.edge` / `sel.fill` | `#B49CFF` 2.2 px / `#8B5CF6` 22 % | `#6D3FE0` 2.5 px / `#6D3FE0` 18 % | 6.27/7.82 · 5.95/4.38 | |
| `pre.edge` | `#E0D6FF` 1.5 px | `#8257F0` 1.5 px | 10.45/13.04 · 4.47/3.30 | hover (preselection) |
| `halo.dark` / `halo.light` | `#0E0A1F` / — | — / `#FFFFFF` | — | **Halo rule**: any accent line drawn over shaded bodies gets a 1 px halo. Purple and gold on the grey body give only 1.03 and 1.24 without one. With the halo, ink on the face is ≥ 7:1. |
| `preview.live` | fill `#F5B700` 50 %, edges `#FFD24D` 1.8 px + `#1A1400` halo | fill `#F5B700` 55 %, edges `#6B4A00` 1.8 px + `#FFFFFF` halo | ≥ 7.8 | extrude, revolve and the like (role `Live`). Fill raised in phase 5 (p4-check L5: 40 / 45 % read as a pale tan). |
| `preview.cut` | fill `#FF5C8A` 30 %, edges `#FF5C8A` dashed | `#C4125E` | 4.90 · 5.69 | Cut keeps the error hue, with a dashed edge as its cue |
| `preview.newBody` | fill `#3CC8F0` 30 % | `#0A6F91` | | |
| `manip.handle` | `#F5B700` diamond, `#1A1400` 1 px outline | same | | drag handle. `live.bright` on hover and drag, plus a 6 px glow. |
| `manip.axisArrow` | axis colours | axis colours | | triad (Move), shown with letter labels |
| `sk.free` | `#4775FF` | `#1C469C` | 3.61/4.50 · 8.50/6.27 | under-constrained curves. Chosen in phase 0 (decision 8; was `#6FA8FF`/`#1E6FD9`): a darker azure keeps a lightness gap to the lavender selection under protan/deutan simulation. Light raised from `#00248F` after the v6 token mockup (at L\*21 it read as body-edge black). Sketch curves follow the 1px halo rule (dark `#4775FF` is only 1.5:1 on the lit body face). |
| `sk.constrained` | `#E8ECF2` | `#171A20` | 12.15/15.16 · 16.97/12.51 | fully constrained (it inverts the usual "black" rule in dark) |
| `sk.construction` | `#9AA2AE` dashed 6/4 | `#5E6675` dashed 6/4 | 5.59/6.98 · 5.63/4.15 | the dash is the cue (orange is no longer used, so it cannot be confused with gold or warning) |
| `sk.projected` | `#3CC8F0` dash-dot | `#0A6F91` dash-dot | 7.35/9.17 · 5.53/4.08 | as in the mockup |
| `sk.fixed` | `#4CC27A` + lock glyph | `#1E7F45` + lock glyph | 6.38 · 4.89 | |
| `sk.conflict` | `#FF5C8A` 2.5 px + `⚠` glyph | `#C4125E` | 4.90/6.12 · 5.69/4.19 | over-constrained or failed |
| `sk.point` | as curve state, 5 px squares | | | |
| `sk.profileFill` | `#8B5CF6` 14 % (purple when selected), otherwise `#A7AEB9` 14 % | `#6D3FE0` 10 % / `#4A515D` 12 % | X | closed-profile tint (phase 5, p4-check L4: was 8 % / 6 %, nearly invisible in light) |
| `sk.constraintGlyph` | chip `surface.overlay`, glyph `text.secondary`, border `border.default` | chip `#FFFFFF`, glyph `#4A515D` | glyph 6.46 · 7.32 (T) | selected chip: `accent.selFill` + `accent.text`; conflicting chip: `status.errorFill` + `status.error` |
| `dim.line` / `dim.text` | `#3CC8F0` / `#F3F5F8` on chip `#171A20` with a `#3CC8F0` 1 px border | `#0A6F91` / `#171A20` on `#FFFFFF` | 9.17 · 4.08 | driving dimension. A driven (reference) dimension is shown in parentheses in `text.secondary`. |
| `vp.rubberBand` | `#B49CFF` line, `#8B5CF6` 14 % (window), dashed (crossing) | `#6D3FE0` | | |
| `drawing.paper` / `drawing.ink` | `#FFFFFF` / `#000000` | `#FFFFFF` / `#000000` | 21.0 | **Fixed in both themes.** Hidden lines, centre lines and dimensions are black too. |
| `drawing.canvas` | `#0E1014` | `#C9CED6` | — | area around the sheet |
| `drawing.select` | `#6D3FE0` | `#6D3FE0` | 6.11 on paper | the selection always uses the light value because it sits on white paper |

### 4.6 Disabled, suppressed and rolled-back states

| State | Dark | Light | Cue besides colour |
|---|---|---|---|
| Disabled control | text `#6E7682`, border `#2A2F37`, icon at 35 % | `#8E96A2`, `#E1E4E8` | no hover and no focus |
| Suppressed feature or group | text `text.disabled`, struck through, icon at 40 % | same | unchecked box, `off` in the value column |
| Rolled back (below the bar) | text `text.tertiary`, icon at 55 %, no strike | same | it sits below the ROLLBACK bar |
| Hidden (eye off) | eye glyph crossed, row text `text.secondary` | same | |
| Error row | 3 px `status.error` bar + octagon icon | same | message in the tooltip and the status bar |
| Warning row | 3 px `status.warning` bar + triangle | same | |
| Live or edited row | `live.fill` background + 3 px `live.base` bar + `live.text` | same | |
| Selected row | `accent.selFill` + 3 px `accent.indicator` bar | same | |

### 4.7 Colour-vision test and the gold decision

ΔE00 for the pairs that **actually appear together** (normal / protan / deutan / tritan):

| Pair | Dark | Light | Verdict |
|---|---|---|---|
| live (`#F5B700`) vs warning | 35.9 / 24.1 / 14.4 / 21.6 | (fill) 42.8 / 38.8 / 28.4 / 31.5 | pass |
| live vs error | 52.5 / 38.0 / 22.2 / 18.8 | 61.1 / 55.2 / 34.4 / 29.5 | pass |
| warning vs error | 22.8 / 28.4 / 16.2 / **6.8** | 23.7 / 31.1 / 15.3 / **2.4** | tritan relies on shape (octagon vs triangle) |
| light-theme thin ochre stroke `#8A5F00` vs warning | — | 23.6 / 5.8 / **2.0** / 13.2 | **fail**. Rule: gold in the light theme is always the bright `#F5B700` fill or halo with dark ink, never a thin ochre line alone. |
| selection (`#B49CFF`) vs `sk.free` (`#4775FF` dark / `#1C469C` light; was `#6FA8FF`: 16.9 / **0.7** / **2.9** / 21.9) | 19.0 / 12.3 / 16.2 / 22.3 | 15.7 / 10.6 / 11.1 / 13.6 | pass (decision 8, phase 0). Selection still adds a halo, endpoint grips and a profile tint. |
| selection vs preselect | 17.1 / 16.1 / 16.4 / 13.0 | width + halo | pass |

**Decision test (gate G, phase 5)**. Gold stays unless one of these fails:

1. **Numeric**: ctest `theme_tokens` checks the `cvd` rows. ΔE00(live, warning) and ΔE00(live, error) must be
   ≥ 10 for normal, protan and deutan, and ≥ 6 for tritan, in both themes.
2. **Over geometry**: every live element has a measured outline contrast ≥ 3:1 against both the gradient
   endpoints and `body.default`. Screenshot pixels are sampled at the preview edge.
3. **Perception**: 3 Sonnet testers get 24 screenshot crops of the "status zoo" scene (a warning row, an error row,
   a live extrude, a warning toast during a live drag). Each crop is shown in normal, protan, deutan and tritan
   simulation (`tools/uitest/cvd.py`). Testers label each marked element as live, warning or error. Pass:
   ≥ 95 % correct overall and 100 % for live vs warning.

**Fallback** if a check fails: live becomes **teal** (`#2DD4BF` dark, `#00796B` light, ink `#04201C`), and
warning stays orange-red. Dimensions and projected curves then leave cyan for neutral (`#C3C9D2`/`#4A515D`), so
teal is not confused with cyan. Teal vs success green is only 6.4 in tritan, so success keeps its check icon.
After a fallback, re-run gate G.

**Result (phase 5, 2026-10-22, `docs/uiux/redesign/p5-summary.md`).** Numeric: green. Perception: live vs warning
40/40; overall 90.3 % strict on the first pass. All 14 misses were *errors* shown by colour alone (red text,
status cells, the conflict curve), not live-colour confusions, so the teal fallback could not help. The error shape
cues were added (octagon `theme::StatusGlyph` on every error message and cell, conflict curves 2x wide), giving
97.2 % on re-score. Gold stays. A blind tester re-run is still open.

---

## 5. Component inventory

All controls use radius 0 (containers) or 1 px (inputs and chips). Heights are logical px. "Focus" always means a
2 px `accent.focus` ring outside the control, never a colour change only.

### 5.1 Window chrome

| Component | Size / padding | States | Notes |
|---|---|---|---|
| Title bar `#AppBar` | 36 h, `bg.app`, bottom 1 px `border.subtle` | — | logo + `FABRICAD`; workspace selector; document tabs; search; undo, redo, save, find, help |
| Workspace selector `#WorkspaceSwitcher` | 28 h, 8 px padding, `surface.header`, 1 px `border.default`, keycap `ALT 1`, ▾ | hover border `accent.text`; open: `accent.selFill` | Menu: PART, ASSEMBLY, DRAWING (open question 2) |
| Document tab `#DocumentTabs` | 36 h, min 120 w, angled sides (12° slant, 8 px run), 12 px text padding | inactive `text.secondary` on `bg.app`; hover `surface.hover`; active `surface.panel` + 2 px top `accent.indicator`; modified dot | Custom `QTabBar` paint (section 7.1). The `+` tab is `#NewTabButton`. |
| Search box | 26 h, 260 w, `>_` glyph, keycap `CTRL K` | focus ring | opens the existing `#SearchPopup` |
| Main toolbar `#RibbonRow` (horizontal) | 70 h: button row 52 + caption row 16; sections divided by a straight 1 px `border.default`, 8 px gaps | — | sections SKETCH, CREATE, MODIFY, ASSEMBLE, CONSTRUCT, INSPECT on the left; DOCUMENT right-aligned |
| Tool button `#RibbonTool` | 48–60 w × 50 h: 24 px icon, 4 px gap, `label.sm`; split arrow ▾ 8 px when it has variants | hover `surface.hover`; pressed `surface.pressed`; active (running tool) `accent.selFill` + 1 px `accent.indicator` border; disabled 35 % | gets the dynamic property `toolId` (new: today's `Ribbon.cpp` sets no dynamic properties) |
| Section caption `#RibbonGroupLabel` | 16 h, `caption`, ▾, straight; full width of the section | hover `accent.text`; open `accent.selFill` | the dropdown lists every tool in the group (as today) |
| Vertical toolbar | 64 w; each section = caption bar (16 h, straight rule above) + one column of 40 × 40 icon buttons, label below at 9 px if it fits, else in the tooltip | as above | for the left and right positions (section 6.2) |
| Status bar | 28 h, `bg.app`, cells divided by 1 px `border.subtle` | — | 3 px left bar + gold square + command name; prompt (`text.primary`); key hints (`text.tertiary`); command line cell; OCCT version; units; grid; snap; rebuild meter (8 bars + ms); selection count |
| Command line `#CommandLine` | 22 h inside the bar, 360–420 w, `surface.input`, `>` prompt, `value` font, `history ↑` hint | focus ring | as-typed case |

### 5.2 Sidebar

| Component | Size / padding | States | Notes |
|---|---|---|---|
| Panel header | 36 h, full-width band in `surface.header` (one step lighter/darker than the panel rows) with a 1 px `border.strong` line above and below, a 3 px left `accent.indicator` tick, a section glyph, `header` style one size up from row text (12 px, weight 800, +1.5 tracking) in `text.primary`, count chip, and 6 px of space before the first row. Rows below are indented and use `text.secondary` for metadata, so the header always reads as a separate level (user feedback 2026-10-22: FEATURES must stand out more from its items). | collapsible (click); chevron at the right | FEATURES has search and filter icons. MODEL has a master eye. |
| Feature row | 28 h, indent 18 px per level, icon 16, name, right value column (`value`, `text.tertiary`) | hover, selected, live, error, warning, suppressed, rolled back (4.6) | `FeatureListWidget` (feature-list plan) |
| Group row | 28 h, chevron, folder icon, count, tri-state checkbox 14 × 14 at the right | collapsed or expanded; tri-state | |
| Suppress checkbox | 14 × 14, 1 px `border.control`, checked = `accent.solid` + white check | hover border `accent.text`; focus ring | |
| Rollback bar | 4 h `text.primary` line + `ROLLBACK` tag (caption, `surface.overlay`, 1 px border), drag area ± 6 px, start knob square 8 × 8 | hover and drag: `live.base` line, gold tag | its keyboard focus moves with ↑/↓ |
| Insert-at-rollback button | 28 h, dashed 1 px `border.control`, `+ INSERT FEATURE AT ROLLBACK` | hover solid border | opens the command search scoped to the rollback position |
| Model row | 28 h, eye 16 px at the right edge, count, badges (`ACTIVE`: 1 px `accent.text` border, caption) | hover, selected, hidden | `BrowserTree` (#Browser) restyled |
| Selection panel | header + key/value grid, 26 h rows, key column 92 px `caption` `text.tertiary`, value `value` | empty state `NOTHING SELECTED` | new `SelectionPanel` from `SelectionModel` + `Selectors` |
| Inline tip | `surface.overlay`, 1 px `border.default`, 8 × 6 padding, title `body`, text `text.secondary` | — | e.g. "GROUP SUPPRESSED / Tick to include its 2 features again" |
| Splitters | 1 px `border.subtle`, 6 px grab area | hover `accent.indicator` | sizes are persisted |

### 5.3 Viewport overlays

| Component | Size | States | Notes |
|---|---|---|---|
| Breadcrumb bar | 28 h, `surface.panel` 92 %, 1 px border; crumbs separated by `>`; edit chip `live.fill` + 1 px `live.base` + gold square + `EDITING Extrude 3` (light: 2 px border, see `live.base`) | crumbs hover `accent.text` | top-left at 16 px |
| Selection-filter bar | 28 h; filter icon + segments `BODIES FACES EDGES SKETCH PROFILES` (auto-hides segments that do not apply) | active: `accent.selFill` + 2 px `accent.indicator` underline | drives `ViewerWidget::setSelectionPriority`/`SelectionModel` filter. Tooltip shows the shortcut. |
| Command card `#CommandDialog` | 360 w, header 40, step bar 22, rows 34–52, footer 52; padding 16 | active: corner brackets + gold hairline (7.1) | 5.4 |
| Value box `#fcValueBox` | tag 16 h (`DIST`, `live.base` fill, `live.ink` caption) above a 30 h × ≥118 w box: `surface.input`, 1.5 px `live.base` border, value `value` 12.5, unit `text.secondary`, spinner ▴▾ 14 w | editing: digits on a `live.base` 30 % fill; invalid: border `status.error` + octagon | anchored to the handle with a 1 px gold leader line |
| Drag handle | arrow 18 × 20 `live.base` + `live.ink` outline; base diamond 14 × 14 | hover `live.bright` + glow; drag: glow + cursor hidden | `ManipulatorPrs` |
| Dimension chip | 18 h, `dim.*` | selected purple | e.g. `ø 40` |
| Orientation dial | 140 × 140 hexagon + 22 h AZ/EL readout + 26 h view-button row (`ISO TOP FRONT RIGHT`, segmented) | axis tips: hover = brighter + ring; the current view button is `accent.selFill` | top-right at 16 px; 7.3 |
| View-tool column | 36 w, buttons 32 × 32 with 4 gaps: fit, orbit, pan, zoom, display style, visual style | toggled tool: `accent.selFill` + 2 px `accent.indicator` left bar | right edge; replaces `NavBar` (#NavBar is kept as its objectName) |
| Coordinate readout | 26 h, 3 cells (`X 62.000`, `Y …`, `Z …`) each with a 3 × 10 axis-colour tick, unit cell | — | bottom-left |
| Scale bar | 140 w ruler, ticks every 10 %, label `50 mm` | — | bottom-right; picks 1/2/5 × 10ⁿ |
| Floating quick toolbar | 40 w vertical strip (or 40 h horizontal), 32 × 32 buttons, default contents: repeat last, the 6 most-used tools, Finish Sketch while sketching | as toolbar buttons | toggle 2 |
| Sketch palette `SketchPalette` | restyled card (same frame and brackets without gold), 260 w | — | it obeys the card placement collision rules |
| Toast | 32 h, `surface.overlay`, status icon + text, 4 s | — | for `showMessage` levels |

### 5.4 Command card body controls

**Open (user decision, from the v6-tokens mockup):** in the 360 px card the 96 px label column leaves 58 px cells for the 4-way OPERATION control, and `INTERSECT` in `label.sm` (≈ 59 px) collides with `NEW BODY`. The mockup puts the label above segmented rows (DIRECTION, OPERATION) at full card width. Proposed: segmented rows always use label-above; alternative: change the card width.

| Control | Spec |
|---|---|
| Header | icon 18, title `title`, keycap (`E`), context `PART · BRACKET` `text.tertiary`; pin (keeps the card open for repeats) and close buttons 24 × 24 |
| Step indicator | 3–4 segments, 2 px bar + `caption` label `01 PROFILE`. Done = `accent.indicator`; current = `live.base` bar + `live.text` label; to do = `border.default`. Clicking a done step focuses that input. |
| Selection chip (Ref input) | 28 h, `accent.selFill`, 1 px `accent.indicator`, square glyph + `1 PROFILE` + `×` clear. Its sub-label (`Sketch 3`) is in `text.tertiary`. The active input gets a 2 px left `accent.indicator` bar. |
| Segmented control | equal cells 52 h (icon 18 + label) or 28 h (label only); 1 px `border.control` between cells. Checked = `accent.selFill` + 1 px `accent.indicator` + `text.primary`. Arrow keys move inside it. Used for enums with ≤ 4 values; more values use a combo. |
| Combo `QComboBox` | 28 h, `surface.input`, 1 px `border.control`, ▾ in a 24 px cell |
| Value field | 28 h, value in `value`, unit suffix, trailing action (⇄ flip) 24 px. Live-edited: 1 px `live.base` border and the digits on gold. Expression line below: `fx` + expression in `value`, `text.secondary`. |
| Switch (Preview) | 32 × 16, 1 px border, knob 12 × 12 square. On: `accent.solid`. |
| Footer | Cancel (secondary, 1 px `border.control`, keycap `ESC`) and Apply (`accent.solid`, check icon, keycap `ENTER`). The existing `OkButton` objectName and behaviour are kept. |

---

## 6. Layout system and settings toggles

### 6.1 Settings (Preferences > **Appearance** and **Layout** pages)

All are live (no restart), stored in QSettings under `ui/` and applied through one `UiSettings` QObject
(`changed(key)`).

| # | Setting (QSettings key) | Values | Default |
|---|---|---|---|
| 1 | Command card placement (`ui/cardPlacement`) | `sidebar`, `floatRight`, `floatLeft`, `free` (set by dragging) | `floatRight` (mockup) |
| 1b | Remember the dragged position (`ui/cardFreePos`) | anchor corner + offset as a fraction of the viewport | on |
| 2 | Floating quick toolbar (`ui/quickToolbar`) | `off`, `left`, `right` | `off` |
| 3 | Main toolbar position (`ui/toolbarArea`) | `top`, `left`, `right` | `top` |
| 4 | Sidebar side (`ui/sidebarSide`) | `left`, `right` | `left` |
| 5 | Orientation widget (`ui/orientation`) | `hexDial`, `viewCube` | `hexDial` |
| 6 | Theme (`ui/theme`) | `dark`, `light`, `system` | `dark` |
| 7 | Timeline style (`ui/timelineStyle`; a new key, no older timeline key exists in the code) | `featureList` (sidebar), `strip` (bottom) | `featureList` (open question 1) |
| — | UI scale (`ui/fontScale`) | 90–130 % | 100 % |

Preferences gets a small **live preview** thumbnail of the layout, drawn with QPainter from the same `LayoutSpec`.

### 6.2 Areas and widgets

`MainWindow` today puts the app bar and ribbon in `setMenuWidget`, the browser in a left dock and the timeline in
a bottom dock. The new arrangement:

| Area | Widget | Mechanism |
|---|---|---|
| Menu widget | `TitleBar` (#AppBar) only | `setMenuWidget` |
| Toolbar | `MainToolbar` inside a non-movable `QToolBar` (`#MainToolbarHost`) | `addToolBar(Qt::Top/Left/RightToolBarArea)`. Qt keeps toolbars outside the dock areas, so "toolbar left + sidebar left" gives toolbar \| sidebar \| viewport. |
| Sidebar | `QDockWidget` `#BrowserDock` (name kept), no title bar, not floatable. Contents: `SidebarHost` = vertical `QSplitter` [card slot, FEATURES, MODEL, SELECTION] with a per-tab `QStackedWidget` for FEATURES and MODEL (as `browserStack_` today) | `addDockWidget(Left/Right)` + `resizeDocks` |
| Bottom | `#TimelineDock` (strip style only) | hidden in `featureList` style |
| Central | viewer or drawing page (unchanged) + `OverlayLayer` | |

`MainToolbar` keeps one model (`ToolbarModel`, unit-tested) and has two layouts:
- **Horizontal**: sections side by side, captions below.
- **Vertical**: captions above the icon columns. A section that does not fit collapses to its caption, whose
  dropdown lists the hidden tools.

On `QToolBar::orientationChanged` it rebuilds its layout but keeps the button objects, so tool ids and checked
states survive.

### 6.3 Overlay layout inside the viewport

`OverlayLayout` (pure geometry, unit-tested) takes the viewport rect and the visible overlay sizes. It returns rects
in this priority order:

1. orientation widget (top-right; `viewCubeRect()` or the dial rect);
2. view-tool column (right edge, below the orientation widget);
3. breadcrumb and filter bar (top-left, single row; they wrap to two rows under 900 px width);
4. command card (by placement);
5. sketch palette;
6. quick toolbar;
7. coordinate readout (bottom-left) and scale bar (bottom-right).

Rules:
- A floating card on the right sits left of the tool column and below the dial's top edge (as in the mockup). On
  the left it sits below the breadcrumb.
- The quick toolbar on the same side as the card sits between the card and the viewport edge. On the right it sits
  left of the tool column. The card shifts by its width.
- When the space is too small (height < card + 120), the card becomes scrollable (as today), then compacts the step
  bar into the header.
- `ViewerWidget::setViewportInsets` gets the union of the occupied edges, so Fit and Home keep the model clear.
- The sketch palette stacks below the card when they share a side, or uses the opposite side when that side is free.

### 6.4 Combination rules

| Combination | Behaviour |
|---|---|
| Card `sidebar` + timeline `featureList` | The card fills the sidebar's top slot and FEATURES collapses to its header plus the live row (option: hidden). It restores on finish. Open question 6. |
| Card `sidebar` + timeline `strip` | The card takes the top slot above MODEL. |
| Card `sidebar` + sidebar hidden (`ShowBrowserAction` off) or a drawing tab | Falls back to `floatRight` (or `floatLeft` if the sidebar side is left and the tutorial is not running), with no setting change. |
| Card `sidebar` + sidebar `right` | The card docks on the right. That is valid and gives the least travel to the right-edge tools. |
| Card `floatRight` + sidebar `right` | Allowed. The card is in the viewport, next to the sidebar. |
| Card `floatLeft` + sidebar `left` | Allowed. This is the shortest path from feature list to card. |
| Toolbar `left` + sidebar `left` | Toolbar outermost (64 px), then the sidebar. The title bar spans the full width. |
| Toolbar `right` + sidebar `right` | Mirror of the above. The view-tool column stays on the viewport's right edge. |
| Toolbar `left` or `right` + quick toolbar on the same side | Allowed. The quick toolbar is inside the viewport. |
| Quick toolbar `right` + card `floatRight` | Order from the edge: tool column, quick toolbar, card. |
| Orientation `viewCube` | `AIS_ViewCube` recoloured from tokens, with its existing overlay controls. The AZ/EL readout and view buttons are still shown under it (option). |
| Theme `system` | Follows `QStyleHints::colorScheme()`. Unknown counts as dark. |
| Drawing workspace | Sidebar shows the sheet browser. Card placement rules apply to the drawing dialogs. The paper stays white. |

**Drag to move the card.** Drag the header. Drop targets (the sidebar slot, the left and right float slots) light up
with a dashed `accent.indicator` outline. Dropping elsewhere stores `free` with an anchor-relative position.
Double-clicking the header returns the card to the chosen placement. The position is clamped on resize, as in
`CommandDialog::reposition()` today.

### 6.5 Persistence

- `ui/*` keys are loaded once at startup before the window shows. This avoids a flash of the wrong layout, so the
  theme is applied in `main()`.
- `QMainWindow::saveState(kLayoutVersion = 2)` stores the dock sizes. A version mismatch resets to the defaults.
- Splitter sizes go in `ui/sidebarSplit`, and the per-panel collapse state in `ui/panels`.
- Tests and testers isolate settings with `XDG_CONFIG_HOME` per display (section 8.3).

---

## 7. Custom-painted pieces

### 7.1 Framing: brackets, hairline, angled tabs

- **Card brackets**: `CardFrame::paintEvent`. There are 4 L-shapes with 10 px arms, 1.5 px `accent.indicator`,
  6 px outside the card rect (the card reserves an 8 px transparent margin). A gold hairline (1 px `live.base`) runs
  along the top edge while a live operation is in progress, and is absent for plain dialogs. Only the active card
  is framed.
- **Angled tabs**: subclass the `QTabBar` paint. Each tab is a `QPainterPath` trapezoid (left and right sides slanted
  12°, 8 px horizontal run), antialiased, with the fill and top 2 px from tokens. Hit-testing uses the path (so the
  overlapped slant belongs to the front tab). Close and modified glyphs are drawn too. The text area is not slanted.
- Toolbar dividers and caption dropdowns are **straight** (the v6 decision): plain 1 px lines with no paint code.

### 7.2 Hex orientation dial (alongside AIS_ViewCube)

- `viewer::OrientationDial` is a QWidget overlay child of `ViewerWidget`, the same pattern as `NavBar`. It is fed
  by a new `cameraChanged` signal (the tutorial plan needs the same signal).
- **Paint**: a hexagon plate (`surface.overlay` 90 %, 1 px `border.default`); a tick ring every 15° (major ticks every
  45°); the 3 world axes projected from the camera. The positive tips are filled squares with the letters X, Y, Z
  in axis colours. Negative tips are hollow and dimmed. A gold mark shows the azimuth on the ring. Below the dial:
  `AZ 045°  EL 35°` and the `ISO TOP FRONT RIGHT` segmented buttons.
- **Input**:
  - click an axis tip: that standard view (a second click gives the opposite);
  - click a hex vertex: the nearest iso;
  - drag the ring: azimuth;
  - drag inside: free orbit;
  - wheel: elevation in 15° steps;
  - keyboard focus (Tab): arrows orbit 15°, Home gives Home.
  The dial animates through the existing `OcctView::animateTo`.
- **Switching**: `ui/orientation=viewCube` displays the `AIS_ViewCube` and hides the dial. `viewCubeRect()` becomes
  `orientationRect()`, and `SketchPalette` and the card use it. Both widgets keep the same right-click menu (Home,
  Set Current as Home, perspective or ortho).
- **AZ/EL definition**: Z is up, azimuth is measured from +X toward +Y, and elevation is from the XY plane. Unit tests
  are in `ViewerMath`.

### 7.3 Value box, glow, rulers and readouts

- The value box stays in the sketch module (`ValueInputOverlay`). It gets objectName `#fcValueBox` (already set),
  a `tag` property and QSS styling only. The gold leader line and `DIST` tag are painted by a thin `ValueBoxFrame`
  that the sketch owner adopts (request).
- **Glow**: a pre-blurred `QImage` (stack blur, radius 6 px at device resolution), cached by (size, colour, dpr). It
  is used for the live handle, the active card brackets (optional) and the hovered dial tip. Do not use
  `QGraphicsEffect` on overlays, because it forces offscreen rendering on every frame. The OCCT handle glow is a
  second, wider, 35 %-alpha stroke drawn under the main stroke.
- **Readouts**: the coordinate readout updates from the hover hit point (throttled to the frame rate). The scale bar
  is computed from `pixelSize()` at the view centre. In sketch mode an optional edge ruler (ticks along the viewport
  top and left at the grid step) uses `vp.gridMajor` with `caption` labels.

### 7.4 HiDPI rules

| Topic | Rule |
|---|---|
| Hairlines | 1 *device* px at any scale: `QPen` width `1.0 / dpr` and coordinates snapped to `floor(x * dpr) / dpr + 0.5 / dpr`. Thick strokes (1.5–2.5) scale with dpr. |
| Fonts | pixel sizes in logical px. The bundled TTF is rendered with `QFont::PreferNoHinting` for consistency across scales. |
| Icons | SVG through `QIcon` at the device size. The cache key includes the theme and dpr. |
| OCCT | line widths and text heights × `devicePixelRatioF()`. The dial and other Qt overlays are separate widgets, so OCCT does not scale them. Check `OcctView` resize at fractional dpr. |
| Glow and shadows | rendered at device resolution and cached per dpr. Invalidate on `screenChanged`. |
| Test matrix | `QT_SCALE_FACTOR` = 1, 1.25, 1.5 and 2 in the offscreen `test_layout_text` and in one uitest pass at 1.5. |
| Helpers | `theme/Hairline.h`: `hairlineFrame`, `hairlineV/H`, `snapInside` fill whole device pixels inside the logical rect (ctest `theme_tokens` checks 1 device px at dpr 1–2 with fractional origins). |

HiDPI pass (phase 5, offscreen grabs at 1, 1.25, 1.5 and 2, both themes): icons are rendered at device size
and sharp at every scale; title-bar tabs, status cells, the dial and the drawing sheet are clean. Fixed: the
segmented control lost its top and left frame edges at 1.25 (widget origin half-way into a device pixel), now
drawn with the hairline helpers. Open (card files, other owner): the card's `live.base` hairline is 2 device px
at 1.5 (a 1 px `fillRect`), and the card border is a `QPen` 1 px (2 device px at 2×); both should move to
`hairlineFrame`/`hairlineH`.

---

## 8. Impact on existing code, tests and tooling

### 8.1 Icons (313 generated SVGs)

- `generate_icons.py` gets a `THEMES = {"light": {...}, "dark": {...}}` palette (from the token generator's dict) and
  writes `resources/icons/light/*.svg` and `resources/icons/dark/*.svg`. That is 626 files, and the qrc entries
  double. Both are deterministic, as today.
- The style moves toward the mockup: filled isometric glyphs, 1.5 px stroke, ink `#C3C9D2` (dark) or `#3C4148`
  (light), purple and gold accents only where they mean selection or live state.
- `ToolInfo::icon` strings (`:/icons/x.svg`) do not change. `Icons.cpp` gains a `ThemedSvgIconEngine` that resolves
  `:/icons/<theme>/x.svg` **at paint time**, so a theme switch needs no `setIcon` calls.
- `test_icons.cpp` checks the same rules for both sets and that the sets have the same names. `resources/ui/*.svg`
  (arrows, checks) get the same treatment.
- `docs/screenshots/icons.png` is regenerated.

### 8.2 objectNames, test hooks and tutorial anchors

**Kept unchanged** (tests or the tutorial plan rely on them):
`FabriCADMainWindow, AppBar, DocumentTabs, NewTabButton, WorkspaceSwitcher, RibbonRow, RibbonGroup, RibbonGroupLabel,
RibbonTool, FinishSketchButton, BrowserDock, Browser, TimelineDock, Timeline, TimelineStrip, CommandDialog,
CommandDialogTitle, CommandDialogMessage, CommandDialogScroll, OkButton, dialogField, prop_<name>, SelectionInput,
NavBar, SearchPopup, CommandLine, CommandHistory, StatusPrompt, StatusDim, ShowBrowserAction, ShowTimelineAction,
ShowPanelsMenu, PreferencesDialog, PropertyEditor, measureTable, fcValueBox, MenuHeader, PaletteCollapse`.

**New**: `MainToolbarHost, FeatureList, SelectionPanel, SidebarHost, CardSlot, Breadcrumb, SelectionFilterBar,
OrientationDial, ViewToolColumn, QuickToolbar, CoordReadout, ScaleBar, StepIndicator, CardPin, SegmentedControl`
(the last with `prop_<name>`).

**New dynamic properties** (not in today's code; the tutorial plan relies on them): `toolId` on every tool button,
`toolbarTab`/`toolbarGroup` on every toolbar section and caption.

| Item | Change |
|---|---|
| `RibbonTabs` (the tab bar) | **Removed**: the mockup has no tab row. The toolbar shows one "page" per context: SOLID by default; the contextual SKETCH page while sketching. SURFACE and TOOLS become extra sections or dropdowns (open question 5). The new property `toolbarTab` on each section keeps the `tab:` and `group:` anchors resolvable. |
| Tutorial anchor resolver | `tool:<id>` returns the first **visible** widget with `toolId` (toolbar, quick toolbar or caption menu). `group:TAB/GROUP` matches the section caption by properties `toolbarTab`/`toolbarGroup`. `tab:TOOLS` maps to the section holding the TOOLS tools. `prop:operation` resolves to a segmented control or a combo (the tutorial's `do` for a segmented control is a click on the cell, so update steps 5.6 and 6.2). Step 13.1's text changes from "TOOLS tab" to the new section name. (Applied in `tutorial.md` on 2026-10-22.) |
| Overlay accent | The tutorial ring and spotlight use `accent.focus`, not `theme-light.qss`. The bubble uses `surface.overlay` and `text.primary` in both themes. |
| Tutorial in all layouts | Its lint and GUI test run the bracket tour in 3 layout presets: the default; card `sidebar` + sidebar `right` + toolbar `left`; light + `viewCube` + `strip`. |
| `test_main_window`, `test_app_logic`, `test_g2_tools` | Only `findChild` by objectName and type. `prop_type` stays a `QComboBox` (it has more than 4 values). Segmented enums get `findChild<SegmentedControl*>`. Add tests: each toggle switches live and round-trips through QSettings; `OverlayLayout` geometry; the `UiSettings` defaults. |
| Marking menu, S-box, Parameters, Preferences | restyle only |
| `docs/screenshots/*.png` | regenerated in the dark default plus one light set |

### 8.3 GUI harness (`tools/uitest/ui.sh`)

Changes (in phase 5):
- `start <N> [bin] [WxH] [scale]` accepts 1920x1080 (mockup size) and a `QT_SCALE_FACTOR`.
- Each display gets `XDG_CONFIG_HOME=$logdir/cfg-$N`, plus a `prefs <N> key=value…` command that writes QSettings
  before launch.
- `shot` keeps `import`, and a new `cvd <in> <out> prot|deut|trit` calls `tools/uitest/cvd.py`.
- New `tools/uitest/compare.py <shot> <mockup.svg>`:
  - reads named region boxes from the mockup SVG (`data-region` attributes added to the v6 SVG);
  - checks layout boxes within ±4 px of the regions;
  - samples token colours at the listed points (ΔE00 ≤ 3);
  - runs a per-region perceptual diff (ImageMagick `compare -metric SSIM`), reported, not gating;
  - writes a side-by-side HTML report.

**Done (phase 5, `ui/p5tools`)**: `ui.sh start <N> [bin] [WxH] [scale]`, `prefs`, `reset`, `geom`, `compare`,
`cvd`; per-display `XDG_CONFIG_HOME`/`XDG_DATA_HOME`; `compare.py` (layout from the app's
`FABRICAD_GEOMETRY_DUMP`, dominant-colour ΔE00, block SSIM, HTML report; stdlib only); `cvd.py` (the
`colormath.py` Machado matrices); `readme-shots.sh`; ctest `uitest_tools`. Usage in `tools/uitest/README.md`.
A rootful Xwayland display may be tiled by the compositor, so exact-size compares use the offscreen grab or
`FABRICAD_SCREENSHOT` + `FABRICAD_WINDOW_SIZE`.

---

## 9. Cross-owner requests (integration notes)

| Owner | Request |
|---|---|
| core | `core::ColorRole` + `PreviewStyle::role`; presets set roles; RGBA stays as a fallback. Optional `ITool::steps()` (labels + which input each step is) for the step indicator. Fallback: the app derives the steps (Ref inputs → steps, then parameters, then CONFIRM). |
| sketch | Replace `colors::` constants with roles; `ValueInputOverlay` loses its inline QSS (objectName/property hooks); constraint chips are drawn with role colours. |
| part | `PropertyPanel` renders enums with ≤ 4 values (and an icon per value, from `ToolInfo`-style hints) as `SegmentedControl` with objectName `prop_<name>`; the expression echo line under length fields. |
| drawing | Canvas surround and selection from tokens; paper and ink stay fixed. |
| integrator | `ARCHITECTURE.md` module map gains `fabricad_theme`; `THIRD_PARTY.md` font row. |

---

## 10. Phased delivery

| Phase | Content | Milestone / exit check |
|---|---|---|
| **0 Foundations** | 0.1 colour audit + lint; 0.2 `tokens.json`, `gen_tokens.py`, `fabricad_theme`, contrast and CVD ctest; 0.3 `ThemeManager`, QSS template, font bundling, caps fonts; 0.4 **v6 mockup** dark + light generated with the token values (`docs/mockups/custom-ui-extrude-v6-tokens-{dark,light}.svg/png`; done 2026-10-22, `data-region` ids) for user sign-off | App runs on tokens with today's layout in dark and light; `theme_tokens` green; user approves v6 |
| **1 Shell** | TitleBar + angled tabs + workspace selector; `MainToolbar` horizontal; status bar; `UiSettings`; Preferences Appearance/Layout pages; theme toggle live | All existing tests green; toolbar fits at 1280 px; theme switch < 150 ms |
| **2 Sidebar** | `SidebarHost`, FEATURES (FeatureListWidget per the feature-list plan; data work done there), MODEL restyle, SELECTION panel, timeline-style toggle, sidebar side toggle | Feature-list plan tests + toggle tests |
| **3 Card** | `CommandCard` (restyled `CommandDialog`), step indicator, segmented control, brackets/hairline, 3 placements + drag/free, collision layout | Extrude flow in all placements; tutorial anchors resolve |
| **4 Viewport** | `ViewerPalette` + OCCT colours, two-tone previews, halo rule, manipulators, hex dial + ViewCube toggle, breadcrumb/filter bar, view-tool column, readouts, scale bar, quick toolbar, vertical toolbar | Screenshot compare vs v6 passes layout boxes; no colour literals in the viewer |
| **5 Icons, light theme, verification** | Themed icon sets (done early, merged with phase 0 from `ui/icons`; the mockup's filled glyph style is still open); light polish; drawing canvas; ui.sh/compare.py/cvd.py; gate G; Sonnet GUI passes; screenshot regeneration; HiDPI pass | Gate G pass (or fallback applied + re-pass); tester issues fixed |

### 10.1 Parallel agent workflow (git worktrees by area)

| Worktree / branch | Agent | Phase | Files | Depends on |
|---|---|---|---|---|
| `ui/tokens` | Opus | 0 | `resources/theme`, `src/theme`, `tools/theme`, `Theme.*`→`ThemeManager.*`, `main.cpp` | — (merged first) |
| `ui/v6-mockup` | Opus/Fable | 0 | `docs/mockups` | tokens.json draft |
| `ui/icons` | Sonnet | 0–5 | `resources/icons/generate_icons.py`, `Icons.cpp`, `test_icons.cpp` | tokens palette dict |
| `ui/shell` | Opus | 1 | `MainWindow` (buildUi/buildAppBar/status), `Ribbon`→`MainToolbar`, `ToolbarModel`, `DocumentTab`, `PreferencesDialog`, `UiSettings` | tokens |
| `ui/sidebar` | Opus | 2 | `FeatureListWidget`, `BrowserTree`, `SelectionPanel`, `SidebarHost` | shell (dock host API), feature-list core work |
| `ui/card` | Opus | 3 | `CommandDialog`→`CommandCard`, `CardFrame`, `SegmentedControl`, `OverlayLayout` | shell |
| `ui/viewport` | Opus | 4 | `src/viewer/**`, `OrientationDial`, overlays, `NavBar`→`ViewToolColumn` | tokens; `OverlayLayout` from card |
| `req/*` | module owners | 2–4 | sketch, part, core, drawing items of section 9 | tokens |

Rules: each agent builds in its own `build-<area>` with `-j2`. Interfaces go first (`UiSettings`, `OverlayLayout`,
`ViewerPalette`, `Tokens` headers are written and merged in phase 0). The integrator merges in phase order and
commits. Each worktree ends with: ctest green, colour lint green, one `ui.sh` screenshot pair (dark and light)
attached to the merge note.

### 10.2 Verification by Sonnet testers (mouse and keyboard only)

1. **T-mockup**: at 1920 × 1080 dark, open the fixture `tests/e2e/bracket-v3.fcad` (groups, a suppressed group,
   13 features). Then press E, click the boss profile, type 25. Shoot and run `compare.py` against v6. Report the
   region deltas and any visual mismatch on a checklist (one row per component in section 5).
2. **T-toggles**: walk all toggle combinations from 6.4 (a pairwise set of 18 runs). For each: no overlap, nothing
   off-screen, Extrude works end to end, and the setting persists after a restart.
3. **T-keyboard**: the whole extrude flow, the feature list (↑/↓, F2, Space = suppress), the dial (Tab, arrows) and
   Preferences, with no mouse. Every focus ring must be visible.
4. **T-cvd**: gate G (4.7).
5. **T-light**: the same as T-mockup in the light theme against the v6 light mockup. The drawing tab shows a white
   sheet with black lines in both themes.
6. **T-tutorial** (once the tutorial exists): the bracket tour in the 3 layout presets.

Testers only use `ui.sh` + xdotool. They never call the probe to act. They report against `docs/uiux` conventions.

### 10.3 Effort estimate

| Area | New/changed lines (incl. tests) |
|---|---|
| Tokens, generator, theme lib, ThemeManager, QSS template, audit lint | 1 800 |
| Fonts, caps, text styles, layout-text test | 400 |
| Title bar, angled tabs, toolbar H/V, status bar | 1 900 |
| UiSettings, Preferences pages, layout manager, persistence | 1 100 |
| Sidebar host, model restyle, selection panel (feature list itself: +1 500–2 500 per its plan) | 900 |
| Command card, step indicator, segmented control, frame, placements, drag | 1 400 |
| Viewer palette, OCCT colours, previews, manipulators, halo | 900 |
| Hex dial, breadcrumb/filter bar, tool column, readouts, scale bar, quick toolbar, OverlayLayout | 1 800 |
| Icon generator themes (+ 626 generated SVGs, not counted) | 400 |
| Harness: ui.sh, compare.py, cvd.py | 500 |
| **Total** | **≈ 11 000** (+ feature list 1 500–2 500) |

That is about 14–18 agent sessions (Opus for code, Sonnet for icons and testing), and about 2–3 weeks of
calendar time with 3–4 worktrees in parallel after phase 0. Phase 0 alone is 2–3 sessions and gates everything
else.

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| ALL CAPS bold mono widens text ~25 %, so the toolbar and sidebar overflow | Width budget (3.4), short labels, caption collapse, `test_layout_text` |
| Qt QSS limits (no letter-spacing, no per-state fonts) | Fonts are set in code per class or property. QSS only colours, borders and paddings. |
| Overlays over `QOpenGLWidget` cost frame time | Overlays are separate widgets with no `QGraphicsEffect` and cached glows. Measure the frame time before and after (≤ 1 ms). |
| Theme switch misses cached colours | Lint bans the colour literals; `themeChanged` is connected in every custom painter; uitest switches the theme mid-command. |
| Cross-owner requests lag | App-side fallbacks (derived steps, combos stay) let phases 3–4 finish without them |
| Tutorial plan written against the old ribbon | Section 8.2's mapping; review the tutorial JSON when phase 3 lands |

---

## 12. Decisions (confirmed by the user, 2026-10-22)

These answers replace the earlier open questions. Where an earlier section says "open question N", this table is the answer.

| # | Question | Decision |
|---|---|---|
| 1 | Timeline style default | **Sidebar feature list** (`ui/timelineStyle = featureList`). The bottom strip stays as an option. |
| 2 | Assembly in the workspace selector | **Toolbar filter of the Design workspace.** No core change; Part / Assembly / Drawing in the dropdown, shortcuts **Alt+1 / Alt+2 / Alt+3** (Ctrl+1..7 stay the standard views). |
| 3 | User-entered names | **As typed** (documents, features, bodies, parameters). All other UI text is caps. |
| 4 | Font | **JetBrains Mono**, bundled (Bold for UI text, Medium for values). |
| 5 | SURFACE and TOOLS tools | **A "MORE ▾" section at the end of the toolbar.** The contextual SKETCH sections replace the main ones while sketching. |
| 6 | Card docked into the sidebar | **Overlay**, with FEATURES collapsed to its header and the live row; it restores when the command finishes. FEATURES **expands automatically while a feature-selection input is active** (Mirror, patterns), then collapses again. |
| 7 | Light-theme viewport | **Light gradient** (`#FBFCFD` to `#D5DBE3`). |
| 8 | Free sketch curves vs selection (protanopia) | **Change the free-curve colour.** Pick a new `sk.free` hue in phase 0 that passes the colour-vision gate against selection purple (ΔE ≥ 10, or ≥ 6 under tritanopia) in both themes. The selection halo and grips stay as extra cues. |
| 9 | Error colour | **Crimson-rose** accepted (`#FF5C8A` dark, `#C4125E` light), always with the octagon icon. |
| 10 | Mockup effects | **Glow on by default; scanline texture off by default.** Both stay settings. |
| 11 | Unsaved-document marker | **Neutral** white/grey dot (`text.secondary`). Gold stays reserved for live operations. |
| 12 | Follow system theme | **Include now:** Theme = Dark / Light / Follow system (Qt 6.5+ colour scheme). |
