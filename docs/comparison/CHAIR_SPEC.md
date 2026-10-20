# Chair test model: exact specification

This is the single source of truth for the chair benchmark. FabriCAD and FreeCAD must both build exactly this geometry so that results can be repeated and compared. All dimensions are in mm and angles in degrees. Every number below is fixed; nothing is left to the modeller's choice except how the tool is driven.

## Coordinate system

- Z is up. The floor is z = 0.
- **Front of the chair is −Y**, back is +Y.
- X is the width. The part on the x < 0 side is called "Left", the part on the x > 0 side is called "Right" (these are model-space names, not the sitter's left/right).
- Everything is modelled in place in world coordinates (component origins = world origin) during stages A–C.

## Stage A: two-component assembly

### Component `Seat` (one body, named `Seat`), features in this order

| # | Feature | Exact definition |
|---|---|---|
| S1 | Sketch + Extrude (new body) | Sketch on the plane z = 430 (XY plane offset +430). Rectangle X[−210, 210] × Y[−200, 200], with **sketch fillets R20 at the two front corners** (y = −200) only; rear corners sharp. Extrude +Z by 30 → slab Z[430, 460]. |
| S2 | Fillet R6 | Select the **top front edge** (y = −200, z = 460). Tangent-chain propagation must include the two R20 arc top edges and both side top edges (x = ±210, z = 460) up to the rear face. The rear top edge (y = 200, z = 460) is **not** filleted. |
| S3 | Sketch + Extrude (join) — backrest | Sketch on the plane y = 175 (XZ plane offset to y = 175). Rectangle X[−210, 210] × Z[430, 840]. Extrude +Y by 25 → Y[175, 200]. Operation: Join. (It overlaps the slab rear strip and refills the S2 fillet there.) |
| S4 | Sketch + Extrude (cut) — lumbar slot | Sketch on the backrest front face (y = 175). Rectangle X[−150, 150] × Z[680, 740] (300 × 60, centre z = 710 = 250 above the seat top). Cut through all in +Y. Sharp corners. |

### Component `LegFrame` (one body, named `LegFrame`), features in this order

| # | Feature | Exact definition |
|---|---|---|
| L1 | Sketch + Extrude (new body) | Sketch on the XY plane (z = 0): four 40 × 40 squares centred at (±170, ±160), i.e. X[−190, −150] or [150, 190] × Y[−180, −140] or [140, 180]. Extrude +Z by 430 → legs Z[0, 430]. |
| L2 | Sketch + Extrude (join) — side rails | Two rails 20 (X) × 280 (Y) × 40 (Z), from front leg to back leg on each side: X[−180, −160] and X[160, 180], Y[−140, 140], Z[130, 170] (centre height 150). E.g. sketch both rectangles Y[−140, 140] × Z[130, 170] on the YZ plane and extrude symmetric 20 about x = ±170, or one sketch per side; geometry must match. |
| L3 | Sketch + Extrude (join) — front cross rail | X[−150, 150] × Y[−170, −150] × Z[130, 170] (300 × 20 × 40), connecting the inner faces of the two front legs. This makes LegFrame one connected solid. |

### Assembly

- Ground `Seat`.
- Rigid joint between `LegFrame` (top face of a leg, z = 430) and `Seat` (bottom face, z = 430), as-built, zero offset. Nothing moves.

Reference volumes after stage A (mm³): Seat ≈ **8,565,976**; LegFrame = **3,440,000**.

## Stage B: split into three parts

1. Split the `LegFrame` body with the **YZ origin plane (x = 0)**. This cuts the front cross rail in half.
2. Make the two resulting bodies separate components: **`LegLeft`** (x < 0) and **`LegRight`** (x > 0). `LegFrame` is removed or left empty. The assembly now has three parts: `Seat`, `LegLeft`, `LegRight`.
3. Before the stage-C features, each leg part is 1,720,000 mm³: two legs (1,376,000) + one side rail (224,000) + half the cross rail, 150 × 20 × 40 (120,000).

## Stage C: make the legs slightly different

| Part | Feature | Exact definition |
|---|---|---|
| LegLeft | Chamfer 3 (equal distance) | The 8 bottom edges (z = 0) of its two legs. |
| LegRight | Fillet R5 | The 8 bottom edges (z = 0) of its two legs. |
| LegRight | Hole Ø10, through all | Axis along X, centred at (170, 0, 150) (the midpoint of the right side rail), through the 20 mm rail. |

Reference volumes after stage C (mm³): LegLeft = **1,718,632** (each chamfer frustum removes 684 per leg). LegRight ≈ **1,716,808** (the fillets remove 810.47 per leg; the hole removes 1,570.80).

## Stage D: transform the parts

1. Delete all joints, then ground all three components at their final placements.
2. `LegLeft`: translate (−15, 0, 0).
3. `LegRight`: translate (+15, 0, 0).
4. `Seat`: rotate **−5°** about the axis through point (0, 200, 430) with direction +X (right-hand rule). The front of the seat rises and the backrest tilts back; the rear-bottom edge stays put.

Expected world bounding boxes after stage D (±0.01):

| Part | X | Y | Z |
|---|---|---|---|
| Seat | [−210, 210] | [−198.478, 235.734] | [430.000, 840.619] |
| LegLeft | [−205, −15] | [−180, 180] | [0, 430] |
| LegRight | [15, 205] | [−180, 180] | [0, 430] |

Volumes are unchanged by stage D. The reference volumes above are analytic. Each app's B-rep volume (from its STEP) must match them within 0.05 %. If it doesn't, report the difference rather than adjusting the spec.

## Exports (exact file names and settings)

Write these into the app's output directory (`docs/comparison/fabricad/out/` or `docs/comparison/freecad/out/`):

| File | Content |
|---|---|
| `chair.step` | The assembly, all three parts at their final placements. |
| `chair.stl` | All three parts at their final placements, one file. |
| `seat.stl`, `leg_left.stl`, `leg_right.stl` | One part each, at its final world placement. |
| `chair_default.stl` | Whole assembly using the app's own default STL export settings (for the "out of the box" comparison). |

STL settings for every file except `chair_default.stl`: **binary STL, units mm, absolute linear deflection 0.05 mm, angular deflection 0.2 rad, relative mode off.** Record the exact tessellation parameters each app actually used.
