# Integration notes: sketch module

Owner: sketch agent. Status of the module and everything the integrator must do. Entry format
for requests follows `README.md`.

## What the module contains (2026-10-19)

Target `fabricad_sketch` (`src/sketch`, links core + Qt6::Widgets + Eigen). All tests headless:
`ctest --test-dir build-sketch -R sketch` -> 5 binaries, all green
(`test_solver`, `test_sketch_ops`, `test_feature`, `test_tools`, `test_sketch_smoke`).

| Area | Files | Notes |
|---|---|---|
| 2D model | `SketchGeometry.h/.cpp`, `Sketch.h/.cpp` | Point/Line/Circle/Arc, construction + projected + blocked flags, 19 constraint types (Coincident, PointOnCurve, H, V, Parallel, Perpendicular, Tangent incl. tangent-at-shared-endpoint, Equal, Concentric, Collinear, Midpoint, Fix, Symmetric, Distance, DistanceX/Y, Radius, Diameter, Angle), driven dimensions, expressions (`d1*2`, document parameters), stable integer ids, JSON. Operations: trim (with constraint transfer), extend, split, offset (chains, corner trimming), mirror (with symmetric constraints), fillet (line/line, line/arc, with tangency), hit testing, window selection, chains. |
| Solver | `SketchSolver.h/.cpp`, `third_party/planegcs/` | FreeCAD PlaneGCS vendored (LGPL-2.1+, rev `703986732cf5`, see `third_party/planegcs/README.md`), DogLeg with LM fallback, DOF count, conflicting / redundant constraint ids, per-geometry "fully constrained", collapse detection, drag (temporary low-priority constraints). |
| Profiles | `SketchProfiles.h/.cpp` | `BRepAlgoAPI_Splitter` of a bounding face by all non-construction curves -> every bounded region as a `TopoDS_Face` with seed point + bounding curve ids. Also `makeEdge/makeVertex/makeEdgesCompound` and `projectShape()` (B-rep vertex/edge -> sketch geometry). |
| Feature | `SketchFeature.h/.cpp` | `class SketchFeature : core::Feature, core::IProfileSource`, type `"Sketch"`, `browserFolder()=="Sketches"`, `plane` Ref (kPlaneKinds) resolved via `ctx.plane()`, geometry/constraints serialised in `toJson/fromJson` (base called first), `outputShape()` = compound of curve edges, `profileSource()==this`. `profileFace()` matches seed point first, then curve-id overlap. Projected geometry re-resolves against the body on every recompute (same index + same curve type first, geometric signature second; stored signature refreshed). `displayItems()` and `statusText()` ("3 DOF" / "Fully constrained" / "Over-constrained"). Fills `namedShapes()` `display:<free|defined|construction|fixed|projected|conflict|points>` per the viewer's convention (`src/viewer/SceneModel.h`). |
| Inference | `SketchInference.h/.cpp` | Fusion-style snapping: endpoints/centres/midpoints, on-curve, H/V (3 deg), parallel/perpendicular to the previous segment, dashed alignment tracking lines; `applyInference()` turns a result into constraints (alignment constraints are `internal`). Ctrl suppresses. |
| Session | `SketchSession.h/.cpp` | One active sketch-mode session (`SketchSession::current()`): enters/leaves viewer sketch mode, hides the feature's persistent display and draws it through previews with Fusion colours (blue free, black defined, orange dashed construction, green fixed, purple projected, red conflict), constraint glyphs, dimension labels (`R5`, `⌀10`, `90°`, `(12)` driven, `fx` prefix for expressions), optional profile shading; snapshot/commit editing through `ModifyFeatureCommand` (one undo step per operation); keeps the idle `sketch.select` tool running between commands. |
| Tools | `tools/SketchToolBase.*`, `tools/SketchModeTools.cpp`, `tools/CreateTools.cpp`, `tools/ModifyTools.cpp`, `tools/ConstraintTools.cpp`, `tools/SketchTools.h` | All registered by `registerModule()`. Ids and shortcuts listed below. Typed dynamic input everywhere via `ITool::commandLineInput`: `12.5`, `width/2`, `10 in`, `x,y`, `@dx,dy`, `@len<ang`. |

### Tool ids

| Group | Id | Name | Key / aliases | Behaviour |
|---|---|---|---|---|
| SOLID/CREATE | `sketch.create` | Create Sketch | `SKETCH`, `SK`; arg `XY|XZ|YZ` | Preselected plane/planar face starts at once; otherwise prompts with selection filter `kPlaneKinds` and starts on the first plane picked (selection or `ViewerMouseEvent::pick`). Adds the feature with `AddFeatureCommand`, enters sketch mode, sets contextual tab `SKETCH`. |
| hidden | `sketch.edit` | Edit Sketch | arg: feature id, or preselection owned by a sketch | Re-enters sketch mode on an existing sketch. **App: run this on double-click of a sketch in the timeline/browser/viewport** (`ViewerWidget::entityDoubleClicked` with a ref whose owner is a Sketch feature). |
| FINISH SKETCH | `sketch.finish` | Finish Sketch | `Ctrl+Return`, `FINISH` | Leaves sketch mode (camera kept). |
| hidden | `sketch.select` | Select | - | Idle tool while in sketch mode: hover preselection, click/Ctrl-click, window (L->R) / crossing (R->L) selection, drag of free geometry with live solve (one undo step), click on a dimension label selects it, double-click on a label runs `sketch.dimension-edit`, double-click on a curve selects its chain, Delete removes selection, Esc clears selection, typing with one dimension selected edits it. |
| OPTIONS (palette) | `sketch.look-at`, `sketch.toggle-snap`, `sketch.toggle-grid`, `sketch.toggle-profiles`, `sketch.toggle-points`, `sketch.toggle-dimensions`, `sketch.toggle-constraints` | | | Instant toggles for the Sketch Palette rows. |
| CREATE | `sketch.line` | Line | `L` | Chained lines, inference (H/V/parallel/perpendicular/coincident/on-curve/alignment), closes the loop on the first point, drag from the last point = tangent arc, double-click/Enter/Esc ends the chain, second Esc exits. |
| CREATE | `sketch.rectangle`, `sketch.rectangle-3point`, `sketch.rectangle-center` | 2-Point / 3-Point / Center Rectangle | `R`, `REC`, `REC3`, `RECC` | Coincident + H/V (2-point; centre adds a construction centre point and symmetric constraints) or 3 perpendiculars (3-point). Typed `w,h`. |
| CREATE | `sketch.circle`, `sketch.circle-2point`, `sketch.circle-3point` | Circles | `C`, `CIRCLE2`, `CIRCLE3` | Typed diameter for centre-diameter. |
| CREATE | `sketch.arc-3point`, `sketch.arc-center`, `sketch.arc-tangent` | Arcs | `ARC`/`A`, `ARCC`, `ARCT` | Tangent arc requires picking the end point of a line/arc (adds the Tangent constraint). |
| CREATE | `sketch.polygon`, `sketch.polygon-inscribed` | Circumscribed / Inscribed Polygon | `POL`, `POLI`; arg or typed integer = sides (default 6) | Lines + coincident + equal + construction circle (tangent / point-on-circle). |
| CREATE | `sketch.slot` | Center to Center Slot | `SLOT` | Two lines, two arcs, construction axis; tangent joins, equal radii; typed width. |
| CREATE | `sketch.point` | Point | `PO` | |
| CREATE | `sketch.mirror` | Mirror | `MI` | Pick objects (or preselect), Enter, pick the mirror line; adds Symmetric constraints. |
| CREATE | `sketch.project` | Project | `P` | Selection filter Edge/Vertex/Face/Body; projects picked B-rep (face/body = all edges) as purple projected geometry with `ShapeRef` sources; associative on recompute. |
| CREATE | `sketch.dimension` | Sketch Dimension | `D`, `DIM` | 1 pick: line length / ⌀ circle / R arc. 2 picks: point-point (aligned, horizontal or vertical by label position), point-line, parallel lines distance, lines angle, circles/arcs through their centres. Place, then type a value/expression (or OK/Enter keeps the measured value). Over-constraining dimensions ask `askYesNo` -> driven, or are refused. `createDialog()` returns a `QLineEdit` while a value is pending. |
| MODIFY | `sketch.fillet` | Fillet | `FILLET`; arg/typed radius (remembered) | Two curve picks (or two preselected curves). |
| MODIFY | `sketch.trim` | Trim | `T` | Red piece preview, click removes the piece between intersections, press-and-sweep power trim. |
| MODIFY | `sketch.extend` | Extend | `EX` | Preview to the next curve. |
| MODIFY | `sketch.break` | Break | `BR` | Split at the clicked point. |
| MODIFY | `sketch.offset` | Offset | `O` | Click a curve (whole chain), move to the side, click or type a distance; Ctrl+click adds chains. |
| MODIFY | `sketch.construction` | Construction | `X` | Toggles construction on the selected curves, or the construction mode for new geometry when nothing is selected. |
| hidden | `sketch.dimension-edit` | Edit Dimension | args: `<constraint id> [value]` | `QLineEdit` dialog or typed value; one undo step. |
| CONSTRAINTS | `sketch.constraint.horizontal-vertical`, `.coincident`, `.tangent`, `.equal`, `.parallel`, `.perpendicular`, `.fix`, `.midpoint`, `.concentric`, `.collinear`, `.symmetric` | | aliases `HV`, `COINCIDENT`, ... | Use the preselection, then clicks; conflicting constraints are rejected with a message, redundant ones reported; the tool stays active until Esc. Fix toggles. |

### Status bar / browser hooks

* `SketchSession::current()->statusText()` -> `"Sketch1: 3 DOF"`; `SketchFeature::statusText()` ->
  `"Fully constrained" | "N DOF" | "Over-constrained" | "Solver failed"`; `Sketch::dofs()`,
  `Sketch::lastResult().conflicting/redundant`. The session calls `IAppHost::showMessage(statusText)`
  after every commit and `setPrompt` with it when the idle tool starts.
* Browser icon state: `SketchFeature::sketch().dofs() == 0 && !hasConflicts()` = fully constrained
  (lock icon), `hasConflicts()` = error overlay.

## Integrator checklist

1. **THIRD_PARTY.md**: the PlaneGCS row has been added (vendored under
   `src/sketch/third_party/planegcs/`, FreeCAD revision `703986732cf5`, LGPL-2.1-or-later, local
   modifications listed in its README). Nothing else is copied from FreeCAD; the Fusion behaviour
   is re-implemented.
2. **App wiring**
   * Double-click on a sketch (timeline item, browser row, or `ViewerWidget::entityDoubleClicked`
     with `ref.owner` being a `"Sketch"` feature) -> `ToolManager::run("sketch.edit", {std::to_string(id)})`.
   * Marking menu in sketch mode: `sketch.line`, `sketch.circle`, `sketch.rectangle`, `sketch.trim`,
     `sketch.dimension`, `sketch.finish`, `sketch.look-at`.
   * Status bar: show `SketchSession::current()->statusText()` while `SketchSession::current()` is
     non-null (update on `Document::recomputeFinished`).
   * Sketch Palette (if the ui agent builds one): bind its checkboxes to the `sketch.toggle-*`
     tools and the linetype button to `sketch.construction`; `SketchSession` getters give the state
     (`snapEnabled()`, `showConstraints()`, ... `constructionMode()`).
   * `ToolInfo::shortcut` for Finish Sketch is `"Ctrl+Return"`; make sure `ShortcutRouter`'s
     `keySequenceText()` produces that spelling (QKeySequence style) or adjust the registration.
   * Toolbar group `OPTIONS` (palette toggles) and `SELECT` (hidden idle tool) appear under tab
     `SKETCH`; `FINISH SKETCH` holds `sketch.finish`.
3. **Viewer** (ui agent, already agreed): sketch features fill `namedShapes()["display:*"]`; the
   session hides the feature via `setFeatureVisibilityOverride(id, false)` while editing and draws
   through previews. `ViewerMouseEvent::planePoint` must be set in sketch mode (used by all tools);
   in-sketch hit testing is done in plane coordinates, viewer picks of sketch entities are only
   consumed as preselection by the constraint/dimension/fillet/offset/mirror tools.
4. **Part module**: consumes `core::IProfileSource` only. Profile refs are
   `ShapeRef::profile(sketchId, seedWorld, curveIds)`; `SketchFeature::regionAt(Pt2)` and
   `profileRef(region)` help the viewer/part build them from a pick on a shaded profile.

## Requests to core

## 2026-10-19 — Expose the active-tool dialog text on accept                 (status: open — deferred; the QPointer workaround is acceptable)
What I need: when the shell's OK button is pressed, the typed-but-not-committed text of the
tool's `QLineEdit` is only reachable because the tool keeps a `QPointer` to the widget it
created. A `ITool::accept()` variant receiving the dialog values, or a `IAppHost::toolDialogText()`
accessor, would be cleaner.
Why: dimension value entry (`sketch.dimension`, `sketch.dimension-edit`) and future
numeric fields.
Workaround in place: `QPointer<QLineEdit>` kept by the tool; `returnPressed` routes through
`ToolManager::runCommandLine()`.

## 2026-10-19 — Reference matching policy for projected geometry             (status: partly done — history names resolve exactly in core now; the sketch keeps its index-first workaround)
What I need: `RecomputeContext::resolve()` downgrades the feature to Warning whenever a sub-shape
is re-matched by signature, and `resolveSubShape()` loses the reference entirely on a 50% size
change of the body (signature distance above the threshold).
Why: Fusion keeps projected geometry silently associative as long as the topology is unchanged.
Workaround in place: `SketchFeature::updateProjected()` resolves itself (`subShapeAt` with the
same curve type first, `resolveSubShape` second) and refreshes `ShapeRef::signature`. If core
adopts "index + same geometry type = exact", the sketch can go back to `ctx.resolve()`.

## Not done / later

* Ellipse, spline, conic, text, rectangular/circular sketch patterns, sketch scale, move/copy
  of sketch geometry (drag works), 2-tangent / 3-tangent circles, "intersect" projection.
* Constraint glyphs are text previews and cannot be clicked (dimension labels can); constraints
  are deleted by deleting the geometry or through the dimension/constraint selection API
  (`SketchSession::selectConstraint`).
* Dynamic input boxes with Tab-locking: done in round 2, see `round2-sketch-input.md`
  (`ValueInput.h`, `ValueInputOverlay.h`, `SketchTool::defineValues`).
