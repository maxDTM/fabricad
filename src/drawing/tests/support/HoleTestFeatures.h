// Test-only feature for the drawing callout tests: a 60 x 40 x 20 block with
//   * a counterbored blind hole at (15, 20): drill 6.6 to z = 5 (depth 15), counterbore 11 x 6;
//   * a countersunk through hole at (35, 20): drill 5, countersink 10 x 90 degrees;
//   * a drilled blind hole at (50, 20): drill 4, depth 8, 118 degree drill point.
// All holes open at the top face (z = 20), axis Z.
#pragma once

#include "core/Document.h"
#include "core/Feature.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <vector>

namespace fabricad::drawing::test {

class HoleBlockFeature : public core::Feature {
 public:
  HoleBlockFeature() : Feature("TestHoleBlock") { addLength("length", 60.0); }
  void recompute(core::RecomputeContext& ctx) override {
    const double L = ctx.length("length");
    const gp_Dir Z(0, 0, 1);
    TopoDS_Shape s = BRepPrimAPI_MakeBox(L, 40, 20).Shape();
    auto cut = [&](const TopoDS_Shape& tool) { s = BRepAlgoAPI_Cut(s, tool).Shape(); };
    // counterbore
    cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15, 20, 5), Z), 3.3, 16).Shape());
    cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15, 20, 14), Z), 5.5, 7).Shape());
    // countersink (45 degree cone from r 2.5 at z 17.5 to r 6 at z 21)
    cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(35, 20, -1), Z), 2.5, 22).Shape());
    cut(BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(35, 20, 17.5), Z), 2.5, 6.0, 3.5).Shape());
    // drilled blind hole with a 118 degree point
    const double tip = 2.0 / std::tan(59.0 * M_PI / 180.0);
    cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50, 20, 12), Z), 2.0, 9).Shape());
    cut(BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(50, 20, 12 - tip), Z), 0.0, 2.0, tip).Shape());
    ctx.createBody("Block", s);
  }
};

/// Plate (w x h x 10) with through holes of radius 2 at the given XY positions (axis Z).
class HolePlateFeature : public core::Feature {
 public:
  HolePlateFeature(double w, double h, std::vector<gp_Pnt> holes)
      : Feature("TestHolePlate"), w_(w), h_(h), holes_(std::move(holes)) {}
  void recompute(core::RecomputeContext& ctx) override {
    TopoDS_Shape s = BRepPrimAPI_MakeBox(w_, h_, 10).Shape();
    for (const auto& p : holes_)
      s = BRepAlgoAPI_Cut(s, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(p.X(), p.Y(), -1), gp_Dir(0, 0, 1)), 2.0, 12).Shape()).Shape();
    ctx.createBody("Plate", s);
  }

 private:
  double w_, h_;
  std::vector<gp_Pnt> holes_;
};

}  // namespace fabricad::drawing::test
