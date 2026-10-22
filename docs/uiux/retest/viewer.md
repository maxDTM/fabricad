# Retest: 3D viewer, navigation and selection (merged build 1530bc7)

Tester: mouse and keyboard only (xdotool) on private display :31, 1400x900 floated Xwayland window.
Screenshots: docs/uiux/retest/viewer/. Screen recording of the session: viewer/retest-recording.mp4
(note: the recording covers this retest session, not the chair test; the chair test was not run by this retester).
Model used: Box 40x30x20 via the Box dialog, then Fillet, sketch, Move/Copy.

## Verdicts

| Issue | Verdict | Evidence |
|---|---|---|
| UX-01 redraw after scene changes | FIXED | Box OK shows body at once (e1), eye toggle hides/shows at once (n1/n2), Origin eye on/off (o1, p1), Fillet OK (u1), sketch finish (w1), undo/redo (x1-x3), rubber band live (g1), Move drag shows live preview (r1). No torn frame seen after cube animation (j0/j1). Move triad is full-size (q2). |
| UX-02 picking / preselect / window select | FIXED | Hover face highlight (f1), click selects "Face of Body1" (f2), window select gives "26 items" (g2), Fillet edge pick with preview (t1), Move body pick from viewport (q2), plane pick starts sketch (v1/v2). |
| UX-04 ViewCube drag orbits, keeps selection | FIXED | h2: drag orbits, selection stays (26 items). |
| UX-05 ViewCube look/hover | PARTIALLY FIXED | Soft blue hover highlight on faces (i1c). Cube is still white on a near-white background, only faintly outlined (h1c). Corner zone: click near a corner gave an edge view (w2). |
| UX-06 home / roll / step arrows | FIXED | Home icon and two roll arrows always shown, four step arrows in face-on views (j1); Home button returns to iso (k1). |
| UX-07 fit / cube zoom | FIXED (with caveat) | Home/Home key fit with margin, clear of nav bar (k1, x1). Cube click keeps margin (j1). Left margin ignores the docked dialog (known open item). |
| UX-08 orthographic after orbit | FIXED | After Shift+MMB orbit, box edges stay parallel and Orthographic stays checked (m1, l1). The grid at grazing angle looks fan-shaped, which I judge to be foreshortening only. |
| UX-09 hidden selected body ghost | FIXED | n1: hidden selected Body1 is not drawn. |
| UX-10 origin planes | PARTIALLY FIXED | Planes show during Box plane input and Create Sketch, are labelled, hover-highlight and pick (d1, v1, v2). Planes are still much larger than the part and run off the viewport; plane quads look doubled (o1, u1). |
| UX-13 light text on light / black widgets | FIXED (viewer scope) | Menus, dialogs, fields, checkboxes, combo, tabs all readable (c1, d1, l1, m0, s1, t1). Native file dialog not checked. |
| UX-14 document / ribbon tabs | FIXED | Light document tab, SOLID tab not clipped in sketch mode (v2). |
| UX-17 dialogs | PARTIALLY FIXED | Dialogs docked at left next to the browser, stable width, no cube occlusion (d1, q1, q2, t1). Joint dialog / Dimension hint not retested. |
| UX-15 Hole icon | FIXED | Distinct hole icon in CREATE menu (c1). |
| UX-16 nav menus | FIXED | Settings menu has a header, no blank row; Views shows "Home View" with shortcut "Home" (l2, m0). |
| UX-32 Move/Copy triad | FIXED | q2, r1, r2: triad with value box, viewport pick accepted. |
| UX-33 Box typed values | FIXED | d2: 40 and 30 typed, preview updated on Tab. |
| UX-39 SELECT menu | PARTIALLY FIXED | Filters and Component priority added (s1). No window/freeform/paint modes, no check mark on the active priority/filter (s2c). |
| UX-40 Origin child eye toggles | NOT FIXED | Origin children (axes, planes, point) have no eye (o1). |

Counts (17 issues): fixed 13 (UX-01, 02, 04, 06, 07 with caveat, 08, 09, 13, 14, 15, 16, 32, 33), partial 4 (UX-05, 10, 17, 39), not fixed 1 (UX-40), regressed 0.

## New defects
- NEW-VIEWER-1 (low) Sketch Palette panel sits over the lower part of the ViewCube in sketch mode (v2).
- NEW-VIEWER-2 (low) During a Move drag the original body keeps its selected-blue overlay at the old place beside the green preview (r1/r2); the original should dim or hide.
- NEW-VIEWER-3 (low) Origin planes (XZ/YZ) draw as overlapping doubled quads and XZ label sits under the Home button when planes exceed the view (o1, u1).
- NEW-VIEWER-4 (info) Harness: the Hyprland tiler resizes the Xwayland window when other windows open; I floated and resized only my window via hyprctl.
