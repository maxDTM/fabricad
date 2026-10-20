# Round 2: on-canvas manipulators (branch `feat/manipulators`)

Hand-over notes for the integrator. Everything below is on `feat/manipulators`, worktree
`fabricad-wt/manipulators`. Build: Dev, `-j2`; all 35 ctest entries pass headlessly.

## What was built

| Layer | Files | Role |
|---|---|---|
| core (headless math) | `src/core/Manipulator.h/.cpp` (new) | `ManipulatorSpec` (kind Linear / Angular / Triad / Point, frame, size, handles, value, increment, limits, label, tag), `ManipulatorModel` (glyph geometry, ray hit test, drag -> value / `gp_Trsf`, 1-2-5 auto snapping from the zoom level, Shift x10, Ctrl free, clamps, cancel), `ManipulatorController` (live gizmos of a view: hover, drag state, event dispatch, re-entrant add/remove safe), `ManipulatorEvent` / `ManipulatorCallback`, `ManipulatorGlyph` (what the viewer draws). |
| core (interface) | `src/core/ViewerContext.h/.cpp` | `IViewerContext::showManipulator / updateManipulator / setManipulatorValue / removeManipulator / clearManipulators / manipulators()`; non-virtual pixel routing `manipulatorPress/Move/Release/Cancel`, `isManipulatorDragging()` built on `screenRay()` + `pixelSize()`. `NullViewerContext` implements the API with the same controller, so tests drive drags with pixels. |
| core (tool manager) | `src/core/Tool.cpp` | `finishActive()` calls `viewer_->clearManipulators()`; `keyPress()` turns Esc into `manipulatorCancel()` while a gizmo is dragged (the command stays open; a second Esc cancels the command as before). |
| viewer | `src/viewer/ManipulatorPrs.h/.cpp` (new), `ViewerWidget.h/.cpp` | `ManipulatorPrs` = `AIS_InteractiveObject` built from the glyph list (shaded cylinders/cones/spheres/tori, translucent quads, dashed guide lines), never AIS-selectable. `ViewerWidget` owns a `ManipulatorController`, routes LMB press/move/release to it **ahead of tools and selection**, shows the hand cursor over handles, highlights the hovered/dragged handle, draws a value label (`AIS_TextLabel`, document units) next to the dragged handle, rebuilds auto-sized gizmos when the zoom changes (`beforeRedraw()`), topmost Z layer. |
| part | `src/part/tools/FeatureManipulators.h/.cpp` (new), `FeatureTool.h/.cpp` | Provider registry `featureType -> bindings` (`registerManipulatorProvider` for other modules). Built-ins: Extrude (distance arrow(s) at the profile centroid for one / two sides / symmetric with scale 0.5 for whole-length, taper ring on each end face), Revolve (angle ring(s) about the axis through the profile, scale 0.5 for symmetric), Fillet (radius), Chamfer (distance) arrows at the first edge pointing into the material, Shell (thickness, inside/outside), Offset Face / Press Pull (distance along the face normal), Offset Plane (distance), Hole (depth, into the material; hidden for Through All). `FeatureTool` syncs gizmos after every preview (`syncManipulators`), a drag writes the property (replacing an expression, Fusion style), refreshes the live preview and the `PropertyPanel`; typed values / command line move the gizmos (`updatePreview -> updateManipulator`). Editing an existing feature shows the same gizmos anchored on the pre-feature state. Fusion's **Join -> Cut** switch: at the end of an Extrude drag the operation becomes Cut when more than half of the tool solid lies inside existing bodies and Join when it is pulled out, until the user picks an operation explicitly (dialog combo or `operation=` on the command line). |
| assembly | `src/assembly/tools/MoveComponentTool.*`, `DriveJointTool.*` | The Move/Rotate component triad is a `ManipulatorKind::Triad` gizmo (arrows, rings, plane squares; only the joint's DOF handles when the component is jointed). Its `mousePress/Move/Release` overrides are gone; `onTriadEvent()` maps the delta transform to placements / a joint value. `assembly::Triad` is kept as the frame/size/handle-mask holder (the old preview drawing and hit code in it is unused now). Drive Joint shows a ring (rotation DOF) or arrow (translation DOF) on the joint frame with the limits as clamps; dragging it stops the animation and drives the value, the slider/edit follow and vice versa. |
| tests | `src/core/tests/test_manipulator.cpp` (new), `src/part/tests/test_manipulators.cpp` (new), `src/assembly/tests/test_assembly_tools.cpp` (edited) | Snapping series, hit/drag of every kind, limits/cancel, controller hover/drag/release through `NullViewerContext` pixels, re-entrant removal, ToolManager clearing and Esc; every part binding with real drags (Extrude both sides / symmetric / expression replacement / sign flip / Join->Cut; Revolve; Fillet/Chamfer; Shell; Press Pull; Offset Plane; Hole; edit mode with undo); Move triad drag and jointed ring drag; Drive Joint ring drag with limit clamp and dialog -> gizmo sync. |

## Shared-file edits (expect merge conflicts here)

* `src/core/ViewerContext.h` (+36) / `.cpp` (+43): new pure virtuals on `IViewerContext`.
  **Any other `IViewerContext` implementation added in round 2 must implement the five
  manipulator methods** (copy the `NullViewerContext` block: an owned `ManipulatorController`
  and one-line forwards).
* `src/core/Tool.cpp`: two small insertions (`finishActive`, `keyPress`). No header change.
* `src/viewer/ViewerWidget.h/.cpp`: new members and ~120 lines in the mouse handlers
  (`onMousePress/Move/Release`, `onLeave`, `beforeRedraw`) plus a new section
  "manipulators" near `setHighlighted`. If another branch reworked the mouse handlers, re-apply:
  manipulator press first (returns before tools), drag moves swallow the event, release ends the
  drag, leave clears the hover.
* `src/part/tools/FeatureTool.h/.cpp`: new members (`manipulators_`, `manipulatorDragging_`,
  `operationAuto_`), `syncManipulators/clearManipulators/onManipulatorEvent/autoOperation`,
  calls at the end of both branches of `updatePreview()`, in `activate()`, `deactivate()`, the
  dialog `valueChanged` lambda and `commandLineInput` (`operation=` disables the auto switch).
* `src/assembly/tools/MoveComponentTool.*`, `DriveJointTool.*`: rewired as described above.
  Public API kept (`components()`, `setTransform`, `triad()`, `constrainingJoint()`, `isDragging()`,
  `setValue`, `step`, ...), new `manipulatorId()` and event entry points.
* `src/assembly/tests/test_assembly_tools.cpp`: `moveToolFreeAndJointed` and
  `driveJointToolRespectsLimits` extended (the "triad drawn as previews" assertion is gone).

No CMake, MainWindow, module registration or `THIRD_PARTY.md` changes (no third-party code).

## Behaviour notes (Fusion 360 mimicry, `docs/research/ux.md` 7 / 8.1 / 8.2 / 8.13)

* Gizmos are auto-sized to ~70 px (`kAutoSizePixels`) unless the binding gives a world size
  (revolve ring radius = profile distance from the axis, taper rings 32 px). Pick tolerance 8 px.
* Snapping: without modifiers the dragged value snaps to a 1-2-5 increment that corresponds to
  about 3 px of mouse travel (angles: 0.1 .. 90 deg series); Shift = ten times coarser; Ctrl =
  free. Specs may fix an increment (`ManipulatorSpec::increment`).
* Extrude distance may be dragged through the profile (negative = other side); radius / thickness
  / depth are clamped at 0; joint handles are clamped to the joint limits.
* The value label reads "Distance: 12.5 mm" in the document's display unit while hovering or
  dragging; Triad shows the amount of the active handle only while dragging.
* Esc during a drag restores the value at drag start (DragCancel) without closing the command.

## Follow-ups / known limits

* The viewer side (`ManipulatorPrs`, `ViewerWidget` routing) cannot be unit-tested on this
  machine (no GL). It was compiled; the math it renders is covered by the core tests. A manual
  pass in the real app (XWayland) is recommended after the merge: hover colours, label placement,
  arrow sizes after zooming, triad over a selected body.
* Only the first edge / face of a Fillet / Chamfer / Shell / Offset Face selection carries the
  arrow (Fusion shows one per selection set). Multiple fillet radius sets are not modelled yet.
* Sweep / Loft / Draft / Patterns / Move Body have no gizmos yet; add a provider with
  `registerManipulatorProvider(type, fn)` (pattern quantity spinner and distance arrows are the
  obvious next ones).
* `assembly::Triad` still contains its own drawing/hit code; it can be reduced to a frame/mask
  struct once nothing else uses it (tests still exercise `Triad::hit/dragTo`).
* Point handles (`ManipulatorKind::Point`) exist in the math layer and the viewer but no tool
  uses them yet (intended for sketch-plane / pivot picking and the Move Body pivot).
* The floating "type a value here" box next to the handle (ux.md 7) is not implemented; typed
  values go through the dialog field / command line, which do move the gizmo.
