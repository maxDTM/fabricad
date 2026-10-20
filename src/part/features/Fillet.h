// FabriCAD part: Fillet and Chamfer (edge dress-up features).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <utility>
#include <vector>

namespace fabricad::part {

/// Fillet on edges (selecting a face adds all its edges). Edges may belong to several bodies;
/// each body is filleted separately. Radius type Constant, or Variable: the radius runs from
/// "radius" at the start of each edge chain to "endRadius" at its end, through optional
/// intermediate "position:radius" points ("radiusPoints", e.g. "0.5:6; 0.25:3"), linearly between points.
class FilletFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Fillet";
  enum RadiusType { Constant = 0, Variable = 1 };
  FilletFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "edges"; }

  /// Sorted (position, radius) law including both ends; empty + error when invalid.
  static std::vector<std::pair<double, double>> radiusLaw(double startRadius, double endRadius,
                                                          const std::vector<std::pair<double, double>>& points, bool flip,
                                                          std::string* error);
  /// "0.5:6; 0.25:3" (also ',' and '=') -> (position, radius) pairs. False + error when malformed.
  static bool parseRadiusPoints(const std::string& text, std::vector<std::pair<double, double>>& points, std::string* error);
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
