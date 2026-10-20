# Third-party code and dependencies

FabriCAD itself is LGPL-2.1-or-later (see `LICENSE`). This file lists everything that is
not ours. **When you copy or adapt code from another project, keep its license header in the
file and add a row here** (path in our tree, origin, revision, license).

## Linked system libraries (not vendored)

| Library | Version on dev box | License | Used for |
|---|---|---|---|
| Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets, Svg, PrintSupport, Concurrent, Test) | 6.11 | LGPL-3.0 / GPL | UI, JSON, 2D drawing canvas, PDF/SVG export |
| OpenCASCADE Technology (OCCT) | 7.9.3 | LGPL-2.1 with OCCT exception | B-rep kernel, booleans, HLR, STEP/STL, AIS/V3d viewer |
| Eigen | 5.0.1 | MPL-2.0 | sketch constraint solver, assembly solver |
| oneTBB | system | Apache-2.0 | parallel booleans/meshing (optional) |

## Vendored / adapted code

| Path in FabriCAD | Origin | Revision | License | Notes |
|---|---|---|---|---|
| `src/sketch/third_party/planegcs/` | FreeCAD `src/Mod/Sketcher/App/planegcs/` (GCS, Constraints, Geo, SubSystem, qp_eq, Util) | `703986732cf5` | LGPL-2.1-or-later | Vendored by the sketch module; local changes marked `// FabriCAD:` and listed in its `README.md`; `compat/` shims replace FreeCAD/boost headers |
| `src/part/features/Hole.cpp` (`HoleFeature::makeProtoHole`) | FreeCAD `src/Mod/PartDesign/App/FeatureHole.cpp` (half cross-section of the hole revolved about its axis; counterbore / countersink / drill-tip construction) | FreeCAD tree in `../fabricad-ref` | LGPL-2.1-or-later | Adapted, not copied verbatim; the file header names the origin |
| `src/viewer/occt/` (`OcctGlTools.*`, `OcctQtTools.*`, `OcctView.*`) | github.com/gkv311/occt-samples-qopenglwidget | `72e32d3a8398` | MIT | `OcctGlTools`/`OcctQtTools` copied, `OcctView` adapted from `OcctQOpenGLWidgetViewer`; headers keep the original copyright and MIT notice |

Not vendored (reference reading only, re-implemented from the recipes in `docs/research/`):
FreeCAD TechDraw (`GeometryObject.cpp`, `DrawViewSection.cpp`) for HLR projection, section
cuts and the DXF R12 layout in `src/drawing`; FreeCAD Assembly for the joint model in
`src/assembly`. The sketch inference, part features other than the hole profile, and the
whole app shell are original code.

Reference copies live outside the repository in `../fabricad-ref/` and are documented in
`docs/research/freecad-occt.md`.
