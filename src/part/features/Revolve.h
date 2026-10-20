// FabriCAD part: Revolve feature (profiles about an axis: full / angle; one side, two
// sides or symmetric; join / cut / intersect / new body).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

namespace fabricad::part {

class RevolveFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Revolve";
  enum Extent { Full = 0, Angle = 1 };
  enum Direction { OneSide = 0, TwoSides = 1, Symmetric = 2 };

  RevolveFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "profiles"; }
};

}  // namespace fabricad::part
