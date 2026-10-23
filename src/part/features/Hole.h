// FabriCAD part: Hole feature (simple / counterbore / countersink at sketch points,
// vertices, circle centres or clicks on a planar face, distance / through all, flat or angled
// drill point; tap type simple / clearance (ISO 273) / tapped (ISO metric thread, cosmetic or
// modelled)). The proto-hole profile
// follows FreeCAD PartDesign FeatureHole.cpp (LGPL-2.1+): a half cross-section revolved
// about the hole axis, then one copy per placement point and a single cut.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/ExtraUtils.h"
#include "part/PartFeature.h"
#include "part/features/Thread.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>

namespace fabricad::part {

class HoleFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Hole";
  enum HoleType { Simple = 0, Counterbore = 1, Countersink = 2 };
  enum Extent { Distance = 0, All = 1 };
  enum DrillPoint { Flat = 0, Angled = 1 };
  enum TapType { SimpleTap = 0, Clearance = 1, Tapped = 2 };

  HoleFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  std::string primaryInput() const override { return "points"; }
  std::vector<std::string> preselectionInputs(const core::ShapeRef& ref) const override {
    if (ref.kind != core::ShapeKind::Face) return {};
    if (ref.hasPoint) return {"face", "points"};
    return {"face"};
  }
  Operation operation() const override { return Operation::Cut; }

  /// Proto hole along -Z starting at the origin (top of the hole at z = 0). Exposed for tests.
  struct Params {
    int type = Simple;
    double diameter = 6.6;
    double depth = 10;
    int drillPoint = Flat;
    double drillAngle = 118;
    double cboreDiameter = 11;
    double cboreDepth = 6;
    double csinkDiameter = 12;
    double csinkAngle = 90;
    /// Extra height above z = 0 (same top diameter, cone continued for countersinks) so a hole
    /// starting flush with a face has no coplanar top face in the boolean. Geometry below
    /// z = 0 is unchanged.
    double topExtension = 0;
  };
  /// The tool solid of one hole (axis -Z through the origin, placement face at z = 0). With a
  /// top extension, a counterbore or countersink keeps only the drill radius above z = 0
  /// (DRW-VIEWS-ANNOT-13: no notch in material above the placement face). `namedFaces` (optional)
  /// receives the faces in naming order (F1, F2, ...): the faces of the plain revolved profile
  /// first, in their old order, then the faces the trim adds, so saved Hole<n>/H<k>/F<i> names
  /// keep their meaning.
  static TopoDS_Shape makeProtoHole(const Params& p, std::string* error, std::vector<TopoDS_Shape>* namedFaces = nullptr);

  /// ISO 273 clearance hole diameter (fit 0 close / 1 normal / 2 loose).
  static double clearanceDiameter(const IsoThread& t, int fit);
  /// Threads of tapped holes from the last recompute (cosmetic or modelled).
  const std::vector<ThreadInfo>& threads() const { return holeThreads_; }
  /// Several clicks on the same face are separate holes.
  bool refsDistinguishedByPoint(const std::string& propName) const override { return propName == "points"; }

 private:
  void applyThreads(core::RecomputeContext& ctx, const IsoThread& iso, const std::vector<gp_Pnt>& centres,
                    const gp_Dir& axisDir, double radius, Outcome& out);
  std::vector<ThreadInfo> holeThreads_;
};

}  // namespace fabricad::part
