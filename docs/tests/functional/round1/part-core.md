# Round 1 - part-core (solid features) functional test

Tester: QA via mouse/keyboard only on private display :32. Screenshots in `part-core/`.

## Issues
- PART-CORE-1 (high, reliability): Rectangular pattern quantity typed as 100000 hangs the UI and RAM climbs about 160 MB/s (3.5 GB, 99% CPU, app killed after ~60 s). Each typed digit recomputes the preview synchronously. Repro: box, Rectangular Pattern, Bodies, edge dir, click Quantity, ctrl+a, type 100000, Tab. Expected: clamp / warn at sane max (Fusion caps), stay responsive. Shots: 81, 82, 84-87. Qty 100 works but takes about 3 s with ~100 bodies in the browser.
- PART-CORE-2 (medium, functional): Shell with thickness larger than the body (50 mm on 20 mm box, top face removed) shows no error, OK enabled, creates a Shell1 feature that leaves the body unchanged (closed top). Expected error "thickness too large". Shot: shell-too-thick.png (163). Contrast: Press Pull says "offset removes the whole body" (170).
- PART-CORE-3 (medium, functional): Chamfer with distance 3 and distance 2 = 8 on a filleted edge succeeded but produced corrupted geometry: body faces missing or inverted, open shell, see-through body with hole artefacts. Repro: box 30 high, fillet top edge 5, hole, chamfer Two Distances 3/8 on the unfilleted top edge, OK. Expected valid result or "failed" message. Shot: chamfer-corrupt.png (129). Ctrl+Z restored.
- PART-CORE-4 (medium, functional): Cut extrude that hits nothing (circle on top face, Cut, +10 mm away from body) is accepted silently: feature added, body unchanged, no warning. Fusion refuses ("no body to cut/ no intersection"). Also on a face sketch the default direction for Cut points away from the body, requiring Flip. Shot: 158.
- PART-CORE-5 (low, visual): Timeline context menu (right-click on a feature) and timeline tooltips render with a black background and dark-grey text (nearly unreadable). Combo popups and toolbar menus are fine. Possibly an Xwayland artefact for popup windows, but combo popups are fine. Shots: ctxmenu-black.png, 142.
- PART-CORE-6 (low, functional): Deleting a feature that others depend on (Extrude1 with Fillet/Chamfer/Hole after it, or Hole under RectangularPattern) deletes without confirmation; dependents turn red with only a hover tooltip. Fusion warns. Undo restores correctly. Shots: 141, 152. Tooltip also stayed visible after Ctrl+Z.
- PART-CORE-7 (low, functional): Editing an early feature so a later one fails (Extrude 30 -> 3 with Fillet 5 after it): Fillet turns red, later Hole still computes, fixing the value (3 -> 30) recovers everything. Works; but no error message in status bar/dialog (only red icon). Shots: 146, 147.
- PART-CORE-8 (low, usability): Fillet radius 12 on all top edges of a 20x15 mm face (selecting face) previews and succeeds with partially intersecting rounds, no warning; large radius on a single edge of a thin plate (500) gives proper "fillet failed (radius too large)" message with OK disabled. Inconsistent but acceptable.

## Tested and working
- Extrude: New Body, Join (via sketch on face), Cut (+All extent, Flip), Intersect, One Side, Two Sides (25/10 with taper), Symmetric, Distance 0 (message), negative distance (flips), expression "2+3", taper 20 deg, All extent (cut through, verified from below), Objects to Cut/Join present.
- Revolve: Full about X axis, Angle 90/270, 0 deg (message), 400 deg accepted, -90 accepted, axis not on profile edge ("revolve failed" message).
- Fillet: single edge, face selection, radius 1/5, huge radius message, undo; Chamfer equal/two distances, too large (message).
- Shell: remove face 2 mm, body-only, thickness too big (see 2).
- Hole: simple/counterbore, dimension edits, depth.
- Rectangular pattern: features (holes) 3x, bodies 3x, quantity 1 ("pattern has no instances"), -5 (message), 100.
- Circular pattern: bodies about Z axis, count 6.
- Mirror: body Join across YZ plane.
- Combine: Join (6 bodies), Cut.
- Press Pull: offset face with message for removing whole body.
- Timeline: roll marker, insert feature at marker, roll to end, suppress/unsuppress, edit feature by double-click, delete + undo.
- No crash occurred.

## Not tested
Extrude To Object, Thin, Intersect result check, Combine Intersect/Keep tools, Mirror features, Circular angle types, Pattern Symmetric/Direction 2.
