// FabriCAD sketch: 2D geometry value types, queries and JSON.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchGeometry.h"

#include "core/Json.h"

#include <QJsonArray>

#include <algorithm>

namespace fabricad::sketch {

using core::qs;
using core::ss;

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

const char* geomTypeName(GeomType t) {
  switch (t) {
    case GeomType::Point: return "Point";
    case GeomType::Line: return "Line";
    case GeomType::Circle: return "Circle";
    case GeomType::Arc: return "Arc";
  }
  return "Line";
}

std::optional<GeomType> geomTypeFromName(const std::string& s) {
  if (s == "Point") return GeomType::Point;
  if (s == "Line") return GeomType::Line;
  if (s == "Circle") return GeomType::Circle;
  if (s == "Arc") return GeomType::Arc;
  return std::nullopt;
}

const char* pointPosName(PointPos p) {
  switch (p) {
    case PointPos::None: return "none";
    case PointPos::Start: return "start";
    case PointPos::End: return "end";
    case PointPos::Center: return "center";
    case PointPos::Mid: return "mid";
  }
  return "none";
}

PointPos pointPosFromName(const std::string& s) {
  if (s == "start") return PointPos::Start;
  if (s == "end") return PointPos::End;
  if (s == "center") return PointPos::Center;
  if (s == "mid") return PointPos::Mid;
  return PointPos::None;
}

core::SketchPointRole toRole(PointPos p) {
  switch (p) {
    case PointPos::Start: return core::SketchPointRole::Start;
    case PointPos::End: return core::SketchPointRole::End;
    case PointPos::Center: return core::SketchPointRole::Center;
    case PointPos::Mid: return core::SketchPointRole::Mid;
    case PointPos::None: break;
  }
  return core::SketchPointRole::Standalone;
}

PointPos fromRole(core::SketchPointRole r) {
  switch (r) {
    case core::SketchPointRole::Start: return PointPos::Start;
    case core::SketchPointRole::End: return PointPos::End;
    case core::SketchPointRole::Center: return PointPos::Center;
    case core::SketchPointRole::Mid: return PointPos::Mid;
    case core::SketchPointRole::Standalone: return PointPos::Center;
  }
  return PointPos::None;
}

PointPos fromRole(int subIndex) {
  if (subIndex < 0 || subIndex > 4) return PointPos::None;
  return fromRole(static_cast<core::SketchPointRole>(subIndex));
}

const char* constraintTypeName(ConstraintType t) {
  switch (t) {
    case ConstraintType::Coincident: return "Coincident";
    case ConstraintType::PointOnCurve: return "PointOnCurve";
    case ConstraintType::Horizontal: return "Horizontal";
    case ConstraintType::Vertical: return "Vertical";
    case ConstraintType::Parallel: return "Parallel";
    case ConstraintType::Perpendicular: return "Perpendicular";
    case ConstraintType::Tangent: return "Tangent";
    case ConstraintType::Equal: return "Equal";
    case ConstraintType::Concentric: return "Concentric";
    case ConstraintType::Collinear: return "Collinear";
    case ConstraintType::Midpoint: return "Midpoint";
    case ConstraintType::Fix: return "Fix";
    case ConstraintType::Symmetric: return "Symmetric";
    case ConstraintType::Distance: return "Distance";
    case ConstraintType::DistanceX: return "DistanceX";
    case ConstraintType::DistanceY: return "DistanceY";
    case ConstraintType::Radius: return "Radius";
    case ConstraintType::Diameter: return "Diameter";
    case ConstraintType::Angle: return "Angle";
  }
  return "Coincident";
}

std::optional<ConstraintType> constraintTypeFromName(const std::string& s) {
  static const ConstraintType all[] = {
      ConstraintType::Coincident, ConstraintType::PointOnCurve, ConstraintType::Horizontal, ConstraintType::Vertical,
      ConstraintType::Parallel,   ConstraintType::Perpendicular, ConstraintType::Tangent,  ConstraintType::Equal,
      ConstraintType::Concentric, ConstraintType::Collinear,    ConstraintType::Midpoint,   ConstraintType::Fix,
      ConstraintType::Symmetric,  ConstraintType::Distance,     ConstraintType::DistanceX,  ConstraintType::DistanceY,
      ConstraintType::Radius,     ConstraintType::Diameter,     ConstraintType::Angle};
  for (ConstraintType t : all)
    if (s == constraintTypeName(t)) return t;
  return std::nullopt;
}

bool isDimensional(ConstraintType t) {
  switch (t) {
    case ConstraintType::Distance:
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::Angle: return true;
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// angles & intersections
// ---------------------------------------------------------------------------

double normalizeAngle(double a) {
  a = std::fmod(a, 2 * kPi);
  if (a < 0) a += 2 * kPi;
  if (a >= 2 * kPi) a -= 2 * kPi;
  return a;
}

double angleDelta(double a, double b) {
  double d = std::fmod(b - a, 2 * kPi);
  if (d > kPi) d -= 2 * kPi;
  if (d <= -kPi) d += 2 * kPi;
  return d;
}

std::optional<Pt2> intersectLines(Pt2 a, Pt2 d, Pt2 b, Pt2 e) {
  double den = d.cross(e);
  double scale = std::max(1e-12, d.length() * e.length());
  if (std::abs(den) < 1e-12 * scale) return std::nullopt;
  double t = (b - a).cross(e) / den;
  return a + d * t;
}

std::vector<Pt2> intersectLineCircle(Pt2 a, Pt2 d, Pt2 c, double r) {
  std::vector<Pt2> out;
  double dl = d.length();
  if (dl < 1e-15) return out;
  Pt2 u = d / dl;
  double t0 = (c - a).dot(u);
  Pt2 foot = a + u * t0;
  double h2 = r * r - (foot - c).dot(foot - c);
  if (h2 < -1e-9 * std::max(1.0, r * r)) return out;
  if (h2 <= 1e-12) {
    out.push_back(foot);
    return out;
  }
  double h = std::sqrt(h2);
  out.push_back(foot - u * h);
  out.push_back(foot + u * h);
  return out;
}

std::vector<Pt2> intersectCircles(Pt2 c1, double r1, Pt2 c2, double r2) {
  std::vector<Pt2> out;
  double d = c1.distance(c2);
  if (d < 1e-12) return out;
  if (d > r1 + r2 + 1e-9 || d < std::abs(r1 - r2) - 1e-9) return out;
  double a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
  double h2 = r1 * r1 - a * a;
  Pt2 u = (c2 - c1) / d;
  Pt2 m = c1 + u * a;
  if (h2 <= 1e-12) {
    out.push_back(m);
    return out;
  }
  double h = std::sqrt(h2);
  out.push_back(m + u.perp() * h);
  out.push_back(m - u.perp() * h);
  return out;
}

namespace {

bool onFiniteCurve(const Geometry& g, Pt2 p, double tol) {
  switch (g.type) {
    case GeomType::Point: return g.center().near(p, tol);
    case GeomType::Line: {
      Pt2 d = g.end() - g.start();
      double len2 = d.dot(d);
      if (len2 < 1e-24) return g.start().near(p, tol);
      double t = (p - g.start()).dot(d) / len2;
      return t >= -tol / std::sqrt(len2) && t <= 1 + tol / std::sqrt(len2);
    }
    case GeomType::Circle: return true;
    case GeomType::Arc: {
      double ang = (p - g.center()).angle();
      if (g.containsAngle(ang)) return true;
      // endpoints within tolerance
      return g.start().near(p, tol) || g.end().near(p, tol);
    }
  }
  return false;
}

}  // namespace

std::vector<Pt2> intersectCurves(const Geometry& g1, const Geometry& g2, double tol) {
  std::vector<Pt2> cand;
  if (g1.type == GeomType::Point || g2.type == GeomType::Point) return cand;
  if (g1.type == GeomType::Line && g2.type == GeomType::Line) {
    if (auto p = intersectLines(g1.start(), g1.direction(), g2.start(), g2.direction())) cand.push_back(*p);
  } else if (g1.type == GeomType::Line) {
    cand = intersectLineCircle(g1.start(), g1.direction(), g2.center(), g2.radius());
  } else if (g2.type == GeomType::Line) {
    cand = intersectLineCircle(g2.start(), g2.direction(), g1.center(), g1.radius());
  } else {
    cand = intersectCircles(g1.center(), g1.radius(), g2.center(), g2.radius());
  }
  std::vector<Pt2> out;
  for (const Pt2& p : cand)
    if (onFiniteCurve(g1, p, tol) && onFiniteCurve(g2, p, tol)) out.push_back(p);
  return out;
}

double projectParam(Pt2 a, Pt2 d, Pt2 q) {
  double len2 = d.dot(d);
  if (len2 < 1e-24) return 0;
  return (q - a).dot(d) / len2;
}

double signedDistanceToLine(Pt2 a, Pt2 d, Pt2 q) {
  double l = d.length();
  if (l < 1e-15) return q.distance(a);
  return d.cross(q - a) / l;
}

std::optional<Pt2> circumcenter(Pt2 a, Pt2 b, Pt2 c) {
  double d = 2 * ((b - a).cross(c - a));
  double scale = std::max(1.0, (b - a).length() * (c - a).length());
  if (std::abs(d) < 1e-12 * scale) return std::nullopt;
  double a2 = a.dot(a), b2 = b.dot(b), c2 = c.dot(c);
  double ux = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
  double uy = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
  return Pt2{ux, uy};
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

Geometry Geometry::makePoint(Pt2 p) {
  Geometry g;
  g.type = GeomType::Point;
  g.p = {p.x, p.y};
  return g;
}

Geometry Geometry::makeLine(Pt2 a, Pt2 b) {
  Geometry g;
  g.type = GeomType::Line;
  g.p = {a.x, a.y, b.x, b.y};
  return g;
}

Geometry Geometry::makeCircle(Pt2 c, double r) {
  Geometry g;
  g.type = GeomType::Circle;
  g.p = {c.x, c.y, std::abs(r)};
  return g;
}

Geometry Geometry::makeArc(Pt2 c, double r, double a0, double a1) {
  Geometry g;
  g.type = GeomType::Arc;
  g.p.assign(9, 0.0);
  g.p[0] = c.x;
  g.p[1] = c.y;
  g.p[2] = std::abs(r);
  a0 = normalizeAngle(a0);
  a1 = normalizeAngle(a1);
  if (a1 <= a0 + 1e-12) a1 += 2 * kPi;
  g.p[3] = a0;
  g.p[4] = a1;
  g.normalize();
  return g;
}

std::optional<Geometry> Geometry::makeArc3Points(Pt2 a, Pt2 mid, Pt2 b) {
  auto c = circumcenter(a, mid, b);
  if (!c) return std::nullopt;
  double r = c->distance(a);
  double aa = (a - *c).angle(), am = (mid - *c).angle(), ab = (b - *c).angle();
  // ccw from a to b must pass through mid
  double sweepAB = normalizeAngle(ab - aa);
  double sweepAM = normalizeAngle(am - aa);
  if (sweepAM <= sweepAB) return makeArc(*c, r, aa, aa + sweepAB);
  return makeArc(*c, r, ab, ab + normalizeAngle(aa - ab));
}

Geometry Geometry::makeArcCenter(Pt2 c, Pt2 start, Pt2 end, bool ccw) {
  double r = c.distance(start);
  double a0 = (start - c).angle(), a1 = (end - c).angle();
  if (ccw) return makeArc(c, r, a0, a1);
  return makeArc(c, r, a1, a0);
}

Pt2 Geometry::start() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return {p[0], p[1]};
    case GeomType::Circle: return center() + Pt2{p[2], 0};
    case GeomType::Arc: return {p[5], p[6]};
  }
  return {};
}

Pt2 Geometry::end() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return {p[2], p[3]};
    case GeomType::Circle: return center() + Pt2{p[2], 0};
    case GeomType::Arc: return {p[7], p[8]};
  }
  return {};
}

Pt2 Geometry::center() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return (start() + end()) * 0.5;
    case GeomType::Circle:
    case GeomType::Arc: return {p[0], p[1]};
  }
  return {};
}

Pt2 Geometry::midpoint() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return (start() + end()) * 0.5;
    case GeomType::Circle: return center() + Pt2{p[2], 0};
    case GeomType::Arc: return center() + Pt2::polar(p[2], 0.5 * (p[3] + p[4]));
  }
  return {};
}

double Geometry::radius() const { return isCircular() ? p[2] : 0.0; }
double Geometry::startAngle() const { return type == GeomType::Arc ? p[3] : 0.0; }
double Geometry::endAngle() const { return type == GeomType::Arc ? p[4] : 2 * kPi; }
double Geometry::sweep() const { return type == GeomType::Arc ? std::max(0.0, p[4] - p[3]) : (type == GeomType::Circle ? 2 * kPi : 0.0); }

double Geometry::length() const {
  switch (type) {
    case GeomType::Point: return 0;
    case GeomType::Line: return start().distance(end());
    case GeomType::Circle: return 2 * kPi * p[2];
    case GeomType::Arc: return p[2] * sweep();
  }
  return 0;
}

Pt2 Geometry::direction() const {
  if (type != GeomType::Line) return {1, 0};
  return (end() - start()).normalized();
}

Pt2 Geometry::point(PointPos pos) const {
  switch (pos) {
    case PointPos::Start: return start();
    case PointPos::End: return end();
    case PointPos::Center: return center();
    case PointPos::Mid: return midpoint();
    case PointPos::None: break;
  }
  return center();
}

std::vector<PointPos> Geometry::pointPositions() const {
  switch (type) {
    case GeomType::Point: return {PointPos::Center};
    case GeomType::Line: return {PointPos::Start, PointPos::End, PointPos::Mid};
    case GeomType::Circle: return {PointPos::Center};
    case GeomType::Arc: return {PointPos::Start, PointPos::End, PointPos::Center, PointPos::Mid};
  }
  return {};
}

void Geometry::setStart(Pt2 q) {
  switch (type) {
    case GeomType::Point: p[0] = q.x; p[1] = q.y; break;
    case GeomType::Line: p[0] = q.x; p[1] = q.y; break;
    case GeomType::Circle: break;
    case GeomType::Arc: {
      double a1 = p[4];
      double a0 = normalizeAngle((q - center()).angle());
      double sw = normalizeAngle(a1 - a0);
      if (sw < 1e-9) sw = 2 * kPi;
      p[3] = a0;
      p[4] = a0 + sw;
      normalize();
      break;
    }
  }
}

void Geometry::setEnd(Pt2 q) {
  switch (type) {
    case GeomType::Point: p[0] = q.x; p[1] = q.y; break;
    case GeomType::Line: p[2] = q.x; p[3] = q.y; break;
    case GeomType::Circle: break;
    case GeomType::Arc: {
      double a0 = normalizeAngle(p[3]);
      double a1 = normalizeAngle((q - center()).angle());
      double sw = normalizeAngle(a1 - a0);
      if (sw < 1e-9) sw = 2 * kPi;
      p[3] = a0;
      p[4] = a0 + sw;
      normalize();
      break;
    }
  }
}

void Geometry::setCenter(Pt2 c) {
  switch (type) {
    case GeomType::Point: p[0] = c.x; p[1] = c.y; break;
    case GeomType::Line: translate(c - center()); break;
    case GeomType::Circle: p[0] = c.x; p[1] = c.y; break;
    case GeomType::Arc: p[0] = c.x; p[1] = c.y; normalize(); break;
  }
}

void Geometry::setRadius(double r) {
  if (!isCircular()) return;
  p[2] = std::abs(r);
  if (type == GeomType::Arc) normalize();
}

void Geometry::setAngles(double a0, double a1) {
  if (type != GeomType::Arc) return;
  a0 = normalizeAngle(a0);
  a1 = normalizeAngle(a1);
  if (a1 <= a0 + 1e-12) a1 += 2 * kPi;
  p[3] = a0;
  p[4] = a1;
  normalize();
}

void Geometry::normalize() {
  if (type != GeomType::Arc) return;
  // keep start angle in [0, 2pi) and end angle in (start, start + 2pi]
  double a0 = p[3], a1 = p[4];
  double n0 = normalizeAngle(a0);
  a1 += n0 - a0;
  a0 = n0;
  while (a1 <= a0 + 1e-12) a1 += 2 * kPi;
  while (a1 > a0 + 2 * kPi + 1e-12) a1 -= 2 * kPi;
  p[3] = a0;
  p[4] = a1;
  Pt2 c = center();
  Pt2 s = c + Pt2::polar(p[2], a0);
  Pt2 e = c + Pt2::polar(p[2], a1);
  p[5] = s.x;
  p[6] = s.y;
  p[7] = e.x;
  p[8] = e.y;
}

void Geometry::translate(Pt2 d) {
  switch (type) {
    case GeomType::Point: p[0] += d.x; p[1] += d.y; break;
    case GeomType::Line: p[0] += d.x; p[1] += d.y; p[2] += d.x; p[3] += d.y; break;
    case GeomType::Circle: p[0] += d.x; p[1] += d.y; break;
    case GeomType::Arc: p[0] += d.x; p[1] += d.y; normalize(); break;
  }
}

void Geometry::mirror(Pt2 a, Pt2 d) {
  Pt2 u = d.normalized();
  auto refl = [&](Pt2 q) {
    Pt2 v = q - a;
    double along = v.dot(u);
    Pt2 foot = a + u * along;
    return foot * 2.0 - q;
  };
  switch (type) {
    case GeomType::Point: {
      Pt2 q = refl(center());
      p[0] = q.x;
      p[1] = q.y;
      break;
    }
    case GeomType::Line: {
      Pt2 s = refl(start()), e = refl(end());
      p = {s.x, s.y, e.x, e.y};
      break;
    }
    case GeomType::Circle: {
      Pt2 c = refl(center());
      p[0] = c.x;
      p[1] = c.y;
      break;
    }
    case GeomType::Arc: {
      // mirroring reverses orientation: new start = reflected old end
      Pt2 c = refl(center());
      Pt2 s = refl(end()), e = refl(start());
      *this = makeArc(c, p[2], (s - c).angle(), (e - c).angle());
      break;
    }
  }
}

bool Geometry::containsAngle(double angle) const {
  if (type == GeomType::Circle) return true;
  if (type != GeomType::Arc) return false;
  double rel = normalizeAngle(angle - p[3]);
  double sw = sweep();
  const double eps = 1e-9;
  return rel <= sw + eps || rel >= 2 * kPi - eps;
}

Pt2 Geometry::closestPoint(Pt2 q) const {
  switch (type) {
    case GeomType::Point: return center();
    case GeomType::Line: {
      Pt2 d = end() - start();
      double t = std::clamp(projectParam(start(), d, q), 0.0, 1.0);
      return start() + d * t;
    }
    case GeomType::Circle: {
      Pt2 v = q - center();
      if (v.length() < 1e-12) return center() + Pt2{p[2], 0};
      return center() + v.normalized() * p[2];
    }
    case GeomType::Arc: {
      Pt2 v = q - center();
      if (v.length() < 1e-12) return start();
      double ang = v.angle();
      if (containsAngle(ang)) return center() + v.normalized() * p[2];
      return q.distance(start()) <= q.distance(end()) ? start() : end();
    }
  }
  return {};
}

Pt2 Geometry::tangentAt(Pt2 q) const {
  switch (type) {
    case GeomType::Point: return {1, 0};
    case GeomType::Line: return direction();
    case GeomType::Circle:
    case GeomType::Arc: {
      Pt2 v = (q - center()).normalized();
      return v.perp();
    }
  }
  return {1, 0};
}

Pt2 Geometry::pointAt(double param) const {
  switch (type) {
    case GeomType::Point: return center();
    case GeomType::Line: return start() + (end() - start()) * param;
    case GeomType::Circle:
    case GeomType::Arc: return center() + Pt2::polar(p[2], param);
  }
  return {};
}

double Geometry::paramOf(Pt2 q) const {
  switch (type) {
    case GeomType::Point: return 0;
    case GeomType::Line: return projectParam(start(), end() - start(), q);
    case GeomType::Circle: return normalizeAngle((q - center()).angle());
    case GeomType::Arc: {
      double a = normalizeAngle((q - center()).angle());
      // express in the arc's range [a0, a1]
      double a0 = p[3];
      double rel = normalizeAngle(a - a0);
      return a0 + rel;
    }
  }
  return 0;
}

std::pair<double, double> Geometry::paramRange() const {
  switch (type) {
    case GeomType::Point: return {0, 0};
    case GeomType::Line: return {0, 1};
    case GeomType::Circle: return {0, 2 * kPi};
    case GeomType::Arc: return {p[3], p[4]};
  }
  return {0, 0};
}

std::pair<Pt2, Pt2> Geometry::bbox() const {
  switch (type) {
    case GeomType::Point: return {center(), center()};
    case GeomType::Line: {
      Pt2 s = start(), e = end();
      return {{std::min(s.x, e.x), std::min(s.y, e.y)}, {std::max(s.x, e.x), std::max(s.y, e.y)}};
    }
    case GeomType::Circle: return {center() - Pt2{p[2], p[2]}, center() + Pt2{p[2], p[2]}};
    case GeomType::Arc: {
      Pt2 mn = start(), mx = start();
      auto grow = [&](Pt2 q) {
        mn = {std::min(mn.x, q.x), std::min(mn.y, q.y)};
        mx = {std::max(mx.x, q.x), std::max(mx.y, q.y)};
      };
      grow(end());
      for (int k = 0; k < 4; ++k) {
        double a = k * kPi / 2;
        if (containsAngle(a)) grow(center() + Pt2::polar(p[2], a));
      }
      return {mn, mx};
    }
  }
  return {};
}

std::vector<Pt2> Geometry::tessellate(int segments) const {
  std::vector<Pt2> out;
  switch (type) {
    case GeomType::Point: out.push_back(center()); break;
    case GeomType::Line: out = {start(), end()}; break;
    case GeomType::Circle:
    case GeomType::Arc: {
      double a0 = startAngle(), sw = sweep();
      int n = std::max(8, static_cast<int>(std::ceil(segments * sw / (2 * kPi))));
      out.reserve(static_cast<size_t>(n) + 1);
      for (int i = 0; i <= n; ++i) out.push_back(center() + Pt2::polar(p[2], a0 + sw * i / n));
      break;
    }
  }
  return out;
}

QJsonObject Geometry::toJson() const {
  QJsonObject o;
  o["id"] = id;
  o["type"] = geomTypeName(type);
  QJsonArray params;
  size_t n = p.size();
  if (type == GeomType::Arc) n = 5;  // start/end are derived
  for (size_t i = 0; i < n && i < p.size(); ++i) params.append(p[i]);
  o["p"] = params;
  if (construction) o["construction"] = true;
  if (projected) o["projected"] = true;
  if (blocked) o["blocked"] = true;
  if (source.isValid()) o["source"] = source.toJson();
  return o;
}

Geometry Geometry::fromJson(const QJsonObject& o) {
  Geometry g;
  g.id = o["id"].toInt(-1);
  g.type = geomTypeFromName(ss(o["type"].toString())).value_or(GeomType::Line);
  g.p.clear();
  for (const auto& v : o["p"].toArray()) g.p.push_back(v.toDouble());
  size_t need = 2;
  switch (g.type) {
    case GeomType::Point: need = 2; break;
    case GeomType::Line: need = 4; break;
    case GeomType::Circle: need = 3; break;
    case GeomType::Arc: need = 9; break;
  }
  g.p.resize(need, 0.0);
  if (g.type == GeomType::Arc) g.normalize();
  g.construction = o["construction"].toBool(false);
  g.projected = o["projected"].toBool(false);
  g.blocked = o["blocked"].toBool(false);
  if (o.contains("source")) g.source = core::ShapeRef::fromJson(o["source"].toObject());
  return g;
}

// ---------------------------------------------------------------------------
// Constraint
// ---------------------------------------------------------------------------

namespace {
QJsonValue refToJson(const GeoRef& r) {
  QJsonObject o;
  o["g"] = r.geo;
  if (r.pos != PointPos::None) o["pos"] = pointPosName(r.pos);
  return o;
}
GeoRef refFromJson(const QJsonValue& v) {
  GeoRef r;
  if (!v.isObject()) return r;
  QJsonObject o = v.toObject();
  r.geo = o["g"].toInt(-1);
  r.pos = pointPosFromName(ss(o["pos"].toString()));
  return r;
}
}  // namespace

QJsonObject Constraint::toJson() const {
  QJsonObject o;
  o["id"] = id;
  o["type"] = constraintTypeName(type);
  if (a.isValid()) o["a"] = refToJson(a);
  if (b.isValid()) o["b"] = refToJson(b);
  if (c.isValid()) o["c"] = refToJson(c);
  if (isDimensional()) {
    o["value"] = value;
    if (!expression.empty()) o["expr"] = qs(expression);
    if (driven) o["driven"] = true;
    if (!name.empty()) o["name"] = qs(name);
    if (hasLabelPos) o["label"] = QJsonArray{labelPos.x, labelPos.y};
  }
  if (internal) o["internal"] = true;
  return o;
}

Constraint Constraint::fromJson(const QJsonObject& o) {
  Constraint c;
  c.id = o["id"].toInt(-1);
  c.type = constraintTypeFromName(ss(o["type"].toString())).value_or(ConstraintType::Coincident);
  c.a = refFromJson(o["a"]);
  c.b = refFromJson(o["b"]);
  c.c = refFromJson(o["c"]);
  c.value = o["value"].toDouble(0.0);
  c.expression = ss(o["expr"].toString());
  c.driven = o["driven"].toBool(false);
  c.name = ss(o["name"].toString());
  if (o.contains("label")) {
    QJsonArray l = o["label"].toArray();
    if (l.size() == 2) {
      c.labelPos = {l[0].toDouble(), l[1].toDouble()};
      c.hasLabelPos = true;
    }
  }
  c.internal = o["internal"].toBool(false);
  return c;
}

}  // namespace fabricad::sketch
