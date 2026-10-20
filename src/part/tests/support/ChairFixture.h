// Test fixture: the chair benchmark's LegFrame (docs/comparison/CHAIR_SPEC.md, stage A,
// features L1-L3) built feature by feature with exactly the numbers of the spec, so the
// stage-B split (YZ plane) and the stage-C/D checks are repeatable. Every dimension is a named
// constant here; change the spec and this file together. Header-only: the assembly tests
// include it as "part/tests/support/ChairFixture.h".
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/ShapeRef.h"
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/tests/support/TestSketch.h"

#include <memory>

namespace fabricad::part::test::chair {

// --- LegFrame (spec stage A, mm) ------------------------------------------------------
constexpr double kLegSize = 40;          ///< legs are 40 x 40 squares ...
constexpr double kLegCentreX = 170;      ///< ... centred at (+-170, +-160)
constexpr double kLegCentreY = 160;
constexpr double kLegHeight = 430;       ///< L1: extrude +Z 430 -> Z[0, 430]
constexpr double kRailZ0 = 130;          ///< L2/L3: rails Z[130, 170]
constexpr double kRailZ1 = 170;
constexpr double kSideRailX0 = 160;      ///< L2: side rails X[+-160, +-180] x Y[-140, 140]
constexpr double kSideRailX1 = 180;
constexpr double kSideRailY = 140;
constexpr double kCrossRailX = 150;      ///< L3: front cross rail X[-150, 150] x Y[-170, -150]
constexpr double kCrossRailY0 = -170;
constexpr double kCrossRailY1 = -150;

// --- reference values (spec) -----------------------------------------------------------
constexpr double kLegFrameVolume = 3440000;   ///< after stage A
constexpr double kHalfVolume = 1720000;       ///< each leg part after the stage-B split
constexpr double kLeftAfterChamfer = 1718632; ///< stage C: chamfer 3 on the 8 bottom edges
constexpr double kRightAfterFilletHole = 1716808.26;  ///< stage C: fillet R5 + hole D10 (810.47/leg, 1570.80)
constexpr double kChamfer = 3, kFillet = 5, kHoleDiameter = 10;
constexpr double kStageDShift = 15;           ///< stage D: legs move -+15 along X

struct LegFrame {
  core::ObjectId body = core::kInvalidId;
  core::Feature* legs = nullptr;   ///< L1
  core::Feature* rails = nullptr;  ///< L2 + L3 (one sketch, one join)
};

/// L1-L3 in the active component. The rails share one sketch on the XY plane offset to
/// z = 130 and one 40 mm join (geometry identical to the spec's per-rail construction).
inline LegFrame buildLegFrame(core::Document& doc) {
  LegFrame lf;
  // L1: four 40 x 40 legs on XY, extrude +Z 430, new body
  auto legs = std::make_unique<TestSketch>();
  legs->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
  const double h = kLegSize / 2;
  for (double sx : {-1.0, 1.0})
    for (double sy : {-1.0, 1.0})
      legs->rects.push_back({sx * kLegCentreX - h, sy * kLegCentreY - h, sx * kLegCentreX + h, sy * kLegCentreY + h});
  auto* legSketch = static_cast<TestSketch*>(doc.addFeature(std::move(legs)));
  doc.recompute();
  auto ex = std::make_unique<ExtrudeFeature>();
  for (size_t i = 0; i < legSketch->rects.size(); ++i) ex->prop("profiles").addRef(legSketch->rectProfile(i));
  ex->prop("distance").setDouble(kLegHeight);
  ex->prop("operation").setEnum("New Body");
  lf.legs = doc.addFeature(std::move(ex));
  doc.recompute();
  lf.body = lf.legs->createdBodies().front();

  // L2 + L3: rails on the plane z = 130, extrude +Z 40, join
  auto off = std::make_unique<OffsetPlaneFeature>();
  off->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
  off->prop("distance").setDouble(kRailZ0);
  core::Feature* plane = doc.addFeature(std::move(off));
  doc.recompute();
  auto rails = std::make_unique<TestSketch>();
  rails->prop("plane").setRef(core::ShapeRef::feature(plane->id(), core::ShapeKind::ConstructionPlane));
  rails->rects.push_back({-kSideRailX1, -kSideRailY, -kSideRailX0, kSideRailY});
  rails->rects.push_back({kSideRailX0, -kSideRailY, kSideRailX1, kSideRailY});
  rails->rects.push_back({-kCrossRailX, kCrossRailY0, kCrossRailX, kCrossRailY1});
  auto* railSketch = static_cast<TestSketch*>(doc.addFeature(std::move(rails)));
  doc.recompute();
  auto ex2 = std::make_unique<ExtrudeFeature>();
  for (size_t i = 0; i < railSketch->rects.size(); ++i) ex2->prop("profiles").addRef(railSketch->rectProfile(i));
  ex2->prop("distance").setDouble(kRailZ1 - kRailZ0);
  ex2->prop("operation").setEnum("Join");
  lf.rails = doc.addFeature(std::move(ex2));
  doc.recompute();
  return lf;
}

}  // namespace fabricad::part::test::chair
