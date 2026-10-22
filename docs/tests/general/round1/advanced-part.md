# ADVANCED-PART QA, round 1 (display :31)

Screenshots: docs/tests/general/round1/advanced-part/ (s*.png primitives, th*/md* thread, ho* hole, fv* variable fillet, sc* scale, mv* move/copy, op*/an*/mp*/p3*/ax*/pt* construction, sp* split, dr* draft, pp* press-pull, tk* thicken, rp*/cp* patterns, sw* sweep, lf* loft, rb* rib, pn* paste new, cc* components, un* undo/redo).

Note: the first ~60 screenshots could not be viewed live (image-read limit), they were reviewed afterwards via montages.

## Issues
- ADVANCED-PART-1 (medium): Torus dialog opens with invalid defaults (ring 40 mm, tube 50 mm), OK disabled with red text "the torus diameter must be smaller than the ring diameter". Screenshot s11/s13 montage. Expected valid defaults.
- ADVANCED-PART-2 (high): Modeled thread fails on a plain cylinder. Create cylinder (10 mm dia), CREATE > Thread, click its side face, check Modeled: error "modelled thread failed: boolean result is not a valid shape", OK disabled. Screenshot md1.png. Cosmetic thread works.
- ADVANCED-PART-3 (medium): Thread dialog has only "ISO Metric profile"; unchecking Auto size reveals no size/pitch/class fields (th8c.png). Fusion exposes Size, Designation, Class.
- ADVANCED-PART-4 (medium): Paste New (Ctrl+Shift+V) after copying a body (browser select Body7, Ctrl+C, mouse over viewport) does nothing: no new body, no status message (pn3/pn4). Ctrl+C with a face selected silently copies nothing useful.
- ADVANCED-PART-5 (low/medium): Constant 2 mm fillet on a vertical edge of the drafted box fails with "fillet produced an invalid shape (radius too large for the geometry?)" (vf1.png); likely valid in Fusion. Box had been split and drafted.
- ADVANCED-PART-6 (low): Visible dark seam lines on sphere, torus, cylinder and cone in shaded view (th22z.png, lf4z.png); Fusion does not draw seams.
- ADVANCED-PART-7 (low): Several dialogs remember the last typed value across commands (Offset plane -12 mm, Press-Pull -8 mm) and carry it into the next unrelated use; Fusion resets to defaults.
- ADVANCED-PART-8 (low): Selecting a face in the viewport as first step of Offset Plane sometimes needed a second click before the dialog showed it selected (op1/op2).

## Tested and working
Box, Cylinder (join/cut/new body/intersect), Sphere, Torus (after fixing sizes), Coil (circular/square sections, revolutions+height), cosmetic Thread, Hole (simple and tapped M6x1, countersink dropdown present), variable-radius fillet (end radius + intermediate radii), Scale (uniform/non-uniform, sphere to ellipsoid), Move/Copy with copy (rotation+translation), Draft with neutral plane, Press-Pull, Thicken, Split Body (plane tool), Sweep (profile+path, perpendicular/parallel), Loft, Rib, Offset Plane, Plane at Angle, Midplane, Plane Through Three Points, Axis (cylinder), Point (vertex), Create Components from Bodies, Rectangular Pattern of components (2 directions), sketch on offset plane, browser visibility toggles, right-click body menu, 30x undo then 30x redo, app menu. No crashes occurred.
