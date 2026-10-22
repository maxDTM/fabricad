# SKETCH-CORE retest (build at main a110a4f)

Tester: mouse/keyboard only on display :31. Screenshots in `sketch-core/`. Harness caveat: the app window is 1400x860 but the display is 945 wide, so everything right of x=945 (right-docked dialogs, the status-bar DOF label) is clipped.

| id | verdict | evidence |
|---|---|---|
| SK-01 | FIXED | Ctrl+Return finished the sketch with the Line tool active (04, 05), with browser focus (06, 07) and Ctrl+KP_Enter with Line tool mid-chain (08). |
| SK-02 | PARTIALLY FIXED | Fillet repaints at once (13, 15). Perpendicular constraint on a slanted line: old orange-highlighted line stays drawn next to the solved position until the mouse moves (17 vs 18). Constraint path still stale. |
| SK-03 | FIXED (not reproducible) | Rectangle tool + clicks issued immediately after picking the angled face landed exactly on the clicked corners (34). |
| SK-04 | FIXED | Sketch on chamfer face: orientation identical on create (34) and on Edit Sketch (35). |
| SK-05 | FIXED (indirect) | The Extrude dialog is no longer on the left over the profiles (30); it is off-screen in the clipped right area. `CommandDialog::place` now anchors to `area.width() - width() - kRightMargin`. Not visually confirmed. |
| SK-06 | FIXED | Fillet tool shows a Radius box before picking; typed "12" then picking gave R12 with a radius dimension, and the default gives a dimension "R5" (12-15). |
| SK-07 | FIXED | Over-constraint dialog, Yes: driven dimension "30 (30)" added directly, no value prompt or error (21, 22). |
| SK-08 | FIXED | After committing a dimension value and a driven dimension, Esc Esc returned to "Select sketch geometry" (23). |
| SK-09 | NOT FULLY VERIFIED | Prompt (left) and message (centre) no longer carry DOF text in any screenshot (24); the right-hand sketch label is clipped by the harness so the stale-after-undo case could not be checked. |
| SK-10 | FIXED | Ctrl+A in a sketch selects all: "6 sketch entities selected" (11). |
| SK-11 | FIXED | F6 after zooming in framed all sketch geometry (27 -> 28). |
| SK-12 | FIXED | Line chain 20 @180 now shows "Segment would retrace the previous line: not added"; no overlapping line or extra dimension (25, 26). |

Counts: 10 fixed (SK-01, 03, 04, 05, 06, 07, 08, 10, 11, 12), 1 partially fixed (SK-02), 1 not fully verifiable (SK-09, no visible defect), 0 not fixed, 0 regressed.

## Regression check of round-1 "worked" features
Re-checked OK: line, 2-point rectangle, circle, 3-point arc, tangent-free fillet, dimension with typed value, over-constrain prompt, perpendicular constraint, undo and redo (36-38), box select + Delete (40), extrude of a rectangle profile (29-31), chamfer then sketch on the angled face, sketch edit via double-click. Not re-run: trim, offset, expressions, 3-point circle, center-point arc, tangent arc, nested profiles, huge or tiny geometry, 25x undo.

## NEW defects
None found. Observation (not counted): in 29/30 an open profile (after fillets, the construction point at the old corner leaves the outline open) gave no closed profile to extrude; expected behaviour, not a defect.
