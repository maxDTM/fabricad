# Integration notes: part module

Owner: part agent. Status as of 2026-10-19: the module builds (`fabricad_part`), all five
test executables pass headlessly (`QT_QPA_PLATFORM=offscreen ctest -R fabricad_part`, build dir
`build-part/`, 34 test cases). Section 4 holds the open requests for shared files.

## 1. What was built (file map)

```
src/part/
  PartModule.h/.cpp           registerModule(ctx) / registerFeatureTypes(factory)
  PartFeature.h/.cpp          PartFeature base: compute(ctx, Outcome&) + commit(); Operation enum;
                              applyDefaultsForNewFeature (Join when bodies exist, else New Body),
                              primaryInput()/primaryNumeric(), toolShape()
  PartUtils.h/.cpp            OCCT helpers: booleans with history + UnifySameDomain + validity
                              (booleanOp, fuseAll), applyTool (Join/Cut/Intersect/New Body against
                              the document bodies), replaceBody (dress-ups), prismFace/revolveFace
                              with history names, halfSpaceOf, resolveByBody, profileFaces,
                              sceneSize/throughAllLength
  Naming.h/.cpp               topological naming layer 1: History wrapper over
                              BRepBuilderAPI_MakeShape / BRepTools_History, mapNames, addEdgeNames
                              ("{FaceA|FaceB}"), upstreamNames/currentNames (cumulative per body),
                              resolveRef (name first, then core index/signature), annotateRef
  features/Construction.*     OffsetPlane, PlaneAtAngle, TangentPlane, Midplane, PlaneThreePoints,
                              ConstructionAxis (edge/cylinder, two points, perpendicular at point,
                              two planes), ConstructionPoint (point/circle centre, edge x plane,
                              three planes, two edges)
  features/Extrude.*          one side / two sides / symmetric; distance / to object / all; taper;
                              start offset; join / cut / intersect / new body; explicit targets
  features/Revolve.*          full / angle; one side / two sides / symmetric; same operations
  features/SweepLoft.*        Sweep (perpendicular / parallel, path = edges or sketch curves),
                              Loft (wires + optional end points, ruled, closed)
  features/Fillet.*           Fillet (face picks expand to all edges), Chamfer (equal / two
                              distances / distance-angle, flip)
  features/Shell.*            Shell (remove faces, or hollow whole bodies; inside/outside), Draft,
                              OffsetFace (= Press Pull on a face), Combine, MoveBody (translate /
                              rotate / point-to-point, optional copy), SplitBody
  features/Hole.*             Hole: simple / counterbore / countersink, distance / through all,
                              flat / angled drill point, N placement points, one cut
  features/Pattern.*          RectangularPattern, CircularPattern, Mirror of features or bodies
                              (instance suppression list in a hidden Json property)
  features/Import.*           Import feature (STEP/STL -> bodies, cached by file mtime, scale),
                              exportStep(), exportStl()
  tools/FeatureTool.*         generic create/edit tool for any PartFeature: live preview by running
                              compute() against the body state at the rollback marker, selection
                              -> active input with filter, command-line input ("25",
                              "distance=2*w mm", "direction=Symmetric"), OK -> AddFeatureCommand
                              or ModifyFeatureCommand(before, after), Cancel restores.
                              PressPullTool (face -> OffsetFace, edge -> Fillet, profile -> Extrude),
                              EditFeatureTool ("part.edit-feature <id>")
  tools/PropertyPanel.*       Qt dialog body generated from the feature's properties (expression
                              fields, combos, check boxes, selection buttons with clear)
  tools/PartTools.*           registration of all tools (ids, shortcuts, aliases, tabs/groups),
                              export tools (LambdaTool), import with file chooser
  tests/                      test_extrude_revolve, test_dressup, test_construction_pattern,
                              test_tools (QTEST_MAIN, widgets offscreen), test_part_smoke;
                              tests/support/TestSketch.h = IProfileSource stand-in so the tests do
                              not depend on the sketch module's internals
```

Feature type names registered (needed to load `.fcad` files): `Extrude Revolve Sweep Loft
Fillet Chamfer Shell Draft OffsetFace Hole RectangularPattern CircularPattern Mirror Combine
MoveBody SplitBody Import OffsetPlane PlaneAtAngle TangentPlane Midplane PlaneThreePoints
ConstructionAxis ConstructionPoint`.

Tool ids (workspace Design, tab `SOLID` unless noted; promoted = big icon):

| Group | id (shortcut) [aliases] |
|---|---|
| CREATE | `part.extrude` (E) [EXTRUDE, EXT] promoted, `part.revolve` [REVOLVE, REV] promoted, `part.sweep`, `part.loft`, `part.hole` (H) promoted, `part.rectangular-pattern` [RECTPATTERN, ARRAY, AR], `part.circular-pattern` [CIRCPATTERN, POLAR], `part.mirror` [MIRROR, MI] |
| MODIFY | `part.press-pull` (Q) [PRESSPULL, PP] promoted, `part.offset-face`, `part.fillet` (F) promoted, `part.chamfer` [CHA] promoted, `part.shell` promoted, `part.draft`, `part.combine` promoted, `part.split-body` [SPLIT], `part.move` (M) [MOVE, M, COPY, CO], `part.edit-feature` (hidden) [EDITFEATURE] |
| CONSTRUCT | `construct.offset-plane` [OFFSETPLANE, PLANE] promoted, `construct.plane-at-angle` promoted, `construct.tangent-plane`, `construct.midplane` promoted, `construct.plane-three-points` [PLANE3], `construct.axis` promoted, `construct.point` |
| INSERT | `part.import` (dialog with a path field) promoted, `part.import-file` (file chooser, instant) |
| TOOLS / EXPORT | `part.export-step` [EXPORTSTEP, STEP], `part.export-stl` [EXPORTSTL, STL]: instant; selected bodies or all visible ones; path from `args[0]` or a save dialog |

## 2. THIRD_PARTY.md rows to add (integrator)

| Path in FabriCAD | Origin | Revision | License | Notes |
|---|---|---|---|---|
| `src/part/features/Hole.cpp` (`HoleFeature::makeProtoHole`) | FreeCAD `src/Mod/PartDesign/App/FeatureHole.cpp` (half cross-section of the hole revolved about its axis; counterbore / countersink / drill-tip construction) | FreeCAD tree in `../fabricad-ref` | LGPL-2.1-or-later | adapted, not copied verbatim; the file header names the origin |

Nothing else was copied. The other features are written against OCCT directly, following the
recipes in `docs/research/freecad-occt.md`.

## 3. App / viewer wiring the integrator must do (ui agent)

1. Call `fabricad::part::registerModule(ctx)` after `sketch::registerModule` (the part module
   codes against `core::IProfileSource` only; the order matters for `.fcad` loading).
2. **Do not display `outputShape()` of solid part features.** Solid features (Extrude, Hole,
   patterns, Offset Face, ...) store their *tool solid* in `outputShape()` so Pattern/Mirror can
   reuse it. The viewer must draw `outputShape()` only for features whose `browserFolder()` is
   `"Sketches"` or `"Construction"` (ARCHITECTURE section 13 already says so). Construction
   features put a plane face / axis edge / vertex there; draw planes translucent, axes as lines,
   points as markers. They also implement `asPlane/asAxis/asPoint`.
3. Timeline / browser "Edit feature" (double click, context menu): run
   `toolManager.run("part.edit-feature", {std::to_string(featureId)})`, or run the feature's own
   tool with `ShapeRef::feature(id)` preselected; `part::toolIdForFeatureType(type)` gives the
   tool id. If you want Fusion's "preview in context" (later features hidden while editing), set
   the rollback marker just after the feature before running the tool and restore it on
   `toolFinished`; the tool itself does not move the marker.
4. Command dialog: `ITool::createDialog()` returns a `part::PropertyPanel`; the shell wraps it
   with title + OK/Cancel and routes Enter/Esc to `ToolManager::accept/cancel`. The tool calls
   `IAppHost::setToolAcceptEnabled` after every preview. Press Pull calls
   `IAppHost::refreshToolDialog()` once the feature type is known (after the first pick), so the
   shell must rebuild the dialog on that call. Enum changes rebuild the panel internally.
5. Picking: profile picks must arrive as `ShapeRef::profile(sketchId, seed, curveIds)` (the
   sketch module's `SketchFeature::regionAt/profileRef`), sketch points as
   `ShapeRef::sketchPoint`, B-rep picks through `makeBrepRef` (index + signature; the tool adds
   the history name). Feature picks for patterns/mirror: clicking a timeline/browser feature
   while a part tool is active should add `ShapeRef::feature(id)` to the selection (the
   `objects` input accepts `ShapeKind::Feature | ShapeKind::Body`).
6. Shortcuts as registered: E, Q, F, H, M. Both `part.move` and the sketch Move/Copy want M; the
   shell should prefer the contextual SKETCH tab's tool while in sketch mode.
7. Marking menu suggestions (ux.md 6.3): face -> `part.press-pull`, `construct.offset-plane`;
   edge -> `part.fillet`, `part.chamfer`; profile -> `part.extrude`; feature -> `part.edit-feature`.
8. Icons: features use the default `iconName()` (`type()` lower-cased, e.g. `extrude`,
   `offsetplane`); tools use their id as icon name (`part.extrude`).

## 4. Requests for core

### 2026-10-19 — name-aware `RecomputeContext::resolve()`                       (status: done — core tries `Document::namedSubShape()` (last toucher's namedShapes) before index/signature; also: side faces are now named by sketch curve id, `Side/c<id>`, see `stabilizeSideNames`)
What I need: `RecomputeContext::resolve(ref)` / `core::resolveShape` should try the history name
first: look up `ref.name` in `namedShapes()` of the last upstream feature that touched the body
`ref.owner`, and fall back to index/signature only when the name is missing.
Why: layer-1 naming currently works only for refs that go through `part::resolveRef`. Assembly
joints and drawing views that store face/edge refs would become robust to topology changes too.
Workaround in place: `part::resolveRef` / `part::annotateRef` in `src/part/Naming.cpp`
(`upstreamNames` walks `Document::features()` backwards using `touchedBodies()/createdBodies()`).
Suggested change: move `lastToucher/upstreamNames` into core (e.g. `Document::namesForBody(bodyId,
beforeIndex)`), call it in `resolveShape` before `resolveSubShape`, and let `makeBrepRef` fill
`ShapeRef::name` from the same map.

### 2026-10-19 — `unifySameDomain` with history                                   (status: rejected for now — `part::unifyWithHistory` stays; no other module needs it)
What I need: an overload `core::unifySameDomain(shape, Handle(BRepTools_History)&)`.
Why: names must be mapped through the unify step too, otherwise merged faces lose their names.
Workaround in place: `part::unifyWithHistory` in `PartUtils.cpp`. Only worth doing if another
module needs it.

### 2026-10-19 — live edit recompute cost                                           (status: info)
Editing an existing feature previews by `notifyFeatureChanged + recompute()` on each change, as
ARCHITECTURE section 6 prescribes. On large documents this is the hot path; the hook is the
`Feature::inputsToJson()` caching mentioned in section 5. No action needed now.

## 5. Behaviour notes for other module agents

* Join with no explicit targets fuses the tool with every body it touches; a join that touches
  nothing creates a new body (Fusion behaviour). Cut/Intersect touching nothing is an Error.
  `targets` (RefList of Body, label "Objects to Cut/Join") restricts the participants.
* On failure the features only call `ctx.setError` and return; the Document keeps the previous
  body state.
* `Feature::namedShapes()` of the latest feature touching a body is that body's cumulative name
  map: `Extrude1/End`, `Extrude1/Side/3`, `Fillet1/Extrude1/{...}`, edges
  `{Extrude1/End|Extrude1/Side/2}`. `ShapeRef::name` is filled when a pick is annotated
  (`part::annotateRef(doc, ref)`) and refreshed on every successful resolve.
* `PartFeature::toolShape()` (default `outputShape()`) is what patterns/mirror copy. Dress-ups
  (Fillet/Chamfer/Shell/Draft) have no tool solid, so they cannot be patterned as features
  (same limitation as Fusion; pattern the bodies instead).
* `part::exportStep(shapes, names, path, &err)` / `part::exportStl(shapes, path, binary,
  linDefl, angDeflDeg, &err)` are plain functions the assembly/drawing modules may call. STEP
  export is AP214 in mm without product names (XCAF naming belongs to the assembly agent).
* `ImportFeature::readFile(path, imported, &err)` is static and reusable.

## 6. Known limitations / later work

* Extrude "To Object" with a non-planar target clips a through-all prism with the target's
  half-space, so the target must span the profile. Symmetric extrusion has no "To Object".
* Hole: no thread / tapped types and no "To" extent (Distance / All only).
* Fillet is constant radius only (no variable radius, rule fillet, G2).
* Sweep "Parallel" uses a fixed trihedron; no guide rails or twist. Loft has no guide rails or
  tangency conditions.
* Not implemented: Pattern on Path, Thicken, Rib/Web, Emboss, Replace Face, Scale, Thread.
* Shell "Rounded" corners map to `GeomAbs_Arc`, which OCCT may refuse on complex bodies.
