# Round 2: sketch geometry + drawing annotations (branch `feat/sketch-drawing`)

Date: 2026-10-20. Everything below builds with `-DCMAKE_BUILD_TYPE=Dev` and the full ctest
suite passes (40 tests, offscreen). No edits outside `src/sketch/**`, `src/drawing/**`,
`docs/integration-notes/` and one appended paragraph in `THIRD_PARTY.md`. No core, app, CMake
or viewer files were touched.

## 1. What was added

### Sketch (`src/sketch`)
* New geometry types in `GeomType`: `Ellipse`, `EllipseArc`, `Spline` (control points),
  `FitSpline` (fit points, chord-length parameters frozen at creation so the curve stays linear
  in the fit points for the solver), `Text` (font outline from `QPainterPath`, cap-height
  scaled). Parameter layouts are documented in `SketchGeometry.h`.
* Solver (`SketchSolver.cpp`): ellipses / elliptical arcs / B-splines mapped to PlaneGCS
  geometry; coincident, point-on (ellipse, B-spline), tangent (line-ellipse; curve-curve incl.
  splines via PlaneGCS angle-via-point), equal radii, spline control / fit point references.
* Point references gained `PointPos::Vertex` + `GeoRef::index` (spline control / fit points).
* `SketchCurves.*` (ellipse frame, B-spline evaluation, global interpolation),
  `SketchText.*` (glyph contours, cached), `SketchTransform.*` (move / copy / rotate / scale,
  rectangular + circular pattern, sketch chamfer).
* Profiles (`SketchProfiles.cpp`): OCCT edges for ellipses (`Geom_Ellipse`), B-splines
  (`Geom_BSplineCurve`) and text (one edge per glyph segment), region detection over the new
  curve types; text is usable as an extrude profile (test `textProfilesExtrude`).
* Tools (`tools/CurveTools.cpp`, `tools/TransformTools.cpp`, registered from
  `registerSketchTools`): `sketch.ellipse`, `sketch.ellipse-arc`, `sketch.spline-fit`,
  `sketch.spline-control`, `sketch.text`, `sketch.pattern-rectangular`,
  `sketch.pattern-circular`, `sketch.move`, `sketch.rotate`, `sketch.scale`, `sketch.chamfer`.
* Tests: `test_curves.cpp`, `test_curve_tools.cpp`, `test_sketch_transform.cpp`.

### Drawing (`src/drawing`)
* **Tolerances and fits** (`Tolerance.*`): `DimTolerance` on every `Dimension` (persisted under
  `"tolerance"`), modes Symmetric / Deviation / Limits / Fit / Basic / Reference; ISO 286-1 IT1..IT16
  table and ISO 286-2 fundamental deviations (holes A-H, JS, K, M, N, P, R, S; shafts d-h, js, k,
  m, n, p, r, s; up to 500 mm), `analyzeFit("H7/g6")` (clearance / transition / interference).
  Rendered by `addToleranceText` (stacked deviations, limits, framed basic dimensions).
  `drawing.edit-dimension` accepts `tolerance= upper= lower= pm= fit= fitdisplay= tolprecision=`
  and has dialog fields for them.
* **Symbols as geometry** (`Symbols.*`): GD&T characteristic glyphs, circled modifiers, depth /
  counterbore / countersink glyphs, rich text lines mixing text runs and glyphs. Drawn as curves so
  PDF, SVG and DXF show identical symbols independent of fonts.
* **Callouts** (`Callouts.*`, `CalloutRenderer.*`, `HoleAnalysis.cpp`), stored per sheet in
  `Sheet::callouts` (`CalloutSet`):
  hole / thread callouts (B-rep hole recognition: coaxial internal cylinders / cones, bottom
  discs, drill points; Hole feature instance count + optional thread properties), feature control
  frames (composite rows), datum identifiers, surface texture (ISO 1302 positions a-e, all around,
  lay), ordinate dimension sets, revision tables (title block `Rev` auto-filled from the last row).
  Baseline / chain dimensions are builders (`makeBaselineDimensions`, `makeChainDimensions`) that
  create ordinary `Dimension`s, like Fusion.
* **Break views** (`BreakView.*`): `DrawingView::breaks / breakGap / breakStyle`; `toSheet()` /
  `fromSheet()` include the break compression (exact inverses), curves are clipped in
  `SheetRenderer::addViewCurves`, structural / curved break lines. Dimensions keep true values.
* **Stepped / aligned sections**: `SectionLine::points` (+ `aligned`), `p1/p2` stay the first
  segment, so `sectionFrame()` and the existing call sites are unchanged. `computeSection()`
  handles the polyline (prism tools in the parent frame, hatch faces per cutting segment; the
  aligned arm is revolved about the bend). Cut line drawn with thick ends and bend marks.
* **Exports**: everything goes through `SheetRenderer::addSheet` -> PDF / SVG / DXF. DXF text now
  encodes Ø / ° / ± as `%%c` / `%%d` / `%%p` (`DxfWriter::dxfEncodeText`).
* **Tools** (`tools/CalloutTools.*`, registered by one line in `registerDrawingTools`):
  `drawing.hole-callout`, `drawing.thread-callout`, `drawing.feature-control-frame`,
  `drawing.datum`, `drawing.surface-texture` (group SYMBOLS); `drawing.dimension-ordinate`,
  `drawing.dimension-baseline`, `drawing.dimension-chain` (DIMENSIONS); `drawing.revision-table`,
  `drawing.revision` (TABLES); `drawing.section-view-stepped`, `drawing.section-view-aligned`
  (CREATE); `drawing.break-view` (MODIFY); `drawing.edit-callout` (hidden, opened by
  double-click on any callout).
* Tests: `test_drawing_tolerance.cpp`, `test_drawing_callouts.cpp`, `test_drawing_sections.cpp`,
  `test_drawing_callout_tools.cpp`; shared test geometry in `tests/support/HoleTestFeatures.h`.

## 2. Edits to existing (shared) files - expect merge conflicts here

Sketch (all inside the module, but other round-2 agents may touch the same files):
* `SketchGeometry.h/.cpp` (enum `GeomType` extended, new fields on `Geometry`, `GeoRef::index`,
  `PointPos::Vertex`), `Sketch.cpp`, `SketchSolver.cpp`, `SketchProfiles.h/.cpp` (new
  `makeEdges()`), `SketchSession.cpp`, `SketchFeature.cpp` (display items: one per text contour),
  `tools/SketchModeTools.cpp` (+2 registration calls), `tools/SketchToolBase.cpp`
  (`previewGeometry` uses `polylines()`), `tools/SketchTools.h`.
* **ShapeRef encoding**: `ShapeRef::subIndex` of a sketch point is now `0..4` for the roles as
  before and `16 + index` for spline control / fit points (`kVertexSubIndexBase`). Code outside
  the sketch module that decodes `subIndex` with `fromRole()` keeps working for the old roles;
  use `fromSubIndex()` to also get spline points.

Drawing:
* `Dimension.h/.cpp` (+`tolerance` member and JSON key), `DrawingView.h/.cpp` (section path,
  breaks; `toSheet/fromSheet` now branch to `toSheetBroken/fromSheetBroken` when breaks exist),
  `Sheet.h/.cpp` (+`callouts` member; `contains`, `entitiesOf`, JSON), `SheetRenderer.cpp`
  (tolerance text, broken curves, polyline section line, callouts in `addSheet`, title block Rev),
  `DrawingModel.cpp` (`removeEntity`, `moveView`, `moveObjects`, `refreshAnnotations`,
  `makeJob` passes the section path), `DrawingCanvas.h/.cpp` (new `HitKind::Callout`: canvas
  items, hit testing, movable, double-click -> `drawing.edit-callout`), `DxfWriter.h/.cpp`,
  `SectionCut.h/.cpp`, `tools/AnnotationTools.h/.cpp` (edit-dimension tolerance settings),
  `tools/DrawingTools.cpp` (one `registerCalloutTools(tools)` line + include).
* `THIRD_PARTY.md`: one paragraph appended (standards data / textbook algorithms, no code copied).

## 3. Follow-ups / known limitations

* README "Features" lists should mention the new sketch curves and drawing annotations
  (README is integrator-owned; not edited).
* The ribbon has no icons for the new tool ids (`icon = id`); the UI falls back to text.
* Hole callouts read thread data from a part `Hole` feature only if it has properties named
  `threadDesignation` / `threadSize` / `thread` (String or Enum), `threaded` (Bool),
  `threadClass`, `threadDepth` (Length). The current part `Hole` feature has no thread options;
  if the part agent adds them under other names, extend `analyzeHole()` in
  `src/drawing/HoleAnalysis.cpp`. Without them, `drawing.thread-callout` derives the metric
  coarse designation from the drill size (tap drill or nominal diameter).
* Stepped sections still show the edges of the step faces (ISO omits them); aligned sections
  support exactly one bend (three points). Arbitrary multi-bend aligned cuts are not supported.
* Break views: picking geometry inside a removed band is still possible through
  `DrawingView::pickCurve` (the band is invisible, so users rarely click there).
* GD&T: no all-around / between symbols, no projected-zone height value, no datum targets.
* Ordinate dimensions do not auto-stagger overlapping labels (manual drag works).
* Revision rows other than the last one can only be edited by re-creating them (edit tool works
  on the last row).
