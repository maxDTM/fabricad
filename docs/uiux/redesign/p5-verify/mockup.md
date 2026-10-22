# P5 T-mockup: dark theme against the v6 token mockup (ui-redesign 10.2 #1)

Verifier: Sonnet. Binary `build/bin/fabricad` (main at fefa14c; the only newer source file is a sketch test). Reference: `docs/mockups/custom-ui-extrude-v6-tokens-dark.png/.svg`. Evidence is in `mockup/`.

## What was run

1. Offscreen grab, 1920 x 1080, dark: `QT_QPA_PLATFORM=offscreen FABRICAD_WINDOW_SIZE=1920x1080`, bracket-v3, `ROLLBACK Sketch3`, `SELECT profile:@100,55,12`, `EXTRUDE 25` (no GL offscreen, so the 3D view is blank). `mockup/offscreen-1920x1080-dark.png`, geometry `geometry-offscreen.json`, compare `compare-offscreen.{txt,json}` and `compare-offscreen/index.html`.
2. Interactive, private display :85 (xdotool only, `ui/theme=dark`, own `XDG_*`): open `tests/e2e/bracket-v3.fcad`, drag the rollback bar in FEATURES to just under Sketch3 (the fixture is saved with the boss already extruded, so this gives the "boss not yet extruded" state), click empty viewport, press `E`, click the Sketch3 row in FEATURES (picks the boss profile), type `25`. The card moved to `02 DISTANCE`, DISTANCE `25 mm`, value box `25`, gold preview. `mockup/interactive-1920x1080-dark.png` (1920 x 1080, 3D view included), `compare-interactive.{txt,json}`, `compare-interactive/index.html`, `geometry-interactive.json`.
   - Display note: the host tiles the Xwayland root to 466 x 490 / 1900 x 241 (it changed while I worked), so a root screenshot cannot show a 1920 x 1080 window and shifting the window gave stale root grabs. I drove the app with the mouse and keyboard on the shifted window and let the app grab itself (`FABRICAD_SCREENSHOT` with a 60 s delay, nothing else scripted) to get an exact 1920 x 1080 frame with the 3D view. I tried to float and resize my own Xwayland window with `hyprctl`; the permission check denied it, nothing was changed and I did not retry.
   - `interactive-run1-stray-click-2-profiles.png`: first run, my empty-viewport click landed on the back wall face and added a second profile (`2 SELECTED`, wall extruded too). My mistake, kept only because it shows multi-profile preview. `card-initial-validation-text-bottom.png`: card right after `E` with nothing picked.

## Region deltas (compare.py, tol 4 px)

Layout: 6 of 31 regions inside 4 px in both runs (AppBar, MainToolbarHost, SidebarHost, StatusBar, ScaleBar, ViewToolColumn). Colour dE00 flags (> 3):

| Region | Offscreen | Interactive | Reason |
|---|---|---|---|
| Viewer | 1.6 | 6.3 | interactive has real 3D body and the app's fit/camera, mockup body is a different camera |
| fcValueBox | 7.0, box at (444,158) | 42.6, box at (1040,628) | anchored to the handle, so it follows the camera; offscreen has no handle (top-left fallback, covers breadcrumb, = p4 M3) |
| Breadcrumb | 5.3 | 4.3 | edit chip text and width differ (P5-MOCKUP-7) |
| SelectionFilterBar | 6.8 | 7.0 | no active segment (P5-MOCKUP-7) |
| OkButton | 35.0 | 0.0 | offscreen card is 38 px shorter than the mockup, so the sample misses the button |
| RollbackBar | 18.3 | 18.3 | mockup samples the gold live row; app row/bar sit 6 to 8 px lower |

Layout edges (l,t,r,b, px, same in both runs unless noted): Viewer +6,0,0,0; Breadcrumb +6,0,-94.6,0; SelectionFilterBar -94.6,0,-215,0; OrientationDial -14,0,+14,+30; StepIndicator +16,0,-16,0; CommandDialog 0,0,0,-16 (offscreen -38); OkButton -6.3,-16,0,-16; WorkspaceSwitcher -3,0,-28.5,0; DocumentTabs -32.5,-4,-253.5,0; SearchBox +66,0,+66,0; RibbonGroup:* left -4 to +84, right -129 to +150 (all fail); FeatureList 0,+36,0,-7; Browser 0,+26,0,+31; SelectionPanel 0,+18,0,0; CommandLine +455,+1,+335,-2; CoordReadout +6,0,+6,0. Systematic causes: the sidebar is 326 px wide (mockup 320), which shifts every left overlay by 6; the mockup sidebar boxes include the panel header, the app widgets do not; the app dial widget includes the view-button row; the mockup shows 3 tabs and extra tools (PROJECT, SWEEP, LOFT, PATTERN, MIRROR, SURFACE, SECTION) that the app does not have, so group widths cannot match. `RollbackBar` and `ViewButtons` have no own widget (`missing`).

## Per-component checklist (ui-redesign section 5, dark)

PASS = matches spec and mockup in the grabs. PARTIAL = present with deviations (issue id). FAIL = missing or wrong. N/V = not exercised in this run (covered by p4 check or needs hover/other state).

### 5.1 Window chrome

| Component | Result | Notes |
|---|---|---|
| Title bar `#AppBar` | PARTIAL | 36 h, border, logo + FABRICAD, tabs, search, undo/redo/save/help; find (magnifier) button missing, undo/redo/save icons accent-coloured, logo glyph is the blue cube (P5-MOCKUP-9) |
| Workspace selector | PARTIAL | `PART ALT 1 ▾` per decision 2 (mockup says CTRL 1); 25 px narrower than the mockup box; arrow not in its own cell |
| Document tab | PASS | active tab on `surface.panel`, 2 px top indicator, modified dot, close x, `+` tab. Only one tab (mockup has three: fixture data) |
| Search box | FAIL | `CTRL K` keycap clipped to `S`, right border cut (P5-MOCKUP-1) |
| Main toolbar `#RibbonRow` | PASS | 70 h, sections SKETCH CREATE MODIFY ASSEMBLE CONSTRUCT INSPECT MORE, DOCUMENT right-aligned, straight dividers (tool set differs, P5-MOCKUP-10) |
| Tool button | PARTIAL | active EXTRUDE has selFill + indicator border, labels 9 px caps; icons are multi-colour (mockup monochrome), no split arrows on tools with variants (P5-MOCKUP-10) |
| Section caption | PARTIAL | caption + ▾ straight; the section holding the running tool is not highlighted (mockup CREATE) (P5-MOCKUP-10) |
| Vertical toolbar | N/V | p4 check |
| Status bar | PARTIAL | 28 h, left gold bar + square + EXTRUDE, key hints, rebuild meter OK; prompt stale (P5-MOCKUP-2); no grid/snap icons, no selection-count cell (P5-MOCKUP-8) |
| Command line `#CommandLine` | PARTIAL | `>` prompt, `HISTORY ↑` hint; sits right of centre (x 1086 vs 652), placeholder truncated `Profiles: 1 selected · Enter to f…` (P5-MOCKUP-8) |

### 5.2 Sidebar

| Component | Result | Notes |
|---|---|---|
| Panel header | PASS | 36 h band, 3 px tick, glyph, bold tracked caps, count chip, chevron; FEATURES search + filter, MODEL master eye |
| Feature row | PASS | 28 h, indent, icon, right value column; live (gold), suppressed (struck, dim), rolled back (dim) states seen |
| Group row | PASS | chevron, folder, count, tri-state box (checked purple, unchecked) |
| Suppress checkbox | PASS | 14 px, purple check; hover/focus N/V |
| Rollback bar | PASS | white line + ROLLBACK tag + square knobs; mouse drag from after Extrude3 to under Sketch3 worked and the model rolled back; keyboard focus ring seen; hover/drag gold N/V |
| Insert-at-rollback | PARTIAL | dashed `+ INSERT FEATURE AT ROLLBACK` exists but only its edge shows above the MODEL header until the list is scrolled (P5-MOCKUP-14) |
| Model row | PARTIAL | 28 h, eye at right, count, SOLID/PART tags; the `ACTIVE` badge on Bracket is missing (P5-MOCKUP-11) |
| Selection panel | PARTIAL | header + `NOTHING SELECTED`; stays empty while a profile is picked for Extrude (mockup: 1, TYPE/SOURCE/AREA/PERIMETER) (P5-MOCKUP-6) |
| Inline tip | N/V | hover tip (GROUP SUPPRESSED) not triggered |
| Splitters | N/V | handle visible, hover not tested |

### 5.3 Viewport overlays

| Component | Result | Notes |
|---|---|---|
| Breadcrumb bar | PARTIAL | 28 h, `bracket-v3 > Bracket >` chip; chip reads `EXTRUDE`, spec/mockup `EDITING Extrude 3` (P5-MOCKUP-7) |
| Selection-filter bar | PARTIAL | segments auto-hide (FACES, SKETCH PROFILES only); no segment shown active (P5-MOCKUP-7) |
| Command card | PASS | 360 w, corner brackets, gold hairline while active; body see 5.4 |
| Value box `#fcValueBox` | PARTIAL | gold DISTANCE tag over a 1.5 px gold box, `25`; no unit, no spinner, dashed vertical leader only, box sits left/below the handle over the preview (P5-MOCKUP-4) |
| Drag handle | PARTIAL | gold arrow with ink outline and base diamond; an extra ring around the handle; jagged fill artefact at the preview base (P5-MOCKUP-13) |
| Dimension chip | FAIL | no `ø 40` chip on the Sketch3 circle (P5-MOCKUP-13) |
| Orientation dial | PASS | hexagon, axis tips, AZ 315 EL 35, ISO/TOP/FRONT/RIGHT with ISO active |
| View-tool column | PASS | 36 w, 6 buttons, right edge (box within tolerance) |
| Coordinate readout | PASS | 3 cells with axis ticks + unit cell, `—` with no pick (+6 px sidebar shift) |
| Scale bar | PASS | 20 mm at this zoom, ticks, bottom-right |
| Quick toolbar, sketch palette, toast | N/V | off by default / not triggered |

### 5.4 Command card body

| Control | Result | Notes |
|---|---|---|
| Header | PASS | icon, EXTRUDE, `E` keycap, `PART · bracket-v3` (mockup `Bracket`), pin, close |
| Step indicator | PASS | 01 purple (done), 02 gold (current), 03 grey; 22 h |
| Selection chip | PARTIAL | `1 SELECTED` selFill field with a separate ×; no square glyph, no `Sketch 3` sub-label (P5-MOCKUP-3) |
| Segmented control | PARTIAL | label-above, 28 h, checked = selFill + indicator; no icons (mockup 52 h with icons), order ONE SIDE / TWO SIDES / SYMMETRIC (mockup ONE SIDE / SYMMETRIC / TWO SIDES) (P5-MOCKUP-3) |
| Combo | PASS | EXTENT DISTANCE with ▾ cell |
| Value field | PARTIAL | `25 mm`, but no live gold border or gold digits, no ⇄ flip, no `fx boss_height = 25 mm` line (P5-MOCKUP-5) |
| Switch (Preview) | FAIL | missing (P5-MOCKUP-3) |
| Footer | PASS | CANCEL `ESC`, APPLY `ENTER`, accent fill |

## Issues

| ID | Severity | Repro | Expected | Actual |
|---|---|---|---|---|
| P5-MOCKUP-1 | Medium | Any state, dark, 1920 x 1080 (`mockup/interactive-1920x1080-dark.png`, top right) | Search box 260 w with full `CTRL K` keycap and 1 px border | Keycap clipped to `S`, right border cut; box is 66 px right of the mockup because the find button is missing |
| P5-MOCKUP-2 | Medium | Open bracket-v3, `E`, pick profile, card shows `02 DISTANCE` | Status bar prompt `DRAG THE HANDLE OR TYPE A DISTANCE.` (step 2); command line cell fully readable | Prompt still `PROFILES: 1 SELECTED · ENTER TO FINISH`; command line shows truncated placeholder `Profiles: 1 selected · Enter to f…` |
| P5-MOCKUP-3 | Medium | Same state, card body | Profile chip + sub-label, icon segments (ONE SIDE / SYMMETRIC / TWO SIDES), `fx` line, flip, TAPER with `DRAFT OUTWARD`, MERGE WITH combo, PREVIEW switch (5.4) | Plain `1 SELECTED` field, label-only segments in another order, extra START OFFSET / FLIP DIRECTION / OBJECTS TO CUT/JOIN rows, no MERGE WITH, no PREVIEW switch; card 516 h vs 532 |
| P5-MOCKUP-4 | Medium | Same state, viewport | Value box `25 mm` with spinner, anchored right of the handle with a 1 px gold leader (mockup 924,531) | `25` only (no unit, no spinner), box left/below handle overlapping the preview, dashed vertical axis line instead of a leader. Offscreen (no GL) it lands top-left over the breadcrumb (known p4 M3) |
| P5-MOCKUP-5 | Low | Same state, type `25` | DISTANCE field in live-edit style: 1 px `live.base` border, digits on gold | Field looks like the others; the gold shows only in the viewport value box |
| P5-MOCKUP-6 | Low | Same state | SELECTION panel shows `1` with TYPE / SOURCE / AREA / PERIMETER for the picked profile | `SELECTION 0`, `NOTHING SELECTED` |
| P5-MOCKUP-7 | Low | Same state, breadcrumb and filter bar | Chip `EDITING Extrude 3`; `SKETCH PROFILES` active (selFill + 2 px underline) | Chip `EXTRUDE`; no segment active |
| P5-MOCKUP-8 | Low | Status bar | Grid and snap icons, `1 PROFILE` selection cell at right, command line next to the key hints | Text only, no selection cell, command line at x 1086 (mockup 652) |
| P5-MOCKUP-9 | Low | App bar | Neutral monochrome undo/redo/save, find button between save and help, purple F logo | Accent-coloured icons, no find button, blue cube logo |
| P5-MOCKUP-10 | Low | Main toolbar | PROJECT, SWEEP, LOFT, PATTERN, MIRROR, SURFACE (MORE), split arrows, active section caption highlighted | These tools absent (may be backend scope), no split arrows, no active-caption highlight, multi-colour icons; every RibbonGroup region fails layout |
| P5-MOCKUP-11 | Low | MODEL panel | `ACTIVE` badge on Bracket (1 px `accent.text`) | Shows only `SOLID` |
| P5-MOCKUP-12 | Low | Press `E` with nothing selected (`card-initial-validation-text-bottom.png`) | Quiet initial state | Red `SELECT A PROFILE TO EXTRUDE` shown at once; card is 494 h in the offscreen run and 516 h interactive in the same step (blank band above the footer) |
| P5-MOCKUP-13 | Low | Preview with boss profile picked | Clean amber preview, `ø 40` dimension chip, glow | Extra ring around the handle, jagged fill artefact on the lower ellipse, no dimension chip |
| P5-MOCKUP-14 | Low | FEATURES with 13 rows, 1920 x 1080 | `+ INSERT FEATURE AT ROLLBACK` visible under the last row | Only its dashed edge shows above MODEL until the list is scrolled |
| P5-MOCKUP-15 | Low (tooling) | `tools/uitest/compare.py` on a correct 1920 x 1080 grab | Regions describe the same boxes as the widgets | 25 of 31 layout fails are systematic: sidebar 326 vs 320 (+6 on all left overlays), mockup sidebar boxes include the header, dial box vs dial+buttons, 3 mockup tabs; regenerate `data-region` boxes or align the sidebar width so the gate can pass |
