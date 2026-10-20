// FabriCAD viewer: topology helpers for selection.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/TopoHelpers.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <deque>

namespace fabricad::viewer {

namespace {

/// Unit tangent of `e` at vertex `v` (oriented along the edge parameter), false if undefined.
bool tangentAt(const TopoDS_Edge& e, const TopoDS_Vertex& v, gp_Vec& t) {
  if (BRep_Tool::Degenerated(e)) return false;
  BRepAdaptor_Curve c(e);
  const double u = BRep_Tool::Parameter(v, e);
  gp_Pnt p;
  c.D1(u, p, t);
  if (t.Magnitude() < 1e-12) return false;
  t.Normalize();
  return true;
}

}  // namespace

std::vector<TopoDS_Edge> tangentChain(const TopoDS_Shape& shape, const TopoDS_Edge& seed, double angleTolDeg) {
  std::vector<TopoDS_Edge> out;
  if (shape.IsNull() || seed.IsNull()) return out;
  TopTools_IndexedDataMapOfShapeListOfShape v2e;
  TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_VERTEX, TopAbs_EDGE, v2e);
  const double sinTol = std::sin(angleTolDeg * M_PI / 180.0);

  TopTools_MapOfShape visited;
  std::deque<TopoDS_Edge> queue{seed};
  visited.Add(seed);
  while (!queue.empty()) {
    TopoDS_Edge e = queue.front();
    queue.pop_front();
    out.push_back(e);
    TopoDS_Vertex v1, v2;
    TopExp::Vertices(e, v1, v2);
    for (const TopoDS_Vertex& v : {v1, v2}) {
      if (v.IsNull() || !v2e.Contains(v)) continue;
      gp_Vec t1;
      if (!tangentAt(e, v, t1)) continue;
      for (TopTools_ListOfShape::Iterator it(v2e.FindFromKey(v)); it.More(); it.Next()) {
        const TopoDS_Edge& e2 = TopoDS::Edge(it.Value());
        if (visited.Contains(e2) || BRep_Tool::Degenerated(e2)) continue;
        gp_Vec t2;
        if (!tangentAt(e2, v, t2)) continue;
        if (t1.Crossed(t2).Magnitude() <= sinTol) {
          visited.Add(e2);
          queue.push_back(e2);
        }
      }
    }
  }
  return out;
}

}  // namespace fabricad::viewer
