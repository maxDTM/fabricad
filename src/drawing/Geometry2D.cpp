// FabriCAD drawing: 2D primitive math (sampling, distances, hatching, clipping).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Geometry2D.h"

#include "core/Json.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;

namespace {
QJsonArray ptJson(const Point2& p) { return QJsonArray{p.x, p.y}; }
Point2 ptFrom(const QJsonValue& v) {
  auto a = v.toArray();
  return a.size() >= 2 ? Point2{a[0].toDouble(), a[1].toDouble()} : Point2{};
}
}  // namespace

Curve2D Curve2D::segment(const Point2& a, const Point2& b, LineClass cls) {
  Curve2D c;
  c.type = CurveType::Segment;
  c.cls = cls;
  c.a = a;
  c.b = b;
  return c;
}

Curve2D Curve2D::circle(const Point2& center, double r, LineClass cls) { return arc(center, r, 0.0, 360.0, cls); }

Curve2D Curve2D::arc(const Point2& center, double r, double startDeg, double sweepDeg, LineClass cls) {
  Curve2D c;
  c.type = CurveType::Arc;
  c.cls = cls;
  c.center = center;
  c.radius = r;
  c.startAngle = normalizeAngle(startDeg);
  c.sweep = std::clamp(sweepDeg, -360.0, 360.0);
  return c;
}

Curve2D Curve2D::polyline(std::vector<Point2> pts, LineClass cls) {
  Curve2D c;
  c.type = CurveType::Polyline;
  c.cls = cls;
  c.pts = std::move(pts);
  return c;
}

Point2 Curve2D::start() const { return pointAt(0.0); }
Point2 Curve2D::end() const { return pointAt(1.0); }
Point2 Curve2D::midpoint() const { return pointAt(0.5); }

Point2 Curve2D::pointAt(double t) const {
  switch (type) {
    case CurveType::Segment: return a + (b - a) * t;
    case CurveType::Arc: return center + Point2::polar(startAngle + sweep * t, radius);
    case CurveType::Polyline: {
      if (pts.empty()) return {};
      if (pts.size() == 1) return pts.front();
      double total = length();
      if (total <= 0) return pts.front();
      double target = std::clamp(t, 0.0, 1.0) * total;
      double acc = 0;
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        double l = pts[i].distance(pts[i + 1]);
        if (acc + l >= target) {
          double u = l > 0 ? (target - acc) / l : 0.0;
          return pts[i] + (pts[i + 1] - pts[i]) * u;
        }
        acc += l;
      }
      return pts.back();
    }
  }
  return {};
}

Point2 Curve2D::direction(double t) const {
  switch (type) {
    case CurveType::Segment: return (b - a).normalized();
    case CurveType::Arc: {
      double ang = startAngle + sweep * t;
      Point2 radial = Point2::polar(ang, 1.0);
      Point2 tan = radial.perp();
      return sweep >= 0 ? tan : -tan;
    }
    case CurveType::Polyline: {
      if (pts.size() < 2) return {};
      Point2 p0 = pointAt(std::max(0.0, t - 0.01)), p1 = pointAt(std::min(1.0, t + 0.01));
      return (p1 - p0).normalized();
    }
  }
  return {};
}

double Curve2D::length() const {
  switch (type) {
    case CurveType::Segment: return a.distance(b);
    case CurveType::Arc: return std::abs(deg2rad(sweep)) * radius;
    case CurveType::Polyline: {
      double l = 0;
      for (size_t i = 0; i + 1 < pts.size(); ++i) l += pts[i].distance(pts[i + 1]);
      return l;
    }
  }
  return 0;
}

BBox2 Curve2D::bbox() const {
  BBox2 b;
  switch (type) {
    case CurveType::Segment:
      b.add(a);
      b.add(this->b);
      break;
    case CurveType::Arc: {
      b.add(start());
      b.add(end());
      // extremes at 0/90/180/270 if inside the arc
      for (double ang : {0.0, 90.0, 180.0, 270.0})
        if (angleInArc(ang, startAngle, sweep)) b.add(center + Point2::polar(ang, radius));
      break;
    }
    case CurveType::Polyline:
      for (const auto& p : pts) b.add(p);
      break;
  }
  return b;
}

double Curve2D::distanceTo(const Point2& p) const {
  switch (type) {
    case CurveType::Segment: return distanceToSegment(a, b, p);
    case CurveType::Arc: {
      Point2 d = p - center;
      double dist = d.length();
      if (dist < 1e-12) return radius;
      double ang = d.angleDeg();
      if (isCircle() || angleInArc(ang, startAngle, sweep)) return std::abs(dist - radius);
      return std::min(p.distance(start()), p.distance(end()));
    }
    case CurveType::Polyline: {
      if (pts.empty()) return 1e300;
      if (pts.size() == 1) return p.distance(pts[0]);
      double best = 1e300;
      for (size_t i = 0; i + 1 < pts.size(); ++i) best = std::min(best, distanceToSegment(pts[i], pts[i + 1], p));
      return best;
    }
  }
  return 1e300;
}

std::vector<Point2> Curve2D::sample(double tol) const {
  switch (type) {
    case CurveType::Segment: return {a, b};
    case CurveType::Polyline: return pts;
    case CurveType::Arc: {
      double absSweep = std::abs(sweep);
      // chordal deviation: r(1-cos(step/2)) <= tol
      double step = 360.0;
      if (radius > tol) step = rad2deg(2.0 * std::acos(std::clamp(1.0 - tol / radius, -1.0, 1.0)));
      step = std::clamp(step, 1.0, 30.0);
      int n = std::max(2, static_cast<int>(std::ceil(absSweep / step)));
      std::vector<Point2> out;
      out.reserve(n + 1);
      for (int i = 0; i <= n; ++i) out.push_back(pointAt(static_cast<double>(i) / n));
      return out;
    }
  }
  return {};
}

Curve2D Curve2D::transformed(const Point2& translate, double scale, bool flipY) const {
  auto tp = [&](const Point2& p) { return Point2{translate.x + p.x * scale, translate.y + (flipY ? -p.y : p.y) * scale}; };
  Curve2D c = *this;
  switch (type) {
    case CurveType::Segment:
      c.a = tp(a);
      c.b = tp(b);
      break;
    case CurveType::Arc:
      c.center = tp(center);
      c.radius = radius * scale;
      if (flipY) {
        c.startAngle = normalizeAngle(-startAngle);
        c.sweep = -sweep;
      }
      break;
    case CurveType::Polyline:
      for (auto& p : c.pts) p = tp(p);
      break;
  }
  return c;
}

QJsonObject Curve2D::toJson() const {
  QJsonObject o;
  o["cls"] = lineClassName(cls);
  if (sourceBody != core::kInvalidId) o["body"] = idToJson(sourceBody);
  if (sourceEdge) o["edge"] = sourceEdge;
  if (!sourceName.empty()) o["name"] = QString::fromStdString(sourceName);
  if (sourceSig.isValid()) o["sig"] = sourceSig.toJson();
  switch (type) {
    case CurveType::Segment:
      o["t"] = "seg";
      o["a"] = ptJson(a);
      o["b"] = ptJson(b);
      break;
    case CurveType::Arc:
      o["t"] = "arc";
      o["c"] = ptJson(center);
      o["r"] = radius;
      o["s"] = startAngle;
      o["w"] = sweep;
      break;
    case CurveType::Polyline: {
      o["t"] = "poly";
      QJsonArray arr;
      for (const auto& p : pts) {
        arr.push_back(p.x);
        arr.push_back(p.y);
      }
      o["p"] = arr;
      break;
    }
  }
  return o;
}

Curve2D Curve2D::fromJson(const QJsonObject& o) {
  Curve2D c;
  c.cls = lineClassFromName(o["cls"].toString().toStdString());
  c.sourceBody = idFromJson(o["body"]);
  c.sourceEdge = o["edge"].toInt(0);
  c.sourceName = o["name"].toString().toStdString();
  if (o.contains("sig")) c.sourceSig = core::GeomSignature::fromJson(o["sig"].toObject());
  QString t = o["t"].toString();
  if (t == "arc") {
    c.type = CurveType::Arc;
    c.center = ptFrom(o["c"]);
    c.radius = o["r"].toDouble();
    c.startAngle = o["s"].toDouble();
    c.sweep = o["w"].toDouble();
  } else if (t == "poly") {
    c.type = CurveType::Polyline;
    auto arr = o["p"].toArray();
    for (int i = 0; i + 1 < arr.size(); i += 2) c.pts.push_back({arr[i].toDouble(), arr[i + 1].toDouble()});
  } else {
    c.type = CurveType::Segment;
    c.a = ptFrom(o["a"]);
    c.b = ptFrom(o["b"]);
  }
  return c;
}

double normalizeAngle(double deg) {
  double a = std::fmod(deg, 360.0);
  if (a < 0) a += 360.0;
  if (a >= 360.0) a -= 360.0;
  if (a == 0.0) return 0.0;  // never return -0.0
  return a;
}

bool angleInArc(double a, double startDeg, double sweepDeg) {
  if (std::abs(sweepDeg) >= 360.0 - 1e-9) return true;
  double s = normalizeAngle(startDeg);
  double rel = normalizeAngle(a - s);
  if (sweepDeg >= 0) return rel <= sweepDeg + 1e-9;
  // clockwise: angles from s going negative
  double relCw = rel == 0 ? 0 : 360.0 - rel;
  return relCw <= -sweepDeg + 1e-9;
}

double polygonArea(const std::vector<Point2>& poly) {
  double a = 0;
  for (size_t i = 0, n = poly.size(); i < n; ++i) a += poly[i].cross(poly[(i + 1) % n]);
  return 0.5 * a;
}

bool pointInPolygon(const std::vector<Point2>& poly, const Point2& p) {
  bool in = false;
  for (size_t i = 0, n = poly.size(), j = n - 1; i < n; j = i++) {
    const Point2& pi = poly[i];
    const Point2& pj = poly[j];
    if ((pi.y > p.y) != (pj.y > p.y)) {
      double x = pj.x + (p.y - pj.y) * (pi.x - pj.x) / (pi.y - pj.y);
      if (p.x < x) in = !in;
    }
  }
  return in;
}

std::vector<Curve2D> hatchLoops(const std::vector<std::vector<Point2>>& loops, double angleDeg, double spacing,
                                LineClass cls) {
  std::vector<Curve2D> out;
  if (spacing <= 0) return out;
  // Rotate everything so the hatch lines are horizontal in the rotated frame.
  const Point2 dir = Point2::polar(angleDeg, 1.0);
  const Point2 nrm = dir.perp();
  auto toLocal = [&](const Point2& p) { return Point2{p.dot(dir), p.dot(nrm)}; };
  auto fromLocal = [&](const Point2& q) { return dir * q.x + nrm * q.y; };

  double yMin = 1e300, yMax = -1e300;
  std::vector<std::vector<Point2>> local;
  for (const auto& loop : loops) {
    if (loop.size() < 3) continue;
    std::vector<Point2> l;
    l.reserve(loop.size());
    for (const auto& p : loop) {
      Point2 q = toLocal(p);
      yMin = std::min(yMin, q.y);
      yMax = std::max(yMax, q.y);
      l.push_back(q);
    }
    local.push_back(std::move(l));
  }
  if (local.empty()) return out;
  // Lines at y = k*spacing + spacing/2 (offset so a line never coincides with an axis-aligned edge)
  const double start = (std::floor(yMin / spacing) + 0.5) * spacing;
  std::vector<double> xs;
  for (double y = start; y <= yMax; y += spacing) {
    xs.clear();
    for (const auto& loop : local) {
      for (size_t i = 0, n = loop.size(); i < n; ++i) {
        const Point2& p = loop[i];
        const Point2& q = loop[(i + 1) % n];
        if ((p.y > y) != (q.y > y)) xs.push_back(p.x + (y - p.y) * (q.x - p.x) / (q.y - p.y));
      }
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {
      if (xs[i + 1] - xs[i] < 1e-9) continue;
      out.push_back(Curve2D::segment(fromLocal({xs[i], y}), fromLocal({xs[i + 1], y}), cls));
    }
  }
  return out;
}

namespace {
// Clip segment ab to the disc; returns false when fully outside.
bool clipSegmentToCircle(Point2 a, Point2 b, const Point2& c, double r, Point2& oa, Point2& ob) {
  Point2 d = b - a;
  Point2 f = a - c;
  double A = d.dot(d);
  if (A < 1e-18) {
    if (f.length() <= r) { oa = a; ob = b; return true; }
    return false;
  }
  double B = 2 * f.dot(d);
  double C = f.dot(f) - r * r;
  double disc = B * B - 4 * A * C;
  if (disc < 0) return false;
  double sq = std::sqrt(disc);
  double t0 = (-B - sq) / (2 * A), t1 = (-B + sq) / (2 * A);
  t0 = std::max(0.0, t0);
  t1 = std::min(1.0, t1);
  if (t1 <= t0) return false;
  oa = a + d * t0;
  ob = a + d * t1;
  return true;
}
}  // namespace

std::vector<Curve2D> clipToCircle(const Curve2D& c, const Point2& center, double radius) {
  std::vector<Curve2D> out;
  if (c.type == CurveType::Arc) {
    // Fast path: arc fully inside the circle stays an arc (keeps true circles for dimensions).
    BBox2 b = c.bbox();
    bool inside = true;
    for (const Point2& p : {b.min, b.max, Point2{b.min.x, b.max.y}, Point2{b.max.x, b.min.y}})
      if (p.distance(center) > radius) inside = false;
    if (inside) {
      out.push_back(c);
      return out;
    }
  }
  if (c.type == CurveType::Segment) {
    Point2 a, b;
    if (clipSegmentToCircle(c.a, c.b, center, radius, a, b)) {
      Curve2D s = c;
      s.a = a;
      s.b = b;
      out.push_back(s);
    }
    return out;
  }
  std::vector<Point2> pts = c.sample(0.02);
  std::vector<Point2> run;
  auto flush = [&]() {
    if (run.size() >= 2) {
      Curve2D p = Curve2D::polyline(run, c.cls);
      p.sourceBody = c.sourceBody;
      p.sourceEdge = c.sourceEdge;
      p.sourceName = c.sourceName;
      p.sourceSig = c.sourceSig;
      out.push_back(std::move(p));
    }
    run.clear();
  };
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    Point2 a, b;
    if (clipSegmentToCircle(pts[i], pts[i + 1], center, radius, a, b)) {
      if (run.empty() || run.back().distance(a) > 1e-9) {
        flush();
        run.push_back(a);
      }
      run.push_back(b);
    } else {
      flush();
    }
  }
  flush();
  return out;
}

std::optional<Point2> lineIntersection(const Point2& p1, const Point2& d1, const Point2& p2, const Point2& d2) {
  double den = d1.cross(d2);
  if (std::abs(den) < 1e-12) return std::nullopt;
  double t = (p2 - p1).cross(d2) / den;
  return p1 + d1 * t;
}

Point2 closestOnSegment(const Point2& a, const Point2& b, const Point2& p) {
  Point2 d = b - a;
  double l2 = d.dot(d);
  if (l2 < 1e-18) return a;
  double t = std::clamp((p - a).dot(d) / l2, 0.0, 1.0);
  return a + d * t;
}

double distanceToSegment(const Point2& a, const Point2& b, const Point2& p) {
  return p.distance(closestOnSegment(a, b, p));
}

}  // namespace fabricad::drawing
