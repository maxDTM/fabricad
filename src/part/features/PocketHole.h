// FabriCAD part: Pocket Hole feature (part.pocket-hole, POCKETHOLE). An angled stepped screw
// hole for butt joints in wood and sheet goods: the screw enters the back face of a board and
// leaves through the edge face at mid-thickness. Pick the entry face, then the exit edges (the
// edges between the entry face and the edge face); holes are spaced along each edge. Each hole
// is a step bore (jig counterbore) followed by the pilot, at the jig angle to the entry face;
// with "Also drill the mating part" the pilot continues into the touching body. Jig presets
// (Kreg 15°, Castle 6°, custom) and the thickness -> screw-length chart are in
// standards/PocketJigs.h.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>

#include <vector>

namespace fabricad::part {

class PocketHoleFeature : public PartFeature {
 public:
  static constexpr const char* kType = "PocketHole";
  PocketHoleFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  void fromJson(const QJsonObject& o) override;
  bool applyScriptOption(const std::string& name, const std::string& value) override;
  std::string primaryInput() const override { return "face"; }
  std::string primaryNumeric() const override { return "count"; }
  Operation operation() const override { return Operation::Cut; }
  std::vector<std::string> preselectionInputs(const core::ShapeRef& ref) const override {
    if (ref.kind == core::ShapeKind::Face) return {"face"};
    if (ref.kind == core::ShapeKind::Edge) return {"edges"};
    return {};
  }

  /// One hole of the last recompute (component-local coordinates).
  struct Placed {
    gp_Pnt entry;      ///< axis meets the entry face
    gp_Pnt exit;       ///< axis meets the edge face (named exit point PocketHole<n>/P<k>)
    gp_Dir axis;       ///< entry -> exit
    double thickness = 0;
    double stepDepth = 0;
    double screwLength = 0;
  };
  const std::vector<Placed>& placed() const { return placed_; }
  /// "POCKET HOLE 15° ⌀9.5/3.6 Kreg, #8 x 1 1/4" screw"
  std::string callout() const;

 private:
  std::vector<Placed> placed_;
  int lastJig_ = -1;
};

}  // namespace fabricad::part
