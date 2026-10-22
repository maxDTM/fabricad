# Advanced sketch - retest of round 1 (build d2278a1)

Retester: Claude, mouse/keyboard only on display :32, 1400x860 window, 2026-10-22 09:23-09:32. Screenshots: `advanced-sketch/NNN.png`.
Harness note: the Xwayland window was tiled to 469x490 by Hyprland; I floated/resized only the :32 window (hyprctl) to 1400x860. Screenshots can lag the app by ~1-2 s, so I waited before shooting.

## Verdicts

| id (alias) | verdict | evidence |
|---|---|---|
| APP-06 (10) | fixed | Rect pattern prompt "Enter applies": Enter in the empty cmd box applied the pattern (extra copy, DOF 87->91) instead of starting Circle (029-033) |
| APP-12 (14) | fixed | Sketch Palette sits below the ViewCube, no overlap (003); with a tool dialog open the palette moves left of it (029, 034) |
| PART-03 (8) | fixed | Extrude, click inner ellipse in a multi-profile sketch: only that profile highlighted, "1 selected" (021) |
| SK-01 (1) | fixed (ellipse); arc not retested | Dragging ellipse centre translates it rigidly (006); driving centre-to-origin dimension 50 keeps shape/orientation (010) |
| SK-02 (6) | fixed (point-to-origin case) | Dimension reads +31.305 and, after typing 50, stays positive on the same side (009-010). Plain two-point-on-lines case not re-run |
| SK-03 (2) | fixed | Trim on a fit-point spline removes the piece (015). Break on spline: no "Cannot break here", DOF 80->82 (016) |
| SK-04 (3) | fixed | Clicking the first point closes the spline, finishes the tool, shaded profile (013) |
| SK-05 (4) | fixed | Offset of ellipse creates a spline approximation (011) |
| SK-06 (5) | fixed (partly retested) | Ellipse edge dimension works (007/008, second one correctly became driven), centre-to-origin works (009-010). Ellipse-to-line combo not tried |
| SK-07 (7) | fixed | Hexagon with its construction circle extrudes as one whole profile (024, 026) |
| SK-08 (9) | fixed for Ellipse, Circular Pattern, Rect Pattern prompt | Ellipse: on-canvas "Major radius/Angle" box, typed 30 / 15 -> R30/R15 (004-005); Circular pattern: on-canvas Count / Total angle, typed 4,180 worked (039-040). Scale, Chamfer not retested |
| SK-09 (11) | fixed | Text typed in cmd box ("Hello"), then dialog field changed to "World": preview shows only World, placed text is World (035-036) |
| SK-10 (12) | fixed | Mirror of ellipse about the sketch Y axis worked (019) |
| SK-11 (15) | mostly fixed | Selection count cleared after Delete (038); polygon Sides box now sits beside Radius (024). Stale tool message on tool switch not specifically checked |
| SK-12 (17) | NOT fixed | After Finish Sketch no profile shading is shown (020); shading only appears while editing or inside Extrude |
| SK-13 (13) | partly fixed | Create menu now has Edge Polygon, Overall Slot, Center Point Slot, Intersect (030). Conic curve, include 3D geometry, project to surface, spline handles not seen |

Counts (15 issues): fixed 11 (APP-06, APP-12, PART-03, SK-01, SK-02, SK-03, SK-04, SK-05, SK-06, SK-07, SK-09, SK-10 = 12 incl. caveats; SK-08 fixed for tested tools), partially fixed 2 (SK-11, SK-13), not fixed 1 (SK-12), regressed 0. Strict tally: 12 fixed, 2 partial (SK-11, SK-13), 1 not fixed, 0 regressed (SK-08 counted as fixed with Scale/Chamfer untested).

## Regression check of round 1 "worked" list
Re-checked and still working: ellipse creation, fit-point spline creation, polygon (circumscribed, Sides box), mirror, text via dialog, circular and rectangular pattern, trim line/spline, offset, Extrude of ellipse/polygon/spline profiles, edit sketch via timeline double-click, construction circle ignored for polygon profile. Not re-run: elliptical arc, Move/Copy, Rotate, Scale, Chamfer, Extend, project, slot. No crash.

## NEW defects
- NEW-ADVANCED-SKETCH-1 (low): cmd box keeps the old text ("Hello") after the Text tool finishes; keyboard focus stays in the cmd box so Delete/E/Ctrl+Enter shortcuts then go to the box (038, only worked once focus returned).
- NEW-ADVANCED-SKETCH-2 (low): after trimming the closed spline the remaining piece has many clustered fit points along one side (015).
- NEW-ADVANCED-SKETCH-3 (low): after dragging an ellipse, its R15/R30 dimension labels stay at the old position (006).
