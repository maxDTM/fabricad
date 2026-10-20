# FabriCAD architecture

Status: skeleton v0.1 (2026-10-19). Owner: architect, then integrator. Module agents read
this before writing code and code against the headers in `src/core/` only. Research
background: `docs/research/freecad-occt.md` (OCCT recipes), `ux.md` (UI spec),
`algorithms.md` (solvers, profiles, HLR, file format).

Contents

1. Module map and dependency direction
2. Ownership table (who may edit what)
3. Build layout and conventions
4. Data model (Document, Feature, Property, Body, Component, ShapeRef)
5. Recompute model and timeline
6. Undo/redo (command pattern)
7. Selection model and references (topological naming mitigation)
8. Tool/Command registry API (ITool, ToolRegistry, ToolManager)
9. Viewer and host interfaces (IViewerContext, IAppHost)
10. Document file format (.fcad)
11. Threading, performance, testing
12. Coding conventions
13. Per-module notes (what each agent must know)

---

## 1. Module map and dependency direction

```
            +-------------------- app (fabricad exe) -----------------------+
            |   main window, toolbars, browser, timeline, dialogs, S-box     |
            +---------+-------------+-----------+------------+--------------+
                      |             |           |            |
                   viewer        drawing     assembly       part        sketch
                 (OCCT/Qt GL)  (QGraphics,   (components,  (features,   (2D geometry,
                      |         HLR, PDF)     joints)       OCCT ops)    solver, profiles)
                      |             |           |            |              |
                      +-------------+-----------+------------+--------------+
                                                |
                                              core
                       Document, Feature, Property, Body, Component, ShapeRef,
                       UndoStack, SelectionModel, Tool registry, IViewerContext, IAppHost
```

Allowed `#include` / link directions (arrows mean "may depend on"):

| Module | Target | May depend on | Must NOT depend on |
|---|---|---|---|
| core | `fabricad_core` | Qt Core/Gui, OCCT modelling toolkits, Eigen | Widgets (only forward-declared `QWidget*`), viewer, any module |
| sketch | `fabricad_sketch` | core, Qt Widgets (dialogs), Eigen | part, assembly, drawing, viewer, app |
| part | `fabricad_part` | core, sketch, Qt Widgets | assembly, drawing, viewer, app |
| assembly | `fabricad_assembly` | core, sketch, part, Qt Widgets, Eigen | drawing, viewer, app |
| drawing | `fabricad_drawing` | core, part, assembly, Qt Widgets/Svg/PrintSupport/Concurrent | viewer, app |
| viewer | `fabricad_viewer` | core, Qt Widgets/OpenGL/OpenGLWidgets, OCCT visualisation (TKV3d, TKOpenGl, TKService) | sketch, part, assembly, drawing, app |
| app | `fabricad` | everything | - |

Why the viewer is isolated: OCCT's GL viewer cannot run headless on this machine. Everything
else is built and tested with `QT_QPA_PLATFORM=offscreen`, so tools in sketch/part/assembly/
drawing talk to the view only through `core::IViewerContext` (and use `core::NullViewerContext`
in tests).

Modules register themselves through one function each:

```cpp
// src/<module>/<Module>Module.h
namespace fabricad::sketch { void registerModule(fabricad::core::ModuleContext& ctx); }
// ModuleContext { FeatureFactory& features; ToolRegistry& tools; }
```

The app calls them in order sketch, part, assembly, drawing. A module's `registerModule`
registers its feature types (needed to load `.fcad` files) and its tools.

## 2. Ownership table

| Path | Owner now | Owner later | Others may |
|---|---|---|---|
| `CMakeLists.txt`, `cmake/`, `.gitignore`, `LICENSE`, `README.md`, `THIRD_PARTY.md` | architect | integrator | append rows to `THIRD_PARTY.md` for code they vendor; nothing else |
| `src/core/**` | architect | integrator | read only. Requests go to `docs/integration-notes/<module>.md` |
| `docs/ARCHITECTURE.md`, `docs/research/**` | architect | integrator | read only |
| `docs/integration-notes/<module>.md` | the named module agent | integrator | - |
| `src/sketch/**` (incl. its `CMakeLists.txt`, `tests/`) | sketch agent | sketch agent | read only |
| `src/part/**` | part agent | part agent | read only |
| `src/assembly/**` | assembly agent | assembly agent | read only |
| `src/drawing/**` | drawing agent | drawing agent | read only |
| `src/viewer/**`, `src/app/**`, `resources/**` | ui agent | ui agent | read only |

Rules for module agents:

* Never edit another owner's files. If you need an API from another module that does not
  exist yet, code against the interface described here, add a local adapter/stub in your own
  directory, and write the request down in your integration-notes file.
* Your module's `CMakeLists.txt` GLOBs your sources; adding files needs no CMake edit. You may
  add compile options/definitions/links to *your* target only.
* Keep the `registerModule()` signature and the stub class names mentioned in section 13.
* Use your own build directory (e.g. `build-sketch/`) and always `-j2`.
* Tests: `src/<module>/tests/test_*.cpp` (one ctest entry each), `tests/support/*.cpp` is
  shared by all your tests. Headless only.

## 3. Build layout and conventions

* CMake >= 3.25, C++20 (`std::format`, designated initialisers, ranges ok), GCC 16.
* All modules are **static libraries**; one executable. Fast startup, no plugin scanning.
* Include root is `src/`: `#include "core/Document.h"`, `#include "sketch/SketchFeature.h"`.
* Namespaces: `fabricad::core`, `fabricad::sketch`, `fabricad::part`, `fabricad::assembly`,
  `fabricad::drawing`, `fabricad::viewer`, `fabricad::app`.
* Build types: `Dev` (default, `-O1 -g1`), `Debug`, `Release`. lld is used when found.
* Helper functions (`cmake/FabriCADModule.cmake`): `fabricad_add_module(target)`,
  `fabricad_add_tests(target [EXTRA_LIBS ...])`.
* Interface targets: `fabricad_occt` (modelling toolkits, linked by core), `fabricad_occt_vis`
  (TKV3d/TKOpenGl/TKService, viewer only), `fabricad_warnings`.
* Test framework: QtTest. `QTEST_GUILESS_MAIN` when no widgets are involved, `QTEST_MAIN`
  otherwise (ctest sets `QT_QPA_PLATFORM=offscreen`).

## 4. Data model

All types live in `fabricad::core`. Headers: `src/core/*.h`.

### 4.1 Identity

`ObjectId` (`uint64_t`, `Types.h`) identifies every document object: features, bodies,
components, and module-owned objects (a module may call `Document::allocateId()`). Reserved:
`kRootComponentId = 1`, `kOriginId = 2` (owner of origin planes/axes/point). Ids are
persisted and never reused within a document.

### 4.2 Document (`Document.h`)

`Document : QObject` owns

* the ordered feature list (the **timeline**) with `rollbackIndex()`;
* `Body` records (`Body.h`) and the current body **shapes** (`bodyShape(id)`), which depend on
  the timeline position;
* the `Component` tree (`Component.h`, root = the document, placements relative to parent,
  `worldPlacement(id)`, `activeComponent()`);
* user **parameters** (`name`, expression, evaluated `Quantity`) and `evaluate(expr, dim)`;
* `UnitSystem units()` (mm/deg internally, display unit per document);
* **extension data**: `extensionData("drawing")` JSON blobs for module state that is not a
  feature (drawing sheets); modules write on `aboutToSave()` and read on `loaded()`;
* the `UndoStack` and the `SelectionModel`;
* JSON (de)serialisation and `save()/load()` of `.fcad` files.

It emits Qt signals for every change (see the header). UI widgets bind to these; tools
mostly do not need to.

### 4.3 Feature (`Feature.h`)

Everything in the timeline is a `Feature` subclass: sketches, solid features, construction
geometry, joints, "new component" markers, imports. A feature has

* `id()`, `type()` (registered name, e.g. `"Extrude"`), `name()` (`"Extrude1"`), `isSuppressed()`;
* `properties()`: ordered `Property` list = its inputs (see 4.4). Helpers `addLength`,
  `addEnum`, `addRef`, `addRefs`...; typed getters `length("distance")`, `ref("profile")`...;
* `recompute(RecomputeContext&)`: the only place geometry is produced (section 5);
* `status()` / `statusMessage()`: `Stale | Ok | Warning | Error`, set by the Document;
* `dependencies()`: owners of all Ref properties + `extraDependencies()`;
* `createdBodies()`: ids of bodies this feature created (slot order; persisted so ids survive
  recompute and reload);
* optional roles: `asPlane()`, `asAxis()`, `asPoint()` (construction geometry),
  `profileSource()` (sketches, returns an `IProfileSource`), `outputShape()` (a display
  shape: sketch curves compound, plane face, joint glyph geometry), `namedShapes()` (history
  names -> sub-shapes for stable references);
* `toJson()/fromJson()`: base handles id/name/type/suppressed/properties/createdBodies;
  override to add keys and call the base.
* `browserFolder()`: `"Bodies" | "Sketches" | "Construction" | "Joints" | ""`.

Register types with `FeatureFactory::instance().registerType("Extrude", creator)` inside
`registerModule()`. Unknown types on load become `PlaceholderFeature` (status Error, raw JSON
preserved and written back).

### 4.4 Property (`Property.h`)

`Property` = name + `PropertyType` + value. Types: `Bool, Int, Double, Length, Angle, String,
Enum, Vec3, Ref, RefList, Json`.

* `Length`/`Angle`/`Double` may carry an **expression** (`setExpression("width/2 + 3 mm")`).
  `RecomputeContext::eval(prop)` evaluates it in the document's parameter scope, caches the
  value in the property, and reports errors as feature errors. Values are always mm / deg.
* `Enum` has an option list; JSON stores index *and* name (robust to reordering).
* `Ref`/`RefList` hold `ShapeRef`s; `setRefFilter(mask)` tells auto-generated dialogs which
  kinds the user may pick; `setMaxRefs(n)`.
* `PropertyFlags`: `hidden`, `readOnly`, `output`.
* `label()`, `tooltip()` for dialogs.

Dialog generation (ui agent): a generic `PropertyEditor` can render any feature's properties
(length fields with expressions, enum combos, selection inputs). Modules may still provide a
custom `ITool::createDialog()`.

### 4.5 Units and expressions (`Units.h`, `Expression.h`)

`Quantity{value, Dimension}`; base units mm and degrees; `Dimension` tracks integer powers
of length and angle. `evaluateExpression(text, ctx, expected)` supports `+ - * / ^`,
parentheses, units (`mm cm m in " ft deg ° rad`), functions (`sin cos tan asin acos atan
atan2 sqrt abs min max floor ceil round`, `pi`), and parameter names via a lookup callback.
Bare numbers are scalars; a scalar is promoted to the other operand's dimension (or to the
expected dimension at the end) using the document display unit, so with inch documents
`"2"` for a length means 2 in. `formatQuantity()` renders for display.

### 4.6 Body (`Body.h`) and Component (`Component.h`)

`Body` is a record (`id, name, component, creator, visible, color, material`). Its shape
lives in the document's recompute state: `Document::bodyShape(id)` for the current timeline
position, `RecomputeContext::bodyShape(id)` for the state before a feature.

`Component` is a tree node with `placement` (gp_Trsf relative to parent), `grounded`,
`visible`, optional `definition` (occurrence of another component), `externalPath`, BOM
fields. Bodies belong to exactly one component. Core stores the tree; the assembly module
owns joints (features) and placement solving (it writes `placement` via
`makeComponentEditCommand` / `notifyComponentChanged`).

### 4.7 ShapeRef (`ShapeRef.h`) and GeometryRefs (`GeometryRefs.h`)

`ShapeRef` is the universal "thing the user picked / a feature input":

```
owner    ObjectId   body id (Vertex/Edge/Face/Body), feature id (Sketch*, Profile,
                    Construction*, Feature, Joint...), component id, kOriginId
kind     ShapeKind  Vertex Edge Face Body | SketchPoint SketchCurve Profile Sketch |
                    ConstructionPlane/Axis/Point | OriginPlane/Axis/Point | Component Joint
                    JointOrigin Feature DrawingView DrawingEntity
index    int        B-rep: 1-based index in TopTools_IndexedMapOfShape(ownerShape, kind)
                    Sketch: geometry id.  Drawing: view/entity index.
subIndex int        SketchPoint role (Start/End/Center/Mid/Standalone)
name     string     stable name: history name "Extrude1/End", origin "XY", profile "1,2,3"
signature GeomSignature  fingerprint for re-matching B-rep sub-shapes
point    Vec3       pick point (B-rep) / seed point inside the region (Profile)
```

Factories: `ShapeRef::subShape(bodyId, bodyShape, sub)`, `::face/edge/vertex/body`,
`::originPlane("XY")`, `::profile(sketchId, seed, curveIds)`, `::sketchCurve`,
`::sketchPoint`, `::feature(id, kind)`, `::component(id)`. `ShapeKind` doubles as a bitmask
(`ShapeKindMask`, `kPlaneKinds`, `kAxisKinds`, `kPointKinds`, `kBrepKinds`).

`GeometryRefs.h` resolves refs against a body-state map: `resolvePlane` (origin plane,
planar face, construction plane, sketch plane), `resolveAxis` (origin axis, linear edge,
circle axis, cylinder axis, construction axis), `resolvePoint`, `resolveShape`,
`resolveProfileFace`, `makeBrepRef`. Features use the same through `RecomputeContext`.

## 5. Recompute model and timeline

* Features `[0, rollbackIndex)` are active. Each feature keeps the complete
  `bodyStatesAfter_` map (body id -> `TopoDS_Shape`, cheap handles). The state before feature
  *i* is `features[i-1]->bodyStatesAfter_`; rolling back is a lookup, no recompute.
* `Document::markDirty(id)` records the smallest dirty index; `notifyFeatureChanged(id)` does
  this for you. `recompute()` re-evaluates parameters, then replays features from the first
  dirty index to the marker. Nothing recomputes implicitly: **mutate, then call
  `recompute()`** (the core undo commands do).
* For each feature the Document builds a `RecomputeContext` with the state before it. The
  feature reads `ctx.bodyShape(id)`, `ctx.resolve(ref)`, `ctx.plane(ref)`,
  `ctx.profileFace(ref)`, `ctx.length("distance")`, `ctx.feature(id)` /
  `ctx.featureAs<sketch::SketchFeature>(id)` (upstream only), and writes
  `ctx.setBodyShape(id, shape)` or `ctx.createBody(name, shape, component)`.
  `createBody` matches slots in call order, so a feature creating N bodies calls it N times
  on every recompute and keeps stable body ids.
* Errors: `ctx.setError(msg)` or any thrown `Standard_Failure`/`std::exception` -> status
  `Error`, the feature's effect is skipped (state passes through). `ctx.setWarning(msg)` ->
  `Warning`. Features whose inputs have errors get `Warning` ("input X has errors").
  Suppressed features pass the state through with status Ok/"suppressed".
* Features after the marker are `Stale`. New features are inserted **at the marker**
  (`addFeature(f, -1)`), which then advances.
* Dependencies (`dependenciesOf`, `dependentsOf`, `canMoveFeature`): ref owners that are
  features, plus for body refs all earlier features that created or modified that body
  (`touchedBodies()`). Reordering across a dependency is rejected with a reason string.
* Caching is deliberately simple for the MVP (replay from first dirty index). The hook for
  input-hash caching is `Feature::inputsToJson()`; add it later without changing the API.

## 6. Undo/redo (`UndoStack.h`, `Commands.h`)

Command pattern: `UndoCommand { redo(); undo(); text(); mergeId(); mergeWith(); }`.
`UndoStack::push(cmd)` executes `redo()` and records; `beginMacro/endMacro` group; merge
support for drags; `setClean/isClean` for the modified flag. Ready-made commands:

| Command | Does |
|---|---|
| `AddFeatureCommand(doc, unique_ptr<Feature>, index=-1)` | insert at marker, recompute; undo removes |
| `RemoveFeatureCommand(doc, id)` | remove; undo re-inserts at the same index |
| `ModifyFeatureCommand(doc, id, edit-lambda)` or `(doc, id, beforeJson, afterJson)` | property edits with recompute |
| `MoveFeatureCommand`, `SetRollbackCommand`, `SetParameterCommand` | timeline/parameter edits |
| `makeBodyEditCommand`, `makeComponentEditCommand` | name/visibility/color/placement edits |
| `DocumentSnapshotCommand(doc, text, edit)` | whole-document JSON snapshot fallback (rare structural edits) |

Pattern for a feature dialog (OK): `ctx.undo->push(std::make_unique<AddFeatureCommand>(*ctx.document, std::move(feature)));` then `ctx.finish()`. Live preview during the dialog uses
`IViewerContext::showPreviewShape`, never a document mutation. Editing an existing feature:
keep `before = f->toJson()` at dialog open, apply edits directly to the feature plus
`doc.notifyFeatureChanged(id); doc.recompute();` for preview, and on OK push
`ModifyFeatureCommand(doc, id, before, f->toJson())` (which is already applied, so its first
redo is a no-op re-apply); on Cancel `f->fromJson(before); notifyFeatureChanged; recompute`.

## 7. Selection model and references

`SelectionModel` (`Selection.h`, owned by the Document): ordered `ShapeRef` items,
`filter()` mask (tools restrict what is pickable; the viewer mirrors it), `preselection()`
(hover), `removeOwner(id)` on deletion, signals `changed / filterChanged /
preselectionChanged`. The viewer converts AIS picks into `ShapeRef`s with
`makeBrepRef(doc, bodyId, subShape)` (index + signature) and sketch picks into
`ShapeRef::sketchCurve/sketchPoint/profile`.

Topological naming mitigation (two layers, see research section 3):

1. **History names.** Part features fill `namedShapes()` from the OCCT maker history
   (`Pad1/End`, `Pad1/Side/3`, `Fillet1/<edge>`), and set `ShapeRef::name` when they can.
   Booleans carry names through `Modified()`. (Part agent's responsibility; the core stores
   the string.)
2. **Geometric signature re-match.** `ShapeRef::signature` (`GeomSignature`: surface/curve
   type, centroid/midpoint, oriented normal/axis, area/length, radius, bbox, vertex count) is
   taken when the ref is created. `resolveSubShape()` first tries the stored index and
   verifies the signature; otherwise scores all candidates (`signatureDistance`) and accepts
   the best if it is under the threshold and clearly better than the runner-up, else the
   result is `ambiguous`. `RecomputeContext::resolve(ref)` updates `ref.index` on success and
   raises a Warning for re-matched or ambiguous refs and "reference lost" on failure
   (Fusion/SolidWorks behaviour: yellow feature, user re-picks).

Profiles are referenced by **seed point + bounding curve ids** (`ShapeRef::profile`), never by
face index; the sketch module's `IProfileSource::profileFace(ref)` re-finds the region
containing the seed.

## 8. Tool/Command registry API (`Tool.h`)

```cpp
struct ToolInfo { id, name, tooltip, icon, shortcut, aliases, workspace, tab, group,
                  order, promoted, repeatable, needsDocument, hidden };
class ITool {
  info(); isEnabled(ctx); isInstant();
  activate(ctx); deactivate(ctx); accept(ctx) /*OK*/; cancel(ctx) /*Esc*/;
  mousePress/Move/Release/DoubleClick(ctx, ViewerMouseEvent) -> bool consumed;
  keyPress(ctx, ViewerKeyEvent) -> bool;  selectionChanged(ctx); documentChanged(ctx);
  commandLineInput(ctx, text) -> bool;   createDialog(ctx, parent) -> QWidget*;
};
struct ToolContext { document, selection, undo, viewer, host, manager, args, preselection;
                     finish(); cancel(); setPrompt(); showMessage(); };
class ToolRegistry { registerTool(unique_ptr<ITool>); tool(id); toolsFor(ws, tab, group);
                     tabs(ws); groups(ws, tab); search(query); byAlias(); byShortcut(); };
class ToolManager : QObject { run(id, args); runCommandLine(line); accept(); cancel();
                     repeatLast(); triggerShortcut(key); mouse*/keyPress forwarding;
                     signals toolActivated/toolFinished/promptChanged; };
```

Conventions:

* Tool ids: `"<module>.<name>"` (`sketch.line`, `part.extrude`, `construct.offset-plane`,
  `assembly.joint`, `drawing.base-view`, `view.fit`, `app.undo`). Names and shortcuts follow
  `docs/research/ux.md` section 9 (E Extrude, Q Press Pull, F Fillet, H Hole, L Line, R
  Rectangle, C Circle, D Dimension, X Construction, P Project, T Trim, O Offset, J Joint...).
* `workspace`/`tab`/`group` place the tool in the toolbar: Design workspace tabs `SOLID`,
  `SURFACE`, `SKETCH` (contextual), `TOOLS`; groups `CREATE`, `MODIFY`, `ASSEMBLE`,
  `CONSTRUCT`, `INSPECT`, `INSERT`, `SELECT`, `CONSTRAINTS`. Drawing workspace tabs: `DRAWING`
  with groups `CREATE`, `MODIFY`, `GEOMETRY`, `DIMENSIONS`, `TEXT`, `TABLES`, `EXPORT`.
  `promoted = true` shows the big icon; others are in the group dropdown.
* `ToolManager::run()` cancels the active tool, copies the selection into
  `ctx.preselection`, echoes the id to the command history, calls `activate()`. On
  `finish()/cancel()` the manager calls `deactivate()`, clears previews, highlights,
  selection filter and prompt, and emits `toolFinished(id, accepted)`.
* Instant tools (`isInstant()`, or `LambdaTool`) do their work in `activate()`.
* Mouse events reach the active tool after the viewer handled navigation (MMB/wheel). A tool
  returning `false` from `mousePress` lets the viewer do default selection.
* Command line: `runCommandLine("EXTRUDE 10")` -> alias lookup -> `run(id, {"10"})`; with an
  active tool the text goes to `commandLineInput()` first (coordinates `x,y`, `@dx,dy`,
  `@len<ang`). Empty line = repeat last.
* Dialogs: `createDialog()` returns the body widget; the shell wraps it with the title and
  OK/Cancel (`wantsDialogButtons()`), routes Enter/Esc to `accept()/cancel()`, and
  `IAppHost::setToolAcceptEnabled(bool)` disables OK while the input is invalid.

## 9. Viewer and host interfaces

`IViewerContext` (`ViewerContext.h`), implemented by `viewer::ViewerWidget`:

* Preview graphics: `showPreviewShape/Lines/Polyline/Points/Text`, `updatePreview*`,
  `removePreview`, `clearPreviews`; `PreviewStyle` presets `join()/cut()/newBody()/
  sketchLine()/inference()`.
* `setHighlighted(refs)`, `setSelectionFilter(mask)`, `setSelectionEnabled`, `pick(x,y,
  kinds, out, hitPoint)`, `pickAll`.
* Sketch mode: `enterSketchMode(plane, lookAt)`, `exitSketchMode`, `sketchPlane`,
  `setGridVisible`, `setGridSnap`. In sketch mode `ViewerMouseEvent::planePoint` is valid.
* Camera: `lookAt`, `setStandardView`, `fitAll`, `fitShapes`, `setProjection`,
  `setDisplayMode`, `viewDirection`, `upDirection`.
* Mapping: `screenToPlane`, `screenRay`, `worldToScreen`, `pixelSize(at)` (snap tolerances
  = N px * pixelSize).
* `setGhostedComponents`, `setFeatureVisibilityOverride`, `setCursor`, `requestRedraw`,
  `widget()`.

Persistent display is the viewer's job (it listens to Document signals): bodies (visible,
color), sketch `outputShape()` of sketch features, construction geometry `outputShape()`,
component placements. Tools never display document geometry themselves.

`NullViewerContext` records calls and maps pixels onto the view/sketch plane at
`pixelSize()` mm per pixel, which is enough to drive sketch tools in unit tests.

`IAppHost` (`AppHost.h`), implemented by `app::MainWindow`: `setPrompt`, `showMessage`,
`askYesNo`, `workspace/setWorkspace`, `setContextualTab("SKETCH", on)`, `echoCommand`,
`mainWindow()`, `refreshToolDialog`, `setToolAcceptEnabled`. `NullAppHost` for tests.

## 10. Document file format (.fcad)

Plain JSON (UTF-8, `QJsonDocument` indented), written atomically with `QSaveFile`.
Only the parametric definition is stored; geometry is recomputed on load.

```json
{
  "format": "fabricad", "version": 1, "name": "Bracket",
  "units": {"length": "mm", "angle": "deg", "precision": 3},
  "nextId": 57,
  "parameters": [{"name": "width", "expression": "40 mm", "comment": "overall"}],
  "components": [{"id": 1, "name": "Bracket", "parent": 0, "children": [23], "placement": {"t":[0,0,0],"q":[0,0,0,1]}, "grounded": true, "visible": true}],
  "activeComponent": 1,
  "bodies": [{"id": 18, "name": "Body1", "component": 1, "creator": 16, "visible": true, "color": [0.73,0.74,0.77,1]}],
  "features": [
    {"id": 16, "type": "Sketch", "name": "Sketch1",
     "properties": {"plane": {"type":"Ref","value":{"owner":2,"kind":"OriginPlane","name":"XY"}}},
     "geometry": [...], "constraints": [...]},
    {"id": 17, "type": "Extrude", "name": "Extrude1", "createdBodies": [18],
     "properties": {
       "profiles": {"type":"RefList","value":[{"owner":16,"kind":"Profile","name":"1,2,3,4","pt":[10.2,4.1,0]}]},
       "distance": {"type":"Length","value":10,"expr":"width/4"},
       "operation": {"type":"Enum","value":3,"name":"New Body"}}}
  ],
  "rollbackIndex": 2,
  "extensions": {"drawing": {"sheets": [...]}}
}
```

* `Feature::toJson()` writes `id, type, name, suppressed, properties, createdBodies`;
  subclasses add their own keys (sketch geometry/constraints, joint data).
* `ShapeRef` JSON: `owner, kind, index, sub, name, sig, pt`.
* Unknown feature types load as `PlaceholderFeature` and are written back unchanged.
* `version` must be bumped with a migration step in `Document::fromJson` when the schema
  changes incompatibly; additive changes do not need a bump.
* Future: zip container (`document.json` + `cache.brep` + `thumbnail.png`); keep
  `Document::load()` the single entry point so this stays transparent.

## 11. Threading, performance, testing

* The Document and everything Qt is GUI-thread only. Recompute is synchronous now; features
  may poll `ctx.isCancelled()` in long loops. The drawing module runs HLR on a worker
  (`QtConcurrent`) and posts results back; it must copy the `TopoDS_Shape` handle before
  leaving the GUI thread and never touch Document/AIS from the worker.
* After booleans call `core::unifySameDomain()` (clean faces for picking and drawings).
  Validate with `core::isValidShape()`; on failure set an error and keep the previous shape.
* Meshing/display parameters: see `algorithms.md` section 6 (viewer owner).
* Tests must run with `QT_QPA_PLATFORM=offscreen` and no GL. Use `NullViewerContext`,
  `NullAppHost`, and the test features in `src/core/tests/support/TestFeatures.h` as a model.
* Memory: always build `-j2`. Prefer forward declarations and keep OCCT/Qt includes out of
  widely included headers.

## 12. Coding conventions

* C++20, `#pragma once`, 2-space indent, 120 columns, `PascalCase` types, `camelCase`
  functions/variables, trailing underscore for private members, `k` prefix for constants.
* `std::string` in the data model; `QString` only at the UI boundary (`core::qs()/ss()`).
* Angles in degrees in properties and JSON; radians only inside math (`Quantity::radians()`).
* Length unit is millimetres everywhere internally.
* No exceptions across module boundaries except inside `recompute()` (caught by the
  Document). Return `bool` + `std::string* error` or `std::optional` elsewhere.
* Logging: `FC_INFO/FC_WARN/FC_ERROR(fmt, ...)` (`Log.h`, std::format syntax).
* Every file starts with a one-line purpose comment and `SPDX-License-Identifier:
  LGPL-2.1-or-later`. Copied code keeps its original header and is listed in `THIRD_PARTY.md`.
* Commit nothing from agent sessions unless asked; the integrator commits.

## 13. Per-module notes

### sketch (`src/sketch`, target `fabricad_sketch`)

* Implement `class SketchFeature : public core::Feature, public core::IProfileSource`
  (type name `"Sketch"`, `browserFolder() == "Sketches"`). Properties: `plane` (Ref,
  `kPlaneKinds`), plus geometry/constraints serialised in `toJson()` overrides (not as
  Properties). `recompute()`: resolve the plane via `ctx.plane(ref("plane"))`, solve, build
  profiles, set `outputShape()` to the compound of curve edges (viewer displays it), make
  `profileSource()` return `this`.
* `IProfileSource` is the contract with the part module: `sketchPlane()`, `profiles()`,
  `profileFace(ref)` (match by seed point first, then curve-id overlap), `curveEdge(id)`,
  `allEdges(construction)`, `sketchPoint(ref)`. Profile refs: `ShapeRef::profile(sketchId,
  seed, curveIds)`.
* Sketch geometry ids are stable integers owned by the sketch; curve/point refs use
  `ShapeRef::sketchCurve(sketchId, id)` / `sketchPoint(sketchId, id, role)`.
* Tools: `sketch.create` (plane pick -> enter sketch mode via `ctx.viewer->enterSketchMode`,
  `ctx.host->setContextualTab("SKETCH", true)`), `sketch.finish`, `sketch.line`, ... all with
  `tab = "SKETCH"`. Use preview lines/points for rubber-banding and inference glyphs;
  colour geometry by constraint status by rebuilding the sketch `outputShape()` or by
  providing a richer display structure (coordinate with the ui agent via integration notes).
* Solver: PlaneGCS vendoring recipe or custom Eigen solver per `algorithms.md` section 1.
  Expose DOF count so the status bar can show "Sketch: N DOF".
* Project/include edges from 3D: refs to `Edge/Face` of bodies, resolved with `ctx.resolve`.

### part (`src/part`, target `fabricad_part`)

* One `core::Feature` subclass per feature with typed properties; register all types in
  `registerModule`. Inputs: `profiles` (RefList of Profile/Face), `body`/`bodies`
  (RefList of Body), `edges` (RefList of Edge), `faces` (RefList of Face), `plane`/`axis`
  refs. Use `ctx.profileFace(ref)` to get faces from sketches (`IProfileSource`), and
  `ctx.resolve(ref)` for B-rep refs (writes back the index).
* Target body choice (Join/Cut/Intersect/New Body): find bodies in `ctx.bodies()`; for Join
  with no target, create a body with `ctx.createBody`. After booleans:
  `unifySameDomain` + `isValidShape`.
* Fill `namedShapes()` from maker history (`FirstShape/LastShape/Generated/Modified`) so
  downstream refs can carry names; set `ShapeRef::name` when building refs from your own
  output.
* Construction geometry features: `browserFolder() == "Construction"`, override
  `asPlane()/asAxis()/asPoint()`, set `outputShape()` to a display face/edge/vertex.
* Import STEP/STL as an `Import` feature creating bodies; export tools are instant tools.
* Pattern/Mirror need the "tool solid" of a feature: read the source feature's
  `outputShape()` (store your tool solid there) or recompute delta = after - before.

### assembly (`src/assembly`, target `fabricad_assembly`)

* Components are core objects (`Document::addComponent`, `Component::placement`,
  `worldPlacement`). Occurrences: set `Component::definition`. Ground: `Component::grounded`.
* Joints are features (`type "Joint"`, `browserFolder() == "Joints"`, `ShapeKind::Joint`):
  properties `componentA/componentB` (Ref Component), `originA/originB` (Json: gp_Ax3 in
  component-local coordinates + the picked ShapeRef), `type` (Enum), `value/limits`.
  `recompute()` runs the tree solver and writes placements (`doc.component(id)->placement`
  + `notifyComponentChanged`). Set `outputShape()` to a glyph shape if you want the viewer
  to draw it, or leave it null and draw glyphs via preview during tools.
* Snap-point picking uses `ctx.viewer->pick()`/selection with `kBrepKinds`; compute frames
  with `resolvePlane/resolveAxis/resolvePoint`.
* Manipulators (triad) are preview graphics + mouse handling in your tool; the viewer does
  not provide an `AIS_Manipulator` wrapper yet (ask via integration notes if you need one).
* BOM: `Component::partNumber/description/material` + body materials; expose a function
  `assembly::buildBom(const core::Document&)` for the drawing module (header in your dir).
* Assembly STEP export: XCAF, see research section 5.

### drawing (`src/drawing`, target `fabricad_drawing`)

* Sheets, views, dimensions live in your own classes and are stored in
  `Document::extensionData("drawing")` (write on `Document::aboutToSave`, read on `loaded`
  and `documentReset`). Undo via `DocumentSnapshotCommand` or your own commands.
* Views reference model geometry through `ShapeRef` (bodies/components); recompute views on
  `Document::recomputeFinished`.
* Canvas: `QGraphicsView`, no GL. Drawing tools register with `workspace =
  Workspace::Drawing`, tab `"DRAWING"`; the app swaps the central widget when the workspace
  changes (`IAppHost::setWorkspace`). Provide `drawing::DrawingWorkspaceWidget` (a QWidget)
  that the app can instantiate; document the header in your integration notes.
* HLR per research section 4; run it off the GUI thread with `QtConcurrent`.
* Export: `QPdfWriter`, `QSvgGenerator`, own DXF R12 writer.

### ui (`src/viewer` + `src/app`, targets `fabricad_viewer`, `fabricad`)

* `viewer::ViewerWidget` implements `core::IViewerContext` on OCCT AIS/V3d in a
  `QOpenGLWidget` (copy the MIT sample listed in `THIRD_PARTY.md`). Keep the class name and
  `setDocument()/setToolManager()` so `MainWindow` compiles. Translate Qt events into
  `ViewerMouseEvent` (ray, plane point in sketch mode, pick as ShapeRef via `makeBrepRef`)
  and call `ToolManager::mouse*`; do default selection only when the tool did not consume
  the event. Mirror `SelectionModel::filter()`.
* Display sync: bodies from `Document::liveBodies(true)` + `bodyChanged`; sketch and
  construction `outputShape()`s from features with status Ok and visible flag; components'
  `worldPlacement`. Colours and gestures per `ux.md` sections 5-6 and 12.
* `app::MainWindow` implements `core::IAppHost`, owns `Document`, `ToolRegistry`,
  `ToolManager`, builds the toolbar from `ToolRegistry::tabs/groups/toolsFor`, the S-box
  from `search()`, shortcuts from `ToolInfo::shortcut` -> `ToolManager::triggerShortcut`,
  command dialogs from `ITool::createDialog`, browser tree and timeline from Document
  signals, and the command line from `runCommandLine`.
* Startup must stay fast: no work before the window shows; create the GL context after
  `show()`.
