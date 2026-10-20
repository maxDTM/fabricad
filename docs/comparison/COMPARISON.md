# FabriCAD vs FreeCAD: the 3-part chair benchmark

Both apps built the chair in `CHAIR_SPEC.md` from a script, headless, on the same machine. This
document compares how easy that was, how fast it ran and how good the exported meshes are. Inputs:

* `fabricad/REPORT.md`. It covers the test run before the fixes and the "After fixes" section.
  FabriCAD is at main @ b246294 (Release build, OCCT 7.9.3).
* `freecad/REPORT.md`. FreeCAD 1.1.4 AppImage, extracted, OCCT 7.8.1.

All timings and every STL/STEP number below were **re-measured for this document** (2026-10-20) with
the tools in `tools/`. Raw results are in `measurements/` and the merged numbers are in `metrics.json`.

Read the comparison with these caveats in mind:

1. **The FabriCAD fixes were made in response to this same benchmark.** The seven defects the FabriCAD
   tester hit were fixed, and the script was then rewritten without workarounds. FreeCAD's quirks were
   only worked around, not fixed. "After fixes" therefore shows FabriCAD tuned on this exact model.
   The "before" column is the fairer first-contact picture.
2. **The GUI click/key counts are estimates for both apps.** Each was counted from code or docs, not
   recorded in a live session, and the two testers used slightly different counting rules. Treat
   differences under about 20 % as noise.
3. **The sketches are not equivalent.** The FreeCAD sketches are fully constrained (dimensions and
   geometric constraints). The FabriCAD sketches are placed by typed coordinates and keep 2 to 8 DOF.
   FreeCAD's model therefore carries more design intent, and the GUI counts reward FabriCAD for skipping
   dimensions. Adding Fusion-style dimensions would cost about +70 clicks and +40 keys (fabricad/REPORT.md).
4. Both apps reach the spec. Every B-rep volume is within 0.00002 % and every box within 0.0003 mm.
   The spec boxes are rounded to 3 decimals. In each app's STEP, the two models agree to the digits shown.

## 1. Ease of use

### Effort and concepts

| Metric | FabriCAD after fixes | FabriCAD before fixes | FreeCAD 1.1.4 |
|---|---|---|---|
| Script | `chair_min.fcs`, 170 lines | 181 lines | `chair_freecad.py`, 121 `op()` GUI actions (55 top-level + 66 in-sketch) |
| What the lines are | 63 tool invocations, 70 select/pick/OK, 22 typed values, 15 typed properties | similar | Python API calls, 552 lines including helpers and checks |
| Distinct commands / properties | 27 / 8 | 28 / 8 | n/a (Python API) |
| Workbenches / workspaces | 1 design workspace with ribbon tabs (SOLID, SKETCH contextual, ASSEMBLE, TOOLS) | same | 5: PartDesign, Sketcher, Assembly, Part, Mesh |
| Workbench switches in the GUI path | 0 | 0 | 8 (A: PartDesign, Assembly; B: Part, PartDesign, Assembly; C: PartDesign; D: Assembly; export: Mesh) |
| Distinct concepts | about 22 (component, active component, body, offset plane + flip, sketch, profile, Extrude operation/extent, Fillet chain, Chamfer, Hole, Split Body, Components from Bodies, Delete Component, Ground, As-built joint, Move Component + pivot, STEP, STL settings, ...) | same | 24 (adds Tip/active body, AttachmentOffset, constraints/full constraint, BaseFeature body, App::Link, JointGroup, JCS picking, solver, Slice Apart + CompoundFilter, Placement rotation centre, mesh conversion) |
| Sketches fully constrained | no (2-8 DOF left) | no | yes |
| Failed attempts before a clean run | 0 after fixes | 14 (11 caused by the app, 3 by the tester) | 5 (+2 fixes in the verifier) |
| Spec deviations / workarounds | 0 / 0 | 3 / 4 | 1 (joint referenced on the R20 arc centre instead of the bottom face) |
| Estimated GUI clicks / keys | **155 / 455** (610 inputs) | 144 / 610 (754) | **407 / 362** (769) |

### Estimated GUI effort per stage (clicks / keys)

| Stage | FabriCAD after fixes | FreeCAD | Main reason for the difference |
|---|---:|---:|---|
| A: Seat + LegFrame + joint | 76 / 271 | 277 / 215 | FreeCAD draws, then adds 4 dimensions per rectangle (3 clicks each) and sets plane offsets in the property editor. FabriCAD types the corner and size (`: x,y`, `w Tab h`). That costs more keys, but the sketch is unconstrained (caveat 3). |
| B: split into LegLeft / LegRight | 11 / 20 | 29 / 21 | Fusion-style Split Body + Create Components from Bodies, versus Part workbench Slice Apart, 2 new Bodies with BaseFeature, delete the link, insert 2 links and re-pick the joint. |
| C: chamfer, fillet, hole | 25 / 20 | 43 / 18 | Both pick 8 edges per leg. FreeCAD also switches workbench, activates the body and sets up a dimensioned hole sketch. |
| D: delete joint, ground, transforms | 20 / 20 | 26 / 16 | Similar. FreeCAD's Placement dialog with a rotation centre is as direct as FabriCAD's Move Component with a pivot. |
| Exports + save | 23 / 124 | 32 / 92 | FreeCAD needs Mesh workbench + Mesh_FromPartShape before exporting. FabriCAD's new Export STL dialog remembers its settings. Most keys on both sides are file names. |
| **Total** | **155 / 455** | **407 / 362** | |

### How Fusion-like the process is

| Spec step | Fusion 360 | FabriCAD | FreeCAD |
|---|---|---|---|
| Parts as components with bodies | Component > Body | same model (NEWCOMPONENT, active component) | Body (PartDesign) + App::Link in an Assembly object: two separate structures |
| Sketch on an offset plane | Offset Plane, Create Sketch | same | XY_Plane + AttachmentOffset in the property editor (the panel has no offset field) |
| Extrude with New Body / Join / Cut | Extrude, Operation dropdown | same (Join is the default only when the component already has a body, after F1) | separate Pad / Pocket features; one solid per Body; no cross-body Join |
| Fillet with tangent chain | Fillet, Tangent chain on | same | PartDesign Fillet (OCCT propagation) |
| Split a body and turn the pieces into components | Split Body, Create Components from Bodies | same command names | Part workbench Slice Apart, then new Bodies with a BaseFeature (no in-place split) |
| Ground + as-built rigid joint | Ground, As-built Joint | same (faces or components) | Ground + Fixed joint with JCS picking. The reference order matters, and a face pick spins the part 180°. |
| Rotate a component about an axis | Move/Copy component, pivot | Move Component with pivot + rx | Std Placement with a rotation centre (clear, but not an assembly command) |
| STL with refinement settings | Export / 3D Print dialog | Export STL dialog (linear/angular, binary) | Mesh_FromPartShape (Standard mesher), then Mesh export (or File > Export using preferences) |

FabriCAD maps the spec almost one-to-one onto Fusion 360 command names and concepts. FreeCAD reaches
the same geometry through five workbenches and several FreeCAD-specific concepts (Link, BaseFeature,
JCS, Tip), which a Fusion user has to learn first.

### Pain points each tester hit

| | FabriCAD (before fixes) | FreeCAD 1.1.4 |
|---|---|---|
| Silent wrong geometry | F1: the first Extrude in an empty component joined into another component's body. F3: a through-all Hole drilled the other component too. | Attempt 04: the Fixed joint moved the *grounded* Seat 180° (reference order + reversed face JCS). |
| Silent wrong export | F2: STL reused an old triangulation, so the result depended on export order. F6: the STEP lost the Seat part name. | none found |
| Data loss / structure | F4: DELETECOMPONENT deleted the child components | "Link(s) go out of the allowed scope" when Chamfer/Fillet referenced the slice piece (needs the internal FeatureBase) |
| Missing UI | F5: `IMPORT <file>` ignored its path. F7: no STL settings in the GUI. MOVECOMPONENT could not rotate about an axis. | No in-place split. The plane-offset field exists only in the property editor. |
| Sketcher friction | none (typed coordinates) | Redundant H + Symmetric constraint (attempt01). The sketch fillet deleted a dimension on the trimmed point (attempt02). |
| Errors with obscure text | none | "The graph must be a DAG" (joint references rooted at the Assembly) |
| Conventions | U3: the XZ plane normal is −Y, so it needs `flip=true` (unchanged). U4: a preselected face goes into the Hole's placement points (unchanged). | Same XZ −Y convention: offset −175 + Reversed. Fast `BoundBox` padded by the fillet control nets (±211.6). |
| Still open after fixes | STEP import flattens the assembly into root bodies "chair 1..3" | n/a |

### Where FreeCAD is better

* **Parametric intent.** Fully constrained sketches survive edits. FabriCAD's typed rectangles keep their DOF.
* **Speed of the headless run.** It is 2x faster (section 2), and `freecadcmd` starts in 0.07 s.
* **Scripting language.** Python gives loops, variables, asserts and direct OCCT access. FabriCAD's
  `.fcs` is a typed command stream with CHECK/INFO, closer to replaying GUI input but less expressive.
* **Default STL quality.** It is 6.7x more accurate out of the box (section 3).
* **STEP round trip.** The STEP file is 2.6x smaller and reads back as an assembly. FabriCAD reads its own
  STEP back as three flat root bodies.
* **Maturity of the failure modes.** No FreeCAD problem produced a wrong export. All of them showed up as
  console errors or as visibly wrong placements.

## 2. Speed (re-measured)

`tools/bench.py` ran 3 interleaved rounds, one of each item per round, and the table shows the
median. Load average was 1.3 to 2.3, with other agents running. FabriCAD ran `--offscreen` and FreeCAD
ran `AppRun freecadcmd`, each with outputs in a scratch directory. Every run passed: 0 failed
commands, exit code 0.

| Metric | FabriCAD | FreeCAD | Ratio |
|---|---:|---:|---|
| Headless runner startup + shutdown (empty / one-ECHO script) | 0.217 s (0.217 / 0.217 / 0.226) | 0.074 s (0.246 cold / 0.074 / 0.074) | FreeCAD 2.9x faster |
| GUI startup, offscreen, until the main window is shown and the event loop runs | 0.217 s (script mode builds and shows the full MainWindow; no 3D view offscreen) | 2.638 s (3.362 cold / 2.638 / 2.616; no GL context either) | FabriCAD 12x faster |
| Modelling + save + STEP + 5 STL, process wall | **2.600 s** (`chair_min.fcs`: 2.617 / 2.595 / 2.600) | **1.325 s** (1.519 / 1.325 / 1.320) | FreeCAD 2.0x faster |
| Same, time inside the app/script | 2.353 s | 1.170 s (0.342 s of it is module imports) | FreeCAD 2.0x faster |
| Full test script incl. 130 checks, .fcad reopen, STEP read-back | 3.536 s (`chair.fcs`; in-app 3.299 s) | n/a (verification is a separate `verify_freecad.py`) | |
| Earlier reports, for reference | 2.61 s / 0.227 s startup | 1.302 s / 0.081 s startup | consistent within 2 % |

Per stage, in ms. FabriCAD values are the modelling commands of `chair.fcs`, without CHECK/INFO/LIST/
OPTIONS. FreeCAD values are the script's `STAGE_TIMES`. Both are from the last benchmark run.

| Stage | FabriCAD | FreeCAD | Note |
|---|---:|---:|---|
| A Seat | 559 | 190 | |
| A LegFrame | 797 | 121 | FabriCAD: each rail Extrude costs about 160 ms. The preview is recomputed on `EXTRUDE` (63 ms) and again on each typed property (`direction=` 40 ms, `symmetricMeasure=` 33 ms) before `OK` (27 ms). |
| A assembly | 60 | 48 | |
| B split | 84 | 60 | |
| C leg features | 376 | 209 | |
| D transforms | 366 | 5 | FabriCAD: `DELETECOMPONENT` 188 ms and `DELETE` joint 109 ms each trigger a full recompute. GROUND/MOVECOMPONENT are now 5 ms. |
| Save + STEP + 5 STL | 141 | 196 | FabriCAD is faster here |
| (imports / startup) | 217 (app startup) | 342 (Python module imports) + 74 (startup) | |

Where FabriCAD's time goes (`chair_min.fcs`, 2.35 s in-app):

* `OK` (23x, 387 ms) and `EXTRUDE` (7x, 348 ms) are the largest items, followed by the 39 `SELECT` lines
  (143 ms).
* Every command has a floor of about 2 to 3 ms (event-loop settle), which is about 0.4 s over 170 lines.
* The 15 property lines cost 360 ms because each one recomputes the live preview.

## 3. STL quality (measured with `tools/stlcheck`)

`tools/stlcheck.cpp` is a small OCCT 7.9.3 tool. For each STL it does the following:

* It welds the vertices by exact coordinates.
* It checks edge use (open, non-manifold, winding consistency), stored normals against the winding,
  degenerate triangles, the connected shells, the signed volume (positive means outward normals) and
  the Euler genus.
* It samples every triangle on a barycentric grid (subdiv 4: 12 non-corner points per triangle, plus
  every vertex) and measures each sample's distance to the boundary faces of the matching solid in the
  app's own `chair.step` with `BRepExtrema_DistShapeShape`. The maximum is the chordal deviation.
  Rerunning with subdiv 8 changed no maximum (`chair`, `chair_default`, both apps).
* As a cross-check it measures every mesh against the *other* app's STEP. The results are identical to
  5 decimals, so the two B-reps are the same geometry.

Every file from both apps is binary, mm, watertight (0 open, 0 non-manifold and 0 inconsistently
wound edges), has outward normals (every shell volume > 0, 0 stored normals disagreeing with the
winding) and has 0 degenerate triangles. The vertices lie on the B-rep within 3e-5 mm. The genus is
right: Seat 1 (lumbar slot), LegLeft 0, LegRight 1 (hole). Every mesh box is within 0.0003 mm of the
spec, which is the spec's own rounding.

### Matched setting: linear 0.05 mm absolute, angular 0.2 rad, relative off

| File | App | Triangles | Vertices | Bytes | Volume vs spec | Max chordal dev (mm) | Mean dev (mm) | Max on |
|---|---|---:|---:|---:|---:|---:|---:|---|
| chair.stl | FabriCAD | 2032 | 1018 | 101,684 | −0.00072 % | 0.03128 | 0.00811 | R6 fillet torus at the R20 corners |
| chair.stl | FreeCAD | 2040 | 1022 | 102,084 | −0.00072 % | 0.03128 | 0.00808 | same |
| seat.stl | FabriCAD | 1320 | 660 | 66,084 | −0.00097 % | 0.03128 | 0.01142 | torus |
| seat.stl | FreeCAD | 1320 | 660 | 66,084 | −0.00097 % | 0.03128 | 0.01142 | torus |
| leg_left.stl | FabriCAD | 76 | 40 | 3,884 | 0.00000 % | 0.00000 | 0.00000 | planar only (chamfers) |
| leg_left.stl | FreeCAD | 76 | 40 | 3,884 | 0.00000 % | 0.00000 | 0.00000 | planar only |
| leg_right.stl | FabriCAD | 636 | 318 | 31,884 | −0.00020 % | 0.01076 | 0.00219 | R5 leg fillet (cylinder) |
| leg_right.stl | FreeCAD | 644 | 322 | 32,284 | −0.00019 % | 0.01076 | 0.00217 | R5 leg fillet |

At the matched setting the two exporters are **equivalent**. Both call `BRepMesh_IncrementalMesh`
with the same parameters. The only difference is 8 more triangles on FreeCAD's R5 fillet faces
(328 vs 320), which probably comes from OCCT 7.8.1 vs 7.9.3 or from the fillet face parametrisation.
It makes no measurable change in deviation or volume. Both stay below the 0.05 mm target: the maximum
is 0.031 mm, and the angular limit drives the result on these small radii.

### Defaults ("out of the box", `chair_default.stl`)

| App | Setting actually used | Triangles | Vertices | Bytes | Volume vs spec | Max chordal dev (mm) | Mean dev (mm) | Max by surface (torus / cylinder) |
|---|---|---:|---:|---:|---:|---:|---:|---|
| FabriCAD | 0.05 mm, 20° (0.349 rad), relative off | 1436 | 720 | 71,884 | −0.00169 % | **0.05287** | 0.01314 | 0.0529 / 0.0241 |
| FreeCAD | 0.1 mm, 0.1 rad, relative off | 5992 | 2998 | 299,684 | −0.00018 % | **0.00784** | 0.00252 | 0.0078 / 0.0060 |

* FabriCAD's default is coarser (0.24x the triangles of FreeCAD's). It slightly **exceeds its own
  nominal 0.05 mm**: 0.0529 mm on the R6 fillet torus, because OCCT's deflection is a target, not a
  bound.
* FreeCAD's default is 6.7x more accurate, at 4.2x the file size, because its 0.1 rad angular limit
  dominates on the R5 to R20 radii.
* Both defaults are reasonable for printing. A 0.05 mm error is under one FDM layer line.
* A user who compares the two defaults sees FreeCAD's fillets as visibly smoother.

### STEP (the B-rep reference)

| | FabriCAD | FreeCAD |
|---|---|---|
| Schema | AP214 (AUTOMOTIVE_DESIGN) | AP214 |
| Size / entities | 239,623 bytes / 5,002 | 91,358 bytes / 2,231 |
| Why the size differs | writes PCURVEs (356 `PCURVE` + definitional representations), the OCCT default | no pcurves |
| Product tree | chair → Seat, LegLeft, LegRight (3 instances; the Seat instance carries the −5° placement) | Assembly → Seat, LegLeft, LegRight (3 instances, same placements) |
| Colour | 1 `COLOUR_RGB` | none |
| Volumes (Seat / LegLeft / LegRight) | 8,565,975.690 / 1,718,632.000 / 1,716,808.259 | identical |
| Read back by its own app | flattened into root bodies "chair 1..3" | App::Part assembly with the 3 parts |

Pcurves make the file bigger, but some importers need them less repair. This is a trade-off, not a
defect.

## 4. Verdict and recommendations

**Verdict.**

* **FabriCAD is the easier and more Fusion-like path to this chair.** It needs one workspace instead
  of five workbenches, uses Fusion's own command names for split/components/joints, needs no JCS,
  Link or BaseFeature concepts, and takes about 60 % fewer estimated clicks (21 % fewer clicks + keys combined). After the fixes there are
  no workarounds left.
* **This ease was earned on this benchmark.** On first contact FabriCAD had 14 failed attempts against
  FreeCAD's 5. Four of its defects (F1, F2, F3, F6) produced wrong geometry or wrong exports *silently*,
  which FreeCAD never did.
* **FreeCAD is twice as fast headless and builds a fully constrained, more robust model.** Its default
  STL is far more accurate, and its STEP round-trips the assembly.
* **The exported data is equal.** At matched tessellation the meshes are equivalent (same deviation to
  1e-5 mm, same volume). Both apps produce valid, watertight, correctly oriented meshes and correct STEP
  geometry.

**Recommendations for FabriCAD**, by impact:

1. **Script/preview performance.** In non-interactive (script) mode, skip or defer the live preview
   and compute a feature once at `OK`. A property line now re-runs the full preview, at 30 to 40 ms
   each, about 360 ms of 2.35 s. Make `DELETECOMPONENT` and joint `DELETE` recompute only what changed
   (188 + 109 ms now). Look at the 2 to 3 ms per-command settle floor. Together these should bring
   `chair_min` from 2.6 s close to FreeCAD's 1.3 s.
2. **A true headless runner.** Run without building the MainWindow, like `freecadcmd` at 0.07 s, for
   batch/CI use. Keep the current fast GUI start (0.22 s versus FreeCAD's 2.6 s): it is a real strength.
3. **Default STL quality.** Change the default to about 0.05 mm / 0.2 rad (11.5°), or offer a
   "fine / medium / coarse" preset like Fusion's refinement levels. Today's 20° default exceeds 0.05 mm
   on small fillets.
4. **STEP import should keep the assembly.** Create components from the XCAF tree with product names
   (not root bodies named "chair 1..3"), so FabriCAD round-trips its own STEP.
5. **Design intent in sketches.** Make typed corner/size input optionally create the matching
   dimensions and constraints (Fusion does this when you type values into the sketch boxes), so typed
   sketches are fully defined, not left at 2 to 8 DOF.
6. **Guard against silent cross-component effects.** F1 and F3 are fixed, but any automatic Join, Cut
   or Hole that touches a body of another component should say so in the status line.
7. **Small conventions (U3, U4).** Offer an Offset Plane that follows a +Y "world direction" (or show
   the normal arrow in the dialog), and route a preselected face into the Hole's face input.
8. **Optional STEP size.** Consider a preference to omit pcurves (`write.surfacecurve.mode`) for a
   smaller STEP file.
9. **Measure the GUI path.** Record one live GUI session with real clicks to replace both estimates.

## Reproduce

```sh
# timings (writes nothing into docs/comparison)
python3 -I docs/comparison/tools/bench.py build/bin/fabricad ../fabricad-ref/freecad-app/squashfs-root <scratch> 3 > <scratch>/bench.json
python3 -I docs/comparison/tools/fab_stages.py docs/comparison/fabricad/chair.fcs <scratch>/fab/chair.fcs.log > <scratch>/fab_stages.json
# STL/STEP checker (stand-alone CMake project, system OCCT)
cmake -S docs/comparison/tools -B <scratch>/stlcheck-build -DCMAKE_BUILD_TYPE=Release && cmake --build <scratch>/stlcheck-build -j2
# all STL metrics + merged metrics.json (raw results in docs/comparison/measurements/)
python3 -I docs/comparison/tools/collect_metrics.py <scratch>/stlcheck-build/stlcheck <scratch>/bench.json <scratch>/fab_stages.json <scratch>/fc/last_run.log
```
