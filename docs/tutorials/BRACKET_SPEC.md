# Bracket: the part of the guided tour

This is the single source for the numbers of the in-app tour "Your first part: a mounting bracket"
(`resources/tutorials/bracket/tutorial.json`, plan: `docs/plans/tutorial.md`). Two tests build it:

- `fabricad_tutorial_bracket_twin` (label `tutorial`, headless): `tests/tutorial/bracket.fcs` types the
  same inputs as commands and CHECKs every number below. It also writes the chapter checkpoints
  `ch1.fcad` … `ch10.fcad` that ship in `resources/tutorials/bracket/`.
- `fabricad_tutorial_bracket_{default,alt,light}` (labels `gui;tutorial`, opt-in with `FABRICAD_GUI=1`):
  `tools/uitest/tutorial_run.py` replays the tour as real X input with the overlay active, then checks
  the exported STL.

Units are mm. Z is up and the plate bottom is z = 0. The front is −Y, the flange is at the back (+Y),
and the part is symmetric about x = 0. `t` is the plate thickness: built at **t = 8**, changed to
**t = 10** in the "Edit the history" chapter.

## Features

| # | Feature | Definition (as the tour builds it) |
|---|---|---|
| 1 | Sketch1 (XY) | Center Rectangle at the origin, typed 80 Tab 50: X[−40, 40] × Y[−25, 25]. 0 DOF. |
| 2 | Extrude1 (New Body `Body1`) | Distance t, +Z: the plate Z[0, t]. |
| 3 | Sketch2 (plate top face) | Project the face. Line chain from the projected back-left corner (−40, 25): 10 toward the front (typed), 80 to the right (typed), click the back-right corner, click the start point. The clicks snap to the projected corners, so the strip X[−40, 40] × Y[15, 25] is fully defined (0 DOF) without an extra constraint. |
| 4 | Extrude2 (Join) | The strip, 40 up: the flange X[−40, 40] × Y[15, 25] × Z[t, t+40]. |
| 5 | Fillet1 R10 | The two front vertical edges (x = ±40, y = −25). |
| 6 | Sketch3 (plate top face) | Project the face. **Center to Center Slot**: centres 30 apart (typed), width 8 (typed), horizontal. Dimensions from the **left arc centre**: 10 to the projected front edge, 25 to the projected left edge. Centres (−15, −15) and (15, −15). 0 DOF. |
| 7 | Extrude3 (Cut, All) | The slot through the plate (the cut flips into the material by itself). |
| 8 | Hole1 | Counterbore on the top arc edge of the right fillet (its centre, (30, −15)). Ø6.6 through all, counterbore Ø11 × 4. |
| 9 | Mirror1 | Hole1 mirrored across the YZ origin plane: (−30, −15). |
| 10 | Sketch4 (flange front face, y = 15) | Project the face. Circle Ø12 (typed). Centre 10 below the projected top edge and 40 from the projected left edge: (0, z = t+30). 0 DOF. |
| 11 | Extrude4 (Cut, All) | A Ø12 hole through the 10 mm flange. |
| 12 | Chamfer1 2 mm | The two long top edges of the flange (y = 15 and y = 25, z = t+40). |
| 13 | Sketch5 (YZ) | Project the right side face (x = 40, L-shaped). One line from the **midpoint** of the projected plate top line (y −15..15, z = t) to the **midpoint** of the projected flange line (y = 15, z t..t+38): (y 0, z t) to (y 15, z t+19). Both ends snap to the midpoints: 0 DOF. |
| 14 | Rib1 | Thickness 6, symmetric about x = 0, To Next: a right triangle 15 × 19 × 6. |

Timeline (14 features, all OK, marker at the end): Sketch1, Extrude1, Sketch2, Extrude2, Fillet1, Sketch3,
Extrude3, Hole1, Mirror1, Sketch4, Extrude4, Chamfer1, Sketch5, Rib1.

Changes against the first plan (2026-10-23, found while building the tour on the real UI): the Equal
constraint of Sketch2 is gone (the corner clicks already fix the strip, so Equal was redundant); the slot is
a Center to Center Slot positioned from its left arc centre (25 from the left edge instead of the slot
centre at 40: the same slot, and both of its dimensions are typed); the rib line runs between the two
projected midpoints (two snapped clicks instead of two dimensions), so the rib is 15 × 19 instead of 20 × 20.

## Reference volumes (analytic)

| Contribution | Formula | t = 8 | t = 10 |
|---|---|---|---|
| Plate | 80·50·t | 32 000 | 40 000 |
| Flange | 80·10·40 | +32 000 | +32 000 |
| Fillets | −2·(1−π/4)·10²·t | −343.363 | −429.204 |
| Slot | −(30·8 + π·4²)·t | −2 322.124 | −2 902.655 |
| 2 counterbores | −2π(5.5²·4 + 3.3²·(t−4)) | −1 033.961 | −1 170.809 |
| Flange hole | −π·6²·10 | −1 130.973 | −1 130.973 |
| Chamfers | −2·(½·2·2)·80 | −320 | −320 |
| Rib | +½·15·19·6 | +855 | +855 |
| **Total** | | **59 704.579** | **66 901.359** |

Cumulative checkpoints at t = 8 (the tour asserts each one; the shipped chapter checkpoint is in brackets):

| After | Volume (mm³) |
|---|---|
| Extrude1 | 32 000 (ch2) |
| Extrude2 | 64 000 (ch3) |
| Fillet1 | 63 656.637 (ch4) |
| Extrude3 | 61 334.513 (ch5) |
| Hole1 | 60 817.533 |
| Mirror1 | 60 300.552 (ch6) |
| Extrude4 | 59 169.579 (ch7) |
| Chamfer1 | 58 849.579 (ch8) |
| Rib1 | 59 704.579 (ch9) |
| Extrude1 = 10 | 66 901.359 (ch10) |

**Bounding box** (tight): at t = 8 X[−40, 40] Y[−25, 25] Z[0, 48]; the final part is
**X[−40, 40] Y[−25, 25] Z[0, 50]**. Clearances: the slot (x −19..19, y −19..−11) misses the counterbores
(x 24.5..35.5) and the rib (y ≥ 0); the flange hole (z t+24..t+36) is clear of the rib top (t+19) and of the
chamfer (t+38).

**Tolerances**: B-rep volume 0.01 %, box 0.01 mm.

**Measure** (chapter 11): the two counterbore top rims give a centre distance of 60 mm.

**STL** (chapter 13): binary, linear deflection 0.02 mm, angular 10°. `tools/uitest/stlcheck.py` checks:
watertight and consistently oriented, one shell, no degenerate triangles, volume within 0.3 % of 66 901.359,
box within 0.05 mm.

**Reopen** (chapter 12): the saved `bracket.fcad`, closed and opened again, has the 14 features, all OK, the
volume 66 901.359 and zmax 50.
