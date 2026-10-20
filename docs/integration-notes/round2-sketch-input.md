# Round 2 — sketch-input: Fusion-style on-screen value boxes (dynamic input)

Branch `feat/sketch-input`, worktree `fabricad-wt/sketch-input`. Owner: sketch-input agent.

## What was built

| Piece | Files | Notes |
|---|---|---|
| Headless model | `src/sketch/ValueInput.h/.cpp` | `ValueInputModel`: fields (id, label, kind Length/Angle/Count/Scalar, text, value, locked, visible, world anchor), active field, `keyPress()` with Fusion/AutoCAD semantics (printable -> type; empty box only accepts digits `. - + (` so letter shortcuts keep working; `,` / `<` = next box; Tab / Shift+Tab; Backspace; Esc clears text, then unlocks all; Enter -> `Commit` once something is typed/locked), `lock/lockValue/unlock/resetInput`, `setMeasured/setAnchor` from the tool, `lockedFields()` for the commit. Evaluation/formatting are injected (`makeSketchEvaluator(doc)`, `makeUnitFormatter(doc)` in `tools/SketchToolBase.h`: units, expressions, document parameters, sketch dimension names). No Qt Widgets. |
| Widget overlay | `src/sketch/ValueInputOverlay.h/.cpp` | One `QFrame` (label + `QLineEdit` + lock mark) per visible field, parented to `IViewerContext::widget()`, positioned with `worldToScreen(anchor)` + 18/14 px, stacked below each other at the same anchor, clamped into the viewport, re-positioned on viewer Paint/Resize/Wheel/MouseMove (event filter, coalesced 0 ms timer). Active box = accent border `#0696D7`, invalid = red, locked = lock mark. `textEdited` pushes widget text into the model; Tab/Enter inside a box are forwarded to the tool through `ToolManager::keyPress`. Headless (`NullViewerContext::widget() == nullptr`): no widgets, tools draw `ValueInputModel::summary()` as a preview text next to the cursor instead. |
| Tool base | `src/sketch/tools/SketchToolBase.h/.cpp` | `SketchTool` owns a `ValueInputModel` + overlay: `defineValues`, `setValue(id, measured, anchor)`, `setValueVisible`, `lockedValue`, `showValueBoxes`, hooks `valuesChanged()` / `commitValues()`, `handleValueKey()` (called first in `keyPress`), `typedValuesToBoxes()` (command-line text -> boxes: `"30"`, `"30,20"`, `"50<30"`), `addLockedDimension()` (value + expression -> driving dimension; if the solver reports conflict/redundancy it becomes driven and a message is shown — never a red sketch), `addRightAngleInstead()` (angles that are multiples of 90 deg become H/V or parallel/perpendicular constraints, no redundant Angle dimension). |
| Sketch tools | `src/sketch/tools/CreateTools.cpp`, `ModifyTools.cpp` (Offset) | Line: length + angle (angle relative to the previous line of the chain, else to +X; locks drive the rubber band; commit adds `Distance` + `Angle`, right angles -> H/V/Parallel/Perpendicular). Rectangles: width + height (`Distance` on the first two edges; 3-point: width at the 2nd point, height at the 3rd). Circles (centre-diameter, 2-point): diameter (`Diameter`). Arcs: radius (+ sweep for centre and tangent arcs; `Radius` + `Angle` on the arc = sweep). Polygon: sides (before the centre click) + radius (`Radius` on the construction circle). Slot: centre distance (`Distance` on the construction axis) + width (`Diameter` on an arc). Offset: distance (`Distance` between the first source line and its copy). Clicking applies the typed value (Fusion). |
| Command line | same files | `commandLineInput` now goes through the boxes, so typed values create driving dimensions: `30,20` (rectangle), `10` (circle diameter / slot width / offset distance), `@50<30` (line: `Distance` 50 + angle 30 or H/V), `@dx,dy` (line: `DistanceX`/`DistanceY`, or H/V + length when one component is 0). Absolute `x,y` still places a point without constraints (nothing to reference). |
| Solver | `src/sketch/SketchSolver.cpp`, `Sketch.cpp` | `ConstraintType::Angle` with `a` = arc and no `b` is the arc **sweep** (`GCS::addConstraintDifference(start, end)`); `Sketch::measure()` and `defaultLabelPos()` handle it. Existing files unaffected otherwise. |
| Feature tools | `src/part/tools/FeatureValueBox.h/.cpp`, small hooks in `FeatureTool.h/.cpp` | Reusable component for the part module: one box for `PartFeature::primaryNumeric()` (extrude distance, fillet radius, shell thickness, offset-plane distance, revolve angle...), anchored at the centre of the preview bbox (edit mode: the feature's `outputShape()`), mirroring the dialog field; keys forwarded to the tool type into it, each evaluable keystroke calls `FeatureTool::setNumeric()` (live preview + panel refresh), Enter calls `accept()`. `FeatureTool::keyPress()` override, `valueBox_` created in `updatePreview()`, destroyed in `activate()/deactivate()`. |
| Tests | `src/sketch/tests/test_value_input.cpp` (new, QTEST_MAIN offscreen: model + overlay placement), `src/sketch/tests/test_tools.cpp` (3 new cases + updated DOF expectations), `src/part/tests/test_value_box.cpp` (new), `tests/e2e/test_e2e.cpp` (updated, see below) | Test shapes are fixed numbers (see the comments in each test): line 50 mm @ 30 deg then 20 mm @ 90 deg, rectangle 30 x 20, centre rectangle 40 x 10, circle d12, centre arc R10 / 90 deg, octagon R8, slot 40 x 10, offset 5, extrude 25 and `2*h`. |

## Shared-file edits the integrator must know about

* `tests/e2e/test_e2e.cpp` (integrator-owned): the sketch-1 block changed because `80,60` and `12`
  now create dimensions d1/d2/d3. The three explicit width/height/diameter dimensions were removed
  (they would be redundant -> driven), the DOF check after Fix is `2` (circle centre), and the
  two centre-position dimensions keep their meaning (now d4/d5 as before). Everything downstream
  (extrude, hole, drawing...) is unchanged. Expect a conflict if another round-2 agent edited the
  same block.
* `src/part/tools/FeatureTool.h/.cpp` (part agent): +1 forward declaration, +1 `keyPress`
  override, +1 member, +4 lines in `updatePreview()/activate()/deactivate()`. Everything else is in
  the new `FeatureValueBox.*` files. If the part agent reworked `FeatureTool` concurrently, re-apply
  these hooks (they are self-contained).
* `src/sketch/tools/SketchToolBase.h/.cpp`: `SketchTool` got an out-of-line constructor/destructor
  (`unique_ptr` to a forward-declared overlay) — any new sketch tool keeps using
  `SketchTool(ToolInfo)`; `using ToolBase::ToolBase` was replaced by an explicit constructor.
* `src/sketch/tools/CreateTools.cpp` was rewritten around the boxes (every create tool). Merge
  other agents' changes to this file by hand (the tool ids, inference and constraint logic are
  unchanged; the typed-input paths moved into `PointsTool::commandLineInput`/`commitValues`).
* `README.md`: "Typed input everywhere" bullet extended, the "no on-screen value boxes" limitation
  removed. `docs/integration-notes/sketch.md`: the "Not done" bullet about dynamic input removed.
* No core edits. No third-party code copied (`THIRD_PARTY.md` unchanged).
* `README.md` line "ctest ... # 33 tests" was not touched: this branch adds 2 ctest entries
  (`fabricad_sketch_test_value_input`, `fabricad_part_test_value_box`); the integrator should set
  the final count once all round-2 branches are merged.

## Behaviour notes (Fusion parity)

* Polygon: after the centre click the **size** box becomes the active one (`values().setActive("radius")`
  in `PolygonTool::updateValueVisibility`), so "8 Enter" after the click is the radius; the sides box
  stays reachable with Shift+Tab. (Fixed when the interrupted attempt was resumed.)

* Boxes appear after the first click of a shape (polygon: the Sides box is available before the
  centre click). Typing a digit fills the active box; Tab locks it (lock mark) and moves on; Enter
  commits the point/shape; Esc clears the text, a second Esc unlocks all, a third one behaves as
  before (ends the chain / tool). A click commits with whatever is typed or locked.
* Locked values override the cursor: a locked line length slides the end point along the cursor
  direction (H/V inference still applies), a locked angle fixes the direction and the cursor only
  sets the distance; a locked rectangle width/height picks the quadrant from the cursor; locked
  arc radius / sweep follow the same rule.
* All boxes accept expressions with units and document parameters (`width/2 + 3 mm`, `1 in`,
  `len/2`); the dimension keeps the expression (`fx` label).
* Keyboard routing in the app: `MainWindow::handleKeyPress` already forwards keys to the active
  tool first when the viewport has focus (`routeKey` -> `toolFirst`), including Tab/Backtab
  (`ForwardOnly`) — no app change was needed. Keys typed while a dialog field has focus stay in the
  dialog (Fusion behaviour).

## Follow-ups / not done

* Boxes have no drag arrows / manipulators (ux.md 6.2); the extrude box sits at the preview centre
  rather than on an arrow handle.
* Offset of circular geometry gets no offset dimension (there is no point-curve distance
  constraint type); a message says so.
* `sketch.fillet` (radius) and the dimension tools keep their existing `QLineEdit` dialogs.
* Absolute `x,y` coordinates create no constraints (a projected origin point would be needed).
* Light theme styling only (hard-coded colours from ux.md 13); dark theme needs the palette hook.
