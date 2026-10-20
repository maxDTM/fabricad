// FabriCAD part: Rectangular pattern, Circular pattern and Mirror of features or bodies.
// Feature patterns reuse the source feature's tool solid (PartFeature::toolShape()) and
// its operation; body patterns create new bodies (Mirror of bodies may also join).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <gp_Trsf.hxx>

#include <vector>

namespace fabricad::part {

/// Shared machinery: resolve objects, apply a list of transforms, write the outcome.
class TransformFeature : public PartFeature {
 public:
  enum PatternType { Features = 0, Bodies = 1 };
  using PartFeature::PartFeature;

 protected:
  void addObjectProperties();
  /// Applies `transforms` (identity excluded) to every object. `joinBodies`: fuse body
  /// copies into the source body when they touch (Mirror), else create new bodies.
  void applyTransforms(core::RecomputeContext& ctx, const std::vector<gp_Trsf>& transforms, Outcome& out, bool joinBodies);
  /// Instance indices listed in the "suppressed" Json property.
  std::vector<int> suppressedInstances() const;
};

class RectangularPatternFeature : public TransformFeature {
 public:
  static constexpr const char* kType = "RectangularPattern";
  enum Distribution { Extent = 0, Spacing = 1 };
  RectangularPatternFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "objects"; }
};

class CircularPatternFeature : public TransformFeature {
 public:
  static constexpr const char* kType = "CircularPattern";
  enum Type { Full = 0, Angle = 1, Symmetric = 2 };
  CircularPatternFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "objects"; }
};

class MirrorFeature : public TransformFeature {
 public:
  static constexpr const char* kType = "Mirror";
  MirrorFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "objects"; }
};

}  // namespace fabricad::part
