// FabriCAD part: Scale (uniform / non-uniform about a point), Thicken (faces -> solid), Rib
// (thin wall from an open sketch curve to the next body or by depth) and Split Face.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

namespace fabricad::part {

/// Scale bodies about a point (default: the origin). Uniform or per-axis (non-uniform).
class ScaleBodyFeature : public PartFeature {
 public:
  static constexpr const char* kType = "ScaleBody";
  enum ScaleType { Uniform = 0, NonUniform = 1 };
  ScaleBodyFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "bodies"; }
  std::string primaryNumeric() const override { return "factor"; }
};

/// Thicken faces into a solid: one side (thickness sign picks the side; positive = along the
/// face normal, i.e. outward for faces of a solid) or symmetric. Operation as Extrude
/// (defaults to New Body like Fusion).
class ThickenFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Thicken";
  enum Direction { OneSide = 0, Symmetric = 1 };
  ThickenFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void applyDefaultsForNewFeature(const core::Document&) override {}
  std::string primaryInput() const override { return "faces"; }
  std::string primaryNumeric() const override { return "thickness"; }
};

/// Rib: an open sketch curve is given a thickness normal to its sketch plane and extended in
/// the sketch plane, perpendicular to the curve, until it meets a body (To Next) or by a depth.
/// The rib joins the bodies it touches.
class RibFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Rib";
  enum Thickness { Symmetric = 0, OneSide = 1, OtherSide = 2 };
  enum DepthType { ToNext = 0, Depth = 1 };
  RibFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "curves"; }
  std::string primaryNumeric() const override { return "thickness"; }
  Operation operation() const override { return Operation::Join; }
};

/// Split Face: split the selected faces with a plane, face (extended) or body; the body stays
/// one solid with more faces (for draft, offset face, appearance...).
class SplitFaceFeature : public PartFeature {
 public:
  static constexpr const char* kType = "SplitFace";
  SplitFaceFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "faces"; }
};

}  // namespace fabricad::part
