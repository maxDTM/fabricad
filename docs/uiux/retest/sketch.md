# Retest - Sketch area (merged build, main after ui/G1-G4)

Tester: real X events (xdotool) + screenshots only, private display :32 (Xwayland re-tiled by Hyprland; floated and resized to 1600x1000 via hyprctl for my window only; app fills about 1400x860 of it - harness artefact).
Screenshots in `sketch/`. Round 1: `../round1/sketch.md`.

## Verdicts
| id | verdict | evidence |
|---|---|---|
| UX-01 viewport redraw | FIXED | Rect/offset/fillet/undo/eye/dimension commit all appear immediately with no extra input. Rubber-band preview follows the cursor (04, 22). Eye hides sketch at once (30). ViewCube click to Top: no torn frame (36). Not covered here: Move triad size, Move Component drag, Edit Feature body vanishing. |
| UX-02 picking | FIXED (sketch scope) | Plane hover highlight (02) and click starts sketch (03); edge picks for Dimension/Fillet/Offset; Extrude profile hover (32) and click -> "1 selected", OK enabled, preview + distance handle (33). Not covered: body/face/vertex pick, window select, joint pick. |
| UX-04 ViewCube drag | FIXED | LMB drag on cube orbits (34), profile selection kept, no rubber band. |
| UX-10 origin planes | MOSTLY FIXED | Planes appear immediately in Create Sketch, labelled XZ/XY (01), hover highlights blue (02), click starts the sketch. With a model present planes are smaller and sized near the model (45). Not checked: Origin eye toggling shows all three; YZ label in first view was off-screen. |
| UX-13 light-on-light | FIXED | Palette, CREATE menu (43), Extrude dialog (33), Sketch/Edit Dimension field (07, 38), command box, checkboxes (unchecked boxes are light now) all legible. Not checked: file dialog, crash-recovery dialog. |
| UX-14 tabs | FIXED | Document tab light; SOLID tab fully visible in SKETCH mode (05); no black scroll arrows. |
| UX-18 sketch palette | FIXED | Header drag moves the palette (46); it has a collapse chevron. Default position still overlaps the right edge of geometry and the ViewCube lower edge (see NEW-SKETCH-3). |
| UX-22 fillet destroys | FIXED | Fillet on fully constrained, dimensioned 60x30 rectangle: arc inserted, geometry kept, DOF 0 -> 1, no over-constraint (10, 11). Triangle line chain fillet also fine (42). Regular polygon not retested. |
| UX-23 hidden alignment | FIXED | Second rectangle started on the x of the first (inference line shown, 20): total DOF = 1 + 4 (no hidden constraint). Editing its dimension to 90 left rectangle A unchanged (26). |
| UX-24 offset invisible | FIXED | Offset of filleted rectangle draws the loop (17 preview, 18 result; DOF 1 -> 12). Minor: no offset dimension/constraint is created, and the right edge sits under the palette. Slot not retested. |
| UX-25 dimension editor | FIXED | Editor opens right after placing a dimension, text pre-selected (07); typing 60 + Enter commits (08). Double-click on a dimension opens Edit Dimension focused with selected text; "50" + Enter applies (38, 39). |
| UX-26 profiles | MOSTLY FIXED | Construction circle crossing a rectangle no longer splits or replaces the profile (28, 37); Show Profile is on by default; fill is pale cream-orange. Pentagon-vs-construction-circle case not retested. |
| UX-27 stale inference line | FIXED (line) / NOT RETESTED (polygon) | Closing a 3-line chain leaves no dashed line (41). Polygon fillet keeping regular constraints not retested. |
| UX-28 glyphs | FIXED | H/V/= are small box icons (05), Fix shows a green lock icon (40), dimension text is offset from the dimension line, not struck through (08). |
| UX-29 origin coincident | FIXED | Rectangle corner on origin: 2 DOF after drawing (05) instead of 4. Line from origin: 3 DOF with rect at 1 DOF (13), i.e. start point is coincident. |
| UX-30 Ctrl+Z after Offset | FIXED | Ctrl+Z removes the offset, stays in sketch mode, grid and palette remain (19). Ctrl+Z after Line also fine (14). |
| UX-38 double-click rename | FIXED | Double-click Sketch1 in browser enters Edit Sketch with no inline rename box (31). |

Counts: 15 issues retested: 13 fixed, 2 mostly fixed (UX-10, UX-26), 0 not fixed, 0 regressed. Sub-items not exercised are listed in the table.

## New defects noticed
- NEW-SKETCH-1 (medium): Value input boxes can sit under the cursor and swallow the click that should place the point. Rectangle tool, 2nd click at about (1000,640): the "Height" box was drawn over that spot, the click hit the box, then Escape discarded the rectangle (two attempts, 22/23). Place boxes away from the cursor or let the click through to the viewport.
- NEW-SKETCH-2 (low): ViewCube still low contrast in Top/Front views - a flat white face with only a faint "TOP"/"FRONT" label on a near-white background (36). Hover/edge zones are still small.
- NEW-SKETCH-3 (low): Sketch palette default position overlaps drawn geometry on the right and the lower part of the ViewCube (03, 18); a rectangle offset to the right ends under it.
- NEW-SKETCH-4 (low): Extrude distance value box does not follow the 3D handle after the camera is orbited via the ViewCube (34) - it stays at its old screen position (known cross-group item "value boxes stay on screen").
- NEW-SKETCH-5 (low): After the Offset command the new loop has no offset constraint/dimension (Fusion adds one), so DOF jumps to 12 and the offset distance cannot be edited later.
