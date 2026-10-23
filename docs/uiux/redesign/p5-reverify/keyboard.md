# P5 re-verify: T-keyboard on the merged build (keyboard only)

Tester: Sonnet verifier. Build: main c3fbdc3 (`build/bin/fabricad`, no newer sources). Private Xwayland :93 (stopped afterwards), xdotool
key/type only, no mouse events at all. QT_SCALE_FACTOR 0.6; the shared compositor tiled the display to 1900x490, so the own window
was set to 1900x490 (own window only). Dark first, then Light via Preferences. Screenshots in `keyboard/` (kept: key steps and contact
sheets, numbering = run order).

Harness notes (not app bugs): no window manager on :93, so X input focus was set with `xdotool windowfocus` on the main window or on a
dialog window (Preferences, recovery dialog) as a WM would. Screenshots lagged behind the app (see P5R-KBD-2); after a 1 px resize of the
own window ("nudge") the screen shows the true state, so most grabs were taken after a nudge.

## Result: PASS with issues (extrude can be completed keyboard-only; no blocker left)

| Area | Result |
|---|---|
| Open command search (S), run BOX, type height, Enter | pass (05) |
| Create sketch by keyboard (command line `:`, `SKETCH XY`, `REC`, `-15,-15`, `30,30`, Ctrl+Return) | pass |
| Extrude: `E`, card Tab walk (11 stops), Space-to-pick Sketch1 in FEATURES, type 15, Enter applies | pass, Extrude1 15 mm (36-41) |
| FEATURES list: Up/Down, Space suppress/unsuppress, F2 rename ("Base") | pass (42-46); during a command the arrows only move the cursor and Space picks (pass, but see P5R-KBD-2) |
| Dial: Tab to it, ring, Left/Right/Up/Down orbit, Home resets to ISO | pass (51), see P5R-KBD-6 |
| Preferences: open via S "prefer", Tab order, tab bar with Left/Right, theme radio with Right applies Light live, Layout combos (Down changes sidebar side live) | pass (53d-61c) |
| Toolbar, captions, title bar, document tab, `+`, search, save, help, view-tool column: Tab stops with rings | pass in dark and light (73-75, 11, 63) |
| Persistence after restart (killed app, restarted the same display) | theme=light and sidebarSide=right persisted (70); the autosave recovery dialog was answered by keyboard (Shift+Tab, Enter) and restored the document with "Base" and Extrude1 15 mm (72) |

## Tab order observed
* Idle: viewport start, FEATURES header, search, filter, rows (one stop, arrows move), MODEL header, eye, MODEL tree, SELECTION header,
  command line, dial, 6 view-tool column buttons, toolbar tools and captions left to right (SKETCH ... TOOLS, PARAMS, SETTINGS), title bar
  (logo, PART switcher, document tab, `+`, search box, save, search, help; disabled undo/redo skipped), then FEATURES again.
  ISO/TOP/FRONT/RIGHT are not stops (known open item, Ctrl+1..7).
* Extrude card (dark and light, 10-11, 63): PROFILES field, its clear button, START OFFSET, EXTENT, DISTANCE, TAPER, FLIP, OBJECTS field and
  clear, DIRECTION segments, OPERATION segments, then out of the card to the title bar. Tab on the canvas goes into the card.
* Preferences: tab bar, then the page's controls, CLOSE, back to the tab bar.

## Focus rings
Dark and light: visible on card fields (gold for numeric fields, purple frame for others), toolbar tools and captions, title bar widgets,
panel headers, FEATURES/MODEL rows, dial (hexagon outline), view-tool column, combos, Preferences tab bar and CLOSE (dark). Weak ones are
listed in issues 3, 4 and 8.

## Issues

**P5R-KBD-1, medium: first key after a command ends is ignored.**
Repro: BOX via S, Enter (box created); press `E` once. Also after Esc cancelling the Extrude card, and after Ctrl+Return finishing a sketch.
Expected: EXTRUDE card opens. Actual: nothing, even after 2 s and a repaint; a second `E` opens it (06-08, 24-25 in the log; kept 25-e2).
After closing Preferences with Esc the first `E` worked. Suspect the viewport loses key focus when the command ends.

**P5R-KBD-2, medium: stale paint after keyboard input (rings and values).**
Repro: Extrude active, Tab into FEATURES (Origin row ring), press Down/Up. Expected: ring follows the cursor. Actual: ring stays painted on the
old row; after Down x3 and Space two rings were painted (36-space); the true state (ring on Sketch1) shows only after a window resize (38-nudged).
Same with the value box: typing "15" left DISTANCE at "10 mm" for more than 3 s (40-typed15) until a resize (40b-nudged). The first grabs after
box creation also show ghost text in the status bar (05-box-20). Only seen on the private Xwayland display; not tested on a real X11/Wayland
session, so it may be the harness, but it matches the old P5-KEYBOARD-1 symptom (old ring not invalidated).

**P5R-KBD-3, medium: focus on icon buttons is nearly invisible.**
Repro: Tab to FEATURES search, FEATURES filter, MODEL eye, in dark (28-t series, not kept) and light (75-light-tab-sheet, tiles 3, 4, 7).
Expected: 2 px `accent.focus` ring, 1 px offset. Actual: a faint tinted square with a hairline border, low contrast in both themes.

**P5R-KBD-4, medium: Light theme, Preferences CLOSE has no visible focus ring.**
Repro: theme Light, Preferences > Appearance, Tab to CLOSE (59-light-app-tab-sheet tile 4). Expected: ring. Actual: purple button with a purple
ring, indistinguishable. In dark a pale ring shows (54-prefs-tab-sheet tile 5).

**P5R-KBD-5, low: card CANCEL is not a Tab stop, Shift+Tab traps inside the card.**
Repro: open Extrude card, Tab through (APPLY is disabled, so the card ends at OPERATION). Expected: CANCEL reachable. Actual: Tab goes to the
title bar, CANCEL is never focused (Esc works). Shift+Tab from the first card field wraps to the last card field instead of leaving the card
(forward Tab leaves it).

**P5R-KBD-6, low: dial azimuth readout at the pole.**
Repro: view TOP (AZ 0, EL 90), focus dial, Right twice. Expected: AZ 015, 030 (15 degree steps, plan 5.x). Actual: the dial graphic turns but the
readout stays "AZ 000 EL 90"; Down gives EL 75 and Left then AZ 345 (readout inconsistent with the first two Rights). Home gives ISO 315/35 correctly.

**P5R-KBD-7, low: sketch plane cannot be picked from FEATURES by keyboard.**
Repro: SKETCH (command waits for a plane), Tab to the Origin row, Space, Right, Enter. Expected: a way to pick XY/XZ/YZ (the chips are
mouse-only). Actual: Space does nothing, Enter cancels the command. Workaround that passes: `:` then `SKETCH XY`.

**P5R-KBD-8, low: weak rings on checkboxes, radios, the recovery dialog and the light PROFILES field.**
Preferences checkboxes and radios show only a tinted label (dark 56-app-tab-sheet, light 59-light-app-tab-sheet). The recovery dialog shows
focus only as a filled button (71-dlg: YES filled at start, NO filled after Tab, YES turns white) so default and focus are ambiguous. In
Light the PROFILES select ring in the card is a pale purple on lavender (63-light-card-sheet tile 1).

## Not covered
Exact 1920x1080 or 1280x800 layouts (display tiled to 1900x490; no offscreen grab was needed for this role), Ctrl+K, the rollback bar keyboard
move, undo/redo by keyboard, the strip timeline, drawing tabs, viewCube orientation widget.
