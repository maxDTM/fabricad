# FabriCAD research: reusing FreeCAD and OpenCASCADE

Date: 2026-10-19. This document is for the people building FabriCAD. All paths below are under
`../fabricad-ref` (written `REF/` below). That material sits outside the repo
and is untrusted reference code: read it, do not run its build scripts.

| Source | Path | Revision | License |
|---|---|---|---|
| FreeCAD (sparse: planegcs, Part/App, PartDesign/App, TechDraw/App, Assembly, App/ElementMap*, MappedName*, IndexedName*) | `REF/FreeCAD` | `703986732cf5` (main, 2026-10-19) | LGPL-2.1-or-later |
| OndselSolver (assembly solver used by FreeCAD) | `REF/OndselSolver` | `4be80eef02a3` | LGPL-2.1 |
| OCCT samples/qt (sparse, tag V7_9_3) | `REF/occt-samples/occt/samples/qt` | `a016080bf673` | LGPL-2.1 + OCCT exception |
| gkv311 OCCT+Qt QOpenGLWidget sample (OcctQOpenGLWidgetViewer, AIS_ViewCube) | `REF/occt-samples/qopenglwidget` | `72e32d3a8398` | MIT |

Note: `OcctQtViewer` / `OcctQOpenGLWidgetViewer` is not part of OCCT's own `samples/qt` (that folder has
the older `Common/View.cxx`, which uses a native `QWidget` and `WA_PaintOnScreen`). The current reference
for QOpenGLWidget embedding is Kirill Gavrilov's repo (he is an OCCT developer), cloned above. It is MIT,
so it can be copied into an LGPL project as long as its copyright header is kept and it is listed in THIRD_PARTY.md.

## Machine facts (verified on this box)

- OCCT 7.9.3 from the Arch package is built with **X11/GLX** (`libTKOpenGl.so` links `libX11` and `libGLX`;
  it has no EGL or Wayland support, and `OpenCASCADE_WITH_GLES2 OFF`). This means **the viewer must run Qt on
  `xcb` (XWayland)**. Qt has both `libqwayland.so` and `libqxcb.so` (with the glx integration). `DISPLAY=:0` is
  set and XWayland exists.
- GL: Mesa Intel UHD 620, OpenGL 4.6 core/compat over GLX/XWayland. That is enough for AIS shaded and
  wireframe display with MSAA 4. Turn MSAA down to 0 to 2 if it is slow.
- CPU i5-8350U (4C/8T), 7 GB RAM (about 4 GB free while idle), 100 GB disk free.
- Boost is **not installed**, which matters for PlaneGCS (see section 1).

---

## 1. PlaneGCS: vendoring with Eigen only (verified: it compiles, links and solves)

Files are in `REF/FreeCAD/src/Mod/Sketcher/App/planegcs/` (13.3 kLOC, all LGPL-2.1+, author K. Poulios and others):

```
GCS.cpp GCS.h            System: constraint list, partitioning, DogLeg/LM/BFGS, diagnose (QR)
Constraints.cpp/.h       about 60 constraint classes (error and gradient)
Geo.cpp/.h               Point, Line, Circle, Arc, Ellipse, ArcOfEllipse, Hyperbola, Parabola, BSpline, DeriVector2
SubSystem.cpp/.h         per-component subsystem
qp_eq.cpp/.h             quadratic program helper for DogLeg
Util.h                   VEC_pD/MAP_pD_* typedefs
```

These are the only external dependencies:

| Include | Where | What to do |
|---|---|---|
| `../../SketcherGlobal.h` | GCS.h, Geo.h, Constraints.h | stub: `#define SketcherExport` |
| `<FCConfig.h>`, `<Base/Tools.h>` | GCS.cpp | empty stubs (nothing from them is used) |
| `<Base/Console.h>` | GCS.cpp: `Base::Console().log/warning(fmt, ...)`, std::format style | small shim (below) |
| `Base::TimeElapsed` | only under `#ifdef PROFILE_DIAGNOSE` | none |
| `<boost/math/constants/constants.hpp>` | Geo.h, not actually used | shim that includes `<cmath>` and `<cassert>`. Sources rely on it, through FreeCAD's precompiled header, for `sqrt`/`std::sin` and `assert` |
| `<boost/graph/graph_concepts.hpp>` | Constraints.cpp, unused | same shim |
| `<boost/graph/connected_components.hpp>`, `<boost_graph_adjacency_list.hpp>` | GCS.cpp ~L463 and L1786-1805: `adjacency_list<vecS,vecS,undirectedS>`, `add_vertex`, `add_edge`, `num_vertices`, `connected_components` | 25-line DFS shim (below), or a union-find edit in the source |
| `std::unreachable` | GCS.cpp L1959 | needs **`-std=c++23`** for GCS.cpp (or define a shim). The rest builds as C++20 |
| SubSystem.cpp `assert` | | compile with `-include cassert` or add `#include <cassert>` |
| `std::async` | GCS.cpp diagnose: runs two QR decompositions in parallel | pure std, needs `Threads::Threads` |

Eigen 5.0.1 works. Its `Eigen/Version` reports `3.5.0` for compatibility, so the `EIGEN_VERSION` guards in
GCS.h (`>= 30202` enables SparseQR) still pass. Add `-DEIGEN_NO_DEBUG` as FreeCAD does.

**Verified build** (shims and the smoke test are in `REF/planegcs-compat/`, ready to copy):
`g++ -std=c++23 -O2 -DEIGEN_NO_DEBUG -include cassert -Ishim -I/usr/include/eigen3 -c GCS.cpp` took 21 s
single-threaded. The other files take 1 to 3 s each. A smoke test (fix p1 at the origin, horizontal line, distance 10) returned
`status=0 p2=(10,0) dofs=0`.

Recommended layout: `third_party/planegcs/` holds the unmodified sources (license headers kept), plus
`third_party/planegcs/compat/` holding these shims. Add a line in THIRD_PARTY.md: "FreeCAD src/Mod/Sketcher/App/planegcs @703986732cf5, LGPL-2.1+".

```cpp
// compat/Base/Console.h
#pragma once
#include <format>
#include <iostream>
namespace Base {
struct ConsoleShim {
  template<class... A> void log(std::format_string<A...> f, A&&... a){ std::clog << std::format(f, std::forward<A>(a)...); }
  template<class... A> void warning(std::format_string<A...> f, A&&... a){ std::cerr << std::format(f, std::forward<A>(a)...); }
  template<class... A> void error(std::format_string<A...> f, A&&... a){ std::cerr << std::format(f, std::forward<A>(a)...); }
};
inline ConsoleShim& Console(){ static ConsoleShim c; return c; }
}
// compat/boost_graph_adjacency_list.hpp  (boost/graph/connected_components.hpp = empty #pragma once)
#pragma once
#include <vector>
namespace boost {
struct vecS {}; struct undirectedS {};
template<class, class, class> struct adjacency_list { std::vector<std::vector<int>> adj; };
template<class G> inline int add_vertex(G& g){ g.adj.emplace_back(); return int(g.adj.size())-1; }
template<class G> inline void add_edge(int a, int b, G& g){ g.adj[a].push_back(b); g.adj[b].push_back(a); }
template<class G> inline std::size_t num_vertices(const G& g){ return g.adj.size(); }
template<class G> inline int connected_components(const G& g, int* comp){
  const int n = int(g.adj.size()); std::vector<int> st; int c = 0;
  for (int i=0;i<n;++i) comp[i] = -1;
  for (int i=0;i<n;++i){ if (comp[i]>=0) continue; comp[i]=c; st.push_back(i);
    while(!st.empty()){ int v=st.back(); st.pop_back(); for(int w: g.adj[v]) if(comp[w]<0){comp[w]=c; st.push_back(w);} }
    ++c; }
  return c; }
}
// compat/SketcherGlobal.h  -> must resolve as "../../SketcherGlobal.h" relative to planegcs/,
// i.e. place planegcs at third_party/planegcs/src/Mod/Sketcher/App/planegcs or patch the 3 includes.
#pragma once
#define SketcherExport
// compat/FCConfig.h, compat/Base/Tools.h: `#pragma once` only
// compat/boost/math/constants/constants.hpp and compat/boost/graph/graph_concepts.hpp:
#pragma once
#include <cmath>
#include <cassert>
```

```cmake
add_library(planegcs STATIC GCS.cpp Constraints.cpp Geo.cpp SubSystem.cpp qp_eq.cpp)
target_include_directories(planegcs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE compat)
target_link_libraries(planegcs PUBLIC Eigen3::Eigen Threads::Threads)
target_compile_definitions(planegcs PRIVATE EIGEN_NO_DEBUG)
target_compile_features(planegcs PUBLIC cxx_std_23)  # std::unreachable in GCS.cpp
target_compile_options(planegcs PRIVATE -include cassert -w)
```

Usage pattern. The solver works on raw `double*` that your sketch owns. Keep one parameter array per sketch
and rebuild the `GCS::System` whenever topology changes (cheap):

```cpp
GCS::System sys;
GCS::Point p1{&x1,&y1}, p2{&x2,&y2};  GCS::Line l; l.p1=p1; l.p2=p2;
sys.addConstraintCoordinateX(p1, &fx, /*tag=*/1);       // tags = your UI constraint ids
sys.addConstraintHorizontal(l, 3);
sys.addConstraintP2PDistance(p1, p2, &dist, 4);         // driving=false -> reference (driven) dim
GCS::VEC_pD unknowns{&x1,&y1,&x2,&y2};
sys.declareUnknowns(unknowns);
sys.initSolution(GCS::DogLeg);
if (sys.solve(GCS::DogLeg) == GCS::Success) sys.applySolution(); else sys.undoSolution();
int dofs = sys.diagnose();            // -> DOF count for "fully constrained" coloring
GCS::VEC_I conflicting, redundant; sys.getConflicting(conflicting); sys.getRedundant(redundant);
```

Dragging: add temporary constraints with tag `GCS::DefaultTemporaryConstraint` (-1) that pull the point
toward the mouse, solve, then `clearByTag(-1)`. This is how FreeCAD's `Sketcher/App/Sketch.cpp` does it (not cloned; the
algorithm is all in GCS). Arcs need `addConstraintArcRules(arc)` (start/end points tied to angles). Circles
use `Circle{center, &rad}`. Arc fields are `start, end, center, startAngle, endAngle, rad`.

---

## 2. PartDesign features mapped to OCCT calls

FreeCAD wraps everything in `Part::TopoShape::makeElement*()` in
`REF/FreeCAD/src/Mod/Part/App/TopoShapeExpansion.cpp` (6.4 kLOC). That file is where each raw OCCT call sits
next to its history (naming) mapping. The PartDesign feature files hold the parameter logic.

| Feature | FreeCAD file | OCCT API (what FabriCAD should call) |
|---|---|---|
| Sketch to face | `Part/App/FaceMakerBullseye.cpp` (nested wires become a face with holes), `FaceMakerCheese.cpp` | `BRepBuilderAPI_MakeWire` (edges from `GC_MakeSegment`/`GC_MakeArcOfCircle` and `BRepBuilderAPI_MakeEdge`), `BRepBuilderAPI_MakeFace(gp_Pln, outerWire)` then `.Add(innerWire.Reversed())`. Sort wires by `BRepBndLib` box or area. Alternative: `BOPAlgo_Tools::WiresToFaces` |
| Pad (blind/symmetric/two-side) | `PartDesign/App/FeatureExtrude.cpp`, `FeaturePad.cpp`, `TopoShapeExpansion.cpp:4644` | `BRepPrimAPI_MakePrism(face, gp_Vec(dir*L))`. For symmetric, translate the face by -L/2 first. Then `BRepAlgoAPI_Fuse(base, prism)` |
| Pad up-to-face/up-to-next | `TopoShapeExpansion.cpp:4656 makeElementPrismUntil` | `BRepFeat_MakePrism::Init(base, profileFace, supportFace, dir, fuse=1/cut=0, modify)` then `Perform(untilFace)` / `PerformUntilEnd()` / `PerformThruAll()`. Set `BRep_Builder().NaturalRestriction(face,true)` on the until-face (see the comment at L4684) |
| Taper (draft) pad | `Part/App/ExtrusionHelper.cpp` (`createTaperedPrismOffset`, L486) | offset the wire with `BRepOffsetAPI_MakeOffset` (FreeCAD uses its `BRepOffsetAPI_MakeOffsetFix` workaround for placement bugs), then `BRepOffsetAPI_ThruSections(true,true)` between the original and offset wire |
| Pocket | `FeaturePocket.cpp` | same prism with the direction reversed, then `BRepAlgoAPI_Cut(base, prism)`. Through-all = prism length set to the bbox diagonal |
| Revolve/Groove | `FeatureRevolution.cpp`, `FeatureGroove.cpp`, `TopoShapeExpansion.cpp:4854` | `BRepPrimAPI_MakeRevol(face, gp_Ax1, angleRad)` then Fuse/Cut. Up-to-face uses `BRepFeat_MakeRevol` |
| Fillet | `FeatureFillet.cpp`, `TopoShapeExpansion.cpp:4148` | `BRepFilletAPI_MakeFillet mk(shape); mk.Add(r, edge); mk.Build()`. FreeCAD then runs `ShapeFix_ShapeTolerance` and `ShapeFix_Shape` |
| Chamfer | `FeatureChamfer.cpp`, `TopoShapeExpansion.cpp:4180` | `BRepFilletAPI_MakeChamfer mk(shape); mk.Add(d1,d2,edge,face)` / `mk.AddDA(d, angleRad, edge, face)`. Get the reference face from `TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, map)` |
| Hole (simple/counterbore/countersink) | `FeatureHole.cpp` L1772 execute, L1990-2040 profile | build a half cross-section wire in the XZ plane (`BRepBuilderAPI_MakeEdge` segments, drill-point cone), `BRepBuilderAPI_MakeFace(wire)`, `BRepPrimAPI_MakeRevol(face, gp_Ax1(top, zDir), 2π)` = proto-hole. For each sketch point, `BRepBuilderAPI_Transform` (translate), compound them, one `BRepAlgoAPI_Cut`. Modeled threads: `BRepOffsetAPI_MakePipeShell` along a helix (`makeThread` L2325), skip for MVP (cosmetic thread only) |
| Linear/Polar pattern, Mirror | `FeatureTransformed.cpp`, `Part/App/LinearPatternExtension.cpp`, `PolarPatternExtension.cpp:127`, `FeatureMirrored.cpp:202` | compute `std::list<gp_Trsf>` (`SetTranslation`, `SetRotation(gp_Ax1, a*i)`, `SetMirror(gp_Ax2)`). Take the **feature's tool solid** (the prism or revol before the boolean, or delta=after−before) and apply `BRepBuilderAPI_Transform(tool, trsf, copy=true)`. Fuse or Cut all copies against the support in one boolean (`BRepAlgoAPI_BuilderAlgo` / Fuse with a `TopTools_ListOfShape` of tools). FreeCAD rejects copies that don't touch the support (`getRemainingSolids`) |
| Shell (Thickness) | `FeatureThickness.cpp`, `TopoShapeExpansion.cpp:3073` | `BRepOffsetAPI_MakeThickSolid mk; mk.MakeThickSolidByJoin(solid, facesToRemove, -t, 1e-7, BRepOffset_Skin, false, false, GeomAbs_Intersection /*or GeomAbs_Arc*/); mk.Build();` |
| Loft | `FeatureLoft.cpp`, `TopoShapeExpansion.cpp:4518` | `BRepOffsetAPI_ThruSections mk(isSolid=true, ruled=false); mk.AddWire(w)...; mk.CheckCompatibility(false); mk.Build();` (vertex sections via `AddVertex`) |
| Sweep (Pipe) | `FeaturePipe.cpp`, `TopoShapeExpansion.cpp:2439` | `BRepOffsetAPI_MakePipeShell mk(spineWire); mk.SetMode(isFrenet)` (or `SetMode(gp_Dir binormal)` / auxiliary spine), `mk.Add(profileWire, withContact=false, withCorrection=false); mk.SetTransitionMode(BRepBuilderAPI_Transformed /*RightCorner*/); mk.Build(); mk.MakeSolid();` |
| Draft | `FeatureDraft.cpp`, `TopoShapeExpansion.cpp:4925` | `BRepOffsetAPI_DraftAngle mk(shape); mk.Add(face, pullDir, angleRad, neutralPlane); mk.Build()` |
| Boolean / combine bodies | `FeatureBoolean.cpp`, `Part/App/FCBRepAlgoAPI_*.cpp` | `BRepAlgoAPI_Fuse/Cut/Common`. FreeCAD's FC wrappers add `SetRunParallel(true)` and fuzzy value; use `SetFuzzyValue(1e-7)` and `SetNonDestructive(true)` |
| Refine | `PartDesign/App/FeatureRefine.cpp`, `Part/App/modelRefine.cpp` | use `ShapeUpgrade_UnifySameDomain u(shape, true, true, false); u.Build();` after each boolean. This merges coplanar faces and is **important for clean face picking** |
| Datum plane/axis/point | `PartDesign/App/DatumPlane.cpp`, `Part/App/Attacher.cpp` (3.2 kLOC; modes `mmFlatFace`, `mmThreePointsPlane`, `mmNormalToPath`, `mmTangentPlane`, `mmObjectXY`...) | pure math to `gp_Ax3` / `gp_Pln`. Offset plane = `gp_Pln(face plane).Translated(n*d)`. Angle plane = rotate about an edge `gp_Ax1`. Midplane of two faces. Display with `AIS_Plane(new Geom_Plane(pln))` or `AIS_Shape(BRepBuilderAPI_MakeFace(pln,-s,s,-s,s))` |
| Validity check | everywhere | `BRepCheck_Analyzer(shape).IsValid()` after each feature. On failure, mark the feature red and keep the previous shape |

Mass properties: `BRepGProp::VolumeProperties(shape, GProp_GProps&)`. Bounding box: `BRepBndLib::AddOptimal`.
STEP: `STEPControl_Writer w; w.Transfer(shape, STEPControl_AsIs); w.Write(path)` (use `STEPCAFControl_*` with
XCAF for assemblies with names and colors). STL: `BRepMesh_IncrementalMesh(shape, 0.1, false, 0.5, true); StlAPI_Writer().Write(shape, path)`.

---

## 3. Topological naming (TNP): what FreeCAD does, and an MVP plan

FreeCAD 1.0 approach (Realthunder's port): every `TopoShape` carries an `ElementMap`
(`REF/FreeCAD/src/App/ElementMap.cpp` 1.5 kLOC, `MappedName.h`, `IndexedName.h`). Each OCCT operation runs through
`TopoShape::makeShapeWithElementMap` (`TopoShapeExpansion.cpp:1420`), using a `Mapper` that wraps the
OCCT maker's history: `BRepBuilderAPI_MakeShape::Modified(s)`, `Generated(s)`, `IsDeleted(s)`
(`MapperMaker` L5766-5790, `MapperHistory` over `BRepTools_History` L5795). Names look like
`;#7:1;:G;XTR;:H1:7,F`: an op code, source element, Generated/Modified and the tag. Feature-specific mappers include
`MapperPrism` (L3682) and `MapperThruSections` (L3639). This is the full solution. It is heavy (thousands of LOC, tangled
with App::Document, StringHasher) and **not worth vendoring for an MVP**.

**Recommended MVP plan for FabriCAD (two layers):**

1. **Stable names from history, per feature.** Each feature produces a `std::map<std::string, TopoDS_Shape>`
   of named faces and edges, built from the OCCT maker it used:
   - `BRepPrimAPI_MakePrism`: `FirstShape()` gives the `Pad1/Start` face, `LastShape()` gives `Pad1/End`, and
     `Generated(sketchEdge)` gives `Pad1/Side/<sketchGeoId>`. Sketch geometry ids are stable because the sketch owns them.
   - `BRepPrimAPI_MakeRevol`: same calls (`FirstShape/LastShape/Generated`).
   - `BRepFilletAPI_MakeFillet`: `Generated(edge)` gives the fillet faces `Fillet1/<inputEdgeName>`.
   - Booleans (`BRepAlgoAPI_BooleanOperation`): carry the names of the previous feature through
     `Modified(face)` (1→N becomes `name#k`, with k ordered by a deterministic key, see below) and drop `IsDeleted`.
     `ShapeUpgrade_UnifySameDomain::History()` returns a `BRepTools_History` for the refine step.
   - Edges are named by their two adjacent named faces: `{FaceA|FaceB}` (plus an index if there are several).
     This is FreeCAD's fallback trick, and it survives most edits.
2. **Geometric signature re-match as fallback.** Store, alongside each reference (sketch-on-face,
   fillet edges, dimension refs), a signature taken when the reference was made:
   - Face: surface type (`BRepAdaptor_Surface::GetType()`), area (`BRepGProp::SurfaceProperties`), centroid,
     normal at the centroid (planes) or axis+radius (`gp_Cylinder`), bbox.
   - Edge: curve type (`BRepAdaptor_Curve`), length (`GCPnts_AbscissaPoint::Length`), midpoint,
     tangent/direction, radius for circles, endpoints.
   On recompute, when the history name no longer resolves, score all candidates of the same type: weighted
   distance of centroid/midpoint normalized by model size, plus area/length ratio, plus a normal dot penalty. Accept
   the best match if its score is under a threshold *and* clearly better than the second best. Otherwise flag the
   feature "reference lost" (yellow) so the user can repick. That is the Fusion/SolidWorks behavior.
   TechDraw already has a matcher you can crib from: `REF/FreeCAD/src/Mod/TechDraw/App/GeometryMatcher.cpp` and
   `DimensionAutoCorrect.cpp`. They re-find dimension references after model changes by comparing geometry.
3. Deterministic ordering, needed for `#k` suffixes and when there is no history: sort by (centroid projected on a fixed axis,
   area), never by `TopExp_Explorer` order alone.

Keep a feature-timeline model (Fusion-like) where each feature holds `{inputs by name, params, output
shape, nameMap}`. A rebuild replays from the first dirty feature.

---

## 4. TechDraw: HLR views, sections, dimensions

Core: `REF/FreeCAD/src/Mod/TechDraw/App/GeometryObject.cpp:150 projectShape`:

```cpp
Handle(HLRBRep_Algo) hlr = new HLRBRep_Algo();
hlr->Add(shape, /*nbIso=*/0);
hlr->Projector(HLRAlgo_Projector(viewAxis));     // gp_Ax2(origin, viewDir, xDir); perspective: (ax2, focal)
hlr->Update(); hlr->Hide();
HLRBRep_HLRToShape out(hlr);
TopoDS_Shape visSharp   = out.VCompound();          // visible hard edges
TopoDS_Shape visSmooth  = out.Rg1LineVCompound();   // tangent (smooth) edges - often hidden by default in drawings
TopoDS_Shape visSeam    = out.RgNLineVCompound();
TopoDS_Shape visOutline = out.OutLineVCompound();   // silhouettes of curved faces (cylinders!)
TopoDS_Shape hidSharp   = out.HCompound();          // hidden lines (dashed), plus OutLineHCompound()
BRepLib::BuildCurves3d(visSharp);                   // FreeCAD does this for every compound
```

- The output edges lie in the **projector's 2D frame (Z=0)**, already in view coordinates. Convert them to
  drawing primitives with `BRepAdaptor_Curve` (Line, Circle, Ellipse, BSpline; discretize anything else with
  `GCPnts_TangentialDeflection`). See `TechDraw/App/Geometry.cpp` (`BaseGeom::baseFactory`).
- Exact HLR is slow on big models. `HLRBRep_PolyAlgo` + `HLRBRep_PolyHLRToShape` (GeometryObject.cpp:310) is
  the fast mesh-based alternative (it needs `BRepMesh_IncrementalMesh` first). Run HLR in a worker thread
  (`QtConcurrent`); FreeCAD runs it async too.
- Before projecting, FreeCAD centers, scales and rotates the shape (`DrawViewPart.cpp:325-340`,
  `ShapeUtils::centerShape/scaleShape/rotateShape`). View CS: `DrawViewPart::getProjectionCS` L1267, which is
  `gp_Ax2(origin, gp_Dir(direction), gp_Dir(xDirection))`.
- Standard views/projection groups (first and third angle): `DrawProjGroup.cpp`. Front = dir (0,-1,0), xDir (1,0,0); Top =
  (0,0,1); Right = (1,0,0) and so on. Third-angle (ANSI) puts Top above Front. First-angle (ISO) puts Top below.
- **Section view** (`DrawViewSection.cpp:504-626`): the cutting tool is
  `BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(sectionPln, -S,S,-S,S), S*normal, false, true)` (a half-space
  box). For each solid, `BRepAlgoAPI_Cut(solid, tool)`, then run HLR on the cut pieces looking along the normal.
  Hatched faces are the cut-piece faces lying on the section plane (`findSectionPlaneIntersections` L768:
  planar face, coplanar to the section plane). Project them and fill with a hatch pattern. Detail view:
  `DrawViewDetail.cpp` (common with a cylinder, then scale).
- **Dimensions** (`DrawViewDimension.cpp`): references are 2D (projected edge/vertex in the view) or 3D.
  `getDimValue` L659 → projected value from 2D geometry (`getPointsOneEdge` L826, `getPointsTwoEdges` L866,
  `getPointsTwoVerts` L985), or "True" value via 3D `Measure`. Types: Distance, DistanceX/Y, Radius,
  Diameter, Angle, Angle3Pt. The dimension stores the view-local geometry index plus a signature, and
  `DimensionAutoCorrect.cpp` re-matches it after model change (see section 3).
- Output: render the drawing in a `QGraphicsScene` (our own, as FreeCAD's TechDraw Gui does) and export with
  `QSvgGenerator` / `QPdfWriter` (QtSvg is installed). DXF can be written by hand, as polylines/arcs.
  No GL is needed, so drawings are fully testable headless.

---

## 5. Assembly joint solving

FreeCAD Assembly (`REF/FreeCAD/src/Mod/Assembly/App/AssemblyObject.cpp`): `solve()` L313 builds a fresh
`MbD::ASMTAssembly` each time. For each part it makes an `ASMTPart` with its placement. For each joint it makes two
`ASMTMarker`s (local frames on each part, from the picked face/edge/vertex, which are `Placement1/2` in `JointObject.py`).
It creates the joint (`makeMbdJointOfType` L1814: `ASMTFixedJoint`, `ASMTRevoluteJoint`,
`ASMTCylindricalJoint`, `ASMTTranslationalJoint`, `ASMTSphericalJoint`, `ASMTPlanarJoint`, distance variants
L1894, limits), grounds the fixed parts, calls `mbdAssembly->runPreDrag()` and reads back the placements. Dragging uses
`runPreDrag()` + `runDragStep(parts)` (L628-691). Joint types: Fixed, Revolute, Cylindrical, Slider, Ball,
Distance, Parallel, Perpendicular, Angle, RackPinion, Screw, Gears, Belt.

OndselSolver (`REF/OndselSolver`, LGPL-2.1): 317 .cpp, 42 kLOC, **std-only C++ with no external deps**. It is
vendorable as a CMake subproject but heavy (expect roughly 10 min at -j2) and opaque. Treat it as the phase-2 option.

**MVP approach (Fusion 360 style "joints" with a tree):**

- Each joint = `{partA, frameA (gp_Ax3 in A's local coords), partB, frameB, type, dofValues, limits}`.
  The frame comes from the pick: planar face → origin = picked point projected or face centroid, Z = normal.
  Cylindrical face or circular edge → origin = axis point/circle center, Z = axis. Vertex → point.
  These are Fusion's "joint origins".
- Joint motion transform `M(type, q)`: Rigid = I; Revolute = RotZ(q0); Slider = TransZ(q0);
  Cylindrical = TransZ(q0)·RotZ(q1); Pin-slot; Planar = Trans(q0,q1,0)·RotZ(q2); Ball = RotZYX(q).
- If joints form a **tree** rooted at grounded components (the usual case), place by direct composition with no
  iterative solve: `P_B = P_A · frameA · M(q) · Flip · frameB⁻¹`. `Flip` = RotX(π), so the Z axes oppose and the faces mate.
  Offer a "flip" toggle like Fusion. Walk the graph breadth-first from ground.
- For **loops** or mate-style constraints (SolidWorks-style coincident/concentric/distance/angle), use a generic
  least-squares solve with Eigen: unknowns are 6 per free part (translation + rotation vector, linearized
  incrementally around the current placement). Residuals per mate:
  coincident point-plane `n·(p−o)`, parallel `n1×n2` (2 independent components), concentric = axis-point distance +
  direction cross, distance, angle. Solve with `Eigen::LevenbergMarquardt` or a hand-written Gauss-Newton with
  `Eigen::ColPivHouseholderQR` (rank gives DOF/over-constrained diagnosis). Start from the current placements so the
  result is continuous while dragging. For dragging, add a soft residual pulling the dragged point to the cursor ray.
- Assembly structure: components reference part documents (or bodies) with a `gp_Trsf` placement. Display:
  `AIS_Shape` per component with `SetLocalTransformation`, or `AIS_ConnectedInteractive` for instancing.
  Export with XCAF (`XCAFDoc_ShapeTool::AddShape`/`AddComponent`, `STEPCAFControl_Writer`).

---

## 6. Qt6 + OCCT viewer embedding (copy from `REF/occt-samples/qopenglwidget`)

Files to copy (MIT; keep the headers):
- `occt-qopenglwidget/OcctQOpenGLWidgetViewer.{h,cpp}`: a `QOpenGLWidget` + `AIS_ViewController` viewer with `AIS_ViewCube`
- `occt-qt-tools/OcctGlTools.{h,cpp}`: wraps Qt's GL context and FBO for OCCT (`InitializeGlWindow`, `InitializeGlFbo`,
  `ResetGlStateBeforeOcct/AfterOcct`, the `OcctQtFrameBuffer` sRGB fix)
- `occt-qt-tools/OcctQtTools.{h,cpp}`: `qtGlPlatformSetup()`, `qtGlSurfaceFormat()`, Qt to `Aspect_VKey`
  mouse/key/wheel/touch translation (`qtHandleMouseEvent`, `qtHandleWheelEvent`, `qtKey2VKey`)
- `occt-qopenglwidget/main.cpp`, `OcctQMainWindowSample.cpp`: wiring examples. Their `CMakeLists.txt` shows the link list.

Key code (OcctQOpenGLWidgetViewer.cpp):

```cpp
// ctor
Handle(Aspect_DisplayConnection) disp = new Xw_DisplayConnection();  // X11 build of OCCT (ours)
Handle(OpenGl_GraphicDriver) drv = new OpenGl_GraphicDriver(disp, false);
drv->ChangeOptions().buffersNoSwap      = true;   // Qt swaps
drv->ChangeOptions().buffersOpaqueAlpha = true;
drv->ChangeOptions().useSystemBuffer    = false;  // render into Qt's FBO
myViewer = new V3d_Viewer(drv); myViewer->SetDefaultLights(); myViewer->SetLightOn();
myContext = new AIS_InteractiveContext(myViewer);
myViewCube = new AIS_ViewCube(); myViewCube->SetViewAnimation(myViewAnimation);
myViewCube->SetFixedAnimationLoop(false); myViewCube->SetAutoStartAnimation(true);
myView = myViewer->CreateView(); myView->SetImmediateUpdate(false);
setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

// initializeGL(): wrap Qt's context in an Aspect_NeutralWindow subclass (virtual)
OcctGlTools::InitializeGlWindow(myView, (Aspect_Drawable)effectiveWinId(), size, devicePixelRatioF());
makeCurrent(); myContext->Display(myViewCube, 0, 0, false);

// paintGL()
OcctGlTools::InitializeGlFbo(myView);           // wrap QOpenGLWidget's FBO as OCCT default FBO (each frame)
OcctGlTools::ResetGlStateBeforeOcct(myView);
myView->InvalidateImmediate();
AIS_ViewController::FlushViewEvents(myContext, myView, true);
OcctGlTools::ResetGlStateAfterOcct(myView);

// input: OcctQtTools::qtHandleMouseEvent(*this, myView, ev) -> update()
// handleViewRedraw override: if (myToAskNextFrame) update();   // keeps ViewCube animations running
// dtor: hold the display connection, RemoveAll, myView->Remove(), then makeCurrent() before releasing it
```

`main()` must call this **before** `QApplication` is constructed:
```cpp
OcctQtTools::qtGlPlatformSetup();   // sets QT_QPA_PLATFORM=xcb if unset (non-Wayland OCCT build),
                                    // AA_ShareOpenGLContexts, QSurfaceFormat::setDefaultFormat(qtGlSurfaceFormat())
```

**Wayland:** our OCCT has no Wayland/EGL build (no `HAVE_WAYLAND`), so running Qt on native `wayland` would give
OCCT a GLX-less EGL context, and `OpenGl_Context::Init` fails. Force xcb (XWayland). Hard-code
`qputenv("QT_QPA_PLATFORM","xcb")` if the variable is empty, or call `qtGlPlatformSetup()`. If a user sets `wayland` explicitly,
warn and fall back. With an EGL-enabled OCCT build, define `HAVE_WAYLAND` and the sample's EGL branch in
`OcctGlTools::InitializeGlWindow` handles it (`OpenGl_GraphicDriver::InitEglContext`).

**Headless/tests:** `QT_QPA_PLATFORM=offscreen` cannot host this viewer (no GLX). Keep the viewer a thin layer.
Unit-test the document, features, solver, naming, HLR and drawing scene with no GL at all. If an offscreen render is
ever needed, use `Xvfb` (not installed) or an EGL/GLES OCCT build. Do not depend on it.

**Fusion/SolidWorks navigation** via `AIS_ViewController::ChangeMouseGestureMap()`:
```cpp
auto& g = ChangeMouseGestureMap(); g.Clear();
// Fusion 360: MMB drag = pan, Shift+MMB = orbit, wheel = zoom at cursor
g.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
g.Bind(Aspect_VKeyMouse_MiddleButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_RotateOrbit);
// SolidWorks preset: MMB = orbit, Ctrl+MMB = pan, Shift+MMB = zoom
// LMB stays free for selection: AIS_MouseGesture_SelectRectangle on LMB drag
SetRotationMode(AIS_RotationMode_PickCenter); // orbit about the point under the cursor, like Fusion
```
Selection of faces/edges/vertices: `myContext->Activate(aisShape, AIS_Shape::SelectionMode(TopAbs_FACE))`
(and EDGE/VERTEX), `myContext->MoveTo(x,y,view,true)` for hover highlight (done by the ViewController), then
`myContext->InitSelected(); ...SelectedShape()`. Sketch mode: `myView->SetProj(normal)` + `V3d_View::Convert` /
`ConvertWithProj` to turn the mouse into a 3D ray, intersected with the sketch `gp_Pln` (`IntAna_IntConicQuad`).
Use `AIS_Shape`/`AIS_Line`/`AIS_Point` for sketch entities, `PrsDim_LengthDimension`/`PrsDim_RadiusDimension`
for on-canvas dimensions, `AIS_Manipulator` for move/rotate gizmos (assembly drag, "Move/Copy").

Older alternative, `REF/occt-samples/occt/samples/qt/Common/src/View.cxx`: a native `QWidget` with
`WA_PaintOnScreen` and `Xw_Window(winId)`. It is simpler, but Qt widgets can't overlay it and docking can recreate the
window. **Prefer QOpenGLWidget.**

---

## Build-cost notes (answer to "is this computer powerful enough")

Yes. The heavy parts (OCCT, Qt) are prebuilt system packages, so FabriCAD only compiles its own code plus
PlaneGCS. The biggest PlaneGCS file (GCS.cpp) took 21 s on one core. TUs that include OCCT and Qt headers take
about 5-20 s and 300-700 MB each, so `-j2` stays well inside 4 GB free. A full MVP rebuild should take a few minutes. Use
`ccache` if it is available, and consider precompiled headers for OCCT (`target_precompile_headers`). OndselSolver, if adopted later, adds
about 10 min to a one-time build. The Intel UHD 620 (GL 4.6) handles AIS shaded views of typical parts. For large assemblies, lower MSAA and
`Prs3d_Drawer` deviation settings.
