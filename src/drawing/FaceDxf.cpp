// FabriCAD drawing: 1:1 DXF of faces and sketches.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/FaceDxf.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>

#include <QSettings>

#include <algorithm>
#include <cmath>
#include <optional>

namespace fabricad::drawing {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct P2 {
  double x = 0, y = 0;
};

/// One edge in output coordinates.
struct Seg {
  enum Kind { Line, Arc, Circle, Spline } kind = Line;
  P2 a, b;               ///< start / end (traversal order)
  P2 c;                  ///< arc / circle centre
  double r = 0;
  double sweep = 0;      ///< arc: signed sweep (radians, + = CCW)
  // spline
  int degree = 3;
  std::vector<std::pair<double, double>> poles;
  std::vector<double> knots, weights;
  std::vector<P2> fit;   ///< sampled points (spline fallback / arc fitting)
};

struct Ctx {
  gp_Trsf toLocal;  ///< world -> plane coordinates (x, y in the plane)
  double ox = 0, oy = 0, scale = 1;
  const FaceDxfOptions* opt = nullptr;
  P2 map(const gp_Pnt& w) const {
    gp_Pnt p = w.Transformed(toLocal);
    return {(p.X() - ox) * scale, (p.Y() - oy) * scale};
  }
};

double cross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
P2 sub(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
double dist(P2 a, P2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::vector<P2> sampleEdge(const TopoDS_Edge& e, const Ctx& ctx, double deflection) {
  std::vector<P2> pts;
  BRepAdaptor_Curve c(e);
  GCPnts_QuasiUniformDeflection d(c, deflection, c.FirstParameter(), c.LastParameter());
  if (d.IsDone() && d.NbPoints() >= 2) {
    for (int i = 1; i <= d.NbPoints(); ++i) pts.push_back(ctx.map(d.Value(i)));
  } else {
    for (int i = 0; i <= 32; ++i) pts.push_back(ctx.map(c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * i / 32.0)));
  }
  if (e.Orientation() == TopAbs_REVERSED) std::reverse(pts.begin(), pts.end());
  return pts;
}

/// Circle through three points; nullopt when they are (nearly) collinear.
std::optional<std::pair<P2, double>> circleThrough(P2 a, P2 b, P2 c) {
  const double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
  if (std::abs(d) < 1e-12) return std::nullopt;
  const double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
  P2 o{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d, (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
  return std::make_pair(o, dist(o, a));
}

/// Greedy arc fit of a polyline: each arc (or line) covers as many points as stay within `tol`.
std::vector<Seg> fitArcs(const std::vector<P2>& pts, double tol) {
  std::vector<Seg> out;
  size_t i = 0;
  while (i + 1 < pts.size()) {
    size_t best = i + 1;
    std::optional<Seg> bestSeg;
    for (size_t j = i + 2; j < pts.size(); ++j) {
      const size_t m = (i + j) / 2;
      auto circ = circleThrough(pts[i], pts[m], pts[j]);
      bool good = true;
      Seg s;
      if (!circ || circ->second > 1e6) {
        // straight: every point within tol of the chord
        s.kind = Seg::Line;
        const P2 d = sub(pts[j], pts[i]);
        const double len = std::hypot(d.x, d.y);
        for (size_t k = i + 1; k < j && good; ++k) good = len > 0 && std::abs(cross(d, sub(pts[k], pts[i]))) / len <= tol;
      } else {
        s.kind = Seg::Arc;
        s.c = circ->first;
        s.r = circ->second;
        for (size_t k = i + 1; k < j && good; ++k) good = std::abs(dist(pts[k], s.c) - s.r) <= tol;
        if (good) {
          // signed sweep through the middle point
          const double a0 = std::atan2(pts[i].y - s.c.y, pts[i].x - s.c.x), a1 = std::atan2(pts[j].y - s.c.y, pts[j].x - s.c.x);
          const bool ccw = cross(sub(pts[m], pts[i]), sub(pts[j], pts[m])) > 0;
          double sw = a1 - a0;
          if (ccw) while (sw <= 0) sw += 2 * kPi;
          else while (sw >= 0) sw -= 2 * kPi;
          s.sweep = sw;
          good = std::abs(sw) < 2 * kPi - 1e-6;
        }
      }
      if (!good) break;
      s.a = pts[i];
      s.b = pts[j];
      best = j;
      bestSeg = s;
    }
    if (!bestSeg) {
      Seg s;
      s.kind = Seg::Line;
      s.a = pts[i];
      s.b = pts[i + 1];
      bestSeg = s;
      best = i + 1;
    }
    out.push_back(*bestSeg);
    i = best;
  }
  return out;
}

void convertEdge(const TopoDS_Edge& e, const Ctx& ctx, std::vector<Seg>& out) {
  BRepAdaptor_Curve c(e);
  const bool rev = e.Orientation() == TopAbs_REVERSED;
  const double f = c.FirstParameter(), l = c.LastParameter();
  const double t0 = rev ? l : f, t1 = rev ? f : l;
  Seg s;
  s.a = ctx.map(c.Value(t0));
  s.b = ctx.map(c.Value(t1));
  if (c.GetType() == GeomAbs_Line) {
    s.kind = Seg::Line;
    out.push_back(s);
    return;
  }
  if (c.GetType() == GeomAbs_Circle) {
    s.c = ctx.map(c.Circle().Location());
    s.r = c.Circle().Radius() * ctx.scale;
    if (std::abs(l - f) >= 2 * kPi - 1e-9) {
      s.kind = Seg::Circle;
      out.push_back(s);
      return;
    }
    // the sweep sign from a point in between (the plane may see the circle clockwise)
    const P2 m = ctx.map(c.Value(0.5 * (f + l)));
    const double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x), a1 = std::atan2(s.b.y - s.c.y, s.b.x - s.c.x);
    const bool ccw = cross(sub(m, s.a), sub(s.b, m)) > 0;
    double sw = a1 - a0;
    if (ccw) while (sw <= 0) sw += 2 * kPi;
    else while (sw >= 0) sw -= 2 * kPi;
    s.kind = Seg::Arc;
    s.sweep = sw;
    out.push_back(s);
    return;
  }
  const double tol = ctx.opt->arcTolerance > 0 ? ctx.opt->arcTolerance : 0.01;
  std::vector<P2> fit = sampleEdge(e, ctx, (ctx.opt->arcTolerance > 0 ? tol / 4 : tol) / ctx.scale);
  if (ctx.opt->arcTolerance > 0 || ctx.opt->version == DxfWriter::Version::R12) {
    for (const Seg& a : fitArcs(fit, tol)) out.push_back(a);
    return;
  }
  // R2000 SPLINE from the exact B-spline (any curve converted)
  TopLoc_Location loc;
  double cf, cl;
  Handle(Geom_Curve) gc = BRep_Tool::Curve(e, loc, cf, cl);
  Handle(Geom_BSplineCurve) bs;
  try {
    if (!gc.IsNull()) bs = GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(gc, cf, cl));
  } catch (const Standard_Failure&) {
    bs.Nullify();
  }
  if (bs.IsNull()) {
    for (const Seg& a : fitArcs(fit, tol)) out.push_back(a);
    return;
  }
  bs = Handle(Geom_BSplineCurve)::DownCast(bs->Copy());
  if (!loc.IsIdentity()) bs->Transform(loc.Transformation());
  if (rev) bs->Reverse();
  s.kind = Seg::Spline;
  s.degree = bs->Degree();
  for (int i = 1; i <= bs->NbPoles(); ++i) {
    const P2 p = ctx.map(bs->Pole(i));
    s.poles.emplace_back(p.x, p.y);
  }
  TColStd_Array1OfReal knots(1, bs->NbPoles() + bs->Degree() + 1);
  bs->KnotSequence(knots);
  for (int i = knots.Lower(); i <= knots.Upper(); ++i) s.knots.push_back(knots(i));
  if (bs->IsRational())
    for (int i = 1; i <= bs->NbPoles(); ++i) s.weights.push_back(bs->Weight(i));
  s.fit = fit;
  out.push_back(s);
}

std::vector<Seg> wireSegments(const TopoDS_Wire& w, const Ctx& ctx) {
  std::vector<Seg> segs;
  for (BRepTools_WireExplorer ex(w); ex.More(); ex.Next()) convertEdge(ex.Current(), ctx, segs);
  if (segs.empty())  // wire explorer needs a connected wire: fall back to the edges as stored
    for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) convertEdge(TopoDS::Edge(ex.Current()), ctx, segs);
  return segs;
}

void emitSegment(DxfWriter& dxf, const std::string& layer, const Seg& s, FaceDxfResult& r) {
  switch (s.kind) {
    case Seg::Line: dxf.line(layer, s.a.x, s.a.y, s.b.x, s.b.y); break;
    case Seg::Circle: dxf.circle(layer, s.c.x, s.c.y, s.r); break;
    case Seg::Arc: {
      const double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x) * 180 / kPi, a1 = std::atan2(s.b.y - s.c.y, s.b.x - s.c.x) * 180 / kPi;
      if (s.sweep > 0) dxf.arc(layer, s.c.x, s.c.y, s.r, a0, a1);
      else dxf.arc(layer, s.c.x, s.c.y, s.r, a1, a0);
      break;
    }
    case Seg::Spline: {
      std::vector<std::pair<double, double>> fit;
      for (const P2& p : s.fit) fit.emplace_back(p.x, p.y);
      dxf.spline(layer, s.degree, s.poles, s.knots, s.weights, false, fit);
      ++r.splines;
      break;
    }
  }
}

/// A closed contour: one polyline with bulges when it has only lines and arcs, else entities.
void emitWire(DxfWriter& dxf, const std::string& layer, const std::vector<Seg>& segs, bool closed, FaceDxfResult& r) {
  if (segs.empty()) return;
  if (segs.size() == 1 && segs.front().kind == Seg::Circle) {
    emitSegment(dxf, layer, segs.front(), r);
    return;
  }
  const bool simple = std::all_of(segs.begin(), segs.end(), [](const Seg& s) { return s.kind == Seg::Line || s.kind == Seg::Arc; });
  if (!simple) {
    for (const Seg& s : segs) emitSegment(dxf, layer, s, r);
    return;
  }
  std::vector<DxfWriter::PolyVertex> v;
  for (const Seg& s : segs) v.push_back({s.a.x, s.a.y, s.kind == Seg::Arc ? std::tan(s.sweep / 4) : 0.0});
  if (!closed) v.push_back({segs.back().b.x, segs.back().b.y, 0});
  dxf.lwpolyline(layer, v, closed);
}

DxfWriter makeWriter(const FaceDxfOptions& o) {
  DxfWriter dxf = DxfWriter::model(o.version);
  dxf.setUnits(o.inches ? DxfWriter::Units::Inches : DxfWriter::Units::Millimeters);
  dxf.addLayer("OUTER", 7);
  dxf.addLayer("INNER", 5);
  dxf.addLayer("ETCH", 3);
  dxf.addLayer("BEND", 4, "CENTER");  // placeholder for sheet metal flat patterns
  return dxf;
}

void boxOf(const TopoDS_Shape& s, const gp_Trsf& toLocal, double& minX, double& minY, double& maxX, double& maxY, bool& any) {
  Ctx c;
  c.toLocal = toLocal;
  for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next())
    for (const P2& p : sampleEdge(TopoDS::Edge(ex.Current()), c, 0.001)) {
      if (!any) {
        minX = maxX = p.x;
        minY = maxY = p.y;
        any = true;
      }
      minX = std::min(minX, p.x);
      minY = std::min(minY, p.y);
      maxX = std::max(maxX, p.x);
      maxY = std::max(maxY, p.y);
    }
}

std::vector<P2> polygonOf(const std::vector<Seg>& segs) {
  std::vector<P2> pts;
  for (const Seg& s : segs) {
    if (s.kind == Seg::Circle) {
      for (int i = 0; i < 48; ++i) pts.push_back({s.c.x + s.r * std::cos(2 * kPi * i / 48), s.c.y + s.r * std::sin(2 * kPi * i / 48)});
    } else if (s.kind == Seg::Arc) {
      const double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x);
      for (int i = 0; i < 16; ++i) pts.push_back({s.c.x + s.r * std::cos(a0 + s.sweep * i / 16), s.c.y + s.r * std::sin(a0 + s.sweep * i / 16)});
    } else if (s.kind == Seg::Spline) {
      pts.insert(pts.end(), s.fit.begin(), s.fit.end());
    } else {
      pts.push_back(s.a);
    }
  }
  return pts;
}

bool inside(const std::vector<P2>& poly, P2 p) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    if ((poly[i].y > p.y) != (poly[j].y > p.y) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
      in = !in;
  return in;
}

}  // namespace

FaceDxfResult faceDxf(const std::vector<TopoDS_Face>& faces, const FaceDxfOptions& o) {
  FaceDxfResult r;
  if (faces.empty()) {
    r.error = "select a planar face (or several coplanar faces) or a sketch";
    return r;
  }
  try {
    // the plane of the first face, normal outwards (seen from outside)
    BRepAdaptor_Surface s0(faces.front(), Standard_False);
    if (s0.GetType() != GeomAbs_Plane) {
      r.error = "the face is not planar";
      return r;
    }
    const gp_Ax3 pos = s0.Plane().Position();
    gp_Dir n = pos.XDirection().Crossed(pos.YDirection());
    if (faces.front().Orientation() == TopAbs_REVERSED) n.Reverse();
    const gp_Ax3 frame(pos.Location(), n, pos.XDirection());
    const gp_Pln pln(frame);
    for (const TopoDS_Face& f : faces) {
      BRepAdaptor_Surface s(f, Standard_False);
      if (s.GetType() != GeomAbs_Plane) {
        r.error = "a selected face is not planar";
        return r;
      }
      const gp_Ax3 p = s.Plane().Position();
      gp_Dir fn = p.XDirection().Crossed(p.YDirection());
      if (f.Orientation() == TopAbs_REVERSED) fn.Reverse();
      if (!fn.IsParallel(n, 1e-6) || pln.Distance(p.Location()) > 1e-4) {
        r.error = "the selected faces are not coplanar";
        return r;
      }
    }
    Ctx ctx;
    ctx.opt = &o;
    ctx.toLocal.SetTransformation(frame);
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
    for (const TopoDS_Face& f : faces) boxOf(f, ctx.toLocal, minX, minY, maxX, maxY, any);
    ctx.ox = minX;
    ctx.oy = minY;
    ctx.scale = o.inches ? 1.0 / 25.4 : 1.0;
    r.width = (maxX - minX) * ctx.scale;
    r.height = (maxY - minY) * ctx.scale;
    DxfWriter dxf = makeWriter(o);
    for (const TopoDS_Face& f : faces) {
      const TopoDS_Wire outer = BRepTools::OuterWire(f);
      for (TopExp_Explorer ex(f, TopAbs_WIRE); ex.More(); ex.Next()) {
        const TopoDS_Wire& w = TopoDS::Wire(ex.Current());
        const bool isOuter = w.IsSame(outer);
        emitWire(dxf, isOuter ? "OUTER" : "INNER", wireSegments(w, ctx), true, r);
        ++(isOuter ? r.outer : r.inner);
      }
    }
    r.dxf = dxf.finish();
    r.ok = true;
  } catch (const Standard_Failure& e) {
    r.error = std::string("DXF export failed: ") + (e.GetMessageString() ? e.GetMessageString() : "OCCT error");
  }
  return r;
}

FaceDxfResult sketchDxf(const gp_Ax3& plane, const TopoDS_Shape& edges, const TopoDS_Shape& construction, const FaceDxfOptions& o) {
  FaceDxfResult r;
  try {
    Ctx ctx;
    ctx.opt = &o;
    ctx.toLocal.SetTransformation(plane);
    ctx.scale = o.inches ? 1.0 / 25.4 : 1.0;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
    if (!edges.IsNull()) boxOf(edges, ctx.toLocal, minX, minY, maxX, maxY, any);
    if (!any) {
      r.error = "the sketch has no curves";
      return r;
    }
    r.width = (maxX - minX) * ctx.scale;
    r.height = (maxY - minY) * ctx.scale;
    DxfWriter dxf = makeWriter(o);
    Handle(TopTools_HSequenceOfShape) in = new TopTools_HSequenceOfShape, wires;
    for (TopExp_Explorer ex(edges, TopAbs_EDGE); ex.More(); ex.Next()) in->Append(ex.Current());
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(in, 1e-6, Standard_False, wires);
    struct Loop {
      std::vector<Seg> segs;
      bool closed;
      std::vector<P2> poly;
    };
    std::vector<Loop> loops;
    for (int i = 1; i <= wires->Length(); ++i) {
      const TopoDS_Wire w = TopoDS::Wire(wires->Value(i));
      TopoDS_Vertex v1, v2;
      TopExp::Vertices(w, v1, v2);
      const bool closed = BRep_Tool::IsClosed(w) || (!v1.IsNull() && !v2.IsNull() && BRep_Tool::Pnt(v1).Distance(BRep_Tool::Pnt(v2)) < 1e-6);
      Loop l{wireSegments(w, ctx), closed, {}};
      if (closed) l.poly = polygonOf(l.segs);
      loops.push_back(std::move(l));
    }
    for (size_t i = 0; i < loops.size(); ++i) {
      int depth = 0;
      if (loops[i].closed && !loops[i].poly.empty())
        for (size_t j = 0; j < loops.size(); ++j)
          if (j != i && loops[j].closed && loops[j].poly.size() > 2 && inside(loops[j].poly, loops[i].poly.front())) ++depth;
      const bool inner = depth % 2 == 1;
      emitWire(dxf, inner ? "INNER" : "OUTER", loops[i].segs, loops[i].closed, r);
      ++(inner ? r.inner : r.outer);
    }
    if (o.etch && !construction.IsNull()) {
      for (TopExp_Explorer ex(construction, TopAbs_EDGE); ex.More(); ex.Next()) {
        std::vector<Seg> segs;
        convertEdge(TopoDS::Edge(ex.Current()), ctx, segs);
        for (const Seg& s : segs) emitSegment(dxf, "ETCH", s, r);
        ++r.etch;
      }
    }
    r.dxf = dxf.finish();
    r.ok = true;
  } catch (const Standard_Failure& e) {
    r.error = std::string("DXF export failed: ") + (e.GetMessageString() ? e.GetMessageString() : "OCCT error");
  }
  return r;
}

bool applyFaceDxfOption(const std::string& word, FaceDxfOptions& o) {
  std::string w = word;
  for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (w == "r12" || w == "version=r12" || w == "ac1009") {
    o.version = DxfWriter::Version::R12;
    return true;
  }
  if (w == "r2000" || w == "version=r2000" || w == "ac1015") {
    o.version = DxfWriter::Version::R2000;
    return true;
  }
  if (w == "in" || w == "inch" || w == "inches" || w == "units=in" || w == "units=inch" || w == "units=inches") {
    o.inches = true;
    return true;
  }
  if (w == "mm" || w == "units=mm") {
    o.inches = false;
    return true;
  }
  if (w == "noetch" || w == "etch=off") {
    o.etch = false;
    return true;
  }
  if (w == "etch" || w == "etch=on") {
    o.etch = true;
    return true;
  }
  if (w.starts_with("arcs=") || w.starts_with("arctol=")) {
    const std::string v = w.substr(w.find('=') + 1);
    char* end = nullptr;
    const double t = std::strtod(v.c_str(), &end);
    o.arcTolerance = (end && *end == '\0' && t > 0) ? t : (v == "off" ? 0.0 : 0.01);
    return true;
  }
  if (w == "arcs") {
    o.arcTolerance = 0.01;
    return true;
  }
  return false;
}

DxfWriter::Version faceDxfVersionDefault() {
  return QSettings().value("export/dxf/version", "R2000").toString().toUpper() == "R12" ? DxfWriter::Version::R12 : DxfWriter::Version::R2000;
}

void setFaceDxfVersionDefault(DxfWriter::Version v) {
  QSettings().setValue("export/dxf/version", v == DxfWriter::Version::R12 ? "R12" : "R2000");
}

}  // namespace fabricad::drawing
