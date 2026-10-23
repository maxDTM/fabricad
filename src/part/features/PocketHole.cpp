// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/PocketHole.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"
#include "part/standards/PocketJigs.h"
#include "part/standards/Threads.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

namespace {
enum Jig { Kreg15 = 0, Castle6 = 1, CustomJig = 2 };

TopoDS_Shape axisCylinder(const gp_Pnt& from, const gp_Dir& dir, double length, double dia) {
  return BRepPrimAPI_MakeCylinder(gp_Ax2(from, dir), dia / 2.0, length).Shape();
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

PocketHoleFeature::PocketHoleFeature() : PartFeature(kType) {
  addRef("face", core::mask(core::ShapeKind::Face)).setLabel("Entry face").setTooltip("Planar face the screw enters (back of the board)");
  addRefs("edges", core::mask(core::ShapeKind::Edge))
      .setLabel("Exit edges")
      .setTooltip("Straight edges of the entry face; the screw leaves through the face on the other side of each edge");
  std::vector<std::string> jigs;
  for (const auto& j : standards::pocketJigs()) jigs.push_back(j.name);
  addEnum("jig", jigs, Kreg15).setLabel("Jig");
  addAngle("angle", 15.0).setLabel("Angle").setTooltip("Screw axis to the entry face");
  addBool("measureThickness", true).setLabel("Measure thickness").setTooltip("Board thickness from the face opposite the entry face");
  addLength("thickness", 19.05).setLabel("Thickness");
  addBool("autoScrew", true).setLabel("Screw from chart").setTooltip("Pocket screw length from the jig chart for the thickness");
  addLength("screwLength", 31.75).setLabel("Screw length");
  addLength("stepDiameter", 9.5).setLabel("Step diameter");
  addLength("pilotDiameter", 3.6).setLabel("Pilot diameter");
  addBool("autoStepDepth", true).setLabel("Step depth from collar").setTooltip("Half the screw in each board");
  addLength("stepDepth", 20.0).setLabel("Step depth");
  addLength("exitOffset", 0.0).setLabel("Exit point offset").setTooltip("From the mid-thickness towards the entry face");
  addInt("count", 2).setLabel("Holes per edge").flags().intMin = 1;
  addLength("edgeDistance", 25.0).setLabel("Edge distance").setTooltip("First and last hole from the ends of the edge");
  addLength("spacing", 0.0).setLabel("Spacing").setTooltip("0 = holes spread evenly between the edge distances");
  addBool("drillMating", false).setLabel("Also drill the mating part").setTooltip("Pilot continues into the selected touching body");
  addRef("matingBody", core::mask(core::ShapeKind::Body)).setLabel("Mating body");
  prop("count").flags().intMax = 100;
  updatePropertyFlags();
}

void PocketHoleFeature::updatePropertyFlags() {
  const int jig = enumIndex("jig");
  if (jig != lastJig_ && jig >= 0 && jig < static_cast<int>(standards::pocketJigs().size())) {
    if (lastJig_ >= 0 && jig != CustomJig) {  // switching presets fills the jig values (not on load)
      const auto& j = standards::pocketJigs()[static_cast<size_t>(jig)];
      prop("angle").setDouble(j.angle);
      prop("pilotDiameter").setDouble(j.pilot);
      prop("stepDiameter").setDouble(j.step);
    }
    lastJig_ = jig;
  }
  prop("angle").flags().readOnly = jig != CustomJig;
  prop("thickness").flags().hidden = flag("measureThickness");
  prop("screwLength").flags().hidden = flag("autoScrew");
  prop("stepDepth").flags().hidden = flag("autoStepDepth");
  prop("matingBody").flags().hidden = !flag("drillMating");
}

void PocketHoleFeature::fromJson(const QJsonObject& o) {
  PartFeature::fromJson(o);
  lastJig_ = enumIndex("jig");
  updatePropertyFlags();
}

bool PocketHoleFeature::applyScriptOption(const std::string& nameIn, const std::string& value) {
  const std::string name = lower(nameIn);
  auto num = [&](double& out) {
    try {
      out = std::stod(value);
      return true;
    } catch (...) {
      return false;
    }
  };
  double v = 0;
  if (name == "jig") {
    const auto* j = standards::findPocketJig(value);
    if (!j) return false;
    prop("jig").setEnum(static_cast<int>(j - standards::pocketJigs().data()));
    updatePropertyFlags();
    return true;
  }
  if (name == "thickness" && value != "measured") {
    if (!num(v) || v <= 0) return false;
    prop("measureThickness").setBool(false);
    prop("thickness").setDouble(v);
    return true;
  }
  if (name == "thickness") {
    prop("measureThickness").setBool(true);
    return true;
  }
  if (name == "screw") {
    if (value == "auto") {
      prop("autoScrew").setBool(true);
      return true;
    }
    if (!num(v) || v <= 0) return false;
    prop("autoScrew").setBool(false);
    prop("screwLength").setDouble(v);
    return true;
  }
  if (name == "count") {
    if (!num(v) || v < 1 || v > 100) return false;
    prop("count").setInt(static_cast<std::int64_t>(v));
    return true;
  }
  if (name == "mating") {
    prop("drillMating").setBool(value == "yes" || value == "true" || value == "1");
    return true;
  }
  return false;
}

std::string PocketHoleFeature::callout() const {
  const auto& jigs = standards::pocketJigs();
  const int j = std::clamp(enumIndex("jig"), 0, static_cast<int>(jigs.size()) - 1);
  const double screw = placed_.empty() ? prop("screwLength").toDouble() : placed_.front().screwLength;
  std::string brand;
  if (j != CustomJig) {
    const std::string jn = jigs[static_cast<size_t>(j)].name;
    brand = " " + jn.substr(0, jn.find(' '));
  }
  return std::format("POCKET HOLE {:g}° ⌀{:g}/{:g}{}, {}\" SCREW", prop("angle").toDouble(), prop("stepDiameter").toDouble(),
                     prop("pilotDiameter").toDouble(), brand, standards::inchText(std::round(screw / 25.4 * 64) / 64));
}

void PocketHoleFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  placed_.clear();
  if (!ref("face").isValid()) {
    ctx.setError("select the entry face (back of the board)");
    return;
  }
  const core::ShapeRef faceRef = ref("face");
  TopoDS_Shape face = resolveRefProperty(ctx, prop("face"));
  const TopoDS_Shape& body = ctx.bodyShape(faceRef.owner);
  // PRODUCTION-3: the resolved face can come without the orientation it has in the body (a
  // board's underside then "points" into the material and no opposite face is found); take the
  // body's own copy, whose orientation gives the outward normal
  if (!face.IsNull())
    for (const TopoDS_Shape& f : facesOf(body))
      if (f.IsSame(face)) {
        face = f;
        break;
      }
  auto pl = face.IsNull() ? std::nullopt : core::facePlane(face);
  auto nrm = face.IsNull() ? std::nullopt : faceNormal(face);
  if (!pl || !nrm) {
    ctx.setError("the entry face must be planar");
    return;
  }
  const gp_Dir n = *nrm;  // outward normal of the entry face
  const gp_Pnt origin = pl->Location();

  // thickness: nearest parallel face with the opposite normal
  double thickness = ctx.length("thickness");
  if (flag("measureThickness")) {
    double best = 0;
    for (const TopoDS_Shape& f : facesOf(body)) {
      auto fn = faceNormal(f);
      auto fp = core::facePlane(f);
      if (!fn || !fp || !fn->IsOpposite(n, 1e-6)) continue;
      const double d = gp_Vec(fp->Location(), origin).Dot(gp_Vec(n));
      if (d > 1e-6 && (best == 0 || d < best)) best = d;
    }
    if (best <= 0) {
      ctx.setError("no face opposite the entry face: type the thickness");
      return;
    }
    thickness = best;
  }
  const double angle = ctx.angle("angle");
  if (angle <= 1 || angle >= 89) {
    ctx.setError("pocket angle must be between 1 and 89 degrees");
    return;
  }
  const double screw = flag("autoScrew") ? standards::pocketScrewLength(thickness) : ctx.length("screwLength");
  const double exitOffset = ctx.length("exitOffset");
  if (std::abs(exitOffset) >= thickness / 2) {
    ctx.setError("exit point offset must stay inside the board");
    return;
  }
  standards::PocketGeometry geo = standards::pocketGeometry(thickness, angle, screw, exitOffset);
  if (!flag("autoStepDepth")) geo.stepDepth = ctx.length("stepDepth");
  const double step = ctx.length("stepDiameter"), pilot = ctx.length("pilotDiameter");
  if (pilot <= 0 || step <= pilot) {
    ctx.setError("the step diameter must exceed the pilot diameter");
    return;
  }
  if (geo.stepDepth <= 0 || geo.stepDepth >= geo.axisToExit) {
    ctx.setError("step depth must lie between the entry and the exit point");
    return;
  }
  const bool mating = flag("drillMating") && ref("matingBody").isValid();
  const int count = std::max<int>(1, static_cast<int>(prop("count").toInt()));
  const double edgeDist = ctx.length("edgeDistance"), spacing = ctx.length("spacing");

  GProp_GProps fprops;
  BRepGProp::SurfaceProperties(face, fprops);
  const gp_Pnt faceCentre = fprops.CentreOfMass();

  std::vector<TopoDS_Shape> tools;
  for (core::ShapeRef& er : prop("edges").refs()) {
    if (er.owner != faceRef.owner) {
      ctx.setWarning("exit edges must be edges of the entry face's body (an edge of the touching part was picked)");
      continue;
    }
    TopoDS_Shape e = resolveRef(ctx, er);
    if (e.IsNull() || e.ShapeType() != TopAbs_EDGE) continue;
    BRepAdaptor_Curve c(TopoDS::Edge(e));
    if (c.GetType() != GeomAbs_Line) {
      ctx.setWarning("pocket hole exit edges must be straight");
      continue;
    }
    const gp_Pnt a = c.Value(c.FirstParameter()), b = c.Value(c.LastParameter());
    const double len = a.Distance(b);
    if (len < 1e-6) continue;
    const gp_Dir t(gp_Vec(a, b));
    if (std::abs(gp_Vec(t).Dot(gp_Vec(n))) > 1e-6 || std::abs(gp_Vec(origin, a).Dot(gp_Vec(n))) > 1e-4) {
      ctx.setWarning("exit edge does not lie on the entry face");
      continue;
    }
    gp_Dir u = n.Crossed(t);  // in the entry plane, perpendicular to the edge
    const gp_Pnt mid = a.Translated(gp_Vec(a, b) * 0.5);
    if (gp_Vec(mid, faceCentre).Dot(gp_Vec(u)) < 0) u.Reverse();  // u points into the face
    // positions along the edge
    std::vector<double> at;
    if (count == 1) at.push_back(len / 2);
    else if (spacing > 1e-9)
      for (int i = 0; i < count; ++i) at.push_back(edgeDist + i * spacing);
    else
      for (int i = 0; i < count; ++i) at.push_back(edgeDist + (len - 2 * edgeDist) * i / (count - 1));
    const double ar = angle * M_PI / 180.0;
    const gp_Dir axis(gp_Vec(u).Reversed() * std::cos(ar) - gp_Vec(n) * std::sin(ar));  // entry -> exit
    for (double s : at) {
      if (s < -1e-9 || s > len + 1e-9) {
        ctx.setWarning(std::format("pocket hole at {:g} mm is off the edge", s));
        continue;
      }
      const gp_Pnt onEdge = a.Translated(gp_Vec(t) * s);
      const gp_Pnt exitP = onEdge.Translated(gp_Vec(n) * -(thickness / 2.0 - exitOffset));
      const gp_Pnt entry = exitP.Translated(gp_Vec(axis) * -geo.axisToExit);
      // the step starts well above the entry face so it leaves a clean elongated pocket
      const double ext = step / 2.0 / std::tan(ar) + step;
      TopoDS_Shape stepCyl = axisCylinder(entry.Translated(gp_Vec(axis) * -ext), axis, ext + geo.stepDepth, step);
      const double pilotStart = geo.stepDepth * 0.5;
      const double pilotEnd = geo.axisToExit + (mating ? screw / 2.0 + 2.0 : pilot);
      TopoDS_Shape pilotCyl = axisCylinder(entry.Translated(gp_Vec(axis) * pilotStart), axis, pilotEnd - pilotStart, pilot);
      BRepAlgoAPI_Fuse fu(stepCyl, pilotCyl);
      fu.Build();
      if (!fu.IsDone()) {
        ctx.setError("could not build the pocket hole tool");
        return;
      }
      tools.push_back(fu.Shape());
      placed_.push_back({entry, exitP, axis, thickness, geo.stepDepth, screw});
    }
  }
  if (tools.empty()) {
    ctx.setError("select exit edges on the entry face");
    return;
  }
  std::vector<core::ShapeRef> targets{core::ShapeRef::body(faceRef.owner)};
  if (mating) targets.push_back(core::ShapeRef::body(ref("matingBody").owner));
  NameMap names;
  TopoDS_Shape tool = tools.size() == 1 ? tools.front() : compoundOf(tools);
  applyTool(ctx, tool, Operation::Cut, targets, names, out);
}

}  // namespace fabricad::part
