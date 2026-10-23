# P5 re-verify: layout toggles (10.2 #2), PASS with notes

Verifier: Sonnet. Build `build/bin/fabricad` (22:02, merged main c3fbdc3; no src change since). Private display :93, stopped afterwards.

## Method and limits
- Matrix (18 rows, the pairwise set of the first pass, covers every value pair of toolbar x sidebar x card x quick x orientation x theme x timeline): offscreen grabs at 1600x1000 with the Extrude script (card live) plus FABRICAD_GEOMETRY_DUMP, checked by script: every widget box inside the window, and no overlap between card, dial, quick toolbar, view-tool column, breadcrumb, selection filter, scale bar, coordinate readout, toolbar host and dock (card inside the dock is expected). Screenshots in `toggles/rNN-*.png` (file name = toolbar-sidebar-card-quick-orient-theme-timeline). The 3D view (and so the AIS ViewCube) is blank offscreen.
- GUI half (mouse and keyboard only) on :93: Hyprland tiled the Xwayland window to 1900x490, then 945x490 (another tester's Xwayland :67 is on the same workspace; P5-TOGGLES-3 still applies). I resized only my own window to the display. So the GUI half ran at 945x490, not at a normal size. Toggles applied by mouse in Preferences > Layout (combo click, Down, Return): toolbar left, sidebar right, card in the sidebar, quick toolbar right, view cube, bottom strip.

## Results
| Check | Result |
|---|---|
| 18 combinations: nothing off-screen (all widget boxes inside 1600x1000) | PASS, 18/18 |
| 18 combinations: no overlap between overlay widgets / toolbar / dock | PASS, 18/18 (card/dock overlap only where the card is docked, by design) |
| Right sidebar + card in sidebar (old P5-TOGGLES-1: r02, r04) | PASS: dock 320 wide, card 320x472, inside the window |
| Card in sidebar + feature list (r02, r15, r18): FEATURES keeps header and rows | PASS, card 473 px tall, no clipping at 1600x1000 |
| Strip mode hides FEATURES, strip shown at bottom | PASS (r03, r04, r07-r12, r17; also GUI) |
| Both themes, both orientations render | PASS (dial; ViewCube checked live in the GUI) |
| Each Layout combo applies right away through Preferences with the mouse | PASS (`toggles/gui-prefs-layout-applied-live.png`) |
| Persistence after restart (toolbar left, sidebar right, strip, view cube read back from the config and applied) | PASS (`gui-after-restart-persisted-945x490.png`; config file lists all six keys) |
| Extrude end to end in the GUI with card in the sidebar: pick profile with the mouse, type 25 + Enter | PASS: Body1 (ACTIVE, SOLID) in MODEL, extrude node in the strip (`gui-extrude-*.png`) |

## Issues
### P5-TOGGLES-R1 (minor, low priority) Docked card overlaps the MODEL header at very small heights
- Repro: ui/cardPlacement=sidebar, sidebar right, window 945x490 (Hyprland-tiled size), strip timeline, press EXTRUDE.
- Expected: MODEL and SELECTION panels sit below the card slot, or the slot scrolls.
- Actual: the card slot (about 255 px) is taller than the room above the panels; the MODEL header is cut by the card's bottom edge and its first row ("Untitled") is drawn at y=287 under it (`gui-extrude-card-in-sidebar-945x490.png`). Not seen at 1600x1000.

### P5-TOGGLES-R2 (info) GUI half not run at a normal size
- Display :93 was tiled to 1900x490 and 945x490 by the compositor; no Xvfb/Xephyr. The GUI checks above are valid for the behaviour (apply, persist, Extrude) but not for exact layout, which comes from the offscreen grabs. Also, the quick toolbar at 945x490 was mostly out of view (the window is shorter than the toolbar); not judged.

### Observations (not defects)
- Window and chrome were not touched on the real desktop; `ui.sh start` printed an X BadWindow error when the compositor replaced the window (harmless; the app kept running).
- The 3D view and ViewCube overlap against the quick toolbar and dial is only checkable in the GUI; at 945x490 the cube and the nav column sit side by side without overlap.
