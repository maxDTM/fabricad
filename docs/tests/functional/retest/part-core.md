# Retest - part-core (build HEAD a110a4f, Release)

Tester: mouse/keyboard on private display :32 (setup shortcut: the in-app command line was used to draw the first sketch rectangle; all repro steps used GUI). Screenshots in `part-core/`.
Harness note: the Xwayland window was tiled at 466x490; I floated/resized my own :32 window via hyprctl to 1500x950 (harness only).

## Verdicts
| Issue | Verdict | Evidence |
|---|---|---|
| PART-01 (PART-CORE-1) rect pattern qty 100000 | FIXED | Typing 100000 clamps to 1000 in the spinbox (45-qty-tab.png); UI stayed responsive, RSS ~250 MB; OK with 1000 bodies done in <5 s (46-qty1000-ok.png). Overlapping bodies render black (z-fighting, expected with 20 mm spacing on a 40 mm body). |
| PART-02 (PART-CORE-2) shell thickness too large | FIXED | 50 mm on 20 mm box: red "thickness too large for the geometry", OK disabled (11-shell-50.png). 2 mm shell still works (12-shell-2ok.png). |
| PART-03 (PART-CORE-3) two-distance chamfer corrupt | FIXED (partially verifiable) | My geometry differs from original (box 20 high, single-edge fillet 5). Chamfer 3/8 now rejected: "chamfer produced an invalid shape (distance too large for the geometry?)", OK disabled (55-ch-3-8.png). 3/2 succeeds and yields a closed solid (57-ch-applied.png). Original exact model not rebuilt; see NEW-PART-CORE-2. |
| PART-04 (PART-CORE-4) cut hitting nothing / default direction | FIXED | Cut on top-face sketch now defaults to Flip checked, arrow pointing into the body (33-cut-default.png). Un-flipping (outward) shows "the cut does not remove material from any body", OK disabled (34, 35). |
| DOC-08 (PART-CORE-7) failed feature message | FIXED | After deleting Extrude1, status bar shows red "Shell1 failed: select faces to remove or bodies to hollow" (23-after-delete.png). |
| DOC-09 (PART-CORE-6) delete with dependents warns | FIXED | Timeline right-click > Delete on Extrude1 shows "Delete Extrude1 (1 dependent feature may fail)? These later features use it and may fail: Shell1" Yes/No (22-delete-dep.png). Deleting a feature with no dependents (Hole1) goes straight through, undo restores (63, 64). |
| DOC-10 (PART-CORE-5) timeline menu/tooltip unreadable | FIXED | Context menu and "Extrude1" tooltip are light background with dark text, readable (21-ctxmenu.png, 20-tooltip.png). |

Counts: 7 issues retested, 7 fixed, 0 partial, 0 not fixed, 0 regressed.

## Regression sweep (round1 "working" list), quick
Re-checked OK: Extrude New Body/Join-default/Cut, Fillet (single edge, face selection), Chamfer equal/two distances + invalid message, Shell 2 mm, Hole simple (create, delete, undo), Rectangular pattern bodies (3 and 1000), timeline context menu (Edit/Roll/Suppress/Rename/Find/Delete), undo/redo, viewcube home. No crash. Not re-run (time): Revolve, Circular pattern, Mirror, Combine, Press Pull, Suppress toggle, roll marker.

## NEW defects
- NEW-PART-CORE-1 (low): the red status-bar message "Shell1 failed: ..." stays after Ctrl+Z restores a valid model (24-undo.png). Message should clear when the failure disappears.
- NEW-PART-CORE-2 (low, needs confirmation): box with a 5 mm fillet on one top edge, then Chamfer Two Distances 3/2 on the adjacent top edge: the result also gets a blended strip running down the whole vertical corner edge to the bottom (zoom of 57-ch-applied.png). Solid is valid and closed, but the chamfer propagates beyond the selected edge. The preview (56-ch-3-2-ok.png) also shows a triangular shading artefact on the top face.
- NEW-PART-CORE-3 (low, usability): Create Sketch tool picked the XZ origin plane/front face instead of the top face when clicking the visible top face in iso view; selecting the face first and then pressing Create Sketch works (30-sketch-top.png). Not verified whether original behaviour.
