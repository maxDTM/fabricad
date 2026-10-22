// FabriCAD sketch: 2D geometry and constraint value types (plane coordinates, mm / rad).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <QJsonObject>

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::sketch {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGeomTol = 1e-7;  ///< mm, "same point" tolerance for topology

/// 2D point / vector in sketch-plane coordinates (mm).
struct Pt2 {
  double x = 0, y = 0;
  constexpr Pt2() = default;
  constexpr Pt2(double x_, double y_) : x(x_), y(y_) {}
  Pt2 operator+(const Pt2& o) const { return {x + o.x, y + o.y}; }
  Pt2 operator-(const Pt2& o) const { return {x - o.x, y - o.y}; }
  Pt2 operator*(double s) const { return {x * s, y * s}; }
  Pt2 operator/(double s) const { return {x / s, y / s}; }
  Pt2 operator-() const { return {-x, -y}; }
  Pt2& operator+=(const Pt2& o) { x += o.x; y += o.y; return *this; }
  Pt2& operator-=(const Pt2& o) { x -= o.x; y -= o.y; return *this; }
  bool operator==(const Pt2& o) const { return x == o.x && y == o.y; }
  double dot(const Pt2& o) const { return x * o.x + y * o.y; }
  double cross(const Pt2& o) const { return x * o.y - y * o.x; }
  double length() const { return std::sqrt(x * x + y * y); }
  double distance(const Pt2& o) const { return (*this - o).length(); }
  Pt2 normalized() const { double l = length(); return l > 0 ? *this / l : *this; }
  Pt2 perp() const { return {-y, x}; }  ///< rotated 90 deg counter-clockwise
  double angle() const { return std::atan2(y, x); }
  static Pt2 polar(double r, double angle) { return {r * std::cos(angle), r * std::sin(angle)}; }
  bool near(const Pt2& o, double tol = kGeomTol) const { return distance(o) <= tol; }
};
inline Pt2 operator*(double s, const Pt2& p) { return p * s; }

enum class GeomType { Point, Line, Circle, Arc, Ellipse, EllipseArc, Spline, FitSpline, Text };
const char* geomTypeName(GeomType t);
std::optional<GeomType> geomTypeFromName(const std::string& s);

/// Which point of a geometry a reference means. `None` = the curve itself. `Vertex` is an
/// indexed point (GeoRef::index): spline control / fit points.
enum class PointPos { None, Start, End, Center, Mid, Vertex };
const char* pointPosName(PointPos p);
PointPos pointPosFromName(const std::string& s);
core::SketchPointRole toRole(PointPos p);
PointPos fromRole(core::SketchPointRole r);
PointPos fromRole(int subIndex);
/// ShapeRef::subIndex encoding of a point reference (roles 0..4, indexed vertices 16 + index).
constexpr int kVertexSubIndexBase = 16;

struct BSpline2;
struct GeoRef;
/// ShapeRef::subIndex of a point reference / the reference decoded from (geo, subIndex).
int toSubIndex(const GeoRef& r);
GeoRef fromSubIndex(int geo, int subIndex);
struct EllipseFrame;

/// One sketch entity. Parameter layout (all mm / radians):
///   Point : x, y
///   Line  : x1, y1, x2, y2
///   Circle: cx, cy, r
///   Arc   : cx, cy, r, startAngle, endAngle, sx, sy, ex, ey   (counter-clockwise from start to end;
///           start/end points are kept consistent with the angles by normalize())
///   Ellipse   : cx, cy, fx, fy, b          (centre, focus1 on the major axis, minor radius;
///               PlaneGCS layout, major radius a = sqrt(|f-c|^2 + b^2))
///   EllipseArc: cx, cy, fx, fy, b, u0, u1, sx, sy, ex, ey  (eccentric-anomaly parameters u,
///               ccw from u0 to u1; start/end points derived by normalize())
///   Spline    : x0, y0, x1, y1, ...        (control points = poles; `degree`, clamped uniform knots)
///   FitSpline : x0, y0, x1, y1, ...        (fit points the curve passes through; `knots` holds the
///               interpolation parameter of each fit point, fixed at creation so the curve stays
///               linear in the fit points for the solver; cubic, or lower for < 4 points)
///   Text      : x, y, height, angle        (baseline-left anchor, cap height in mm, radians;
///               `text`, `font`, `bold`, `italic`, `mirrored`)
struct Geometry {
  int id = -1;
  GeomType type = GeomType::Line;
  std::vector<double> p;
  bool construction = false;  ///< excluded from profiles, drawn dashed
  bool projected = false;     ///< comes from 3D geometry (source), solver treats it as fixed
  bool blocked = false;       ///< user "Fix" on the whole entity
  core::ShapeRef source;      ///< for projected geometry
  int degree = 3;             ///< Spline: requested degree
  std::vector<double> knots;  ///< FitSpline: parameter per fit point
  std::string text;           ///< Text: the string
  std::string font;           ///< Text: font family (empty = defaultTextFont())
  bool bold = false, italic = false, mirrored = false;  ///< Text style

  static Geometry makePoint(Pt2 p);
  static Geometry makeLine(Pt2 a, Pt2 b);
  static Geometry makeCircle(Pt2 center, double radius);
  /// Counter-clockwise arc from `startAngle` to `endAngle` (radians, endAngle > startAngle after normalisation).
  static Geometry makeArc(Pt2 center, double radius, double startAngle, double endAngle);
  /// Arc through three points (null-opt if collinear).
  static std::optional<Geometry> makeArc3Points(Pt2 a, Pt2 mid, Pt2 b);
  /// Arc from start point, end point and a centre (direction chosen so the arc goes ccw from start).
  static Geometry makeArcCenter(Pt2 center, Pt2 start, Pt2 end, bool ccw = true);
  /// Ellipse from centre, major radius along `majorAngle` (radians) and minor radius (swapped if a < b).
  static Geometry makeEllipse(Pt2 center, double majorRadius, double minorRadius, double majorAngle = 0.0);
  /// Elliptical arc of that ellipse from parameter u0 to u1 (ccw, eccentric anomaly).
  static Geometry makeEllipseArc(Pt2 center, double majorRadius, double minorRadius, double majorAngle, double u0, double u1);
  /// Control-point spline (degree reduced for few points).
  static Geometry makeControlSpline(const std::vector<Pt2>& poles, int degree = 3);
  /// Fit-point spline through the points (chord-length parameters).
  static Geometry makeFitSpline(const std::vector<Pt2>& fitPoints);
  /// Sketch text at a baseline-left anchor.
  static Geometry makeText(Pt2 anchor, const std::string& text, double height, double angle = 0.0, const std::string& font = {});

  // --- typed access ------------------------------------------------------------
  Pt2 start() const;      ///< line/arc start, point position for Point, circle: point at angle 0
  Pt2 end() const;
  Pt2 center() const;     ///< circle/arc centre; midpoint for lines; the point for points
  Pt2 midpoint() const;   ///< line midpoint, arc mid-angle point, circle: point at angle 0
  double radius() const;
  double startAngle() const;
  double endAngle() const;
  double sweep() const;   ///< arc: endAngle - startAngle (>= 0)
  double length() const;  ///< curve length (0 for points)
  Pt2 direction() const;  ///< line unit direction (start -> end)
  Pt2 point(PointPos pos) const;
  /// Points a user can snap to / reference on this entity.
  std::vector<PointPos> pointPositions() const;
  bool isCurve() const { return type != GeomType::Point; }
  bool isCircular() const { return type == GeomType::Circle || type == GeomType::Arc; }
  bool isElliptical() const { return type == GeomType::Ellipse || type == GeomType::EllipseArc; }
  bool isSpline() const { return type == GeomType::Spline || type == GeomType::FitSpline; }
  /// Closed curve without end points (circle, ellipse).
  bool isClosedCurve() const { return type == GeomType::Circle || type == GeomType::Ellipse; }
  /// Spline control / fit points (PointPos::Vertex indices 0..n-1); 0 for other types.
  int vertexCount() const { return isSpline() ? static_cast<int>(p.size() / 2) : 0; }
  Pt2 vertex(int index) const { return {p[static_cast<size_t>(2 * index)], p[static_cast<size_t>(2 * index + 1)]}; }
  void setVertex(int index, Pt2 q) { p[static_cast<size_t>(2 * index)] = q.x; p[static_cast<size_t>(2 * index + 1)] = q.y; }
  /// Every referencable point of this geometry (geo = id), including spline vertices.
  std::vector<GeoRef> pointRefs() const;
  /// Position of a referenced point (pos + vertex index).
  Pt2 point(PointPos pos, int index) const;
  /// Ellipse frame (Ellipse / EllipseArc).
  EllipseFrame ellipse() const;
  /// The B-spline of a Spline / FitSpline (empty for other types).
  BSpline2 bspline() const;
  double majorRadius() const;
  double minorRadius() const;
  /// Is the solver allowed to move this entity? (not projected, not blocked)
  bool isMovable() const { return !projected && !blocked; }
  /// Hidden helper standing for the sketch origin or an origin axis (Sketch::originHelper()):
  /// projected (fixed) construction geometry without a 3D source.
  bool isOriginHelper() const { return projected && construction && !source.isValid(); }

  // --- setters that keep arcs consistent ------------------------------------------
  void setStart(Pt2 p);
  void setEnd(Pt2 p);
  void setCenter(Pt2 c);
  void setRadius(double r);
  void setAngles(double a0, double a1);
  /// Recompute arc start/end points from centre/radius/angles (call after solving).
  void normalize();
  /// Translate the whole entity.
  void translate(Pt2 d);
  /// Mirror about a line through `a` with direction `d`.
  void mirror(Pt2 a, Pt2 d);
  /// Similarity transform about `origin`: rotate by `angle` (radians), then scale by `scale` (> 0).
  void transform(Pt2 origin, double angle, double scale);

  // --- geometric queries ---------------------------------------------------------------
  /// Closest point on the (finite) curve to q.
  Pt2 closestPoint(Pt2 q) const;
  double distanceTo(Pt2 q) const { return closestPoint(q).distance(q); }
  /// Unit tangent at a point of the curve (lines: direction; circles: ccw tangent).
  Pt2 tangentAt(Pt2 q) const;
  /// Point for a curve parameter: lines t in [0,1]; circles/arcs angle (radians).
  Pt2 pointAt(double param) const;
  /// Curve parameter of the closest point.
  double paramOf(Pt2 q) const;
  /// Parameter range (lines 0..1, circles 0..2pi, arcs start..end).
  std::pair<double, double> paramRange() const;
  /// True if the angle lies within the arc's sweep (circles: always).
  bool containsAngle(double angle) const;
  /// Bounding box (min, max).
  std::pair<Pt2, Pt2> bbox() const;
  /// Sample the curve as a polyline (for display); points count depends on `segments`.
  std::vector<Pt2> tessellate(int segments = 32) const;
  /// Display polylines: one for ordinary curves, one per contour for text.
  std::vector<std::vector<Pt2>> polylines(int segments = 32) const;

  QJsonObject toJson() const;
  static Geometry fromJson(const QJsonObject& o);
};

/// Constraint kinds. Dimensional constraints carry a value (mm or degrees in `value`).
enum class ConstraintType {
  Coincident,     ///< a=point, b=point        (point/point)
  PointOnCurve,   ///< a=point, b=curve
  Horizontal,     ///< a=line  | a=point,b=point
  Vertical,       ///< a=line  | a=point,b=point
  Parallel,       ///< a=line, b=line
  Perpendicular,  ///< a=line, b=line
  Tangent,        ///< a=curve, b=curve (one a line or both circular); if both carry point positions: tangent at that shared endpoint
  Equal,          ///< lines: equal length; circles/arcs: equal radius
  Concentric,     ///< a=circle/arc, b=circle/arc
  Collinear,      ///< a=line, b=line
  Midpoint,       ///< a=point, b=line
  Fix,            ///< a=point or a=curve (whole entity blocked)
  Symmetric,      ///< a=point, b=point, c=line (or c=point)
  Distance,       ///< a=point,b=point | a=point,b=line | a=line (length) | a=line,b=line (parallel distance)
  DistanceX,      ///< a=point,b=point (signed b.x - a.x) | a=line
  DistanceY,      ///< a=point,b=point | a=line
  Radius,         ///< a=circle/arc
  Diameter,       ///< a=circle/arc
  Angle,          ///< a=line,b=line (degrees) | a=line (angle to +X)
  MajorRadius,    ///< a=ellipse/elliptical arc: semi-major axis
  MinorRadius,    ///< a=ellipse/elliptical arc: semi-minor axis
};

/// Which sketch-origin reference a helper geometry stands for (Sketch::originHelper()).
enum class OriginRef { Point, XAxis, YAxis };
const char* constraintTypeName(ConstraintType t);
std::optional<ConstraintType> constraintTypeFromName(const std::string& s);
bool isDimensional(ConstraintType t);

/// Reference to a geometry or one of its points.
struct GeoRef {
  int geo = -1;
  PointPos pos = PointPos::None;
  int index = -1;  ///< PointPos::Vertex: spline control / fit point index
  bool isValid() const { return geo >= 0; }
  bool isPoint() const { return geo >= 0 && pos != PointPos::None; }
  bool operator==(const GeoRef&) const = default;
};

struct Constraint {
  int id = -1;
  ConstraintType type = ConstraintType::Coincident;
  GeoRef a, b, c;
  double value = 0;          ///< mm or degrees (dimensional constraints)
  std::string expression;    ///< optional expression ("width/2"); value is the evaluated result
  bool driven = false;       ///< reference dimension: measured, not enforced
  std::string name;          ///< "d1" for dimensions (usable as a parameter)
  Pt2 labelPos;              ///< dimension text position in sketch coords
  bool hasLabelPos = false;
  bool internal = false;     ///< created automatically (inference), not shown as a glyph

  bool isDimensional() const { return sketch::isDimensional(type); }
  /// Horizontal / vertical distance between two points (or along a line): stored signed (the
  /// solver needs the side), but a length to the user, shown and typed as an absolute value.
  bool isRelativeOffset() const {
    return (type == ConstraintType::DistanceX || type == ConstraintType::DistanceY) && (b.isValid() || !a.isPoint());
  }
  bool references(int geoId) const { return a.geo == geoId || b.geo == geoId || c.geo == geoId; }
  QJsonObject toJson() const;
  static Constraint fromJson(const QJsonObject& o);
};

// --- 2D math helpers ----------------------------------------------------------------------

/// Wrap angle to [0, 2pi).
double normalizeAngle(double a);
/// Smallest signed difference b - a in (-pi, pi].
double angleDelta(double a, double b);
/// Intersection of two infinite lines (a + t*d, b + s*e). nullopt if parallel.
std::optional<Pt2> intersectLines(Pt2 a, Pt2 d, Pt2 b, Pt2 e);
/// Intersections of an infinite line with a circle (0, 1 or 2 points).
std::vector<Pt2> intersectLineCircle(Pt2 a, Pt2 d, Pt2 c, double r);
/// Intersections of two circles.
std::vector<Pt2> intersectCircles(Pt2 c1, double r1, Pt2 c2, double r2);
/// Intersections of two finite curves (within their extents).
std::vector<Pt2> intersectCurves(const Geometry& g1, const Geometry& g2, double tol = kGeomTol);
/// Projection parameter of q onto the infinite line through a with direction d (not normalised).
double projectParam(Pt2 a, Pt2 d, Pt2 q);
/// Signed distance of q to the infinite line through a with direction d (positive on the left).
double signedDistanceToLine(Pt2 a, Pt2 d, Pt2 q);
/// Circumcircle centre of three points; nullopt if collinear.
std::optional<Pt2> circumcenter(Pt2 a, Pt2 b, Pt2 c);

}  // namespace fabricad::sketch
