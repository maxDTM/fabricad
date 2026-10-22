# Advanced sketch tools - QA round 1

Tester: Claude (QA, mouse/keyboard only, display :32). Date 2026-10-22 06:26-06:50. Screenshots: `docs/tests/general/round1/advanced-sketch/NNN.png` (NN = number quoted below).
Note: for the first ~40 minutes the harness display was only 945x490/990 px, so the right part of the UI (cmd box, Text dialog, Sketch Palette) was clipped; findings 10, 11, 14 were re-verified later at the full 1400x860 window.

## Issues

| id | sev | summary |
|---|---|---|
| ADVANCED-SKETCH-1 | high | Dragging an ellipse / elliptical-arc centre point (or editing a driving dimension on it) distorts the ellipse instead of translating it (160, 215, 156) |
| ADVANCED-SKETCH-2 | medium | Trim does nothing on a fit-point spline; Break on spline says "Cannot break here" (135-144) |
| ADVANCED-SKETCH-3 | medium | Fit-point spline closed by clicking its first point is not closed/coincident, no profile (163-165) |
| ADVANCED-SKETCH-4 | medium | Offset of an ellipse: "Offset produced no geometry", no preview curve (98-100) |
| ADVANCED-SKETCH-5 | medium | Cannot dimension an ellipse (no major/minor axis dimension) -> "Cannot measure that combination" (146); picking the 2nd entity (sketch origin, a line) after an ellipse centre is silently ignored (149-153) |
| ADVANCED-SKETCH-6 | medium | Point-to-point dimension shows a negative value (-40.52) and entering 30 flips the geometry to the other side (155-156) |
| ADVANCED-SKETCH-7 | medium | Polygon's construction circle splits the polygon into extra profiles (circle disc + corner slivers) in Extrude (124/125) |
| ADVANCED-SKETCH-8 | medium | Extrude: clicking one profile of a multi-profile sketch ("1 selected") previews/extrudes all profiles of that sketch (190, 195, 198) |
| ADVANCED-SKETCH-9 | medium | Typed entry is inconsistent: Line/Circle/Polygon have an on-canvas input, but Ellipse, Elliptical Arc, Rect/Circular Pattern, Scale, Chamfer prompts say "type ..." while viewport keystrokes are silently ignored (letters even switch tools) (41-46, 55, 94) |
| ADVANCED-SKETCH-10 | low | In the pattern prompt "Enter applies", Enter with focus in the empty cmd box repeats the last command (Circle) and cancels the pattern (212) |
| ADVANCED-SKETCH-11 | low | Text dialog: text typed earlier in the cmd box wins over the dialog's Text field; stale/overstruck preview (183-185) |
| ADVANCED-SKETCH-12 | low | Origin axes cannot be picked as mirror line (62) |
| ADVANCED-SKETCH-13 | low | Missing vs ux.md: slot variants (overall, centre point, 3-point arc, centre-point arc), polygon by edge, conic curve, intersect, include 3D geometry, project to surface; spline fit-point tangent/curvature handles (menu 07) |
| ADVANCED-SKETCH-14 | low | Sketch Palette overlaps and clips the ViewCube; no responsive layout when window < ~1400 px (toolbar, cmd box, Text dialog cut off) (173, 05) |
| ADVANCED-SKETCH-15 | low | Stale UI state: "8 sketch entities selected" after Delete (67); "Nothing to extend to" remains while Break tool active (143); polygon "Sides" box stays at old position (114) |
| ADVANCED-SKETCH-16 | low | Start-up recovery dialog listed autosaves of other concurrently running instances and /tmp/doccore/b.fcad (autosave dir shared between instances) (00) |
| ADVANCED-SKETCH-17 | low | Finished sketch shows no profile shading (122) |

## Reproduction details

1. Sketch on any plane, Create > Ellipse (centre, major end, minor), Escape; press D, click ellipse centre, click a circle centre, place, type 30 Enter -> ellipse becomes long and rotated (156). Or just drag the ellipse centre point with LMB (160). Dragging the curve itself translates rigidly (161, correct). Same with an elliptical arc (214 -> 215: arc endpoints/shape change).
2. Fit Point Spline (5 points, Enter) + Line crossing it. T then click a spline piece beyond the intersection: nothing, no status message (135-138). Break tool on spline: hover offers a point, click -> "Cannot break here" (143-144). Trim on a line crossing the spline works (140).
3. Fit Point Spline, click 4 points, click on first point again, Enter. Drag first point: end stays behind -> open curve (165). No shaded profile.
4. Select ellipse, O, click outside: status red "Offset produced no geometry" (100). Circle offset works (102).
5. D, click ellipse (edge) -> "Cannot measure that combination" (146).
6. See 1; the dimension shows -40.52 (155); after 30 Enter the ellipse sits on the other side of the circle (156).
7. Create > Circumscribed Polygon (centre, edge midpoint), finish sketch, E, click circle area and corner area: both highlight as separate profiles (124).
8. Sketch2 containing ellipse, circle (projected), text, spline pattern: E, click one profile -> Profiles "1 selected" and every profile of the sketch is shown green (198).
9. Ellipse tool, click centre, type 20 Enter -> ignored (45-46). Only after clicking the cmd box (or Ctrl+9) typed values work (181).
10. Rect pattern, click cmd box, type 3,40 Enter, press Enter again in cmd box -> Circle tool starts, pattern cancelled (212). Clicking the viewport (MMB) then Enter applies it (213).

## Tested features and results

Worked correctly:
- Ellipse (centre/major/minor, tilted axis, undo/redo), elliptical arc creation (centre, major, start, end - correct CCW sweep)
- Fit-point spline and control-point spline creation (double-click / Enter to finish), control-point drag editing, spline profile shading, spline + line profile regions
- Mirror of spline and ellipse about a line (mirror constraints created, mirrored orientation correct)
- Sketch text (via Text dialog: text, height 10, font) and Hello via cmd box
- Rectangular pattern (default and typed 3x40 when applied from viewport focus; 2 directions prompt), circular pattern (preview, typed "4,180" gives 4 instances over 180 deg, correct angles), pattern of splines/ellipses
- Move/Copy (preview + distance label), Rotate (preview angle, apply, undo/redo), Scale (typed 1.5 about base point, correct)
- Sketch chamfer (2 mm default), Extend line to line, Break on line, Trim line by spline, Trim ellipse by line (elliptical arc remains, profile OK)
- Offset of circle, Offset of line pairs
- Construction toggle: context menu "Construction", X key on selection, X mode toggle ("Construction mode off"), construction centre line of slot and construction circle ignored for slot profile
- Polygons: circumscribed, inscribed, typed side count (5), live Sides/Radius fields
- Slot (centre to centre): preview, width click, centre line construction
- Project: project edge, circle edge, vertex from a body onto a sketch (purple projected geometry), "Projected n item(s)"
- Dimension: circle diameter typed; point-point dimension drives geometry (but see 6)
- Extrude of sketch profiles incl. ellipse, slot, annulus, polygon, text pieces; Finish sketch (Ctrl+Enter); edit sketch by double-click in timeline

No crash was observed.
