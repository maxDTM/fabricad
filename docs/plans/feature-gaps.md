# Plan: feature gaps vs Fusion 360, SolidWorks and Onshape

Status: research plan, no code (2026-10-23, branch `ui2/gaps`). Scope: everyday part, assembly and drawing work in a
local desktop modeller. Execution order: after `ROADMAP.md` steps 1-4 (redesign, open issues, drawing round,
tutorial) and step 5, the agentic-design plan (`docs/plans/agentic-design.md`, on branch `plan/agentic`, not yet on
main).

**Sources.** Code on main @ 673bcde: the tool registry (211 tool ids: `grep '"<area>.<id>"' src`), feature properties
in `src/part/features/*.cpp`, `src/sketch/SketchGeometry.h` (constraint types), `src/assembly/JointTypes.h` and
`JointFeature.cpp`, `src/drawing/DrawingTypes.h`; `docs/STATUS-2026-10-22.pdf` §5 "Missing features";
`docs/comparison/COMPARISON.md`; `docs/plans/backend-deferred.md`; the issue tables in `docs/tests/*/ISSUES.md` and
`docs/uiux/ISSUES.md`. Reference apps: vendor feature pages and 2026 release notes (Fusion 2026 release notes and
the Sept 2026 sheet-metal update; SOLIDWORKS 2026 "What's New": Auto-Generate Drawing, fastener recognition,
flattened BOM; Onshape Variable Studios, branching and merging, mate connectors, configurations).

**Legend.** Value: **H** (most users hit it weekly), **M** (common, a workaround exists), **L** (specialised).
Size: **S** (1-3 agent-days), **M** (1-2 weeks), **L** (several weeks). Refs: F = Fusion, SW = SolidWorks,
O = Onshape. OCCT names are the classes that would carry the geometry work (OCCT 7.9.3 open source).

## 0. What already exists (so it is not listed below)

| Area | Present today |
|---|---|
| Sketch | PlaneGCS solver; line, rectangles (2pt/3pt/centre), circles (centre/2pt/3pt), arcs (3pt/centre/tangent), ellipse + arc, polygon (3 kinds), slots, control/fit splines, point, text; trim, extend, break, offset, fillet, chamfer, mirror, move/rotate/scale, rectangular/circular pattern, project, intersect, construction; 22 constraint types incl. symmetric, midpoint, ellipse radii; driving/driven dims; value boxes |
| Part | Extrude (Distance / To Object / All, one side / two sides / symmetric, taper, start offset), Revolve, Sweep, Loft (ruled/closed), Rib, Coil/helix/spring, Hole (simple/cbore/csink, tapped, clearance fits), Thread (modelled or cosmetic), Fillet (constant/variable, tangent chain), Chamfer (3 modes), Shell, Draft, Press Pull, Offset Face, Thicken, Split Body, Split Face, Combine, Mirror, Rect/Circular pattern of features/bodies/components, Move, Scale, primitives, construction planes/axis/point, user parameters + expressions, physical materials, appearance, section analysis, measure (incl. mass), feature groups, rollback, suppress |
| Assembly | Components (nested), active component, ground, 7 joint types (= Fusion's set) with offsets, limits, lock and drive, as-built joints, rigid groups, move/triad, copy/paste, Paste New, components from bodies, linked external designs (Insert Design), interference, BOM (CSV), STEP export with structure and colours, STEP import keeping structure |
| Drawing | Sheets, first/third angle, title block, base/projected/section (full, aligned, stepped)/detail/break views, hidden/tangent toggles, linear/aligned/radius/diameter/angle + baseline/chain/ordinate dims, tolerances (ISO 286 fits), GD&T frames, datums, surface texture, centre marks/lines, hole and thread callouts, leaders, text, parts list, balloons, revision table; PDF/SVG/DXF (R12)/CSV export |
| Data | `.fcad` (JSON), STEP AP214 in/out, STL in/out (presets), `.fcs` scripting, headless `--batch`, autosave recovery |

## 1. Sketch

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| Typed coordinates and value-box entries create constraints (COMPARISON rec. 5) | F, SW, O | H | S | - | sketch layer only (`SketchSession`, PlaneGCS) |
| Point-to-curve distance, arc-length and offset dimensions (STATUS §5) | F, SW, O | H | S | - | PlaneGCS `ConstraintP2LDistance`, `ConstraintArcLength`; offset = own constraint chain |
| Auto-constrain / "Fully define sketch" | SW, O (auto), F (AutoConstrain) | M | M | typed constraints | inference rules in `SketchInference` + solver DOF query |
| Curvature (G2) and spline handle constraints (tangent weight, handle angle) | F, SW, O | M | M | - | PlaneGCS B-spline constraints; `GeomConvert` for knots |
| Conic and equation-driven curves | F (conic), SW, O | L | S | - | rational `Geom_BSplineCurve` (rho conic); `GeomAPI_Interpolate` for equations |
| DXF / SVG import into a sketch (STATUS §5) | F, SW, O | H | M | - | own reader (LINE, ARC, CIRCLE, LWPOLYLINE, SPLINE, ELLIPSE); OCCT open source has no DXF reader. SVG paths via Qt `QPainterPath` -> `GeomAPI_PointsToBSpline` |
| Image canvas (attached reference picture, calibrate) | F, SW (sketch picture), O | M | S | - | viewer: textured quad `AIS_TexturedShape` / `Graphic3d_Texture2D` on the sketch plane |
| 3D sketch / 3D curves (helix-free paths for sweeps, wires) | F, SW, O (3D fit spline) | M | M | - | `GeomAPI_Interpolate` 3D, `BRepBuilderAPI_MakePolygon`; solver outside PlaneGCS (points only) |
| Project silhouette / include body outline; Slice (F) | F, SW | M | S | - | `HLRBRep_Algo` outlines onto the sketch plane; Slice = section plane in sketch mode |
| Sketch blocks (SW) / derived sketches | SW, O | L | M | - | sketch model only |

## 2. Part modelling

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| Configurations / design tables (variants of one part) | F, SW, O | H | L | parameters, `.fcad` schema | document model (parameter sets + suppression states per configuration); no OCCT work |
| Pattern along a path, pattern by table/sketch points, fill pattern | F, SW, O | M | S | - | `GCPnts_UniformAbscissa` on `BRepAdaptor_CompCurve` + `gp_Trsf` (frame from `GeomLProp_CLProps`) |
| Thin extrude / revolve / sweep (wall from an open profile) | F, SW, O | M | S | - | `BRepOffsetAPI_MakeOffset` on the wire (2D) then prism/revol; or `BRepOffsetAPI_MakeThickSolid` |
| Up To Next / Up To Vertex / Offset From Surface extents | SW, O | M | S | - | `BRepFeat_MakePrism` (`PerformUntilEnd`, `PerformUntilShape`), `BRepIntCurveSurface_Inter` for "next" |
| Delete Face / Replace Face / Move Face (direct edit, imported parts) | F, SW, O | H | M | - | `BRepAlgoAPI_Defeaturing` (delete + heal), `BRepOffsetAPI_MakeOffsetShape`/`LocOpe` for move; replace via `BRepAlgoAPI_Splitter` + `BOPAlgo_MakerVolume` |
| Sweep with guide rail, twist and scale laws; loft with rails/centreline and end tangency | F, SW, O | M | M (sweep) / L (loft rails) | - | `BRepOffsetAPI_MakePipeShell` (`SetMode(auxiliary spine)`, `SetLaw`); loft rails via `GeomFill_NSections`/`BRepFill_Filling`, tangency via `BRepOffsetAPI_ThruSections` + `GeomPlate` |
| Fillet setback, chord length, full-round (face-to-face) and rule fillet | F, SW, O | M | M | - | `BRepFilletAPI_MakeFillet` (`SetLaw`, chord via own radius solve); full round: `ChFi3d` has none, needs own three-face blend (`GeomFill` + sewing) |
| Emboss / deboss text and sketches onto curved faces; Wrap | F, SW, O (Wrap) | M | L | sketch text | `BRepProj_Projection` / `BRepFeat_MakeDPrism`; wrap = own unrolling of cylinders/cones (`BRepAdaptor_Surface` + `BRepBuilderAPI_MakeFace` on (u,v)) |
| Boundary fill (STATUS §5) | F | L | M | - | `BOPAlgo_MakerVolume` + cell selection UI |
| Silhouette split, split by surface extension | F, SW | L | S | - | `HLRBRep` outline + `BRepAlgoAPI_Splitter` |
| Mass properties: centre of gravity, inertia tensor, principal axes (only mass/volume today) | F, SW, O | H | S | materials | `BRepGProp::VolumeProperties` -> `GProp_GProps::CentreOfMass`, `MatrixOfInertia`, `PrincipalProperties` |
| Inspection: zebra, curvature map/comb, draft analysis, min radius, thickness | F, SW, O | M | M | - | `Graphic3d_ShaderProgram` (zebra env), per-vertex colours on `MeshVS`/`AIS_ColoredShape` from `BRepLProp_SLProps`; thickness via `BRepExtrema_DistShapeShape` sampling |
| Hole: inch standards (ANSI UNC/UNF), hole from sketch points pattern, slot/tapered holes | F, SW, O | M | S | - | data tables in `Hole.cpp`/`Thread.cpp`; existing prism/revol tools |
| Derived part (insert bodies/parameters of another design, associative) | F (Derive), SW (insert part), O (derived) | M | M | linked designs | reuse `assembly.insert-design` link + `BRepBuilderAPI_Copy` of chosen bodies |
| Feature recognition / defeaturing of imported STEP (holes, fillets) | SW (FeatureWorks), F (fillet removal) | L | L | delete face | `BRepAlgoAPI_Defeaturing`, `ShapeUpgrade_UnifySameDomain`; recognition by face typing (`GeomAdaptor_Surface::GetType`) |
| Mesh-to-B-rep (edit an STL as solid) | F (Mesh), SW (ScanTo3D lite) | L | S (faceted) | - | `RWStl` + `BRepBuilderAPI_Sewing` + `ShapeUpgrade_UnifySameDomain` on planar facets |
| Global variables shared across designs (Onshape Variable Studio) | O, F (linked params) | L | S | parameters | `.fcad` link, no OCCT |

## 3. Surface modelling

Thicken and Split Face exist; there is a SURFACE tab but no surface bodies.

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| Surface bodies as a body kind (open shells in browser, timeline, STEP) | F, SW, O | M | M | - | `TopoDS_Shell`/`TopoDS_Face` bodies; `Body` type flag; viewer two-sided shading |
| Open-profile Extrude / Revolve / Sweep / Loft to surfaces | F, SW, O | M | S | surface bodies | same builders without `MakeSolid` (`BRepPrimAPI_MakePrism` on wires gives faces) |
| Patch (n-sided fill with tangency) | F, SW (Boundary/Fill), O (Fill) | M | M | surface bodies | `BRepFill_Filling` / `BRepOffsetAPI_MakeFilling` (G0/G1 constraints) |
| Offset surface, Extend, Trim / Untrim | F, SW, O | M | M | surface bodies | `BRepOffsetAPI_MakeOffsetShape`, `BRepLib::ExtendFace`/`GeomLib::ExtendSurfByLength`, `BRepAlgoAPI_Splitter` + `BRepBuilderAPI_MakeFace(surface, wire)` |
| Stitch / Unstitch / Knit to solid; Ruled surface | F, SW, O | M | S | surface bodies | `BRepBuilderAPI_Sewing` + `BRepBuilderAPI_MakeSolid`, `BRepFill::Shell` |

## 4. Sheet metal

None of it exists; all three reference apps have it and Fusion keeps adding to it (2026: join by bend, corner
closure). It is the largest single gap for a part modeller used for fabrication.

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| Sheet-metal rules (thickness, bend radius, K-factor, reliefs) per component | F, SW, O | H | S | - | document model |
| Base / contour flange from a sketch; edge flange, hem, tab | F, SW, O | H | L | rules | `BRepPrimAPI_MakePrism` + `BRepPrimAPI_MakeRevol` (bend = cylindrical shell), `BRepOffsetAPI_MakeThickSolid` |
| Bend from a sketch line, convert solid to sheet metal (Onshape model) | SW, O | M | M | flange | thickness detection with `BRepExtrema_DistShapeShape`; face pairing |
| Unfold / refold, flat pattern, bend lines and table | F, SW, O | H | L | flange | own unfolder: face adjacency tree, develop cylindrical/conical faces (`BRepAdaptor_Surface`), place planar faces with `gp_Trsf` (FreeCAD SheetMetal does this on OCCT) |
| Corner relief and corner closure | F, SW, O | M | M | flange | `BRepAlgoAPI_Cut` with relief tools |
| Flat pattern to DXF; flat view in drawings | F, SW, O | H | S | unfold, DXF R2000 | `DxfWriter` + drawing base view of the flat body |

## 5. Assembly, joints and motion

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| Motion links (gear, rack-pinion, screw, linear coupling of joint values) (STATUS §5) | F, SW (mechanical mates), O (relations) | M | S | - | `AssemblySolver`: q_b = k * q_a as an extra residual; no OCCT |
| Exploded views (steps, trails) and drawing exploded view (STATUS §5) | F (Animation), SW, O | H | M | - | per-step component transforms in the document; trails as `AIS_Line`; drawing view uses the exploded placements |
| Contact sets / collision stop while dragging | F, SW (physical dynamics), O | M | M | - | `BRepExtrema_ShapeProximity` (BVH), `BRepAlgoAPI_Common` volume check |
| Motion study (keyframed joint drive, playback, export video) | F, SW (Motion), O (animations) | M | M | motion links | timeline of joint values; `Image_VideoRecorder` (OCCT, ffmpeg) or frame dumps |
| Clearance check (minimum gap between components) | SW, F | M | S | - | `BRepExtrema_DistShapeShape` |
| Tangent / cam / path / slot-on-curve mates | SW, O | L | M | - | solver residual from `Extrema_ExtPS` / `GeomAPI_ProjectPointOnSurf` |
| Replace component; mirror component | F, SW, O | M | S | - | document ops; mirror via `BRepBuilderAPI_Transform` with `gp_Trsf::SetMirror` |
| Standard parts library (ISO/DIN screws, nuts, washers, pins) and auto-fastening (SW 2026) | F (McMaster insert), SW (Toolbox), O (Standard content) | H | M | Hole/Thread data | parametric generators built from existing Revolve/Thread/Hole features |
| Linked design update notices, version pinning, edit-in-context of a linked design | F, SW, O | M | M | linked designs | file watcher + `externalPath` stamp; cross-file references through `GeometryRefs` |
| Assembly mass properties and centre of gravity | F, SW, O | M | S | mass props | `GProp_GProps::Add` over occurrences |
| BOM options: flattened vs structured, custom columns, part number/description properties | F, SW (2026 flattened BOM), O | H | S | part properties | BOM model only |
| Large-assembly display (simplified/LOD, lightweight load) | SW (selective load 2026), O | L | M | - | coarser `BRepMesh_IncrementalMesh` per occurrence, `AIS_ConnectedInteractive` instancing |

## 6. Drawing

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| Auxiliary view (normal to an edge or face) | F (projected custom), SW, O | M | S | - | existing `HLRBRep` projection with a custom `gp_Ax2` |
| Broken-out section and crop view | SW, O, F | M | S | - | `BRepAlgoAPI_Cut` with a prism of a closed sketch, then HLR; crop = 2D clip in `Geometry2D` |
| Hole table, general tables, bend table | F, SW, O | M | S | sheet metal for bend table | `HoleAnalysis.cpp` already recognises holes |
| Model dimensions into views (retrieve sketch/feature dims), auto-dimension, auto-drawing (SW 2026) | SW, F (automated drawings), O | H | M | agentic plan for the auto variant | map sketch constraints to view coordinates through the view `gp_Trsf` |
| Arc-length, chamfer and angle-across-views dimensions; dimension breaks | F, SW, O | M | S | - | `GCPnts_AbscissaPoint::Length` |
| Sheet templates (custom borders, title block fields bound to document properties), company standards file | F, SW, O | H | S | part properties | `Sheet`/`SheetRenderer` templates as SVG + field map |
| Assembly section options (exclude components, hatch per component/material) | F, SW, O | M | S | - | `SectionCut` per occurrence |
| Drawing out-of-date indication and update control | F, SW, O | M | S | - | model change stamp per view |
| DXF R2000 (lineweights, blocks) and DWG | F, SW, O | M | M (DXF) | - | `DxfWriter` AC1015 (backend-deferred DRW-ASM-OUTPUT-9). DWG: no free writer (ODA licence): out of scope |
| Weld symbols, cosmetic weld beads | SW, F | L | S | - | `Symbols.cpp` |
| Exploded and animated views (STATUS §5) | F, SW, O | M | S | exploded views | drawing view with explode state |

## 7. Data and interop

| Gap | Refs | Value | Size | Depends on | OCCT / implementation |
|---|---|---|---|---|---|
| IGES import/export | F, SW, O | M | S | - | `IGESCAFControl_Reader` / `Writer` (TKDEIGES) |
| 3MF / OBJ / glTF export (3D printing, web viewers, renders) | F, SW, O | H (3MF) | S | - | `RWObj_CafWriter`, `RWGltf_CafWriter` (OCCT); 3MF via lib3mf (BSD) on the existing mesh |
| STEP AP242 with PMI/GD&T, layers, per-face colours, part properties | F, SW, O | M | M | part properties | `STEPCAFControl_Writer` + `XCAFDoc_DimTolTool`, `XCAFDoc_LayerTool`, `XCAFDoc_ColorTool` per face |
| STEP import healing options (sew, fix, unify faces) and a report | F, SW, O | M | S | - | `ShapeFix_Shape`, `ShapeUpgrade_UnifySameDomain`, `BRepCheck_Analyzer` |
| Part properties (part number, description, revision, vendor, cost) | F, SW, O | H | S | - | document model; feeds BOM, title block, STEP |
| Parasolid, ACIS, native SW/F3D/Inventor files | F, SW, O | M | L | - | not in open OCCT (commercial CAD Exchanger): out of scope; document "use STEP" |
| Scripting with variables, loops and functions (or a Python binding) | F (Python API), SW (VBA/C#), O (FeatureScript) | M | M | agentic `ICommandApi` | `.fcs` extensions, or embed Python over the command API |
| Local version history: named versions, restore, compare two versions | F, O | H | M | - | snapshots inside or beside `.fcad`; geometric diff via `BRepAlgoAPI_Cut` both ways |

## 8. Collaboration and PDM

Fusion and Onshape are cloud products; SolidWorks uses PDM. FabriCAD is local. Only the local equivalents are worth
planning.

| Gap | Refs | Value | Size | Depends on | Implementation |
|---|---|---|---|---|---|
| Git-friendly `.fcad` (stable key order, one feature per block, external heavy data) and a `fabricad diff` | O (branch/merge) | M | S | - | JSON writer rules; diff of feature lists |
| Branch and merge of designs | O | L | L | version history | three-way merge of the feature list; conflicts on the same feature |
| Comments and markups pinned to geometry / views | F, O | L | M | - | annotations stored in `.fcad` with `GeometryRefs` |
| Release states, revisions, where-used across files | SW PDM, O, F | L | L | part properties | project index file; out of scope until a project concept exists |
| Real-time co-editing, sharing links, cloud | F, O | - | - | - | **out of scope** |

## 9. CAM, simulation and the rest (out of scope notes)

| Area | Refs | Note |
|---|---|---|
| CAM (2.5/3/5-axis toolpaths, post-processors) | F, SW (CAM), O (CAM Studio) | Out of scope. Bridge: STEP/3MF export to FreeCAD CAM, or OpenCAMLib later. |
| FEA (static stress, modal, thermal) | F, SW Simulation, O (Simulation) | Out of scope. OCCT has no volume mesher; bridge via Gmsh/Netgen + CalculiX if ever. |
| Generative design / topology optimisation | F | Out of scope. |
| Photoreal rendering | F, SW Visualize, O Render Studio | Cheap step possible: OCCT path tracing (`Graphic3d_RenderingParams::Method = Graphic3d_RM_RAYTRACING`, PBR materials) in the existing viewer, size S. |
| Electronics / PCB, harness, routing, weldments, molds | F, SW | Out of scope (weldments could follow sheet metal, size L). |
| 3D print preparation (orientation, supports, slicing) | F | Out of scope beyond 3MF export. |

## 10. Proposed roadmap after the tutorial and the agentic plan

Ordered by value per size, then by unlocking later work. Each step is one or more worktree workflows with tests and
a Sonnet GUI round, as in `ROADMAP.md`.

| Step | Content | Size | Why here |
|---|---|---|---|
| **6** | **Everyday quick wins**: typed constraints (rec. 5), point-curve/arc-length dims, mass properties + CoG, part properties, BOM options, clearance check, replace/mirror component, pattern along path, thin extrude, auxiliary/broken-out/crop views, hole table, sheet templates, IGES, 3MF/OBJ/glTF | ≈ 6-8 weeks of S items, parallel worktrees | All S, all H/M, mostly isolated files; part properties unlock BOM, title block and STEP metadata |
| **7** | **Imported-part editing + interop**: Delete/Replace/Move Face, STEP healing report, DXF/SVG import into sketches, DXF R2000 writer | M | Most users start from STEP/DXF; direct edit is the biggest gap for imported geometry |
| **8** | **Assembly motion and presentation**: motion links, exploded views (+ drawing view), contact sets, motion study, standard parts library | M-L | Exploded views are H for drawings; motion links are S on the existing solver |
| **9** | **Sheet metal MVP**: rules, base/edge flange, hem, bend, unfold/flat pattern, flat DXF, bend table | L | Largest remaining gap; needs DXF R2000 (step 7) and tables (step 6) |
| **10** | **Configurations and version history** | L | Touches the document model and `.fcad` schema; do once, after the feature set settles |
| **11** | **Surface bodies and tools**, sweep rails/laws, loft rails, advanced fillets, inspection (zebra, curvature, draft) | L | Specialised; builds on the same body-kind work |
| **12** | Model dimensions in drawings and auto-drawing (with the agent from step 5), emboss/wrap, 3D sketch, render mode | M-L | Agent-assisted drawing is where SW 2026 and Fusion are heading |

### Top 10 gaps (value per size)

| # | Gap | Area | Value | Size |
|---|---|---|---|---|
| 1 | Sheet metal (flanges, bends, flat pattern, DXF) | sheet metal | H | L |
| 2 | Configurations / design variants | part | H | L |
| 3 | Delete / Replace / Move Face on imported parts | part | H | M |
| 4 | Exploded views (+ drawing exploded view) | assembly, drawing | H | M |
| 5 | Typed constraints + point-curve / arc-length dims | sketch | H | S |
| 6 | Mass properties (CoG, inertia) + part properties feeding BOM/title block | part, data | H | S |
| 7 | DXF/SVG import into sketches | sketch, interop | H | M |
| 8 | 3MF/OBJ/glTF + IGES export/import | interop | H/M | S |
| 9 | Standard parts library and motion links | assembly | H/M | M/S |
| 10 | Model dimensions, hole table, auxiliary/broken-out views, sheet templates | drawing | H/M | S-M |

Local version history (H, M) is the first runner-up; it moves up if the agentic plan needs a safe restore point
beyond the undo stack.
