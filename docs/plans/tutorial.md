# Plan: interactive "First part" tutorial (in-app guided tour + real-input GUI test)

Status: planned, not started. No code yet. Section 8 questions answered by the user (2026-10-22). Starts after roadmap steps 1–3.

Reconciled on 2026-10-22 with the newer `docs/plans/ui-redesign.md` (it wins where they differ) and
`docs/plans/feature-list-timeline.md`. The tour is built **on the redesigned UI**: no ribbon tab row, a sectioned
main toolbar with a MORE ▾ section, the FEATURES list instead of the bottom timeline by default, the hex orientation
dial instead of the ViewCube by default, a floating command card, dark theme by default, bold monospace caps.
Execution order: `docs/plans/ROADMAP.md` (this plan is step 4, after the redesign, the open-issue fixes and a
drawing test round).

## 0. Goal and shape of the deliverable

The deliverable is an **interactive, in-app guided tour**. It takes a new user from first launch to an STL of a
mounting bracket. A coach-mark bubble sits next to the real toolbar button, command-card field, FEATURES row or model face
for the current step. The target is spotlit. The tour **waits for the user** to do the real action with mouse and
keyboard, then moves on by itself when the app reaches the expected state. It never does modelling actions for the user.

One tutorial definition file drives three consumers:

| Consumer | Uses from each step |
|---|---|
| (1) In-app overlay (primary) | anchor, bubble text, completion condition, hint, "show me" path, checkpoint |
| (2) ctest `gui` test | the same steps, plus the canonical input (`do`). It replays that input as **real X events** with the overlay active, then asserts the overlay reaches the end and the part / STL / reopen checks pass |
| (3) Docs export (optional) | text and anchors, plus the screenshots captured by (2), rendered to `docs/tutorials/bracket.md` |

Ground rules:
- Every user action is mouse or keyboard in the real GUI.
- The tutorial never needs the `cmd>` line. One optional aside mentions it (step 12.4). No scripting is used.
- Introspection may **locate** targets and **read** state. It never acts.

---

## 1. Part spec: `Bracket` (single body, mm)

Coordinates: Z is up and the plate bottom is z = 0. The front is −Y and the flange is at the back (+Y). X is width,
and the part is symmetric about x = 0.

`t` is the plate thickness. It is built at **t = 8** and changed to **t = 10** in chapter 10 (timeline edit). The final
part has t = 10.

| # | Feature | Exact definition |
|---|---|---|
| 1 | Sketch1 (XY plane) | Centre rectangle at the origin, 80 × 50: X[−40, 40] × Y[−25, 25]. Driving dims 80 and 50 come from the value boxes. 0 DOF. |
| 2 | Extrude1 (New Body `Body1`) | Distance t, +Z, so the plate is Z[0, t]. |
| 3 | Sketch2 (plate top face, z = t) | Project the top face. A closed 4-line chain from the projected back-left corner (−40, 25): down 10 (typed), right 80 (typed), up (free), then close on the start point. Equal(L1, L3) gives 0 DOF. The strip is X[−40, 40] × Y[15, 25]. |
| 4 | Extrude2 (Join) | Strip, 40 up. The flange is X[−40, 40] × Y[15, 25] × Z[t, t+40]. |
| 5 | Fillet1 R10 | The two front vertical plate edges (x = ±40, y = −25, z 0..t). |
| 6 | Sketch3 (plate top face) | Project the top face. Centre-point slot, horizontal, length 30 (centre to centre), width 8. Slot centre is 10 from the projected front edge and 40 from the projected left edge, so it sits at (0, −15). 0 DOF. |
| 7 | Extrude3 (Cut, through all) | The slot cuts through the plate. |
| 8 | Hole1 | Counterbore, placed on the top arc edge of the right fillet, so its centre is (30, −15). Ø6.6 through all, counterbore Ø11 × 4 deep, direction −Z. |
| 9 | Mirror1 | Mirror feature Hole1 across the YZ origin plane, giving a second hole at (−30, −15). |
| 10 | Sketch4 (flange front face, y = 15) | Project the face. Circle Ø12 with its centre 10 below the projected top edge and 40 from the projected left edge, so it sits at (0, z = t+30). |
| 11 | Extrude4 (Cut, through all) | A Ø12 hole through the 10 mm flange. |
| 12 | Chamfer1 2 mm (equal distance) | The two long top edges of the flange (y = 15 and y = 25, z = t+40). |
| 13 | Sketch5 (YZ plane) | Project the side face (x = +40, L-shaped). One line from the projected line z = t to the projected line y = 15. The lower end is 20 from y = 15 and the upper end is 20 above z = t, so the line runs (y −5, z t) to (y 15, z t+20). |
| 14 | Rib1 | Thickness 6 symmetric about x = 0, To Next. The gusset is a 20 × 20 right triangle × 6. |

Expected timeline (14 items, all OK, marker at the end): Sketch1, Extrude1, Sketch2, Extrude2, Fillet1, Sketch3,
Extrude3, Hole1, Mirror1, Sketch4, Extrude4, Chamfer1, Sketch5, Rib1.

### Reference volumes (analytic)

| Contribution | Formula | t = 8 | t = 10 |
|---|---|---|---|
| Plate | 80·50·t | 32 000 | 40 000 |
| Flange | 80·10·40 | +32 000 | +32 000 |
| Fillets | −2·(1−π/4)·10²·t | −343.363 | −429.204 |
| Slot | −(30·8 + π·4²)·t | −2 322.124 | −2 902.655 |
| 2 counterbores | −2π(5.5²·4 + 3.3²·(t−4)) | −1 033.961 | −1 170.809 |
| Flange hole | −π·6²·10 | −1 130.973 | −1 130.973 |
| Chamfers | −2·(½·2·2)·80 | −320 | −320 |
| Rib | +½·20·20·6 | +1 200 | +1 200 |
| **Total** | | **60 049.579** | **67 246.359** |

Cumulative checkpoints at t = 8:

| After | Volume (mm³) |
|---|---|
| Extrude1 | 32 000 |
| Extrude2 | 64 000 |
| Fillet1 | 63 656.637 |
| Extrude3 | 61 334.513 |
| Hole1 | 60 817.533 |
| Mirror1 | 60 300.552 |
| Extrude4 | 59 169.579 |
| Chamfer1 | 58 849.579 |
| Rib1 | 60 049.579 |

**Bounding box** (tight): t = 8 gives X[−40, 40] Y[−25, 25] Z[0, 48]. The **final** part is
**X[−40, 40] Y[−25, 25] Z[0, 50]** (±0.01). Clearances were checked: the slot (x ±19, y −19..−11) misses the
counterbores (x 24.5..35.5) and the rib (y ≥ −5). The flange hole (z t+24..t+36) sits clear of the rib top (t+20)
and of the chamfer (t+38).

**Tolerances**:
- B-rep volume within 0.01 %.
- Box within 0.01 mm.

**STL** settings: binary, linear deflection **0.02 mm**, angular **10°**. The STL must meet these checks:
- watertight and consistently oriented, one shell;
- volume within 0.3 % of 67 246.359;
- box within 0.05 mm.

**Reopen**: the saved `bracket.fcad`, reopened, must meet these checks:
- canonical document JSON equal to the saved one, ignoring UI state;
- the same 14 features, with status, names and the dimension values 80/50/10/40/10/30/8/…;
- the same volume and box within 1e-6 relative;
- the same face, edge and vertex counts.

This spec goes to `docs/tutorials/BRACKET_SPEC.md`. It is the single source for the numbers, like
`CHAIR_SPEC.md`. A headless twin `tests/tutorial/bracket.fcs` (script plus `CHECK`s, label `tutorial`) builds the same
part, which proves the spec independently of the GUI.

---

## 2. Tutorial steps

Columns:
- **Bubble**: the user-facing text.
- **Anchor**: what the bubble points at.
- **Input**: the canonical real input the test replays.
- **Advance when**: the completion condition.
- **CP**: checkpoint assert. ✓ means a saved checkpoint `.fcad`.

| # | Bubble (abridged) | Anchor | Input | Advance when | CP |
|---|---|---|---|---|---|
| **Ch 0 Welcome** (first-launch card or Help > Tutorials) | | | | | |
| 0.1 | "Welcome. We'll build a bracket and 3D-print it. Start?" | welcome card | click `TutorialStart` | tour running | |
| 0.2 | "Orbit: Shift + middle-drag. Zoom: wheel. Home key resets the view." | viewport | Shift+MMB drag; Home | camera changed, then Home view | |
| 0.3 | (only if the workspace selector does not show PART) "Open the workspace menu and choose **Part**." | `WorkspaceSwitcher` | click, click | toolbar filter = Part | |
| **Ch 1 First sketch** | | | | | |
| 1.1 | "Click **Create Sketch**." | `tool:sketch.create` | click | tool `sketch.create` active | |
| 1.2 | "Click the **XY** plane (the flat one)." | `plane:XY` in view | click | sketch mode, plane XY, look-at done | |
| 1.3 | "Open **Create ▾** and choose **Center Rectangle**." | `group:SKETCH/CREATE`, `tool:sketch.rectangle-center` | click, click | tool active | |
| 1.4 | "Click the **origin** dot." | `sketchpoint:origin` | click | first point = origin | |
| 1.5 | "Type **80**, press **Tab**, type **50**, press **Enter**." | value box `Width` | type `80`, Tab, `50`, Enter | 4 lines, dims 80/50, DOF 0 | dof == 0 |
| 1.6 | "The lines turn the fully-defined colour (white in the dark theme, black in light) and the status shows DOF 0. Click **Finish Sketch**." | `FinishSketchButton` | click | not in sketch, Sketch1 exists | |
| **Ch 2 Extrude** | | | | | |
| 2.1 | "Press **E** (Extrude)." | keycap E | key `e` | `part.extrude` active | |
| 2.2 | "Click inside the rectangle." | `profile:@0,0,0` | click | 1 profile selected | |
| 2.3 | "Type **8**, Enter. This is the plate thickness." | `prop:distance` / arrow tip | type `8`, Enter | Extrude1 exists, op New Body | vol 32 000, box ✓ |
| **Ch 3 Flange (sketch on a face, lines, constraints)** | | | | | |
| 3.1 | "Click the **top face** of the plate, then **Create Sketch**." | `body:Body1/face:top`, then tool | click, click | sketch on face z = 8 | |
| 3.2 | "Press **P** (Project) and click the top face: its edges appear as cyan dash-dot lines." | same face | key `p`, click, Esc | ≥ 4 projected curves | |
| 3.3 | "Press **L**. Click the **back-left corner**." | `sketchpoint:@-40,25,8` | key `l`, click | line started, coincident | |
| 3.4 | "Move toward the front, type **10**, Enter." | value box `Length` | move, type `10`, Enter | line 1, V, length 10 | |
| 3.5 | "Move right, type **80**, Enter." | value box | move, type `80`, Enter | line 2, H, 80 | |
| 3.6 | "Click roughly at the back edge on the right (no value)." | `@40,25,8` area | click | line 3 | |
| 3.7 | "Click the **start point** to close the chain. Press Esc." | `sketchpoint:@-40,25,8` | click, Esc | closed loop of 4 | DOF 1 |
| 3.8 | "**Equal**: click the left and the right short lines." | `tool:sketch.constraint.equal`, curves | click, click, click | DOF 0 | dof == 0 |
| 3.9 | "Finish Sketch (Ctrl+Enter)." | keycap | Ctrl+Return | Sketch2 done | |
| 3.10 | "E, click the strip, type **40**, Enter." | `profile:@0,20,8` | e, click, `40`, Enter | Extrude2, Join | vol 64 000 ✓ |
| **Ch 4 Fillet** | | | | | |
| 4.1 | "Press **F**, click the two **front corner edges**." | `edge:@40,-25,4`, `edge:@-40,-25,4` | f, click, click | 2 edges in input | |
| 4.2 | "Type **10**, Enter." | `prop:radius` | `10`, Enter | Fillet1 | vol 63 656.637 |
| **Ch 5 Slot and cut** | | | | | |
| 5.1 | "Select the top face, Create Sketch, P + click face, Esc." | face, tool | as 3.1–3.2 | Sketch3 on top face | |
| 5.2 | "Create ▾ **Center Point Slot**: click near the front middle." | tool, `@0,-15,8` | click, click, click | slot centre placed | |
| 5.3 | "Move right, type **30**, Tab, **8**, Enter." | value box | move, `30`, Tab, `8`, Enter | slot 30 × 8 | |
| 5.4 | "Press **D**: click the slot centre, then the front edge, place the text, type **10**, Enter." | centre point, projected edge | d, click ×3, `10`, Enter | dim 10 | |
| 5.5 | "Same for the left edge: **40**." | | click ×3, `40`, Enter, Esc | DOF 0 | dof == 0 |
| 5.6 | "Finish, E, click the slot, set **Operation: Cut** and **Extent: All**, Enter." | `prop:operation`, `prop:extentType` | keys, a click on the `Cut` cell of the segmented control (redesign 5.4), combo or segmented pick for Extent | Extrude3 Cut | vol 61 334.513 ✓ |
| **Ch 6 Counterbore holes and Mirror** | | | | | |
| 6.1 | "Press **H**, click the **curved top edge** of the right fillet." | `edge:circle@30,-15,8` (arc) | h, click | hole preview at (30, −15) | |
| 6.2 | "Hole type **Counterbore**, Extent **All**, Ø **6.6**, counterbore Ø **11**, depth **4**. Enter." | `prop:holeType`, … | click the `Counterbore` cell (segmented, ≤ 4 values) or combo pick, fields, Enter | Hole1 | vol 60 817.533 |
| 6.3 | "Click **Hole1** in the {timeline}, then CREATE ▾ **Mirror**. Click **Mirror Plane** and **YZ** under Origin in the MODEL panel. Enter." | `timeline:Hole1`, `tool:part.mirror`, `browser:Origin/YZ` | clicks, Enter | Mirror1 | vol 60 300.552 ✓ |
| **Ch 7 Flange hole** | | | | | |
| 7.1 | "Click the flange's **front face**, Create Sketch, project it." | `body:Body1/face:front@0,15,30` | as 3.1–3.2 | Sketch4 | |
| 7.2 | "Press **C**, click near the middle, type **12**, Enter." | `@0,15,38` | c, click, `12`, Enter | circle Ø12 | |
| 7.3 | "D: centre to top edge **10**, centre to left edge **40**." | | as 5.4–5.5 | DOF 0 | |
| 7.4 | "Finish, E, click the circle, Cut, All, Enter." | | | Extrude4 | vol 59 169.579 |
| **Ch 8 Chamfer** | | | | | |
| 8.1 | "MODIFY ▾ **Chamfer**, click both long top edges of the flange, type **2**, Enter." | `edge:@0,15,48`, `edge:@0,25,48` | clicks, `2`, Enter | Chamfer1 | vol 58 849.579 ✓ |
| **Ch 9 Rib** | | | | | |
| 9.1 | "Create Sketch, then click **YZ** under Origin in the MODEL panel." | `browser:Origin/YZ` | click, click | sketch on YZ, looked at | |
| 9.2 | "P, click the side face, Esc." | `body:Body1/face:right` | p, click, Esc | projected L outline | |
| 9.3 | "L: click on the plate top line, then on the flange line. Esc." | `curve:@0,-2,8`, `curve:@0,15,26` | l, click, click, Esc | one line, ends on curves | |
| 9.4 | "D: lower end to flange line **20**, upper end to plate line **20**." | | d, clicks, values | DOF 0 | |
| 9.5 | "Finish. CREATE ▾ **Rib**, click the line, thickness **6**, Enter." | `tool:part.rib`, `curve:@0,5,18` | clicks, `6`, Enter | Rib1 | vol 60 049.579 ✓ |
| **Ch 10 Change history** | | | | | |
| 10.1 | "Double-click **Extrude1** in the {timeline}." | `timeline:Extrude1` | double-click | edit dialog, marker after Extrude1 | |
| 10.2 | "Change 8 to **10**, Enter. Watch the flange, holes and rib follow." | `prop:distance` | Ctrl+A, `10`, Enter | marker at end, all OK | vol 67 246.359, zmax 50 |
| **Ch 11 Measure** | | | | | |
| 11.1 | "Press **I**. Click the bore of each counterbore hole." | `face:cylinder@30,-15,3` and `@-30,…` | i, click, click | measureTable centre distance 60 | |
| 11.2 | "Read the **centre distance: 60 mm**. Click the body to see its volume, then Esc." | `measureTable` | click, Esc | tool none | |
| **Ch 12 Save and reopen** | | | | | |
| 12.1 | "Ctrl+S, type the name **bracket**, Enter." | `QFileDialog/fileNameEdit` | ctrl+s, type path, Enter | saved, clean | file exists |
| 12.2 | "Close the tab (×)." | `tabclose:current` (the painted close glyph of the angled tab) | click | tab gone | |
| 12.3 | "Ctrl+O and open bracket.fcad." | file dialog | ctrl+o, type, Enter | doc reopened | reopen identical |
| 12.4 | (aside) "Power users: Ctrl+9 opens the cmd> line; `LIST features` prints the timeline." | none | no input | | |
| **Ch 13 Export STL** | | | | | |
| 13.1 | "Open **MORE ▾** at the end of the toolbar, then **Export STL**." | `tab:TOOLS` (resolves to the MORE ▾ section), `tool:part.export-stl` | click, click | dialog open | |
| 13.2 | "File: **bracket.stl**. Linear deflection **0.02**, angular **10**, Binary. OK." | dialog fields | type, Tab…, Enter | message "exported" | STL checks |
| 13.3 | "Done! Open the STL in your slicer." | bubble | click Finish | tour complete | |

That makes about 45 user-visible steps (≈ 120 replay actions). The test's step ids follow the table. The tests for 0.2
and 12.4 are only non-blocking checks. Step 0.3 is skipped when its condition is already true at step start (the
default first launch is Part).

Step 6.3 selects Hole1 **before** starting Mirror. With the card docked into the sidebar, FEATURES collapses to its
header and the live row while a command runs (redesign decision 6), so Hole1 would not be visible mid-command.
Selecting it first works in every layout; phase 0 verifies that Mirror takes a preselected feature. The general
question is listed in `ROADMAP.md` (needs user decision).

Steps 3.3 and 3.6 depend on the face-sketch orientation (on screen, X goes right and +Y goes up after Look At), and
the bubble wording does too. Phase 0 verifies this.

---

## 3. Single-source definition

File: `resources/tutorials/bracket/tutorial.json`. It is strict JSON, so Qt reads it natively with no new dependency.
It is compiled into the qrc. Chapter checkpoints are `resources/tutorials/bracket/ch*.fcad`, which the test run writes
and verifies.

```json
{ "id": "bracket", "title": "Your first part: a mounting bracket", "version": 1,
  "window": [1600, 1000],
  "chapters": [{ "id": "extrude", "title": "Extrude", "checkpoint": "ch2.fcad", "steps": [
    { "id": "2.2", "text": "Click inside the rectangle to select it as the profile.",
      "anchor": {"model": "profile:@0,0,0"},
      "do": [{"click": "@anchor"}],
      "until": {"all": [{"tool": "part.extrude"}, {"selection": "profile", "count": 1}]},
      "hint": "Click inside the shaded rectangle, not on its edge.",
      "timeout_ms": 8000 },
    { "id": "2.3", "text": "Type **8** and press **Enter**.",
      "anchor": {"widget": "prop:distance"}, "keys_shown": ["8", "Enter"],
      "do": [{"type": "8"}, {"key": "Return"}],
      "until": {"check": "feature:Extrude1 status ok"},
      "assert": ["body:Body1 volume ~ 32000 0.01%", "body:Body1 zmax ~ 8 0.01"],
      "stable_ms": 200 } ] }] }
```

### Schema

**Anchor kinds** (shared by overlay and test):

| Kind | Example | Resolves to |
|---|---|---|
| `widget` | `FinishSketchButton`, `CommandDialog/OkButton` | objectName path (the names kept or added by redesign 8.2) |
| `tool` | `tool:part.extrude` | the first **visible** widget with dynamic property `toolId`: main toolbar (`#RibbonTool`), quick toolbar or a section caption menu (redesign 8.2) |
| `group` | `group:SOLID/CREATE` | the toolbar section caption dropdown (`#RibbonGroupLabel`) matched by properties `toolbarTab`/`toolbarGroup` |
| `menu` | `menu:Help/Tutorials` | a title-bar button menu (`#AppBar`) and action text |
| `prop` | `prop:distance` | `prop_<name>` card fields: line edit, spin box, combo or `SegmentedControl` |
| `valuebox` | `valuebox:Width` | `fcValueBox` with property `field` |
| `browser` | `browser:Origin/YZ` | MODEL panel (`#Browser`) item path |
| `timeline` | `timeline:Extrude1` | FEATURES row (`#FeatureList`) in `featureList` style, strip item (`#TimelineStrip`) in `strip` style |
| `tab` | `tab:TOOLS` | the toolbar section(s) holding that tab's tools. The ribbon tab row is removed: `tab:SURFACE` and `tab:TOOLS` resolve to the MORE ▾ section, `tab:SOLID` to the main sections, `tab:SKETCH` to the contextual sketch sections |
| `tabclose` | `tabclose:current` | the close glyph rect of a document tab, from a `closeRect(index)` hook on the custom-painted tab bar (redesign 7.1) |
| `model` | `model:<selector>` | 3D target, see below |
| `sketchpoint` | `sketchpoint:@x,y,z` | sketch point at a world position |
| `none` | | no anchor |

**Matching rules.** The UI shows caps through `QFont::setCapitalization` (redesign 3.1), so on-screen text differs
in case from the strings. Anchors therefore match the source strings (tool ids, properties, objectNames, item
names, action text), never rendered text, and every text comparison (`menu:`, `browser:`, `timeline:`, `group:`,
`tab:`) is **case-insensitive** with `&` mnemonics stripped. User-entered names (features, bodies) keep their case
on screen (decision 3) but are still matched case-insensitively.

**Text placeholders.** Bubble text may use `{timeline}` (expands to "FEATURES list" or "timeline" by
`ui/timelineStyle`) and `{orientation}` ("orientation dial" or "ViewCube" by `ui/orientation`), so one text works in
every layout preset.

`model:<selector>` uses the script selector grammar of `docs/SCRIPTING.md` (`body:Body1/face:top`,
`edge:@40,-25,4`, `profile:@…`, `plane:XY`). The resolver turns it into a screen point (see "Locating" below).

**Actions** (`do`, test only; the overlay uses them only to draw "show me"):

| Action | Fields |
|---|---|
| `click` / `dclick` / `rclick` | target, optional `mods` |
| `move` | target or `{dx, dy}` from the last point; used for "move the cursor right" before typing |
| `drag` | from, to, `button`, `mods`, `steps` |
| `wheel` | target, `clicks` |
| `type` | text |
| `key` | chord, e.g. `ctrl+Return` |
| `wait` | condition |
| `shot` | name |
| `assert` | `CHECK` expressions |

**Conditions** (`until`, `assert`, `wait`) can be:
- `tool` (active id), `dialog` (open), `selection` (kind / selector / count), `valuebox` (focused field), `prompt` (text),
  `sketch` (`dof == 0`), `tour` (step id), or `check` (any `CHECK` expression).
- Combined with `all` / `any` / `not`.

The step completes when its condition stays true for `stable_ms` (default 150 ms, which covers the 150 ms edit-apply delay).

**Single evaluator.** The `CHECK` code of the script runner is refactored into `app::StateQuery`. Script `CHECK`, the
overlay's conditions and the test probe all call it, so every check means the same thing everywhere.

### Locating targets for real-input replay

| Option | Pros | Cons |
|---|---|---|
| Fixed coordinates | trivial | break with window size, HiDPI, docking, fonts, camera; today's reports are full of these |
| Image matching (template/OCR) | needs no app changes | slow; sensitive to theme (dark and light), scale and the caps rendering; can't find faces or edges; ambiguous icons |
| **Introspection (recommended)** | exact; follows layout and camera; the overlay needs it anyway | needs a small in-app hook and stable names |

**Recommendation: introspection through a read-only probe.** One class, `AnchorResolver`, maps an anchor to global
screen geometry. The overlay uses it to draw, and `TestProbe` uses it to answer the runner. The test therefore checks
that the overlay points at the right thing.

For a 3D anchor, the resolver works like this:
1. Sample candidate points on the target (an interior UV grid on a face, 25/50/75 % along an edge, the profile seed
   point).
2. Project each point with `worldToScreen`.
3. Keep points that are inside the viewport and not covered by a widget (`QApplication::widgetAt` must be the GL
   view, so no command card, palette, orientation dial or ViewCube, view-tool column, breadcrumb or filter bar,
   readout, quick toolbar or value box covers the point; the redesign's viewport overlays are separate widgets,
   redesign 5.3 and 6.3).
4. Keep points where `viewer.pick(x, y)` returns the target, i.e. the viewer's own pick agrees.
5. Prefer the point nearest the visual centre.

If no point qualifies, the overlay says "orbit or press Home". The runner presses Home once and retries.

The runner then moves the real cursor there with `xdotool`. It waits until the probe reports that the hovered widget or
preselected entity is the target, then clicks. **Every action is an X event. The probe has no write commands.**

---

## 4. In-app overlay (Qt design)

New code in `src/app/tutorial/`:

| Class | Role |
|---|---|
| `TutorialDefinition` | Parse and validate the JSON. Report unknown anchors, tools and conditions. |
| `TutorialEngine` | State machine: current step, condition watch, checkpoints, undo index per step, persistence in QSettings `tutorial/<id>/step`. |
| `AnchorResolver` | Anchor to rect or point (above). Shared with `TestProbe`. |
| `StateQuery` | Condition and `CHECK` evaluation. Shared with the script runner. |
| `TutorialOverlay` | Full-window painter: dim, spotlight and ghost cursor. |
| `TutorialBubble` | Coach mark: text with keycaps, chapter, "Step 12 / 45" progress bar, Back · Show me · Skip · Exit. |
| `TutorialLauncher` | Help > Tutorials list and the first-launch welcome card ("Start tour" / "Not now", remembered). |

**Layering.**
- `TutorialOverlay` is a **child of the MainWindow**, sized to it and raised above the central widget, docks,
  main toolbar and the viewport overlays. It sets `WA_TransparentForMouseEvents` and `WA_NoSystemBackground`.
- An event filter re-raises it on `ChildAdded` and `ZOrderChange`.
- The bubble is a sibling child that does accept mouse input.
- Child widgets avoid translucent top-level windows, which render black without a compositor (the dropped SOLID-12
  finding). They also compose correctly over the `QOpenGLWidget` viewer. Native child windows (`WA_NativeWindow`)
  must be avoided.
- Top-level popups (QMenu, combo popups, QFileDialog, QMessageBox such as the over-constraint prompt) cannot be
  covered. The engine detects `activePopupWidget` / `activeModalWidget` and switches to an opaque,
  rectangular, frameless tool-window bubble placed beside the popup, with no dim.

**Spotlight.**
- A light dim (≈ 30 % alpha, optional in Preferences) with an even-odd cut-out around the anchor rect, plus a pulsing
  2 px ring in `accent.focus` (redesign 8.2; `theme-light.qss` is deleted by the redesign). The dim colour is a new
  token (`tutorial.dim`) added to `resources/theme/tokens.json`; the overlay has no colour literals (colour lint).
- The bubble uses `surface.overlay`, `text.primary`, a 1 px `border.default` outline and the redesign text styles
  (`title` for the chapter, `body` for the text, `keycap` for keys), in both themes, repainting on `themeChanged`.
  Whether the bubble prose is caps or as typed is open (`ROADMAP.md`, needs user decision); until then it follows
  the redesign rule (caps).
- The bubble and spotlight avoid the rects `OverlayLayout` reports as occupied (card, dial, tool column), so the
  bubble never covers the command card it points into.
- For model anchors, the target face or edge is highlighted in the viewer through `setHighlighted(refs)` with a new
  "tutorial" highlight style (a `ViewerPalette` entry from `accent.focus`, with the halo rule of redesign 4.5)
  instead of a rectangle.

**Tracking.**
- Widget anchors: event filter on the target and its ancestors (Move, Resize, Show, Hide) and on scroll areas
  (`FeatureList`, `TimelineScroll`, `Browser`, `CommandDialogScroll`), and on `UiSettings::changed` (a layout or
  theme switch re-resolves the anchor).
- Model anchors: the viewer `cameraChanged` signal (added by redesign phase 4 for the dial), throttled to the frame rate. The bubble arrow re-targets.
  When the target is off-screen or occluded, the bubble says so.

**Non-blocking.**
- All input passes through to the app. Orbit, zoom and Home work at any time, and camera changes never count as
  off-script.
- The bubble never takes focus (`Qt::NoFocus`, `WA_ShowWithoutActivating`), so typed values reach the value boxes.

**Keyboard and accessibility.**
- Proposed tour shortcuts: F7 show me, Alt+Right skip, Alt+Left back. **Esc stays "cancel command" and never exits
  the tour.**
- The bubble has an accessible name and description. Step changes post `QAccessibleEvent` announcements.
- Text follows the app font and `ui/fontScale` and uses tokens, never hard-coded white-on-colour.

**Show me.** A ghost cursor (painted by the overlay, not the real pointer) animates along the step's `do` path from the
bubble to the anchor. It shows clicks as ripples and typed text as keycaps. Nothing is sent to the app.

**Off-script and recovery.**
- The engine records `UndoStack::index()` at each step start.
- A document change that neither completes nor partly satisfies the step (a new undo command the step does not allow)
  shows a gentle hint: "That added *Extrude2*. Press Ctrl+Z, or click **Undo to step start**."
- **Back** undoes to the previous step's index.
- **Restore chapter** (with confirmation) replaces the tab's document with the shipped chapter checkpoint `.fcad`.
- Wrong values (e.g. 81 instead of 80) give a hint naming the expected value. Strict mode (exact values) is the
  default, see the open questions.
- The recovery buttons are explicit user choices. They are the only document changes the tour makes.

**Start.** The tour starts from Help > Tutorials > "Your first part", or from the welcome card on first launch. The
card never appears for `--script`, `--offscreen` or `FABRICAD_NO_TUTORIAL=1`. A closed tour can be resumed at the
saved step.

---

## 5. Test runner

`tools/uitest/tutorial_run.py` uses only the Python stdlib, `xdotool`, `import` (ImageMagick) and optionally
`ffmpeg`. It is wrapped by `tests/tutorial/run_tutorial.cmake`.

1. **Backend.** Use the first that works:
   1. `FABRICAD_GL_DISPLAY=:N`, an existing private display, as the GL test does.
   2. `Xvfb :N -screen 0 1600x1000x24 +extension GLX` with Mesa llvmpipe (CI).
   3. `weston --backend=headless --xwayland`, which needs no desktop session and has no tiling.
   4. Rootful `Xwayland :N` on the desktop, only if `FABRICAD_GUI_ALLOW_DESKTOP=1`. It floats and resizes its own
      window through `hyprctl`, the step the testers did by hand. The probe then checks that the X root stays
      1600×1000, which catches the "display grew mid-run" and "tiled to 469×490" problems.

   If no backend is available, or `xdotool` or GLX is missing, the runner exits **77** (ctest `SKIP_RETURN_CODE 77`).
2. **Clean environment.** Temporary `XDG_CONFIG_HOME` and `XDG_DATA_HOME`, so this is a genuine first launch, there
   are no remembered STL settings and the recovery folder is isolated. Also:
   - `FABRICAD_NO_RECOVERY=1`, `QT_QPA_PLATFORM=xcb`
   - `QT_ENABLE_HIGHDPI_SCALING=0`, `QT_SCALE_FACTOR=1`, `QT_FONT_DPI=96`, `LANG=C.UTF-8`
   - `FABRICAD_TEST_PROBE=<socket>`

   A first launch gets the redesign defaults (redesign 6.1): dark theme, `featureList`, `hexDial`, card
   `floatRight`, toolbar `top`, sidebar `left`. **Layout presets** (redesign 8.2): `--layout <preset>` writes the
   `ui/*` keys into the temporary `XDG_CONFIG_HOME` before launch, the same mechanism as `ui.sh prefs <N> key=value…`
   (redesign 8.3):
   - `default`: no keys written;
   - `alt`: `ui/cardPlacement=sidebar`, `ui/sidebarSide=right`, `ui/toolbarArea=left`;
   - `light`: `ui/theme=light`, `ui/orientation=viewCube`, `ui/timelineStyle=strip`.
3. **Start.** Launch the app and connect to the probe. Wait for `ready` (window exposed and `viewInitialized`). Size the
   window with `xdotool windowmove/windowsize` and wait until the probe reports the size. There are **no fixed sleeps**.
   The X server inside the backend has no window manager, so nothing re-tiles it.
4. **Per step.**
   1. Assert `tour.step == id`.
   2. Assert that `until` is **false** before acting, to catch trivially true conditions.
   3. For each action: resolve, move, wait for the hover target, then click / type (`xdotool type --delay 25`) / key.
   4. After each X event, wait until the probe's `inputSeq` has advanced. This fixes the "screenshots one action stale"
      lag.
   5. Then wait for `idle`.
   6. Wait for `until`, check the `assert`s, and confirm the overlay has advanced. Take a step screenshot.
5. **Idle.** The probe's `idle` (an app change) is true when all of these hold:
   - no camera animation is running;
   - no deferred apply or recompute timer is pending;
   - the event queue has drained (a zero-timer round trip);
   - the viewer has painted a frame after the last document or selection change.

   Waits poll every 20 ms.
6. **Timeouts.** Actions 5 s, steps 20 s by default (overridable per step), and ctest `TIMEOUT 900`. A failed hover
   retries once at the next candidate point. Nothing is retried after a state-changing click.
7. **Artefacts** go to `build/tutorial-out/<run>/`:
   - per-step PNGs;
   - an `ffmpeg -f x11grab` recording of the whole run at 10 fps, kept on failure or with `FABRICAD_UITEST_VIDEO=1`;
   - the app log and probe transcript;
   - a probe `state` dump and the serialised document at the failure point;
   - `report.json` / JUnit XML.

   The step PNGs also feed the Markdown export.
8. **End checks.**
   - Probe `CHECK`s for volume, box and the timeline list.
   - STL checks: reuse `analyse()` and `read_binary_stl()` by moving them from `tests/chair/check_stl.py` into a shared
     `tools/uitest/stlcheck.py`.
   - Reopen equality: canonical JSON diff plus metrics.
   - Copy the chapter checkpoint `.fcad`s for comparison with `resources/tutorials/bracket/`.
9. **ctest.**
   - `fabricad_tutorial_bracket_{default,alt,light}`: one entry per layout preset, LABELS `gui;tutorial`,
     `RUN_SERIAL`, skip code 77.
   - `fabricad_tutorial_offscript`: `gui`. A short scenario that does a wrong action, expects the hint, uses Back and
     Restore chapter, and checks that "show me" does not change the document.
   - `fabricad_tutorial_lint`: offscreen, label `tutorial`. Validates the JSON (anchors exist, tool ids registered,
     conditions parse), checks that every widget anchor resolves in all 3 layout presets, and runs `bracket.fcs`.

   The default `ctest` stays headless: the GUI tests skip unless a backend is available and `FABRICAD_GUI=1` (or
   `FABRICAD_GL_DISPLAY`) is set.
10. **CI.** A Linux job installs `xvfb mesa-utils xdotool imagemagick ffmpeg` and runs
    `FABRICAD_GUI=1 ctest -L gui --output-on-failure`, uploading `tutorial-out/` on failure. On this dev box (no Xvfb,
    per the README), weston headless or the opt-in desktop Xwayland is used.

---

## 6. Fragility risks, mitigations and app changes needed first

| Risk (source) | Mitigation in the tutorial or runner | App change |
|---|---|---|
| Rectangle and other value boxes sit under the cursor and swallow the second click (NEW-SKETCH-1) | Never click a second corner. Type W, Tab, H, Enter. | Place boxes offset from the cursor, or make them mouse-transparent until focused |
| Orientation-widget targets are small: hex dial tips (default), ViewCube corners in the `light` preset (UX-05, NEW-SKETCH-2) | Never use the dial or the ViewCube in a step. Use Home and Ctrl+1..7. | none required (the dial and the token-coloured ViewCube come with redesign phase 4) |
| Layout variety: card placement, sidebar side, toolbar position, list vs strip, theme | Anchors resolve by name and property, never by position; `{timeline}`/`{orientation}` text placeholders; the tour runs in the 3 presets | `OverlayLayout` and `UiSettings::changed` (redesign) |
| Origin planes win picks over bodies (NEW-ASSEMBLY-DRAWING-1); Create Sketch picks XZ instead of the top face (NEW-PART-CORE-3) | Select the face **first**, then Create Sketch. Pick YZ in the browser. Pick-check before every click. | Lower origin-plane pick priority below bodies |
| Entering a sketch does not orient the view until the first click (NEW-SOLID-4); clicks during camera animation land wrong (SK-03) | Wait for `idle`, which includes the animation | Look At on sketch entry; expose `isAnimating()` and `cameraChanged` |
| Stale viewport after a constraint (SK-02, partial); screenshots lag | Assert from probe state, not pixels | Request a redraw after a constraint solve |
| Dialog height jumps and moves OK (NEW-APP-TOOLS-2, NEW-ADVANCED-PART-2) | Commit with Enter. Resolve button geometry after `idle`. | Reserve the message row (in the redesign's command card, phase 3) |
| Wheel over dialogs changes combos (NEW-ADVANCED-PART-1) | No wheel over dialogs. Combos by click plus popup item rect, or keyboard. | Ignore wheel on unfocused combos |
| cmd box keeps focus and swallows shortcuts (NEW-ADVANCED-SKETCH-1); log popup lingers (NEW-SOLID-2) | Avoid the cmd box. Check the focus widget before keys. | Return focus to the viewport after a command |
| Window and display size change under Hyprland (several retests) | Use a backend without a WM; assert the root size | none |
| Face-sketch axes and origin are not obviously world-aligned (`alignToStoredAxes`) | Every face-sketch position comes from projected edges and dimensions, never from the face sketch's origin | none; verify in Phase 0 |
| Projected curves and profile regions; Equal and redundancy messages | Phase 0 spike confirms the profile split of Sketch2 | Fix if found |
| Mirror of a Hole feature, Rib "To Next" result | Phase 0 confirms. Fallback: Rectangular Pattern of Hole1 (2 × −60 mm); Rib fallback: a triangle profile extruded symmetric 6 | Fix if found |
| Short 8 mm vertical edges hard to hit (Fillet) | Pick-check and hover confirmation; Home view and a fixed window size | none |
| Hole placement by face click is imprecise | Place on the fillet's circular edge (exact centre) | none |

**App work before the tour.** Items marked *(redesign)* or *(roadmap 2)* are delivered earlier by
`ui-redesign.md` or by ROADMAP step 2; the tutorial only verifies them.
1. Stable ids:
   - toolbar buttons get property `toolId`, sections get `toolbarTab`/`toolbarGroup` *(redesign phase 1,
     `MainToolbar`; none of the three exists in today's `Ribbon.cpp`)*;
   - value boxes get property `field`;
   - objectNames on the card's OK (`OkButton`, exists) and Cancel, the STL dialog fields (`stl_path`, `stl_linear`,
     `stl_angular`, `stl_format`), the welcome card and the tour bubble;
   - a FEATURES/strip and MODEL item lookup API;
   - `closeRect(index)` on the custom-painted document tab bar, for `tabclose:`.
2. `StateQuery` refactor out of the script `CHECK`.
3. `TestProbe`: a read-only JSON-lines `QLocalSocket`, only with `FABRICAD_TEST_PROBE`. It answers `locate`,
   `pickcheck`, `eval`, `state` (tool, prompt, selection, focus widget, hover/preselection, tour step, `inputSeq`,
   frame, `idle`, the active `ui/*` layout keys) and `docjson`.
4. Viewer: `cameraChanged` *(redesign phase 4)*, `isAnimating`, a frame counter and a tutorial highlight style
   (a `ViewerPalette` entry).
5. The fixes in the table above *(roadmap 2)*: value box placement (NEW-SKETCH-1), origin-plane pick priority
   (NEW-ASSEMBLY-DRAWING-1, NEW-PART-CORE-3), Look At on sketch entry (NEW-SOLID-4), a redraw after constraints
   (SK-02), stable dialog height (NEW-APP-TOOLS-2, NEW-ADVANCED-PART-2), wheel on unfocused combos
   (NEW-ADVANCED-PART-1), focus return (NEW-ADVANCED-SKETCH-1, NEW-SOLID-2).
6. `ui.sh`: replace `sleep 1.5` with a probe `ready` wait (optional). This sits on top of the redesign's harness
   changes (redesign 8.3: `start <N> [bin] [WxH] [scale]`, per-display `XDG_CONFIG_HOME`, `prefs`, `cvd`), which
   land first; the tutorial keeps their command syntax.

---

## 7. Deliverables, effort and workflow

**Deliverables:**
- `docs/tutorials/BRACKET_SPEC.md`
- `tests/tutorial/bracket.fcs`
- `resources/tutorials/bracket/{tutorial.json, ch*.fcad}`
- `src/app/tutorial/*` (overlay)
- `src/app/TestProbe.*` and `StateQuery`
- viewer hooks
- `tools/uitest/{tutorial_run.py, stlcheck.py}`
- `tools/tutorial/export_md.py` and `docs/tutorials/bracket.md`
- `tests/tutorial/CMakeLists.txt` (3 ctest entries)
- README and SCRIPTING notes

**Effort** (agent-days):

| Work | Days |
|---|---|
| Phase 0 spike (manual walk-through via `ui.sh` **on the redesigned UI**, verify the risks marked "Phase 0") | 1 |
| Spec + `.fcs` twin | 0.5 |
| App prerequisites not delivered by the redesign or ROADMAP step 2 (probe, `StateQuery`, `field`, STL names, `closeRect`, highlight style) | 1–1.5 |
| Overlay: engine, resolver, bubble, spotlight, show me, recovery, launcher, a11y | 5–7 |
| Tutorial JSON and checkpoints (with layout placeholders) | 2 |
| Runner, backends, artefacts, ctest, 3 layout presets | 2.5–3.5 |
| Markdown export | 0.5 |
| Two tester rounds plus fixes | 2–3 |
| **Total** | **≈ 14.5–19 agent-days**, about 1.5 weeks wall clock with 3–4 agents in parallel, starting after ROADMAP steps 1–3 and the answers to section 8 |

**Workflow:**
1. **Integrator (Opus)**:
   - writes the interface headers first (`AnchorResolver`, `StateQuery`, `TutorialEngine` API and the probe protocol)
     and owns this plan;
   - merges, in the ownership style of `docs/ARCHITECTURE.md` §2.
2. Parallel worktrees:
   - **A, app hooks**: the ids, probe, `StateQuery` and viewer hooks not already delivered by the redesign; re-verify
     the §6 fixes done in ROADMAP step 2.
   - **B, overlay**: `src/app/tutorial/`, coded against A's headers.
   - **C, runner**: Python, against a probe stub, then the real one.
   - **D, content**: spec, `.fcs`, `tutorial.json`, checkpoints.
3. **Sonnet testers** (private displays via `ui.sh`, real input only, reports in `docs/tests/tutorial/round1/`):
   - **T1 newcomer**: follows only the bubbles. Reports unclear text, wrong anchors and steps that do not advance or
     advance early.
   - **T2 off-script**: wrong values, wrong order, extra features, orbiting mid-step, Back, Skip, Restore.
   - **T3 docs-only**: follows the exported Markdown without the overlay.

   Then triage into `ISSUES.md`, fix, and retest (round 2).
4. Acceptance:
   - `fabricad_tutorial_bracket_default` passes **10 runs in a row** (also with a CPU load generator running), and
     the `alt` and `light` presets pass 3 runs each;
   - testers report no blocker;
   - the Markdown export is regenerated from the passing run.

---

## 8. Decisions (confirmed by the user, 2026-10-22)

| # | Question | Decision |
|---|---|---|
| 1 | Part | The bracket as specified, with **Mirror** for the second counterbored hole (rectangular pattern only as the documented fallback). |
| 2 | GUI tests in ctest | **Opt-in** with `FABRICAD_GUI=1`; skip (exit 77) otherwise. No CI backend is standardised yet; the private Xwayland harness is the dev-box backend. |
| 3 | CI | **None for now.** Keep the runner CI-ready (Mesa llvmpipe + ffmpeg) for later. |
| 4 | Strictness | **Exact values** required so checkpoints can verify geometry; a free mode is a later addition. |
| 5 | First launch | A **welcome card** that offers the tour; not shown again after updates unless a new tour is added. |
| 6 | Overlay look | **Light dim** around the target (configurable in Preferences). |
| 7 | Shortcuts | **F7 / Alt+Left / Alt+Right** for the tour. **Esc cancels the current tool, never exits the tour.** |
| 8 | Show me | **Ghost cursor only**; never moves the real pointer. |
| 9 | Recovery | "Restore chapter" **opens a new tab**, so nothing is lost. The test may refresh shipped checkpoints only with an explicit `--update-checkpoints` flag. |
| 10 | Format | **Strict JSON**; translation later (keep text in one place per step so it can be extracted). |
| 11 | Artefacts | Keep the video **on failure only**. |
| 12 | More tours | **Design Help > Tutorials for multiple tours now; build only the bracket tour.** |

Also confirmed: bubble body text is shown **as typed** (mixed case); bubble titles and buttons follow the caps rule. Workspace selector shortcuts are Alt+1/2/3, so the tour shortcuts above do not collide.
