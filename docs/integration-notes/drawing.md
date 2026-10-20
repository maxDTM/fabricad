# Integration notes: drawing module

Owner: drawing agent. Status of the module: complete for the MVP scope (sheets, base /
projected / section / detail views with exact HLR on a worker, hidden + tangent edges,
centre marks/lines, associative dimensions, notes/leaders, parts list + balloons, PDF/SVG/DXF/
CSV export, QGraphicsView canvas, workspace widget, 28 tools). Everything builds with the
module's own `build-drawing/` and is tested headless (`ctest -R fabricad_drawing`: 5 test
executables, all passing).

## 1. What the module provides

| Header | Purpose |
|---|---|
| `drawing/DrawingModule.h` | `void registerModule(core::ModuleContext&)` registers all `drawing.*` tools (no feature types). |
| `drawing/DrawingWorkspaceWidget.h` | **The central widget for `Workspace::Drawing`** (see section 2). |
| `drawing/DrawingCanvas.h` | `QGraphicsView` sheet canvas; `ICanvasInteraction` is how tools receive input; `CanvasHit` hit tests; `simulate*()` for scripting/tests. |
| `drawing/DrawingModel.h` | Per-document model (sheets/views/annotations), HLR jobs on `QtConcurrent`, persistence in `Document::extensionData("drawing")`, undo (`DrawingSnapshotCommand`). `DrawingModel::forDocument(doc)` creates it on demand as a child QObject of the Document. |
| `drawing/Sheet.h`, `DrawingView.h`, `Dimension.h`, `Annotations.h` | Value types (persisted as JSON). |
| `drawing/Projection.h`, `SectionCut.h`, `Geometry2D.h` | HLR projection (`projectShapes`: exact `HLRBRep_Algo`, or `HLRBRep_PolyAlgo` for previews), section cutting + hatching, 2D curve maths. Worker-thread safe, no Document/AIS. |
| `drawing/SheetRenderer.h` | Display lists (sheet mm) + `QPainter` painting shared by canvas, PDF and SVG; the DXF writer walks the same lists. |
| `drawing/DrawingExport.h`, `DxfWriter.h` | `exportPdf/exportSvg/exportDxf/exportPartsListCsv`, DXF R12 writer. |
| `drawing/tools/DrawingTools.h` | `registerDrawingTools(ToolRegistry&)`, `ExportTool`; `ViewTools.h` and `AnnotationTools.h` hold the tool classes; `DrawingToolBase.h` is the shared base (+ `PreviewRunner`). |

Tool ids (workspace `Drawing`, tab `DRAWING`), by group:

| Group | Tools |
|---|---|
| CREATE | `drawing.create` (New Drawing), `drawing.new-sheet`, `drawing.base-view`, `drawing.projected-view` (P), `drawing.section-view`, `drawing.detail-view`, `drawing.move` (M), `drawing.delete` |
| MODIFY | `drawing.edit-view`, `drawing.edit-sheet`, `drawing.edit-title-block`, `drawing.toggle-hidden-lines`, `drawing.toggle-tangent-edges`, `drawing.delete-sheet` |
| GEOMETRY | `drawing.center-mark`, `drawing.centerline`, `drawing.auto-center-marks` |
| DIMENSIONS | `drawing.dimension` (D), `drawing.edit-dimension` |
| TEXT | `drawing.text` (T), `drawing.leader` (L) |
| TABLES | `drawing.parts-list`, `drawing.balloon`, `drawing.balloon-all` |
| EXPORT | `drawing.export-pdf`, `drawing.export-svg`, `drawing.export-dxf`, `drawing.export-csv` |

All drawing tools are enabled only while `IAppHost::workspace() == Workspace::Drawing` (so the
single-letter shortcuts do not collide with sketch keys), except `drawing.create` which works
from the Design workspace too: it adds the first sheet (dialog: ISO/ASME standard, sheet size
A4..A0 / ANSI A..E, scale) and calls `host->setWorkspace(Workspace::Drawing)`.

Every tool also works from the command box / scripting with arguments, e.g.
`BASEVIEW front 1:2 150,120`, `PROJ right above iso`, `SECTION 150,100 150,140 230,120`,
`DETAIL 150,110 6 250,200`, `DIM 150,105 150,90`, `TEXT 50,50 GENERAL NOTES`,
`EDITVIEW scale=1:2 hidden=on`, `EDITSHEET size=A2 standard=asme`, `TITLEBLOCK Title=Bracket`,
`PDF /tmp/out.pdf`. Points are sheet millimetres (origin top-left, Y down).

## 2. App wiring the ui agent must do

1. **Host the workspace widget.** When `IAppHost::setWorkspace(Workspace::Drawing)` is chosen,
   swap the central widget for a `drawing::DrawingWorkspaceWidget` (create once, keep it):

   ```cpp
   #include "drawing/DrawingWorkspaceWidget.h"
   drawingWidget_ = new drawing::DrawingWorkspaceWidget(this);
   drawingWidget_->setDocument(document_);          // again whenever the Document object is replaced
   drawingWidget_->setToolManager(toolManager_);    // Esc/Enter/Delete + double-click edits run tools
   // setWorkspace(Drawing): stacked->setCurrentWidget(drawingWidget_); show the DRAWING toolbar tab
   // setWorkspace(Design):  stacked->setCurrentWidget(viewerWidget_)
   ```

   The widget holds the sheet browser (left), the canvas (centre), sheet tabs + zoom/cursor
   status (bottom). Tools find the canvas through the static
   `DrawingWorkspaceWidget::activeCanvas(document)`; nothing else has to be passed. Do **not**
   route viewer mouse events to drawing tools: the canvas delivers its own input to the active
   tool (`ICanvasInteraction`), and `ToolManager::finishActive` detaches it.
2. **Toolbar.** Build the DRAWING tab from `ToolRegistry::toolsFor(Workspace::Drawing, "DRAWING",
   group)` with the groups in the order `CREATE, MODIFY, GEOMETRY, DIMENSIONS, TEXT, TABLES, EXPORT`.
   `promoted` tools: Base View, Projected View, Section View, Detail View, Edit View, Center Mark,
   Centerline, Dimension, Text, Leader, Parts List, Balloon, Output PDF, Output DXF.
3. **Command dialogs.** Drawing tools return field widgets from `createDialog()` like the other
   modules (Base View: orientation / style / scale / tangent edges; Edit View; Edit Sheet; Edit
   Title Block; Text; Edit Dimension; New Drawing). `DimensionTool::wantsDialogButtons()` is
   false (it only shows a type selector). Previews are drawn on the canvas; OK = `accept()`,
   Cancel reverts live edits. **Without a dialog shell** (`IAppHost::mainWindow()` null, as in
   tests and scripts) tools commit on the placing click; with a window they wait for OK (Fusion).
4. **Workspace switcher.** "DRAWING > From Design" = `toolManager->run("drawing.create")`.
5. **Selection.** The canvas mirrors its selection into `Document::selection()` as
   `ShapeRef{owner = view/entity id, kind = ShapeKind::DrawingView / DrawingEntity, index 0}`. The
   viewer and browser should ignore these kinds (or show them as drawing items). The browser may
   list sheets from `DrawingModel::find(doc)->sheets()` and listen to `sheetsChanged / sheetChanged`.
6. **Icons.** `ToolInfo::icon == id` for every drawing tool (e.g. `drawing.base-view`); add SVGs
   with those names under `resources/icons/`.
7. **Document lifecycle.** The model connects itself to `aboutToSave / loaded / documentReset /
   recomputeFinished`. `DrawingModel::waitForIdle()` finishes pending HLR jobs (export does this
   itself); `busyChanged(bool)` can drive a status-bar "Updating views" indicator.
8. **Shutdown order.** Destroy the `ToolManager` before the `IAppHost` (it clears the host prompt
   when a tool is still active) and the workspace widget before the Document.

## 3. Threading notes

HLR runs through `QtConcurrent::run(runViewJob, job)`, one job per view. `ViewJob` carries deep
copies of the placed shapes (`BRepBuilderAPI_Transform(copy = true)`, made on the GUI thread in
`DrawingModel::collectSources`). Workers never touch the Document, AIS or widgets; results are
applied in the `QFutureWatcher::finished` slot on the GUI thread and dropped when stale
(generation counter per view). Tool previews use the fast `HLRBRep_PolyAlgo` path
(`ProjectionInput::exact = false`) through `PreviewRunner`; committed views use the exact
algorithm unless "Exact" is unticked in Edit View. Tests set `DrawingModel::setSynchronous(true)`.

## 4. Requests for core / other modules

## 2026-10-19 — assembly::buildBom adapter                         (status: open — left as is, both implementations agree on the e2e assembly)
What I need: nothing blocking. `drawing/Annotations.h` has a local `drawing::buildBom()` (one row
per leaf component definition with occurrence counting; one row per body in part documents; body
mass from density) that mirrors `assembly::buildBom()` (`assembly/Bom.h`).
Why: `assembly/Bom.h` appeared while the drawing BOM was already written and tested; both use
the same grouping rules. For a single source of truth, replace the body of `drawing::buildBom()`
with a call to `assembly::buildBom(doc, {root, flatten = true})` mapped onto `drawing::BomRow`
(item, quantity, partNumber, description, material, mass kg = g / 1000, keys = occurrences).
`fabricad_drawing` already links `fabricad_assembly`.
Workaround in place: local implementation in `src/drawing/Annotations.cpp`.

## 2026-10-19 — ShapeKind::DrawingView / DrawingEntity in the viewer   (status: done — viewer/browser only resolve B-rep kinds)
What I need: `viewer::ViewerWidget` and the browser must tolerate selection items of kind
`DrawingView` / `DrawingEntity` (owner = drawing object id, index 0, name = hit kind) and not try
to resolve them as B-rep. The canvas writes them with `SelectionModel::set()`.
Why: tools read `ctx.preselection` to find the parent view (Fusion: select the view, press P).
Workaround in place: none needed on the drawing side.

## 2026-10-19 — TestFeatures.h used by drawing tests                 (status: info)
The drawing tests include `core/tests/support/TestFeatures.h` (BoxFeature / HoleFeature) as the
architecture suggests. Keep that header stable or tell the drawing agent if it moves.

## 5. Third-party code

No code was copied from FreeCAD or elsewhere into `src/drawing`. The HLR recipe
(`Projection.cpp`), the section cut (`SectionCut.cpp`) and the DXF R12 layout (`DxfWriter.cpp`)
are re-implementations of the approaches documented in `docs/research/freecad-occt.md` section 4
and `docs/research/algorithms.md` section 3 (FreeCAD TechDraw `GeometryObject.cpp` /
`DrawViewSection.cpp` were reference reading only). Nothing to add to `THIRD_PARTY.md`.

## 6. Known limits / follow-ups (not blocking integration)

* Section views: one straight cut line (no offset/aligned sections); a single hatch angle and
  spacing for all materials.
* Detail views are 2D clips of the parent's projected curves (exact curves, parent's hidden-line
  state); they do not re-run HLR.
* Dimensions: linear / horizontal / vertical / aligned / radius / diameter / angle; no ordinate,
  baseline or chain dimensions yet. Associativity is by 3D source edge (body id + edge index) with
  a view-local position fallback.
* Break views, GD&T symbols, hole and revision tables are out of scope for the MVP.
* Fonts: "DejaVu Sans" (`DrawingStyle::fontFamily`); bundle osifont later if wanted.
