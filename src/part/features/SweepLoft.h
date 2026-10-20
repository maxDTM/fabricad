// FabriCAD part: Sweep (profile along a path) and Loft (through sections).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

namespace fabricad::part {

class SweepFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Sweep";
  enum Orientation { Perpendicular = 0, Parallel = 1 };
  SweepFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "profiles"; }
};

class LoftFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Loft";
  LoftFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "profiles"; }
};

}  // namespace fabricad::part
