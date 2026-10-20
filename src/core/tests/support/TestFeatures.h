// Test-only features used by the core unit tests (and handy as a template for modules).
#pragma once

#include "core/Document.h"
#include "core/Feature.h"
#include "core/Registry.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include <memory>

namespace fabricad::core::test {

/// Creates one body: an axis-aligned box of (dx, dy, dz) at the origin.
class BoxFeature : public Feature {
 public:
  BoxFeature() : Feature("TestBox") {
    addLength("dx", 10.0).setLabel("Width");
    addLength("dy", 20.0);
    addLength("dz", 30.0);
    addString("bodyName", "");
  }
  void recompute(RecomputeContext& ctx) override {
    double dx = ctx.length("dx"), dy = ctx.length("dy"), dz = ctx.length("dz");
    if (dx <= 0 || dy <= 0 || dz <= 0) {
      ctx.setError("box dimensions must be positive");
      return;
    }
    TopoDS_Shape box = BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
    ctx.createBody(prop("bodyName").toString(), box);
  }
};

/// Cuts a cylinder (axis Z through (cx, cy)) out of the referenced body.
class HoleFeature : public Feature {
 public:
  HoleFeature() : Feature("TestHole") {
    addRef("body", mask(ShapeKind::Body));
    addLength("cx", 5.0);
    addLength("cy", 5.0);
    addLength("radius", 2.0);
  }
  void recompute(RecomputeContext& ctx) override {
    ShapeRef bodyRef = ref("body");
    TopoDS_Shape base = ctx.resolve(bodyRef);
    if (base.IsNull()) {
      ctx.setError("no body");
      return;
    }
    gp_Ax2 ax(gp_Pnt(ctx.length("cx"), ctx.length("cy"), -1.0), gp_Dir(0, 0, 1));
    TopoDS_Shape tool = BRepPrimAPI_MakeCylinder(ax, ctx.length("radius"), 1000.0).Shape();
    BRepAlgoAPI_Cut cut(base, tool);
    if (!cut.IsDone()) {
      ctx.setError("cut failed");
      return;
    }
    ctx.setBodyShape(bodyRef.owner, cut.Shape());
  }
};

/// Always fails.
class FailingFeature : public Feature {
 public:
  FailingFeature() : Feature("TestFail") {}
  void recompute(RecomputeContext& ctx) override { ctx.setError("always fails"); }
};

inline void registerTestFeatures() {
  auto& f = FeatureFactory::instance();
  f.registerType("TestBox", [] { return std::make_unique<BoxFeature>(); });
  f.registerType("TestHole", [] { return std::make_unique<HoleFeature>(); });
  f.registerType("TestFail", [] { return std::make_unique<FailingFeature>(); });
}

}  // namespace fabricad::core::test
