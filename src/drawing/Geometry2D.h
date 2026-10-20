// FabriCAD drawing: 2D drawing primitives produced by projection and consumed by the
// renderer, dimensions and exporters. Pure math, no Qt/OCCT types (worker-thread safe).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "drawing/DrawingTypes.h"

#include <QJsonObject>

#include <optional>
#include <vector>

namespace fabricad::drawing {

enum class CurveType { Segment, Arc, Polyline };

/// One drawable curve in view-local coordinates (model mm, Y up).
struct Curve2D {
  CurveType type = CurveType::Segment;
  LineClass cls = LineClass::Visible;
  // Segment
  Point2 a, b;
  // Arc / circle: CCW positive sweep in degrees; full circle: sweep == 360
  Point2 center;
  double radius = 0;
  double startAngle = 0;  ///< degrees
  double sweep = 0;       ///< degrees, signed (negative = clockwise)
  // Polyline
  std::vector<Point2> pts;
  // Associativity: 3D origin of this curve (kInvalidId / 0 when unknown, e.g. silhouettes)
  core::ObjectId sourceBody = core::kInvalidId;
  int sourceEdge = 0;  ///< 1-based edge index in the source body shape, 0 = unknown
  /// Stable identity of the 3D edge: its history name ("{Extrude1/Side/c1|Extrude1/End}") and
  /// its body-local geometric signature. Indices are renumbered by every upstream edit; these
  /// are not. Empty / invalid when the projection could not recover them.
  std::string sourceName;
  core::GeomSignature sourceSig;

  static Curve2D segment(const Point2& a, const Point2& b, LineClass cls = LineClass::Visible);
  static Curve2D circle(const Point2& c, double r, LineClass cls = LineClass::Visible);
  static Curve2D arc(const Point2& c, double r, double startDeg, double sweepDeg, LineClass cls = LineClass::Visible);
  static Curve2D polyline(std::vector<Point2> pts, LineClass cls = LineClass::Visible);

  bool isCircle() const { return type == CurveType::Arc && std::abs(std::abs(sweep) - 360.0) < 1e-9; }
  bool isLine() const { return type == CurveType::Segment; }
  Point2 start() const;
  Point2 end() const;
  Point2 midpoint() const;
  /// Point at normalised parameter t in [0,1].
  Point2 pointAt(double t) const;
  /// Unit tangent direction (segments; arcs at parameter t).
  Point2 direction(double t = 0.5) const;
  double length() const;
  BBox2 bbox() const;
  /// Shortest distance from p to the curve.
  double distanceTo(const Point2& p) const;
  /// Polyline approximation with chordal deviation <= tol (segments: 2 points).
  std::vector<Point2> sample(double tol = 0.05) const;
  Curve2D transformed(const Point2& translate, double scale, bool flipY) const;

  QJsonObject toJson() const;
  static Curve2D fromJson(const QJsonObject& o);
};

/// Normalise an angle in degrees to [0, 360).
double normalizeAngle(double deg);
/// True if angle `a` (deg) lies within the arc defined by start/sweep.
bool angleInArc(double a, double startDeg, double sweepDeg);

/// Signed area of a polygon (positive = CCW).
double polygonArea(const std::vector<Point2>& poly);
bool pointInPolygon(const std::vector<Point2>& poly, const Point2& p);

/// Parallel hatch lines (as segments) covering the even-odd interior of `loops` (polygons,
/// possibly with holes). angleDeg: direction of the lines; spacing: distance between lines.
std::vector<Curve2D> hatchLoops(const std::vector<std::vector<Point2>>& loops, double angleDeg, double spacing,
                                LineClass cls = LineClass::Hatch);

/// Clip a curve to the inside of a circle. Arcs/polylines are approximated by polylines.
std::vector<Curve2D> clipToCircle(const Curve2D& c, const Point2& center, double radius);

/// Intersection of two infinite lines (p + t*d). nullopt if parallel.
std::optional<Point2> lineIntersection(const Point2& p1, const Point2& d1, const Point2& p2, const Point2& d2);
/// Closest point on segment ab to p.
Point2 closestOnSegment(const Point2& a, const Point2& b, const Point2& p);
double distanceToSegment(const Point2& a, const Point2& b, const Point2& p);

/// A vertex of a view (curve endpoint or arc centre) for snapping/dimensioning.
struct Vertex2D {
  Point2 p;
  core::ObjectId sourceBody = core::kInvalidId;
  int sourceEdge = 0;
  int role = 0;  ///< 0 start, 1 end, 2 centre
  std::string sourceName;        ///< see Curve2D::sourceName
  core::GeomSignature sourceSig;

  static Vertex2D of(const Curve2D& c, const Point2& p, int role) {
    return Vertex2D{p, c.sourceBody, c.sourceEdge, role, c.sourceName, c.sourceSig};
  }
};

}  // namespace fabricad::drawing
