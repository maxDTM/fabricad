// FabriCAD sketch: 2D sketch model implementation (geometry, constraints, queries, operations).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"

#include "core/Json.h"
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
  if (g.type == GeomType::Arc) g.normalize();
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
  if (g->type == GeomType::Circle && ref.pos != PointPos::Center) return std::nullopt;
  if (g->type == GeomType::Line && ref.pos == PointPos::Center) return g->midpoint();
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
    case ConstraintType::Angle: {
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
    case ConstraintType::Angle: {
      const Geometry* gb = c.b.isValid() ? geometry(c.b.geo) : nullptr;
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
  if (g->type == GeomType::Arc) g->normalize();
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
    if (exclude.count(g.id)) continue;
    for (PointPos pos : g.pointPositions()) {
      Pt2 p = g.point(pos);
      double d = p.distance(q);
      if (d <= tol && (!best || d < best->distance)) best = SketchHit{{g.id, pos}, p, d};
    }
  }
  return best;
}

std::optional<SketchHit> Sketch::hitCurve(Pt2 q, double tol, const std::set<int>& exclude) const {
  std::optional<SketchHit> best;
  for (const auto& g : geometries_) {
    if (exclude.count(g.id) || !g.isCurve()) continue;
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
    if (o.id == curveId || !o.isCurve()) continue;
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
    if (!includeConstruction && g.construction) continue;
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
    if (g.type == GeomType::Line || g.type == GeomType::Arc) pts = {g.start(), g.end()};
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
  if (g.type == GeomType::Circle) {
    if (hits.size() < 2) {
      br.whole = true;
      br.circleClosed = true;
      return br;
    }
    std::vector<ParamHit> as = hits;
    for (auto& h : as) h.param = normalizeAngle(h.param);
    std::sort(as.begin(), as.end(), [](const ParamHit& a, const ParamHit& b) { return a.param < b.param; });
    double aAt = normalizeAngle((at - g.center()).angle());
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
  double aAt = a0 + std::clamp(normalizeAngle((at - g.center()).angle() - a0), 0.0, a1 - a0);
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

}  // namespace

std::optional<Geometry> Sketch::trimPiece(int curveId, Pt2 at) const {
  const Geometry* g = geometry(curveId);
  if (!g || !g->isCurve()) return std::nullopt;
  TrimBracket br = computeBracket(*this, *g, at);
  if (br.whole) return *g;
  Geometry piece = *g;
  piece.id = -1;
  if (g->type == GeomType::Line) {
    piece = Geometry::makeLine(g->pointAt(br.lo), g->pointAt(br.hi));
  } else {
    piece = Geometry::makeArc(g->center(), g->radius(), br.lo, br.hi);
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
  auto onCurve = [&](int geo, PointPos pos, int other) {
    if (other < 0) return;
    addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {geo, pos}, .b = {other, PointPos::None}, .internal = true});
  };

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

  if (orig.type == GeomType::Circle) {
    // the remaining arc runs ccw from the hi intersection round to the lo intersection
    Geometry arc = Geometry::makeArc(orig.center(), orig.radius(), br.hi, br.lo);
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
  Geometry second = Geometry::makeArc(orig.center(), orig.radius(), br.hi, a1);
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
    if (g->type == GeomType::Circle || g->type == GeomType::Point) return std::pair<Pt2, Pt2>{s, s};
    return rev ? std::pair<Pt2, Pt2>{e, s} : std::pair<Pt2, Pt2>{s, e};
  };
  while (!left.empty()) {
    int first = *left.begin();
    left.erase(first);
    std::deque<ChainItem> run{{first, false}};
    const Geometry* fg = sk.geometry(first);
    if (fg->type == GeomType::Circle) {
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
        if (g->type == GeomType::Circle || g->type == GeomType::Point) continue;
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

Geometry offsetCurve(const Geometry& g, bool reversed, double d) {
  // d > 0 = to the left of the traversal direction
  Geometry o = g;
  switch (g.type) {
    case GeomType::Line: {
      Pt2 n = g.direction().perp() * (reversed ? -1.0 : 1.0);
      o.translate(n * d);
      break;
    }
    case GeomType::Circle: o.setRadius(std::max(1e-6, g.radius() + d)); break;  // positive = outward
    case GeomType::Arc: {
      // ccw traversal: left = towards centre
      double r = reversed ? g.radius() + d : g.radius() - d;
      if (r <= 1e-6) r = 1e-6;
      o.setRadius(r);
      break;
    }
    case GeomType::Point: break;
  }
  return o;
}

}  // namespace

std::vector<int> Sketch::offset(const std::vector<int>& ids, double distance, std::optional<Pt2> side) {
  std::vector<int> out;
  if (ids.empty() || std::abs(distance) < 1e-12) return out;
  std::vector<int> curves;
  for (int id : ids) {
    const Geometry* g = geometry(id);
    if (g && g->isCurve()) curves.push_back(id);
  }
  auto runs = orderChains(*this, curves);
  for (auto& run : runs) {
    double d = distance;
    if (side) {
      // sign so that the first curve moves towards `side`
      const Geometry* g0 = geometry(run.front().id);
      double s = 0;
      if (g0->type == GeomType::Line) s = signedDistanceToLine(g0->start(), g0->direction(), *side) * (run.front().reversed ? -1 : 1);
      else if (g0->type == GeomType::Circle) s = side->distance(g0->center()) - g0->radius();
      else s = (side->distance(g0->center()) - g0->radius()) * (run.front().reversed ? 1 : -1);
      if (s < 0) d = -std::abs(distance);
      else d = std::abs(distance);
    }
    std::vector<Geometry> offs;
    for (const ChainItem& item : run) offs.push_back(offsetCurve(*geometry(item.id), item.reversed, d));
    // trim/extend corners between consecutive offsets
    for (size_t i = 0; i + 1 < run.size(); ++i) {
      Geometry& a = offs[i];
      Geometry& b = offs[i + 1];
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
        if (a.type == GeomType::Line && b.type == GeomType::Line) {
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
    std::vector<int> newIds;
    for (Geometry& o : offs) {
      o.id = -1;
      o.projected = false;
      o.blocked = false;
      o.source = core::ShapeRef{};
      newIds.push_back(addGeometry(o));
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
      if (og && og->type == GeomType::Arc) {
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
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Start}, .b = {out[i], PointPos::End}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::End}, .b = {out[i], PointPos::Start}, .c = {mirrorLineId, PointPos::None}});
        addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {filtered[i], PointPos::Center}, .b = {out[i], PointPos::Center}, .c = {mirrorLineId, PointPos::None}});
        break;
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
      v.push_back(offsetCurve(g, false, r));
      v.push_back(offsetCurve(g, false, -r));
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
  PointPos moved1 = trimToPoint(*g1, t1, near1);
  PointPos moved2 = trimToPoint(*g2, t2, near2);
  // the fillet arc: minor arc from t1 to t2 around the centre
  double a1 = (t1 - center).angle(), a2 = (t2 - center).angle();
  Geometry arc;
  if (normalizeAngle(a2 - a1) <= kPi) arc = Geometry::makeArc(center, radius, a1, a2);
  else arc = Geometry::makeArc(center, radius, a2, a1);
  bool startIsT1 = arc.start().near(t1, 1e-6);
  arc.construction = g1->construction && g2->construction;
  int arcId = addGeometry(arc);
  GeoRef arcStart{arcId, PointPos::Start}, arcEnd{arcId, PointPos::End};
  GeoRef c1{id1, moved1}, c2{id2, moved2};
  if (g1->type == GeomType::Circle) c1 = {id1, PointPos::None};
  if (g2->type == GeomType::Circle) c2 = {id2, PointPos::None};
  // drop the coincidence between the two trimmed endpoints if they shared a corner
  if (moved1 != PointPos::None && moved2 != PointPos::None) {
    if (const Constraint* co = findConstraint(ConstraintType::Coincident, {id1, moved1}, {id2, moved2})) removeConstraint(co->id);
  }
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

std::optional<int> Sketch::split(int curveId, Pt2 at) {
  Geometry* g = geometry(curveId);
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
