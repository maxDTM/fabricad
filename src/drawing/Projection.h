// FabriCAD drawing: view frames and hidden-line-removal projection (OCCT HLR).
//
// projectShapes() is self-contained (no Document, no Qt widgets, no AIS) so the model can
// run it on a QtConcurrent worker; it only reads the TopoDS_Shape handles it is given.
// Recipe follows FreeCAD TechDraw's GeometryObject::projectShape (see
// docs/research/freecad-occt.md section 4), re-implemented here.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"

#include <QJsonObject>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>

#include <string>
#include <vector>

namespace fabricad::drawing {

/// Stable identity of one edge of a source shape (see Curve2D::sourceName).
struct SourceEdgeId {
  std::string name;               ///< history name in the body, "" if unnamed
  core::GeomSignature signature;  ///< body-local signature (placement independent)
};

/// World-space source shape of a view (already placed with the component transform).
struct SourceShape {
  core::ObjectId bodyId = core::kInvalidId;
  TopoDS_Shape shape;
  /// edges[i-1] identifies edge i (TopExp::MapShapes order of `shape`); may be empty.
  std::vector<SourceEdgeId> edges;
};

// --- view frames ----------------------------------------------------------------------
// A frame is a gp_Ax2: Direction() points from the scene towards the viewer (projector Z),
// XDirection() is the drawing's X (right), YDirection() is the drawing's Y (up).

/// Frame of a standard orientation (world Z up, Front looks along +Y from -Y).
gp_Ax2 frameForOrientation(ViewOrientation o);
/// Frame of a view projected from `parent` towards `dir` under the given standard.
gp_Ax2 projectedFrame(const gp_Ax2& parent, ProjectionDirection dir, ProjectionStandard standard);
/// Frame of a section view cut along the line p1->p2 (parent view-local coordinates) on the
/// parent view, looking in the direction `arrow` (parent view-local unit vector, perpendicular
/// to the line). The material in front of the plane (observer side) is removed.
gp_Ax2 sectionFrame(const gp_Ax2& parent, const Point2& p1, const Point2& p2, const Point2& arrow);
/// World point for a parent view-local 2D point (on the plane through the frame origin).
gp_Pnt viewToWorld(const gp_Ax2& frame, const Point2& p);
Point2 worldToView(const gp_Ax2& frame, const gp_Pnt& p);
QJsonObject frameToJson(const gp_Ax2& f);
gp_Ax2 frameFromJson(const QJsonObject& o, const gp_Ax2& fallback);

// --- HLR projection -------------------------------------------------------------------

struct ProjectionInput {
  std::vector<SourceShape> sources;
  gp_Ax2 frame;                 ///< orientation; the origin is replaced by the sources' bbox centre
  bool useFrameOrigin = false;  ///< keep frame.Location() as origin (section/detail views)
  bool hiddenLines = false;
  bool tangentEdges = false;
  bool exact = true;            ///< HLRBRep_Algo; false = HLRBRep_PolyAlgo preview (meshed)
  double deflection = 0.0;      ///< polyline tolerance for non-line/arc curves; 0 = auto
  /// Extra curves appended unchanged (e.g. hatch lines from a section cut).
  std::vector<Curve2D> extraCurves;
};

struct ProjectionOutput {
  bool ok = false;
  std::string error;
  std::vector<Curve2D> curves;
  BBox2 bbox;             ///< bbox of the curves (view-local)
  core::Vec3 origin;      ///< world point projected to (0,0)
  gp_Ax2 frame;           ///< the frame actually used (with origin)
  int visibleCount = 0, hiddenCount = 0, tangentCount = 0;
  double millis = 0;      ///< wall time of the HLR
  /// Hidden curves (whole, with their 3D identity) that were dropped because they lie under a
  /// visible curve. Not drawn; dimensions made on them before still resolve (DimRef).
  std::vector<Curve2D> covered;
};

/// Run HLR on the sources and convert the result to 2D curves. Thread-safe with respect to
/// everything except the input shapes (which it only reads; the poly algorithm meshes a copy).
ProjectionOutput projectShapes(const ProjectionInput& in);
/// Drop the parts of hidden curves that lie on a visible curve (same line / same circle, within
/// `tol`): HLR reports a back edge seen edge-on behind a front edge as hidden, which plots twice
/// and makes outlines look dashed (DRW-ASM-OUTPUT-4). Returns the number of curves changed.
int removeHiddenUnderVisible(std::vector<Curve2D>& curves, double tol, std::vector<Curve2D>* removed = nullptr);

/// Convert one HLR output edge (lying in the projection plane, Z = 0) to a curve.
/// Returns false for degenerate ("crazy") edges.
bool edgeToCurve(const TopoDS_Shape& edge, LineClass cls, double deflection, Curve2D& out);

/// Collect vertices (curve endpoints, arc centres) with duplicates merged within tol.
std::vector<Vertex2D> collectVertices(const std::vector<Curve2D>& curves, double tol = 1e-3);

/// Bounding box centre of all source shapes (world). False if empty.
bool sourcesCenter(const std::vector<SourceShape>& sources, gp_Pnt& center, double* diagonal = nullptr);

}  // namespace fabricad::drawing
