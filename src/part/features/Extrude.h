// FabriCAD part: Extrude feature (Fusion "Extrude": one side / two sides / symmetric,
// distance / to object / through all, taper, join / cut / intersect / new body).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

namespace fabricad::part {

class ExtrudeFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Extrude";
  enum Direction { OneSide = 0, TwoSides = 1, Symmetric = 2 };
  enum Extent { Distance = 0, ToObject = 1, All = 2 };

  ExtrudeFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "profiles"; }
};

}  // namespace fabricad::part
