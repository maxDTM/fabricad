# Hard-coded colour audit (redesign step 0.1)

`tools/theme/audit_colors.py` (stdlib Python) searches `src/**` (not `third_party`, not tests),
`resources/**/*.qss` and `resources/**/*.svg` outside `resources/icons/` and the generated
`resources/ui/{dark,light}/` for: `#hex`, `QColor(` with a literal argument, `Qt::white/black/...`,
`Quantity_NOC_*`, `Quantity_Color(<numbers>)`, `rgb()/rgba()` (in code: inside string literals),
`setStyleSheet(` and float RGBA literals `{0.x f, 0.x f, 0.x f`. Comments are ignored, and a line
marked `colour-lint: ok` is not counted (two in the theme code: the rendered QSS is applied, and
token colours are formatted as `rgba()`).

The ctest `theme_color_lint` runs it with `--lint` against `tools/theme/color-allowlist.txt`: a file
outside the list may not contain colour literals, and a listed file may not gain more than its count.
Regenerate this table with `python3 tools/theme/audit_colors.py --report`.

## Before (main at 9ac3d78, 2026-10-22)

| File | Hits | Owner | Allowed | Planned action |
|---|---|---|---|---|
| `resources/theme-light.qss` | 178 | — | 0 | **not allow-listed** |
| `src/viewer/ViewerWidget.cpp` | 45 | — | 0 | **not allow-listed** |
| `src/core/Materials.cpp` | 26 | — | 0 | **not allow-listed** |
| `src/app/Theme.cpp` | 24 | — | 0 | **not allow-listed** |
| `src/app/MarkingMenu.cpp` | 23 | — | 0 | **not allow-listed** |
| `src/app/TimelineWidget.cpp` | 23 | — | 0 | **not allow-listed** |
| `src/app/Icons.cpp` | 17 | — | 0 | **not allow-listed** |
| `src/part/tools/PropertyPanel.cpp` | 11 | — | 0 | **not allow-listed** |
| `src/core/Manipulator.cpp` | 9 | — | 0 | **not allow-listed** |
| `src/sketch/SketchSession.h` | 9 | — | 0 | **not allow-listed** |
| `src/app/BrowserTree.cpp` | 8 | — | 0 | **not allow-listed** |
| `src/app/MainWindow.cpp` | 8 | — | 0 | **not allow-listed** |
| `src/drawing/DrawingWorkspaceWidget.cpp` | 8 | — | 0 | **not allow-listed** |
| `src/sketch/ValueInputOverlay.cpp` | 8 | — | 0 | **not allow-listed** |
| `src/viewer/ManipulatorPrs.cpp` | 5 | — | 0 | **not allow-listed** |
| `src/app/CommandSearch.cpp` | 4 | — | 0 | **not allow-listed** |
| `src/app/ParametersDialog.cpp` | 4 | — | 0 | **not allow-listed** |
| `src/assembly/Triad.cpp` | 4 | — | 0 | **not allow-listed** |
| `src/assembly/tools/JointTool.cpp` | 4 | — | 0 | **not allow-listed** |
| `src/core/ViewerContext.h` | 4 | — | 0 | **not allow-listed** |
| `src/drawing/DrawingCanvas.cpp` | 4 | — | 0 | **not allow-listed** |
| `src/sketch/tools/SketchToolBase.cpp` | 4 | — | 0 | **not allow-listed** |
| `src/app/DocumentTab.cpp` | 3 | — | 0 | **not allow-listed** |
| `src/assembly/tools/ToolCommon.cpp` | 3 | — | 0 | **not allow-listed** |
| `src/theme/ColorMath.cpp` | 3 | — | 0 | **not allow-listed** |
| `src/theme/Tokens.h` | 3 | — | 0 | **not allow-listed** |
| `src/app/NavBar.cpp` | 2 | — | 0 | **not allow-listed** |
| `src/app/Ribbon.cpp` | 2 | — | 0 | **not allow-listed** |
| `src/assembly/tools/InterferenceTool.cpp` | 2 | — | 0 | **not allow-listed** |
| `src/drawing/SheetRenderer.h` | 2 | — | 0 | **not allow-listed** |
| `src/app/AppTools.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/assembly/StepExport.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/core/Body.h` | 1 | — | 0 | **not allow-listed** |
| `src/core/Manipulator.h` | 1 | — | 0 | **not allow-listed** |
| `src/core/Materials.h` | 1 | — | 0 | **not allow-listed** |
| `src/drawing/DrawingExport.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/drawing/SheetRenderer.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/part/tools/FeatureTool.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/part/tools/MeasureTool.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/sketch/Sketch.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/sketch/SketchSession.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/theme/QssTemplate.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/theme/Tokens.cpp` | 1 | — | 0 | **not allow-listed** |
| `src/viewer/SceneModel.h` | 1 | — | 0 | **not allow-listed** |
| `src/viewer/occt/OcctView.cpp` | 1 | — | 0 | **not allow-listed** |

Total: 465 hits in 45 files.

The counts are higher than the plan's 2.4 grep because this audit also counts `setStyleSheet(` calls
and `QColor(` with numbers, and the old `theme-light.qss` counts each literal.

## After phase 0 (branch ui/tokens)

Migrated in this phase (ui owner): `resources/theme-light.qss` (deleted; replaced by the token
template `resources/theme/app.qss.tmpl`), `src/app/Theme.{h,cpp}` (replaced by `ThemeManager`),
`MarkingMenu`, `TimelineWidget`, `BrowserTree`, `Icons` (badge palette = `icon.badge.*`),
`MainWindow` (message and sketch-status colours are QSS properties), `CommandSearch`,
`ParametersDialog`, `Ribbon`, `NavBar`, `DocumentTab`, `SketchPalette`, the ui SVGs (generated per
theme from `resources/theme/ui-svg/*.svg.tmpl`), and the viewport background, grid, face edges,
selection and preselection highlight, ViewCube colours, cube controls and rubber band in
`ViewerWidget` (through the new `viewer::ViewerPalette`). `src/app` has no colour literals left.

| File | Hits | Owner | Allowed | Planned action |
|---|---|---|---|---|
| `src/core/Materials.cpp` | 26 | core | 26 | document material library (body appearance data, not UI chrome): stays; review in phase 4 |
| `src/viewer/ViewerWidget.cpp` | 13 | ui | 13 | phase 4: triedron, lights (white), material, sketch-edge/feature-edge/label colours -> ViewerPalette |
| `src/part/tools/PropertyPanel.cpp` | 11 | part | 11 | inline error/checked styles -> dynamic properties (invalid, messageLevel) styled by app.qss.tmpl |
| `src/sketch/SketchSession.h` | 11 | sketch | 11 | colors:: constants -> ColorRole (sk.free, sk.constrained, sk.construction, sk.projected, ...) |
| `src/core/Manipulator.cpp` | 8 | core | 8 | manipulator colours -> ColorRole (manip.handle, axis.*) |
| `src/drawing/DrawingWorkspaceWidget.cpp` | 8 | drawing | 8 | side-panel inline label colours -> QSS/tokens (request) |
| `src/sketch/ValueInputOverlay.cpp` | 8 | sketch | 8 | inline QSS -> objectName #fcValueBox + tag property; tokens in app.qss.tmpl |
| `src/assembly/tools/JointTool.cpp` | 6 | assembly | 6 | joint preview colours -> ColorRole |
| `src/core/ViewerContext.h` | 6 | core | 6 | PreviewStyle presets join/cut/newBody/sketchLine -> core::ColorRole (RGBA kept as headless fallback) |
| `src/assembly/Triad.cpp` | 4 | assembly | 4 | triad axis colours -> ColorRole (axis.x/y/z) |
| `src/drawing/DrawingCanvas.cpp` | 4 | drawing | 4 | paper/ink fixed (allowed); canvas surround drawing.canvas, selection drawing.select (request) |
| `src/sketch/tools/SketchToolBase.cpp` | 4 | sketch | 4 | preview colours -> ColorRole |
| `src/assembly/tools/InterferenceTool.cpp` | 3 | assembly | 3 | interference highlight -> ColorRole (status.error) |
| `src/assembly/tools/ToolCommon.cpp` | 3 | assembly | 3 | inline QLineEdit error style -> property invalid="true" (app.qss.tmpl) |
| `src/core/Manipulator.h` | 2 | core | 2 | manipulator defaults -> ColorRole |
| `src/drawing/SheetRenderer.h` | 2 | drawing | 2 | paper/ink fixed (allowed) |
| `src/core/Body.h` | 1 | core | 1 | default body colour (document data, saved in .fcad): stays |
| `src/core/Materials.h` | 1 | core | 1 | material default colour (document data): stays |
| `src/drawing/DrawingExport.cpp` | 1 | drawing | 1 | export colours (allowed) |
| `src/drawing/SheetRenderer.cpp` | 1 | drawing | 1 | paper/ink fixed (allowed) |
| `src/part/tools/FeatureTool.cpp` | 1 | part | 1 | preview colour -> ColorRole Live |
| `src/part/tools/MeasureTool.cpp` | 1 | part | 1 | measure line colour -> ColorRole (Dimension) |
| `src/sketch/SketchSession.cpp` | 1 | sketch | 1 | -> ColorRole |
| `src/viewer/SceneModel.h` | 1 | ui | 1 | phase 4: default body colour -> body.default via ViewerPalette |
| `src/viewer/occt/OcctView.cpp` | 1 | ui | 1 | phase 4: OCCT helper default -> ViewerPalette (or allow-list as OcctQtTools-like helper) |

Total: 128 hits in 25 files.

Everything left is allow-listed with its owner and planned action (`tools/theme/color-allowlist.txt`):
the viewer remainder migrates in phase 4, the other modules through the section 9 requests
(`docs/integration-notes/ui.md`, 2026-10-22), and drawing paper/ink, export colours and document
material data stay.
