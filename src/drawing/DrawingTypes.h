// FabriCAD drawing: shared enums and small value types of the drawing module.
// Coordinate conventions used throughout the module:
//   * view-local coordinates: model millimetres (unscaled), Y up, origin at the projected
//     centre of the source geometry (what HLR produces);
//   * sheet coordinates: millimetres, origin top-left, X right, Y down (Qt/PDF/SVG);
//   * DrawingView::toSheet()/fromSheet() convert (position + scale + Y flip).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <cmath>
#include <string>
#include <vector>

namespace fabricad::drawing {

constexpr double kPi = 3.14159265358979323846;
inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }

/// Plain 2D point/vector (mm).
struct Point2 {
  double x = 0, y = 0;
  constexpr Point2() = default;
  constexpr Point2(double x_, double y_) : x(x_), y(y_) {}
  Point2 operator+(const Point2& o) const { return {x + o.x, y + o.y}; }
  Point2 operator-(const Point2& o) const { return {x - o.x, y - o.y}; }
  Point2 operator*(double s) const { return {x * s, y * s}; }
  Point2 operator/(double s) const { return {x / s, y / s}; }
  Point2 operator-() const { return {-x, -y}; }
  Point2& operator+=(const Point2& o) { x += o.x; y += o.y; return *this; }
  Point2& operator-=(const Point2& o) { x -= o.x; y -= o.y; return *this; }
  bool operator==(const Point2& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Point2& o) const { return !(*this == o); }
  double dot(const Point2& o) const { return x * o.x + y * o.y; }
  double cross(const Point2& o) const { return x * o.y - y * o.x; }
  double length() const { return std::sqrt(x * x + y * y); }
  double distance(const Point2& o) const { return (*this - o).length(); }
  Point2 normalized() const { double l = length(); return l > 1e-15 ? *this / l : Point2{}; }
  Point2 perp() const { return {-y, x}; }  ///< rotated +90 degrees
  /// Angle in degrees in [0, 360).
  double angleDeg() const { double a = rad2deg(std::atan2(y, x)); return a < 0 ? a + 360.0 : a; }
  static Point2 polar(double angleDeg, double r) { return {r * std::cos(deg2rad(angleDeg)), r * std::sin(deg2rad(angleDeg))}; }
};
inline Point2 operator*(double s, const Point2& p) { return p * s; }

/// Axis-aligned bounding box.
struct BBox2 {
  Point2 min{1e300, 1e300}, max{-1e300, -1e300};
  bool isValid() const { return min.x <= max.x && min.y <= max.y; }
  void add(const Point2& p) {
    if (p.x < min.x) min.x = p.x;
    if (p.y < min.y) min.y = p.y;
    if (p.x > max.x) max.x = p.x;
    if (p.y > max.y) max.y = p.y;
  }
  void add(const BBox2& b) { if (b.isValid()) { add(b.min); add(b.max); } }
  double width() const { return isValid() ? max.x - min.x : 0.0; }
  double height() const { return isValid() ? max.y - min.y : 0.0; }
  Point2 center() const { return isValid() ? (min + max) * 0.5 : Point2{}; }
  bool contains(const Point2& p, double tol = 0.0) const {
    return isValid() && p.x >= min.x - tol && p.x <= max.x + tol && p.y >= min.y - tol && p.y <= max.y + tol;
  }
  BBox2 expanded(double d) const { BBox2 b = *this; if (isValid()) { b.min -= {d, d}; b.max += {d, d}; } return b; }
};

/// Line classification of projected/drawn curves (drives pen, layer, visibility toggles).
enum class LineClass {
  Visible,     ///< visible sharp edges and outlines (0.5 mm continuous)
  Hidden,      ///< hidden edges (0.25 mm dashed)
  Tangent,     ///< smooth (G1) visible edges, "tangent edges" toggle (0.18 mm thin)
  Section,     ///< section-cut boundary (drawn as visible)
  Hatch,       ///< hatch lines of a section (0.18 mm)
  Center,      ///< centre marks / centre lines (0.18 mm chain)
  Dimension,   ///< dimension / extension lines (0.18 mm)
  Annotation,  ///< text frames, balloons, leaders (0.25 mm)
  Border,      ///< sheet frame, title block (0.5 / 0.25 mm)
  Detail,      ///< detail circle on the parent view (0.25 mm)
};
const char* lineClassName(LineClass c);
LineClass lineClassFromName(const std::string& s);
/// ISO 128 pen widths in mm for each class.
double lineClassWidth(LineClass c);

/// Standard view orientations (names follow Fusion 360's base-view dialog).
enum class ViewOrientation {
  Front, Back, Top, Bottom, Left, Right,
  IsoTopRight, IsoTopLeft, IsoBottomRight, IsoBottomLeft,
  Custom,  ///< explicit frame stored in the view
};
const char* viewOrientationName(ViewOrientation o);
ViewOrientation viewOrientationFromName(const std::string& s);
std::vector<ViewOrientation> standardOrientations();

enum class ProjectionStandard { FirstAngle, ThirdAngle };  ///< ISO (first) vs ASME (third)
const char* projectionStandardName(ProjectionStandard s);
ProjectionStandard projectionStandardFromName(const std::string& s);

enum class ViewType { Base, Projected, Section, Detail };
const char* viewTypeName(ViewType t);
ViewType viewTypeFromName(const std::string& s);

/// Where a projected view sits relative to its parent (keeps alignment when moving).
enum class ViewAlignment { None, Horizontal, Vertical };
const char* viewAlignmentName(ViewAlignment a);
ViewAlignment viewAlignmentFromName(const std::string& s);

/// Which side of the parent a projected view was dragged to.
enum class ProjectionDirection { Left, Right, Above, Below, AboveLeft, AboveRight, BelowLeft, BelowRight };
const char* projectionDirectionName(ProjectionDirection d);
ProjectionDirection projectionDirectionFromName(const std::string& s);
/// Classify an offset (sheet coordinates, Y down) from the parent centre into a direction.
ProjectionDirection classifyDirection(const Point2& sheetOffset);
bool isDiagonal(ProjectionDirection d);
/// Unit vector (sheet coordinates, Y down) pointing from the parent towards that side.
Point2 projectionDirectionVector(ProjectionDirection d);

enum class TangentEdgeMode { Off, Full };  ///< Fusion also has "Shortened"; omitted
const char* tangentEdgeModeName(TangentEdgeMode m);
TangentEdgeMode tangentEdgeModeFromName(const std::string& s);

enum class DimensionType {
  Linear,     ///< horizontal or vertical distance (auto by text position) between two points
  Horizontal,
  Vertical,
  Aligned,    ///< distance parallel to the two points
  Radius,
  Diameter,
  Angle,
};
const char* dimensionTypeName(DimensionType t);
DimensionType dimensionTypeFromName(const std::string& s);

/// Standard drawing scales offered in dialogs (value = sheet mm / model mm).
struct ScalePreset {
  const char* label;
  double value;
};
const std::vector<ScalePreset>& scalePresets();
/// "1:2", "2:1", "1:1" ... from a scale value (nearest preset or "x.xx:1").
std::string scaleLabel(double scale);
/// Parse "1:2", "2:1", "0.5" -> value; nullopt-like: returns false on failure.
bool parseScale(const std::string& text, double& value);

/// Text/line sizes per ISO 3098 / ISO 128 (mm on paper).
struct DrawingStyle {
  double dimTextHeight = 3.5;
  double noteTextHeight = 3.5;
  double titleTextHeight = 5.0;
  double arrowLength = 3.0;
  double arrowWidth = 1.0;
  double extensionGap = 1.0;        ///< gap between geometry and extension line
  double extensionOvershoot = 2.0;  ///< extension line beyond the dimension line
  double hatchSpacing = 3.0;
  double hatchAngle = 45.0;
  double centerMarkExtension = 2.0; ///< centre mark beyond the circle
  double balloonRadius = 4.0;
  std::string fontFamily = "DejaVu Sans";
};

}  // namespace fabricad::drawing
