# T-light: light theme against the v6 light token mockup, and the Drawing sheet in both themes

Verifier: Sonnet, private display :89 (`UI_LOGDIR=/tmp/fabricad-uitest-p5light`, own config and data, `ui/theme=light`), binary `build/bin/fabricad` (main at fefa14c), fixture `tests/e2e/bracket-v3.fcad`. Reference: `docs/mockups/custom-ui-extrude-v6-tokens-light.{png,svg}`. Nothing outside the private Xwayland window was touched. Screenshots: `light/`.

## Method and environment limits

* The host tiled my Xwayland root to 466 x 239 px (other testers were running). Negative window moves do not scroll the content on this display, so a 1920 x 1080 window cannot be panned. The interactive run therefore used `QT_SCALE_FACTOR=0.24` with the window at the root size (about 1940 x 996 logical). The `gui-*.png` shots are 4x enlargements of that and are layout evidence only, not readable text. Real mouse and keyboard only (xdotool): `E`, click, type `25`, Enter, workspace menu, toolbar clicks.
* The app needs `vblank_mode=0` here: without it the window stopped repainting after the first frame (grabs identical after input). Root grabs also lag one to three input events, so every shot nudges the pointer first.
* Exact 1920 x 1080 pixel evidence is the app's own window grab (`FABRICAD_WINDOW_SIZE=1920x1080 FABRICAD_SCREENSHOT`, GL on :89; scene set by `--script`, `static-*.png`) fed to `compare.py`. These are static references, not driven checks.

## Result summary

| Area | Result | Evidence |
|---|---|---|
| Light tokens (AppBar, toolbar host, sidebar host, status bar) | pass: dE00 0.0 against the mockup, layout ok | static-extrude-light, compare-light.json |
| Command card colours, Apply button (#6D3FE0), card bg, selected feature row (#FFF3CC), extrude pill | pass: pixel colours equal the mockup | static-extrude-light |
| Viewport background / body shading | pass with deltas: viewport #ECEFF2 vs #F3F5F8, body top #AAAEB2 vs #BFC3C8, wall #9DA0A4 vs #ADB0B5 (mockup bodies are lighter; edges and ordering are right) | static-extrude-light |
| Layout (compare.py): 6 of 31 regions within 4 px | structural deltas, listed below | compare-light.json |
| Extrude flow, mouse and keyboard: `E`, pick profile, type `25`, Enter | pass via the FEATURES row (Extrude4, 25 mm, new row in list, dirty marker). Direct viewport pick fails, see P5-LIGHT-1 | gui-01..05 |
| Drawing workspace, light and dark: white sheet, black lines | pass (sheet #FFFFFF, frame, title block, deselected views black) in both themes; see P5-LIGHT-4 for selected views | static-drawing-light/dark, gui-06, gui-07 |

### Component checklist (section 5, light)

| Component | Status | Note |
|---|---|---|
| Title bar: logo, workspace switcher, tabs, search, undo/redo/save/help | partial | switcher shows `ALT 1` (mockup `CTRL 1`); one tab only (fixture); search box lacks the magnifier and `CTRL K` chip |
| Toolbar groups | partial | mockup tools missing: PROJECT, SWEEP, LOFT, PATTERN, MIRROR, SURFACE, SECTION; app adds PRESS PULL, COMBINE, AXIS, INTERFERE. Likely backend-deferred, mark for the plan owner |
| Extrude tool highlight | pass | |
| FEATURES panel: header tick, chip 13, rows, suppressed group strike, live row, rollback bar | pass | header has an extra chevron; no `BOSS PROFILE` tags (shows `40 x 40`) |
| MODEL panel | partial | rows present; mockup shows an ACTIVE badge on Bracket and a PARAMETERS group |
| SELECTION panel | mismatch | stays `NOTHING SELECTED` while the card says 1 selected (P5-LIGHT-3) |
| Command card | partial | pass: header, 3 steps, fields, segmented controls, Cancel/Apply. Missing vs mockup: segmented icons, MERGE WITH row, PREVIEW toggle, `fx boss_height` line; extra START OFFSET row; card is 38 px shorter so Apply is 38 px higher |
| Value box on the handle | partial | gold tag reads `DISTANCE` (mockup `DIST`), no unit/stepper; no longer under the card (keep-out works) |
| Orientation dial, ISO/TOP/FRONT/RIGHT, AZ/EL | pass | AZ 315 vs mockup 045 (different camera) |
| View tool column, scale bar, coordinate readout | pass | readout shows `X —` until a pick |
| Breadcrumb + filter bar | partial | chip `EXTRUDE` (mockup `EDITING Extrude 3`); BODIES and EDGES hidden for Extrude (by design) |
| Status bar and command line | partial | command line box is 280 px wide at x 1107 (mockup 400 px at x 652), text truncated; no `1 PROFILE` tail |

## Issues

| ID | Severity | Repro | Expected | Actual |
|---|---|---|---|---|
| P5-LIGHT-1 | Medium | Open bracket-v3 (rolled back to Sketch3), `E`, click the centre of the boss circle in the 3D view | The boss profile is picked (T-mockup step: "click the boss profile") | The click hits the later Extrude3 cylinder wall: red error `Only planar faces can be used as profiles`; the profile circle on the plate is hidden behind that body. Clicking the Sketch3 FEATURES row works. gui-02 |
| P5-LIGHT-2 | Low | After P5-LIGHT-1, click the Sketch3 row | PROFILES `1 SELECTED` (the rejected face dropped) | `2 SELECTED`: the rejected non-planar face stays in the count (Extrude still applied fine). gui-03 |
| P5-LIGHT-3 | Medium | Select the boss profile in Extrude | SELECTION panel lists the profile (TYPE, SOURCE, AREA, PERIMETER) as in the mockup | Panel keeps `0` / `NOTHING SELECTED`; static-extrude-light, gui-04 |
| P5-LIGHT-4 | Low | Place a base view in Drawing (gui-06/07), do not click away; also static-drawing-* | Lines black on the white sheet | A just-placed view stays selected and draws all its lines in purple until the sheet is clicked (clicking empty sheet gives black, gui-07). In the static grab the iso view is purple even after `SELECT none`. Selection colour on the sheet should arguably be a bbox only, or the view should be deselected after Apply |
| P5-LIGHT-5 | Low | Value box and handle during Extrude, light | Tag `DIST`, value with unit and stepper, `fx` parameter line | Tag `DISTANCE`, value without stepper, no `fx` line (known L2, still open) |
| P5-LIGHT-6 | Low | Compare toolbar, card and title bar with the mockup | Mockup tools and card rows (see checklist) | Missing tools, MERGE WITH/PREVIEW rows, segmented icons, `CTRL` vs `ALT` workspace shortcuts. Confirm which are intended before the next mockup sign-off |
| P5-LIGHT-7 | Low | Viewport and body, light | Body `#BFC3C8` top / `#ADB0B5` wall, viewport `#F3F5F8` | `#AAAEB2` / `#9DA0A4`, viewport `#ECEFF2`; app body is about 10 L darker than the mockup in light (opposite of p4 M1, which is now fixed), viewport gradient slightly darker |
| P5-LIGHT-8 | Low (unverified) | `Drawing` from the workspace menu on a part with no sheet, click CREATE DRAWING... | The New Drawing card appears next to the status line "choose the standard, sheet size and scale" | Grabs for several seconds showed the status line but only two white lines where the card sits (top right); the sheet appeared later and the card drew fine for BASE VIEW. May be the grab lag; recheck on a full-size display |

compare.py flagged dE (`!`) only for fcValueBox (19.2), OkButton (45.8) and CommandLine (4.7). The first two are artefacts of the shorter card and the different handle position (the mockup box lands on body or card background in the shot); the third is the narrower command line. All other regions: colour dE00 <= 1.0.
