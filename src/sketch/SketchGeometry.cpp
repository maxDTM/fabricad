// FabriCAD sketch: 2D geometry value types, queries and JSON.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchGeometry.h"

#include "sketch/SketchCurves.h"
#include "sketch/SketchText.h"

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
    case GeomType::Ellipse: return "Ellipse";
    case GeomType::EllipseArc: return "EllipseArc";
    case GeomType::Spline: return "Spline";
    case GeomType::FitSpline: return "FitSpline";
    case GeomType::Text: return "Text";
  }
  return "Line";
}

std::optional<GeomType> geomTypeFromName(const std::string& s) {
  if (s == "Point") return GeomType::Point;
  if (s == "Line") return GeomType::Line;
  if (s == "Circle") return GeomType::Circle;
  if (s == "Arc") return GeomType::Arc;
  if (s == "Ellipse") return GeomType::Ellipse;
  if (s == "EllipseArc") return GeomType::EllipseArc;
  if (s == "Spline") return GeomType::Spline;
  if (s == "FitSpline") return GeomType::FitSpline;
  if (s == "Text") return GeomType::Text;
  return std::nullopt;
}

const char* pointPosName(PointPos p) {
  switch (p) {
    case PointPos::None: return "none";
    case PointPos::Start: return "start";
    case PointPos::End: return "end";
    case PointPos::Center: return "center";
    case PointPos::Mid: return "mid";
    case PointPos::Vertex: return "vertex";
  }
  return "none";
}

PointPos pointPosFromName(const std::string& s) {
  if (s == "start") return PointPos::Start;
  if (s == "end") return PointPos::End;
  if (s == "center") return PointPos::Center;
  if (s == "mid") return PointPos::Mid;
  if (s == "vertex") return PointPos::Vertex;
  return PointPos::None;
}

core::SketchPointRole toRole(PointPos p) {
  switch (p) {
    case PointPos::Start: return core::SketchPointRole::Start;
    case PointPos::End: return core::SketchPointRole::End;
    case PointPos::Center: return core::SketchPointRole::Center;
    case PointPos::Mid: return core::SketchPointRole::Mid;
    case PointPos::Vertex:
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

int toSubIndex(const GeoRef& r) {
  if (r.pos == PointPos::Vertex) return kVertexSubIndexBase + std::max(0, r.index);
  return static_cast<int>(toRole(r.pos));
}

GeoRef fromSubIndex(int geo, int subIndex) {
  if (subIndex >= kVertexSubIndexBase) return GeoRef{geo, PointPos::Vertex, subIndex - kVertexSubIndexBase};
  return GeoRef{geo, fromRole(subIndex)};
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
    case GeomType::Ellipse: return true;
    case GeomType::EllipseArc: {
      if (g.containsAngle(g.ellipse().closestParam(p))) return true;
      return g.start().near(p, tol) || g.end().near(p, tol);
    }
    case GeomType::Spline:
    case GeomType::FitSpline: return g.distanceTo(p) <= std::max(tol, 1e-6);
    case GeomType::Text: return false;
  }
  return false;
}

/// Newton refinement of an approximate intersection point of two parametric curves.
std::optional<Pt2> refineIntersection(const Geometry& g1, const Geometry& g2, Pt2 p) {
  double u = g1.paramOf(p), v = g2.paramOf(p);
  auto [u0, u1] = g1.paramRange();
  auto [v0, v1] = g2.paramRange();
  auto deriv = [](const Geometry& g, double t, double lo, double hi) {
    double h = std::max(1e-7, (hi - lo) * 1e-6);
    return (g.pointAt(std::min(hi, t + h)) - g.pointAt(std::max(lo, t - h))) / (std::min(hi, t + h) - std::max(lo, t - h));
  };
  bool periodic1 = g1.isClosedCurve(), periodic2 = g2.isClosedCurve();
  for (int it = 0; it < 40; ++it) {
    Pt2 f = g1.pointAt(u) - g2.pointAt(v);
    if (f.length() < 1e-11) break;
    Pt2 a = deriv(g1, u, periodic1 ? u - 1 : u0, periodic1 ? u + 1 : u1);
    Pt2 b = deriv(g2, v, periodic2 ? v - 1 : v0, periodic2 ? v + 1 : v1);
    // solve a*du - b*dv = -f
    double det = -a.x * b.y + a.y * b.x;
    if (std::abs(det) < 1e-18) break;
    double du = (-f.x * -b.y - -f.y * -b.x) / det;
    double dv = (a.x * -f.y - a.y * -f.x) / det;
    u += du;
    v += dv;
    if (!periodic1) u = std::clamp(u, u0, u1);
    if (!periodic2) v = std::clamp(v, v0, v1);
  }
  Pt2 q1 = g1.pointAt(u), q2 = g2.pointAt(v);
  if (q1.distance(q2) > 1e-6) return std::nullopt;
  return (q1 + q2) * 0.5;
}

}  // namespace

std::vector<Pt2> intersectCurves(const Geometry& g1, const Geometry& g2, double tol) {
  std::vector<Pt2> cand;
  if (g1.type == GeomType::Point || g2.type == GeomType::Point) return cand;
  if (g1.type == GeomType::Text || g2.type == GeomType::Text) return cand;
  auto analytic = [](const Geometry& g) { return g.type == GeomType::Line || g.isCircular(); };
  if (!analytic(g1) || !analytic(g2)) {
    // numeric: polyline crossings refined with Newton on both curve parameters
    std::vector<Pt2> out;
    auto [mn1, mx1] = g1.bbox();
    auto [mn2, mx2] = g2.bbox();
    if (mx1.x < mn2.x - tol || mx2.x < mn1.x - tol || mx1.y < mn2.y - tol || mx2.y < mn1.y - tol) return out;
    for (const Pt2& x : intersectPolylines(g1.tessellate(256), g2.tessellate(256))) {
      auto r = refineIntersection(g1, g2, x);
      if (!r || !onFiniteCurve(g1, *r, std::max(tol, 1e-7)) || !onFiniteCurve(g2, *r, std::max(tol, 1e-7))) continue;
      bool dup = false;
      for (const Pt2& o : out)
        if (o.near(*r, 1e-6)) dup = true;
      if (!dup) out.push_back(*r);
    }
    return out;
  }
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

namespace {
size_t paramCount(GeomType t) {
  switch (t) {
    case GeomType::Point: return 2;
    case GeomType::Line: return 4;
    case GeomType::Circle: return 3;
    case GeomType::Arc: return 9;
    case GeomType::Ellipse: return 5;
    case GeomType::EllipseArc: return 11;
    case GeomType::Text: return 4;
    case GeomType::Spline:
    case GeomType::FitSpline: return 0;  // variable
  }
  return 0;
}

std::vector<Pt2> vertexList(const Geometry& g) {
  std::vector<Pt2> v;
  for (int i = 0; i < g.vertexCount(); ++i) v.push_back(g.vertex(i));
  return v;
}

Pt2 rotateAbout(Pt2 q, Pt2 o, double c, double s, double k) {
  Pt2 v = q - o;
  return o + Pt2{v.x * c - v.y * s, v.x * s + v.y * c} * k;
}
}  // namespace

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

Geometry Geometry::makeEllipse(Pt2 c, double a, double b, double angle) {
  a = std::abs(a);
  b = std::abs(b);
  if (a < b) {
    std::swap(a, b);
    angle += kPi / 2;
  }
  Geometry g;
  g.type = GeomType::Ellipse;
  Pt2 f = ellipseFocus(c, Pt2::polar(1.0, angle), a, b);
  g.p = {c.x, c.y, f.x, f.y, b};
  return g;
}

Geometry Geometry::makeEllipseArc(Pt2 c, double a, double b, double angle, double u0, double u1) {
  a = std::abs(a);
  b = std::abs(b);
  if (a < b) {
    // rotating the frame by +90 deg shifts the parameter by -90 deg
    std::swap(a, b);
    angle += kPi / 2;
    u0 -= kPi / 2;
    u1 -= kPi / 2;
  }
  Geometry g;
  g.type = GeomType::EllipseArc;
  Pt2 f = ellipseFocus(c, Pt2::polar(1.0, angle), a, b);
  g.p.assign(11, 0.0);
  g.p[0] = c.x;
  g.p[1] = c.y;
  g.p[2] = f.x;
  g.p[3] = f.y;
  g.p[4] = b;
  double n0 = normalizeAngle(u0);
  double sw = u1 - u0;
  if (sw <= 1e-12) sw = normalizeAngle(sw);
  if (sw <= 1e-12) sw = 2 * kPi;
  g.p[5] = n0;
  g.p[6] = n0 + std::min(sw, 2 * kPi);
  g.normalize();
  return g;
}

Geometry Geometry::makeControlSpline(const std::vector<Pt2>& poles, int degree) {
  Geometry g;
  g.type = GeomType::Spline;
  g.degree = std::max(1, degree);
  for (const Pt2& q : poles) {
    g.p.push_back(q.x);
    g.p.push_back(q.y);
  }
  return g;
}

Geometry Geometry::makeFitSpline(const std::vector<Pt2>& fit) {
  Geometry g;
  g.type = GeomType::FitSpline;
  for (const Pt2& q : fit) {
    g.p.push_back(q.x);
    g.p.push_back(q.y);
  }
  g.knots = chordLengthParams(fit);
  return g;
}

Geometry Geometry::makeText(Pt2 anchor, const std::string& text, double height, double angle, const std::string& font) {
  Geometry g;
  g.type = GeomType::Text;
  g.p = {anchor.x, anchor.y, std::abs(height), angle};
  g.text = text;
  g.font = font;
  return g;
}

EllipseFrame Geometry::ellipse() const {
  if (!isElliptical() || p.size() < 5) return EllipseFrame{};
  return ellipseFrame({p[0], p[1]}, {p[2], p[3]}, p[4]);
}

BSpline2 Geometry::bspline() const {
  if (type == GeomType::Spline) return controlPointSpline(vertexList(*this), degree);
  if (type == GeomType::FitSpline) {
    std::vector<Pt2> fit = vertexList(*this);
    std::vector<double> t = knots.size() == fit.size() ? knots : chordLengthParams(fit);
    return interpolateSpline(fit, t);
  }
  return BSpline2{};
}

double Geometry::majorRadius() const { return isElliptical() ? ellipse().a : radius(); }
double Geometry::minorRadius() const { return isElliptical() ? ellipse().b : radius(); }

Pt2 Geometry::start() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return {p[0], p[1]};
    case GeomType::Circle: return center() + Pt2{p[2], 0};
    case GeomType::Arc: return {p[5], p[6]};
    case GeomType::Ellipse: return ellipse().value(0.0);
    case GeomType::EllipseArc: return {p[7], p[8]};
    case GeomType::Spline:
    case GeomType::FitSpline: return vertexCount() > 0 ? vertex(0) : Pt2{};
    case GeomType::Text: return {p[0], p[1]};
  }
  return {};
}

Pt2 Geometry::end() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return {p[2], p[3]};
    case GeomType::Circle: return center() + Pt2{p[2], 0};
    case GeomType::Arc: return {p[7], p[8]};
    case GeomType::Ellipse: return ellipse().value(0.0);
    case GeomType::EllipseArc: return {p[9], p[10]};
    case GeomType::Spline:
    case GeomType::FitSpline: return vertexCount() > 0 ? vertex(vertexCount() - 1) : Pt2{};
    case GeomType::Text: return {p[0], p[1]};
  }
  return {};
}

Pt2 Geometry::center() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return (start() + end()) * 0.5;
    case GeomType::Circle:
    case GeomType::Arc:
    case GeomType::Ellipse:
    case GeomType::EllipseArc: return {p[0], p[1]};
    case GeomType::Spline:
    case GeomType::FitSpline:
    case GeomType::Text: {
      auto [mn, mx] = bbox();
      return (mn + mx) * 0.5;
    }
  }
  return {};
}

Pt2 Geometry::midpoint() const {
  switch (type) {
    case GeomType::Point: return {p[0], p[1]};
    case GeomType::Line: return (start() + end()) * 0.5;
    case GeomType::Circle: return center() + Pt2{p[2], 0};
    case GeomType::Arc: return center() + Pt2::polar(p[2], 0.5 * (p[3] + p[4]));
    case GeomType::Ellipse: return ellipse().value(0.0);
    case GeomType::EllipseArc: return ellipse().value(0.5 * (p[5] + p[6]));
    case GeomType::Spline:
    case GeomType::FitSpline: {
      BSpline2 s = bspline();
      return s.isValid() ? s.value(0.5 * (s.first() + s.last())) : center();
    }
    case GeomType::Text: return center();
  }
  return {};
}

double Geometry::radius() const { return isCircular() ? p[2] : 0.0; }
double Geometry::startAngle() const {
  if (type == GeomType::Arc) return p[3];
  if (type == GeomType::EllipseArc) return p[5];
  return 0.0;
}
double Geometry::endAngle() const {
  if (type == GeomType::Arc) return p[4];
  if (type == GeomType::EllipseArc) return p[6];
  return 2 * kPi;
}
double Geometry::sweep() const {
  switch (type) {
    case GeomType::Arc: return std::max(0.0, p[4] - p[3]);
    case GeomType::EllipseArc: return std::max(0.0, p[6] - p[5]);
    case GeomType::Circle:
    case GeomType::Ellipse: return 2 * kPi;
    default: return 0.0;
  }
}

double Geometry::length() const {
  switch (type) {
    case GeomType::Point: return 0;
    case GeomType::Line: return start().distance(end());
    case GeomType::Circle: return 2 * kPi * p[2];
    case GeomType::Arc: return p[2] * sweep();
    case GeomType::Ellipse:
    case GeomType::EllipseArc:
    case GeomType::Spline:
    case GeomType::FitSpline:
    case GeomType::Text: {
      double l = 0;
      for (const auto& pl : polylines(256))
        for (size_t i = 1; i < pl.size(); ++i) l += pl[i].distance(pl[i - 1]);
      return l;
    }
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
    case PointPos::Vertex: return vertexCount() > 0 ? vertex(0) : center();
    case PointPos::None: break;
  }
  return center();
}

Pt2 Geometry::point(PointPos pos, int index) const {
  if (pos == PointPos::Vertex && index >= 0 && index < vertexCount()) return vertex(index);
  return point(pos);
}

std::vector<PointPos> Geometry::pointPositions() const {
  switch (type) {
    case GeomType::Point: return {PointPos::Center};
    case GeomType::Line: return {PointPos::Start, PointPos::End, PointPos::Mid};
    case GeomType::Circle: return {PointPos::Center};
    case GeomType::Arc: return {PointPos::Start, PointPos::End, PointPos::Center, PointPos::Mid};
    case GeomType::Ellipse: return {PointPos::Center};
    case GeomType::EllipseArc: return {PointPos::Start, PointPos::End, PointPos::Center};
    case GeomType::Spline:
    case GeomType::FitSpline: return {PointPos::Start, PointPos::End};
    case GeomType::Text: return {PointPos::Start};
  }
  return {};
}

std::vector<GeoRef> Geometry::pointRefs() const {
  std::vector<GeoRef> out;
  for (PointPos pos : pointPositions()) out.push_back(GeoRef{id, pos});
  // interior spline vertices (the end vertices are Start / End)
  for (int i = 1; i + 1 < vertexCount(); ++i) out.push_back(GeoRef{id, PointPos::Vertex, i});
  return out;
}

void Geometry::setStart(Pt2 q) {
  switch (type) {
    case GeomType::Point: p[0] = q.x; p[1] = q.y; break;
    case GeomType::Line: p[0] = q.x; p[1] = q.y; break;
    case GeomType::Circle:
    case GeomType::Ellipse: break;
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
    case GeomType::EllipseArc: {
      double u1 = p[6];
      double u0 = ellipse().closestParam(q);
      double sw = normalizeAngle(u1 - u0);
      if (sw < 1e-9) sw = 2 * kPi;
      p[5] = u0;
      p[6] = u0 + sw;
      normalize();
      break;
    }
    case GeomType::Spline:
    case GeomType::FitSpline:
      if (vertexCount() > 0) setVertex(0, q);
      break;
    case GeomType::Text: p[0] = q.x; p[1] = q.y; break;
  }
}

void Geometry::setEnd(Pt2 q) {
  switch (type) {
    case GeomType::Point: p[0] = q.x; p[1] = q.y; break;
    case GeomType::Line: p[2] = q.x; p[3] = q.y; break;
    case GeomType::Circle:
    case GeomType::Ellipse: break;
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
    case GeomType::EllipseArc: {
      double u0 = normalizeAngle(p[5]);
      double u1 = ellipse().closestParam(q);
      double sw = normalizeAngle(u1 - u0);
      if (sw < 1e-9) sw = 2 * kPi;
      p[5] = u0;
      p[6] = u0 + sw;
      normalize();
      break;
    }
    case GeomType::Spline:
    case GeomType::FitSpline:
      if (vertexCount() > 0) setVertex(vertexCount() - 1, q);
      break;
    case GeomType::Text: p[0] = q.x; p[1] = q.y; break;
  }
}

void Geometry::setCenter(Pt2 c) {
  switch (type) {
    case GeomType::Point: p[0] = c.x; p[1] = c.y; break;
    case GeomType::Line: translate(c - center()); break;
    case GeomType::Circle: p[0] = c.x; p[1] = c.y; break;
    case GeomType::Arc: p[0] = c.x; p[1] = c.y; normalize(); break;
    case GeomType::Ellipse:
    case GeomType::EllipseArc:
    case GeomType::Spline:
    case GeomType::FitSpline:
    case GeomType::Text: translate(c - center()); break;
  }
}

void Geometry::setRadius(double r) {
  if (!isCircular()) return;
  p[2] = std::abs(r);
  if (type == GeomType::Arc) normalize();
}

void Geometry::setAngles(double a0, double a1) {
  if (type != GeomType::Arc && type != GeomType::EllipseArc) return;
  a0 = normalizeAngle(a0);
  a1 = normalizeAngle(a1);
  if (a1 <= a0 + 1e-12) a1 += 2 * kPi;
  size_t i = type == GeomType::Arc ? 3 : 5;
  p[i] = a0;
  p[i + 1] = a1;
  normalize();
}

void Geometry::normalize() {
  if (type == GeomType::Arc) {
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
  } else if (type == GeomType::EllipseArc) {
    p[4] = std::abs(p[4]);
    double u0 = p[5], u1 = p[6];
    double n0 = normalizeAngle(u0);
    u1 += n0 - u0;
    u0 = n0;
    while (u1 <= u0 + 1e-12) u1 += 2 * kPi;
    while (u1 > u0 + 2 * kPi + 1e-12) u1 -= 2 * kPi;
    p[5] = u0;
    p[6] = u1;
    EllipseFrame f = ellipse();
    Pt2 s = f.value(u0), e = f.value(u1);
    p[7] = s.x;
    p[8] = s.y;
    p[9] = e.x;
    p[10] = e.y;
  } else if (type == GeomType::Ellipse) {
    p[4] = std::abs(p[4]);
  }
}

void Geometry::translate(Pt2 d) {
  switch (type) {
    case GeomType::Point: p[0] += d.x; p[1] += d.y; break;
    case GeomType::Line: p[0] += d.x; p[1] += d.y; p[2] += d.x; p[3] += d.y; break;
    case GeomType::Circle: p[0] += d.x; p[1] += d.y; break;
    case GeomType::Arc: p[0] += d.x; p[1] += d.y; normalize(); break;
    case GeomType::Ellipse:
    case GeomType::EllipseArc:
      p[0] += d.x; p[1] += d.y; p[2] += d.x; p[3] += d.y;
      normalize();
      break;
    case GeomType::Spline:
    case GeomType::FitSpline:
      for (int i = 0; i < vertexCount(); ++i) setVertex(i, vertex(i) + d);
      break;
    case GeomType::Text: p[0] += d.x; p[1] += d.y; break;
  }
}

void Geometry::transform(Pt2 o, double angle, double k) {
  k = std::abs(k) < 1e-12 ? 1.0 : std::abs(k);
  const double c = std::cos(angle), s = std::sin(angle);
  auto xf = [&](Pt2 q) { return rotateAbout(q, o, c, s, k); };
  switch (type) {
    case GeomType::Point: {
      Pt2 q = xf(center());
      p = {q.x, q.y};
      break;
    }
    case GeomType::Line: {
      Pt2 a = xf(start()), b = xf(end());
      p = {a.x, a.y, b.x, b.y};
      break;
    }
    case GeomType::Circle: {
      Pt2 q = xf(center());
      p = {q.x, q.y, p[2] * k};
      break;
    }
    case GeomType::Arc: {
      Pt2 q = xf(center());
      p[0] = q.x;
      p[1] = q.y;
      p[2] *= k;
      p[3] += angle;
      p[4] += angle;
      normalize();
      break;
    }
    case GeomType::Ellipse:
    case GeomType::EllipseArc: {
      Pt2 q = xf({p[0], p[1]}), f = xf({p[2], p[3]});
      p[0] = q.x;
      p[1] = q.y;
      p[2] = f.x;
      p[3] = f.y;
      p[4] *= k;
      normalize();
      break;
    }
    case GeomType::Spline:
    case GeomType::FitSpline:
      for (int i = 0; i < vertexCount(); ++i) setVertex(i, xf(vertex(i)));
      break;
    case GeomType::Text: {
      Pt2 q = xf({p[0], p[1]});
      p[0] = q.x;
      p[1] = q.y;
      p[2] *= k;
      p[3] += angle;
      break;
    }
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
    case GeomType::Ellipse:
    case GeomType::EllipseArc: {
      // reflected P(u) = c' + a cos(u) M' - b sin(u) N' = P'(-u): the parameter range flips
      Pt2 c = refl({p[0], p[1]}), f = refl({p[2], p[3]});
      p[0] = c.x;
      p[1] = c.y;
      p[2] = f.x;
      p[3] = f.y;
      if (type == GeomType::EllipseArc) {
        double u0 = p[5], u1 = p[6];
        p[5] = -u1;
        p[6] = -u0;
      }
      normalize();
      break;
    }
    case GeomType::Spline:
    case GeomType::FitSpline:
      for (int i = 0; i < vertexCount(); ++i) setVertex(i, refl(vertex(i)));
      break;
    case GeomType::Text: {
      // the anchor is reflected; the glyphs are flipped about the baseline direction
      Pt2 q = refl({p[0], p[1]});
      double lineAngle = u.angle();
      p[0] = q.x;
      p[1] = q.y;
      p[3] = normalizeAngle(2 * lineAngle - p[3] + kPi);
      // Refl(theta) Rot(alpha) = Rot(2 theta - alpha + pi) G with G = flip of the glyph x axis
      mirrored = !mirrored;
      break;
    }
  }
}

bool Geometry::containsAngle(double angle) const {
  if (type == GeomType::Circle || type == GeomType::Ellipse) return true;
  if (type != GeomType::Arc && type != GeomType::EllipseArc) return false;
  double rel = normalizeAngle(angle - startAngle());
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
    case GeomType::Ellipse: {
      EllipseFrame f = ellipse();
      return f.value(f.closestParam(q));
    }
    case GeomType::EllipseArc: {
      EllipseFrame f = ellipse();
      double u = f.closestParam(q);
      if (containsAngle(u)) return f.value(u);
      return q.distance(start()) <= q.distance(end()) ? start() : end();
    }
    case GeomType::Spline:
    case GeomType::FitSpline: {
      BSpline2 s = bspline();
      if (!s.isValid()) return start();
      return s.value(s.closestParam(q));
    }
    case GeomType::Text: {
      Pt2 best = start();
      double bd = 1e300;
      for (const auto& pl : polylines(8))
        for (size_t i = 1; i < pl.size(); ++i) {
          Pt2 d = pl[i] - pl[i - 1];
          double t = std::clamp(projectParam(pl[i - 1], d, q), 0.0, 1.0);
          Pt2 c = pl[i - 1] + d * t;
          if (c.distance(q) < bd) {
            bd = c.distance(q);
            best = c;
          }
        }
      return best;
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
    case GeomType::Ellipse:
    case GeomType::EllipseArc: {
      EllipseFrame f = ellipse();
      return f.derivative(f.closestParam(q)).normalized();
    }
    case GeomType::Spline:
    case GeomType::FitSpline: {
      BSpline2 s = bspline();
      if (!s.isValid()) return {1, 0};
      return s.derivative(s.closestParam(q)).normalized();
    }
    case GeomType::Text: return Pt2::polar(1.0, p[3]);
  }
  return {1, 0};
}

Pt2 Geometry::pointAt(double param) const {
  switch (type) {
    case GeomType::Point: return center();
    case GeomType::Line: return start() + (end() - start()) * param;
    case GeomType::Circle:
    case GeomType::Arc: return center() + Pt2::polar(p[2], param);
    case GeomType::Ellipse:
    case GeomType::EllipseArc: return ellipse().value(param);
    case GeomType::Spline:
    case GeomType::FitSpline: return bspline().value(param);
    case GeomType::Text: return start();
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
    case GeomType::Ellipse: return ellipse().closestParam(q);
    case GeomType::EllipseArc: {
      double u = ellipse().closestParam(q);
      return p[5] + normalizeAngle(u - p[5]);
    }
    case GeomType::Spline:
    case GeomType::FitSpline: return bspline().closestParam(q);
    case GeomType::Text: return 0;
  }
  return 0;
}

std::pair<double, double> Geometry::paramRange() const {
  switch (type) {
    case GeomType::Point: return {0, 0};
    case GeomType::Line: return {0, 1};
    case GeomType::Circle:
    case GeomType::Ellipse: return {0, 2 * kPi};
    case GeomType::Arc: return {p[3], p[4]};
    case GeomType::EllipseArc: return {p[5], p[6]};
    case GeomType::Spline:
    case GeomType::FitSpline: {
      BSpline2 s = bspline();
      return {s.first(), s.last()};
    }
    case GeomType::Text: return {0, 0};
  }
  return {0, 0};
}

std::pair<Pt2, Pt2> Geometry::bbox() const {
  auto ofPoints = [](const std::vector<std::vector<Pt2>>& pls, Pt2 fallback) {
    Pt2 mn = fallback, mx = fallback;
    bool any = false;
    for (const auto& pl : pls)
      for (const Pt2& q : pl) {
        if (!any) {
          mn = mx = q;
          any = true;
        }
        mn = {std::min(mn.x, q.x), std::min(mn.y, q.y)};
        mx = {std::max(mx.x, q.x), std::max(mx.y, q.y)};
      }
    return std::pair<Pt2, Pt2>{mn, mx};
  };
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
    case GeomType::Ellipse:
    case GeomType::EllipseArc: {
      EllipseFrame f = ellipse();
      Pt2 mn = start(), mx = start();
      auto grow = [&](Pt2 q) {
        mn = {std::min(mn.x, q.x), std::min(mn.y, q.y)};
        mx = {std::max(mx.x, q.x), std::max(mx.y, q.y)};
      };
      grow(end());
      // extremal parameters: dx/du = 0 and dy/du = 0
      double ux = std::atan2(f.b * f.minor.x, f.a * f.major.x);
      double uy = std::atan2(f.b * f.minor.y, f.a * f.major.y);
      for (double u : {ux, ux + kPi, uy, uy + kPi})
        if (containsAngle(u)) grow(f.value(u));
      return {mn, mx};
    }
    case GeomType::Spline:
    case GeomType::FitSpline:
    case GeomType::Text: return ofPoints(polylines(32), start());
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
    case GeomType::Ellipse:
    case GeomType::EllipseArc: {
      EllipseFrame f = ellipse();
      double a0 = startAngle(), sw = sweep();
      int n = std::max(12, static_cast<int>(std::ceil(std::max(segments, 48) * sw / (2 * kPi))));
      out.reserve(static_cast<size_t>(n) + 1);
      for (int i = 0; i <= n; ++i) out.push_back(f.value(a0 + sw * i / n));
      break;
    }
    case GeomType::Spline:
    case GeomType::FitSpline: {
      BSpline2 s = bspline();
      if (!s.isValid()) {
        for (int i = 0; i < vertexCount(); ++i) out.push_back(vertex(i));
        break;
      }
      std::vector<double> vals;
      std::vector<int> mults;
      s.distinctKnots(vals, mults);
      int per = std::max(4, segments / std::max<int>(1, static_cast<int>(vals.size()) - 1));
      out = s.sample(std::max(per, 8));
      break;
    }
    case GeomType::Text: {
      auto pls = textPolylines(*this, 6);
      if (!pls.empty()) out = pls.front();
      else out.push_back(start());
      break;
    }
  }
  return out;
}

std::vector<std::vector<Pt2>> Geometry::polylines(int segments) const {
  if (type == GeomType::Text) return textPolylines(*this, std::clamp(segments / 4, 4, 32));
  return {tessellate(segments)};
}

QJsonObject Geometry::toJson() const {
  QJsonObject o;
  o["id"] = id;
  o["type"] = geomTypeName(type);
  QJsonArray params;
  size_t n = p.size();
  if (type == GeomType::Arc) n = 5;         // start/end are derived
  if (type == GeomType::EllipseArc) n = 7;  // start/end are derived
  for (size_t i = 0; i < n && i < p.size(); ++i) params.append(p[i]);
  o["p"] = params;
  if (construction) o["construction"] = true;
  if (projected) o["projected"] = true;
  if (blocked) o["blocked"] = true;
  if (source.isValid()) o["source"] = source.toJson();
  if (type == GeomType::Spline) o["degree"] = degree;
  if (type == GeomType::FitSpline) {
    QJsonArray k;
    for (double v : knots) k.append(v);
    o["params"] = k;
  }
  if (type == GeomType::Text) {
    o["text"] = qs(text);
    if (!font.empty()) o["font"] = qs(font);
    if (bold) o["bold"] = true;
    if (italic) o["italic"] = true;
    if (mirrored) o["mirrored"] = true;
  }
  return o;
}

Geometry Geometry::fromJson(const QJsonObject& o) {
  Geometry g;
  g.id = o["id"].toInt(-1);
  g.type = geomTypeFromName(ss(o["type"].toString())).value_or(GeomType::Line);
  g.p.clear();
  for (const auto& v : o["p"].toArray()) g.p.push_back(v.toDouble());
  size_t need = paramCount(g.type);
  if (need > 0) g.p.resize(need, 0.0);
  else if (g.p.size() % 2) g.p.pop_back();
  g.construction = o["construction"].toBool(false);
  g.projected = o["projected"].toBool(false);
  g.blocked = o["blocked"].toBool(false);
  if (o.contains("source")) g.source = core::ShapeRef::fromJson(o["source"].toObject());
  g.degree = o["degree"].toInt(3);
  for (const auto& v : o["params"].toArray()) g.knots.push_back(v.toDouble());
  if (g.type == GeomType::FitSpline && g.knots.size() != static_cast<size_t>(g.vertexCount()))
    g.knots = chordLengthParams(vertexList(g));
  g.text = ss(o["text"].toString());
  g.font = ss(o["font"].toString());
  g.bold = o["bold"].toBool(false);
  g.italic = o["italic"].toBool(false);
  g.mirrored = o["mirrored"].toBool(false);
  if (g.type == GeomType::Arc || g.type == GeomType::EllipseArc) g.normalize();
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
  if (r.pos == PointPos::Vertex) o["idx"] = r.index;
  return o;
}
GeoRef refFromJson(const QJsonValue& v) {
  GeoRef r;
  if (!v.isObject()) return r;
  QJsonObject o = v.toObject();
  r.geo = o["g"].toInt(-1);
  r.pos = pointPosFromName(ss(o["pos"].toString()));
  if (r.pos == PointPos::Vertex) r.index = o["idx"].toInt(0);
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
  } else if (!name.empty()) {
    o["name"] = qs(name);  // e.g. the "origin" Fix
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
