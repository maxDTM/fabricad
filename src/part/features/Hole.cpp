// SPDX-License-Identifier: LGPL-2.1-or-later
// Hole profile construction adapted from FreeCAD src/Mod/PartDesign/App/FeatureHole.cpp
// (LGPL-2.1-or-later, (c) 2011 Juergen Riegel and FreeCAD contributors), see THIRD_PARTY.md.
#include "part/features/Hole.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

HoleFeature::HoleFeature() : PartFeature(kType) {
  addRefs("points", core::ShapeKind::SketchPoint | core::ShapeKind::Vertex | core::ShapeKind::ConstructionPoint | core::ShapeKind::Edge)
      .setLabel("Placement points")
      .setTooltip("Sketch points, vertices or circular edges (centre) where holes start");
  addRef("face", core::mask(core::ShapeKind::Face)).setLabel("Start face").setTooltip("Optional: face defining the hole direction");
  addEnum("holeType", {"Simple", "Counterbore", "Countersink"}, Simple).setLabel("Hole type");
  addLength("diameter", 6.6).setLabel("Diameter");
  addEnum("extentType", {"Distance", "All"}, Distance).setLabel("Extent");
  addLength("depth", 10.0).setLabel("Depth");
  addEnum("drillPoint", {"Flat", "Angle"}, Flat).setLabel("Drill point");
  addAngle("drillAngle", 118.0).setLabel("Drill point angle");
  addLength("cboreDiameter", 11.0).setLabel("Counterbore diameter");
  addLength("cboreDepth", 6.0).setLabel("Counterbore depth");
  addLength("csinkDiameter", 12.0).setLabel("Countersink diameter");
  addAngle("csinkAngle", 90.0).setLabel("Countersink angle");
  addBool("flip", false).setLabel("Flip direction");
  addRefs("targets", core::mask(core::ShapeKind::Body)).setLabel("Bodies to cut").setTooltip("empty = every body the hole touches");
  updatePropertyFlags();
}

void HoleFeature::updatePropertyFlags() {
  int t = enumIndex("holeType");
  bool all = enumIndex("extentType") == All;
  prop("depth").flags().hidden = all;
  prop("drillPoint").flags().hidden = all;
  prop("drillAngle").flags().hidden = all || enumIndex("drillPoint") != Angled;
  prop("cboreDiameter").flags().hidden = t != Counterbore;
  prop("cboreDepth").flags().hidden = t != Counterbore;
  prop("csinkDiameter").flags().hidden = t != Countersink;
  prop("csinkAngle").flags().hidden = t != Countersink;
}

TopoDS_Shape HoleFeature::makeProtoHole(const Params& p, std::string* error) {
  auto fail = [&](const char* msg) {
    if (error) *error = msg;
    return TopoDS_Shape();
  };
  const double radius = p.diameter / 2.0;
  if (radius <= 1e-9) return fail("diameter must be positive");
  if (p.depth <= 1e-9) return fail("depth must be positive");
  try {
    // Half cross-section in the XZ plane: x = radius, z downwards (hole axis = -Z through origin).
    // Points are listed clockwise starting on the axis at the top.
    std::vector<gp_Pnt> pts;
    const double ext = std::max(0.0, p.topExtension);
    const gp_Pnt top(0, 0, ext);
    pts.push_back(top);
    double depth = p.depth;
    if (p.type == Counterbore) {
      double cr = p.cboreDiameter / 2.0;
      if (cr <= radius) return fail("counterbore diameter must exceed the hole diameter");
      if (p.cboreDepth <= 0 || p.cboreDepth >= depth) return fail("counterbore depth must be between 0 and the hole depth");
      pts.emplace_back(cr, 0, ext);
      pts.emplace_back(cr, 0, -p.cboreDepth);
      pts.emplace_back(radius, 0, -p.cboreDepth);
    } else if (p.type == Countersink) {
      double cr = p.csinkDiameter / 2.0;
      if (cr <= radius) return fail("countersink diameter must exceed the hole diameter");
      if (p.csinkAngle <= 0 || p.csinkAngle >= 180) return fail("countersink angle must be between 0 and 180 degrees");
      double tanHalf = std::tan(p.csinkAngle * M_PI / 360.0);
      double csDepth = (cr - radius) / tanHalf;
      if (csDepth >= depth) return fail("countersink is deeper than the hole");
      pts.emplace_back(cr + ext * tanHalf, 0, ext);  // cone continued above the surface
      pts.emplace_back(radius, 0, -csDepth);
    } else {
      pts.emplace_back(radius, 0, ext);
    }
    if (p.drillPoint == Angled) {
      if (p.drillAngle <= 0 || p.drillAngle >= 180) return fail("drill point angle must be between 0 and 180 degrees");
      double tipH = radius / std::tan(p.drillAngle * M_PI / 360.0);
      pts.emplace_back(radius, 0, -depth);
      pts.emplace_back(0, 0, -depth - tipH);
    } else {
      pts.emplace_back(radius, 0, -depth);
      pts.emplace_back(0, 0, -depth);
    }
    BRepBuilderAPI_MakeWire mw;
    for (size_t i = 0; i + 1 < pts.size(); ++i) mw.Add(BRepBuilderAPI_MakeEdge(pts[i], pts[i + 1]).Edge());
    mw.Add(BRepBuilderAPI_MakeEdge(pts.back(), pts.front()).Edge());
    if (!mw.IsDone()) return fail("hole profile is not closed");
    BRepBuilderAPI_MakeFace mf(mw.Wire(), Standard_True);
    if (!mf.IsDone()) return fail("hole profile is not planar");
    BRepPrimAPI_MakeRevol rev(mf.Face(), gp_Ax1(top, gp_Dir(0, 0, 1)), 2 * M_PI);
    rev.Build();
    if (!rev.IsDone()) return fail("could not revolve the hole profile");
    TopoDS_Shape s = rev.Shape();
    if (!core::isValidShape(s)) return fail("hole profile produced an invalid solid");
    return s;
  } catch (const Standard_Failure& e) {
    if (error) *error = e.GetMessageString() ? e.GetMessageString() : "hole construction failed";
    return {};
  }
}

void HoleFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  Params p;
  p.type = enumIndex("holeType");
  p.diameter = ctx.length("diameter");
  p.depth = ctx.length("depth");
  p.drillPoint = enumIndex("drillPoint");
  p.drillAngle = ctx.angle("drillAngle");
  p.cboreDiameter = ctx.length("cboreDiameter");
  p.cboreDepth = ctx.length("cboreDepth");
  p.csinkDiameter = ctx.length("csinkDiameter");
  p.csinkAngle = ctx.angle("csinkAngle");
  const bool all = enumIndex("extentType") == All;
  if (all) {
    p.depth = throughAllLength(ctx);
    p.drillPoint = Flat;
    // Start a little above the surface so a coplanar top face does not confuse the boolean;
    // the extension keeps the counterbore/countersink depth measured from z = 0.
    p.topExtension = 1.0;
  }

  // Direction: from the start face (into the material = -normal), else the sketch plane of
  // the first sketch point, else -Z.
  std::optional<gp_Dir> normal;
  if (ref("face").isValid()) {
    TopoDS_Shape f = resolveRefProperty(ctx, prop("face"));
    normal = faceNormal(f);
    if (!normal) {
      ctx.setError("start face must be planar");
      return;
    }
  }
  std::vector<gp_Pnt> centres;
  for (auto& r : prop("points").refs()) {
    if (!normal && r.kind == core::ShapeKind::SketchPoint) {
      if (const core::IProfileSource* ps = ctx.profileSource(r)) normal = ps->sketchPlane().Direction();
    }
    if (r.isBrep()) {
      TopoDS_Shape s = resolveRef(ctx, r);
      if (s.IsNull()) continue;
    }
    auto pt = ctx.point(r);
    if (!pt) {
      ctx.setWarning(std::format("placement point not found: {}", r.toString()));
      continue;
    }
    centres.push_back(*pt);
  }
  if (centres.empty()) {
    ctx.setError("select placement points (sketch points, vertices or circle centres)");
    return;
  }
  if (!normal) normal = gp_Dir(0, 0, 1);
  gp_Dir axisDir = normal->Reversed();  // into the material
  if (flag("flip")) axisDir.Reverse();

  std::string err;
  TopoDS_Shape proto = makeProtoHole(p, &err);
  if (proto.IsNull()) {
    ctx.setError(err);
    return;
  }
  std::vector<TopoDS_Shape> holes;
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int k = 0;
  for (const gp_Pnt& c : centres) {
    ++k;
    gp_Ax3 target(c, axisDir.Reversed(), anyPerpendicular(axisDir));
    gp_Trsf t;
    t.SetDisplacement(gp_Ax3(), target);  // moves the proto frame (Z up at origin) onto the target frame
    TopoDS_Shape h = copyTransformed(proto, t);
    holes.push_back(h);
    // name the cylindrical wall(s) of each hole
    int fi = 0;
    for (const auto& f : facesOf(h)) names[std::format("{}/H{}/F{}", base, k, ++fi)] = f;
  }
  TopoDS_Shape tool = holes.size() == 1 ? holes.front() : compoundOf(holes);
  applyTool(ctx, tool, Operation::Cut, prop("targets").refs(), names, out);
}

}  // namespace fabricad::part
