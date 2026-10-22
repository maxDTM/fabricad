# Phase 1 shell: GUI check (mouse and keyboard only)

Binary: `build/bin/fabricad` of main at fdbcb40 (phase 1 shell plus flcore merged). Display :64 (Xwayland, 1920x1080),
driven with xdotool only. Dark and light, window 1920x1080 and 1280x800, fresh config. Screenshots: `p1-check/`.

## Verdict
The shell works in both themes and both sizes. No crash, no clipped label, no cut-off toolbar section.
Sketch, extrude, fillet, save and open, the three workspaces (Alt+1/2/3), the theme switch (live and
after restart) and GROUP / RENAME / SUPPRESS / UNSUPPRESS / CHECK on the cmd> line all work.
Open issues are small, except the light ViewCube.

## Checked and fine
| Area | Result | Shots |
|---|---|---|
| Title bar | Logo menu (File menu), workspace selector with keycap, angled tabs with accent line and unsaved dot, `+`, search box, undo, redo, save, help. Tab and browser names are shown as typed (`Bracket_Äö v1`). | 01, 02, 26, 39 |
| Workspace selector | Click opens PART / ASSEMBLY / DRAWING with ALT+1/2/3 and a check mark; Alt+1/2/3 switch (Assembly puts ASSEMBLE first and collapses SKETCH, CREATE, MODIFY; Drawing opens a drawing tab with its own toolbar). | 03, 24, 46, 49 |
| Toolbar sections | SKETCH, CREATE, MODIFY, ASSEMBLE, CONSTRUCT, INSPECT, MORE, DOCUMENT with straight dividers. CREATE and MORE dropdowns list everything, including MORE > INSERT / SELECT / TOOLS groups; disabled items are greyed. | 04, 05, 36, 48 |
| Contextual sketch page | CREATE, MODIFY, CONSTRAINTS (small icon grid), INSPECT, MORE (OPTIONS, TOOLS), FINISH SKETCH. At 1280 PROJECT and DIMENSION fold into the CREATE caption. Nothing clipped. Ctrl+Enter finishes. | 07, 08, 40, 43 |
| Status bar | Active command cell (gold square and name), prompt in caps, key hints, message as typed, command line with HISTORY, `SKETCH1: 0 DOF`, OCCT, unit (lowercase), GRID and SNAP cells, rebuild meter with ms. At 1280 OCCT and the hints drop as documented, nothing overlaps. | 06, 11, 13, 47 |
| Command line | Typed `list features`, `group ...`, `rename ...`, `suppress ...`, `check groups == 1`: all run and the message appears in the status bar. Quoted names with spaces and umlauts are shown unchanged. | 15 to 23 |
| Flow | Sketch (rectangle), finish, extrude 12, fillet one edge, save to a path with spaces and umlauts, reopen dialog completer shows the file. Timeline strip gets one icon per feature. | 07 to 14, 25 to 27 |
| GROUP / SUPPRESS | `group Base Sketch1 Extrude1` ok; `rename group:Base "Base Plate_v2 äöü"` ok; `suppress Fillet1` removes the fillet and shows the struck icon in the strip; `suppress "group:..."` then `unsuppress` round-trip; `suppress Nope` gives a clear error with the selector hint. | 16 to 23 |
| Preferences | Tabs General, Document, Appearance, Layout. Theme radio switches live (no flicker, all regions follow). `ui/theme=light` is in the config and the next start is light. Layout combos change and are saved (`sidebarSide=right`). | 28 to 34, 38, 44, 45 |
| File dialog | Dark styled, path and file names as typed, completer, no clipping at 1920. | 25, 27 |

## Issues
Severity: M = should fix before the shell is called done, L = polish, I = information.

| # | Sev | Issue | Shots |
|---|---|---|---|
| 1 | M | **Light theme: the ViewCube is white on white.** Faces are nearly invisible and the labels (TOP, FRONT, RIGHT) are faint. The sketch-mode TOP face and its four corner marks vanish too. The cube is drawn with fixed colours, not tokens. | 33, 38, 40, 41 |
| 2 | M | **Multi-line command output is cut to the first line.** `list groups` shows only `Groups (1):` in the status message cell; `list features` shows nothing readable (the message was already gone after 3 s). There is no log or history view in the shell to read the rest, so LIST / INFO / CHECK fail output cannot be read in the GUI. Needs a history popup from the HISTORY control (it opens only a recall list) or a message line that expands. | 15, 18 |
| 3 | L | Suppressing a group whose features a later feature uses shows only the downstream error (`Fillet1 failed: select edges to fillet`). The result of the SUPPRESS itself is lost. | 20 |
| 4 | L | Status prompt does not follow the command state: Extrude and Fillet still say `SELECT PROFILES` / `SELECT EDGES` after a profile or edge is picked (the card shows `1 SELECTED`). | 11, 47 |
| 5 | L | Search popup is not aligned to the search box: at 1280 it spans x 960 to 1340, past the right edge of the window. On a 1280 px screen it is cut off. | 50 |
| 6 | L | Collapsed caption buttons (SKETCH, CREATE, MODIFY in the Assembly filter) sit about 12 px higher than the other section captions (y 86 against 98). | 46 |
| 7 | L | Sketch MORE dropdown: toggle items (SNAP, SKETCH GRID, SHOW PROFILE ...) show no check state, though the palette shows the same toggles as check boxes. | 08 |
| 8 | L | Dark theme: the on-canvas extrude DISTANCE field is a light box, and the selected profile fill is near white. They look like light-theme leftovers. | 11 |
| 9 | L | Light theme: the Preferences dialog has no border or shadow, so its edge dissolves into the light viewport. The dark dialog is frameless too, but contrasts. | 33, 44 |
| 10 | L | File dialog: the type column shows `Plain t…ocument` for `.fcad` (system MIME text, elided). | 27 |
| 11 | I | Prefs > Layout: SIDEBAR, COMMAND CARD, QUICK TOOLBAR, ORIENTATION and TIMELINE are saved but not applied yet (the page says so). The strip is shown although TIMELINE says FEATURE LIST. Expected until phase 2. | 30, 32 |
| 12 | I | The GRID cell value changes without user input (10, 5, 2, 1 mm) as the view or window changes. It is probably the adaptive grid, but the label reads like a setting. | 02, 14, 41 |
| 13 | I | The window repaints 2 to 3 s after an action (software GL under Xwayland). Not judged. The screenshots wait for it. |  |

## Against the mockups (shell regions only)
Compared with `docs/mockups/custom-ui-extrude-v6-tokens-{dark,light}.png`:
- Title bar, tab strip, toolbar section labels with carets, DOCUMENT section and status bar follow the mockup layout and tokens in both themes.
- Differences: the mockup keycaps read `CTRL 1` and `CTRL K`; ours read `ALT 1` (tentative decision) and `S`. The mockup active tool has a gold outline, ours a purple one. The mockup has carets on single tools (SKETCH ▾, PLANE ▾); ours only on section captions. The mockup status bar has a selection cell ("1 PROFILE"); none was visible here because the selection was empty in all shots.
- Not shell, so not compared: feature panel, selection panel, command card, ViewCube style, timeline.

## Notes for the harness
- `tools/uitest/ui.sh start` resizes `xdotool search --name FabriCAD | head -1`, which is not the Qt window (it is a 1x1 or helper window); the app stays at its own 1400x860. Use the last match.
- A rootful Xwayland under Hyprland is tiled to 945x490 and falls back to that whenever the client restarts, so 1920x1080 needs a float and exact resize of the Xwayland window each time.
- Other agents' `pkill` patterns killed `fabricad-ui-64` twice (exit 143). Running the binary through a symlink with another name avoided it.

## Fix status (branch ui/p1fix)
| # | Status | Change |
|---|---|---|
| 1 | Fixed in phase 4 | ViewCube from tokens (`vp.cube.face`, darker `vp.cube.edge` edges/corners); the hex dial is the default. |
| 2 | Fixed | The HISTORY pane is now an overlay of the window above the status bar (header band, CLEAR, close, Esc/F2), with every line of command output. Output of several lines opens it by itself when it answers a command typed in the box; the message cell shows the first line plus `... +N lines (history)` and opens the pane on click. |
| 3 | Fixed | A feature failure caused by the command just run is shown beside its result: `Suppress Base · Extrude1 failed: ...`. |
| 4 | Fixed | Feature commands update the prompt after each pick: `Profiles: 1 selected · Enter to finish`. |
| 5 | Fixed | The search popup opens under the search box and stays inside the window and the screen. |
| 6 | Fixed | A collapsed section shows an icon button in the button row and keeps its caption on the shared caption line. |
| 7 | Fixed | Toggle tools carry their state (`ToolInfo::checked`); toolbar dropdowns show check marks and refresh when they open. |
| 8 | Fixed in phase 4 | Value box styled by the token QSS (dark field); selected faces/profiles get the `sel.fill` tint instead of an 85 % lavender wash. |
| 9 | Fixed | Preferences, Parameters, message and input dialogs have a 1 px `border.control` edge. |
| 10 | Fixed | `.fcad` shows as `FabriCAD design` in Qt's file dialog (icon provider type). |
| 12 | Fixed | The GRID cell tooltip says the spacing follows the zoom. |
