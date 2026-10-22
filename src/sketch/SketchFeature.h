// FabriCAD sketch: the "Sketch" timeline feature. Owns a 2D Sketch on a plane, solves it on
// recompute, exposes closed regions as profiles (core::IProfileSource) for the part module.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Feature.h"
#include "sketch/Sketch.h"
#include "sketch/SketchProfiles.h"

#include <gp_Ax3.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::sketch {

/// What the viewer needs to draw a sketch with Fusion colours (used by the edit session and
/// available to the ui module for persistent display).
struct SketchDisplayItem {
  int geoId = -1;
  std::vector<core::Vec3> points;  ///< polyline (world); a single point for Point geometry
  GeometryStatus status = GeometryStatus::Free;
  bool construction = false;
  bool isPoint = false;
};

class SketchFeature : public core::Feature, public core::IProfileSource {
 public:
  static constexpr const char* kType = "Sketch";
  SketchFeature();

  std::string browserFolder() const override { return "Sketches"; }
  std::string iconName() const override { return "sketch"; }

  // --- model -----------------------------------------------------------------
  Sketch& sketch() { return sketch_; }
  const Sketch& sketch() const { return sketch_; }
  /// Plane after the last recompute (or set explicitly for a fresh sketch before recompute).
  const gp_Ax3& plane() const { return plane_; }
  void setPlane(const gp_Ax3& p) { plane_ = p; }
  /// Sketch on a face: remember the plane's axes so later recomputes (which may resolve the
  /// face with another orientation or parametrisation) keep the same normal side and X axis
  /// (SK-04). Saved as "planeAxes".
  void storePlaneAxes(const gp_Ax3& p);
  /// `p` with its normal sign and X direction aligned to the stored axes (stores them if none).
  gp_Ax3 alignToStoredAxes(const gp_Ax3& p);
  bool hasStoredPlaneAxes() const { return planeAxes_.has_value(); }
  bool isVisible() const { return flag("visible"); }
  void setVisible(bool v) { prop("visible").setBool(v); }

  gp_Pnt toWorld(Pt2 p) const { return sketch::toWorld(plane_, p); }
  core::Vec3 toWorldVec(Pt2 p) const { return core::Vec3::from(toWorld(p)); }
  Pt2 toLocal(const gp_Pnt& p) const { return sketch::toLocal(plane_, p); }
  Pt2 toLocal(const core::Vec3& p) const { return toLocal(p.toPnt()); }

  // --- recompute ---------------------------------------------------------------
  void recompute(core::RecomputeContext& ctx) override;
  std::vector<core::ObjectId> extraDependencies() const override;
  /// Re-solve and rebuild the output shape without a document context (used while editing
  /// in sketch mode; expressions are evaluated with `lookup`, which may be empty).
  SolveResult updateGeometry(const Sketch::ExternalEval& eval = {});

  // --- IProfileSource ----------------------------------------------------------
  const core::IProfileSource* profileSource() const override { return this; }
  gp_Ax3 sketchPlane() const override { return plane_; }
  std::optional<gp_Ax3> asPlane() const override { return plane_; }
  std::vector<core::ProfileInfo> profiles() const override;
  TopoDS_Face profileFace(const core::ShapeRef& profileRef) const override;
  TopoDS_Shape curveEdge(int curveId) const override;
  TopoDS_Shape allEdges(bool includeConstruction) const override;
  std::optional<gp_Pnt> sketchPoint(const core::ShapeRef& pointRef) const override;

  /// Regions (cached; computed lazily after a recompute).
  const std::vector<ProfileRegion>& regions() const;
  /// Region containing the sketch-plane point (for picking profiles), nullptr if none.
  const ProfileRegion* regionAt(Pt2 p) const;
  /// Profile ref for a region.
  core::ShapeRef profileRef(const ProfileRegion& r) const;
  void invalidateProfiles() { profilesValid_ = false; }

  // --- display -----------------------------------------------------------------
  std::vector<SketchDisplayItem> displayItems() const;
  /// Status-bar text: "Fully constrained" / "3 DOF" / "Over-constrained".
  std::string statusText() const;

  // --- serialisation -----------------------------------------------------------
  QJsonObject toJson() const override;
  void fromJson(const QJsonObject& o) override;

 private:
  void rebuildOutput();
  /// Fill namedShapes() "display:<style>" compounds (viewer colour convention).
  void rebuildDisplayShapes();
  void updateProjected(core::RecomputeContext& ctx);

  Sketch sketch_;
  gp_Ax3 plane_{gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)};
  struct PlaneAxes {
    gp_Dir normal;
    gp_Dir x;
    bool direct = true;  ///< right-handed (older files may hold a mirrored face frame: kept as is)
  };
  std::optional<PlaneAxes> planeAxes_;
  mutable std::vector<ProfileRegion> regions_;
  mutable bool profilesValid_ = false;
};

}  // namespace fabricad::sketch
