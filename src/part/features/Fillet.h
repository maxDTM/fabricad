// FabriCAD part: Fillet and Chamfer (edge dress-up features).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

namespace fabricad::part {

/// Constant-radius fillet on edges (selecting a face adds all its edges). Edges may belong
/// to several bodies; each body is filleted separately.
class FilletFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Fillet";
  FilletFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "edges"; }
};

/// Chamfer: equal distance / two distances / distance and angle.
class ChamferFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Chamfer";
  enum Type { EqualDistance = 0, TwoDistances = 1, DistanceAngle = 2 };
  ChamferFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "edges"; }
};

}  // namespace fabricad::part
