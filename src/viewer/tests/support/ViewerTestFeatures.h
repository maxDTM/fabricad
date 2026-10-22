// Test-only features for viewer tests: a fake sketch (IProfileSource) and a construction plane.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <map>

namespace fabricad::viewer::test {

/// Two line curves (ids 1 and 2) on XY; optional "display:construction" for curve 2.
class FakeSketch : public core::Feature, public core::IProfileSource {
 public:
  FakeSketch() : Feature("FakeSketch") {
    addBool("styled", false);
    addBool("closed", false);  // one closed profile (a 10 x 5 rectangle) for the profile tests
  }
  std::string browserFolder() const override { return "Sketches"; }
  const core::IProfileSource* profileSource() const override { return this; }
  void recompute(core::RecomputeContext&) override {
    edges_.clear();
    edges_[1] = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    edges_[2] = BRepBuilderAPI_MakeEdge(gp_Pnt(10, 0, 0), gp_Pnt(10, 5, 0)).Edge();
    setOutputShape(core::makeCompound({edges_[1], edges_[2]}));
    namedShapes().clear();
    if (flag("styled")) {
      namedShapes()["display:free"] = edges_[1];
      namedShapes()["display:construction"] = edges_[2];
    }
  }
  gp_Ax3 sketchPlane() const override { return gp_Ax3(); }
  std::vector<core::ProfileInfo> profiles() const override {
    if (!flag("closed")) return {};
    core::ProfileInfo p;
    p.ref = core::ShapeRef::profile(id(), core::Vec3{5, 2.5, 0}, {1, 2});
    p.area = 50.0;
    p.curveIds = {1, 2};
    return {p};
  }
  TopoDS_Face profileFace(const core::ShapeRef&) const override {
    if (!flag("closed")) return {};
    return TopoDS::Face(core::planeFace(gp_Ax3(gp_Pnt(5, 2.5, 0), gp_Dir(0, 0, 1)), 2.5));
  }
  TopoDS_Shape curveEdge(int id) const override {
    auto it = edges_.find(id);
    return it == edges_.end() ? TopoDS_Shape() : TopoDS_Shape(it->second);
  }
  TopoDS_Shape allEdges(bool) const override { return outputShape(); }
  std::optional<gp_Pnt> sketchPoint(const core::ShapeRef& r) const override {
    auto it = edges_.find(r.index);
    if (it == edges_.end()) return std::nullopt;
    BRepAdaptor_Curve c(it->second);
    if (r.subIndex == static_cast<int>(core::SketchPointRole::Start)) return c.Value(c.FirstParameter());
    if (r.subIndex == static_cast<int>(core::SketchPointRole::End)) return c.Value(c.LastParameter());
    return std::nullopt;
  }

 private:
  std::map<int, TopoDS_Edge> edges_;
};

/// Construction plane offset along Z.
class FakePlane : public core::Feature {
 public:
  FakePlane() : Feature("FakePlane") { addLength("offset", 5.0); }
  std::string browserFolder() const override { return "Construction"; }
  std::optional<gp_Ax3> asPlane() const override { return plane_; }
  void recompute(core::RecomputeContext& ctx) override {
    plane_ = gp_Ax3(gp_Pnt(0, 0, ctx.length("offset")), gp_Dir(0, 0, 1));
    setOutputShape(core::planeFace(plane_, 20.0));
  }

 private:
  gp_Ax3 plane_;
};

}  // namespace fabricad::viewer::test
