// FabriCAD sketch: move / copy / rotate / scale, rectangular and circular patterns, chamfer.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchTransform.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fabricad::sketch {

Pt2 SketchTransform::apply(Pt2 p) const {
  Pt2 v = p - origin;
  double c = std::cos(angle), s = std::sin(angle);
  return origin + Pt2{v.x * c - v.y * s, v.x * s + v.y * c} * scale + translation;
}

namespace {

void applyTo(Geometry& g, const SketchTransform& t) {
  g.transform(t.origin, t.angle, t.scale);
  if (t.translation.length() > 0) g.translate(t.translation);
}

bool isFixed(const Sketch& sk, const Geometry& g) {
  if (!g.isMovable()) return true;
  for (const Constraint& c : sk.constraints())
    if (c.type == ConstraintType::Fix && c.a.geo == g.id) return true;
  return false;
}

/// Constraint kinds that encode an orientation relative to the sketch axes.
bool isOrientationConstraint(const Constraint& c) {
  switch (c.type) {
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY: return true;
    case ConstraintType::Angle: return !c.b.isValid();  // angle to the X axis
    default: return false;
  }
}

bool isLengthDimension(ConstraintType t) {
  return t == ConstraintType::Distance || t == ConstraintType::DistanceX || t == ConstraintType::DistanceY ||
         t == ConstraintType::Radius || t == ConstraintType::Diameter || t == ConstraintType::MajorRadius ||
         t == ConstraintType::MinorRadius || t == ConstraintType::PointCurveDistance ||
         t == ConstraintType::CurveCurveDistance || t == ConstraintType::ArcLength;
}

void scaleDimension(Constraint& c, double k) {
  if (!isLengthDimension(c.type)) return;
  c.value *= k;
  c.expression.clear();
}

}  // namespace

std::vector<int> transformEntities(Sketch& sk, const std::vector<int>& ids, const SketchTransform& t, bool copy) {
  std::vector<int> out;
  std::vector<int> unique;
  for (int id : ids)
    if (sk.geometry(id) && std::find(unique.begin(), unique.end(), id) == unique.end()) unique.push_back(id);
  if (unique.empty()) return out;

  if (!copy) {
    std::set<int> moved;
    for (int id : unique) {
      Geometry* g = sk.geometry(id);
      if (!g || isFixed(sk, *g)) continue;
      applyTo(*g, t);
      moved.insert(id);
      out.push_back(id);
    }
    auto& cs = sk.constraints();
    cs.erase(std::remove_if(cs.begin(), cs.end(),
                            [&](const Constraint& c) {
                              bool anyIn = false, anyOut = false;
                              for (const GeoRef* r : {&c.a, &c.b, &c.c}) {
                                if (!r->isValid()) continue;
                                if (moved.count(r->geo)) anyIn = true;
                                else anyOut = true;
                              }
                              if (!anyIn) return false;
                              if (anyOut) return true;  // ties the moved set to geometry left behind
                              return t.rotates() && isOrientationConstraint(c);
                            }),
             cs.end());
    if (t.scales())
      for (Constraint& c : cs) {
        bool in = c.a.isValid() && moved.count(c.a.geo);
        if (in && c.isDimensional()) scaleDimension(c, t.scale);
      }
    for (Constraint& c : cs)
      if (c.isDimensional() && c.hasLabelPos && c.a.isValid() && moved.count(c.a.geo)) c.labelPos = t.apply(c.labelPos);
    return out;
  }

  std::map<int, int> idMap;
  for (int id : unique) {
    Geometry g = *sk.geometry(id);
    g.id = -1;
    g.projected = false;
    g.blocked = false;
    g.source = core::ShapeRef{};
    applyTo(g, t);
    int nid = sk.addGeometry(g);
    idMap[id] = nid;
    out.push_back(nid);
  }
  std::vector<Constraint> copies;
  for (const Constraint& c : sk.constraints()) {
    if (c.type == ConstraintType::Fix || c.internal) continue;
    bool allIn = c.a.isValid();
    for (const GeoRef* r : {&c.a, &c.b, &c.c})
      if (r->isValid() && !idMap.count(r->geo)) allIn = false;
    if (!allIn) continue;
    if (t.rotates() && isOrientationConstraint(c)) continue;
    Constraint copyC = c;
    copyC.id = -1;
    for (GeoRef* r : {&copyC.a, &copyC.b, &copyC.c})
      if (r->isValid()) r->geo = idMap[r->geo];
    if (copyC.isDimensional()) {
      copyC.name.clear();
      if (t.scales()) scaleDimension(copyC, t.scale);
      if (copyC.hasLabelPos) copyC.labelPos = t.apply(copyC.labelPos);
    }
    copies.push_back(copyC);
  }
  for (Constraint& c : copies) sk.addConstraint(c);
  return out;
}

std::vector<int> rectangularPattern(Sketch& sk, const std::vector<int>& ids, const RectangularPatternSpec& spec) {
  std::vector<int> out;
  Pt2 d1 = spec.direction1.normalized(), d2 = spec.direction2.normalized();
  if (d1.length() < 0.5) d1 = {1, 0};
  if (d2.length() < 0.5) d2 = d1.perp();
  int n1 = std::max(1, spec.count1), n2 = std::max(1, spec.count2);
  int lo1 = spec.symmetric ? -(n1 - 1) : 0, lo2 = spec.symmetric ? -(n2 - 1) : 0;
  for (int j = lo2; j < n2; ++j)
    for (int i = lo1; i < n1; ++i) {
      if (i == 0 && j == 0) continue;
      Pt2 delta = d1 * (spec.spacing1 * i) + d2 * (spec.spacing2 * j);
      auto made = transformEntities(sk, ids, SketchTransform::translate(delta), true);
      out.insert(out.end(), made.begin(), made.end());
    }
  return out;
}

std::vector<int> circularPattern(Sketch& sk, const std::vector<int>& ids, const CircularPatternSpec& spec) {
  std::vector<int> out;
  int n = std::max(1, spec.count);
  if (n < 2) return out;
  bool full = std::abs(std::abs(spec.totalAngle) - 360.0) < 1e-9;
  double stepDeg = full ? spec.totalAngle / n : spec.totalAngle / (n - 1);
  for (int k = 1; k < n; ++k) {
    auto made = transformEntities(sk, ids, SketchTransform::rotate(spec.center, stepDeg * k * kPi / 180.0), true);
    out.insert(out.end(), made.begin(), made.end());
  }
  return out;
}

std::optional<int> chamfer(Sketch& sk, int id1, Pt2 near1, int id2, Pt2 near2, double d1, double d2) {
  Geometry* g1 = sk.geometry(id1);
  Geometry* g2 = sk.geometry(id2);
  if (!g1 || !g2 || id1 == id2 || g1->type != GeomType::Line || g2->type != GeomType::Line) return std::nullopt;
  if (d1 <= 0 || d2 <= 0) return std::nullopt;
  auto x = intersectLines(g1->start(), g1->direction(), g2->start(), g2->direction());
  if (!x) return std::nullopt;
  // point at distance d from the corner on the kept side of a line, and the end that moves
  auto cut = [&](Geometry& g, Pt2 keepNear, double d, PointPos& moved) -> std::optional<Pt2> {
    double tX = g.paramOf(*x), tK = g.paramOf(keepNear);
    Pt2 u = g.direction() * (tK >= tX ? 1.0 : -1.0);
    Pt2 p = *x + u * d;
    // the far end of the kept part must stay beyond the chamfer point
    Pt2 farEnd = tK >= tX ? g.end() : g.start();
    if ((farEnd - *x).dot(u) <= d + 1e-9) return std::nullopt;
    moved = tK >= tX ? PointPos::Start : PointPos::End;
    return p;
  };
  PointPos m1 = PointPos::None, m2 = PointPos::None;
  auto p1 = cut(*g1, near1, d1, m1);
  auto p2 = cut(*g2, near2, d2, m2);
  if (!p1 || !p2) return std::nullopt;
  if (const Constraint* co = sk.findConstraint(ConstraintType::Coincident, {id1, m1}, {id2, m2})) sk.removeConstraint(co->id);
  const Pt2 old1 = g1->point(m1), old2 = g2->point(m2);
  if (m1 == PointPos::Start) g1->setStart(*p1);
  else g1->setEnd(*p1);
  if (m2 == PointPos::Start) g2->setStart(*p2);
  else g2->setEnd(*p2);
  sk.retargetTrimmedDimensions(id1, m1, id2);
  sk.retargetTrimmedDimensions(id2, m2, id1);
  sk.preserveTrimmedCorner(id1, m1, old1, id2, m2, old2);
  g1 = sk.geometry(id1);  // preserveTrimmedCorner may add geometry
  g2 = sk.geometry(id2);
  Geometry line = Geometry::makeLine(*p1, *p2);
  line.construction = g1->construction && g2->construction;
  int cid = sk.addGeometry(line);
  sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {cid, PointPos::Start}, .b = {id1, m1}});
  sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {cid, PointPos::End}, .b = {id2, m2}});
  return cid;
}

}  // namespace fabricad::sketch
