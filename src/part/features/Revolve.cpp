// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Revolve.h"

#include "core/Feature.h"
#include "part/PartUtils.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

RevolveFeature::RevolveFeature() : PartFeature(kType) {
  addRefs("profiles", core::ShapeKind::Profile | core::ShapeKind::Face | core::ShapeKind::Sketch).setLabel("Profiles");
  addRef("axis", core::kAxisKinds).setLabel("Axis");
  addEnum("extentType", {"Full", "Angle"}, Full).setLabel("Type");
  addAngle("angle", 90.0).setLabel("Angle");
  addEnum("direction", {"One Side", "Two Sides", "Symmetric"}, OneSide).setLabel("Direction");
  addAngle("angle2", 90.0).setLabel("Angle (side 2)");
  addBool("flip", false).setLabel("Flip direction");
  addOperationProperties(*this, Operation::Join);
  updatePropertyFlags();
}

void RevolveFeature::updatePropertyFlags() {
  bool full = enumIndex("extentType") == Full;
  prop("angle").flags().hidden = full;
  prop("direction").flags().hidden = full;
  prop("angle2").flags().hidden = full || enumIndex("direction") != TwoSides;
  prop("flip").flags().hidden = full;
}

void RevolveFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::string err;
  std::vector<TopoDS_Face> faces = profileFaces(ctx, prop("profiles").refs(), &err);
  if (faces.empty()) {
    ctx.setError(err.empty() ? "select a profile to revolve" : err);
    return;
  }
  const core::ShapeRef& axisRef = ref("axis");
  if (!axisRef.isValid()) {
    ctx.setError("select an axis");
    return;
  }
  if (axisRef.kind == core::ShapeKind::Edge || axisRef.kind == core::ShapeKind::Face) {
    resolveRefProperty(ctx, prop("axis"));  // naming layer: writes the index back
  }
  std::optional<gp_Ax1> axis = ctx.axis(ref("axis"));
  if (!axis) {
    ctx.setError("the selected axis reference is not a line");
    return;
  }
  gp_Ax1 ax = *axis;
  if (flag("flip")) ax.Reverse();

  const bool full = enumIndex("extentType") == Full;
  double a1 = full ? 360.0 : ctx.angle("angle");
  double a2 = ctx.angle("angle2");
  int direction = full ? OneSide : enumIndex("direction");
  if (!full && std::abs(a1) < 1e-9) {
    ctx.setError("angle must not be zero");
    return;
  }

  std::vector<TopoDS_Shape> solids;
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int pi = 0;
  for (const TopoDS_Face& face : faces) {
    ++pi;
    std::string prefix = faces.size() > 1 ? std::format("{}/P{}/", base, pi) : base + "/";
    TopoDS_Shape solid;
    gp_Trsf toSketch;  // inverse of the rotation applied to the face before revolving
    if (direction == OneSide) {
      gp_Ax1 a = ax;
      double ang = a1;
      if (ang < 0) {
        a.Reverse();
        ang = -ang;
      }
      solid = revolveFace(face, a, std::min(ang, 360.0), prefix, names, &err);
    } else if (direction == Symmetric) {
      double half = std::abs(a1) * 0.5;
      // rotate the face back by half, then revolve the full angle
      gp_Trsf t;
      t.SetRotation(ax, -half * M_PI / 180.0);
      toSketch = t.Inverted();
      TopoDS_Face moved = TopoDS::Face(BRepBuilderAPI_Transform(face, t, Standard_True).Shape());
      solid = revolveFace(moved, ax, std::min(2 * half, 360.0), prefix, names, &err);
    } else {
      double total = std::abs(a1) + std::abs(a2);
      if (total >= 360.0 - 1e-9) {
        solid = revolveFace(face, ax, 360.0, prefix, names, &err);
      } else {
        gp_Trsf t;
        t.SetRotation(ax, -std::abs(a2) * M_PI / 180.0);
        toSketch = t.Inverted();
        TopoDS_Face moved = TopoDS::Face(BRepBuilderAPI_Transform(face, t, Standard_True).Shape());
        solid = revolveFace(moved, ax, total, prefix, names, &err);
      }
    }
    if (solid.IsNull()) break;
    if (refs("profiles").size() == faces.size())
      stabilizeSideNames(ctx, refs("profiles")[static_cast<size_t>(pi - 1)], prefix, names, toSketch);
    solids.push_back(solid);
  }
  if (solids.size() != faces.size()) {
    ctx.setError(err.empty() ? "revolve failed" : err);
    return;
  }
  TopoDS_Shape tool;
  if (solids.size() == 1) {
    tool = solids.front();
  } else {
    BoolResult r = fuseAll(solids);
    if (!r) {
      ctx.setError("could not combine the profiles: " + r.error);
      return;
    }
    tool = r.shape;
    names = mapNames(names, History::of(r.history), tool);
  }
  applyTool(ctx, tool, operation(), prop("targets").refs(), names, out);
}

}  // namespace fabricad::part
