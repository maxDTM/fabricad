// FabriCAD drawing: a view on a sheet (base, projected, section, detail) and its cached
// 2D geometry. Definition fields are persisted; the geometry cache is rebuilt by the model.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"

#include <QJsonObject>
#include <gp_Ax2.hxx>

#include <string>
#include <vector>

namespace fabricad::drawing {

/// Cut line of a section view, in the parent view's local coordinates.
struct SectionLine {
  Point2 p1, p2;
  Point2 arrow{0, 1};       ///< viewing direction of the section (unit, perpendicular to p1->p2)
  std::string label;        ///< "A" -> view label "A-A"; empty = auto-assign
  QJsonObject toJson() const;
  static SectionLine fromJson(const QJsonObject& o);
};

/// Circle of a detail view, in the parent view's local coordinates.
struct DetailSpec {
  Point2 center;
  double radius = 10.0;
  std::string label;        ///< empty = auto-assign
  QJsonObject toJson() const;
  static DetailSpec fromJson(const QJsonObject& o);
};

struct DrawingView {
  // --- definition (persisted) -----------------------------------------------------
  core::ObjectId id = core::kInvalidId;
  std::string label;                  ///< "VIEW1", "A-A", "DETAIL B"
  ViewType type = ViewType::Base;
  core::ObjectId parent = core::kInvalidId;  ///< parent view for projected/section/detail
  std::vector<core::ShapeRef> sources;       ///< Body/Component refs; empty = all visible bodies
  ViewOrientation orientation = ViewOrientation::Front;
  gp_Ax2 customFrame{gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0)};  ///< used unless Base+standard orientation
  ProjectionDirection direction = ProjectionDirection::Right;  ///< projected views
  ViewAlignment alignment = ViewAlignment::None;
  double scale = 1.0;
  Point2 position;                    ///< sheet coordinates (mm) of the view origin
  bool hiddenLines = false;
  TangentEdgeMode tangentEdges = TangentEdgeMode::Off;
  bool showLabel = false;
  bool hatch = true;                  ///< section views
  bool exact = true;                  ///< exact HLR (false: poly preview)
  SectionLine section;                ///< for Section views
  DetailSpec detail;                  ///< for Detail views

  // --- automatic placement (not persisted) -------------------------------------------
  /// When set, the model moves the view once its geometry is known: at least `gap` away from
  /// the parent along autoDir (sheet space), clear of other views, inside the frame.
  bool autoPlace = false;
  Point2 autoDir;

  // --- cached geometry (not persisted) ----------------------------------------------
  std::vector<Curve2D> curves;        ///< view-local (model mm, Y up)
  std::vector<Vertex2D> vertices;
  BBox2 bbox;                         ///< view-local bbox of curves
  core::Vec3 origin3d;                ///< world point at view-local (0,0)
  gp_Ax2 frame;                       ///< frame used for the last projection
  bool geometryValid = false;
  bool geometryPending = false;
  std::string error;
  int generation = 0;

  /// Frame for projecting (Base views with a standard orientation: from the orientation).
  gp_Ax2 definitionFrame() const;
  bool usesStandardOrientation() const { return type == ViewType::Base && orientation != ViewOrientation::Custom; }

  Point2 toSheet(const Point2& viewLocal) const { return {position.x + viewLocal.x * scale, position.y - viewLocal.y * scale}; }
  Point2 fromSheet(const Point2& sheet) const { return {(sheet.x - position.x) / scale, -(sheet.y - position.y) / scale}; }
  /// Sheet-space bounding box of the geometry (a small default box when empty).
  BBox2 sheetBBox() const;
  /// Label position (below the view).
  Point2 labelPosition() const;

  /// Nearest curve within `tol` (view-local units); nullptr if none.
  const Curve2D* pickCurve(const Point2& viewLocal, double tol, size_t* index = nullptr) const;
  const Vertex2D* pickVertex(const Point2& viewLocal, double tol) const;
  /// Find the curve originating from a 3D edge (first match).
  const Curve2D* curveForSource(core::ObjectId body, int edge) const;
  /// Find the curve originating from a history-named 3D edge, nearest to `near` when several
  /// HLR pieces share the edge.
  const Curve2D* curveForName(core::ObjectId body, const std::string& name, const Point2& near) const;

  /// JSON of the definition fields only.
  QJsonObject toJson() const;
  static DrawingView fromJson(const QJsonObject& o);
  /// Definition key: fields that influence the projected geometry (not position/label).
  QJsonObject geometryKey() const;
};

}  // namespace fabricad::drawing
