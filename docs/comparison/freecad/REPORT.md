# Chair benchmark in FreeCAD 1.1.4

This is the chair from `docs/comparison/CHAIR_SPEC.md`, built in FreeCAD with its standard
workflow and driven headless from `freecadcmd`. The output matches the spec: B-rep volumes are
within 0.00002 % and bounding boxes within 0.0003 mm, so no tolerance is exceeded. One modelling
choice differs from the spec's wording; it is described under "Deviations".

## Setup

| Item | Value |
|---|---|
| FreeCAD | **1.1.4** (tag 1.1.4, git 4fd3bf320d95, built 2026-09-28), `FreeCAD_1.1.4-Linux-x86_64-py311.AppImage` from github.com/FreeCAD/FreeCAD/releases (the "latest" release on 2026-10-20). SHA-256 `f6dc6ba6…b43434` matches the published checksum file. |
| Kernel | OpenCASCADE 7.8.1 (bundled), Python 3.11 |
| Location | `fabricad-ref/freecad-app/FreeCAD.AppImage`, extracted to `fabricad-ref/freecad-app/squashfs-root/` |
| How it runs | `squashfs-root/AppRun freecadcmd chair_freecad.py`. AppRun sets PYTHONHOME and similar variables and then runs `usr/bin/freecadcmd`. `FreeCAD.AppImage freecadcmd …` also works but adds about 0.29 s per launch to mount the squashfs. |
| User config | `FREECAD_USER_HOME` points to a fresh scratch directory, so every preference is a FreeCAD default. |
| Machine | Arch Linux, 8 threads, 7.7 GB RAM, no X server needed |

## Files

| File | Purpose |
|---|---|
| `chair_freecad.py` | The model. Each `op()` call is one GUI action. At the end it prints the operation list, stage timings and volumes. |
| `verify_freecad.py` | Re-reads `out/chair.step` and the STLs, checks them against the spec's reference volumes and bounding boxes, works out the default STL settings, and reopens the .FCStd. |
| `bench.py`, `gui_ready.py` | Timing harness: 3 full runs, freecadcmd startup, and GUI startup offscreen. |
| `out/chair.step` | The assembly as STEP AP214 (product tree: Assembly, Seat, LegLeft, LegRight), with the parts at their final placements. |
| `out/chair.stl`, `seat.stl`, `leg_left.stl`, `leg_right.stl` | Binary STL in mm: absolute linear deflection 0.05 mm, angular deflection 0.2 rad, relative off. |
| `out/chair_default.stl` | The Assembly exported through File > Export with FreeCAD's default settings. |
| `out/chair.FCStd`, `out/chair_stageA.FCStd` | The final document and a snapshot after stage A. |
| `logs/attempt01..06.log` | Logs from each development iteration. |
| `logs/run1..3.log`, `logs/bench.json`, `logs/verify.log`, `logs/gui_startup*.log` | Logs from the measured runs. |

## How it was modelled

Each step uses the tool a FreeCAD 1.x user would pick.

* **Seat** (PartDesign Body):
  * **S1**: Sketch on XY_Plane with AttachmentOffset z = 430. Drawn with the rectangle tool, then the Sketcher fillet tool (R20) on the two front corners. The sketch is fully constrained (16 constraints). Pad 30.
  * **S2**: PartDesign Fillet R6 on the single top-front edge. OCCT's tangent-chain propagation picks up both R20 arcs and both side top edges up to the rear face.
  * **S3**: Sketch on XZ_Plane, offset -175. In FreeCAD 1.x the XZ_Plane normal is -Y, so the offset is negative and the Pad (25) is Reversed.
  * **S4**: Sketch attached to the backrest front face (y = 175), then Pocket Through all.
* **LegFrame** (PartDesign Body):
  * **L1**: One XY sketch with four squares, Pad 430.
  * **L2**: Two YZ_Plane sketches, offset ±170, each Pad 20 with Symmetric to plane.
  * **L3**: XZ_Plane sketch offset +160 (plane y = -160), Pad 20 Symmetric.
* **Stage A assembly**:
  * Built with the Assembly workbench, which **is scriptable headless**. Its own `AssemblyTests/TestCore.py` runs without a GUI.
  * `Assembly::AssemblyObject` plus `App::Link` components (Insert Component), with Seat grounded (`GroundedJoint`).
  * One **Fixed** joint, solved by the bundled OndselSolver (MbD). After solving, neither part has moved (placements are exactly identity; the script checks this).
* **Stage B**:
  * Part workbench, **Part_SliceApart**, i.e. `BOPTools.SplitFeatures.makeSlice` in mode "Split" followed by `CompoundTools.Explode`. Base is the LegFrame body and the tool is the LegFrame **YZ origin plane**.
  * Each of the two compound-filter pieces gets a new PartDesign Body whose BaseFeature is that piece, named LegLeft and LegRight. Each is 1,720,000 mm³ exactly.
  * The LegFrame link is removed from the assembly. The LegFrame body stays in the document, hidden, as the source of the slice.
  * LegLeft and LegRight are inserted as links. The Fixed joint is re-picked onto LegLeft, and nothing moves.
* **Stage C**:
  * LegLeft: PartDesign Chamfer, Equal distance 3, on the 8 bottom edges.
  * LegRight: PartDesign Fillet R5 on its 8 bottom edges, then PartDesign **Hole**: a sketch on the rail's outer face (x = 180) with a circle of diameter 10 centred at (y 0, z 150), Diameter 10, not threaded, DepthType Through all.
* **Stage D**:
  * The Fixed joint is deleted, then LegLeft and LegRight are grounded.
  * Link placements: LegLeft (-15, 0, 0); LegRight (+15, 0, 0); Seat `Placement(0, Rotation(X, -5°), centre (0, 200, 430))`, i.e. the Placement dialog's "rotation around centre".
  * Then Solve. Every part is grounded, so nothing moves.
* **Export**:
  * `Import.export([Assembly])` writes the STEP file (ExportOCAF2, the same path as File > Export in the GUI).
  * `MeshPart.meshFromShape(LinearDeflection=0.05, AngularDeflection=0.2, Relative=False)` is the "Standard" mesher in Mesh_FromPartShape. In the GUI that panel takes 0.05 mm and 11.459°.
  * `Mesh.export([Assembly])` writes the default STL (the GUI's File > Export > STL).

## Verification against the spec

The volumes and bounding boxes below are measured on the solids re-read from `out/chair.step`. Bounding boxes use `optimalBoundingBox()`.

| Part | B-rep volume (mm³) | Reference | Deviation | Bounding box X / Y / Z | Max bbox deviation |
|---|---|---|---|---|---|
| Seat | 8,565,975.690 | 8,565,976 | -0.000004 % | [-210, 210] / [-198.4779, 235.7339] / [430.0000, 840.6187] | 0.00028 mm (the spec values are rounded to 3 decimals) |
| LegLeft | 1,718,632.000 | 1,718,632 | 0.000000 % | [-205, -15] / [-180, 180] / [0, 430] | 4e-11 mm |
| LegRight | 1,716,808.259 | 1,716,808 | +0.000015 % | [15, 205] / [-180, 180] / [0, 430] | 3e-14 mm |

* Volumes at intermediate stages, read from the live document:
  * A: Seat 8,565,975.690 and LegFrame 3,440,000.000.
  * B: 1,720,000.000 for each leg.
  * C: same as above.
* The STEP file holds 3 valid solids. FreeCAD's importer reads it back as Assembly (App::Part) containing Seat, LegLeft and LegRight.
* Reopening `chair.FCStd` and recomputing gives 77 objects, none in error, with the same volumes.
* Every STL is binary, closed (`isSolid`), has no non-manifolds and no self-intersections, and its bounding box matches the spec to 0.0003 mm. Mesh volume deviations: seat -0.0013 %, leg_left 0 %, leg_right -0.00017 %.

| STL | Facets | Bytes | Tessellation actually used |
|---|---|---|---|
| chair.stl | 2040 | 102,084 | linear 0.05 mm absolute, angular 0.2 rad, relative off (BRepMesh_IncrementalMesh) |
| seat.stl | 1320 | 66,084 | same |
| leg_left.stl | 76 | 3,884 | same |
| leg_right.stl | 644 | 32,284 | same |
| chair_default.stl | 5992 | 299,684 | FreeCAD default: **linear 0.1 mm absolute** (preference `Mod/Mesh/MaxDeviationExport`, default 0.1), **angular 0.1 rad** (= min(0.1, 5·lin + 0.005), from `TopoShape::getFaces` / `defaultAngularDeflection` in 1.1.4), relative off. Confirmed by re-meshing the same shapes at (0.1, 0.1): also exactly 5992 facets. |

## Deviations and FreeCAD-specific notes

1. **Joint reference on the Seat side: the R20 arc edge, not the planar bottom face.** The spec asks for the rigid joint between the leg top face and the Seat bottom face, as-built with zero offset. In FreeCAD a planar face's joint coordinate system (JCS) takes the face's surface frame. The Seat bottom face is the reversed sketch plane, so its normal is -Z and its X axis is -X. The leg top face has an identity frame. The Fixed joint matches both Z (or flipped Z) and the X axis, so a face-to-face joint spins LegFrame 180° about Z around the chosen point. That is attempt 04; in that attempt the grounded Seat itself was the part that moved, see note 2.

   No point lies on both bottom faces as a vertex. So a zero-offset as-built joint is made by placing the Seat JCS on the centre of the bottom R20 arc edge at (190, -180, 430). This point lies on the bottom face's boundary, and its JCS frame is identity. The LegFrame JCS sits on the front-right leg's top face, snapped to its corner vertex, which is the same point. Result: identity placements, nothing moves.

   The only way to keep a face-to-face pick would be a 180° rotation offset in the joint panel, which contradicts "zero offset".
2. **Reference order matters in FreeCAD 1.1.4.** With the leg picked first and the grounded Seat second, the joint's pre-solve (`Joint.matchJCS`) moved the grounded Seat to the leg, and the solver kept it there. With the Seat picked first, the non-grounded part moves, which is correct. The script picks the Seat first.
3. **Stage B/C joint handling.** FreeCAD has no in-place "split body inside a component". The split pieces become new Bodies, so the LegFrame link is deleted from the assembly and the Fixed joint is re-picked onto LegLeft before stage D deletes it.
4. **Hole sketch.** The sketch is attached to the rail's outer face (x = 180). The Hole cuts against the sketch normal, so -X, through the 20 mm rail. The circle centre's local coordinates are read from the sketch placement, which is what a user does by reading the grid or constraining to projected edges.
5. **Bounding-box measurement.** `Shape.BoundBox`, OCCT's fast box, reports the rotated Seat as X ±211.648 because it is padded by the torus/B-spline control nets of the fillets. The verification uses `optimalBoundingBox()`; the tessellated STL also gives exactly ±210. This is a measurement detail; the geometry is correct.
6. Harmless console warnings:
   * "Chair#Assembly still touched after recompute"
   * STEP export "GroundedJoint has null shape", meaning joint objects are not exported
   * "Looking for thread definitions" from the Hole feature.

## Measurements

| Metric | Value |
|---|---|
| Full script, process wall-clock (startup + build + save + all exports), median of 3 | **1.302 s** (runs: 1.302 / 1.298 / 1.331 s) |
| Time inside the script, median of 3 | 1.153 s. Of this, module imports (Part, Sketcher, Mesh, MeshPart, Import, BOPTools, Assembly) take 0.33 s; the rest is below. |
| Per stage (run 3) | Seat 0.184 s, LegFrame 0.116 s, assembly + joint 0.048 s, split 0.062 s, stage-C features 0.202 s, transforms 0.004 s, save + STEP + 5 STL 0.202 s |
| freecadcmd startup (empty script), extracted, median of 5 | **0.081 s** |
| freecadcmd startup through the AppImage (squashfs mount), median of 3 | 0.369 s |
| GUI startup, `QT_QPA_PLATFORM=offscreen` with DISPLAY unset, until main window shown and event loop running, median of 3 | **2.68 s** (2.87 / 2.68 / 2.61 s). This is a lower bound: offscreen has no OpenGL ("QOpenGLWidget: Failed to create context"), so the Coin3D view is not really initialised. With DISPLAY set, offscreen aborts with "Could not initialize GLX". |
| Operations (GUI actions in the script) | **121**: 55 top-level commands (feature, workbench switch, dialog) and 66 in-sketch actions (geometry tools and dimensions) |
| Workbenches needed | **5**: PartDesign, Sketcher (entered automatically), Assembly, Part (Slice Apart), Mesh (Mesh_FromPartShape / export). Std Placement and File Export are always available. |
| Distinct concepts | **24**: Body, Origin planes, sketch attachment and AttachmentOffset, Sketcher geometry, geometric constraints, dimensional constraints / full constraint, sketch fillet trimming, Pad (Reversed / Symmetric), Pocket (Through all), Fillet, Chamfer, Hole, BaseFeature body (FeatureBase), Tip/active body, Assembly object, App::Link component, JointGroup, Grounded joint, Fixed joint and JCS picking (face/vertex/arc centre), Slice Apart and CompoundFilter (explode), Placement with rotation centre, solver, Mesh tessellation parameters, STEP/STL export. |
| Failed attempts / iterations | **5** before a clean run (attempt 06), listed below, plus 2 measurement fixes in the verifier. |
| Estimated GUI effort | **about 407 mouse clicks and 362 keystrokes** (table below) |

### Iterations

1. **attempt01**: SeatSketch had a redundant constraint. A Symmetric constraint on the rear line's endpoints duplicates that line's Horizontal constraint; the GUI tool removes the H constraint automatically, but the script added both. Fix: dimension the corner from the origin instead.
2. **attempt02**: SeatSketch was left with 1 DoF. The Sketcher fillet tool trims the front line and **deletes constraints on the trimmed endpoint**, which included the front-to-origin dimension. Fix: add that dimension after filleting.
3. **attempt03**: ran end to end, but logged "The graph must be a DAG". The joint references were rooted at the Assembly object (`[asm, ["Link.Face5", …]]`), which creates a cycle. Fix: root them at the component, as the 1.1 GUI does (`getComponentReference`), i.e. `[link, ["Face5", "Vertex4"]]`. Also switched the deprecated `Midplane` to `SideType = "Symmetric"`.
4. **attempt04**: once the solver ran properly, the Fixed joint rotated the grounded Seat 180° about Z. Two causes: the reference order (deviation 2) and the reversed X axis on the bottom face's JCS (deviation 1). Fix: pick the Seat first and use the arc-centre JCS.
5. **attempt05**: correct geometry, but "Link(s) to object(s) 'Slice_child1' go out of the allowed scope". Chamfer and Fillet referenced the external slice piece. Fix: reference the body's internal `PartDesign::FeatureBase`, which is what a 3D-view pick selects.
6. **attempt06**: clean. Two fixes were needed in the verifier itself: the fast bounding box (deviation 5) and the default-STL settings, which the 1.1.4 source showed to be 0.1 mm / 0.1 rad rather than the OCCT default of 0.5 rad.

### Estimated clicks and keystrokes for a GUI user

How the estimate was made:
* Step-by-step usage is taken from the FreeCAD 1.x wiki, read via the FreeCAD-documentation GitHub mirror because wiki.freecad.org returned bot-protection pages: Sketcher_NewSketch, PartDesign_NewSketch, Assembly_CreateJointFixed, Assembly_InsertLink, Assembly_ToggleGrounded, Part_SliceApart, Mesh_FromPartShape, Mesh_Export, PartDesign_Hole, Std_Placement.
* Every sketch dimension is counted as Dimension tool + 2 picks + value + Enter.
* Plane offsets are set in the property editor (Attachment > Attachment Offset). PartDesign_NewSketch's "Select feature" panel has no offset field.
* Typed values count one key per character plus Enter.
* View rotation by mouse is not counted.
* Using the 1.x on-view parameters while drawing rectangles could save roughly 50 to 80 clicks.

| Stage | GUI step | Clicks | Keys | How counted |
|---|---|---:|---:|---|
| A | New document, switch to PartDesign, Create body, rename 'Seat' | 4 | 7 | Ctrl+N; WB dropdown 2; body 1; tree select 1 + F2 'Seat' Enter |
| A | S1 sketch on XY_Plane + attachment offset z=430 | 11 | 4 | Create sketch 1, pick XY_Plane 1, OK 1; leave sketch 1; property editor Attachment>Offset>z 4 + '430' Enter; double-click to re-enter 1; (+2 view) |
| A | S1 rectangle + 4 dimensions (210, 420, 200, 200) | 15 | 16 | rect tool 1 + 2 corners; Dimension tool 4x(1 tool+2 picks) + value Enter |
| A | S1 sketch fillets R20 x2 + 2 radius dims, close | 10 | 6 | fillet tool 1 + 2 lines x2; radius dim 2x(1+1) + '20' Enter; Close 1 |
| A | S1 Pad 30 | 3 | 2 | tool, length field, OK |
| A | S2 Fillet R6 on top front edge | 4 | 1 | pick edge, tool, radius field '6', OK |
| A | S3 sketch on XZ_Plane + offset -175 | 11 | 5 | as S1 ('-175') |
| A | S3 rectangle + 4 dims, close | 16 | 16 |  |
| A | S3 Pad 25 Reversed | 4 | 2 | tool, field, Reversed checkbox, OK |
| A | S4 sketch on backrest front face | 2 | 0 | pick face, Create sketch (instant) |
| A | S4 rectangle + 4 dims, close | 16 | 15 |  |
| A | S4 Pocket Through all | 4 | 0 | tool, type dropdown 2, OK |
| A | Create body, rename 'LegFrame' | 2 | 10 |  |
| A | L1 sketch on XY_Plane | 3 | 0 | tool, XY_Plane, OK |
| A | L1 four rectangles + 16 dims, close | 61 | 56 | 4x(rect 3 + 4 dims 12); Close 1 |
| A | L1 Pad 430 | 3 | 3 |  |
| A | L2 rail sketches x2 (YZ_Plane + offset +-170), rect + 4 dims each, close | 54 | 39 | 2x(attach+offset 11, rect+dims 15, close 1) |
| A | L2 Pad 20 Symmetric x2 | 10 | 4 | 2x(tool, field, Symmetric dropdown 2, OK) |
| A | L3 cross-rail sketch (XZ_Plane + offset 160), rect + 4 dims, close | 27 | 19 |  |
| A | L3 Pad 20 Symmetric | 5 | 2 |  |
| A | Assembly WB, Create assembly, Insert component (tick 2), Ground Seat | 8 | 1 | WB 2, create 1, insert tool 1 + 2 ticks + OK 1, select 1 + 'G' |
| A | Fixed joint: pick Seat arc (centre), leg top face (corner), OK | 4 | 0 | hover-snap to JCS |
| A | Save | 0 | 7 | Ctrl+S, 'chair' Enter |
| B | Part WB, select LegFrame + its YZ_Plane (expand origin), Part > Split > Slice apart | 8 | 0 |  |
| B | PartDesign WB, 2x (select slice piece, Create body, rename LegLeft/LegRight) | 8 | 20 | WB 2 + 2x(expand/select 2, body 1) ; F2 + name + Enter |
| B | Assembly WB, delete LegFrame link (confirm), Insert component (tick 2) | 8 | 1 |  |
| B | Edit Fixed joint, re-pick Seat left arc + LegLeft leg top, OK | 5 | 0 |  |
| C | PartDesign WB, activate LegLeft, bottom view, pick 8 edges, Chamfer 3 | 13 | 2 | WB 2, dbl-click 1, '5' view, 8 picks, tool/field/OK 3 |
| C | Activate LegRight, pick 8 edges, Fillet R5 | 12 | 1 |  |
| C | Hole sketch on rail outer face: circle + diameter + 2 position dims, close | 13 | 13 | right view '3'; face 1, sketch 1, circle 3, dia 2, 2 pos dims 6 (point-on-axis/distance), close 1 |
| C | Hole: diameter 10, Through all | 5 | 2 | tool, diameter field, depth dropdown 2, OK |
| D | Assembly WB, delete joint, ground LegLeft + LegRight | 5 | 3 | WB 2, select+Del, 2x(select+'G') |
| D | Placement dialog LegLeft x=-15, LegRight x=+15 | 10 | 5 | 2x(select, Edit>Placement 2, X field, OK) |
| D | Placement dialog Seat: axis X, angle -5, centre (0,200,430) | 10 | 8 | select, menu 2, axis dropdown 2, angle, centre Y, centre Z, OK |
| D | Solve assembly | 1 | 0 |  |
| Export | Save As chair.FCStd | 1 | 7 |  |
| Export | Export STEP (select Assembly, File > Export, type filter, name, Save, options OK) | 6 | 11 |  |
| Export | Mesh WB, select 3 parts, Mesh_FromPartShape (Standard: 0.05 mm, 11.459 deg, relative off), OK | 9 | 10 | WB 2, 3 picks, tool, 2 fields, OK |
| Export | Mesh_Export chair.stl (3 meshes) + seat/leg_left/leg_right.stl | 11 | 46 | chair: 3 picks+tool+Save; 3x(pick, tool, Save)+names |
| Export | Export chair_default.stl (Assembly, File > Export, STL filter) | 5 | 18 |  |
| | **Total** | **407** | **362** | |
