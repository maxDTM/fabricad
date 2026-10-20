// FabriCAD drawing: hole recognition for hole / thread callouts. The hole is read from the
// B-rep around the picked circular edge (coaxial internal cylinders and cones, bottom discs),
// so callouts work for every way a hole was made; a part Hole feature that produced the edge
// adds its instance count and thread data.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Callouts.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "drawing/Tolerance.h"
#include "part/Naming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <set>

namespace fabricad::drawing {

namespace {

struct RevFace {
  bool cone = false;
  double t0 = 0, t1 = 0;   ///< axial range
  double r0 = 0, r1 = 0;   ///< radius at t0 / t1
  double semiAngle = 0;    ///< cones, degrees
  bool internal = false;
  double rMin() const { return std::min(r0, r1); }
  double rMax() const { return std::max(r0, r1); }
};

double axialParam(const gp_Ax1& ax, const gp_Pnt& p) {
  return gp_Vec(ax.Location(), p).Dot(gp_Vec(ax.Direction()));
}

/// Axial extent and end radii of a face from samples of its edges.
bool faceExtent(const TopoDS_Face& f, const gp_Ax1& ax, RevFace& rf) {
  gp_Lin line(ax);
  double tmin = 1e300, tmax = -1e300, rAtMin = 0, rAtMax = 0;
  for (TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    double a = c.FirstParameter(), b = c.LastParameter();
    if (!(b > a) || std::isinf(a) || std::isinf(b)) continue;
    for (int i = 0; i <= 8; ++i) {
      gp_Pnt p = c.Value(a + (b - a) * i / 8.0);
      double t = axialParam(ax, p), r = line.Distance(p);
      if (t < tmin - 1e-9 || (std::abs(t - tmin) <= 1e-9 && r > rAtMin)) {
        tmin = t;
        rAtMin = r;
      }
      if (t > tmax + 1e-9 || (std::abs(t - tmax) <= 1e-9 && r > rAtMax)) {
        tmax = t;
        rAtMax = r;
      }
    }
  }
  if (tmin > tmax) return false;
  rf.t0 = tmin;
  rf.t1 = tmax;
  rf.r0 = rAtMin;
  rf.r1 = rAtMax;
  return true;
}

bool isInternal(const TopoDS_Face& f, const gp_Ax1& ax) {
  BRepAdaptor_Surface s(f);
  double u = 0.5 * (s.FirstUParameter() + s.LastUParameter());
  double v = 0.5 * (s.FirstVParameter() + s.LastVParameter());
  gp_Pnt p;
  gp_Vec du, dv;
  s.D1(u, v, p, du, dv);
  gp_Vec n = du.Crossed(dv);
  if (n.Magnitude() < 1e-12) return false;
  if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
  // inward radial direction
  gp_Pnt onAxis = ax.Location().Translated(gp_Vec(ax.Direction()) * axialParam(ax, p));
  gp_Vec in(p, onAxis);
  if (in.Magnitude() < 1e-12) return false;
  return n.Dot(in) > 0;
}

/// Hole feature (part::HoleFeature, type "Hole") that named this edge, if any.
const core::Feature* holeFeatureFor(const core::Document& doc, const std::string& edgeName) {
  if (edgeName.empty()) return nullptr;
  for (const auto& f : doc.features()) {
    if (f->type() != "Hole") continue;
    const std::string key = f->name() + "/H";
    size_t pos = edgeName.find(key);
    // the name must start a component ("{Hole1/H2/F1|...}")
    while (pos != std::string::npos) {
      if (pos == 0 || edgeName[pos - 1] == '{' || edgeName[pos - 1] == '|') return f.get();
      pos = edgeName.find(key, pos + 1);
    }
  }
  return nullptr;
}

std::string stringProp(const core::Feature& f, std::initializer_list<const char*> names) {
  for (const char* n : names)
    if (const core::Property* p = f.property(n)) {
      if (p->type() == core::PropertyType::String && !p->toString().empty()) return p->toString();
      if (p->type() == core::PropertyType::Enum) {
        const auto& opts = p->options();
        int i = p->enumIndex();
        if (i >= 0 && i < static_cast<int>(opts.size())) return opts[static_cast<size_t>(i)];
      }
    }
  return {};
}

}  // namespace

std::string metricThreadForHole(double d, double* nominal, double* pitch) {
  struct T {
    double d, p;
  };
  static const T sizes[] = {{1.6, 0.35}, {2, 0.4},   {2.5, 0.45}, {3, 0.5},  {4, 0.7},  {5, 0.8},
                            {6, 1.0},    {8, 1.25},  {10, 1.5},   {12, 1.75}, {14, 2.0}, {16, 2.0},
                            {18, 2.5},   {20, 2.5},  {22, 2.5},   {24, 3.0}, {27, 3.0}, {30, 3.5},
                            {33, 3.5},   {36, 4.0},  {42, 4.5},   {48, 5.0}};
  const T* best = nullptr;
  double bestErr = 1e300;
  for (const auto& t : sizes) {
    // tap drill (d - P) or a hole modelled at the nominal major diameter; the tap drill wins
    // ties (a 5 mm hole is an M6 tap drill rather than a plain M5 clearance)
    for (double cand : {t.d - t.p, t.d}) {
      double err = std::abs(cand - d) + (cand == t.d ? 1e-3 : 0.0);
      if (err < bestErr) {
        bestErr = err;
        best = &t;
      }
    }
  }
  if (!best || bestErr > std::max(0.051, d * 0.012)) return {};
  if (nominal) *nominal = best->d;
  if (pitch) *pitch = best->p;
  char buf[32];
  std::snprintf(buf, sizeof buf, "M%gx%g", best->d, best->p);
  return buf;
}

HoleInfo analyzeHole(const core::Document& doc, const Curve2D& circle) {
  HoleInfo info;
  if (circle.sourceBody == core::kInvalidId || circle.sourceEdge <= 0) return info;
  const TopoDS_Shape& shape = doc.bodyShape(circle.sourceBody);
  if (shape.IsNull()) return info;
  TopTools_IndexedMapOfShape edges;
  TopExp::MapShapes(shape, TopAbs_EDGE, edges);
  if (circle.sourceEdge > edges.Extent()) return info;
  BRepAdaptor_Curve ac(TopoDS::Edge(edges(circle.sourceEdge)));
  if (ac.GetType() != GeomAbs_Circle) return info;
  const gp_Ax1 axis = ac.Circle().Axis();
  const gp_Lin axisLine(axis);
  const double tol = 1e-4;

  std::vector<RevFace> cyl, cones;
  struct Disc {
    double t, r;
  };
  std::vector<Disc> discs;
  for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face& f = TopoDS::Face(ex.Current());
    BRepAdaptor_Surface s(f, false);
    RevFace rf;
    if (s.GetType() == GeomAbs_Cylinder || s.GetType() == GeomAbs_Cone) {
      gp_Ax1 fa = s.GetType() == GeomAbs_Cylinder ? s.Cylinder().Axis() : s.Cone().Axis();
      if (!fa.Direction().IsParallel(axis.Direction(), 1e-6) || axisLine.Distance(fa.Location()) > tol) continue;
      if (!faceExtent(f, axis, rf)) continue;
      rf.internal = isInternal(f, axis);
      if (s.GetType() == GeomAbs_Cone) {
        rf.cone = true;
        rf.semiAngle = std::abs(s.Cone().SemiAngle()) * 180.0 / kPi;
        cones.push_back(rf);
      } else {
        rf.r0 = rf.r1 = s.Cylinder().Radius();
        cyl.push_back(rf);
      }
    } else if (s.GetType() == GeomAbs_Plane) {
      if (!s.Plane().Axis().Direction().IsParallel(axis.Direction(), 1e-6)) continue;
      GProp_GProps g;
      BRepGProp::SurfaceProperties(f, g);
      if (axisLine.Distance(g.CentreOfMass()) > tol) continue;
      discs.push_back({axialParam(axis, g.CentreOfMass()), std::sqrt(std::max(0.0, g.Mass()) / kPi)});
    }
  }
  // merge split cylinder faces of equal radius and touching ranges
  std::vector<RevFace> holes;
  for (const auto& c : cyl) {
    if (!c.internal) continue;
    bool merged = false;
    for (auto& h : holes)
      if (std::abs(h.r0 - c.r0) < tol && c.t0 <= h.t1 + tol && c.t1 >= h.t0 - tol) {
        h.t0 = std::min(h.t0, c.t0);
        h.t1 = std::max(h.t1, c.t1);
        merged = true;
      }
    if (!merged) holes.push_back(c);
  }
  if (holes.empty()) return info;
  std::sort(holes.begin(), holes.end(), [](const RevFace& a, const RevFace& b) { return a.r0 < b.r0; });
  const RevFace drill = holes.front();
  info.diameter = 2 * drill.r0;
  auto touches = [&](double t) { return std::abs(t - drill.t0) < 1e-3 || std::abs(t - drill.t1) < 1e-3; };

  // features at the ends of the drill cylinder
  int bottomEnd = 0;  // -1: at t0, +1: at t1
  for (const auto& c : cones) {
    if (!c.internal) continue;
    if (c.rMin() < 1e-3 && std::abs(c.rMax() - drill.r0) < 1e-3) {  // drill point
      double tBase = c.r0 > c.r1 ? c.t0 : c.t1;
      if (std::abs(tBase - drill.t0) < 1e-3) bottomEnd = -1;
      else if (std::abs(tBase - drill.t1) < 1e-3) bottomEnd = 1;
      if (bottomEnd != 0) info.drillAngle = 2 * c.semiAngle;
    }
  }
  for (const auto& d : discs)
    if (std::abs(d.r - drill.r0) < 1e-3 * std::max(1.0, drill.r0)) {
      if (std::abs(d.t - drill.t0) < 1e-3) bottomEnd = -1;
      else if (std::abs(d.t - drill.t1) < 1e-3) bottomEnd = 1;
    }
  int entryEnd = 0;  // where the counterbore / countersink sits
  double entryT = 0;
  for (size_t i = 1; i < holes.size(); ++i) {
    const RevFace& c = holes[i];
    if (touches(c.t0) || touches(c.t1)) {
      info.type = HoleInfo::Counterbore;
      info.cboreDiameter = 2 * c.r0;
      info.cboreDepth = c.t1 - c.t0;
      entryEnd = std::abs(c.t0 - drill.t1) < 1e-3 ? 1 : -1;
      entryT = entryEnd > 0 ? c.t1 : c.t0;
      break;
    }
  }
  if (info.type == HoleInfo::Simple) {
    for (const auto& c : cones) {
      if (!c.internal || std::abs(c.rMin() - drill.r0) > 1e-3 || c.rMax() <= drill.r0 + 1e-3) continue;
      double tSmall = c.r0 < c.r1 ? c.t0 : c.t1, tLarge = c.r0 < c.r1 ? c.t1 : c.t0;
      if (!touches(tSmall)) continue;
      info.type = HoleInfo::Countersink;
      info.csinkDiameter = 2 * c.rMax();
      info.csinkAngle = 2 * c.semiAngle;
      entryEnd = tLarge > tSmall ? 1 : -1;
      entryT = tLarge;
      break;
    }
  }
  if (bottomEnd != 0 && entryEnd == bottomEnd) bottomEnd = 0;  // inconsistent: treat as through
  info.through = bottomEnd == 0;
  if (!info.through) {
    double tBottom = bottomEnd < 0 ? drill.t0 : drill.t1;
    double tTop = entryEnd != 0 ? entryT : (bottomEnd < 0 ? drill.t1 : drill.t0);
    info.depth = std::abs(tTop - tBottom);
  } else {
    info.depth = drill.t1 - drill.t0;
  }
  info.ok = true;
  info.source = "geometry";

  // hole feature data: instance count, thread
  if (const core::Feature* f = holeFeatureFor(doc, circle.sourceName)) {
    info.source = "feature:" + f->name();
    std::set<std::string> instances;
    const std::string key = f->name() + "/H";
    for (const auto& [name, sub] : part::currentNames(doc, circle.sourceBody)) {
      (void)sub;
      size_t p = name.find(key);
      if (p == std::string::npos) continue;
      size_t e = name.find('/', p + key.size());
      instances.insert(name.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size()));
    }
    if (!instances.empty()) info.count = static_cast<int>(instances.size());
    else if (const core::Property* pts = f->property("points")) info.count = std::max(1, static_cast<int>(pts->refs().size()));
    info.thread = stringProp(*f, {"threadDesignation", "threadSize", "thread"});
    bool threaded = !info.thread.empty();
    if (const core::Property* p = f->property("threaded"); p && p->type() == core::PropertyType::Bool && p->toBool()) threaded = true;
    if (threaded && info.thread.empty()) info.thread = metricThreadForHole(info.diameter);
    if (threaded) {
      info.threadClass = stringProp(*f, {"threadClass", "threadFit"});
      if (const core::Property* p = f->property("threadDepth"); p && p->type() == core::PropertyType::Length) info.threadDepth = p->toDouble();
    }
  }
  return info;
}

std::vector<RichLine> holeCalloutLines(const HoleInfo& info, const HoleCallout& c, const core::UnitSystem& units) {
  std::vector<RichLine> lines;
  if (!c.overrideText.empty()) {
    size_t start = 0;
    while (true) {
      size_t nl = c.overrideText.find('\n', start);
      lines.push_back({RichRun::t(c.overrideText.substr(start, nl == std::string::npos ? std::string::npos : nl - start))});
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
    return lines;
  }
  if (!info.ok) return {{RichRun::t("?")}};
  const int prec = c.precision >= 0 ? c.precision : units.precision;
  auto L = [&](double mm) { return formatValue(core::toDisplayValue(core::Quantity::mm(mm), units), prec); };
  auto A = [&](double deg) { return formatValue(deg, 1) + "\xC2\xB0"; };
  const std::string dia = "\xC3\x98";
  int count = c.countOverride > 0 ? c.countOverride : info.count;
  std::string countText = count > 1 ? std::to_string(count) + "x " : std::string();

  std::string thread = c.threadOverride;
  if (thread == "auto" || (c.thread && thread.empty())) thread = info.thread.empty() ? metricThreadForHole(info.diameter) : info.thread;
  if (!c.thread && thread.empty()) thread = info.thread;
  std::string cls = info.threadClass.empty() ? c.threadClass : info.threadClass;
  double threadDepth = info.threadDepth > 0 ? info.threadDepth : info.depth;
  auto threadLine = [&](const std::string& prefix) {
    RichLine l{RichRun::t(prefix + thread + (cls.empty() ? std::string() : " - " + cls))};
    if (info.through && info.threadDepth <= 0) l.push_back(RichRun::t(" THRU"));
    else {
      l.push_back(RichRun::t(" "));
      l.push_back(RichRun::g(Glyph::Depth));
      l.push_back(RichRun::t(L(threadDepth)));
    }
    return l;
  };
  auto drillLine = [&](const std::string& prefix) {
    RichLine l{RichRun::t(prefix + dia + L(info.diameter))};
    if (info.through) l.push_back(RichRun::t(" THRU"));
    else if (info.depth > 0) {
      l.push_back(RichRun::t(" "));
      l.push_back(RichRun::g(Glyph::Depth));
      l.push_back(RichRun::t(L(info.depth)));
    }
    return l;
  };
  if (c.thread && !thread.empty()) {
    lines.push_back(threadLine(countText));
    if (!info.through) lines.push_back(drillLine(""));
  } else {
    lines.push_back(drillLine(countText));
    if (info.type == HoleInfo::Counterbore)
      lines.push_back({RichRun::g(Glyph::Counterbore), RichRun::t(" " + dia + L(info.cboreDiameter) + " "), RichRun::g(Glyph::Depth),
                       RichRun::t(L(info.cboreDepth))});
    else if (info.type == HoleInfo::Countersink)
      lines.push_back({RichRun::g(Glyph::Countersink), RichRun::t(" " + dia + L(info.csinkDiameter) + " X " + A(info.csinkAngle))});
    if (!thread.empty()) lines.push_back(threadLine(""));
  }
  return lines;
}

}  // namespace fabricad::drawing
