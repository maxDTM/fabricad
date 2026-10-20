// FabriCAD part: Shell (thickness), Draft, Offset Face (Press Pull), Combine, Move/Copy,
// Split Body: features that modify existing bodies.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

namespace fabricad::part {

/// Shell: remove faces and thicken the remaining skin (or hollow a body without openings).
class ShellFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Shell";
  enum Direction { Inside = 0, Outside = 1 };
  ShellFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "faces"; }
};

/// Draft: tilt faces by an angle about their intersection with a neutral plane.
class DraftFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Draft";
  DraftFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "faces"; }
};

/// Offset Face (Press Pull on a face): move planar faces along their normal by a distance.
class OffsetFaceFeature : public PartFeature {
 public:
  static constexpr const char* kType = "OffsetFace";
  OffsetFaceFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "faces"; }
};

/// Combine: boolean between existing bodies (target + tools).
class CombineFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Combine";
  CombineFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void applyDefaultsForNewFeature(const core::Document&) override {}
  std::string primaryInput() const override { return "target"; }
};

/// Move/Copy bodies: translate, rotate about an axis, or point to point; optional copy.
class MoveBodyFeature : public PartFeature {
 public:
  static constexpr const char* kType = "MoveBody";
  enum MoveType { Translate = 0, Rotate = 1, PointToPoint = 2 };
  MoveBodyFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "bodies"; }
};

/// Split Body with a plane / planar face / body.
class SplitBodyFeature : public PartFeature {
 public:
  static constexpr const char* kType = "SplitBody";
  SplitBodyFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string primaryInput() const override { return "bodies"; }
};

}  // namespace fabricad::part
