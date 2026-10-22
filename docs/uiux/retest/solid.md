# Round 1 retest - Solid modelling (display :33, merged build 1530bc7)

Screenshots: docs/uiux/retest/solid/NN.png. Workflow: XY sketch, rectangle (mouse + typed), Extrude (profile picked in viewport), live preview, ViewCube drag, Edit Feature, rename, eye toggle, rollback, help, cmd>, log popup, Create menu, window select, Home view.
Harness note: the Xwayland window was tiled to 469x490 by Hyprland; I floated/resized it to 1400x881 (own window only). Moving it to a hidden workspace throttles repaints (stale frames) - an artefact, discarded; findings below were made on workspace 1.

| Issue | Verdict | Evidence |
|---|---|---|
| UX-01 redraw after scene change | FIXED | Eye toggle (34,35), Extrude OK (27), Edit Feature OK, rollback drag (36), animated Home (47) all repaint at once; no torn frame. Rectangle rubber band tracks the cursor (16). Move triad larger and visible (31,32). |
| UX-02 viewport picking | FIXED | Plane hover+pick (03,04), profile hover/pick (20,21), face pick with "Face of Body1" (28,29), edge preselect (44), window select works (45, "26 items"). Not exercised: Fillet/Hole/Joint pick. |
| UX-04 ViewCube drag | FIXED | Drag orbits and keeps profile selection (25). |
| UX-11 shading/selection | FIXED (mostly) | Selected body is opaque blue with outline (33,45); unselected faces are grey/off-white and distinct from background (27). Top face still fairly light in some orientations. |
| UX-12 serif 3D text | FIXED | Distance label is sans (25,26); cube labels still italic-looking, but sans. |
| UX-13 light-on-light/black widgets | FIXED | Dialog, sketch palette, checkboxes, Create menu, help menu, help table, cmd> all legible (04,20,38,41). Exception: timeline right-click menu has dim grey text on opaque black (30), probably the no-compositor black popup (SOLID-12) but text contrast is low - see NEW-SOLID-1. |
| UX-14 document/ribbon tabs | FIXED | Active doc tab distinct (white, blue top line); SOLID/SKETCH/TOOLS not clipped in sketch mode (04). |
| UX-19 help/log/Enter | FIXED | Help table aligned (38); log popup stays inside the window (40); first Enter in cmd> ran app.help. |
| UX-20 stale status name | FIXED | Status shows "Bracket" after rename (33). |
| UX-31 rectangle | FIXED | Second click commits (17); Width box is focused first and typing goes to Width (18c). |
| UX-34 Edit Feature preview | FIXED | Live green preview body follows distance 25 -> 40, manipulator follows tip (31,32). |
| UX-41 marking menu | FIXED | During Extrude: OK / Undo / Cancel / Fit / Look At, no Repeat Rectangle (23). Menu on black bg is the harness. |
| UX-42 consumed sketch | FIXED | Sketch1 eye turns off after Extrude (27). Origin planes are now shown when choosing a sketch plane (02). |

Counts: 13 issues retested: 13 fixed (UX-11 and UX-13 with small caveats), 0 partial, 0 not fixed, 0 regressed.

## NEW defects
- NEW-SOLID-1 (low) Timeline context menu (30): dim grey text on black background, hard to read (all items look disabled, though Edit Feature worked).
- NEW-SOLID-2 (low) Command log popup (40) stays open after Escape and while using other tools; it overlays the viewport/ViewCube area and window select start points.
- NEW-SOLID-3 (low) Sketch palette docks over the ViewCube (04): cube bottom is hidden in sketch mode.
- NEW-SOLID-4 (low) Entering a sketch does not orient the view to the plane (04, view stays isometric until the first sketch click; it then snaps to top view in 05).
- NEW-SOLID-5 (low) After rollback the status bar keeps the stale "Bracket" (36).
- NEW-SOLID-6 (low) Window select picks everything (26 items: faces, edges, vertices) with the selection filter at its default; Fusion selects by the filter.
- NEW-SOLID-7 (info) Hyprland tiles the rootful Xwayland to a tiny window (469x490); ui.sh start leaves the app tiny. Needs hyprctl float/resize (Lua syntax: hl.dsp.window.float/resize), not an app defect.

Not done: the chair test with screen recording (separate step).
