# Integration notes: assembly module

Owner: assembly agent. Target `fabricad_assembly` (links `fabricad_part`, `fabricad_core`, `Qt6::Widgets`,
`Eigen3::Eigen`). Entry point `fabricad::assembly::registerModule(core::ModuleContext&)`
(`src/assembly/AssemblyModule.h`). Build dir used: `build-assembly/` (`-j2`).

## 1. What was built (file map)

| File | Content |
|---|---|
| `JointTypes.h/.cpp` | Joint catalogue (Rigid, Revolute, Slider, Cylindrical, Pin-slot, Planar, Ball), DOF info, motion transform `M(q)` and its inverse `jointValuesFromMotion`. |
| `JointFrame.h/.cpp` | Fusion-style snap points: `snapCandidates(doc, ref)` for faces (centre, edge midpoints/ends, circle centres, cylinder axis start/middle/end), edges, vertices, bodies, components, origin/construction geometry (via `core::resolvePlane/Axis/Point`). `JointOrigin` (ref + component + snap mode + component-local `gp_Ax3`, JSON via `core::ax3ToJson`), `makeJointOrigin`, `refreshJointOrigin` (re-resolves the ref after model edits; falls back to the stored frame with a warning). |
| `AssemblySolver.h/.cpp` | Pure-math solver. Phase 1: tree propagation from grounded components (`W_A = W_B L_B Offset M(q) Align L_A^-1`, both traversal directions). Phase 2: Levenberg-Marquardt fallback (Eigen, 6 unknowns per free component, finite-difference Jacobian) for closed loops; free DOF of undriven joints stay free (four-bar linkages work), `lockMotion` drives a joint. Reports per-joint status (Ok/Numeric/Conflict/Skipped), realised motion values, residual and remaining DOF. As-built frames, residuals and `currentValues` (inverse kinematics for dragging) are building blocks. |
| `JointFeature.h/.cpp` | Timeline features: `Joint` (`browserFolder()=="Joints"`, properties `componentA/B` Ref(Component), `originA/B` Json, `type` Enum, `flip`, offsets X/Y/Z + rotation, motion values `angle/distance/...` (visibility per type), limits, `lockMotion`, `asBuilt`); `RigidGroup`; `NewComponent` (timeline marker); `InsertDesign` (bodies of an external `.fcad`, cached by mtime). `IJointProvider` lets any feature contribute solver joints. `gatherAssembly/applySolverResult/solveAssembly` helpers. Joint glyphs are set as `outputShape()` (compound of edges at the joint frame). |
| `ComponentOps.h/.cpp` | Occurrence helpers (`definitionOf`, `displayedBodies`, `bodyInstances` = world shapes, `componentWorldShape`, `componentLabel` "Name:2"), `componentOfRef`, `setWorldPlacement`, snapshot/placement undo commands, `makeGroundCommand`, `createComponent` / `createOccurrence` / `deleteComponent` (snapshot-undo, joints referencing deleted components are removed). |
| `Bom.h/.cpp` | `buildBom(doc, BomOptions)` -> `BomTable` rows (item, name, part number, description, material, qty, level, volume, mass, occurrences), grouping occurrences / external links; `bomToCsv`. **This is the API for the drawing module's parts list.** |
| `Interference.h/.cpp` | `checkInterference(doc, components)`: AABB prefilter + `BRepAlgoAPI_Common` per pair, intersection solids in world coordinates, cancel callback. |
| `StepExport.h/.cpp` | `exportAssemblyStep(doc, path, options)`: XCAF document with one part label per definition, `AddComponent` per occurrence with its placement, names and body colours, `STEPCAFControl_Writer` (AP214 default). |
| `Triad.h/.cpp` | Manipulator math: handle geometry (arrows, rings, filled plane squares) as preview segments, ray hit test, drag deltas with optional snapping. No AIS dependency. |
| `tools/ToolCommon.*` | `componentsOf(refs)`, `jointsOf(refs)`, `ExpressionEdit` (QLineEdit evaluating document expressions). |
| `tools/ComponentTools.*` | `NewComponentTool` (N), `RigidGroupTool`, instant bodies: `toggleGround`, `activateSelectedComponent`, `createComponentsFromSelectedBodies`, `deleteSelectedComponents`, `insertDesign`, `exportStep`. |
| `tools/JointTool.*` | Joint (J) / As-built Joint (Shift+J): hover snap glyphs, click snap on component 1 then 2, component 1 jumps live (solver preview, restored on cancel), dialog with motion type, flip, offsets, rotation, value, animate, limits. OK = one undo macro (placement + feature). |
| `tools/MoveComponentTool.*` | Move/Rotate with the triad: free components get all handles, jointed components only their joint's DOF (drag drives the joint value through `AssemblySolver::currentValues`). Dialog with X/Y/Z distances and angles; command line `x,y,z`, `@angle`. |
| `tools/DriveJointTool.*` | Drive Joints: joint combo, slider, value, animate (timer bounces between limits). |
| `tools/InterferenceTool.*` | INSPECT > Interference: table of pairs + red preview solids. |
| `AssemblyModule.cpp` | Registers the four feature types and all tools (section 3). |

No FreeCAD or other third-party code was copied or adapted into `src/assembly`; the joint model follows
`docs/research/algorithms.md` section 4 / `freecad-occt.md` section 5 only. **No `THIRD_PARTY.md` update is needed for this module.**

## 2. Tests

`src/assembly/tests/` (all headless, `QT_QPA_PLATFORM=offscreen`, no GL):

* `test_assembly_solver.cpp` - motion round trips, face mating, flip, drive + offset, chains traversed backwards,
  floating groups, consistent/inconsistent loops, four-bar linkage (numeric phase + driven crank), as-built frames.
* `test_assembly_features.cpp` - snap candidates on faces/cylinders/edges, Joint feature recompute + JSON round trip
  + snap-back after a free move, revolute drive/limits/type switch, as-built joint, rigid group, BOM with occurrences,
  interference, STEP export read back with `STEPCAFControl_Reader`, external design insertion, component helpers/commands.
* `test_assembly_tools.cpp` - tools through `core::ToolManager` + `NullViewerContext`/`NullAppHost`: triad hit/drag math,
  New Component, Joint tool (live preview, offsets/type/flip, commit as one undo step, cancel restores), As-built joint,
  Move tool (free + jointed), Drive Joints with limits, Ground/Rigid Group/Components-from-bodies/Activate/Delete,
  Interference tool, export STEP/BOM + Insert Design via command-line args, dialog construction for every interactive tool.
* `tests/support/AssemblyTestHelpers.h` - two-component fixture.

Run: `ctest --test-dir build-assembly -R assembly --output-on-failure`.

## 3. Tool ids the app must place (toolbar / menus / shortcuts)

All registered with `workspace = Design`. Tab/group follow `ToolInfo`; the builder only needs `toolsFor()`.

| Id | Name | Shortcut / aliases | Tab > Group | Notes |
|---|---|---|---|---|
| `assembly.new-component` | New Component | `N`; NEWCOMPONENT, NC | SOLID > ASSEMBLE (promoted) | dialog; `ctx.args[0]` = name |
| `assembly.joint` | Joint | `J`; JOINT | SOLID > ASSEMBLE (promoted) | dialog; needs viewport hover/pick (section 4.2) |
| `assembly.as-built-joint` | As-built Joint | `Shift+J`; ASBUILTJOINT | SOLID > ASSEMBLE (promoted) | dialog |
| `assembly.ground` | Ground (toggle) | GROUND | SOLID > ASSEMBLE | instant; enabled with a component selection; **also browser context menu** |
| `assembly.rigid-group` | Rigid Group | RIGIDGROUP | SOLID > ASSEMBLE | dialog |
| `assembly.move` | Move Component | MOVECOMPONENT, MC | SOLID > ASSEMBLE | dialog + triad in viewport; **also "Move" context-menu entry (M key if the ui agent maps Fusion's M)** |
| `assembly.drive-joint` | Drive Joints | DRIVEJOINT | SOLID > ASSEMBLE | dialog; `args = {jointName, value}` |
| `assembly.activate` | Activate Component | ACTIVATE | SOLID > ASSEMBLE | instant; **browser radio button**: run with the component selected (or `args[0]` = name); root when nothing is selected |
| `assembly.components-from-bodies` | Create Components from Bodies | CFB | SOLID > ASSEMBLE | instant; **browser context menu on bodies** |
| `assembly.delete-component` | Delete Component | DELETECOMPONENT | hidden | instant; **browser context menu** |
| `assembly.insert-design` | Insert Design | INSERT | SOLID > INSERT (promoted) | instant; file dialog when `host->mainWindow()` is non-null, else `args[0]` |
| `assembly.interference` | Interference | INTERFERENCE, CLASH | SOLID > INSPECT (promoted) | dialog with results table |
| `assembly.export-step` | Export STEP (assembly) | EXPORTSTEP, STEP | TOOLS > EXPORT | instant; file dialog or `args[0]`; selected component = sub-tree |
| `assembly.export-bom` | Export BOM (CSV) | EXPORTBOM, BOM | TOOLS > EXPORT | instant |

Icons requested (resource names, `ToolInfo::icon`): `assembly-new-component`, `assembly-joint`, `assembly-as-built-joint`,
`assembly-ground`, `assembly-rigid-group`, `assembly-move`, `assembly-drive-joint`, `assembly-activate`,
`assembly-components-from-bodies`, `assembly-delete-component`, `insert-design`, `inspect-interference`, `export-step`,
`export-bom`. Feature icons (`Feature::iconName()`): `joint-rigid`, `joint-revolute`, `joint-slider`, `joint-cylindrical`,
`joint-pin-slot`, `joint-planar`, `joint-ball`, `rigid-group`, `component`, `insert-design`.

## 4. What the integrator / ui agent must wire

### 4.1 App
* Call `assembly::registerModule(ctx)` after part and before drawing (as in ARCHITECTURE.md); this registers the feature
  types `Joint`, `RigidGroup`, `NewComponent`, `InsertDesign` (needed before `Document::load`) and all tools above.
* `InsertDesignFeature::clearCache()` should be called on an explicit "refresh linked designs" action (the cache is
  keyed by path + mtime, so edits on disk are picked up automatically on the next recompute anyway).

### 4.2 Browser tree
* Components: from `Document::components()` / `componentAdded/Removed/Changed` signals. Label with
  `assembly::componentLabel(doc, id)` ("Bracket", "Bracket:2" for occurrences). Grounded -> pin icon. Radio button ->
  `ToolManager::run("assembly.activate")` with that component selected. Context menu: Ground/Unground
  (`assembly.ground`), Activate, Move (`assembly.move`), Delete (`assembly.delete-component`), Create Components from
  Bodies (`assembly.components-from-bodies`, on body nodes).
* "Joints" folder per component: features whose `browserFolder() == "Joints"` (Joint, RigidGroup). Selecting one should
  put `ShapeRef::feature(id, ShapeKind::Joint)` into the SelectionModel (Drive Joints picks it up).
* Timeline: `NewComponent` and `InsertDesign` markers are ordinary timeline features (`isTimelineVisible() == true`).

### 4.3 Viewer (ui agent) - requests
1. **Component placements**: body shapes are component-local. Display each body with
   `doc.worldPlacement(body->component)` (`AIS_Shape::SetLocalTransformation`), refresh on `componentChanged`.
2. **Occurrences**: a component with `definition != kInvalidId` owns no bodies; it displays the definition's bodies
   (`doc.bodiesOf(definitionOf(...))`) under its own placement (`AIS_ConnectedInteractive` or a second `AIS_Shape`).
   The viewer may not link `fabricad_assembly`, so the two-line definition chase (`Component::definition` until
   `kInvalidId`) has to be duplicated there, or moved into core (see 5.2).
3. **Joint glyphs**: display `outputShape()` of features with `type() == "Joint"` (white edges, black outline, on top,
   per ux.md 6.1), hidden when the feature is suppressed. Picking a glyph should yield
   `ShapeRef::feature(jointId, ShapeKind::Joint)`.
4. **Picks carry hit points**: the Joint tool needs `ViewerMouseEvent::hasPick/pick/pickPoint` on mouse move (snap glyphs)
   and on press, and `IViewerContext::pick(x, y, kBrepKinds, out, &hitPoint)`. B-rep refs must be made with
   `core::makeBrepRef(doc, bodyId, sub)` against the **component-local** body shape (index/signature), while
   `pickPoint`/`hitPoint` are in **world** coordinates (the tool converts to component-local itself).
5. **Component picking**: with "Select Component" priority, picks should produce `ShapeRef::component(id)`; body/face
   picks are fine too (`componentsOf()` maps them to the owning component).
6. `setGhostedComponents` is used by Activate (in-context editing) and `setSelectionEnabled(false)` during triad drags.
7. No `AIS_Manipulator` wrapper is needed: the triad is drawn with `showPreviewLines` and hit-tested with the mouse ray
   (`ViewerMouseEvent::rayOrigin/rayDir` must be valid on press/move/release). Preview `onTop` + `lineWidth` should be
   honoured so the triad stays visible inside solids.

### 4.4 Drawing module (parts list / balloons)
Include `assembly/Bom.h`; call `assembly::buildBom(doc)` (options: root component, flatten, include root bodies).
`BomRow::item` is the balloon number, `occurrences` the component ids to attach balloons to, `definition` the
component whose geometry the row shows, `material` the component's material or the bodies' materials joined by ", ".
`bomToCsv()` for export. For world geometry of a component use `assembly::bodyInstances(doc, {compId})` /
`componentWorldShape(doc, compId)` (`assembly/ComponentOps.h`).

## 5. Core change requests

## 2026-10-19 — Document::fromJson wipes the undo stack (DocumentSnapshotCommand crashes)   (status: done — fromJson keeps the stack, load()/clear() reset it; core test `snapshotCommandInsideMacroKeepsUndoStack`)
What I need: `Document::fromJson()` (Document.cpp line ~816) calls `undo_->clear()`. `core::DocumentSnapshotCommand`
restores through `fromJson`, so its `undo()/redo()` destroys the stack that owns the running command: inside a
`MacroCommand` this is a use-after-free crash (observed: SIGSEGV in `MacroCommand::undo`), standalone it silently wipes
the history and leaves `UndoStack::index_` stale (`setIndex(index_-1)` after the clear -> next Redo indexes an empty
vector). The ARCHITECTURE.md recommendation to use `DocumentSnapshotCommand` for component-tree edits is therefore unsafe.
Why: assembly structural edits (new component, delete component, insert design) need undo.
Workaround in place: `src/assembly/ComponentOps.cpp` no longer uses `DocumentSnapshotCommand`; it has its own
`CreateComponentCommand`, `CreateOccurrenceCommand` and `DeleteComponentCommand` built on public Document APIs
(`addComponent/removeComponent/takeFeature/addFeature`), remapping component ids inside re-inserted features.
Suggested change: remove `undo_->clear()` from `fromJson` (keep it in `load()` and `clear()`), or give
`DocumentSnapshotCommand` a private `Document::replaceContent(json)` that keeps the stack. Add a core test that
undoes a snapshot command inside a macro and then redoes.

## 2026-10-19 — Insert a component with a given id                                 (status: done — `Document::addComponent(Component)`)
What I need: `Component& Document::addComponent(Component c)` (or `insertComponent`) that honours `c.id` and bumps
`nextId_`, so undo/redo of component creation/deletion can restore the original ids.
Why: today a component re-created by redo (after undo) gets a fresh id; features whose redo re-applies JSON that
references the old id (e.g. a Joint added after the component) end in Error after undo/undo/redo/redo.
Workaround in place: assembly commands remap ids inside the features they themselves re-insert; the tests look
restored components up by name.
Suggested change: `Component& addComponent(Component c);` with `c.id == kInvalidId` meaning "allocate".

## 2026-10-19 — Feature-owned component for created bodies                      (status: done — `Feature::component()` captured by addFeature, persisted, used by createBody)
What I need: `RecomputeContext::createBody(name, shape, component = kInvalidId)` picks `doc.activeComponent()` *at
recompute time* for a brand-new body slot. A feature created while component X was active gets its body re-homed if
the first recompute happens after the user activated another component (several features added before one
recompute, scripts, or a load where the active component is read before the features replay). Existing body records
keep their component, so this only affects the first recompute.
Why: component membership must be stable; the assembly tests had to recompute after every feature to get
deterministic ownership.
Workaround in place: tests recompute after each `addFeature`; the tools always recompute immediately.
Suggested change: `Document::addFeature` records `feature->component_ = activeComponent()` (persisted in `toJson`),
and `createBody` defaults to `self_.component()` instead of the live active component.

## 2026-10-19 — Core helper for occurrence resolution                             (status: done — `Document::definitionOf/displayedBodies`; the viewer displays occurrences)
What I need: `ObjectId Document::definitionOf(ObjectId comp) const` (follow `Component::definition` to the end of the
chain) and `std::vector<ObjectId> Document::displayedBodies(ObjectId comp) const`.
Why: the viewer must display occurrences but may not link `fabricad_assembly`; today the logic lives in
`assembly/ComponentOps.h` (`definitionOf`, `displayedBodies`).
Workaround in place: assembly keeps its own implementation; the viewer can duplicate the small loop.
Suggested change: move the two functions into `core::Document` (assembly will forward to them).

## 2026-10-19 — Selection: joint refs on feature removal                          (status: done — confirmed: takeFeature calls SelectionModel::removeOwner; browser/viewer emit ShapeKind::Joint refs)
What I need: confirmation that `Document::takeFeature` calls `SelectionModel::removeOwner(featureId)`, and that
browser/viewer picks of joints produce `ShapeKind::Joint` refs (owner = joint feature id).
Why: Drive Joints / Move use `ShapeKind::Joint | Feature` refs to find the joint; stale refs after deletion would
point at a freed feature.
Workaround in place: the tools re-validate every ref with `doc.featureAs<JointFeature>()` before use; Drive Joints falls
back to the last joint with motion when nothing is selected.

## 6. Known limitations / follow-ups
* Motion Link, contact sets and motion study are not implemented (Fusion features out of MVP scope).
* As-built joints of non-rigid types need an explicit axis/point pick (Fusion behaviour).
* The numeric phase uses a finite-difference Jacobian; it is fast for typical loops (<= a dozen components) but not
  intended for large closed-loop mechanisms. Tree-like assemblies never enter it.
* External designs (`InsertDesign`) are static copies of the file's bodies (name + material kept), refreshed when the
  file changes; there is no in-place editing of the linked file.
* Occurrences share geometry by displaying the definition's bodies; `Body::component` stays the definition, so body
  picks on an occurrence resolve to the definition component (the viewer should report the occurrence id as the
  `ShapeRef::component` when it knows it; see 4.3 item 5).
