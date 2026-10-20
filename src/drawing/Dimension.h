// FabriCAD drawing: dimensions attached to projected geometry (associative).
// A DimRef names a projected curve/vertex by its 3D origin: the edge's history name (stable
// across upstream edits), its body-local geometric signature, and lastly the edge index (which
// every upstream edit renumbers) with a view-local fallback position. It is re-resolved after
// every projection; a ref that cannot be found with confidence leaves the dimension orphaned
// instead of silently attaching it to another edge.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"
#include "core/Units.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"
#include "drawing/Tolerance.h"

#include <QJsonObject>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::drawing {

struct DrawingView;

/// Reference to a projected curve or vertex of a view.
struct DimRef {
  bool isVertex = false;
  int vertexRole = 0;          ///< Vertex2D::role (0 start, 1 end, 2 centre)
  core::ObjectId body = core::kInvalidId;
  int edge = 0;                ///< 1-based 3D edge index (0 = unknown, fall back to position)
  std::string name;            ///< history name of the 3D edge ("" = unknown)
  core::GeomSignature sig;     ///< body-local signature of the 3D edge (invalid = unknown)
  Point2 fallback;             ///< view-local position at creation time

  static DimRef toCurve(const Curve2D& c);
  static DimRef toVertex(const Vertex2D& v);
  QJsonObject toJson() const;
  static DimRef fromJson(const QJsonObject& o);
};

/// Resolved target of a DimRef.
struct DimTarget {
  bool ok = false;
  bool isVertex = false;
  Point2 point;      ///< vertex position (view-local)
  Curve2D curve;     ///< curve when !isVertex
  /// How the target was found: 0 none, 1 history name, 2 signature, 3 index (checked against
  /// the fallback position), 4 fallback position only.
  int method = 0;
};
/// Resolve against the view's current geometry: history name, then body-local signature, then
/// the edge index (accepted only when the result lies near the stored fallback position), then
/// any curve at the fallback position within `tol`. `ok == false` means the dimension is
/// orphaned: nothing in the view can be identified as the referenced edge.
DimTarget resolveDimRef(const DimRef& ref, const DrawingView& view, double tol = 0.5);

struct Dimension {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  DimensionType type = DimensionType::Linear;
  std::vector<DimRef> refs;      ///< 1 (curve) or 2 (points/lines)
  Point2 textPos;                ///< sheet coordinates of the text anchor (drives line offset)
  std::string overrideText;      ///< non-empty replaces the measured text
  std::string prefix, suffix;    ///< e.g. "2x ", " TYP"
  int precision = -1;            ///< -1 = document precision
  DimTolerance tolerance;        ///< tolerance / ISO fit display (drawing/Tolerance.h)
  // measured (not persisted)
  double value = 0;              ///< model mm or degrees
  bool valid = false;
  bool orphaned = false;         ///< a reference no longer resolves (shown in red with "?")

  QJsonObject toJson() const;
  static Dimension fromJson(const QJsonObject& o);
};

/// Picked geometry for auto-typing (Fusion "D" behaviour).
struct DimPick {
  bool isVertex = false;
  Vertex2D vertex;
  Curve2D curve;
};
/// Decide the dimension type from one or two picks (circle -> Diameter, arc -> Radius, line ->
/// Linear, two lines -> Angle (non-parallel) or Linear (parallel), two points -> Linear).
/// Returns false when the combination makes no sense.
bool inferDimension(const std::vector<DimPick>& picks, Dimension& out);

/// Measured points of a dimension (view-local): endpoints for linear/aligned, centre+radius
/// for radial, apex for angles. Returns false when a ref cannot be resolved.
struct DimGeometry {
  Point2 p1, p2;         ///< linear: the two measured points; radial: centre and point on arc
  Point2 apex;           ///< angle: intersection of the two lines
  Point2 d1, d2;         ///< angle: unit directions from the apex
  double radius = 0;
  double value = 0;      ///< mm or degrees
};
bool measureDimension(const Dimension& dim, const DrawingView& view, DimGeometry& out);
/// Update dim.value/dim.valid from the view.
bool updateDimension(Dimension& dim, const DrawingView& view);

/// Rendered layout of a dimension in sheet coordinates.
struct DimLayout {
  std::vector<Curve2D> lines;                    ///< dimension + extension lines (LineClass::Dimension)
  std::vector<std::array<Point2, 3>> arrows;     ///< filled arrow heads
  Point2 textPos;                                ///< text centre
  double textAngle = 0;                          ///< degrees, CCW, Qt convention handled by renderer
  std::string text;
  bool ok = false;
};
DimLayout layoutDimension(const Dimension& dim, const DrawingView& view, const DrawingStyle& style,
                          const core::UnitSystem& units);

/// Dimension text: value in display units with precision; Ø / R prefix; ° suffix.
std::string formatDimensionText(const Dimension& dim, const core::UnitSystem& units);
/// Arrow head triangle at `tip` pointing along `dir` (unit, pointing *into* the tip).
std::array<Point2, 3> arrowHead(const Point2& tip, const Point2& dir, const DrawingStyle& style);

}  // namespace fabricad::drawing
