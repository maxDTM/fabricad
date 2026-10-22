// FabriCAD sketch: 2D sketch model implementation (geometry, constraints, queries, operations).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"

#include "core/Json.h"
#include "sketch/SketchCurves.h"
#include "sketch/SketchSolver.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>

namespace fabricad::sketch {

Sketch::Sketch() = default;

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------

int Sketch::addGeometry(Geometry g) {
  g.id = nextGeoId_++;
  g.normalize();
  geometries_.push_back(std::move(g));
  return geometries_.back().id;
}

bool Sketch::removeGeometry(int id) {
  auto it = std::find_if(geometries_.begin(), geometries_.end(), [&](const Geometry& g) { return g.id == id; });
  if (it == geometries_.end()) return false;
  geometries_.erase(it);
  constraints_.erase(std::remove_if(constraints_.begin(), constraints_.end(),
                                    [&](const Constraint& c) { return c.references(id); }),
                     constraints_.end());
  return true;
}

Geometry* Sketch::geometry(int id) {
  for (auto& g : geometries_)
    if (g.id == id) return &g;
  return nullptr;
}

const Geometry* Sketch::geometry(int id) const {
  for (const auto& g : geometries_)
    if (g.id == id) return &g;
  return nullptr;
}

std::optional<Pt2> Sketch::pointOf(const GeoRef& ref) const {
  const Geometry* g = geometry(ref.geo);
  if (!g) return std::nullopt;
  if (ref.pos == PointPos::None) {
    if (g->type == GeomType::Point) return g->center();
    return std::nullopt;
  }
  if (g->type == GeomType::Point) return g->center();
  if (g->isClosedCurve() && ref.pos != PointPos::Center) return std::nullopt;
  if (g->type == GeomType::Line && ref.pos == PointPos::Center) return g->midpoint();
  if (ref.pos == PointPos::Vertex) {
    if (ref.index < 0 || ref.index >= g->vertexCount()) return std::nullopt;
    return g->vertex(ref.index);
  }
  if (g->type == GeomType::Text && ref.pos != PointPos::Start) return std::nullopt;
  if (g->isSpline() && ref.pos != PointPos::Start && ref.pos != PointPos::End) return std::nullopt;
  return g->point(ref.pos);
}

bool Sketch::setPointOf(const GeoRef& ref, Pt2 p) {
  Geometry* g = geometry(ref.geo);
  if (!g) return false;
  switch (ref.pos) {
    case PointPos::Start: g->setStart(p); return true;
    case PointPos::End: g->setEnd(p); return true;
    case PointPos::Center: g->setCenter(p); return true;
    case PointPos::Mid: g->translate(p - g->midpoint()); return true;
    case PointPos::Vertex:
      if (ref.index < 0 || ref.index >= g->vertexCount()) return false;
      g->setVertex(ref.index, p);
      return true;
    case PointPos::None:
      if (g->type == GeomType::Point) {
        g->setCenter(p);
        return true;
      }
      return false;
  }
  return false;
}

void Sketch::setConstruction(const std::vector<int>& ids, bool construction) {
  for (int id : ids)
    if (Geometry* g = geometry(id)) g->construction = construction;
}

void Sketch::toggleConstruction(const std::vector<int>& ids) {
  for (int id : ids)
    if (Geometry* g = geometry(id)) g->construction = !g->construction;
}

// ---------------------------------------------------------------------------
// constraints
// ---------------------------------------------------------------------------

int Sketch::addConstraint(Constraint c) {
  if (!c.isDimensional()) {
    if (const Constraint* existing = findConstraint(c.type, c.a, c.b)) {
      if (c.type != ConstraintType::Symmetric || existing->c == c.c) return existing->id;
    }
    // A tangent at a shared endpoint supersedes the coincidence of those points.
    if (c.type == ConstraintType::Tangent && c.a.isPoint() && c.b.isPoint()) {
      if (const Constraint* co = findConstraint(ConstraintType::Coincident, c.a, c.b)) removeConstraint(co->id);
    }
  } else {
    if (c.name.empty()) c.name = nextDimensionName();
    if (!c.hasLabelPos) {
      c.labelPos = defaultLabelPos(c);
      c.hasLabelPos = true;
    }
  }
  c.id = nextConstraintId_++;
  constraints_.push_back(std::move(c));
  return constraints_.back().id;
}

bool Sketch::removeConstraint(int id) {
  auto it = std::find_if(constraints_.begin(), constraints_.end(), [&](const Constraint& c) { return c.id == id; });
  if (it == constraints_.end()) return false;
  constraints_.erase(it);
  return true;
}

Constraint* Sketch::constraint(int id) {
  for (auto& c : constraints_)
    if (c.id == id) return &c;
  return nullptr;
}

const Constraint* Sketch::constraint(int id) const {
  for (const auto& c : constraints_)
    if (c.id == id) return &c;
  return nullptr;
}

std::vector<const Constraint*> Sketch::constraintsOf(int geoId) const {
  std::vector<const Constraint*> out;
  for (const auto& c : constraints_)
    if (c.references(geoId)) out.push_back(&c);
  return out;
}

const Constraint* Sketch::findConstraint(ConstraintType type, const GeoRef& a, const GeoRef& b) const {
  for (const auto& c : constraints_) {
    if (c.type != type) continue;
    if ((c.a == a && c.b == b) || (c.a == b && c.b == a)) return &c;
  }
  return nullptr;
}

std::vector<GeoRef> Sketch::coincidentGroup(const GeoRef& ref) const {
  std::vector<GeoRef> group{ref};
  std::deque<GeoRef> todo{ref};
  while (!todo.empty()) {
    GeoRef cur = todo.front();
    todo.pop_front();
    for (const auto& c : constraints_) {
      if (c.type != ConstraintType::Coincident || !c.a.isPoint() || !c.b.isPoint()) continue;
      GeoRef other;
      if (c.a == cur) other = c.b;
      else if (c.b == cur) other = c.a;
      else continue;
      if (std::find(group.begin(), group.end(), other) == group.end()) {
        group.push_back(other);
        todo.push_back(other);
      }
    }
  }
  return group;
}

bool Sketch::areCoincident(const GeoRef& a, const GeoRef& b) const {
  if (a == b) return true;
  auto g = coincidentGroup(a);
  return std::find(g.begin(), g.end(), b) != g.end();
}

std::string Sketch::nextDimensionName() const {
  int maxN = 0;
  for (const auto& c : constraints_) {
    if (c.name.size() > 1 && c.name[0] == 'd') {
      try {
        maxN = std::max(maxN, std::stoi(c.name.substr(1)));
      } catch (...) {
      }
    }
  }
  return std::format("d{}", maxN + 1);
}

const Constraint* Sketch::dimensionByName(const std::string& name) const {
  for (const auto& c : constraints_)
    if (c.isDimensional() && c.name == name) return &c;
  return nullptr;
}

std::optional<double> Sketch::measure(const Constraint& c) const {
  const Geometry* ga = geometry(c.a.geo);
  const Geometry* gb = c.b.isValid() ? geometry(c.b.geo) : nullptr;
  if (!ga) return std::nullopt;
  switch (c.type) {
    case ConstraintType::Distance: {
      if (c.a.isPoint() && c.b.isPoint()) {
        auto pa = pointOf(c.a), pb = pointOf(c.b);
        if (pa && pb) return pa->distance(*pb);
        return std::nullopt;
      }
      if (c.a.isPoint() && gb && gb->type == GeomType::Line) {
        auto pa = pointOf(c.a);
        if (pa) return std::abs(signedDistanceToLine(gb->start(), gb->direction(), *pa));
      }
      if (c.b.isPoint() && ga->type == GeomType::Line) {
        auto pb = pointOf(c.b);
        if (pb) return std::abs(signedDistanceToLine(ga->start(), ga->direction(), *pb));
      }
      if (ga->type == GeomType::Line && !gb) return ga->length();
      if (gb && ga->type == GeomType::Line && gb->type == GeomType::Line)
        return std::abs(signedDistanceToLine(ga->start(), ga->direction(), gb->start()));
      if (ga->isCircular() && c.b.isPoint()) {
        auto pb = pointOf(c.b);
        if (pb) return std::abs(pb->distance(ga->center()) - ga->radius());
      }
      return std::nullopt;
    }
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY: {
      bool x = c.type == ConstraintType::DistanceX;
      if (c.a.isPoint() && c.b.isPoint()) {
        auto pa = pointOf(c.a), pb = pointOf(c.b);
        if (pa && pb) return x ? pb->x - pa->x : pb->y - pa->y;
      } else if (c.a.isPoint()) {
        auto pa = pointOf(c.a);
        if (pa) return x ? pa->x : pa->y;
      } else if (ga->type == GeomType::Line) {
        return x ? ga->end().x - ga->start().x : ga->end().y - ga->start().y;
      }
      return std::nullopt;
    }
    case ConstraintType::Radius: return ga->isCircular() ? std::optional<double>(ga->radius()) : std::nullopt;
    case ConstraintType::Diameter: return ga->isCircular() ? std::optional<double>(2 * ga->radius()) : std::nullopt;
    case ConstraintType::MajorRadius: return ga->isElliptical() ? std::optional<double>(ga->majorRadius()) : std::nullopt;
    case ConstraintType::MinorRadius: return ga->isElliptical() ? std::optional<double>(ga->minorRadius()) : std::nullopt;
    case ConstraintType::Angle: {
      if (ga->type == GeomType::Arc && !gb) return ga->sweep() * 180.0 / kPi;  // arc sweep
      if (ga->type != GeomType::Line) return std::nullopt;
      if (gb && gb->type == GeomType::Line) {
        double a = angleDelta(ga->direction().angle(), gb->direction().angle());
        return a * 180.0 / kPi;
      }
      return ga->direction().angle() * 180.0 / kPi;
    }
    default: return std::nullopt;
  }
}

Pt2 Sketch::defaultLabelPos(const Constraint& c) const {
  const Geometry* ga = geometry(c.a.geo);
  if (!ga) return {};
  double off = 5.0;
  switch (c.type) {
    case ConstraintType::Radius:
    case ConstraintType::Diameter: return ga->center() + Pt2::polar(ga->radius() * 1.3 + off, kPi / 4);
    case ConstraintType::MajorRadius:
    case ConstraintType::MinorRadius: {
      if (!ga->isElliptical()) return ga->center();
      EllipseFrame f = ga->ellipse();
      bool major = c.type == ConstraintType::MajorRadius;
      Pt2 axis = major ? f.major : f.major.perp();
      return f.center + axis * ((major ? f.a : f.b) * 0.6) + axis.perp() * off;
    }
    case ConstraintType::Angle: {
      const Geometry* gb = c.b.isValid() ? geometry(c.b.geo) : nullptr;
      if (ga->type == GeomType::Arc) return ga->center() + Pt2::polar(ga->radius() * 0.6, ga->startAngle() + ga->sweep() / 2);
      if (gb && gb->type == GeomType::Line) {
        auto x = intersectLines(ga->start(), ga->direction(), gb->start(), gb->direction());
        Pt2 bis = (ga->direction() + gb->direction()).normalized();
        if (x) return *x + bis * (ga->length() * 0.4);
      }
      return ga->midpoint() + ga->direction().perp() * off;
    }
    default: {
      auto pa = pointOf(c.a);
      auto pb = c.b.isValid() ? pointOf(c.b) : std::nullopt;
      if (pa && pb) {
        Pt2 d = (*pb - *pa);
        Pt2 n = d.length() > 1e-9 ? d.normalized().perp() : Pt2{0, 1};
        return (*pa + *pb) * 0.5 + n * off;
      }
      if (ga->type == GeomType::Line) return ga->midpoint() + ga->direction().perp() * off;
      if (pa) return *pa + Pt2{off, off};
      return ga->midpoint() + Pt2{off, off};
    }
  }
}

int Sketch::findOriginHelper(OriginRef which) const {
  for (const Geometry& g : geometries_) {
    if (!g.isOriginHelper()) continue;
    switch (which) {
      case OriginRef::Point:
        if (g.type == GeomType::Point && g.center().length() < 1e-9) return g.id;
        break;
      case OriginRef::XAxis:
        if (g.type == GeomType::Line && std::abs(g.p[1]) < 1e-9 && std::abs(g.p[3]) < 1e-9) return g.id;
        break;
      case OriginRef::YAxis:
        if (g.type == GeomType::Line && std::abs(g.p[0]) < 1e-9 && std::abs(g.p[2]) < 1e-9) return g.id;
        break;
    }
  }
  return -1;
}

int Sketch::originHelper(OriginRef which) {
  if (int id = findOriginHelper(which); id >= 0) return id;
  Geometry g;
  if (which == OriginRef::Point) {
    g = Geometry::makePoint({0, 0});
  } else {
    // a segment of the axis covering the sketch (the solver treats lines as infinite anyway)
    double ext = 50.0;
    Pt2 mn, mx;
    if (bbox(mn, mx)) ext = std::max({ext, std::abs(mn.x), std::abs(mn.y), std::abs(mx.x), std::abs(mx.y)}) * 1.2;
    g = which == OriginRef::XAxis ? Geometry::makeLine({-ext, 0}, {ext, 0}) : Geometry::makeLine({0, -ext}, {0, ext});
  }
  g.construction = true;
  g.projected = true;  // fixed for the solver, not selectable by Select All, drawn as reference
  return addGeometry(std::move(g));
}

// ---------------------------------------------------------------------------
// solving
// ---------------------------------------------------------------------------

SolveResult Sketch::solve() {
  last_ = SketchSolver::solve(*this, true);
  return last_;
}

GeometryStatus Sketch::statusOf(int geoId) const {
  const Geometry* g = geometry(geoId);
  if (!g) return GeometryStatus::Free;
  for (int cid : last_.conflicting) {
    const Constraint* c = constraint(cid);
    if (c && c->references(geoId)) return GeometryStatus::Conflicting;
  }
  if (g->projected) return GeometryStatus::Projected;
  if (g->blocked) return GeometryStatus::Fixed;
  for (const auto& c : constraints_)
    if (c.type == ConstraintType::Fix && c.a.geo == geoId && c.a.pos == PointPos::None) return GeometryStatus::Fixed;
  return last_.isFullyConstrained(geoId) ? GeometryStatus::FullyConstrained : GeometryStatus::Free;
}

bool Sketch::beginDrag(const GeoRef& ref, Pt2 at) {
  const Geometry* g = geometry(ref.geo);
  if (!g || !g->isMovable()) return false;
  dragging_ = true;
  dragRef_ = ref;
  dragStart_ = at;
  dragParams_ = g->p;
  return true;
}

bool Sketch::drag(Pt2 target) {
  if (!dragging_) return false;
  Geometry* g = geometry(dragRef_.geo);
  if (!g) return false;
  // start from the original geometry each frame to keep the drag stable
  g->p = dragParams_;
  g->normalize();
  DragTarget d;
  d.ref = dragRef_;
  d.target = target;
  d.delta = target - dragStart_;
  SolveResult r = SketchSolver::solve(*this, false, &d);
  if (!r.ok() && r.status != SolveResult::Status::Conflicting) return false;
  return true;
}

void Sketch::endDrag() {
  dragging_ = false;
  dragParams_.clear();
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

std::optional<SketchHit> Sketch::hitPoint(Pt2 q, double tol, const std::set<int>& exclude) const {
  std::optional<SketchHit> best;
  for (const auto& g : geometries_) {
    if (exclude.count(g.id) || g.isOriginHelper()) continue;
    for (const GeoRef& r : g.pointRefs()) {
      Pt2 p = g.point(r.pos, r.index);
      double d = p.distance(q);
      if (d <= tol && (!best || d < best->distance)) best = SketchHit{r, p, d};
    }
  }
  return best;
}

std::optional<SketchHit> Sketch::hitCurve(Pt2 q, double tol, const std::set<int>& exclude) const {
  std::optional<SketchHit> best;
  for (const auto& g : geometries_) {
    if (exclude.count(g.id) || !g.isCurve() || g.isOriginHelper()) continue;
    Pt2 p = g.closestPoint(q);
    double d = p.distance(q);
    if (d <= tol && (!best || d < best->distance)) best = SketchHit{{g.id, PointPos::None}, p, d};
  }
  return best;
}

std::optional<SketchHit> Sketch::hit(Pt2 q, double tol, const std::set<int>& exclude) const {
  if (auto p = hitPoint(q, tol, exclude)) return p;
  return hitCurve(q, tol, exclude);
}

std::vector<int> Sketch::curvesInRect(Pt2 a, Pt2 b, bool crossing) const {
  Pt2 mn{std::min(a.x, b.x), std::min(a.y, b.y)}, mx{std::max(a.x, b.x), std::max(a.y, b.y)};
  std::vector<int> out;
  for (const auto& g : geometries_) {
    if (g.isOriginHelper()) continue;
    auto [gmn, gmx] = g.bbox();
    bool inside = gmn.x >= mn.x && gmn.y >= mn.y && gmx.x <= mx.x && gmx.y <= mx.y;
    bool overlaps = gmn.x <= mx.x && gmx.x >= mn.x && gmn.y <= mx.y && gmx.y >= mn.y;
    if (inside || (crossing && overlaps)) out.push_back(g.id);
  }
  return out;
}

std::vector<Pt2> Sketch::intersectionsOn(int curveId, bool includeConstruction) const {
  std::vector<Pt2> out;
  const Geometry* g = geometry(curveId);
  if (!g || !g->isCurve()) return out;
  for (const auto& o : geometries_) {
    if (o.id == curveId || !o.isCurve() || o.isOriginHelper()) continue;
    if (!includeConstruction && o.construction) continue;
    for (const Pt2& p : intersectCurves(*g, o)) {
      bool dup = false;
      for (const Pt2& q : out)
        if (q.near(p, 1e-6)) dup = true;
      if (!dup) out.push_back(p);
    }
  }
  return out;
}

bool Sketch::bbox(Pt2& mn, Pt2& mx, bool includeConstruction) const {
  bool any = false;
  for (const auto& g : geometries_) {
    if ((!includeConstruction && g.construction) || g.isOriginHelper()) continue;
    auto [a, b] = g.bbox();
    if (!any) {
      mn = a;
      mx = b;
      any = true;
    } else {
      mn = {std::min(mn.x, a.x), std::min(mn.y, a.y)};
      mx = {std::max(mx.x, b.x), std::max(mx.y, b.y)};
    }
  }
  return any;
}

std::vector<int> Sketch::chain(int curveId) const {
  std::vector<int> out;
  const Geometry* start = geometry(curveId);
  if (!start || !start->isCurve()) return out;
  std::deque<int> todo{curveId};
  std::set<int> seen{curveId};
  auto endpointsOf = [](const Geometry& g) {
    std::vector<Pt2> pts;
    if (g.type == GeomType::Line || g.type == GeomType::Arc || g.type == GeomType::EllipseArc || g.isSpline())
      pts = {g.start(), g.end()};
    return pts;
  };
  while (!todo.empty()) {
    int id = todo.front();
    todo.pop_front();
    out.push_back(id);
    const Geometry* g = geometry(id);
    if (!g) continue;
    auto eps = endpointsOf(*g);
    for (const auto& o : geometries_) {
      if (seen.count(o.id) || !o.isCurve() || o.construction != g->construction) continue;
      bool connected = false;
      for (const Pt2& p : eps)
        for (const Pt2& q : endpointsOf(o))
          if (p.near(q, 1e-6)) connected = true;
      if (!connected) {
        for (PointPos pa : {PointPos::Start, PointPos::End})
          for (PointPos pb : {PointPos::Start, PointPos::End})
            if (findConstraint(ConstraintType::Coincident, {id, pa}, {o.id, pb})) connected = true;
      }
      if (connected) {
        seen.insert(o.id);
        todo.push_back(o.id);
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// operations
// ---------------------------------------------------------------------------

namespace {

struct ParamHit {
  double param;
  int otherId;
};

/// Reassign constraints after `oldId` was shortened/split: point refs on `movedPos` move to
/// (newId, newPos); point-on-curve constraints follow the nearest piece.
void redistributeConstraints(Sketch& sk, int oldId, int newId, PointPos movedPos, PointPos newPos) {
  for (Constraint& c : sk.constraints()) {
    for (GeoRef* r : {&c.a, &c.b, &c.c}) {
      if (r->geo == oldId && r->pos == movedPos && newId >= 0) {
        r->geo = newId;
        r->pos = newPos;
      }
    }
  }
}

void followNearestPiece(Sketch& sk, int oldId, int newId) {
  if (newId < 0) return;
  const Geometry* a = sk.geometry(oldId);
  const Geometry* b = sk.geometry(newId);
  if (!a || !b) return;
  std::vector<Constraint> copies;
  for (Constraint& c : sk.constraints()) {
    if (c.type == ConstraintType::Coincident || c.type == ConstraintType::PointOnCurve) {
      GeoRef* curveRef = nullptr;
      GeoRef* ptRef = nullptr;
      if (c.a.geo == oldId && c.a.pos == PointPos::None) { curveRef = &c.a; ptRef = &c.b; }
      else if (c.b.geo == oldId && c.b.pos == PointPos::None) { curveRef = &c.b; ptRef = &c.a; }
      if (!curveRef || !ptRef->isValid()) continue;
      auto p = sk.pointOf(*ptRef);
      if (!p) continue;
      if (b->distanceTo(*p) < a->distanceTo(*p)) curveRef->geo = newId;
    } else if (c.a.geo == oldId && c.a.pos == PointPos::None && !c.isDimensional() &&
               (c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical ||
                c.type == ConstraintType::Parallel || c.type == ConstraintType::Perpendicular ||
                c.type == ConstraintType::Collinear)) {
      Constraint copy = c;
      copy.a.geo = newId;
      copies.push_back(copy);
    } else if (c.b.geo == oldId && c.b.pos == PointPos::None && !c.isDimensional() &&
               (c.type == ConstraintType::Parallel || c.type == ConstraintType::Perpendicular ||
                c.type == ConstraintType::Collinear)) {
      Constraint copy = c;
      copy.b.geo = newId;
      copies.push_back(copy);
    }
  }
  for (Constraint& c : copies) {
    c.id = -1;
    sk.addConstraint(c);
  }
}

void dropPointConstraints(Sketch& sk, int geoId, PointPos pos) {
  auto& cs = sk.constraints();
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [&](const Constraint& c) {
                            if (c.isDimensional()) return false;
                            for (const GeoRef* r : {&c.a, &c.b, &c.c})
                              if (r->geo == geoId && r->pos == pos) return true;
                            return false;
                          }),
           cs.end());
}

}  // namespace

namespace {

/// A spline whose end meets its start (drawn closed): trimmed like a circle.
bool isClosedSpline(const Geometry& g) {
  return g.isSpline() && g.vertexCount() >= 3 && g.start().distance(g.end()) < 1e-6;
}

/// One fit point of a spline piece: position, interpolation parameter, index in the original
/// spline (-1 for cut / added points).
struct FitPt {
  Pt2 p;
  double t;
  int orig;
};

/// Fit points of `g` (a FitSpline) for the parameter range [u0, u1]; with `wrap` the range
/// u1 is beyond the end of a closed spline and continues from its start (shifted by the range).
std::vector<FitPt> fitPoints(const Geometry& g, double u0, double u1) {
  BSpline2 sp = g.bspline();
  const double a = sp.first(), b = sp.last(), len = b - a;
  const double eps = 1e-3 * std::max(len, 1e-9);
  std::vector<double> t = g.knots.size() == static_cast<size_t>(g.vertexCount()) ? g.knots : std::vector<double>{};
  if (t.empty())
    for (int k = 0; k < g.vertexCount(); ++k) t.push_back(sp.closestParam(g.vertex(k)));
  auto at = [&](double u) { return sp.value(u > b ? u - len : u); };
  std::vector<FitPt> out{{at(u0), u0, -1}};
  for (int pass = 0; pass < 2; ++pass) {
    double shift = pass == 0 ? 0.0 : len;
    for (int k = 0; k < g.vertexCount(); ++k) {
      if (pass == 1 && k == 0) continue;  // the seam point is the last fit point already
      double tk = t[static_cast<size_t>(k)] + shift;
      if (tk > u0 + eps && tk < u1 - eps) out.push_back({g.vertex(k), tk, k});
    }
    if (u1 <= b + eps) break;
  }
  out.push_back({at(u1), u1, -1});
  // the refit has other knots and end conditions than the original: add samples of the original
  // where the refit strays (adaptive, so the piece keeps few fit points)
  const double tolDev = 1e-3 * std::max(1.0, g.bbox().second.distance(g.bbox().first));
  for (int iter = 0; iter < 24 && out.size() >= 2; ++iter) {
    std::vector<Pt2> ps;
    std::vector<double> ts;
    for (const FitPt& f : out) {
      ps.push_back(f.p);
      ts.push_back(f.t);
    }
    BSpline2 fit = interpolateSpline(ps, ts);
    if (!fit.isValid()) break;
    size_t worst = 0;
    double worstDev = 0;
    for (size_t i = 0; i + 1 < out.size(); ++i)
      for (double f : {0.25, 0.5, 0.75}) {
        double m = out[i].t + (out[i + 1].t - out[i].t) * f;
        double d = fit.value(m).distance(at(m));
        if (d > worstDev) {
          worstDev = d;
          worst = i;
        }
      }
    if (worstDev <= tolDev) break;
    double m = 0.5 * (out[worst].t + out[worst + 1].t);
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(worst) + 1, FitPt{at(m), m, -1});
  }
  // keep it cubic and close to the original: add samples in the widest gaps
  while (out.size() < 4) {
    size_t w = 0;
    for (size_t i = 1; i + 1 < out.size(); ++i)
      if (out[i + 1].t - out[i].t > out[w + 1].t - out[w].t) w = i;
    double m = 0.5 * (out[w].t + out[w + 1].t);
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(w) + 1, FitPt{at(m), m, -1});
  }
  return out;
}

/// The piece of spline `g` between parameters u0 < u1 (u1 may run past the end of a closed
/// spline, continuing from its start). Control-point splines are cut exactly (knot insertion);
/// fit-point splines keep the fit points in the range plus the cut points, with their original
/// parameters, so the shape barely changes. `origIndex` receives the original index of each
/// fit point (-1 for new points; empty for control-point splines).
Geometry splinePiece(const Geometry& g, double u0, double u1, std::vector<int>* origIndex = nullptr) {
  if (origIndex) origIndex->clear();
  Geometry out;
  if (g.type == GeomType::FitSpline) {
    std::vector<FitPt> pts = fitPoints(g, u0, u1);
    std::vector<Pt2> ps;
    std::vector<double> ts;
    for (const FitPt& f : pts) {
      ps.push_back(f.p);
      ts.push_back(f.t);
      if (origIndex) origIndex->push_back(f.orig);
    }
    out = Geometry::makeFitSpline(ps);
    out.knots = ts;
  } else {
    BSpline2 sp = g.bspline();
    const double a = sp.first(), b = sp.last();
    BSpline2 piece;
    if (u1 <= b + 1e-12) {
      piece = subBSpline(sp, u0, u1);
    } else {
      piece = joinBSplines(subBSpline(sp, u0, b), subBSpline(sp, a, u1 - (b - a)));
    }
    if (!piece.isValid()) return g;
    out = Geometry::makeControlSpline(piece.poles, piece.degree);
    out.knots = piece.knots;
  }
  out.construction = g.construction;
  return out;
}

/// After replacing spline `oldId` by pieces: move constraints on its fit points (Vertex refs) to
/// the piece that kept that point, drop the others. `maps[i]` = original index per fit point of
/// piece `ids[i]`.
void remapVertexConstraints(Sketch& sk, int oldId, const std::vector<int>& ids, const std::vector<std::vector<int>>& maps) {
  auto& cs = sk.constraints();
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [&](Constraint& c) {
                            for (GeoRef* r : {&c.a, &c.b, &c.c}) {
                              if (r->geo != oldId || r->pos != PointPos::Vertex) continue;
                              bool found = false;
                              for (size_t i = 0; i < ids.size() && !found; ++i)
                                for (size_t k = 0; k < maps[i].size(); ++k)
                                  if (maps[i][k] == r->index) {
                                    r->geo = ids[i];
                                    r->index = static_cast<int>(k);
                                    found = true;
                                    break;
                                  }
                              if (!found) return true;
                            }
                            return false;
                          }),
           cs.end());
}

/// Bracketing of the piece of a curve around a pick point by its intersections.
struct TrimBracket {
  double lo = 0, hi = 0;    ///< parameter bounds of the removed piece
  int loOther = -1;         ///< curve producing the lo intersection (-1 = curve start)
  int hiOther = -1;         ///< curve producing the hi intersection (-1 = curve end)
  bool whole = false;       ///< nothing brackets the piece: remove the whole curve
  bool circleClosed = false;  ///< circle with fewer than two intersections
};

TrimBracket computeBracket(const Sketch& sk, const Geometry& g, Pt2 at) {
  TrimBracket br;
  std::vector<ParamHit> hits;
  for (const auto& o : sk.geometries()) {
    if (o.id == g.id || !o.isCurve()) continue;
    for (const Pt2& p : intersectCurves(g, o)) hits.push_back({g.paramOf(p), o.id});
  }
  if (g.type == GeomType::Text) {
    // text is only removed as a whole (when nothing crosses it)
    br.whole = hits.empty();
    if (!br.whole) br.lo = br.hi = 0;
    return br;
  }
  if (g.isSpline()) {
    // by parameter range, like lines (open) or circles (closed, needs two cuts)
    auto [u0, u1] = g.paramRange();
    const double eps = 1e-7 * std::max(1.0, u1 - u0);
    double uAt = std::clamp(g.paramOf(at), u0, u1);
    std::vector<ParamHit> ts;
    for (const auto& h : hits)
      if (h.param > u0 + eps && h.param < u1 - eps) ts.push_back(h);
    std::sort(ts.begin(), ts.end(), [](const ParamHit& a, const ParamHit& b) { return a.param < b.param; });
    if (isClosedSpline(g)) {
      if (ts.size() < 2) {
        br.whole = true;
        br.circleClosed = true;
        return br;
      }
      size_t i = ts.size() - 1;
      bool before = false;
      for (size_t k = 0; k < ts.size(); ++k)
        if (ts[k].param <= uAt) {
          i = k;
          before = true;
        }
      size_t j = before ? (i + 1) % ts.size() : 0;
      br.lo = ts[i].param;
      br.loOther = ts[i].otherId;
      br.hi = ts[j].param;
      br.hiOther = ts[j].otherId;
      if (br.hi <= br.lo) br.hi += u1 - u0;  // the piece runs across the seam
      return br;
    }
    br.lo = u0;
    br.hi = u1;
    for (const auto& h : ts)
      if (h.param <= uAt) {
        br.lo = h.param;
        br.loOther = h.otherId;
      }
    for (const auto& h : ts)
      if (h.param >= uAt) {
        br.hi = h.param;
        br.hiOther = h.otherId;
        break;
      }
    br.whole = br.loOther == -1 && br.hiOther == -1;
    return br;
  }
  if (g.type == GeomType::Line) {
    std::vector<ParamHit> ts;
    for (const auto& h : hits)
      if (h.param > 1e-9 && h.param < 1 - 1e-9) ts.push_back(h);
    std::sort(ts.begin(), ts.end(), [](const ParamHit& a, const ParamHit& b) { return a.param < b.param; });
    double tAt = std::clamp(g.paramOf(at), 0.0, 1.0);
    br.lo = 0;
    br.hi = 1;
    for (const auto& h : ts)
      if (h.param <= tAt) {
        br.lo = h.param;
        br.loOther = h.otherId;
      }
    for (const auto& h : ts)
      if (h.param >= tAt) {
        br.hi = h.param;
        br.hiOther = h.otherId;
        break;
      }
    br.whole = br.loOther == -1 && br.hiOther == -1;
    return br;
  }
  if (g.isClosedCurve()) {
    if (hits.size() < 2) {
      br.whole = true;
      br.circleClosed = true;
      return br;
    }
    std::vector<ParamHit> as = hits;
    for (auto& h : as) h.param = normalizeAngle(h.param);
    std::sort(as.begin(), as.end(), [](const ParamHit& a, const ParamHit& b) { return a.param < b.param; });
    double aAt = normalizeAngle(g.paramOf(at));
    size_t i = as.size() - 1;
    for (size_t k = 0; k < as.size(); ++k)
      if (as[k].param <= aAt) i = k;
    size_t j = (i + 1) % as.size();
    br.lo = as[i].param;
    br.loOther = as[i].otherId;
    br.hi = as[j].param;
    br.hiOther = as[j].otherId;
    if (br.hi <= br.lo) br.hi += 2 * kPi;
    return br;
  }
  // arc
  double a0 = g.startAngle(), a1 = g.endAngle();
  std::vector<ParamHit> as;
  for (const auto& h : hits) {
    double rel = normalizeAngle(h.param - a0);
    if (rel > 1e-9 && rel < (a1 - a0) - 1e-9) as.push_back({a0 + rel, h.otherId});
  }
  std::sort(as.begin(), as.end(), [](const ParamHit& a, const ParamHit& b) { return a.param < b.param; });
  double aAt = a0 + std::clamp(normalizeAngle(g.paramOf(at) - a0), 0.0, a1 - a0);
  br.lo = a0;
  br.hi = a1;
  for (const auto& h : as)
    if (h.param <= aAt) {
      br.lo = h.param;
      br.loOther = h.otherId;
    }
  for (const auto& h : as)
    if (h.param >= aAt) {
      br.hi = h.param;
      br.hiOther = h.otherId;
      break;
    }
  br.whole = br.loOther == -1 && br.hiOther == -1;
  return br;
}

/// Arc of the same circle / ellipse between two parameters (ccw).
Geometry subArc(const Geometry& g, double lo, double hi) {
  if (g.isElliptical()) {
    EllipseFrame f = g.ellipse();
    return Geometry::makeEllipseArc(f.center, f.a, f.b, f.major.angle(), lo, hi);
  }
  return Geometry::makeArc(g.center(), g.radius(), lo, hi);
}

}  // namespace

std::optional<Geometry> Sketch::trimPiece(int curveId, Pt2 at) const {
  const Geometry* g = geometry(curveId);
  if (!g || !g->isCurve()) return std::nullopt;
  TrimBracket br = computeBracket(*this, *g, at);
  if (br.whole) return *g;
  if (g->type == GeomType::Text) return std::nullopt;
  if (g->isSpline()) return splinePiece(*g, br.lo, br.hi);
  Geometry piece = *g;
  piece.id = -1;
  if (g->type == GeomType::Line) {
    piece = Geometry::makeLine(g->pointAt(br.lo), g->pointAt(br.hi));
  } else {
    piece = subArc(*g, br.lo, br.hi);
  }
  piece.construction = g->construction;
  return piece;
}

std::vector<int> Sketch::trim(int curveId, Pt2 at) {
  Geometry* g = geometry(curveId);
  if (!g || !g->isCurve()) return {};
  const Geometry orig = *g;
  TrimBracket br = computeBracket(*this, orig, at);
  if (br.whole) {
    removeGeometry(curveId);
    return {};
  }
  if (orig.type == GeomType::Text) return {curveId};
  auto onCurve = [&](int geo, PointPos pos, int other) {
    if (other < 0) return;
    addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {geo, pos}, .b = {other, PointPos::None}, .internal = true});
  };

  if (orig.isSpline()) {
    auto [u0, u1] = orig.paramRange();
    auto replace = [&](int id, const Geometry& piece) {
      Geometry* t = geometry(id);
      Geometry keep = piece;
      keep.id = id;
      keep.projected = orig.projected;
      keep.blocked = orig.blocked;
      keep.source = orig.source;
      *t = keep;
    };
    std::vector<int> mapA, mapB;
    if (isClosedSpline(orig)) {
      // what stays runs from the hi cut round (across the seam) to the lo cut
      double from = br.hi, to = br.lo;
      if (from >= u1 - 1e-12) from -= u1 - u0;
      if (to <= from) to += u1 - u0;
      Geometry rest = splinePiece(orig, from, to, &mapA);
      dropPointConstraints(*this, curveId, PointPos::Start);
      dropPointConstraints(*this, curveId, PointPos::End);
      replace(curveId, rest);
      remapVertexConstraints(*this, curveId, {curveId}, {mapA});
      onCurve(curveId, PointPos::Start, br.hiOther);
      onCurve(curveId, PointPos::End, br.loOther);
      return {curveId};
    }
    if (br.loOther == -1) {
      dropPointConstraints(*this, curveId, PointPos::Start);
      replace(curveId, splinePiece(orig, br.hi, u1, &mapA));
      remapVertexConstraints(*this, curveId, {curveId}, {mapA});
      onCurve(curveId, PointPos::Start, br.hiOther);
      return {curveId};
    }
    if (br.hiOther == -1) {
      dropPointConstraints(*this, curveId, PointPos::End);
      replace(curveId, splinePiece(orig, u0, br.lo, &mapA));
      remapVertexConstraints(*this, curveId, {curveId}, {mapA});
      onCurve(curveId, PointPos::End, br.loOther);
      return {curveId};
    }
    Geometry second = splinePiece(orig, br.hi, u1, &mapB);
    int newId = addGeometry(second);
    replace(curveId, splinePiece(orig, u0, br.lo, &mapA));
    redistributeConstraints(*this, curveId, newId, PointPos::End, PointPos::End);
    followNearestPiece(*this, curveId, newId);
    remapVertexConstraints(*this, curveId, {curveId, newId}, {mapA, mapB});
    onCurve(curveId, PointPos::End, br.loOther);
    onCurve(newId, PointPos::Start, br.hiOther);
    return {curveId, newId};
  }

  if (orig.type == GeomType::Line) {
    if (br.loOther == -1) {
      dropPointConstraints(*this, curveId, PointPos::Start);
      g->setStart(orig.pointAt(br.hi));
      onCurve(curveId, PointPos::Start, br.hiOther);
      return {curveId};
    }
    if (br.hiOther == -1) {
      dropPointConstraints(*this, curveId, PointPos::End);
      g->setEnd(orig.pointAt(br.lo));
      onCurve(curveId, PointPos::End, br.loOther);
      return {curveId};
    }
    Geometry second = Geometry::makeLine(orig.pointAt(br.hi), orig.end());
    second.construction = orig.construction;
    int newId = addGeometry(second);
    g = geometry(curveId);
    g->setEnd(orig.pointAt(br.lo));
    redistributeConstraints(*this, curveId, newId, PointPos::End, PointPos::End);
    followNearestPiece(*this, curveId, newId);
    onCurve(curveId, PointPos::End, br.loOther);
    onCurve(newId, PointPos::Start, br.hiOther);
    return {curveId, newId};
  }

  if (orig.isClosedCurve()) {
    // the remaining arc runs ccw from the hi intersection round to the lo intersection
    Geometry arc = subArc(orig, br.hi, br.lo);
    arc.id = curveId;
    arc.construction = orig.construction;
    arc.projected = orig.projected;
    arc.blocked = orig.blocked;
    arc.source = orig.source;
    *g = arc;
    onCurve(curveId, PointPos::Start, br.hiOther);
    onCurve(curveId, PointPos::End, br.loOther);
    return {curveId};
  }

  // arc
  double a0 = orig.startAngle(), a1 = orig.endAngle();
  if (br.loOther == -1) {
    dropPointConstraints(*this, curveId, PointPos::Start);
    g->setAngles(br.hi, a1);
    onCurve(curveId, PointPos::Start, br.hiOther);
    return {curveId};
  }
  if (br.hiOther == -1) {
    dropPointConstraints(*this, curveId, PointPos::End);
    g->setAngles(a0, br.lo);
    onCurve(curveId, PointPos::End, br.loOther);
    return {curveId};
  }
  Geometry second = subArc(orig, br.hi, a1);
  second.construction = orig.construction;
  int newId = addGeometry(second);
  g = geometry(curveId);
  g->setAngles(a0, br.lo);
  redistributeConstraints(*this, curveId, newId, PointPos::End, PointPos::End);
  followNearestPiece(*this, curveId, newId);
  addConstraint(Constraint{.type = ConstraintType::Concentric, .a = {curveId, PointPos::None}, .b = {newId, PointPos::None}, .internal = true});
  addConstraint(Constraint{.type = ConstraintType::Equal, .a = {curveId, PointPos::None}, .b = {newId, PointPos::None}, .internal = true});
  onCurve(curveId, PointPos::End, br.loOther);
  onCurve(newId, PointPos::Start, br.hiOther);
  return {curveId, newId};
}

std::optional<Pt2> Sketch::extendTarget(int curveId, Pt2 near, PointPos* whichEnd) const {
  const Geometry* g = geometry(curveId);
  if (!g || (g->type != GeomType::Line && g->type != GeomType::Arc)) return std::nullopt;
  bool atEnd = near.distance(g->end()) <= near.distance(g->start());
  if (whichEnd) *whichEnd = atEnd ? PointPos::End : PointPos::Start;
  if (g->type == GeomType::Line) {
    Pt2 a = g->start(), d = g->end() - g->start();
    double best = std::numeric_limits<double>::infinity();
    std::optional<Pt2> bestP;
    for (const auto& o : geometries_) {
      if (o.id == curveId || !o.isCurve()) continue;
      std::vector<Pt2> cand;
      if (o.type == GeomType::Line) {
        if (auto p = intersectLines(a, d, o.start(), o.direction())) {
          double t = projectParam(o.start(), o.end() - o.start(), *p);
          if (t >= -1e-9 && t <= 1 + 1e-9) cand.push_back(*p);
        }
      } else {
        for (const Pt2& p : intersectLineCircle(a, d, o.center(), o.radius()))
          if (o.containsAngle((p - o.center()).angle())) cand.push_back(p);
      }
      for (const Pt2& p : cand) {
        double t = projectParam(a, d, p);
        double dist = atEnd ? t - 1 : -t;
        if (dist > 1e-9 && dist < best) {
          best = dist;
          bestP = p;
        }
      }
    }
    return bestP;
  }
  Geometry full = Geometry::makeCircle(g->center(), g->radius());
  double best = std::numeric_limits<double>::infinity();
  std::optional<Pt2> bestP;
  double a0 = g->startAngle(), a1 = g->endAngle();
  for (const auto& o : geometries_) {
    if (o.id == curveId || !o.isCurve()) continue;
    for (const Pt2& p : intersectCurves(full, o)) {
      double ang = (p - g->center()).angle();
      double rel = atEnd ? normalizeAngle(ang - a1) : normalizeAngle(a0 - ang);
      if (rel > 1e-9 && rel < 2 * kPi - (a1 - a0) - 1e-9 && rel < best) {
        best = rel;
        bestP = p;
      }
    }
  }
  return bestP;
}

bool Sketch::extend(int curveId, Pt2 near) {
  PointPos which = PointPos::None;
  auto target = extendTarget(curveId, near, &which);
  if (!target) return false;
  Geometry* g = geometry(curveId);
  int other = -1;
  for (const auto& o : geometries_) {
    if (o.id == curveId || !o.isCurve()) continue;
    if (o.distanceTo(*target) < 1e-6) {
      other = o.id;
      break;
    }
  }
  if (g->type == GeomType::Line) {
    if (which == PointPos::End) g->setEnd(*target);
    else g->setStart(*target);
  } else {
    double ang = (*target - g->center()).angle();
    if (which == PointPos::End) g->setAngles(g->startAngle(), ang);
    else g->setAngles(ang, g->endAngle());
  }
  if (other >= 0)
    addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {curveId, which}, .b = {other, PointPos::None}, .internal = true});
  return true;
}

namespace {

/// Order a set of curves into a connected sequence; `reversed[i]` tells whether curve i is
/// traversed end->start. Unconnected curves are appended as their own runs.
struct ChainItem {
  int id;
  bool reversed;
};

std::vector<std::vector<ChainItem>> orderChains(const Sketch& sk, const std::vector<int>& ids) {
  std::vector<std::vector<ChainItem>> runs;
  std::set<int> left(ids.begin(), ids.end());
  auto endpoints = [&](int id, bool rev) {
    const Geometry* g = sk.geometry(id);
    Pt2 s = g->start(), e = g->end();
    if (g->isClosedCurve() || g->type == GeomType::Point) return std::pair<Pt2, Pt2>{s, s};
    return rev ? std::pair<Pt2, Pt2>{e, s} : std::pair<Pt2, Pt2>{s, e};
  };
  while (!left.empty()) {
    int first = *left.begin();
    left.erase(first);
    std::deque<ChainItem> run{{first, false}};
    const Geometry* fg = sk.geometry(first);
    if (fg->isClosedCurve() || isClosedSpline(*fg)) {
      runs.push_back({run.begin(), run.end()});
      continue;
    }
    bool grew = true;
    while (grew) {
      grew = false;
      Pt2 tailEnd = endpoints(run.back().id, run.back().reversed).second;
      Pt2 headStart = endpoints(run.front().id, run.front().reversed).first;
      for (int id : left) {
        const Geometry* g = sk.geometry(id);
        if (g->isClosedCurve() || isClosedSpline(*g) || g->type == GeomType::Point) continue;
        if (g->start().near(tailEnd, 1e-6)) { run.push_back({id, false}); left.erase(id); grew = true; break; }
        if (g->end().near(tailEnd, 1e-6)) { run.push_back({id, true}); left.erase(id); grew = true; break; }
        if (g->end().near(headStart, 1e-6)) { run.push_front({id, false}); left.erase(id); grew = true; break; }
        if (g->start().near(headStart, 1e-6)) { run.push_front({id, true}); left.erase(id); grew = true; break; }
      }
    }
    runs.push_back({run.begin(), run.end()});
  }
  return runs;
}

/// Unit left normal of the traversal of a sampled curve at parameter u.
Pt2 leftNormalAt(const Geometry& g, double u, bool reversed) {
  Pt2 t;
  if (g.isElliptical()) t = g.ellipse().derivative(u);
  else t = g.bspline().derivative(u);
  if (t.length() < 1e-12) return {0, 0};
  t = t.normalized();
  return (reversed ? -t : t).perp();
}

/// Offset of an ellipse / elliptical arc / spline, approximated by a fit-point spline through
/// offset samples (the exact offset of these curves is not of their own type). Ellipses: d > 0 =
/// outward, like circles; others: d > 0 = left of the traversal. Nullopt when the offset would
/// fold over itself (inward beyond the tightest curvature).
std::optional<Geometry> offsetSampled(const Geometry& g, bool reversed, double d) {
  const bool closed = g.type == GeomType::Ellipse;
  double side = d;
  if (g.isElliptical()) {
    EllipseFrame f = g.ellipse();
    // ccw parametrisation: its left side is inside. Folding starts at the minimum radius of
    // curvature b^2 / a.
    if (closed) side = -d;
    bool inward = (closed ? d < 0 : (reversed ? d < 0 : d > 0));
    if (inward && std::abs(d) >= f.b * f.b / std::max(f.a, 1e-12) - 1e-9) return std::nullopt;
  }
  auto [u0, u1] = g.paramRange();
  const int n = closed ? 64 : std::max(24, g.isSpline() ? g.vertexCount() * 12 : 48);
  std::vector<Pt2> pts;
  std::vector<Pt2> src;
  for (int i = 0; i < n; ++i) {
    double u = u0 + (u1 - u0) * i / (closed ? n : n - 1);
    Pt2 q = g.pointAt(u);
    Pt2 nrm = leftNormalAt(g, u, false);
    src.push_back(q);
    pts.push_back(q + nrm * side * (reversed && !closed ? -1.0 : 1.0));
  }
  if (closed) pts.push_back(pts.front());
  // a fold (a segment reversing against the original) means the offset is too large
  for (size_t i = 0; i + 1 < src.size(); ++i)
    if ((pts[i + 1] - pts[i]).dot(src[i + 1] - src[i]) <= 0) return std::nullopt;
  Geometry o = Geometry::makeFitSpline(pts);
  o.construction = g.construction;
  return o;
}

/// Offset one curve; nullopt when the result degenerates (an arc/circle radius collapses).
std::optional<Geometry> offsetCurve(const Geometry& g, bool reversed, double d) {
  // d > 0 = to the left of the traversal direction
  Geometry o = g;
  switch (g.type) {
    case GeomType::Line: {
      Pt2 n = g.direction().perp() * (reversed ? -1.0 : 1.0);
      o.translate(n * d);
      break;
    }
    case GeomType::Circle: {  // positive = outward
      double r = g.radius() + d;
      if (r <= 1e-6) return std::nullopt;
      o.setRadius(r);
      break;
    }
    case GeomType::Arc: {
      // ccw traversal: left = towards centre
      double r = reversed ? g.radius() + d : g.radius() - d;
      if (r <= 1e-6) return std::nullopt;
      o.setRadius(r);
      break;
    }
    case GeomType::Ellipse:
    case GeomType::EllipseArc:
    case GeomType::Spline:
    case GeomType::FitSpline: return offsetSampled(g, reversed, d);
    case GeomType::Point:
    case GeomType::Text: break;  // not offsettable; copied unchanged (filtered by offset())
  }
  return o;
}

/// Signed side of `p` relative to one traversed curve: > 0 = left of the traversal direction.
double sideOfCurve(const Geometry& g, bool reversed, Pt2 p) {
  if (g.type == GeomType::Line) return signedDistanceToLine(g.start(), g.direction(), p) * (reversed ? -1 : 1);
  if (g.type == GeomType::Circle) return p.distance(g.center()) - g.radius();  // circles: outward = positive
  if (g.type == GeomType::Ellipse) {  // outward = positive
    EllipseFrame f = g.ellipse();
    Pt2 v = p - f.center;
    double x = v.dot(f.major) / f.a, y = v.dot(f.minor) / f.b;
    return x * x + y * y - 1;
  }
  if (g.type == GeomType::EllipseArc || g.isSpline()) {
    double u = g.paramOf(p);
    return (p - g.pointAt(u)).dot(leftNormalAt(g, u, reversed));
  }
  return (p.distance(g.center()) - g.radius()) * (reversed ? 1 : -1);
}

/// The run as a polyline in traversal order (for orientation / inside tests of closed loops).
std::vector<Pt2> runPolyline(const Sketch& sk, const std::vector<ChainItem>& run) {
  std::vector<Pt2> pts;
  for (const ChainItem& item : run) {
    std::vector<Pt2> t = sk.geometry(item.id)->tessellate(32);
    if (item.reversed) std::reverse(t.begin(), t.end());
    for (size_t i = pts.empty() ? 0 : 1; i < t.size(); ++i) pts.push_back(t[i]);
  }
  return pts;
}

bool runIsClosed(const Sketch& sk, const std::vector<ChainItem>& run) {
  if (run.size() < 2) return false;
  const Geometry* gl = sk.geometry(run.back().id);
  const Geometry* gf = sk.geometry(run.front().id);
  if (gl->isClosedCurve() || gf->isClosedCurve()) return false;
  Pt2 tail = run.back().reversed ? gl->start() : gl->end();
  Pt2 head = run.front().reversed ? gf->end() : gf->start();
  return tail.near(head, 1e-6);
}

double polygonArea(const std::vector<Pt2>& pts) {
  double a = 0;
  for (size_t i = 0; i < pts.size(); ++i) a += pts[i].cross(pts[(i + 1) % pts.size()]);
  return a / 2;
}

bool insidePolygon(const std::vector<Pt2>& pts, Pt2 q) {
  bool in = false;
  for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
    const Pt2& a = pts[i];
    const Pt2& b = pts[j];
    if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = !in;
  }
  return in;
}

}  // namespace

std::vector<int> Sketch::offset(const std::vector<int>& ids, double distance, std::optional<Pt2> side) {
  std::vector<int> out;
  if (ids.empty() || std::abs(distance) < 1e-12) return out;
  std::vector<int> curves;
  for (int id : ids) {
    const Geometry* g = geometry(id);
    if (g && (g->type == GeomType::Line || g->isCircular() || g->isElliptical() || g->isSpline())) curves.push_back(id);
  }
  auto runs = orderChains(*this, curves);
  for (auto& run : runs) {
    double d = distance;
    if (runIsClosed(*this, run)) {
      // closed loop: inside/outside of the whole loop decides (typed without a side: positive = outward)
      std::vector<Pt2> poly = runPolyline(*this, run);
      bool ccw = polygonArea(poly) > 0;  // ccw: left of the traversal = inside
      bool inward = side ? insidePolygon(poly, *side) : distance < 0;
      d = (inward == ccw) ? std::abs(distance) : -std::abs(distance);
    } else if (side) {
      // open chain / single curve: the run curve nearest the cursor decides, with its own direction
      const ChainItem* nearest = &run.front();
      double best = std::numeric_limits<double>::infinity();
      for (const ChainItem& item : run) {
        double dist = geometry(item.id)->distanceTo(*side);
        if (dist < best) {
          best = dist;
          nearest = &item;
        }
      }
      double s = sideOfCurve(*geometry(nearest->id), nearest->reversed, *side);
      d = s < 0 ? -std::abs(distance) : std::abs(distance);
    }
    std::vector<Geometry> offs;
    bool degenerate = false;
    for (const ChainItem& item : run) {
      auto o = offsetCurve(*geometry(item.id), item.reversed, d);
      if (!o) {
        degenerate = true;
        break;
      }
      offs.push_back(*o);
    }
    if (degenerate) continue;  // e.g. an inward offset larger than an arc radius: no geometry
    // trim/extend corners between consecutive offsets
    for (size_t i = 0; i + 1 < run.size(); ++i) {
      Geometry& a = offs[i];
      Geometry& b = offs[i + 1];
      auto exactKind = [](const Geometry& x) { return x.type == GeomType::Line || x.isCircular(); };
      if (!exactKind(a) || !exactKind(b)) continue;  // sampled offsets (ellipse, spline): no corner trim
      const Geometry* ga = geometry(run[i].id);
      Pt2 junction = run[i].reversed ? ga->start() : ga->end();
      std::vector<Pt2> cand;
      if (a.type == GeomType::Line && b.type == GeomType::Line) {
        if (auto p = intersectLines(a.start(), a.direction(), b.start(), b.direction())) cand.push_back(*p);
      } else if (a.type == GeomType::Line) {
        cand = intersectLineCircle(a.start(), a.direction(), b.center(), b.radius());
      } else if (b.type == GeomType::Line) {
        cand = intersectLineCircle(b.start(), b.direction(), a.center(), a.radius());
      } else {
        cand = intersectCircles(a.center(), a.radius(), b.center(), b.radius());
      }
      if (cand.empty()) continue;
      Pt2 best = cand.front();
      for (const Pt2& p : cand)
        if (p.distance(junction) < best.distance(junction)) best = p;
      // a's tail end and b's head start move to `best`
      if (run[i].reversed) a.setStart(best); else a.setEnd(best);
      if (run[i + 1].reversed) b.setEnd(best); else b.setStart(best);
    }
    // closed chain: also join last -> first
    if (run.size() > 2) {
      const Geometry* gl = geometry(run.back().id);
      const Geometry* gf = geometry(run.front().id);
      Pt2 tail = run.back().reversed ? gl->start() : gl->end();
      Pt2 head = run.front().reversed ? gf->end() : gf->start();
      if (tail.near(head, 1e-6)) {
        Geometry& a = offs.back();
        Geometry& b = offs.front();
        std::vector<Pt2> cand;
        if (!(a.type == GeomType::Line || a.isCircular()) || !(b.type == GeomType::Line || b.isCircular())) {
          // sampled offsets: corners are not trimmed
        } else if (a.type == GeomType::Line && b.type == GeomType::Line) {
          if (auto p = intersectLines(a.start(), a.direction(), b.start(), b.direction())) cand.push_back(*p);
        } else if (a.type == GeomType::Line) {
          cand = intersectLineCircle(a.start(), a.direction(), b.center(), b.radius());
        } else if (b.type == GeomType::Line) {
          cand = intersectLineCircle(b.start(), b.direction(), a.center(), a.radius());
        } else {
          cand = intersectCircles(a.center(), a.radius(), b.center(), b.radius());
        }
        if (!cand.empty()) {
          Pt2 best = cand.front();
          for (const Pt2& p : cand)
            if (p.distance(tail) < best.distance(tail)) best = p;
          if (run.back().reversed) a.setStart(best); else a.setEnd(best);
          if (run.front().reversed) b.setEnd(best); else b.setStart(best);
        }
      }
    }
    // an offset larger than the shape collapses or flips trimmed lines: no geometry for this run
    for (size_t i = 0; i < offs.size(); ++i) {
      const Geometry& o = offs[i];
      if (o.type != GeomType::Line) continue;
      const Geometry* src = geometry(run[i].id);
      if (o.length() < 1e-9 || (o.end() - o.start()).dot(src->end() - src->start()) <= 0) degenerate = true;
    }
    if (degenerate) continue;
    std::vector<int> newIds;
    for (Geometry& o : offs) {
      o.id = -1;
      o.projected = false;
      o.blocked = false;
      o.source = core::ShapeRef{};
      newIds.push_back(addGeometry(o));
      if (o.isSpline() && run.size() == 1 && o.start().near(o.end(), 1e-9))  // offset of a closed curve
        addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {newIds.back(), PointPos::End}, .b = {newIds.back(), PointPos::Start}, .internal = true});
    }
    // coincidences along the offset chain
    for (size_t i = 0; i + 1 < newIds.size(); ++i) {
      const Geometry* a = geometry(newIds[i]);
      const Geometry* b = geometry(newIds[i + 1]);
      PointPos pa = run[i].reversed ? PointPos::Start : PointPos::End;
      PointPos pb = run[i + 1].reversed ? PointPos::End : PointPos::Start;
      if (a->point(pa).near(b->point(pb), 1e-6))
        addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {newIds[i], pa}, .b = {newIds[i + 1], pb}, .internal = true});
    }
    if (newIds.size() > 2) {
      const Geometry* a = geometry(newIds.back());
      const Geometry* b = geometry(newIds.front());
      PointPos pa = run.back().reversed ? PointPos::Start : PointPos::End;
      PointPos pb = run.front().reversed ? PointPos::End : PointPos::Start;
      if (a->point(pa).near(b->point(pb), 1e-6))
        addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {newIds.back(), pa}, .b = {newIds.front(), pb}, .internal = true});
    }
    out.insert(out.end(), newIds.begin(), newIds.end());
  }
  return out;
}

std::vector<int> Sketch::mirror(const std::vector<int>& ids, Pt2 a, Pt2 d) {
  std::vector<int> out;
  std::map<int, int> idMap;
  for (int id : ids) {
    const Geometry* g = geometry(id);
    if (!g) continue;
    Geometry m = *g;
    m.mirror(a, d);
    m.id = -1;
    m.projected = false;
    m.blocked = false;
    m.source = core::ShapeRef{};
    int nid = addGeometry(m);
    idMap[id] = nid;
    out.push_back(nid);
  }
  // copy constraints among the mirrored set
  std::vector<Constraint> copies;
  for (const Constraint& c : constraints_) {
    if (c.isDimensional() || c.type == ConstraintType::Fix) continue;
    bool allIn = true;
    for (const GeoRef* r : {&c.a, &c.b, &c.c})
      if (r->isValid() && !idMap.count(r->geo)) allIn = false;
    if (!allIn || !c.a.isValid()) continue;
    Constraint copy = c;
    copy.id = -1;
    auto mapRef = [&](GeoRef& r) {
      if (!r.isValid()) return;
      const Geometry* og = geometry(r.geo);
      r.geo = idMap[r.geo];
      // arcs reverse orientation under mirroring
      if (og && (og->type == GeomType::Arc || og->type == GeomType::EllipseArc)) {
        if (r.pos == PointPos::Start) r.pos = PointPos::End;
        else if (r.pos == PointPos::End) r.pos = PointPos::Start;
      }
    };
    mapRef(copy.a);
    mapRef(copy.b);
    mapRef(copy.c);
    // horizontal/vertical stay the same only for axis-aligned mirror lines
    if (copy.type == ConstraintType::Horizontal || copy.type == ConstraintType::Vertical) {
      Pt2 u = d.normalized();
      bool axisAligned = std::abs(u.x) < 1e-9 || std::abs(u.y) < 1e-9;
      if (!axisAligned) continue;
    }
    copies.push_back(copy);
  }
  for (Constraint& c : copies) addConstraint(c);
  return out;
}

std::vector<int> Sketch::mirror(const std::vector<int>& ids, int mirrorLineId, bool constrain) {
  const Geometry* line = geometry(mirrorLineId);
  if (!line || line->type != GeomType::Line) return {};
  Pt2 a = line->start(), d = line->direction();
  std::vector<int> filtered;
  for (int id : ids)
    if (id != mirrorLineId) filtered.push_back(id);
  std::vector<int> out = mirror(filtered, a, d);
  if (!constrain) return out;
  for (size_t i = 0; i < filtered.size() && i < out.size(); ++i) {
    const Geometry* og = geometry(filtered[i]);
    const Geometry* ng = geometry(out[i]);
    if (!og || !ng) continue;
    switch (og->type) {
      case GeomType::Point:
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Center}, .b = {out[i], PointPos::Center}, .c = {mirrorLineId, PointPos::None}});
        break;
      case GeomType::Line:
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Start}, .b = {out[i], PointPos::Start}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::End}, .b = {out[i], PointPos::End}, .c = {mirrorLineId, PointPos::None}});
        break;
      case GeomType::Circle:
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Center}, .b = {out[i], PointPos::Center}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Equal, .a = {filtered[i], PointPos::None}, .b = {out[i], PointPos::None}});
        break;
      case GeomType::Arc:
      case GeomType::EllipseArc:
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Start}, .b = {out[i], PointPos::End}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::End}, .b = {out[i], PointPos::Start}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Center}, .b = {out[i], PointPos::Center}, .c = {mirrorLineId, PointPos::None}});
        if (og->type == GeomType::EllipseArc)
          addConstraint(Constraint{.type = ConstraintType::Equal, .a = {filtered[i], PointPos::None}, .b = {out[i], PointPos::None}});
        break;
      case GeomType::Ellipse:
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Center}, .b = {out[i], PointPos::Center}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Equal, .a = {filtered[i], PointPos::None}, .b = {out[i], PointPos::None}});
        break;
      case GeomType::Spline:
      case GeomType::FitSpline:
        for (int k = 0; k < og->vertexCount() && k < ng->vertexCount(); ++k) {
          GeoRef ra{filtered[i], PointPos::Vertex, k}, rb{out[i], PointPos::Vertex, k};
          if (k == 0) ra.pos = rb.pos = PointPos::Start;
          else if (k == og->vertexCount() - 1) ra.pos = rb.pos = PointPos::End;
          if (ra.pos != PointPos::Vertex) ra.index = rb.index = -1;
          addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = ra, .b = rb, .c = {mirrorLineId, PointPos::None}});
        }
        break;
      case GeomType::Text: break;
    }
  }
  return out;
}

namespace {

/// Shorten a line/arc so that the end on the far side of `keepNear` ends at `tangentPt`.
/// Returns the position that moved.
PointPos trimToPoint(Geometry& g, Pt2 tangentPt, Pt2 keepNear) {
  if (g.type == GeomType::Line) {
    double tT = g.paramOf(tangentPt), tK = g.paramOf(keepNear);
    if (tK >= tT) {
      g.setStart(tangentPt);
      return PointPos::Start;
    }
    g.setEnd(tangentPt);
    return PointPos::End;
  }
  if (g.type == GeomType::Arc) {
    double aT = g.paramOf(tangentPt), aK = g.paramOf(keepNear);
    if (aK >= aT) {
      g.setAngles(aT, g.endAngle());
      return PointPos::Start;
    }
    g.setAngles(g.startAngle(), aT);
    return PointPos::End;
  }
  return PointPos::None;
}

}  // namespace

std::optional<int> Sketch::fillet(int id1, Pt2 near1, int id2, Pt2 near2, double radius) {
  Geometry* g1 = geometry(id1);
  Geometry* g2 = geometry(id2);
  if (!g1 || !g2 || id1 == id2 || radius <= 0) return std::nullopt;
  if (g1->type == GeomType::Point || g2->type == GeomType::Point) return std::nullopt;
  if (g1->type == GeomType::Circle && g2->type == GeomType::Circle) return std::nullopt;

  // candidate centres: intersections of the curves offset by r on both sides
  auto offsetsOf = [&](const Geometry& g, double r) {
    std::vector<Geometry> v;
    if (g.type == GeomType::Line) {
      v.push_back(*offsetCurve(g, false, r));
      v.push_back(*offsetCurve(g, false, -r));
    } else {
      Geometry a = g;
      a.setRadius(g.radius() + r);
      v.push_back(a);
      if (g.radius() - r > 1e-6) {
        Geometry b = g;
        b.setRadius(g.radius() - r);
        v.push_back(b);
      }
    }
    return v;
  };
  std::vector<Pt2> centers;
  for (const Geometry& oa : offsetsOf(*g1, radius))
    for (const Geometry& ob : offsetsOf(*g2, radius)) {
      std::vector<Pt2> cand;
      if (oa.type == GeomType::Line && ob.type == GeomType::Line) {
        if (auto p = intersectLines(oa.start(), oa.direction(), ob.start(), ob.direction())) cand.push_back(*p);
      } else if (oa.type == GeomType::Line) {
        cand = intersectLineCircle(oa.start(), oa.direction(), ob.center(), ob.radius());
      } else if (ob.type == GeomType::Line) {
        cand = intersectLineCircle(ob.start(), ob.direction(), oa.center(), oa.radius());
      } else {
        cand = intersectCircles(oa.center(), oa.radius(), ob.center(), ob.radius());
      }
      centers.insert(centers.end(), cand.begin(), cand.end());
    }
  if (centers.empty()) return std::nullopt;
  // The fillet lies in the quadrant of the pick points: the centre must be on the same side
  // of curve 1 as near2 and on the same side of curve 2 as near1; among those, nearest to the picks.
  auto sideOf = [](const Geometry& g, Pt2 p) {
    if (g.type == GeomType::Line) return signedDistanceToLine(g.start(), g.direction(), p) >= 0 ? 1 : -1;
    return p.distance(g.center()) >= g.radius() ? 1 : -1;
  };
  Pt2 mid = (near1 + near2) * 0.5;
  std::optional<Pt2> best;
  double bestScore = std::numeric_limits<double>::infinity();
  for (const Pt2& c : centers) {
    double score = c.distance(mid);
    bool okSide = sideOf(*g1, c) == sideOf(*g1, near2) && sideOf(*g2, c) == sideOf(*g2, near1);
    if (!okSide) score += 1e6;
    if (score < bestScore) {
      bestScore = score;
      best = c;
    }
  }
  if (!best) return std::nullopt;
  Pt2 center = *best;
  // tangent points: closest points on the *infinite* curves
  auto footOn = [&](const Geometry& g) {
    if (g.type == GeomType::Line) {
      double t = projectParam(g.start(), g.end() - g.start(), center);
      return g.start() + (g.end() - g.start()) * t;
    }
    return g.center() + (center - g.center()).normalized() * g.radius();
  };
  Pt2 t1 = footOn(*g1), t2 = footOn(*g2);
  // where the trimmed ends were (the "virtual sharp" when they met at a corner)
  auto endNear = [](const Geometry& g, Pt2 keepNear, Pt2 cut) { return g.paramOf(keepNear) >= g.paramOf(cut) ? g.start() : g.end(); };
  const Pt2 old1 = endNear(*g1, near1, t1), old2 = endNear(*g2, near2, t2);
  PointPos moved1 = trimToPoint(*g1, t1, near1);
  PointPos moved2 = trimToPoint(*g2, t2, near2);
  // typed sizes created driving dimensions on the lines: measure to the virtual sharp (Fusion)
  retargetTrimmedDimensions(id1, moved1, id2);
  retargetTrimmedDimensions(id2, moved2, id1);
  // the fillet arc: minor arc from t1 to t2 around the centre
  double a1 = (t1 - center).angle(), a2 = (t2 - center).angle();
  Geometry arc;
  if (normalizeAngle(a2 - a1) <= kPi) arc = Geometry::makeArc(center, radius, a1, a2);
  else arc = Geometry::makeArc(center, radius, a2, a1);
  bool startIsT1 = arc.start().near(t1, 1e-6);
  arc.construction = g1->construction && g2->construction;
  int arcId = addGeometry(arc);
  g1 = geometry(id1);  // addGeometry may reallocate
  g2 = geometry(id2);
  GeoRef arcStart{arcId, PointPos::Start}, arcEnd{arcId, PointPos::End};
  GeoRef c1{id1, moved1}, c2{id2, moved2};
  if (g1->type == GeomType::Circle) c1 = {id1, PointPos::None};
  if (g2->type == GeomType::Circle) c2 = {id2, PointPos::None};
  // drop the coincidence between the two trimmed endpoints if they shared a corner
  if (moved1 != PointPos::None && moved2 != PointPos::None) {
    if (const Constraint* co = findConstraint(ConstraintType::Coincident, {id1, moved1}, {id2, moved2})) removeConstraint(co->id);
  }
  preserveTrimmedCorner(id1, moved1, old1, id2, moved2, old2);
  auto tangentTo = [&](const GeoRef& arcPt, const GeoRef& curvePt) {
    if (curvePt.pos == PointPos::None) {
      addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {arcId, PointPos::None}, .b = {curvePt.geo, PointPos::None}, .internal = true});
      addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = arcPt, .b = curvePt, .internal = true});
    } else {
      addConstraint(Constraint{.type = ConstraintType::Tangent, .a = arcPt, .b = curvePt, .internal = true});
    }
  };
  tangentTo(startIsT1 ? arcStart : arcEnd, c1);
  tangentTo(startIsT1 ? arcEnd : arcStart, c2);
  return arcId;
}

std::vector<std::string> Sketch::retargetTrimmedDimensions(int lineId, PointPos movedEnd, int otherId) {
  std::vector<std::string> demoted;
  const Geometry* g = geometry(lineId);
  if (!g || g->type != GeomType::Line || movedEnd == PointPos::None) return demoted;
  const Geometry* other = geometry(otherId);
  const bool otherLine = other && other->type == GeomType::Line;
  const PointPos farEnd = movedEnd == PointPos::Start ? PointPos::End : PointPos::Start;
  auto dirOf = [](const Geometry& l) { return l.direction().normalized(); };
  auto perpendicular = [&](const Geometry& a, const Geometry& b) { return std::abs(dirOf(a).dot(dirOf(b))) < 1e-6; };
  auto parallel = [&](const Geometry& a, const Geometry& b) {
    Pt2 da = dirOf(a), db = dirOf(b);
    return std::abs(da.x * db.y - da.y * db.x) < 1e-6;
  };
  for (Constraint& c : constraints_) {
    if (!c.isDimensional() || c.driven || !c.references(lineId)) continue;
    const bool aWholeLine = c.a.geo == lineId && c.a.pos == PointPos::None;
    const bool aMoved = c.a.geo == lineId && c.a.pos == movedEnd;
    const bool bMoved = c.b.geo == lineId && c.b.pos == movedEnd;
    bool handled = false;
    switch (c.type) {
      case ConstraintType::Distance:
        if (aWholeLine && !c.b.isValid()) {
          // line length -> far end to the adjacent line: equal to the length at a square corner
          if (otherLine && perpendicular(*g, *other)) {
            c.a = {lineId, farEnd};
            c.b = {otherId, PointPos::None};
            handled = true;
          }
        } else if (aMoved || bMoved) {
          // point-line distance anchored at the trimmed end -> distance between the two lines
          GeoRef& pt = aMoved ? c.a : c.b;
          const GeoRef& ref = aMoved ? c.b : c.a;
          const Geometry* refLine = (ref.isValid() && ref.pos == PointPos::None) ? geometry(ref.geo) : nullptr;
          if (refLine && refLine->type == GeomType::Line && otherLine && parallel(*refLine, *other)) {
            pt = {otherId, PointPos::None};
          }
          // otherwise it stays on the trimmed end and preserveTrimmedCorner() moves it to the
          // virtual sharp, where it keeps driving (Fusion measures to the virtual sharp)
          handled = true;
        } else {
          handled = true;  // references the far end only: unaffected
        }
        break;
      case ConstraintType::DistanceX:
      case ConstraintType::DistanceY: {
        const bool x = c.type == ConstraintType::DistanceX;
        if (aWholeLine && !c.b.isValid()) {
          // extent of the line along X equals the distance of its far end to a vertical adjacent line
          const bool otherAxisAligned = otherLine && (x ? std::abs(dirOf(*other).x) : std::abs(dirOf(*other).y)) < 1e-6;
          if (otherAxisAligned && c.value >= 0) {
            c.type = ConstraintType::Distance;
            c.a = {lineId, farEnd};
            c.b = {otherId, PointPos::None};
            handled = true;
          }
        } else {
          handled = true;  // far end only, or anchored at the trimmed end (moved to the virtual sharp)
        }
        break;
      }
      default:
        handled = true;  // Angle / Radius / Diameter do not depend on the trimmed end
        break;
    }
    if (!handled) {
      c.driven = true;
      demoted.push_back(c.name);
      demoted_.push_back(c.name);
    }
  }
  return demoted;
}

std::vector<std::string> Sketch::takeDemotedDimensions() {
  std::vector<std::string> out;
  out.swap(demoted_);
  return out;
}

std::vector<std::string> Sketch::takeModifyNotes() {
  std::vector<std::string> out;
  out.swap(notes_);
  return out;
}

int Sketch::preserveTrimmedCorner(int id1, PointPos m1, Pt2 old1, int id2, PointPos m2, Pt2 old2) {
  struct End {
    int id;
    PointPos pos;
    Pt2 old;
    bool pinned = false;     // a constraint still references the trimmed end
    bool lengthDep = false;  // a constraint depends on the (now shorter) line's extent
    int sharp = -1;
  };
  std::vector<End> ends;
  for (auto [id, pos, old] : {std::tuple{id1, m1, old1}, std::tuple{id2, m2, old2}}) {
    const Geometry* g = geometry(id);
    if (!g || pos == PointPos::None || (g->type != GeomType::Line && g->type != GeomType::Arc)) continue;
    ends.push_back(End{.id = id, .pos = pos, .old = old});
  }
  if (ends.empty()) return -1;
  auto refsEnd = [](const GeoRef& r, const End& e) { return r.geo == e.id && r.pos == e.pos; };
  for (End& e : ends) {
    const bool line = geometry(e.id)->type == GeomType::Line;
    for (const Constraint& c : constraints_) {
      if (refsEnd(c.a, e) || refsEnd(c.b, e) || refsEnd(c.c, e)) e.pinned = true;
      if (!line) continue;
      for (const GeoRef* r : {&c.a, &c.b, &c.c})
        if (r->geo == e.id && r->pos == PointPos::Mid) e.lengthDep = true;
      if (c.type == ConstraintType::Equal && ((c.a.geo == e.id && c.a.pos == PointPos::None) || (c.b.geo == e.id && c.b.pos == PointPos::None)))
        e.lengthDep = true;
      if (c.type == ConstraintType::Midpoint && c.b.geo == e.id && c.b.pos == PointPos::None) e.lengthDep = true;
    }
  }
  bool needed = false;
  for (const End& e : ends) needed = needed || e.pinned || e.lengthDep;
  if (!needed) return -1;

  // the virtual sharp(s): one shared point when the trimmed ends met at a corner
  auto makeSharp = [&](Pt2 at) {
    Geometry pt = Geometry::makePoint(at);
    pt.construction = true;
    return addGeometry(pt);
  };
  if (ends.size() == 2 && ends[0].old.near(ends[1].old, 1e-6)) {
    int s = makeSharp(ends[0].old);
    ends[0].sharp = ends[1].sharp = s;
  } else {
    for (End& e : ends)
      if (e.pinned || e.lengthDep) e.sharp = makeSharp(e.old);
  }
  // move the pinning constraints onto the sharp; a constraint collapsing onto one point is dropped
  std::vector<int> drop;
  for (Constraint& c : constraints_) {
    bool changed = false;
    for (GeoRef* r : {&c.a, &c.b, &c.c})
      for (const End& e : ends)
        if (e.sharp >= 0 && refsEnd(*r, e)) {
          *r = GeoRef{e.sharp, PointPos::Center};
          changed = true;
        }
    // a tangency at a shared endpoint cannot hold on a point: keep the connection only
    if (changed && c.type == ConstraintType::Tangent && c.a.isPoint() && c.b.isPoint()) c.type = ConstraintType::Coincident;
    if (changed && c.a == c.b && c.a.isValid()) drop.push_back(c.id);
  }
  for (int cid : drop) removeConstraint(cid);
  std::set<int> sharps;
  for (const End& e : ends)
    if (e.sharp >= 0 && sharps.insert(e.sharp).second) {
      for (const End& on : ends)
        if (on.sharp == e.sharp)
          addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {e.sharp, PointPos::Center}, .b = {on.id, PointPos::None}, .internal = true});
    }
  // length-dependent constraints: a construction line from the far end to the sharp stands in for
  // the untrimmed line (same direction, so Mid / Equal / Midpoint keep their meaning)
  for (const End& e : ends) {
    if (!e.lengthDep || e.sharp < 0) continue;
    const Geometry* g = geometry(e.id);
    const PointPos farEnd = e.pos == PointPos::Start ? PointPos::End : PointPos::Start;
    Geometry ghost = e.pos == PointPos::Start ? Geometry::makeLine(e.old, g->end()) : Geometry::makeLine(g->start(), e.old);
    ghost.construction = true;
    const int gid = addGeometry(ghost);
    for (Constraint& c : constraints_) {
      bool lengthUse = (c.type == ConstraintType::Equal) || (c.type == ConstraintType::Midpoint);
      for (GeoRef* r : {&c.a, &c.b, &c.c}) {
        if (r->geo != e.id) continue;
        if (r->pos == PointPos::Mid) r->geo = gid;
        else if (r->pos == PointPos::None && lengthUse) r->geo = gid;
      }
    }
    addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {gid, farEnd}, .b = {e.id, farEnd}, .internal = true});
    addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {gid, e.pos}, .b = {e.sharp, PointPos::Center}, .internal = true});
  }
  notes_.push_back("the corner's constraints now apply to a construction point at the old corner");
  return *sharps.begin();
}

std::optional<int> Sketch::split(int curveId, Pt2 at) {
  Geometry* g = geometry(curveId);
  if (g && g->isSpline()) return splitSpline(curveId, at);
  if (!g || (g->type != GeomType::Line && g->type != GeomType::Arc)) return std::nullopt;
  Pt2 p = g->closestPoint(at);
  if (p.near(g->start(), 1e-6) || p.near(g->end(), 1e-6)) return std::nullopt;
  const Geometry orig = *g;
  Geometry second = orig;
  second.id = -1;
  second.setStart(p);
  int newId = addGeometry(second);
  g = geometry(curveId);
  g->setEnd(p);
  redistributeConstraints(*this, curveId, newId, PointPos::End, PointPos::End);
  followNearestPiece(*this, curveId, newId);
  addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {curveId, PointPos::End}, .b = {newId, PointPos::Start}, .internal = true});
  if (orig.type == GeomType::Arc) {
    addConstraint(Constraint{.type = ConstraintType::Concentric, .a = {curveId, PointPos::None}, .b = {newId, PointPos::None}, .internal = true});
    addConstraint(Constraint{.type = ConstraintType::Equal, .a = {curveId, PointPos::None}, .b = {newId, PointPos::None}, .internal = true});
  }
  return newId;
}

std::optional<int> Sketch::splitSpline(int curveId, Pt2 at) {
  Geometry* g = geometry(curveId);
  if (!g || !g->isSpline()) return std::nullopt;
  const Geometry orig = *g;
  auto [u0, u1] = orig.paramRange();
  double u = orig.paramOf(at);
  const double eps = 1e-4 * std::max(1.0, u1 - u0);
  auto replace = [&](int id, Geometry piece) {
    piece.id = id;
    piece.projected = orig.projected;
    piece.blocked = orig.blocked;
    piece.source = orig.source;
    *geometry(id) = piece;
  };
  std::vector<int> mapA, mapB;
  if (isClosedSpline(orig)) {
    // one break on a closed spline moves its seam there (break again to get two pieces)
    if (u - u0 < eps || u1 - u < eps) return std::nullopt;
    dropPointConstraints(*this, curveId, PointPos::Start);
    dropPointConstraints(*this, curveId, PointPos::End);
    replace(curveId, splinePiece(orig, u, u + (u1 - u0), &mapA));
    remapVertexConstraints(*this, curveId, {curveId}, {mapA});
    addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {curveId, PointPos::End}, .b = {curveId, PointPos::Start}, .internal = true});
    return curveId;
  }
  if (u - u0 < eps || u1 - u < eps) return std::nullopt;
  int newId = addGeometry(splinePiece(orig, u, u1, &mapB));
  replace(curveId, splinePiece(orig, u0, u, &mapA));
  redistributeConstraints(*this, curveId, newId, PointPos::End, PointPos::End);
  followNearestPiece(*this, curveId, newId);
  remapVertexConstraints(*this, curveId, {curveId, newId}, {mapA, mapB});
  addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {curveId, PointPos::End}, .b = {newId, PointPos::Start}, .internal = true});
  return newId;
}

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

std::vector<std::string> Sketch::evaluateExpressions(const ExternalEval& external) {
  std::vector<std::string> errors;
  for (Constraint& c : constraints_) {
    if (!c.isDimensional() || c.expression.empty()) continue;
    std::string err;
    auto v = external ? external(c.expression, c.type == ConstraintType::Angle, &err) : std::nullopt;
    if (!v) {
      errors.push_back(std::format("{}: {}", c.name, err.empty() ? "cannot evaluate '" + c.expression + "'" : err));
      continue;
    }
    c.value = *v;
  }
  return errors;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

QJsonObject Sketch::toJson() const {
  QJsonObject o;
  QJsonArray geos, cons;
  for (const auto& g : geometries_) geos.append(g.toJson());
  for (const auto& c : constraints_) cons.append(c.toJson());
  o["geometry"] = geos;
  o["constraints"] = cons;
  o["nextGeoId"] = nextGeoId_;
  o["nextConstraintId"] = nextConstraintId_;
  return o;
}

void Sketch::fromJson(const QJsonObject& o) {
  clear();
  for (const auto& v : o["geometry"].toArray()) {
    Geometry g = Geometry::fromJson(v.toObject());
    if (g.id < 0) g.id = nextGeoId_;
    nextGeoId_ = std::max(nextGeoId_, g.id + 1);
    geometries_.push_back(std::move(g));
  }
  for (const auto& v : o["constraints"].toArray()) {
    Constraint c = Constraint::fromJson(v.toObject());
    if (c.id < 0) c.id = nextConstraintId_;
    nextConstraintId_ = std::max(nextConstraintId_, c.id + 1);
    constraints_.push_back(std::move(c));
  }
  nextGeoId_ = std::max(nextGeoId_, o["nextGeoId"].toInt(nextGeoId_));
  nextConstraintId_ = std::max(nextConstraintId_, o["nextConstraintId"].toInt(nextConstraintId_));
}

void Sketch::clear() {
  geometries_.clear();
  constraints_.clear();
  nextGeoId_ = 1;
  nextConstraintId_ = 1;
  last_ = SolveResult{};
  dragging_ = false;
}

}  // namespace fabricad::sketch
