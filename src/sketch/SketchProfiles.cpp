// FabriCAD sketch: OCCT bridge implementation (edges, splitter-based region detection).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchProfiles.h"

#include "core/Log.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_DataMapOfShapeInteger.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>

namespace fabricad::sketch {

gp_Pnt toWorld(const gp_Ax3& plane, Pt2 p) {
  gp_XYZ o = plane.Location().XYZ();
  gp_XYZ x = plane.XDirection().XYZ();
  gp_XYZ y = plane.YDirection().XYZ();
  return gp_Pnt(o + x * p.x + y * p.y);
}

Pt2 toLocal(const gp_Ax3& plane, const gp_Pnt& p) {
  gp_XYZ v = p.XYZ() - plane.Location().XYZ();
  return {v.Dot(plane.XDirection().XYZ()), v.Dot(plane.YDirection().XYZ())};
}

namespace {

/// gp_Ax2 whose X is the plane X and whose Y matches the plane Y (handles left-handed gp_Ax3).
gp_Ax2 circleAxes(const gp_Ax3& plane, const gp_Pnt& center, bool& mirrored) {
  gp_Ax2 ax(center, plane.Direction(), plane.XDirection());
  mirrored = ax.YDirection().Dot(plane.YDirection()) < 0;
  if (mirrored) ax = gp_Ax2(center, plane.Direction().Reversed(), plane.XDirection());
  return ax;
}

}  // namespace

TopoDS_Shape makeEdge(const Geometry& g, const gp_Ax3& plane) {
  try {
    switch (g.type) {
      case GeomType::Point: return {};
      case GeomType::Line: {
        if (g.length() < 1e-9) return {};
        return BRepBuilderAPI_MakeEdge(toWorld(plane, g.start()), toWorld(plane, g.end())).Edge();
      }
      case GeomType::Circle: {
        if (g.radius() < 1e-9) return {};
        bool mirrored = false;
        gp_Ax2 ax = circleAxes(plane, toWorld(plane, g.center()), mirrored);
        return BRepBuilderAPI_MakeEdge(gp_Circ(ax, g.radius())).Edge();
      }
      case GeomType::Arc: {
        if (g.radius() < 1e-9 || g.sweep() < 1e-9) return {};
        bool mirrored = false;
        gp_Ax2 ax = circleAxes(plane, toWorld(plane, g.center()), mirrored);
        gp_Circ circ(ax, g.radius());
        // With the reversed normal the angular parameter runs the other way round.
        double a0 = g.startAngle(), a1 = g.endAngle();
        Handle(Geom_TrimmedCurve) arc = mirrored ? GC_MakeArcOfCircle(circ, -a1, -a0, true).Value()
                                                 : GC_MakeArcOfCircle(circ, a0, a1, true).Value();
        TopoDS_Edge e = BRepBuilderAPI_MakeEdge(arc).Edge();
        if (mirrored) e.Reverse();
        return e;
      }
    }
  } catch (const Standard_Failure& f) {
    FC_WARN("sketch edge failed: {}", f.GetMessageString() ? f.GetMessageString() : "?");
  }
  return {};
}

TopoDS_Shape makeVertex(const Geometry& g, const gp_Ax3& plane) {
  return BRepBuilderAPI_MakeVertex(toWorld(plane, g.center())).Vertex();
}

TopoDS_Shape makeEdgesCompound(const Sketch& sketch, const gp_Ax3& plane, bool includeConstruction) {
  TopoDS_Compound comp;
  BRep_Builder b;
  b.MakeCompound(comp);
  int n = 0;
  for (const Geometry& g : sketch.geometries()) {
    if (g.construction && !includeConstruction) continue;
    TopoDS_Shape s = g.type == GeomType::Point ? makeVertex(g, plane) : makeEdge(g, plane);
    if (s.IsNull()) continue;
    b.Add(comp, s);
    ++n;
  }
  if (n == 0) return {};
  return comp;
}

bool faceContains(const TopoDS_Face& face, const gp_Pnt& p, double tol) {
  if (face.IsNull()) return false;
  BRepClass_FaceClassifier cls(face, p, tol);
  return cls.State() == TopAbs_IN;
}

namespace {

std::optional<gp_Pnt> interiorPoint(const TopoDS_Face& face) {
  GProp_GProps props;
  BRepGProp::SurfaceProperties(face, props);
  gp_Pnt c = props.CentreOfMass();
  if (faceContains(face, c, 1e-7)) return c;
  Standard_Real umin, umax, vmin, vmax;
  BRepTools::UVBounds(face, umin, umax, vmin, vmax);
  Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
  if (surf.IsNull()) return std::nullopt;
  for (int n = 5; n <= 41; n = n * 2 + 1) {
    for (int i = 1; i < n; ++i) {
      for (int j = 1; j < n; ++j) {
        double u = umin + (umax - umin) * i / n;
        double v = vmin + (vmax - vmin) * j / n;
        BRepClass_FaceClassifier cls(face, gp_Pnt2d(u, v), 1e-7);
        if (cls.State() == TopAbs_IN) return surf->Value(u, v);
      }
    }
  }
  return std::nullopt;
}

}  // namespace

std::vector<ProfileRegion> computeProfiles(const Sketch& sketch, const gp_Ax3& plane) {
  std::vector<ProfileRegion> out;
  Pt2 mn, mx;
  if (!sketch.bbox(mn, mx, false)) return out;

  // original edges, indexed by curve id
  TopTools_ListOfShape tools;
  std::vector<std::pair<TopoDS_Shape, int>> originals;
  for (const Geometry& g : sketch.geometries()) {
    if (g.construction || !g.isCurve()) continue;
    TopoDS_Shape e = makeEdge(g, plane);
    if (e.IsNull()) continue;
    tools.Append(e);
    originals.push_back({e, g.id});
  }
  if (originals.empty()) return out;

  double size = std::max({mx.x - mn.x, mx.y - mn.y, 1.0});
  double delta = size * 0.5 + 1.0;
  double umin = mn.x - delta, umax = mx.x + delta, vmin = mn.y - delta, vmax = mx.y + delta;
  TopoDS_Face big;
  try {
    big = BRepBuilderAPI_MakeFace(gp_Pln(plane), umin, umax, vmin, vmax).Face();
  } catch (const Standard_Failure&) {
    return out;
  }

  BRepAlgoAPI_Splitter splitter;
  TopTools_ListOfShape args;
  args.Append(big);
  splitter.SetArguments(args);
  splitter.SetTools(tools);
  splitter.SetNonDestructive(true);
  splitter.SetRunParallel(false);
  splitter.SetFuzzyValue(1e-7);
  try {
    splitter.Build();
  } catch (const Standard_Failure& f) {
    FC_WARN("profile splitter failed: {}", f.GetMessageString() ? f.GetMessageString() : "?");
    return out;
  }
  if (splitter.HasErrors() || !splitter.IsDone()) {
    FC_WARN("profile splitter reported errors");
    return out;
  }

  // split edge -> curve id
  TopTools_DataMapOfShapeInteger edgeIds;
  for (const auto& [e, id] : originals) {
    const TopTools_ListOfShape& mod = splitter.Modified(e);
    if (mod.IsEmpty()) {
      edgeIds.Bind(e, id);
    } else {
      for (TopTools_ListIteratorOfListOfShape it(mod); it.More(); it.Next()) edgeIds.Bind(it.Value(), id);
    }
  }

  auto onBoundary = [&](const gp_Pnt& p) {
    Pt2 q = toLocal(plane, p);
    const double tol = 1e-6 * std::max(1.0, size);
    return std::abs(q.x - umin) < tol || std::abs(q.x - umax) < tol || std::abs(q.y - vmin) < tol || std::abs(q.y - vmax) < tol;
  };

  for (TopExp_Explorer fx(splitter.Shape(), TopAbs_FACE); fx.More(); fx.Next()) {
    TopoDS_Face face = TopoDS::Face(fx.Current());
    bool outer = false;
    std::vector<int> ids;
    for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
      const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
      Standard_Real f, l;
      Handle(Geom_Curve) c = BRep_Tool::Curve(e, f, l);
      if (!c.IsNull() && onBoundary(c->Value(0.5 * (f + l)))) {
        outer = true;
        break;
      }
      if (edgeIds.IsBound(e)) ids.push_back(edgeIds.Find(e));
    }
    if (outer) continue;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.empty()) continue;
    ProfileRegion r;
    r.face = face;
    r.curveIds = ids;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    r.area = props.Mass();
    if (r.area < 1e-9) continue;
    auto seed = interiorPoint(face);
    if (!seed) continue;
    r.seed = toLocal(plane, *seed);
    out.push_back(std::move(r));
  }
  std::sort(out.begin(), out.end(), [](const ProfileRegion& a, const ProfileRegion& b) {
    if (std::abs(a.seed.x - b.seed.x) > 1e-9) return a.seed.x < b.seed.x;
    return a.seed.y < b.seed.y;
  });
  return out;
}

std::optional<Geometry> projectShape(const TopoDS_Shape& s, const gp_Ax3& plane, std::string* warning) {
  if (s.IsNull()) return std::nullopt;
  if (s.ShapeType() == TopAbs_VERTEX) return Geometry::makePoint(toLocal(plane, BRep_Tool::Pnt(TopoDS::Vertex(s))));
  if (s.ShapeType() != TopAbs_EDGE) return std::nullopt;
  const TopoDS_Edge& edge = TopoDS::Edge(s);
  BRepAdaptor_Curve c(edge);
  Pt2 a = toLocal(plane, c.Value(c.FirstParameter()));
  Pt2 b = toLocal(plane, c.Value(c.LastParameter()));
  if (c.GetType() == GeomAbs_Circle) {
    gp_Circ circ = c.Circle();
    double par = std::abs(circ.Axis().Direction().Dot(plane.Direction()));
    if (par > 1 - 1e-6) {
      Pt2 center = toLocal(plane, circ.Location());
      double r = circ.Radius();
      bool closed = BRep_Tool::IsClosed(edge) || a.near(b, 1e-6);
      if (closed) return Geometry::makeCircle(center, r);
      Pt2 mid = toLocal(plane, c.Value(0.5 * (c.FirstParameter() + c.LastParameter())));
      if (auto arc = Geometry::makeArc3Points(a, mid, b)) return arc;
    } else if (par > 1e-6 && warning) {
      *warning = "circle is not parallel to the sketch plane; using its chord";
    }
  } else if (c.GetType() != GeomAbs_Line && warning) {
    *warning = "curve approximated by its chord";
  }
  if (a.near(b, 1e-9)) return Geometry::makePoint(a);
  return Geometry::makeLine(a, b);
}

}  // namespace fabricad::sketch
