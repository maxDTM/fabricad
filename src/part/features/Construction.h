// FabriCAD part: construction geometry features (planes, axes, points).
// All live in the browser's "Construction" folder, expose asPlane/asAxis/asPoint and set a
// display shape (plane face / axis edge / vertex) as outputShape(). Origin planes/axes are
// not features: core handles ShapeKind::Origin* refs directly.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <optional>

namespace fabricad::part {

/// Common base: stores the computed geometry and builds display shapes.
class ConstructionFeature : public PartFeature {
 public:
  using PartFeature::PartFeature;
  std::string browserFolder() const override { return "Construction"; }
  std::optional<gp_Ax3> asPlane() const override { return plane_; }
  std::optional<gp_Ax1> asAxis() const override { return axis_; }
  std::optional<gp_Pnt> asPoint() const override { return point_; }
  void applyDefaultsForNewFeature(const core::Document&) override {}

 protected:
  /// Display half-size for planes/axes: from the "size" property or the scene size.
  double displaySize(core::RecomputeContext& ctx) const;
  void setPlaneResult(core::RecomputeContext& ctx, const gp_Ax3& plane, Outcome& out);
  void setAxisResult(core::RecomputeContext& ctx, const gp_Ax1& axis, Outcome& out);
  void setPointResult(const gp_Pnt& p, Outcome& out);

  std::optional<gp_Ax3> plane_;
  std::optional<gp_Ax1> axis_;
  std::optional<gp_Pnt> point_;
};

/// Offset Plane: a plane parallel to a plane/planar face at a distance.
class OffsetPlaneFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "OffsetPlane";
  OffsetPlaneFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
};

/// Plane at Angle: a plane through a linear edge/axis, rotated by an angle from a reference
/// plane (default: the plane through the axis that is closest to a world plane).
class PlaneAtAngleFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "PlaneAtAngle";
  PlaneAtAngleFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
};

/// Midplane between two planes (parallel: halfway; otherwise the bisector).
class MidplaneFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "Midplane";
  MidplaneFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
};

/// Plane through three points.
class PlaneThreePointsFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "PlaneThreePoints";
  PlaneThreePointsFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
};

/// Tangent plane to a cylindrical/conical face, optionally rotated about the axis.
class TangentPlaneFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "TangentPlane";
  TangentPlaneFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
};

/// Construction axis: through an edge / cylinder axis / two points / perpendicular to a
/// face at a point / intersection of two planes.
class AxisFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "ConstructionAxis";
  enum Mode { ThroughEdgeOrCylinder = 0, TwoPoints = 1, PerpendicularAtPoint = 2, TwoPlanes = 3 };
  AxisFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
};

/// Construction point: at a vertex/point/circle centre, edge x plane, or three planes.
class PointFeature : public ConstructionFeature {
 public:
  static constexpr const char* kType = "ConstructionPoint";
  enum Mode { AtPoint = 0, EdgeAndPlane = 1, ThreePlanes = 2, TwoEdges = 3 };
  PointFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
};

}  // namespace fabricad::part
