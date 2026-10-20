# FabriCAD — Core Algorithms Research (MVP)

Status: research / recommendations. Target stack: C++20, Qt 6 Widgets, OpenCASCADE 7.9.3, Eigen.
All OCCT API names below were checked against `/usr/include/opencascade` on the dev machine.

---

## 0. Machine fitness ("is this computer powerful enough?")

Measured: 8 threads, ~7 GB RAM (about 4 GB available with the desktop running), 7 GB swap.

* **Running FabriCAD**: yes. A Fusion-like MVP (sketches, a few hundred B-rep faces, small assemblies,
  HLR drawings) needs roughly 200–600 MB RAM. OCCT is already installed, so we never build it.
* **Building FabriCAD**: yes, with care. Heavy OCCT/Qt translation units peak at about 0.6–1.2 GB per
  `cc1plus`. `-j2` stays under about 2.5 GB. Do not raise it while other agents compile.
  Mitigations: precompiled header for Qt+OCCT includes (`target_precompile_headers`), split
  OCCT-heavy code into a few static libs, `-O1 -g1` for dev builds, link with `-fuse-ld=mold` or `lld`
  if available, and avoid unity builds (they raise peak RAM).
* **Not feasible here**: building OCCT or FreeCAD from source, or running OCCT GL tests headless
  (there is no Xvfb). Keep the core logic GL-free so it can be tested with `QT_QPA_PLATFORM=offscreen`.

---

## 1. 2D geometric constraint solver

### 1.1 Options

| | Vendor FreeCAD PlaneGCS | Custom Eigen solver |
|---|---|---|
| Maturity | 10+ years, handles B-splines and conics, has DogLeg/LM/BFGS, SQP for drag, QR-based redundancy and conflict detection | Must be written (~2–3 kLOC for MVP) |
| License | LGPL-2.1+ (compatible) | Ours |
| Coupling | `GCS::System`, uses `double*` parameter pointers, depends on Eigen and boost (graph for subsystem splitting in some versions) | Clean, fits our document model |
| Diagnostics | Gives conflicting/redundant constraint *tags*, not DOF per entity | We design it for UI: per-entity DOF, colors |
| Size | about 10 kLOC (GCS.cpp, Constraints.cpp, Geo.cpp, SubSystem.cpp, qp_eq.cpp) | small |

**Recommendation:** write a **custom Eigen solver** with the architecture below, modeled on PlaneGCS
(same constraint formulations, DogLeg plus LM fallback, QR rank analysis). Keep the API at the
level `Solver::solve(params, constraints, dragTargets)` so PlaneGCS could be vendored later behind
the same interface if needed (for example, for B-spline constraints). If code is ported from
PlaneGCS, keep the LGPL headers and record it in THIRD_PARTY.md. Reasons: tighter UI diagnostics,
no boost, easier to keep deterministic and test. Effort: about 1 week for an MVP.

### 1.2 Parameterization

Each entity holds indices into a global parameter vector `x ∈ R^n`:

* Point: `(px, py)`, 2 DOF
* Line segment: two points, 4 DOF
* Circle: center `(cx, cy)` plus `r`, 3 DOF
* Arc: center, `r`, start angle `a0`, end angle `a1` (5 DOF). The endpoints are *derived*:
  `S = c + r(cos a0, sin a0)`, `E = c + r(cos a1, sin a1)`. Store arc endpoints as real
  point params plus internal constraints `|S−c| = r`, `|E−c| = r` (PlaneGCS style). This makes
  coincidences uniform. Arc: 7 params with 2 internal equations, so 5 DOF.
* Ellipse / spline: post-MVP.
* Construction geometry: same params, flagged `construction` (excluded from profiles).

Fixed/reference parameters are removed from `x` and treated as constants. This is cheaper and
better conditioned than adding `x_i − k = 0` equations.

### 1.3 Constraint equations `f_k(x) = 0`

Notation: points P, Q. Line L = (A,B), direction `d = B − A`. Circle/arc C = (c, r).
`cross(u,v) = u.x v.y − u.y v.x`. Distances are signed where noted. Normalizing (dividing by |d|)
keeps residuals in length units, which helps conditioning and makes tolerances meaningful.

| Constraint | Equations | #eq |
|---|---|---|
| Coincident P,Q | `Px−Qx`, `Py−Qy` | 2 |
| Horizontal L | `By − Ay` | 1 |
| Vertical L | `Bx − Ax` | 1 |
| Horizontal P,Q (points) | `Qy − Py` | 1 |
| Parallel L1,L2 | `cross(d1,d2)/(|d1||d2|)` (sin θ) | 1 |
| Perpendicular L1,L2 | `dot(d1,d2)/(|d1||d2|)` | 1 |
| Tangent line–circle | `cross(d, c−A)/|d| − s·r = 0`, s = ±1 fixed at creation from the current side (avoid `|·|`) | 1 |
| Tangent circle–circle, external | `|c1−c2| − (r1+r2)` | 1 |
| Tangent circle–circle, internal | `|c1−c2| − |r1−r2|` → use `|c1−c2| − s(r1−r2)` with s fixed | 1 |
| Tangent at shared endpoint (arc–line, arc–arc, G1 "smooth") | coincident (2) + direction: `cross(t1, t2)/(|t1||t2|)` with tangent of arc at P `t = perp(P − c)` | 3 |
| Equal length L1,L2 | `|d1| − |d2|` | 1 |
| Equal radius C1,C2 | `r1 − r2` | 1 |
| Concentric | `c1x−c2x`, `c1y−c2y` | 2 |
| Midpoint P on L | `Px − (Ax+Bx)/2`, `Py − (Ay+By)/2` | 2 |
| Point on line (infinite) | `cross(d, P−A)/|d|` | 1 |
| Point on circle/arc | `|P−c| − r` | 1 |
| Symmetric P,Q about line L | `dot(Q−P, d)/|d|` (PQ ⟂ L) and `cross(d, M−A)/|d|` with `M=(P+Q)/2` (M on L) | 2 |
| Symmetric P,Q about point M | midpoint equations | 2 |
| Fix P | parameters removed (or `Px−kx, Py−ky`) | 2 |
| Distance P,Q | `|Q−P| − D` (use `|Q−P|² − D²` only when D≫0; the plain form is better conditioned) | 1 |
| Distance P to line | `cross(d, P−A)/|d| − s·D` | 1 |
| Distance line‖line | point-to-line on L2.A | 1 |
| Distance-X / Distance-Y | `Qx − Px − Dx` / `Qy − Py − Dy` (signed) | 1 |
| Radius / Diameter | `r − R` / `2r − D` | 1 |
| Angle L1,L2 = θ | `atan2(cross(d1,d2), dot(d1,d2)) − θ`, wrap the residual to (−π, π]. Better near 0/π: `cross·cosθ − dot·sinθ` normalized | 1 |
| Line length | `|B−A| − D` | 1 |
| Arc length (post-MVP) | `r(a1−a0) − D` | 1 |

Analytic Jacobians: implement `residual()` and `gradient()` per constraint and write the partial
derivatives by hand (PlaneGCS does this). In debug builds check them against central finite
differences, `h = 1e-7·max(1,|x|)`.

### 1.4 Solve algorithm

Minimize `F(x) = ½‖f(x)‖²`, J = ∂f/∂x (m×n, sparse in practice but small: m,n < 500, so dense Eigen is fine).

```
solve(x0):
  decompose constraint graph into connected components (union-find over params)  // big win
  for each component:
     x = DogLeg(x0)              // trust region, Powell; default
     if not converged: x = LevenbergMarquardt(x0)   // fallback
     if not converged: report "failed", keep x0
DogLeg step:
  g  = Jᵀ f
  h_gn = solve J h = −f (least squares / min-norm via ColPivHouseholderQR or CompleteOrthogonalDecomposition)
  h_sd = −α g,  α = ‖g‖² / ‖J g‖²
  if ‖h_gn‖ ≤ Δ: h = h_gn
  elif ‖h_sd‖ ≥ Δ: h = (Δ/‖h_sd‖) h_sd
  else: h = h_sd + β(h_gn − h_sd), β s.t. ‖h‖ = Δ
  ρ = (F(x) − F(x+h)) / (L(0) − L(h)),   L(h)=½‖f+Jh‖²
  accept if ρ>0; Δ ← ρ>0.75 ? max(Δ,3‖h‖) : ρ<0.25 ? Δ/2 : Δ
  stop: ‖f‖∞ < 1e-10 (in model units, mm) or ‖h‖ < 1e-12(‖x‖+1e-12) or iter > 100
```

Under-constrained systems: Gauss-Newton with the **minimum-norm** step (CompleteOrthogonalDecomposition)
moves the geometry as little as possible. This is the "least surprise" behavior users expect.

### 1.5 DOF, status, redundant/conflicting detection

At the solved (or initial) x, compute J (m×n) and a rank-revealing QR with column pivoting of **Jᵀ**
(rows of J are constraints, so pivoting Jᵀ columns picks independent constraints):

```
qr = ColPivHouseholderQR(Jᵀ); qr.setThreshold(1e-9 * max(1, maxAbs(J)))
rank = qr.rank()
DOF  = n − rank
independent constraints = first `rank` pivots of qr.colsPermutation()
dependent set D = remaining constraints
for each k in D:
   if |f_k(x)| > tol after solve → CONFLICTING  (cannot be satisfied with the others)
   else                          → REDUNDANT    (satisfied but adds no information)
status:
   any conflicting  → OVER-CONSTRAINED (red)
   DOF == 0 and no redundants → FULLY CONSTRAINED (black, Fusion style)
   DOF == 0 with redundants   → fully constrained + redundant warning
   DOF  > 0 → UNDER-CONSTRAINED (blue), show "N DOF"
```

To find **which** user constraints conflict (so the UI can list them), take each dependent row
k and express it in the independent rows: solve `J_indᵀ y = J_kᵀ` (least squares). Constraints with
`|y_i| > ε` together with k form the conflict group (this is PlaneGCS's approach). Report the
**most recently added** constraint as the culprit. When the user adds a constraint that makes the
set conflicting, reject it, or keep it as "driven/reference" (Fusion asks to make it a driven dimension).

**Per-entity DOF (to color fully constrained geometry):** compute the null space N of J
(SVD `JacobiSVD` or the `CompleteOrthogonalDecomposition` kernel, n − rank columns). A parameter i
is fixed iff row i of N ≈ 0 (`‖N_i‖ < 1e-8`). An entity is fully constrained iff all its params are
fixed. The SVD is O(n³) but n is below about 300 per component, which takes under 5 ms.

Caveat: rank tests at a degenerate configuration (for example, coincident points made by a horizontal
plus a vertical constraint at zero length) can give false redundancy. Mitigation: evaluate rank at
the solved state, and if the solve lands near-degenerate, perturb x by 1e-6 and re-evaluate (FreeCAD
does a similar "re-solve after perturbation" check).

### 1.6 Dragging under-constrained geometry

Fusion/SolidWorks behavior: the grabbed point follows the mouse as closely as constraints allow, and
everything else moves minimally.

```
drag(point P, target T):
  hard: f(x)=0
  soft: w·(P(x) − T) with w = 1 (weak), plus μ·(x − x_prev) with μ = 1e-3 (inertia, keeps others still)
  Option A (simple, robust): solve min ‖f‖² with augmented rows [f; w(P−T); μ(x−x_prev)]
     with w ≪ 1 relative scaling (e.g. 1e-2) so hard constraints dominate, then do a final pass with
     only hard constraints from that result (projects back onto the constraint manifold).
  Option B (exact, PlaneGCS "SQP"): min ½‖P−T‖² + ½μ‖x−x_prev‖²  s.t. f(x)=0
     Newton-KKT: [H  Jᵀ; J 0][Δx; λ] = [−∇g; −f], with H = Gauss-Newton Hessian of the objective.
```

Recommendation: implement Option B (KKT via `FullPivLU` or `CompleteOrthogonalDecomposition`).
The system is small and the result is stable at 60 fps. Fall back to Option A if KKT is singular.
Warm-start from the previous frame's x and cap iterations at 20 per mouse move. Solve only the
connected component that contains the dragged entity.

### 1.7 Dimensions as parameters

Dimensions store an expression string (`d1 = 2*width + 5`). Parameters are evaluated with a tiny
recursive-descent expression parser (units mm/in/deg, + − * / ^, sqrt, sin…) before the solve.
"Driven" (reference) dimensions are not equations. They are measured after the solve.

---

## 2. Sketch geometry → profiles (Fusion-style region picking)

Goal: every closed bounded region of the planar arrangement of all non-construction curves is
selectable (including regions made by overlapping circles and lines crossing). Multiple selected
regions are fused into one face set for extrude/revolve.

### 2.1 Recommended OCCT pipeline

```
edges = make TopoDS_Edge for each non-construction curve, in sketch plane (3D via plane placement)
       (GC_MakeSegment / GC_MakeArcOfCircle / gp_Circ → BRepBuilderAPI_MakeEdge)
// 1. intersect & split all edges against each other
BOPAlgo_Builder bld;  for e in edges: bld.AddArgument(e)
bld.SetFuzzyValue(1e-7); bld.SetNonDestructive(true); bld.SetRunParallel(false)
bld.Perform();   // check bld.HasErrors()
splitEdges = bld.Shape()     // compound of shared, split edges (common vertices merged)
//  (equivalently BRepAlgoAPI_BuilderAlgo or BRepAlgoAPI_Splitter with all edges as objects)
// 2. faces from the planar arrangement
TopoDS_Shape wires, faces;
BOPAlgo_Tools::EdgesToWires(splitEdges, wires, /*theShared=*/true);
BOPAlgo_Tools::WiresToFaces(wires, faces);
```

`WiresToFaces` builds faces with holes from nested wires. For Fusion-like picking, though, we want
**every minimal region** as its own profile (an annulus is one profile, and the inner disk is another).
The most robust way: build one big face on the sketch plane that covers the bounding box, then split
it with all edges.

```
BRepBuilderAPI_MakeFace bigFace(gp_Pln(sketchPlane), umin-δ, umax+δ, vmin-δ, vmax+δ)
BRepAlgoAPI_Splitter sp; sp.SetArguments({bigFace}); sp.SetTools({edges...}); sp.Build()
regions = faces of sp.Shape() that do NOT touch the bounding-box outer boundary
```

The splitter (General Fuse on face + edges) returns exactly the arrangement's faces, holes are
handled, and dangling edges are ignored automatically. This is the **recommended** method. It is
simple and reliable, and region identity can be tracked through `sp.Modified()`.

### 2.2 Own planar-arrangement fallback (no OCCT, testable, fast for hover)

For hover highlighting during sketching, a pure-2D DCEL is fast and has no OCCT dependency:

```
1. intersect all curve pairs (line-line, line-circle, circle-circle closed form) → split params
2. split curves into sub-edges; snap vertices with tolerance (grid hash) 
3. build half-edges; at each vertex sort outgoing half-edges by tangent angle
   (tie-break by curvature for tangent arcs)
4. face tracing: next(h) = twin(h) rotated clockwise at its origin → cycles
5. signed area (shoelace + arc segment area ½r²(θ−sinθ)): positive = bounded face boundary, negative = hole/outer
6. assign holes to the smallest containing outer cycle (point-in-polygon on a sample point)
7. drop dangling edges first (iteratively remove degree-1 vertices)
```

Use the DCEL for picking and preview. Use the OCCT splitter result to build the actual faces.

### 2.3 Region identity across recomputes (avoid "topological naming" breakage)

Features store the selected profiles as a **sample point inside the region** (an interior point computed
via `BRepTopAdaptor_FClass2d` / a pole of inaccessibility) plus the set of bounding sketch-curve IDs.
On recompute, pick the region that contains the stored point and has the best overlap of boundary IDs.
This is far more stable than storing face indices.

---

## 3. Drawings

### 3.1 HLR algorithm choice

| | `HLRBRep_Algo` (exact) | `HLRBRep_PolyAlgo` (on triangulation) |
|---|---|---|
| Input | exact B-rep | requires `BRepMesh_IncrementalMesh` first |
| Output | exact curves (lines, circles stay circles → clean DXF/dims) | polylines |
| Speed | slow: seconds to minutes for 1000+ faces, roughly O(edges × faces) | fast: 10–100× faster |
| Quality | best; smooth/sewn edges classified | faceting artifacts, broken outlines on curved surfaces |

**Recommendation:** use exact `HLRBRep_Algo` for the final views (dims snap to real circles and lines),
and run it on a **worker thread** (`QtConcurrent`). Use `HLRBRep_PolyAlgo` + `HLRBRep_PolyHLRToShape`
for an instant preview while the exact result computes. Cache results per (shape hash, view direction).
FreeCAD TechDraw makes the same split ("CoarseView").

```
Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
algo->Add(shape, /*nbIso=*/0);
gp_Ax2 viewAx(origin, viewDir /*toward viewer*/, xDir);
algo->Projector(HLRAlgo_Projector(viewAx));          // orthographic
algo->Update(); algo->Hide();
HLRBRep_HLRToShape h(algo);
visibleSharp   = h.VCompound();          // sharp edges, visible
visibleSmooth  = h.Rg1LineVCompound();   // tangent (G1) edges → thin/optional (Fusion "tangent edges")
visibleSewn    = h.RgNLineVCompound();   // seam-ish edges, normally hidden
visibleOutline = h.OutLineVCompound();   // silhouettes of curved surfaces
hidden*        = h.HCompound(), h.OutLineHCompound(), h.Rg1LineHCompound()
```

Results are edges in the **projection plane** (Z=0 in view coordinates), so 2D coordinates are the X,Y
of the curve points. Walk the edges with `BRepAdaptor_Curve`. `GeomAbs_Line` / `GeomAbs_Circle`
maps to an SVG/DXF line or arc. Other types become a polyline from `GCPnts_TangentialDeflection`
(or `GCPnts_QuasiUniformDeflection`), or B-spline approximations.

Line styles (ISO 128): visible 0.5 mm continuous, hidden 0.25 mm dashed, smooth/tangent 0.18 mm thin
(optional), center lines thin chain.

### 3.2 Standard views

Front/top/right/iso through view axes. First- vs third-angle projection only changes view placement on
the sheet. Isometric: `viewDir = (1,1,1)/√3` (third angle: (−1,−1,1) etc.). Scale is applied when
mapping to the sheet. Projected views are aligned with the parent and dragged on one axis only.

### 3.3 Section views

```
section line: 2D polyline drawn on the parent view → plane(s) in 3D (point + normal = viewDir × lineDir)
half = BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(gp_Pln(p, n)).Face(), pointOnRemovedSide)
kept = BRepAlgoAPI_Cut(shape, half.Solid())
HLR(kept, viewDir = n toward viewer)
cutFaces = faces of BRepAlgoAPI_Section / Common(shape, plane face): (BRepAlgoAPI_Common(shape, bigPlanarFace))
          → project them (they lie in the section plane, so drop the coordinate) → hatch regions
hatch: for each cut face polygon (with holes), clip parallel lines at 45° spacing s:
          scanline: for each line y' = k·s in rotated frame, intersect with all boundary segments,
          sort intersection params, fill between pairs (even-odd)
       or in Qt: QPainterPath(face boundary, Qt::OddEvenFill) + QBrush(Qt::BDiagPattern) / custom clipped lines
```

For DXF, emit the hatch as explicit LINE entities (R12 has no HATCH entity). This is simple and universal.
Detail view: circle-clip a scaled copy of the parent's 2D edges (clip in 2D, no new HLR).

### 3.4 Dimensions

* Store drawing dimensions **as references to projected edges/vertices** plus a fallback 2D position.
  Identify each projected edge by (source 3D edge index found via a `TopTools_IndexedMapOfShape` of the model
  + mid-point projection match). HLR loses the 3D↔2D link, so recover it by projecting each 3D edge's
  midpoint with `HLRAlgo_Projector::Project` and matching it to the nearest 2D edge.
* Linear: distance between two projected vertices along X/Y/aligned. Radius/diameter: from a `GeomAbs_Circle`
  projected edge (true circles only appear when the view is normal to the axis). Angle: between two lines.
* Values = model distance = sheet distance / view scale, formatted with tolerance and precision.
* Rendering: extension lines (1–2 mm gap, 2 mm overshoot), arrowheads 3 mm, text 3.5 mm (ISO 3098).
* Optional "auto-dimension from sketch": reuse sketch dimensions whose plane is parallel to the view.

### 3.5 Output

* **Screen/SVG/PDF:** one `DrawingRenderer::paint(QPainter&, sheet)` that works in sheet mm. The same
  code renders to `QWidget` (zoom/pan transform), `QSvgGenerator` (setViewBox in mm), and `QPdfWriter`
  (`setPageSize(QPageSize::A3)`, `setResolution(1200)`, scale = res/25.4). Use cosmetic=false pens
  with widths in mm. Fonts: ISO-ish sans via `QFont` (bundle "osifont" (GPL+font exception) or use DejaVu Sans).
* **DXF R12 ASCII writer (~300 LOC):**

```
0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n9\n$INSUNITS\n70\n4\n0\nENDSEC
0\nSECTION\n2\nTABLES\n  LTYPE table: CONTINUOUS, HIDDEN (dash 6.35/-3.175), CENTER
                     LAYER table: VISIBLE(7), HIDDEN(ltype HIDDEN), CENTER, DIM, HATCH, BORDER
0\nENDSEC
0\nSECTION\n2\nENTITIES
  LINE:     0 LINE 8 <layer> 10 x1 20 y1 30 0 11 x2 21 y2 31 0
  CIRCLE:   0 CIRCLE 8 L 10 cx 20 cy 30 0 40 r
  ARC:      0 ARC 8 L 10 cx 20 cy 30 0 40 r 50 startDeg 51 endDeg     (CCW!)
  POLYLINE: 0 POLYLINE 8 L 66 1 70 0 / 0 VERTEX 8 L 10 x 20 y ... / 0 SEQEND
  TEXT:     0 TEXT 8 L 10 x 20 y 40 height 1 string 50 angle
0\nENDSEC\n0\nEOF
```

  Format numbers with `%.6f` in the "C" locale (avoid QLocale decimal commas). Write dimensions as
  exploded lines plus TEXT (R12 DIMENSION needs anonymous blocks; skip for MVP). Arc direction: HLR
  edges can be reversed. Check edge orientation and swap start/end so the arc is CCW in DXF.
  Mirror Y when going between Qt (y-down) and DXF (y-up).

---

## 4. Assembly

### 4.1 Representation

```
Component { id, name, partRef (document/file + body id), Placement (gp_Trsf), grounded: bool,
            children (sub-assembly) }
JointOrigin { componentId, localFrame: gp_Ax3 }   // defined from geometry: face center, edge midpoint,
                                                   // circle center, vertex; z = face normal/axis
Joint { type, A: JointOrigin, B: JointOrigin, offset (gp_Trsf), driveValues[], limits }
```

World frame of an origin: `W = T_comp ∘ L`. A joint states `T_A ∘ L_A ∘ M(q) = T_B ∘ L_B`, where M(q) is
the joint motion (with flip/offset). Like Fusion: the B component moves so that its origin meets A's.

| Joint (Fusion name) | M(q) | DOF |
|---|---|---|
| Rigid | identity | 0 |
| Revolute | Rot_z(θ) | 1 |
| Slider | Trans_z(d) | 1 |
| Cylindrical | Rot_z(θ)·Trans_z(d) | 2 |
| Pin-slot | Rot_z(θ)·Trans_x(d) | 2 |
| Planar | Trans_x(u)Trans_y(v)Rot_z(θ) | 3 |
| Ball | Rot_zyx(α,β,γ) | 3 |

### 4.2 MVP solver: tree propagation from ground

```
build graph: nodes = components, edges = joints
BFS from grounded components (and components with 'ground' flag)
for each joint (parent P already placed → child C):
   T_C = T_P ∘ L_P ∘ M(q_joint) ∘ L_C⁻¹
detect: a component reached twice (loop) → if consistent within tol fine, else mark joint "conflict" (red)
unreached components keep their free placement (user-dragged)
```

Joint driving (animate/drag): change q in M(q), then re-propagate the subtree. O(N), instant.
This covers most hobbyist/mechanical assemblies (Fusion's joints are mostly tree-like).
"As-built joints" = compute L_C so the current placement is preserved.

### 4.3 Optional general solver (post-MVP, for closed loops such as four-bar linkages)

Unknowns: per free component, translation t and rotation as a rotation vector ω (incremental:
`R = exp([ω]×)·R0`, re-linearize each iteration). Joint residuals are written as coincident points,
parallel axes (`z_A × z_B = 0`, 2 independent eqs), and in-plane distance. Then reuse the same DogLeg/QR
machinery as the sketch solver (6 unknowns per body, same rank/DOF analysis gives "N DOF remaining").
Drag-in-assembly uses the same KKT drag formulation as §1.6.

### 4.4 Interference and display

Interference check: `BRepAlgoAPI_Common` per pair after an AABB prefilter (`Bnd_Box`), with
`BRepExtrema_ShapeProximity` for faster mesh-based checks. Display: one `AIS_Shape` per *part*,
reused with `AIS_ConnectedInteractive` for each instance (shares the mesh, cheap for repeated
fasteners), and `SetLocalTransformation(T)` to move it without re-meshing.

---

## 5. File format (.fcad)

JSON (`QJsonDocument`, or nlohmann/json if speed matters; Qt is fine for MVP). Store **only the
parametric definition** and recompute on load. Optionally embed a cached BREP so files open instantly
and can be viewed even if a recompute fails.

```json
{
  "format": "fabricad", "version": 1, "units": "mm",
  "parameters": [{"name": "width", "expr": "40 mm"}],
  "bodies": [...],
  "features": [
    {"id": "f1", "type": "Sketch", "name": "Sketch1",
     "plane": {"ref": "origin:XY"} ,
     "geometry": [{"id": 1, "type": "line", "p": [0,0,40,0], "construction": false},
                  {"id": 2, "type": "arc", "c": [..], "r": 5, "a": [0, 1.57]}],
     "constraints": [{"type": "coincident", "a": [1, "end"], "b": [2, "start"]},
                     {"type": "distance", "a": [1,"start"], "b": [1,"end"], "expr": "width", "driving": true}]},
    {"id": "f2", "type": "Extrude", "profiles": [{"sketch": "f1", "seed": [10.2, 4.1], "curves": [1,2,3,4]}],
     "extent": {"type": "distance", "expr": "10 mm"}, "operation": "newBody", "direction": "one-side"},
    {"id": "f3", "type": "Fillet", "edges": [{"sig": {...}}], "radius": "2 mm"}
  ],
  "timelineMarker": 3,
  "assembly": {"components": [...], "joints": [...]},
  "drawings": [{"sheet": "A3", "views": [...], "dimensions": [...]}],
  "cache": {"brep": "base64-or-sidecar"}
}
```

* Save solved geometry values too (they are the warm start for the solver and make loading deterministic).
* Writing: write to `file.tmp`, fsync, then rename. Optionally zip (`.fcad` = zip with document.json +
  cache.brep + thumbnail.png), like FreeCAD's .FCStd. MVP: plain JSON plus an optional sidecar.
* Version field plus migration functions from day one.
* **Topological references** (fillet edges, sketch on face): store a *geometric signature*: generating
  feature id + `BRepAlgoAPI_*::Generated/Modified` history tag where available, and as a fallback the
  edge type, midpoint, length, and adjacent-face normals. Resolve on recompute by best match. Accept that
  the MVP is imperfect here. This is the classic "topological naming problem".

**STEP export:**
* Single part: `STEPControl_Writer w; w.Transfer(shape, STEPControl_AsIs); w.Write(path)`.
* Assembly with names/colors: build an XCAF document. `TDocStd_Document` via `XCAFApp_Application`,
  `XCAFDoc_ShapeTool::AddShape(part, /*makeAssembly=*/false)` once per part, then
  `AddComponent(asmLabel, partLabel, TopLoc_Location(trsf))` per instance, `TDataStd_Name::Set`,
  `XCAFDoc_ColorTool::SetColor`, then `STEPCAFControl_Writer::Transfer(doc, STEPControl_AsIs)` and `Write()`.
  Set `write.step.schema` (AP214/AP242) through `Interface_Static::SetCVal` (deprecated but works) or the DE
  wrapper `DESTEP_Parameters`. Import works the same way with `STEPCAFControl_Reader`.
* STL: `BRepMesh_IncrementalMesh` with export tolerance, then `StlAPI_Writer`.

---

## 6. Performance

### 6.1 Incremental recompute

* The feature list is a linear timeline (Fusion style), with an implicit DAG via references
  (parameters → sketches → features → assembly → drawings).
* Each feature caches `(inputHash, outputShape)`. inputHash = hash(own params + evaluated expressions +
  upstream outputs' hashes). When something changes: mark it dirty, then recompute from the first dirty
  feature onward. Features whose inputHash is unchanged reuse their cached shape.
* Editing a sketch: re-solve only the edited connected component(s). Updating profile regions takes about 1 ms.
* Run recompute on a worker thread (OCCT modeling is thread-safe per independent shape; never touch
  AIS objects off the GUI thread). Show the last good model while computing. Cancel with
  `Message_ProgressRange` / `Message_ProgressIndicator::UserBreak()` when the user edits again.
* Booleans: `SetRunParallel(true)` on `BRepAlgoAPI_*` for large shapes (TBB is installed), and
  `SetFuzzyValue` only if needed. Call `ShapeUpgrade_UnifySameDomain` after booleans to merge coplanar
  faces (cleaner shape, faster later ops, and better drawings).

### 6.2 Meshing for display

```
IMeshTools_Parameters p;
p.Deflection = 0.001 * bboxDiagonal (clamped 0.01 .. 1 mm)   // or Relative=true, Deflection=0.001
p.Angle      = 0.5 rad (≈28°; Fusion-like smoothness ~0.35)
p.InParallel = true;  p.MinSize = p.Deflection * 0.1
BRepMesh_IncrementalMesh(shape, p);
```

* Mesh only once per shape version. `BRepMesh_IncrementalMesh` skips faces that already have an
  adequate triangulation, so unchanged faces of `Modified` shapes stay cheap.
* Use coarser settings while dragging or regenerating (deflection ×4), then refine when idle.
* Also set `Prs3d_Drawer::SetDeviationCoefficient` / `SetTypeOfDeflection(Aspect_TOD_RELATIVE)` on
  `AIS_InteractiveContext`'s default drawer so AIS and our mesh agree. Otherwise AIS re-meshes.

### 6.3 AIS display

* Display mode `AIS_Shaded` (1) with `SetFaceBoundaryDraw(true)` on the drawer for Fusion-style
  shaded-with-edges (one draw call for boundaries, which is far cheaper than wireframe+shaded overlays).
* Selection modes enabled lazily: activate face/edge/vertex modes (`AIS_Shape::SelectionMode(TopAbs_FACE)`)
  only while a command needs them. BVH building for selection on a big model is the hidden cost.
* `Graphic3d_RenderingParams`: MSAA 4 (`NbMsaaSamples`), `ToShowStats` in debug. Use the default
  `Graphic3d_TOSM_FRAGMENT` (Phong) shading.
* Sketch overlay: render the sketch with `AIS_Shape` on edges, or better a custom lightweight
  `AIS_InteractiveObject` with `Graphic3d_ArrayOfSegments` rebuilt per solve. This is much cheaper than
  building TopoDS edges at 60 fps while dragging.
* View cube: `AIS_ViewCube`. Grid: `V3d_Viewer::ActivateGrid` or a custom shader-free line array.

### 6.4 Fast startup

* Use OCCT's `OpenGl_GraphicDriver` and create the context only after the main window shows
  (`QTimer::singleShot(0, ...)`). Embed with `QOpenGLWidget`, following OCCT's `AIS_ViewController`
  Qt sample (OCCT ≥7.6 supports wrapping an existing FBO/GL context).
* Link only the needed OCCT toolkits (TKernel, TKMath, TKG2d, TKG3d, TKGeomBase, TKBRep, TKGeomAlgo,
  TKTopAlgo, TKPrim, TKBO, TKBool, TKFillet, TKOffset, TKShHealing, TKMesh, TKHLR, TKService, TKV3d,
  TKOpenGl, TKDESTEP (7.9: TKDESTEP replaces TKSTEP*), TKDESTL, TKXCAF, TKLCAF, TKCAF). Every extra
  shared lib adds relocation time.
* Lazily initialize STEP/XCAF (`STEPCAFControl_Controller::Init()`) on the first import/export.
* No plugin scanning, no Python at startup. Target: under 0.5 s to the first window, warm.
* Build with `-O2` for release. Consider `-fvisibility=hidden` plus `-Wl,--as-needed`.

---

## 7. Suggested module layout (for planners)

```
src/core/expr        expression parser + units           (no Qt/OCCT)
src/core/solver2d    constraint solver (Eigen only)       → unit tests headless
src/core/sketch      entities, constraints, DCEL regions  → unit tests headless
src/core/model       features, recompute engine, OCCT shapes, profiles via BRepAlgoAPI_Splitter
src/core/assembly    components, joints, tree solver
src/core/drawing     HLR, sections, hatching, dims, SVG/PDF/DXF writers (QPainter, no GL)
src/core/io          .fcad JSON, STEP/STL
src/app              Qt Widgets UI, AIS viewer, commands, timeline
tests/               QtTest/Catch2, run with QT_QPA_PLATFORM=offscreen
```

## 8. References

* FreeCAD `src/Mod/Sketcher/App/planegcs/` (GCS.cpp: DogLeg, LM, BFGS, SQP, `diagnose()` with QR); LGPL-2.1+.
* Madsen, Nielsen, Tingleff, *Methods for Non-Linear Least Squares Problems* (DTU, 2004): DogLeg/LM.
* OCCT docs: Boolean Operations user guide (General Fuse, Splitter), HLR (`HLRBRep_Algo`), XDE (XCAF),
  BRepMesh. Headers verified: `BOPAlgo_Tools::EdgesToWires/WiresToFaces`, `HLRBRep_HLRToShape::{V,H,Rg1Line,RgNLine,OutLine,IsoLine}Compound`,
  `IMeshTools_Parameters::{InParallel,Relative}`.
* FreeCAD TechDraw (`GeometryObject.cpp`): HLR usage and the exact/poly split.
* AutoCAD DXF Reference R12 (group codes).
