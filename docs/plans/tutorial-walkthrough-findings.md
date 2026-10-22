# Tutorial walk-through findings (bracket, current main build)

Date: 2026-10-22. Role: prep walk-through for `docs/plans/tutorial.md` (not edited).
Build: `build/bin/fabricad` from main (`9ac3d78`, binary 09:22). This is the **pre-redesign UI**: ribbon with
SOLID/SKETCH/TOOLS tabs, light theme, `DESIGN` workspace button, bottom timeline, ViewCube. There is no FEATURES list,
no MORE menu and no hex dial yet, so every "redesign" anchor in the plan was checked only for its behaviour, not its place.
Input: only xdotool mouse and keyboard on private display :73 (Xwayland window floated to 1600x1000). The `cmd>` box
was used only to read state (`CHECK ... volume`, `INFO`), never to model. Screenshots are in
`docs/plans/tutorial-walkthrough/` (numbers match the steps below, see the index at the end).

## Result in one paragraph

The whole bracket was built from the GUI. **Every cumulative volume matches the plan** (63 656.637,
61 334.513, 60 817.533, 60 300.552, 59 169.579, 58 849.579 at 0.001 %, 60 049.579). After t 8 -> 10 the volume is
**67 246.359** (0.001 %), 14 features all Ok, bbox **X[-40,40] Y[-25,25] Z[0,50]**, Measure says 80 x 50 x 50 and
volume 67246.359 mm3. The saved `.fcad` reopens with the same volume. The STL (binary, 0.02 / 10) has the exact bbox, a
watertight mesh (0 unmatched edges) and volume ratio 1.00002. All 4 unverified items work (details below), but a number of steps
need a different action and four cannot be done as written (slot values 5.3, slot dimension 5.5, rib dimension 9.4, Equal 3.8).
See the table and the fix list.

Legend: **W** worked as written. **D** needed a different action (what). **I** impossible as written (workaround given).

## The 4 unverified items

| Item | Verdict | Evidence |
|---|---|---|
| Sketch-on-face axis orientation on screen | **Verified.** After Create Sketch the view looks at the face. Top face: X right, +Y up, origin = world origin projected (plate centre). Flange front face: X right, Z up, origin at world (0, z=0) (the bottom of the plate, below the face). YZ plane (opened from RIGHT view): Y to the right, Z up. The "back-left corner" of the plate top is the top-left corner on screen, as 3.3 says. | 13, 78, 106 |
| Profile splitting when the drawn chain overlaps projected edges | **Works.** Extrude on Sketch2 selects only the 80 x 10 strip when clicked, Operation defaults to **Join**, 40 up gives 64 000. The whole-plate profile also stays selectable. | 29 |
| Mirror of a Hole feature | **Works.** Preselecting Hole1 in the timeline, then CREATE > Mirror: Objects shows "1 selected" (Pattern type Features). Mirror plane: click the field, expand Origin in the browser, click YZ Plane. Enter. Volume 60 300.552 exactly. | 70, 72, 73 |
| Rib "To Next" | **Works** (default Depth option is To Next, Thickness option Symmetric). With a 20 x 20 triangle line the preview fills the wedge and the volume rises by 1 200 exactly (60 049.579). Note the sketch problems in 9.3/9.4. | 117, 118 |

## Per-step results

| Step | Result | Notes |
|---|---|---|
| 0.1 Start tour | n/a | No tour yet. |
| 0.2 Orbit, Home | **W** | Shift+MMB drag orbits. Home resets to the isometric fit. |
| 0.3 Workspace | n/a | Button is `DESIGN`, no PART workspace here. |
| 1.1 Create Sketch | **W** | Icon in SOLID > CREATE. Shows the three planes, status "Select a plane or planar face". |
| 1.2 Click XY | **W** | Enters sketch and looks at it at once (Look At on entry already works). |
| 1.3 Center Rectangle | **D** | Center Rectangle is in the **CREATE** dropdown of the SKETCH tab (the group label opens the list), not on the toolbar. Item is `Center Rectangle` with no key. | 
| 1.4 Click origin | **W** | |
| 1.5 80 Tab 50 Enter | **W** | Both value boxes, dims 80 and 50 appear. Status says "Fully constrained (redundant)". The rectangle tool stays active ("Specify center"). |
| 1.6 Finish Sketch | **W** | Green FINISH SKETCH button or Ctrl+Enter. Lines are black when defined (light theme). |
| 2.1-2.2 E, click inside | **W** | Profile is selected by that click. |
| 2.3 Type 8 Enter | **W** | The floating Distance box takes the typed value, Operation is New Body. |
| 3.1 Click face, Create Sketch | **W / caution** | Works only if the selection is clean. A face selected earlier stays selected after its command (the plate top stayed blue after Hole, Mirror, Chamfer). First attempt at 7.1 made Sketch4 on the **old top face** instead of the flange (70-75). Click empty space first. Also clicking a pixel on a face edge selects the edge, not the face. |
| 3.2 P, click face, Esc | **D** | Works, status "Projected 4 item(s)". Projected lines are **violet solid**, not cyan dash-dot (colour text is wrong). The cyan area is the profile fill. The sketch is already **"Fully constrained"** here. |
| 3.3 L, click back-left corner | **W** | Snaps to the projected corner. |
| 3.4 Move down, type 10, Enter | **W** | Box shows Length and Angle (270 deg). Gives vertical + dim 10. The new line lies on the projected left edge. |
| 3.5 Move right, type 80, Enter | **W** | Horizontal. No 80 dim is drawn. |
| 3.6 Click back-right | **W** | Snaps to the projected corner. |
| 3.7 Click start point, Esc | **D** | Closes (4 lines). Status is **already "Fully constrained (redundant)"** (0 DOF), not DOF 1. So the "CP: DOF 1" is wrong. |
| 3.8 Equal | **I (not needed)** | Clicks on the short lines land on the overlapping projected edges, so which lines get the constraint is ambiguous ("Constraint added", icons in odd places). It is redundant anyway: drop this step. I undid it. |
| 3.9 Ctrl+Enter | **W** | |
| 3.10 E, click strip, 40 | **W** | Join. 64 000 (not read, shape checked). |
| 4.1 F, two front edges | **W** | Edges at the two corner verticals, "2 selected", tangent chain on. |
| 4.2 10 Enter | **W** | 63 656.637 ok. |
| 5.1 Select top face, sketch, P | **W** | "Projected 6 item(s)". The plate top is now y -25..15 on screen. |
| 5.2 Center Point Slot | **D** | Tool is in CREATE dropdown. Click the centre, then the next input is **"center-to-arc distance"** (half the length). Click the centre at (0,-15) on the grid. |
| 5.3 "30 Tab 8 Enter" | **I** | Tab does not reach a width field. Typed `30` is the centre-to-arc value (60 total c-c). Needed: type **15** Enter (30 centre to centre), then a separate **Width** step: type **8** Enter. My first attempt gave "Degenerate slot ignored". |
| 5.4 D: centre to front edge 10 | **D** | A click on the slot centre point picks the **centre line** (hover shows the point, click picks the line). Line to the front edge works as a distance 10 (they are parallel). |
| 5.5 D: centre to left edge 40 | **I** | Centre line vs left edge gives a **-90 deg angle**. Workaround: left arc-centre point (not the centre line) to the left edge, value **25** (x = -15). Length: dimension the centre line (or the two arc centres) = 30. |
| (5.5 CP) dof == 0 | **I** | With width 8, 10, 25, 30 the status shows **"Sketch3: 30 DOF"** and `CHECK dof == 0` fails (got 30), while the slot turns black (defined) and the extrude works. Looks like a DOF count or solver status bug (undo of the 30 dim gives 1 DOF, so it appears only when the last dim closes the sketch). |
| 5.6 Finish, E, click slot, Cut, All | **W** | Operation and Extent are combos (click, then the item). Cut sets Flip direction by itself. 61 334.513 ok. |
| 6.1 H, click arc edge | **W** | Preview at the arc centre (30,-15). Hover highlights the arc. |
| 6.2 Counterbore dialog | **D** | Defaults: Ø 6.6, counterbore Ø 11, counterbore **depth 6** (set 4: triple-click the field, type, Tab). Extent combo -> All. Enter works. 60 817.533 ok. The `Counterbore` pick is a combo (Simple / Counterbore / Countersink), no segmented control. |
| 6.3 Hole1 in timeline, Mirror | **D** | **The timeline was not visible at start** (collapsed to nothing in this run; the first launch on a 945 x 490 display). View > Show Panels > Timeline off and on brought it back. The rest as the table above. |
| 7.1 Click flange front face, sketch, P | **D** | See 3.1 for the stale selection. Click away from the edge. "Projected 4 item(s)". |
| 7.2 C, click, 12 Enter | **W** | Circle Ø12 (key C = Center Diameter Circle). Click at 10 below the top edge. |
| 7.3 D 10 and 40 | **W** | Here the circle centre point **is** picked by the click. Fully constrained after the two dims. |
| 7.4 E, click circle, Cut, All | **W** | Choosing Extent All **removes the Distance row** and shifts the dialog rows up, so click positions after that change. Flip is set by Cut. 59 169.579 ok. |
| 8.1 MODIFY > Chamfer, two long edges | **D** | Items: MODIFY dropdown > Chamfer, default Equal Distance 1 mm. **Edge picking is fragile**: the edge hit band is about 1 px. My back-edge click was 1-2 px off, it selected the **top face** ("2 selected"), and the chamfer then took all 4 edges of that face: volume 58 820.2 instead of 58 849.579 (extra 29.4). In the left view the edges are end-on points and a face is also a line, which fails too ("distance too large"). With a pixel-exact hover (checked by a screenshot) the result is exact. Suggest: restrict the filter to edges, or a larger edge pick tolerance. |
| 9.1 Create Sketch, YZ in the browser | **W** | Origin must be expanded (arrow). Opens looking from the right: Y right, Z up. Select the plane while "Select a plane or planar face" is shown. |
| 9.2 P, click side face, Esc | **W** | "Projected 8 item(s)". |
| 9.3 L, click plate line then flange line | **D** | Both ends snap to the lines, but the second click ends at **z = 27**, not 28 (INFO: Line (-4.96,8)->(15,27)), and the sketch says "Fully constrained" with a free end. Triangle area is 190 mm2, not 200. |
| 9.4 D: ends to the lines, 20 each | **I** | The first dim (lower end to flange line = 20) works and moves the start to y = -5. The upper end **cannot be picked as a point**, the click picks the flange line (-> 90 deg), and an angle dim is added as **"Driven dimension"** because the sketch already reports fully constrained. Workaround used: draw the line with typed **length 28.2843 Tab angle 45** from the plate line, then dim the lower end to the flange line = 20. Result is exactly (-5,8)->(15,28). |
| 9.5 Finish, Rib, click line, 6 | **D** | Rib is in the CREATE dropdown (above Hole). Dialog: Curve, Plane, Thickness options (Symmetric), Thickness (type in the field, default 2), Depth options (To Next), Bodies to join. 60 049.579 ok. |
| 10.1 Double-click Extrude1 | **W** | Timeline icon 2. The marker moves to after Extrude1 and the later icons dim. |
| 10.2 Ctrl+A, 10, Enter | **W** | Marker back at the end, 14 features Ok (`CHECK status ok`, `CHECK rollback == 14`), volume 67 246.359, zmax 50. Every sketch-on-face followed the new top face. |
| 11.1 I, click the two bores | **D** | Works with bore wall clicks (blue disc = the cylinder face). The headline "Distance" is the **minimum distance 53.4 mm** (60 minus 6.6). "Centre distance 60 mm" is a table row that needs a **scroll** down in the table (the table shows 6 rows). |
| 11.2 Click the body | **D** | A click in the 3D view picked an **origin plane** (OriginPlane, area 10 000), because planes win over the body. Clicking **Body1 in the browser** gives Volume 67246.359 mm3, area 18572.592, bbox 80 x 50 x 50, 26 faces. Esc then closes. |
| 12.1 Ctrl+S | **W** | A Qt dialog. File name field is pre-selected, type the full path without extension, Enter: `.fcad` is appended. Tab title becomes `bracket`. |
| 12.2 Close tab | **D** | The x moves left when the tab title gets shorter (232 -> 219 px in a 1600 px window). Closing leaves a fresh `Untitled` tab. |
| 12.3 Ctrl+O, type, Enter | **D** | The first Enter only accepts the path completer popup, a second Enter opens. It opens **into** the empty Untitled tab (no new tab). Volume 67246.359 after reopen, 14 timeline items. |
| 12.4 Ctrl+9 / cmd | not tried | `LIST features` and `CHECK` work in the cmd box. Output is a one-line status plus a log popup (the icon left of `cmd>`); the popup scrolls with the wheel. |
| 13.1 MORE > Export STL | **D** | Today: hamburger > Export > **Export STL** (also in the TOOLS tab, not opened). No MORE section yet. |
| 13.2 STL dialog | **W** | Fields: File (text + browse), Linear deflection (default 0.05), Angular (11.4592), Format Binary. Type the path in File, set 0.02 and 10, OK. Status "exported 1 body to ...". File: 1936 triangles, 96 884 bytes, bbox exact, 0 unmatched edges, volume ratio 1.00002. |

## Issues worth fixing (or recording in the plan)

1. **Slot value boxes** (5.3): the length box means centre-to-arc (half length) and there is no width field on Tab. Either change the label or the tutorial text to `15` then `8`.
2. **Dimensioning a slot** (5.4/5.5): the centre point cannot be picked (the centre line wins), so "40 from the left edge" is not reachable. Plan: use the left arc centre (25) and 30 on the centre line, or add a symmetric constraint to the origin.
3. **DOF status** (5.5 "30 DOF", 9.3 "fully constrained" with a free end, rib angle becoming a driven dim). The solver count is wrong in two cases; the tutorial's `dof == 0` checkpoints will not hold for Sketch3 and Sketch5.
4. **Chamfer edge pick** (8.1) and **origin-plane pick priority** (11.2): known classes (NEW-ASSEMBLY-DRAWING-1), both hit in this run. The face stale-selection (3.1/7.1) also bit once.
5. Plan text fixes: 3.2 colour, 3.7 DOF 1 -> 0, drop 3.8 (Equal), 6.2 counterbore depth default 6, 11.1 minimum vs centre distance, 12.2/12.3 details.
6. **Timeline missing at launch** in this run (small first display): worth a check at first start, a tutorial that says "Click Hole1 in the timeline" fails when the panel has zero height.

No source file was changed. Fixes for src/app, src/viewer or the theme were not made (see the concurrency rule); items 1-4 and 6 that need them should go into `docs/plans/backend-deferred.md` by whoever owns that.

## Screenshot index (`docs/plans/tutorial-walkthrough/`)

| File | What |
|---|---|
| 04 | XY sketch on entry, looked at |
| 08 | rectangle 80 x 50 done, "Fully constrained (redundant)" |
| 13, 16 | face sketch on the plate top, projected (violet lines) |
| 24, 26 | closed chain already fully constrained, ambiguous Equal result |
| 29 | strip alone selected in Extrude2 (profile split) |
| 35, 37, 43, 45 | slot: projected edges, centre placed, "Degenerate slot ignored", width step |
| 48, 51 | slot dimension picks (distance 10, angle -90) |
| 55 | INFO Sketch3: 30 DOF |
| 60, 66 | Extrude3 Cut/All, Hole counterbore settings |
| 69 | timeline restored (Show Panels) |
| 70, 72, 73 | Mirror preselected, YZ chosen, result |
| 74 | wrong face (stale selection) |
| 78, 79, 83, 85 | flange sketch, projected, dims, Extrude4 Cut/All |
| 90, 91, 103 | chamfer picks and the wrong/right result |
| 106, 109, 110, 112, 114 | YZ sketch, rib line (z = 27), INFO, driven angle, final line |
| 117, 118 | Rib dialog, result |
| 121 | after t = 10 |
| 124, 126 | Measure (two bores, body) |
| 127, 133, 135 | save dialog, STL dialog, exported |
