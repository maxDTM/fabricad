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
///
/// Pattern type Components (Rectangular / Circular pattern; Fusion "Pattern type: Components"):
/// each instance is an occurrence of the source component's definition (same parent), listed
/// in the hidden Json property "instances" ([{source, index, component}]). The occurrences are
/// created / removed when the command is committed (prepareCommit(), undoable, inside the same
/// macro as the feature command) and recompute() writes their placements, so editing the
/// distance or the axis moves them parametrically; changing the quantity needs the dialog (OK
/// re-syncs the occurrences).
class TransformFeature : public PartFeature {
 public:
  enum PatternType { Features = 0, Bodies = 1, Components = 2 };
  using PartFeature::PartFeature;

  /// One instance a Components pattern needs: the source component, the instance index and
  /// the instance's world placement (from the last compute()).
  struct ComponentInstance {
    core::ObjectId source = core::kInvalidId;
    int index = 0;
    gp_Trsf world;
  };
  const std::vector<ComponentInstance>& neededInstances() const { return needed_; }
  /// Occurrence ids currently listed in "instances" (in list order).
  std::vector<core::ObjectId> instanceComponents() const;
  bool wantsCommitPreparation() const override;
  void prepareCommit(core::Document& doc, core::UndoStack* undo) override;

 protected:
  void addObjectProperties();
  /// Applies `transforms` (identity excluded) to every object. `joinBodies`: fuse body
  /// copies into the source body when they touch (Mirror), else create new bodies.
  void applyTransforms(core::RecomputeContext& ctx, const std::vector<gp_Trsf>& transforms, Outcome& out, bool joinBodies);
  /// Instance indices listed in the "suppressed" Json property.
  std::vector<int> suppressedInstances() const;
  /// Instance index of each transform passed to applyTransforms (same order; filled by compute()).
  std::vector<int> transformIndices_;

 private:
  void applyComponentTransforms(core::RecomputeContext& ctx, const std::vector<gp_Trsf>& transforms, Outcome& out);
  std::vector<ComponentInstance> needed_;
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
