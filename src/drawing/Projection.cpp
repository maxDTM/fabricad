// FabriCAD drawing: HLR projection to 2D curves and view-frame math.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Projection.h"

#include "core/Json.h"
#include "core/Log.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRBRep_PolyAlgo.hxx>
#include <HLRBRep_PolyHLRToShape.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace fabricad::drawing {

// ---------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------

namespace {
gp_Ax2 makeFrame(const gp_Dir& z, const gp_Dir& up) {
  // X = up x Z so that X x Y = Z with Y = up.
  gp_Vec x = gp_Vec(up).Crossed(gp_Vec(z));
  return gp_Ax2(gp_Pnt(0, 0, 0), z, gp_Dir(x));
}

gp_Ax2 isoFrame(double sx, double sy, double sz) {
  gp_Dir z(sx, sy, sz);
  gp_Vec worldUp(0, 0, 1);
  gp_Vec zv(z);
  gp_Vec up = worldUp - zv * worldUp.Dot(zv);
  return makeFrame(z, gp_Dir(up));
}
}  // namespace

gp_Ax2 frameForOrientation(ViewOrientation o) {
  switch (o) {
    case ViewOrientation::Front: return makeFrame(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1));
    case ViewOrientation::Back: return makeFrame(gp_Dir(0, 1, 0), gp_Dir(0, 0, 1));
    case ViewOrientation::Top: return makeFrame(gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
    case ViewOrientation::Bottom: return makeFrame(gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
    case ViewOrientation::Right: return makeFrame(gp_Dir(1, 0, 0), gp_Dir(0, 0, 1));
    case ViewOrientation::Left: return makeFrame(gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1));
    case ViewOrientation::IsoTopRight: return isoFrame(1, -1, 1);
    case ViewOrientation::IsoTopLeft: return isoFrame(-1, -1, 1);
    case ViewOrientation::IsoBottomRight: return isoFrame(1, -1, -1);
    case ViewOrientation::IsoBottomLeft: return isoFrame(-1, -1, -1);
    case ViewOrientation::Custom: break;
  }
  return makeFrame(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1));
}

gp_Ax2 projectedFrame(const gp_Ax2& parent, ProjectionDirection dir, ProjectionStandard standard) {
  const gp_Vec X(parent.XDirection()), Y(parent.YDirection()), Z(parent.Direction());
  // Third angle: the view placed on the right shows what is seen from the right (Z_new = +X).
  // First angle: the opposite side.
  const double s = standard == ProjectionStandard::ThirdAngle ? 1.0 : -1.0;
  if (isDiagonal(dir)) {
    double sx = (dir == ProjectionDirection::AboveRight || dir == ProjectionDirection::BelowRight) ? 1.0 : -1.0;
    double sy = (dir == ProjectionDirection::AboveRight || dir == ProjectionDirection::AboveLeft) ? 1.0 : -1.0;
    gp_Vec z = X * (s * sx) + Y * (s * sy) + Z;
    z.Normalize();
    gp_Vec up = Y - z * Y.Dot(z);
    if (up.Magnitude() < 1e-9) up = Z;
    gp_Vec x = up.Crossed(z);
    return gp_Ax2(parent.Location(), gp_Dir(z), gp_Dir(x));
  }
  switch (dir) {
    case ProjectionDirection::Right: {
      gp_Vec z = X * s;
      return gp_Ax2(parent.Location(), gp_Dir(z), gp_Dir(Y.Crossed(z)));
    }
    case ProjectionDirection::Left: {
      gp_Vec z = X * (-s);
      return gp_Ax2(parent.Location(), gp_Dir(z), gp_Dir(Y.Crossed(z)));
    }
    case ProjectionDirection::Above: {
      gp_Vec z = Y * s;
      return gp_Ax2(parent.Location(), gp_Dir(z), gp_Dir(X));  // Y_new = z x X
    }
    case ProjectionDirection::Below: {
      gp_Vec z = Y * (-s);
      return gp_Ax2(parent.Location(), gp_Dir(z), gp_Dir(X));
    }
    default: break;
  }
  return parent;
}

gp_Pnt viewToWorld(const gp_Ax2& frame, const Point2& p) {
  gp_Vec v = gp_Vec(frame.XDirection()) * p.x + gp_Vec(frame.YDirection()) * p.y;
  return frame.Location().Translated(v);
}

Point2 worldToView(const gp_Ax2& frame, const gp_Pnt& p) {
  gp_Vec d(frame.Location(), p);
  return {d.Dot(gp_Vec(frame.XDirection())), d.Dot(gp_Vec(frame.YDirection()))};
}

gp_Ax2 sectionFrame(const gp_Ax2& parent, const Point2& p1, const Point2& p2, const Point2& arrow) {
  const gp_Vec X(parent.XDirection()), Y(parent.YDirection());
  Point2 line = (p2 - p1).normalized();
  Point2 a = arrow.normalized();
  if (a.length() < 0.5) a = line.perp();
  // Observer looks along `arrow`; the new viewer direction (towards the viewer) is -arrow.
  gp_Vec z = -(X * a.x + Y * a.y);
  z.Normalize();
  gp_Vec x, y;
  if (std::abs(line.x) >= std::abs(line.y)) {
    // mostly horizontal cut line -> keep the parent's X, like a top/bottom view
    double sgn = line.x >= 0 ? 1.0 : -1.0;
    x = (X * line.x + Y * line.y) * sgn;
    y = z.Crossed(x);
  } else {
    // mostly vertical cut line -> keep the parent's up, like a side view
    double sgn = line.y >= 0 ? 1.0 : -1.0;
    y = (X * line.x + Y * line.y) * sgn;
    x = y.Crossed(z);
  }
  gp_Pnt origin = viewToWorld(parent, (p1 + p2) * 0.5);
  return gp_Ax2(origin, gp_Dir(z), gp_Dir(x));
}

QJsonObject frameToJson(const gp_Ax2& f) { return core::ax3ToJson(gp_Ax3(f)); }

gp_Ax2 frameFromJson(const QJsonObject& o, const gp_Ax2& fallback) {
  if (o.isEmpty()) return fallback;
  gp_Ax3 a = core::ax3FromJson(o);
  return gp_Ax2(a.Location(), a.Direction(), a.XDirection());
}

// ---------------------------------------------------------------------------------------
// Edge conversion
// ---------------------------------------------------------------------------------------

namespace {
Point2 to2(const gp_Pnt& p) { return {p.X(), p.Y()}; }

std::vector<Point2> discretize(const BRepAdaptor_Curve& adapt, double deflection) {
  std::vector<Point2> pts;
  try {
    GCPnts_TangentialDeflection disc(adapt, adapt.FirstParameter(), adapt.LastParameter(), 0.1, deflection, 2);
    if (disc.NbPoints() >= 2) {
      pts.reserve(disc.NbPoints());
      for (int i = 1; i <= disc.NbPoints(); ++i) pts.push_back(to2(disc.Value(i)));
      return pts;
    }
  } catch (const Standard_Failure&) {
  }
  // fallback: uniform parameter sampling
  const int n = 24;
  double f = adapt.FirstParameter(), l = adapt.LastParameter();
  for (int i = 0; i <= n; ++i) pts.push_back(to2(adapt.Value(f + (l - f) * i / n)));
  return pts;
}
}  // namespace

bool edgeToCurve(const TopoDS_Shape& shape, LineClass cls, double deflection, Curve2D& out) {
  if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) return false;
  const TopoDS_Edge& edge = TopoDS::Edge(shape);
  BRepAdaptor_Curve adapt(edge);
  const double f = adapt.FirstParameter(), l = adapt.LastParameter();
  if (!(l > f) || !std::isfinite(f) || !std::isfinite(l)) return false;
  gp_Pnt ps = adapt.Value(f), pe = adapt.Value(l);
  if (deflection <= 0) deflection = 0.01;

  switch (adapt.GetType()) {
    case GeomAbs_Line: {
      if (ps.Distance(pe) < 1e-7) return false;
      out = Curve2D::segment(to2(ps), to2(pe), cls);
      return true;
    }
    case GeomAbs_Circle: {
      gp_Circ circ = adapt.Circle();
      double r = circ.Radius();
      if (r < 1e-7 || r > 1e6) return false;
      Point2 c = to2(circ.Location());
      Point2 s2 = to2(ps), m2 = to2(adapt.Value(0.5 * (f + l)));
      double sweepDeg = rad2deg(l - f);
      if (sweepDeg >= 360.0 - 1e-6 || (sweepDeg > 300.0 && ps.Distance(pe) < 1e-6)) {
        out = Curve2D::circle(c, r, cls);
        return true;
      }
      double startDeg = (s2 - c).angleDeg();
      bool ccw = (s2 - c).cross(m2 - c) > 0;
      out = Curve2D::arc(c, r, startDeg, ccw ? sweepDeg : -sweepDeg, cls);
      return true;
    }
    default: {
      std::vector<Point2> pts = discretize(adapt, deflection);
      // remove duplicate consecutive points
      std::vector<Point2> clean;
      for (const auto& p : pts)
        if (clean.empty() || clean.back().distance(p) > 1e-9) clean.push_back(p);
      if (clean.size() < 2) return false;
      double len = 0;
      for (size_t i = 0; i + 1 < clean.size(); ++i) len += clean[i].distance(clean[i + 1]);
      if (len < 1e-6 || len > 1e7) return false;
      if (clean.size() == 2) {
        out = Curve2D::segment(clean[0], clean[1], cls);
        return true;
      }
      out = Curve2D::polyline(std::move(clean), cls);
      return true;
    }
  }
}

std::vector<Vertex2D> collectVertices(const std::vector<Curve2D>& curves, double tol) {
  std::vector<Vertex2D> out;
  auto add = [&](const Point2& p, const Curve2D& c, int role) {
    for (const auto& v : out)
      if (v.p.distance(p) <= tol) return;
    out.push_back(Vertex2D::of(c, p, role));
  };
  for (const auto& c : curves) {
    if (c.cls == LineClass::Hatch) continue;
    if (c.type == CurveType::Arc) {
      add(c.center, c, 2);
      if (c.isCircle()) continue;
    }
    add(c.start(), c, 0);
    add(c.end(), c, 1);
  }
  return out;
}

bool sourcesCenter(const std::vector<SourceShape>& sources, gp_Pnt& center, double* diagonal) {
  Bnd_Box box;
  for (const auto& s : sources)
    if (!s.shape.IsNull()) BRepBndLib::Add(s.shape, box, false);
  if (box.IsVoid()) return false;
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  center = gp_Pnt(0.5 * (xmin + xmax), 0.5 * (ymin + ymax), 0.5 * (zmin + zmax));
  if (diagonal) *diagonal = gp_Pnt(xmin, ymin, zmin).Distance(gp_Pnt(xmax, ymax, zmax));
  return true;
}

// ---------------------------------------------------------------------------------------
// Source-edge association (recovers the 3D edge behind each 2D curve)
// ---------------------------------------------------------------------------------------

namespace {
struct Candidate {
  core::ObjectId body;
  int edge;
  std::vector<Point2> poly;
  BBox2 box;
  const SourceEdgeId* ident = nullptr;
};

std::vector<Candidate> buildCandidates(const std::vector<SourceShape>& sources, const gp_Ax2& frame) {
  std::vector<Candidate> out;
  for (const auto& s : sources) {
    if (s.shape.IsNull()) continue;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(s.shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
      const TopoDS_Edge& e = TopoDS::Edge(edges(i));
      BRepAdaptor_Curve adapt(e);
      double f = adapt.FirstParameter(), l = adapt.LastParameter();
      if (!(l > f)) continue;
      int n = adapt.GetType() == GeomAbs_Line ? 1 : 24;
      Candidate c{s.bodyId, i, {}, {}, nullptr};
      if (static_cast<size_t>(i) <= s.edges.size()) c.ident = &s.edges[static_cast<size_t>(i) - 1];
      c.poly.reserve(n + 1);
      for (int k = 0; k <= n; ++k) {
        Point2 p = worldToView(frame, adapt.Value(f + (l - f) * k / n));
        c.poly.push_back(p);
        c.box.add(p);
      }
      out.push_back(std::move(c));
    }
  }
  return out;
}

double distToPoly(const std::vector<Point2>& poly, const Point2& p) {
  if (poly.size() == 1) return poly[0].distance(p);
  double best = 1e300;
  for (size_t i = 0; i + 1 < poly.size(); ++i) best = std::min(best, distanceToSegment(poly[i], poly[i + 1], p));
  return best;
}

void associate(std::vector<Curve2D>& curves, const std::vector<Candidate>& cands, double tol) {
  for (auto& c : curves) {
    if (c.cls == LineClass::Hatch) continue;
    const Point2 samples[3] = {c.pointAt(0.25), c.pointAt(0.5), c.pointAt(0.75)};
    double best = 1e300;
    const Candidate* bestC = nullptr;
    for (const auto& cand : cands) {
      if (!cand.box.contains(samples[1], tol)) continue;
      double worst = 0;
      for (const auto& s : samples) {
        if (!cand.box.contains(s, tol)) { worst = 1e300; break; }
        worst = std::max(worst, distToPoly(cand.poly, s));
        if (worst > tol) break;
      }
      if (worst < best) {
        best = worst;
        bestC = &cand;
      }
    }
    if (bestC && best <= tol) {
      c.sourceBody = bestC->body;
      c.sourceEdge = bestC->edge;
      if (bestC->ident) {
        c.sourceName = bestC->ident->name;
        c.sourceSig = bestC->ident->signature;
      }
    }
  }
}
}  // namespace

// ---------------------------------------------------------------------------------------
// HLR
// ---------------------------------------------------------------------------------------

namespace {
void appendCompound(const TopoDS_Shape& compound, LineClass cls, double deflection, std::vector<Curve2D>& out,
                    int& count) {
  if (compound.IsNull()) return;
  TopoDS_Shape c = compound;
  try {
    BRepLib::BuildCurves3d(c);
  } catch (const Standard_Failure&) {
  }
  for (TopExp_Explorer ex(c, TopAbs_EDGE); ex.More(); ex.Next()) {
    Curve2D curve;
    if (edgeToCurve(ex.Current(), cls, deflection, curve)) {
      out.push_back(std::move(curve));
      ++count;
    }
  }
}
}  // namespace

namespace {
struct Interval {
  double a, b;
};

/// [0,1] minus the union of `cov` (each clamped to [0,1]); pieces shorter than minLen dropped.
std::vector<Interval> subtract(std::vector<Interval> cov, double minLen) {
  std::vector<Interval> out;
  for (auto& c : cov) {
    if (c.a > c.b) std::swap(c.a, c.b);
    c.a = std::clamp(c.a, 0.0, 1.0);
    c.b = std::clamp(c.b, 0.0, 1.0);
  }
  std::sort(cov.begin(), cov.end(), [](const Interval& x, const Interval& y) { return x.a < y.a; });
  double cur = 0.0;
  for (const auto& c : cov) {
    if (c.b <= c.a) continue;
    if (c.a > cur + minLen) out.push_back({cur, c.a});
    cur = std::max(cur, c.b);
  }
  if (1.0 > cur + minLen) out.push_back({cur, 1.0});
  return out;
}

bool nearArc(const Curve2D& v, const Point2& p, const Point2& tangent, double tol) {
  Point2 r = p - v.center;
  double d = r.length();
  if (std::abs(d - v.radius) > tol || d < 1e-12) return false;
  if (!v.isCircle()) {
    // allow tol of slack at the arc ends
    double slack = rad2deg(tol / std::max(v.radius, 1e-9));
    double ang = r.angleDeg();
    if (!angleInArc(ang, v.startAngle - (v.sweep >= 0 ? slack : -slack), v.sweep + (v.sweep >= 0 ? 2 * slack : -2 * slack)))
      return false;
  }
  Point2 tv = r.perp() / d;
  return std::abs(tv.cross(tangent)) < 0.02;
}

bool nearSegment(const Point2& a, const Point2& b, const Point2& p, const Point2& tangent, double tol) {
  Point2 d = b - a;
  double len = d.length();
  if (len < 1e-12) return false;
  Point2 u = d / len;
  if (std::abs(u.cross(tangent)) > 0.02) return false;
  double t = (p - a).dot(u);
  if (t < -tol || t > len + tol) return false;
  return std::abs((p - a).cross(u)) <= tol;
}

/// Parameter intervals of the hidden curve `h` covered by `v` (tangentially, within tol).
void coveredBy(const Curve2D& h, const Curve2D& v, double tol, std::vector<Interval>& out) {
  if (h.type == CurveType::Segment && v.type == CurveType::Segment) {
    // exact: collinear overlap
    Point2 d = h.b - h.a;
    double len2 = d.dot(d);
    if (len2 < 1e-18) return;
    Point2 u = d / std::sqrt(len2);
    Point2 vd = (v.b - v.a).normalized();
    if (std::abs(u.cross(vd)) > 1e-3) return;
    if (std::abs((v.a - h.a).cross(u)) > tol || std::abs((v.b - h.a).cross(u)) > tol) return;
    double ta = (v.a - h.a).dot(d) / len2, tb = (v.b - h.a).dot(d) / len2;
    out.push_back({std::min(ta, tb), std::max(ta, tb)});
    return;
  }
  // sampled: mid-points of small parameter steps
  int n = 64;
  if (h.type == CurveType::Arc) n = std::clamp(static_cast<int>(std::abs(h.sweep)), 16, 720);
  else n = std::clamp(static_cast<int>(h.length() / std::max(tol * 4, 1e-3)), 16, 800);
  double runStart = -1;
  for (int i = 0; i < n; ++i) {
    double t0 = double(i) / n, t1 = double(i + 1) / n, tm = 0.5 * (t0 + t1);
    Point2 p = h.pointAt(tm);
    Point2 tan = h.direction(tm);
    bool cov = false;
    if (v.type == CurveType::Arc) cov = nearArc(v, p, tan, tol);
    else if (v.type == CurveType::Segment) cov = nearSegment(v.a, v.b, p, tan, tol);
    else
      for (size_t k = 0; k + 1 < v.pts.size() && !cov; ++k) cov = nearSegment(v.pts[k], v.pts[k + 1], p, tan, tol * 2);
    if (cov && runStart < 0) runStart = t0;
    if (!cov && runStart >= 0) {
      out.push_back({runStart, t0});
      runStart = -1;
    }
  }
  if (runStart >= 0) out.push_back({runStart, 1.0});
}

Curve2D subCurve(const Curve2D& h, double ta, double tb) {
  Curve2D c;
  switch (h.type) {
    case CurveType::Segment: c = Curve2D::segment(h.pointAt(ta), h.pointAt(tb), h.cls); break;
    case CurveType::Arc: c = Curve2D::arc(h.center, h.radius, h.startAngle + h.sweep * ta, h.sweep * (tb - ta), h.cls); break;
    case CurveType::Polyline: {
      std::vector<Point2> pts;
      const int n = std::max(2, static_cast<int>(std::ceil((tb - ta) * std::max<size_t>(h.pts.size(), 8) * 2)));
      for (int i = 0; i <= n; ++i) pts.push_back(h.pointAt(ta + (tb - ta) * i / n));
      c = Curve2D::polyline(std::move(pts), h.cls);
      break;
    }
  }
  c.sourceBody = h.sourceBody;
  c.sourceEdge = h.sourceEdge;
  c.sourceName = h.sourceName;
  c.sourceSig = h.sourceSig;
  return c;
}
}  // namespace

int removeHiddenUnderVisible(std::vector<Curve2D>& curves, double tol, std::vector<Curve2D>* removed) {
  std::vector<size_t> visible;
  for (size_t i = 0; i < curves.size(); ++i)
    if (curves[i].cls == LineClass::Visible) visible.push_back(i);
  if (visible.empty()) return 0;
  std::vector<BBox2> vbox;
  for (size_t i : visible) vbox.push_back(curves[i].bbox().expanded(tol));
  std::vector<Curve2D> out;
  out.reserve(curves.size());
  int changed = 0;
  for (const Curve2D& c : curves) {
    if (c.cls != LineClass::Hidden) {
      out.push_back(c);
      continue;
    }
    const BBox2 hb = c.bbox().expanded(tol);
    std::vector<Interval> cov;
    for (size_t k = 0; k < visible.size(); ++k) {
      const BBox2& vb = vbox[k];
      if (vb.max.x < hb.min.x || hb.max.x < vb.min.x || vb.max.y < hb.min.y || hb.max.y < vb.min.y) continue;
      coveredBy(c, curves[visible[k]], tol, cov);
    }
    if (cov.empty()) {
      out.push_back(c);
      continue;
    }
    const double len = std::max(c.length(), 1e-9);
    std::vector<Interval> keep = subtract(cov, tol * 2 / len);
    ++changed;
    if (removed) removed->push_back(c);
    for (const auto& k : keep) out.push_back(subCurve(c, k.a, k.b));
  }
  curves = std::move(out);
  return changed;
}

ProjectionOutput projectShapes(const ProjectionInput& in) {
  ProjectionOutput out;
  const auto t0 = std::chrono::steady_clock::now();

  std::vector<SourceShape> sources;
  for (const auto& s : in.sources)
    if (!s.shape.IsNull()) sources.push_back(s);
  if (sources.empty()) {
    out.error = "nothing to project";
    out.frame = in.frame;
    return out;
  }
  gp_Pnt center;
  double diagonal = 1.0;
  if (!sourcesCenter(sources, center, &diagonal)) {
    out.error = "empty geometry";
    out.frame = in.frame;
    return out;
  }
  if (in.useFrameOrigin) center = in.frame.Location();
  gp_Ax2 frame(center, in.frame.Direction(), in.frame.XDirection());
  out.frame = frame;
  out.origin = core::Vec3::from(center);
  double deflection = in.deflection > 0 ? in.deflection : std::clamp(diagonal * 5e-4, 0.005, 0.2);

  TopoDS_Compound compound;
  BRep_Builder builder;
  builder.MakeCompound(compound);
  for (const auto& s : sources) builder.Add(compound, s.shape);

  try {
    HLRAlgo_Projector projector(frame);
    if (in.exact) {
      Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
      algo->Add(compound, 0);
      algo->Projector(projector);
      algo->Update();
      algo->Hide();
      HLRBRep_HLRToShape hlr(algo);
      appendCompound(hlr.VCompound(), LineClass::Visible, deflection, out.curves, out.visibleCount);
      appendCompound(hlr.OutLineVCompound(), LineClass::Visible, deflection, out.curves, out.visibleCount);
      if (in.tangentEdges)
        appendCompound(hlr.Rg1LineVCompound(), LineClass::Tangent, deflection, out.curves, out.tangentCount);
      if (in.hiddenLines) {
        appendCompound(hlr.HCompound(), LineClass::Hidden, deflection, out.curves, out.hiddenCount);
        appendCompound(hlr.OutLineHCompound(), LineClass::Hidden, deflection, out.curves, out.hiddenCount);
      }
    } else {
      // Poly algorithm needs a meshed copy (meshing mutates the shape; never touch the original).
      BRepBuilderAPI_Copy copier(compound);
      TopoDS_Shape meshed = copier.Shape();
      BRepMesh_IncrementalMesh mesher(meshed, std::max(0.01, diagonal * 2e-3), false, 0.3, true);
      Handle(HLRBRep_PolyAlgo) algo = new HLRBRep_PolyAlgo();
      algo->Load(meshed);
      algo->Projector(projector);
      algo->Update();
      HLRBRep_PolyHLRToShape hlr;
      hlr.Update(algo);
      appendCompound(hlr.VCompound(), LineClass::Visible, deflection, out.curves, out.visibleCount);
      appendCompound(hlr.OutLineVCompound(), LineClass::Visible, deflection, out.curves, out.visibleCount);
      if (in.tangentEdges)
        appendCompound(hlr.Rg1LineVCompound(), LineClass::Tangent, deflection, out.curves, out.tangentCount);
      if (in.hiddenLines) {
        appendCompound(hlr.HCompound(), LineClass::Hidden, deflection, out.curves, out.hiddenCount);
        appendCompound(hlr.OutLineHCompound(), LineClass::Hidden, deflection, out.curves, out.hiddenCount);
      }
    }
  } catch (const Standard_Failure& e) {
    out.error = std::string("HLR failed: ") + (e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return out;
  } catch (const std::exception& e) {
    out.error = std::string("HLR failed: ") + e.what();
    return out;
  }

  // Associate with 3D edges for associative dimensions.
  if (in.exact) {
    std::vector<Candidate> cands = buildCandidates(sources, frame);
    associate(out.curves, cands, std::max(0.05, diagonal * 1e-3));
  }
  // hidden edges lying under visible ones (DRW-ASM-OUTPUT-4); after the association so the
  // dropped ones keep their identity for dimensions that reference them
  if (in.hiddenLines) {
    const int before = static_cast<int>(out.curves.size());
    removeHiddenUnderVisible(out.curves, std::max(1e-4, diagonal * 2e-6), &out.covered);
    out.hiddenCount += static_cast<int>(out.curves.size()) - before;
  }
  for (const auto& c : in.extraCurves) out.curves.push_back(c);

  for (const auto& c : out.curves) out.bbox.add(c.bbox());
  out.ok = true;
  out.millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return out;
}

}  // namespace fabricad::drawing
