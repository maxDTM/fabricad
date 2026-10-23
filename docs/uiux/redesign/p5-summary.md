# UI redesign: phase 5 fixes and summary

Branch `ui/p5fix` (merged into `main`). Plan: `docs/plans/ui-redesign.md`. Tester reports: `p5-verify/{mockup,toggles,keyboard,cvd,light}.md`. New evidence: `p5-fix/`.

## 1. What the redesign delivered (phases 0 to 5)

| Phase | Delivered |
|---|---|
| 0 Foundations | Colour audit and lint (`theme_color_lint`). `tokens.json` is the single source for the generator, `fabricad_theme`, the QSS template and the contrast and CVD ctest (`theme_tokens`). Also `ThemeManager`, the bundled JetBrains Mono with caps text styles, the v6 token mockups (dark and light), and themed icon sets. |
| 1 Shell | App bar with angled document tabs, workspace selector (Part / Assembly / Drawing, Alt+1..3), search box and undo/redo/save/help. `MainToolbar` with section captions and a MORE section. Token status bar (command, prompt, key hints, command line, cells, rebuild meter). `UiSettings`, the Preferences Appearance and Layout pages, live theme switch, and Follow system. |
| 2 Sidebar | `SidebarHost` with FEATURES (`FeatureListWidget`: groups, suppress, rollback bar, insert at rollback), the restyled MODEL tree, the SELECTION panel and the card slot. The timeline-style and sidebar-side toggles. |
| 3 Card | `CommandCard` (it was `CommandDialog`): brackets and gold hairline, step indicator, segmented controls, and the float-right, float-left, sidebar and free placements with drag. `OverlayLayout` handles collisions. |
| 4 Viewport | `ViewerPalette` (no colour literals in the viewer): two-tone previews, the halo rule, gold manipulators, the hex orientation dial (ViewCube optional), breadcrumb and selection-filter bar, view-tool column, coordinate readout, scale bar, quick toolbar and the vertical toolbar. |
| 5 Polish and verification | Sketch colour roles, value-box keep-out and leader, edge rulers, drawing canvas tokens, HiDPI hairlines, and the `ui.sh` / `compare.py` / `cvd.py` harness. Five Sonnet passes (T-mockup, T-toggles, T-keyboard, T-cvd, T-light), then this fix round. |

## 2. Gate G (4.7) result

**Result: PASS after the fixes. The teal fallback was not applied.**

| Check | Result |
|---|---|
| 1. Numeric (`theme_tokens` CVD rows) | green |
| 3. Perception, live vs warning | 40/40 = 100 % (needs 100 %) |
| 3. Perception, overall, strict scoring (as tested) | 130/144 = 90.3 %: **FAIL** (needs 95 %) |
| 3. Perception, overall, strict re-score after the fixes | 140/144 = 97.2 %: **PASS**. 144/144 if the conflict-curve width cue counts (not re-shot). |

Every one of the 14 strict misses was an **error** shown by colour alone: red card text, the Parameters error, the status bar "over-constrained" cell and message, and the crimson conflict curve. None of the misses was a gold-vs-warning confusion. The teal fallback replaces the *live* colour, so it would not have changed any of these 14 views.

Decision 9 already requires every error to carry the octagon, so the fix was to add the missing shape cues:

- **Octagon glyph** (`theme::StatusGlyph`) on the card message, the property-panel message, the status bar message (warnings get a triangle) and the sketch "over-constrained" cell.
- **Parameters error:** the octagon is added there too.
- **Error colour:** the status bar message now really turns error-red. Before, `#StatusMessage` overrode the level colour, so it stayed grey.
- **Conflict curves:** drawn twice as wide. A dash would read as construction.

The fixed views were re-run through `cvd.py`. The octagon stays readable in normal, protan, deutan and tritan, in both themes (`p5-fix/gateG-error-glyph-cvd.png`).

**Open:** the 97.2 % is my re-score of the tester's crops with the new cue. It is not a fresh blind tester run (see 4).

## 3. Phase 5 tester issues: what was fixed

| Issue | Sev. | Fix |
|---|---|---|
| P5-MOCKUP-1 | M | The search keycap is a 16 px chip showing `CTRL K`, so the box border is no longer covered. **Ctrl+K** now opens the search from anywhere except text fields. |
| P5-MOCKUP-2 | M | After the profile pick the prompt is `DRAG THE HANDLE OR TYPE A DISTANCE.` (any feature with a handle). Without a handle it is `Type a … · Enter to finish`. |
| P5-MOCKUP-3 | M | Partly fixed. Fixed: the selection chip (`1 PROFILE`, `2 FACES`, …) with its source sub-label (`Sketch3`, as typed), the `fx` expression line under value fields, and Flip as the ⇄ trailing action on DISTANCE (the FLIP DIRECTION row is gone). Still open: the MERGE WITH combo, the PREVIEW switch, the segment icons and order, and the extra START OFFSET row (see 4). |
| P5-MOCKUP-4 | M | The value box now has a `DIST` tag, a unit suffix (`mm`, `°`) and a ▴▾ stepper (Shift ×10). It is anchored at the arrow **tip**, not the base, with the gold leader. Offscreen (no GL) it still uses the fallback position. |
| P5-MOCKUP-5, P5-KEYBOARD-3 | L, H | While the value box is typed into, the card's field shows the live state: 2 px gold border on `live.fill`. Card fields with focus have 2 px rings. |
| P5-MOCKUP-6, P5-LIGHT-3 | L, M | SELECTION lists the running command's picks (profile type, source, area, perimeter) while the selection itself is empty. |
| P5-MOCKUP-12 | L | Quiet first step: no red "select a profile" until something is picked. Apply stays disabled. |
| P5-TOGGLES-1 | major | A right sidebar started at its 260 px minimum, because the pre-show dock width was taken as the user's width. It now starts at 320 and re-applies after layout. The property panel's minimum width went from 280 to 220. In the r02/r04 case the card now fits (`p5-fix/toggles-right-sidebar-card-light-1600x1000.png`). |
| P5-KEYBOARD-1 | M | Stale focus rings: on a private Xwayland, region repaints left old rings on screen and a full repaint did not. Focus changes now repaint the window. |
| P5-KEYBOARD-2 | H | A Tab on the canvas that the tool does not use (a one-field value box) moves into the card. Shift+Tab goes to Apply. Inside the card the Qt focus chain walks every field. |
| P5-KEYBOARD-4, P5-KEYBOARD-9 | H, L | During a command the FEATURES arrows only move the cursor, and **Space picks the row for the command** (it no longer suppresses). FEATURES can be reached by Tab. `SELECT sketch:Sketch3/profile:…` on the command line also works. |
| P5-KEYBOARD-5 | H | Toolbar tools, captions, the title-bar buttons, document tabs, the view column and the quick toolbar are Tab stops (`TabFocus`: a click still leaves the keyboard on the canvas). Start-up focus is on the viewport. |
| P5-KEYBOARD-6 | M | 2 px `accent.focus` rings on chrome tool buttons, push buttons, check and radio indicators, tab bars (painted on the document tab), card combos and card tool buttons. |
| P5-KEYBOARD-7 | M | With a tool button, tab bar or panel header focused, S and the letter shortcuts work as on the canvas. Space, Enter, the arrows and Tab stay with the control. |
| P5-CVD-1, P5-CVD-2, P5-CVD-3 | M, M, L | See 2. |
| P5-LIGHT-1 | M | Works as designed, no change. Sketch3 is consumed by Extrude3 and hidden, so a click there hits the visible wall and gets the correct error. The FEATURES row (mouse or Space) and `SELECT` pick the profile. |
| P5-TOGGLES-3 | process | Not fixable in the repo: no Xvfb or Xephyr on the host, and parallel rootful Xwayland windows tile each other. Run GUI testers one at a time, or use app self-grabs at an exact size (`FABRICAD_WINDOW_SIZE` + `FABRICAD_SCREENSHOT`), as this round did. |

How the fixes were checked:
- **Tests:** full ctest is green (76/76) and so is the colour lint. The `PropertyPanel` allow-list entry was removed because it has no literals left.
- **New tests:** router (Ctrl+K, chrome keys), Tab into the card, FEATURES pick mode, S from a panel header, and the value-box anchor at the tip.
- **GL self-grabs at 1920×1080 on :90**, dark and light (`p5-fix/gl-1920x1080-*.png`, compare reports beside them). Layout is still 6 of 31 regions. The card is now 75 px shorter than the mockup: it has no FLIP row, no MERGE WITH and no PREVIEW.
- **Mouse and keyboard on :90:** the display was tiled to 945×490, so it ran at scale 0.6. Checked there: Tab into the card, the live field, Ctrl+K, toolbar focus rings, and S from the FEATURES header.

## 4. Open items

| Item | Notes |
|---|---|
| Gate G blind re-run | One Sonnet T-cvd pass on the new build: crisp 1x crops, plus a conflict-curve scene and a warning-toast scene (no toast exists yet). |
| Card body vs mockup (rest of P5-MOCKUP-3, P5-LIGHT-6) | MERGE WITH combo (today the OBJECTS TO CUT/JOIN selection input), PREVIEW switch (the tool needs a preview on/off), segment icons, DIRECTION order (mockup ONE SIDE · SYMMETRIC · TWO SIDES; the app uses the enum order), and START OFFSET (a real property the mockup omits). The plan owner should decide. |
| Toolbar vs mockup (P5-MOCKUP-10, P5-LIGHT-6) | **Done on `rm/feat`**: PROJECT, SWEEP, LOFT, PATTERN, MIRROR, SURFACE (MORE), SECTION (new Section Analysis), split arrows and the highlighted caption of the running command's section (`backend-deferred.md`, rm/feat). Monochrome icons remain open. |
| `compare.py` layout gate (P5-MOCKUP-15) | 25 of 31 fails are systematic: sidebar 326 vs 320, the mockup boxes include panel headers, and tab/tool counts differ. Regenerate the mockup `data-region` boxes from the app geometry or relax those regions. |
| Partial-flush staleness under Xwayland | Fixed for focus changes only. Other partial repaints may show late in the same setup (P5-KEYBOARD-1 "some state changes"). This needs a Qt/driver-level look (buffer age with the GL-composed window). |
| Low mockup gaps | P5-MOCKUP-7 (`EDITING …` chip and active filter segment), P5-MOCKUP-8 (status grid/snap icons, selection cell), P5-MOCKUP-9 (neutral app-bar icons, find button, logo), P5-MOCKUP-11 (`ACTIVE` badge), P5-MOCKUP-13 (ring around the handle, fill artefact, `ø 40` chip), P5-MOCKUP-14 (insert row hidden until scrolled), P5-LIGHT-7 (light body about 10 L darker). |
| Low behaviour gaps | P5-TOGGLES-2 (short sidebar slot scrolls the card), P5-KEYBOARD-8 (dial arrows orbit about the view axes, not exact AZ/EL steps), dial view buttons are not separate Tab stops (Ctrl+1..7 are the keys), P5-LIGHT-2 (a rejected face stays in the PROFILES count), P5-LIGHT-4 (a just-placed drawing view stays purple until deselected), P5-LIGHT-8 (unverified). |
| Value box offscreen | Without GL the box falls back near the top-left (p4 M3). It only affects headless grabs. |
| Tester passes not yet repeated | T-keyboard and T-toggles should be re-run on the merged build (one display at a time). This includes persistence after restart and completing Extrude by keyboard only. T-tutorial waits for the tutorial. |
