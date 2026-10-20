# FabriCAD chair benchmark: test-user report

I built the model in `docs/comparison/CHAIR_SPEC.md` as a test user would, through the FabriCAD
command line in script mode (`fabricad --script chair.fcs --offscreen`, docs/SCRIPTING.md), with the
Release build in `build/` (binary of 2026-10-20 03:10, tree at e3d7a93). I wrote no C++ against the
internal APIs and changed no FabriCAD source. The only source change is the new regression test
under `tests/chair/`, plus one `add_subdirectory(tests/chair)` line in the top-level CMakeLists.txt
that registers it.

**Result: every reference value is met.** All B-rep volumes are within 0.05 % and all final bounding
boxes within 0.01 mm. I checked them in the live model, again after reopening the saved .fcad, and again
after reading the exported STEP back. All six export files exist, and every STL is watertight with its
volume within 0.002 % of the reference. Three spec steps could not be done exactly as worded and needed
workarounds; they are listed under "Deviations". I also found 7 defects and several usability issues.

**Update:** the defects were fixed afterwards and the script, outputs and metrics were redone without
any workaround; see "After fixes" at the end. The sections up to there describe the state before.

## Files

| File | What |
|---|---|
| `chair.fcs` | the full script: model + CHECK assertions + re-open + STEP read-back (328 lines, 299 commands) |
| `chair_min.fcs` | the same modelling/export sequence without CHECK/INFO/LIST and without the read-back (181 commands) |
| `chair.log` | log of the final run, per-command status and wall time |
| `chair.run1-3.log`, `chair_min.log` | logs of the timing runs |
| `out/chair.fcad` | saved document |
| `out/chair.step` | XCAF assembly (`assembly.export-step`) |
| `out/chair.stl`, `seat.stl`, `leg_left.stl`, `leg_right.stl` | binary, mm, linear 0.05 mm, angular 0.2 rad, relative off |
| `out/chair_default.stl` | default settings (linear 0.05 mm, angular 20 deg = 0.349 rad, relative off, binary) |
| `out/check_stl.txt` | output of `tests/chair/check_stl.py` on out/ |
| `repro/*.fcs` (+ `.log`, `.after.log`) | minimal scripts that reproduce the defects below; a failing CHECK marks the defect. `.log`: the failing run before the fixes, `.after.log`: the same script with the fixed binary (all pass) |
| `../../../tests/chair/` | regression test: `chair.fcs` (same script), `check_stl.py`, `run_chair.cmake`, `CMakeLists.txt` |

Regression test: `ctest -R fabricad_chair_spec` (label `chair`). It runs the script headless in
`build/chair-out/`, starting from an empty out/. The script's CHECKs cover the B-rep volumes (0.05 %)
and boxes (0.01 mm) in the model, the reopened .fcad and the re-imported STEP. Then `check_stl.py`
(Python standard library only) checks that every export exists, and that each STL is binary,
closed/manifold/consistently oriented, has no degenerate triangles, has the expected number of shells,
has a volume within 0.5 % and a box within 0.05 mm. The test passes in 4.9 s. I checked that it fails
when the model is wrong (hole Ø12: `CHECK ... radius ~ 5` fails) and when a mesh is open (one triangle
removed: "not watertight").

## Verification against CHAIR_SPEC.md

B-rep volumes (mm³). The value read back from the exported STEP is the same to the digits shown.

| Part | Reference | FabriCAD (model) | FabriCAD (STEP re-import) | Deviation |
|---|---|---|---|---|
| Seat (after A) | ≈ 8,565,976 | 8,565,975.690 | 8,565,975.690 | −0.000004 % |
| LegFrame (after A) | 3,440,000 | 3,440,000.000 | – | 0 |
| LegLeft / LegRight (after B) | 1,720,000 | 1,720,000.000 / 1,720,000.000 | – | 0 |
| LegLeft (final) | 1,718,632 | 1,718,632.000 | 1,718,632.000 | 0 |
| LegRight (final) | ≈ 1,716,808 | 1,716,808.259 | 1,716,808.259 | +0.000015 % (the analytic value is 1,716,808.26) |

Final world bounding boxes: every face passes `CHECK ... ~ value 0.0005`, so each deviation is
< 0.0005 mm. This holds in the model, the reopened .fcad and the re-imported STEP.

| Part | X | Y | Z |
|---|---|---|---|
| Seat | [−210, 210] | [−198.478, 235.734] | [430.000, 840.619] |
| LegLeft | [−205, −15] | [−180, 180] | [0, 430] |
| LegRight | [15, 205] | [−180, 180] | [0, 430] |

Intermediate checks in the script: S1 volume 5,034,849.6 (= (168000 − 2·(400 − 100π))·30). S2 makes
5 fillet faces: the front line, both R20 arcs and both side edges. The rear top edge stays a straight
edge, 408 mm long (the side fillets run out at the rear face). The cross rail face is at y = −170. The
hole is a cylinder r = 5, axis −X, centre (170, 0, 150), 20 mm long. Each side has 8 bottom edges.
After the split, the LegLeft box ends at xmax = 0 and the LegRight box starts at xmin = 0.

STL meshes, from `check_stl.py`:

| File | Triangles | Shells | Volume | vs reference | Open / non-manifold edges |
|---|---|---|---|---|---|
| chair.stl | 2026 | 3 | 12,001,329.3 | −0.0007 % | 0 |
| seat.stl | 1314 | 1 | 8,565,892.4 | −0.0010 % | 0 |
| leg_left.stl | 76 | 1 | 1,718,632.0 | 0.0000 % | 0 |
| leg_right.stl | 636 | 1 | 1,716,804.9 | −0.0002 % | 0 |
| chair_default.stl | 1438 | 3 | 12,001,213.1 | −0.0017 % | 0 |

Tessellation parameters FabriCAD actually used (`BRepMesh_IncrementalMesh(shape, lin, relative=false,
ang, parallel=true)` + `StlAPI_Writer`, binary). The app reported them in the log, e.g.
"STL mesh: linear deflection 0.05 mm, angular deflection 11.459155902616466 deg (0.20000000000000004 rad), binary":

* chair/seat/leg_left/leg_right.stl: linear 0.05 mm absolute, angular 0.2 rad, relative off, binary, mm.
* chair_default.stl: the defaults, linear 0.05 mm absolute, angular 20° (0.349 rad), relative off,
  binary, mm. **But** see F2: in the modelling session this file would have silently reused the
  0.2 rad mesh, so the script exports it from a freshly opened copy of the saved document.

## Deviations from the spec wording (all forced by the command interface; the geometry still matches)

1. **Stage D Seat rotation is a body Move, not a component transform.** `MOVECOMPONENT` accepts only
   `x,y,z` (translate) and `@angle` (rotate about Z through the component's bounding-box centre)
   from the command line. `OPTIONS` lists no properties. The GUI dialog's rx/ry/rz also turn about that
   centre, so a turn about the axis (0,200,430)+X cannot be entered directly. Workaround: `MOVE` (body
   Move/Copy), `moveType=Rotate`, `INPUT axis body:Seat/edge:@0,200,430` (the rear-bottom edge, which
   is exactly that axis), `angle=5`. The edge runs in −X, so +5° about it equals −5° about +X; the
   first try with −5 tilted the seat the wrong way. As a result, the Seat component's placement is the
   identity and the rotation is a Move feature in Seat's timeline. In the STEP the rotation is part of
   the Seat shape, not of the instance transform. LegLeft/LegRight are real component translations.
2. **LegFrame is left empty, not removed.** `CFB` creates LegLeft/LegRight as *children of LegFrame*.
   `DELETECOMPONENT` on LegFrame then deletes them too (F4). The STEP structure is therefore
   root → {Seat, LegFrame → {LegLeft, LegRight}} ("3 parts, 4 instances"). The spec allows "removed
   or left empty".
3. **The rigid joint is component-to-component.** `ASBUILTJOINT` takes the two components and has no
   face inputs. The spec's "top face of a leg / bottom face of the seat" cannot be recorded, but the
   result is the same: rigid, as built, zero offset, nothing moves.

Choices the spec left open: the L2 rails use one sketch per side, on YZ offset planes at x = ±170,
extruded symmetric 20 (whole length). The L3 rail is a sketch on XZ offset to y = −150, extruded 20
along the plane normal (−Y).

## Defects found (command, expected, actual, workaround). Repro scripts are in `repro/`

**F1 (high): the first Extrude in an empty component joins into another component's body.**
`NEWCOMPONENT LegFrame`/`OK` (LegFrame active), sketch the four squares on XY, select the 4 profiles,
`EXTRUDE 430`, `OK`. Expected: a new body in LegFrame, as Fusion does in an active component.
Actual: `operation` stays on its automatic default, Join. The legs touch the Seat's underside at
z = 430, so they are merged into body Seat (component Seat), with no message:
`body:Seat ... volume 11317975.69` (= 8,565,976 + 2,752,000), and LegFrame has 0 bodies.
Workaround: `operation=New Body`. Repro: `repro/extrude_autojoin_other_component.fcs`.

**F2 (high): EXPORTSTL silently reuses an existing triangulation, so the result depends on export order.**
`EXPORTSTL a.stl` (defaults: 0.05 mm / 20°) then `EXPORTSTL b.stl deflection=0.05 angle=0.2rad`:
both files are 71,984 bytes. The second export should be 101,384 bytes, which is what a fresh session
produces. In the other order, the "default" file gets the fine 0.2 rad mesh (101,384 bytes instead
of 71,984). In both cases the status message reports the requested angle, not the one used.
Cause, from the code: `exportStl` calls `BRepMesh_IncrementalMesh` on the shape that already holds
a triangulation. OCCT keeps the existing mesh when the linear deflection is not coarser and ignores
the angle. The fix would be `BRepTools::Clean` on a copy first. In the GUI, the viewer's display mesh
may play the same role. Workaround: export the 0.2 rad files first, then `OPEN out/chair.fcad`
(new tab, fresh shapes) and export `chair_default.stl` there. Repro: `repro/stl_mesh_reuse.fcs`.

**F3 (medium): a through-all Hole drills every body on its axis, including other components.**
`HOLE`, `INPUT face body:LegRight/face:@180,0,150`, `PICK 180,0,150 nosnap`, `diameter=10`,
`extentType=All`, `OK`. Expected (spec): only LegRight's 20 mm rail. Actual: `targets=(none)` means
every body, so LegLeft's rail at x = −170 is drilled too, with no message:
`CHECK body:LegLeft volume ~ 1718632 0.05%: FAILED (got 1.71706e+06)` (−1,570.8).
Workaround: `INPUT targets body:LegRight`. Repro: `repro/hole_through_all_cuts_other_component.fcs`.

**F4 (medium): DELETECOMPONENT on a parent deletes its child components without warning; their bodies
drop into the root.** `SELECT component:LegFrame` (0 bodies, children LegLeft/LegRight), `DELETECOMPONENT`.
Actual: `LIST components` shows only root and Seat. LegLeft/LegRight become root bodies at the
untransformed position (`body:LegLeft xmin` = −190 instead of −205). `LIST components` and
`INFO component:` never show the parent, so nothing tells the user that LegLeft is inside LegFrame.
Workaround: keep LegFrame. Repro: `repro/deletecomponent_removes_children.fcs`.

**F5 (medium): string properties cannot be typed; `IMPORT <file>` ignores its argument.**
`IMPORT out/chair.step` starts the command with `filePath=''` and gives no warning. Then
`filePath=out/chair.step` gives `warn: Unknown command: filePath=out/chair.step`:
`FeatureTool::commandLineInput` handles only numeric, enum and bool properties. `OK` then gives
`OK is disabled for Insert STEP/STL`. Workaround: `IMPORTFILE out/chair.step`, which takes the path
as an argument and inserts at once. Repro: `repro/import_path.fcs`.

**F6 (medium): the STEP assembly loses the Seat part name.** In `out/chair.step` the Seat instance is
`NEXT_ASSEMBLY_USAGE_OCCURRENCE('1','Seat',...)`, but its PRODUCT is named
`'Open CASCADE STEP translator 7.9 1.1'`. LegFrame, LegLeft and LegRight keep their names. The root
product is "Untitled" because the document was saved only after the export.

**F7 (medium): the GUI Export STL has no tessellation settings.** `part.export-stl` reads
deflection/angle only from command-line arguments. From the ribbon you get a file dialog and the
defaults (0.05 mm / 20°), so the spec's 0.2 rad needs the typed `EXPORTSTL ... angle=0.2rad`.

Usability / minor:

* U1: `GROUND` is a toggle with no explicit on/off form. My first draft grounded all three parts and
  thereby *ungrounded* Seat ("info: Ungrounded"). A script cannot say "make sure it is grounded".
* U2: the split result is named `LegFrame (2)`, with a space. Selectors have no quoting, so it can be
  addressed only by `body:#44` or `body:@x,y,z`. STEP import names its bodies `chair 1..3` (same
  problem) and flattens the assembly into root bodies.
* U3: the origin XZ plane's normal is −Y, so `OFFSETPLANE 175` on XZ lands at y = −175 and needs
  `flip=true`. Sketches on XZ-parallel planes extrude toward −Y by default. `INFO sketch:` shows this
  clearly, but Fusion users expect +Y.
* U4: preselecting a face before `HOLE` puts it into "Placement points" (`face=(none)`). It works,
  but `OPTIONS` makes it look like the start face is missing.
* U5: performance. `GROUND` and `MOVECOMPONENT`/`OK` take about 240 ms each, the same as a full
  `COMPUTE` (266-280 ms). A placement-only change seems to recompute the whole B-rep history. These
  5 lines are 1.2 s of the 4.4 s run.
* U6: OCCT's "Statistics on Transfer (Write)" banner (with ANSI colours) goes to stdout even with
  `--quiet`.

## Measurements

Machine: 8 threads, other agents running (load average about 1.9). Release build, `--offscreen`.

| Metric | Value |
|---|---|
| App startup + shutdown (script with one `ECHO`, 5 runs) | 0.224 / 0.228 / 0.235 / 0.240 / 0.240 s, **median 0.235 s** |
| Full `chair.fcs` wall-clock (3 runs) | 4.936 / 4.702 / 4.675 s, **median 4.70 s** (in-app sum of command times 4.43-4.66 s) |
| `chair_min.fcs` wall-clock (model + exports only, 3 runs) | 4.130 / 4.104 / 4.063 s, **median 4.10 s** |
| Lines / commands | chair.fcs: 328 lines, 299 commands (116 of them CHECK/INFO/LIST). chair_min.fcs: 181 commands |
| Distinct commands used (chair_min) | 28: SKETCH, FINISH, RECT, SFILLET, OFFSETPLANE, EXTRUDE, FILLET, CHAMFER, HOLE, SPLIT, MOVE, NEWCOMPONENT, ACTIVATE, ASBUILTJOINT, GROUND, CFB, MOVECOMPONENT, DELETE, RENAME, EXPORTSTL, assembly.export-step, SAVE, OPEN + shell verbs SELECT, PICK, INPUT, OK, CANCEL. CHECK/INFO/LIST/IMPORTFILE/NEW are used for verification |
| Distinct properties typed | 8: operation, flip, extentType, direction, symmetricMeasure, diameter, moveType, angle |
| Selector kinds | plane:, profile:@, body:, body:@, face:front@, face:@, face:cylinder@, edge:@, edges:z=0, component:, joint: |
| Development runs | about 37 runs of the growing script, plus 4 side experiments (STL order, tight tolerances, recompute timing, negative tests) |
| Failed attempts / iterations | **14**: 11 caused by app behaviour (F1, F2, F3, F4, F5 ×2: IMPORT path then filePath=, U1, U3, U4, MOVECOMPONENT rotation, rotation sign) and 3 my own mistakes (expected `components == 5`, but the count excludes the root; a sed slip; expected 420 for the rear edge, which is 408) |

Time per stage (run 2; each line includes the event-loop settle):

| Stage | Commands | ms |
|---|---|---|
| A Seat | 61 | 584 |
| A LegFrame | 76 | 1086 |
| A assembly | 10 | 190 |
| B split | 18 | 92 |
| C leg features | 27 | 449 |
| D transforms | 30 | 1171 (5 placement/ground lines ≈ 1200 of it, U5) |
| final checks | 21 | 129 |
| exports + save | 16 | 100 |
| reopen + default STL | 12 | 329 |
| STEP read-back + checks | 28 | 335 |

Slowest single lines: OPEN 268 ms, GROUND 253 / 238 / 146 ms, MOVECOMPONENT OK 244 / 239 ms,
EXTRUDE 430 (4 legs, preview) 118 ms, DELETE joint 104 ms. Every modelling feature takes 20-120 ms.

## GUI effort estimate (same steps, FabriCAD GUI)

This is counted from the tool code and README, not measured in a live session. There is no display
here, and the OCCT view needs XCB.

Conventions:

* A click is one mouse press: a ribbon button, a dropdown item, a viewport/browser pick, a checkbox or
  a field focus. A keystroke is one key. A chord such as Shift+J or Ctrl+Return counts as 1, and
  Shift/Ctrl held while clicking counts as nothing.
* Shortcuts are used where they exist: N, E, F, H, M, R, Shift+J, Ctrl+Return, Del, F2, Ctrl+S, Ctrl+9.
* Feature value boxes take typed numbers at the arrow tip, and Enter = OK.
* Rectangle width/height go into the sketch value boxes (`420 Tab 400 Enter`). The first corner's
  absolute position is typed into the command line (`:` then `x,y` Enter): typed `x,y` in a sketch
  is the only exact way without adding dimensions. A Fusion-style "draw roughly, then dimension"
  would add about 2 dimensions × (D + 2 picks + placement + value) per rectangle, about 70 clicks and
  40 keys more.
* Unpromoted tools (Split Body, Create Components from Bodies, Move Component, export) cost 2 clicks
  (dropdown + item).

| # | Stage | Step | Clicks | Keys |
|---|---|---|---|---|
| 1 | A | New component Seat: N, type Seat, Enter | 0 | 6 |
| 2 | A | Offset plane +430: ribbon Offset Plane, expand Origin in browser, click XY, type 430, Enter | 3 | 4 |
| 3 | A | Create sketch on OffsetPlane1: CS button, click plane | 2 | 0 |
| 4 | A | Rectangle: R; corner typed in command line (: -210,-200 Enter); value boxes 420 Tab 400 Enter | 0 | 20 |
| 5 | A | Sketch fillet R20: Fillet button, type 20 Enter, click 4 lines (2 per front corner), Esc | 5 | 4 |
| 6 | A | Finish sketch (Ctrl+Return) | 0 | 1 |
| 7 | A | Extrude 30: click profile, E, 30, Enter | 1 | 4 |
| 8 | A | Rename body Seat: expand Seat > Bodies (2), click body, F2, Seat, Enter | 3 | 6 |
| 9 | A | Fillet R6: click top front edge, F, 6, Enter (tangent chain on by default) | 1 | 3 |
| 10 | A | Offset plane y=175: button, click XZ, 175, Flip checkbox, Enter | 3 | 4 |
| 11 | A | Sketch on OffsetPlane2 | 2 | 0 |
| 12 | A | Rectangle: R, : -210,430 Enter, 420 Tab 410 Enter | 0 | 19 |
| 13 | A | Finish sketch | 0 | 1 |
| 14 | A | Extrude backrest: click profile, E, 25, Flip, Operation dropdown -> Join, Enter | 4 | 4 |
| 15 | A | Sketch on backrest front face: CS, click face | 2 | 0 |
| 16 | A | Rectangle: R, : -150,680 Enter, 300 Tab 60 Enter | 0 | 18 |
| 17 | A | Finish sketch | 0 | 1 |
| 18 | A | Extrude cut: click profile, E, Extent -> All, Flip, Operation -> Cut, Enter | 6 | 2 |
| 19 | A | Activate root: RMB root in browser, Activate Component | 2 | 0 |
| 20 | A | New component LegFrame: N, LegFrame, Enter | 0 | 10 |
| 21 | A | Sketch on XY: CS, click XY | 2 | 0 |
| 22 | A | 4 rectangles: R; per square : x,y Enter, 40 Tab 40 Enter | 0 | 65 |
| 23 | A | Finish sketch | 0 | 1 |
| 24 | A | Extrude legs: click 4 profiles (Shift), E, 430, Operation -> New Body (needed, see F1), Enter | 6 | 5 |
| 25 | A | Rename body LegFrame: expand Bodies, click body, F2, LegFrame, Enter | 2 | 10 |
| 26 | A | Offset plane x=+170: button, click YZ, 170, Enter | 2 | 4 |
| 27 | A | Sketch + rectangle (: -140,130 Enter, 280 Tab 40 Enter) + finish | 2 | 19 |
| 28 | A | Extrude rail: profile, E, 20, Direction -> Symmetric, Measurement -> Whole Length, Operation -> Join, Enter | 7 | 4 |
| 29 | A | Offset plane x=-170: button, click YZ, 170, Flip, Enter | 3 | 4 |
| 30 | A | Sketch + rectangle + finish | 2 | 19 |
| 31 | A | Extrude rail (same dialog choices) | 7 | 4 |
| 32 | A | Offset plane y=-150: button, click XZ, 150, Enter | 2 | 4 |
| 33 | A | Sketch + rectangle (: -150,130 Enter, 300 Tab 40 Enter) + finish | 2 | 19 |
| 34 | A | Extrude cross rail: profile, E, 20, Operation -> Join, Enter | 3 | 4 |
| 35 | A | Ground Seat: RMB Seat, Ground | 2 | 0 |
| 36 | A | As-built joint: Shift+J, click LegFrame, click Seat (browser), Enter | 2 | 2 |
| 37 | B | Split body: MODIFY dropdown -> Split Body, click LegFrame body, click Splitting tool field, click YZ, Enter | 5 | 1 |
| 38 | B | Rename the two bodies (click, F2, name, Enter) x2 | 2 | 19 |
| 39 | B | Components from bodies: click both bodies (Ctrl), ASSEMBLE dropdown -> Create Components from Bodies | 4 | 0 |
| 40 | C | Chamfer: Bottom view (Ctrl+6), click 8 bottom edges of LegLeft, Chamfer button, 3, Enter | 9 | 3 |
| 41 | C | Fillet: click 8 bottom edges of LegRight, F, 5, Enter | 8 | 3 |
| 42 | C | Hole: sketch on rail face (CS, face), Point tool, typed point (: 0,150 Enter), finish; H, click point, Diameter field 10, Extent -> All, Bodies to cut field, click LegRight, Enter | 10 | 14 |
| 43 | D | Delete joint: click Joint1, Del | 1 | 1 |
| 44 | D | Move LegLeft: click component, ASSEMBLE dropdown -> Move Component, X field, -15, Enter | 4 | 4 |
| 45 | D | Move LegRight: same, 15 | 4 | 3 |
| 46 | D | Rotate Seat body: click body, M, Move type -> Rotate, Axis field, click rear-bottom edge, Angle field, 5, Enter | 7 | 3 |
| 47 | D | Ground LegLeft, LegRight: RMB + Ground x2 | 4 | 0 |
| 48 | E | Assembly STEP: TOOLS tab, EXPORT dropdown -> Export STEP (assembly), file name out/chair.step, Enter | 3 | 15 |
| 49 | E | 4 STL with 0.2 rad: no GUI field for the angle (F7) -> Ctrl+9 + typed EXPORTSTL line (+ click body in browser for the 3 part files) | 3 | 218 |
| 50 | E | Save: Ctrl+S, out/chair.fcad, Enter | 0 | 16 |
| 51 | E | chair_default.stl: reopen the file (Ctrl+O, name, Enter) (F2 workaround), TOOLS > Export STL, out/chair_default.stl, Enter | 2 | 39 |
| | | **Total (51 steps)** | **144** | **610** |

By stage (clicks / keys): A 78 / 271, B 11 / 20, C 27 / 20, D 20 / 11, exports + save 8 / 288. About
290 of the keys are file names and the four typed `EXPORTSTL ... angle=0.2rad` lines that F7 makes
necessary. Without them, and with F2 fixed, the model itself is about **140 clicks + 320 keys**. In the
same terms, the script is 181 typed lines.

## After fixes

The seven defects and the usability points U1, U2, U5 and U6 above were fixed in the FabriCAD
source (round 3, the commits after c5260e7; Release build in `build/`, binary of 2026-10-20 12:54).
`chair.fcs` was then rewritten so that every step is typed as the spec words it: the three
"Deviations" and every workaround line are gone. The file in this directory is identical to
`tests/chair/chair.fcs`, which `ctest -R fabricad_chair_spec` runs; `chair_min.fcs` is the same
sequence without CHECK/INFO/LIST/OPTIONS and without the read-back. All files in `out/`, the logs and
`out/check_stl.txt` were regenerated with the fixed Release binary. Dev and Release builds pass all
54 ctest entries.

### What changed

| Defect | Fix |
|---|---|
| F1 extrude auto-joins into another component | Booleans are scoped to the feature's component tree (`part::bodiesInScope`): without an explicit target list a Join/Cut only touches bodies of the active component and its children, and the dialog default is Join only when that tree already has a body (else New Body). `EXTRUDE 430` / `OK` in the empty LegFrame now makes a new body although the legs touch the Seat. |
| F2 EXPORTSTL reuses an old triangulation | `exportStl` meshes a fresh `BRepBuilderAPI_Copy` of the shapes every time, so the written mesh depends only on the requested deflections. `chair_default.stl` is now exported in the same session after the 0.2 rad files and is coarser (71,884 vs 101,684 bytes), which the script checks. |
| F3 through-all Hole drills other components | With no "Bodies to cut" a Hole cuts the body it is placed on (the owner of the start face / picked point) only. LegLeft's rail stays at 1,718,632 mm³ without `INPUT targets`. |
| F4 DELETECOMPONENT deletes child components | `deleteComponent` re-parents the children to the deleted component's parent with the same world placement (new `Document::reparentComponent`), re-homes the features made inside it, removes the joints on it and reports "Deleted component 'LegFrame' (2 child components moved to the root)"; undo restores the tree. `LIST components` prints the tree indented and `INFO component:` shows parent, children and the placement as translation + XYZ angles. LegFrame is now removed as the spec says; the STEP is root → {Seat, LegLeft, LegRight}, 3 parts, 3 instances. |
| F5 `IMPORT <file>` ignores its argument; string properties cannot be typed | `PartFeature::primaryText()` ("filePath" for Import): the first command-line argument and a bare typed value go there, and `name=value` now also sets String properties. `IMPORT out/chair.step` / `OK` reads the STEP back. |
| F6 STEP loses the Seat part name | A body carrying a `TopLoc_Location` is baked into its geometry before `XCAFDoc_ShapeTool::AddShape` and the prototype label gets the part name too; an untitled root is named after the file. `check_stl.py` now also reads the STEP's PRODUCT names: `chair`, `Seat`, `LegLeft`, `LegRight`, no OCCT default name, no LegFrame. |
| F7 GUI Export STL has no settings | New `ExportStlTool` (`part.export-stl`): from the ribbon a dialog with file, linear deflection (mm), angular deflection (deg) and Binary/ASCII, remembered in QSettings. Typed `EXPORTSTL file [deflection=] [angle=20\|0.2rad] [ascii]` still exports at once from the built-in defaults, and the same options can be typed while the dialog is open. |
| Deviation 1 (Seat rotation as a body Move) | `MOVECOMPONENT` takes `x= y= z= rx= ry= rz=` (one dialog field each) and `pivot=x,y,z`, the world point the angles turn about; the dialog has Pivot X/Y/Z fields. `pivot=0,200,430` / `rx=-5` is the spec's rotation as a component transform: the Seat placement carries it (origin at (0, −36.716, 19.067)) and so does the STEP instance. |
| Deviation 2 (LegFrame left empty) | Gone with F4: `DELETECOMPONENT` on LegFrame keeps LegLeft/LegRight at their placements. |
| Deviation 3 (as-built joint without faces) | `ASBUILTJOINT` accepts a face/edge on each component (`SELECT body:LegFrame/face:@-170,-160,430 body:Seat/face:bottom`); the components are taken from the geometry. |
| U1 GROUND toggles | `GROUND` is idempotent (`GROUND on\|off\|toggle`; `UNGROUND` / `assembly.unground` is its own hidden tool with an icon) and says "Already grounded" when nothing changes, so grounding all three parts at once is safe. |
| U2 names with spaces | The command line has quoting: `"..."` / `'...'` keep spaces in one word (`SELECT body:"LegFrame (2)"`, `SAVE "my chair.fcad"`). |
| U5 GROUND / MOVECOMPONENT recompute everything | `recomputeJoints`: a placement or ground change replays the timeline from the first active joint only (body features do not depend on placements); with no joint nothing is recomputed. `GROUND` and `MOVECOMPONENT` / `OK` went from about 240 ms to about 5 ms. |
| U6 OCCT banner on stdout | OCCT's default messenger printers are raised to warning level in `main`, so the STEP "Statistics on Transfer" banner is gone. |

Tests added for the fixes: `fabricad_part_test_tools` (booleans stay inside the active component, STL
meshes afresh, IMPORT takes path arguments), `fabricad_assembly_test_assembly_features` (delete
component keeps children and placements; STEP keeps the names of moved bodies and names the root),
`fabricad_assembly_test_assembly_tools` (ground on/off/undo, Move Component pivot and named inputs),
`fabricad_core_test_tool` (quoted words), `fabricad_app_test_scripting` (GROUND / DELETECOMPONENT /
LIST components / quoted names from scripts) and `fabricad_chair_spec` itself. The repro scripts in
`repro/` were re-run with the fixed binary: every CHECK that marked a defect passes
(`repro/*.after.log`, 0 failed in all five; the original `*.log` files keep the failing runs).

### Updated verification

B-rep volumes (mm³), checked in the model, after reopening the .fcad and after reading the STEP back.
The CHAIR_SPEC.md reference values are unchanged and met.

| Part | Reference | FabriCAD | Deviation |
|---|---|---|---|
| Seat (after A, unchanged to the end) | ≈ 8,565,976 | 8,565,975.690 (±0.001 in the model and the .fcad, ±0.01 from the STEP) | −0.000004 % |
| LegFrame (after A) | 3,440,000 | 3,440,000 (`CHECK ... == 3440000`) | 0 |
| LegLeft / LegRight (after B) | 1,720,000 | 1,720,000.0 / 1,720,000.0 (0.01 %) | 0 |
| LegLeft (final) | 1,718,632 | 1,718,632.000 (±0.001) | 0 |
| LegRight (final) | ≈ 1,716,808 | 1,716,808.259 (±0.001) | +0.000015 % (analytic 1,716,808.26) |

Final world bounding boxes: the script's `CHECK ... ~ value 0.01` lines pass for every face of the
three parts in the model, in the reopened .fcad and in the re-imported STEP, and `check_stl.py` finds
the same boxes in the meshes (within 0.05 mm): Seat X[−210, 210] Y[−198.478, 235.734] Z[430.000,
840.619]; LegLeft X[−205, −15] Y[−180, 180] Z[0, 430]; LegRight X[15, 205] Y[−180, 180] Z[0, 430].
The Seat rotation is now a component placement, so the STEP instance of `Seat` carries the −5°
transform; `out/chair.step` has the PRODUCTs `chair`, `Seat`, `LegLeft`, `LegRight` and exactly three
instances.

STL meshes (`out/check_stl.txt`): all binary, all watertight (0 open / non-manifold edges, 0 degenerate
triangles). Tessellation actually used (from the log): `chair/seat/leg_left/leg_right.stl` linear
0.05 mm absolute, angular 0.2 rad (11.459°), relative off; `chair_default.stl` the built-in defaults
0.05 mm / 20° (0.349 rad), exported in the same session right after the fine files.

| File | Triangles | Shells | Volume | vs reference | Open / non-manifold edges |
|---|---|---|---|---|---|
| chair.stl | 2032 | 3 | 12,001,329.4 | −0.0007 % | 0 |
| seat.stl | 1320 | 1 | 8,565,892.5 | −0.0010 % | 0 |
| leg_left.stl | 76 | 1 | 1,718,632.0 | 0.0000 % | 0 |
| leg_right.stl | 636 | 1 | 1,716,804.9 | −0.0002 % | 0 |
| chair_default.stl | 1436 | 3 | 12,001,213.2 | −0.0017 % | 0 |

### Updated measurements

Same machine (8 threads, other processes running, load average 2.1–2.7). Release build, `--offscreen`.

| Metric | Before | After |
|---|---|---|
| App startup + shutdown (one `ECHO`, 5 runs) | median 0.235 s | 0.216 / 0.227 / 0.227 / 0.232 / 0.248 s, **median 0.227 s** |
| Full `chair.fcs` wall-clock (3 runs) | median 4.70 s | 3.547 / 3.552 / 3.595 s, **median 3.55 s** (in-app sum of command times 3.30–3.35 s) |
| `chair_min.fcs` wall-clock (model + exports only, 3 runs) | median 4.10 s | 2.588 / 2.611 / 2.625 s, **median 2.61 s** (in-app 2.36 s) |
| Lines / commands | 328 lines, 299 commands (116 CHECK/INFO/LIST) | chair.fcs: 337 lines, 304 commands (130 CHECK/INFO/LIST/OPTIONS); chair_min.fcs: 170 commands (was 181) |
| Workarounds in the script | 3 deviations, 4 workaround lines, 1 extra re-open | **0** |
| Distinct commands (chair_min) | 28 | 27: `MOVE` (body) and `OPEN` are gone, `DELETECOMPONENT` is new |
| Distinct properties typed | 8 | 8: operation, flip, extentType, direction, symmetricMeasure, diameter, pivot, rx (`moveType`, `angle` and `INPUT targets` are gone) |

Time per stage (run 3, `chair.log`; each line includes the event-loop settle):

| Stage | Commands | ms before | ms after |
|---|---|---|---|
| A Seat | 61 | 584 | 578 |
| A LegFrame | 72 | 1086 | 831 |
| A assembly | 11 | 190 | 70 |
| B split | 18 | 92 | 96 |
| C leg features | 26 | 449 | 403 |
| D transforms | 33 | 1171 | 402 (U5: the 5 placement/ground lines are now 30 ms in all) |
| final checks | 21 | 129 | 127 |
| exports + save | 21 | 100 | 146 (the default STL is exported here now) |
| reopen (+ default STL before) | 10 | 329 | 362 |
| STEP read-back + checks | 31 | 335 | 334 |

Slowest single lines: OPEN 306 ms, DELETECOMPONENT 191 ms (one full recompute), DELETE joint 109 ms,
FILLET 5 82 ms, EXTRUDE 20 68 ms. GROUND and MOVECOMPONENT / OK are 5–7 ms.

### Updated GUI effort estimate

Same conventions as above (counted from the tool code, not measured). Only the steps that changed
are listed; the other 45 steps are as in the first table.

| # | Stage | Step | Before (clicks / keys) | After (clicks / keys) | Why |
|---|---|---|---|---|---|
| 24 | A | Extrude legs: click 4 profiles (Shift), E, 430, Enter | 6 / 5 | 4 / 5 | F1: no Operation → New Body needed |
| 36 | A | As-built joint: Shift+J, click a leg top face, click the seat bottom face (viewport), Enter | 2 / 2 | 2 / 2 | faces instead of browser components, same count |
| 42 | C | Hole: sketch point, H, click point, Diameter 10, Extent → All, Enter | 10 / 14 | 8 / 14 | F3: no "Bodies to cut" field and pick |
| 46 | D | Rotate Seat: click component, ASSEMBLE → Move Component, Pivot fields `0 Tab 200 Tab 430`, RX field, `-5`, Enter | 7 / 3 | 5 / 12 | component transform with a typed pivot instead of the body Move with an axis pick |
| 47b | D | Delete LegFrame: RMB LegFrame, Delete Component | – | 2 / 0 | F4: the children survive, so the spec's "removed" is possible |
| 49 | E | 4 STL at 0.2 rad: TOOLS → Export STL dialog, file name, Angular field `11.459`, Enter; then per part: click body, dialog (settings remembered), file name, Enter | 3 / 218 | 16 / 68 | F7: dialog instead of four typed `EXPORTSTL ... angle=0.2rad` lines |
| 51 | E | chair_default.stl: Esc, TOOLS → Export STL, file name, Angular `20`, Enter | 2 / 39 | 4 / 25 | F2: no re-open of the file |
| | | **Total (52 steps)** | **144 / 610** | **155 / 455** | |

By stage (clicks / keys): A 76 / 271, B 11 / 20, C 25 / 20, D 20 / 20, exports + save 23 / 124. The
model alone (stages A–D) is 132 clicks + 331 keys; 124 of the export keys are file names. Clicks went
up by 11 because the STL settings now live in a dialog (the typed `EXPORTSTL ... angle=0.2rad` line is
still available at 1 click + about 50 keys per file), keystrokes went down by 155, and no step needs a
workaround, a re-open or a second attempt any more. In the same terms the script is 170 typed lines.

### Remaining

* U3 (the origin XZ plane's normal is −Y, so `OFFSETPLANE 175` on XZ needs `flip=true` and sketches
  on XZ-parallel planes extrude toward −Y) and U4 (a preselected face lands in "Placement points" of
  HOLE) are unchanged: both are conventions rather than defects, and the script types them as before.
* STEP import still flattens the assembly into root bodies named `chair 1..3`; the read-back addresses
  them by world point.
* The GUI estimate is still counted from the code, not measured in a live session.
