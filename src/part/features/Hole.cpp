// SPDX-License-Identifier: LGPL-2.1-or-later
// Hole profile construction adapted from FreeCAD src/Mod/PartDesign/App/FeatureHole.cpp
// (LGPL-2.1-or-later, (c) 2011 Juergen Riegel and FreeCAD contributors), see THIRD_PARTY.md.
#include "part/features/Hole.h"

#include "core/Document.h"
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
#include <gp_Lin.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

HoleFeature::HoleFeature() : PartFeature(kType) {
  addRefs("points", core::ShapeKind::SketchPoint | core::ShapeKind::Vertex | core::ShapeKind::ConstructionPoint |
                        core::ShapeKind::Edge | core::ShapeKind::Face)
      .setLabel("Placement points")
      .setTooltip("Sketch points, vertices, circular edges (centre) or clicks on a planar face where holes start");
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
  addEnum("tapType", {"Simple", "Clearance", "Tapped"}, Simple)
      .setLabel("Hole tap type")
      .setTooltip("Clearance / Tapped take the diameter from the ISO metric size");
  addEnum("threadSize", isoMetricDesignations(), 0).setLabel("Size").setEnum("M6x1");
  addEnum("clearanceFit", {"Close", "Normal", "Loose"}, 1).setLabel("Fit").setTooltip("ISO 273 clearance hole series");
  addBool("threadModeled", false).setLabel("Modeled").setTooltip("Cut the real thread (slow); off = cosmetic thread");
  addBool("threadFullDepth", true).setLabel("Full tap depth");
  addLength("threadDepth", 8.0).setLabel("Thread depth");
  addRefs("targets", core::mask(core::ShapeKind::Body))
      .setLabel("Bodies to cut")
      .setTooltip("empty = the body the hole is placed on (else every body of the component the hole touches)");
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
  const int tap = enumIndex("tapType");
  prop("diameter").flags().hidden = tap != SimpleTap;
  prop("threadSize").flags().hidden = tap == SimpleTap;
  prop("clearanceFit").flags().hidden = tap != Clearance;
  prop("threadModeled").flags().hidden = tap != Tapped;
  prop("threadFullDepth").flags().hidden = tap != Tapped;
  prop("threadDepth").flags().hidden = tap != Tapped || flag("threadFullDepth");
}

double HoleFeature::clearanceDiameter(const IsoThread& t, int fit) {
  // ISO 273 fine / medium / coarse series for the common sizes, else 1.07 / 1.1 / 1.2 x d
  struct Row {
    double d, fine, medium, coarse;
  };
  static const Row kRows[] = {
      {1.6, 1.7, 1.8, 2.0},  {2, 2.2, 2.4, 2.6},   {2.5, 2.7, 2.9, 3.1},  {3, 3.2, 3.4, 3.6},    {3.5, 3.7, 3.9, 4.2},
      {4, 4.3, 4.5, 4.8},    {5, 5.3, 5.5, 5.8},   {6, 6.4, 6.6, 7},      {8, 8.4, 9, 10},       {10, 10.5, 11, 12},
      {12, 13, 13.5, 14.5},  {14, 15, 15.5, 16.5}, {16, 17, 17.5, 18.5},  {18, 19, 20, 21},      {20, 21, 22, 24},
      {22, 23, 24, 26},      {24, 25, 26, 28},     {27, 28, 30, 32},      {30, 31, 33, 35},      {33, 34, 36, 38},
      {36, 37, 39, 42},      {39, 40, 42, 45},     {42, 43, 45, 48},      {45, 46, 48, 52},      {48, 50, 52, 56},
      {52, 54, 56, 62},      {56, 58, 62, 66},     {60, 62, 66, 70},      {64, 66, 70, 74},
  };
  for (const Row& r : kRows)
    if (std::abs(r.d - t.nominal) < 1e-9) return fit == 0 ? r.fine : fit == 2 ? r.coarse : r.medium;
  return t.nominal * (fit == 0 ? 1.07 : fit == 2 ? 1.2 : 1.1);
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
  holeThreads_.clear();
  Params p;
  p.type = enumIndex("holeType");
  p.diameter = ctx.length("diameter");
  const int tap = enumIndex("tapType");
  const IsoThread* iso = nullptr;
  if (tap != SimpleTap) {
    iso = findIsoThread(enumName("threadSize"));
    if (!iso) {
      ctx.setError("unknown thread size");
      return;
    }
    p.diameter = tap == Tapped ? iso->tapDrill() : clearanceDiameter(*iso, enumIndex("clearanceFit"));
  }
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
    if (r.kind == core::ShapeKind::Face) {
      // click on a planar face: the hole starts at the pick point projected onto the face
      TopoDS_Shape f = resolveRef(ctx, r);
      if (f.IsNull()) continue;
      auto pl = core::facePlane(f);
      if (!pl || !r.hasPoint) {
        ctx.setWarning("hole placement on a face needs a planar face and a pick point");
        continue;
      }
      if (!normal) normal = faceNormal(f);
      gp_Pnt pick = r.point.toPnt();
      gp_Vec n(pl->Direction());
      double d = gp_Vec(pl->Location(), pick).Dot(n);
      centres.push_back(pick.Translated(-n * d));
      continue;
    }
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
  // Fusion: with no explicit "Bodies to cut" a hole cuts the body it was placed on (start face /
  // picked face, vertex or edge), not every body on its axis (a through-all hole would otherwise
  // drill parts of other components behind the rail).
  std::vector<core::ShapeRef> defaultTargets;
  if (prop("targets").refs().empty()) {
    auto addOwner = [&](const core::ShapeRef& r) {
      if (!r.isBrep() || !ctx.document().body(r.owner)) return;
      for (const auto& t : defaultTargets)
        if (t.owner == r.owner) return;
      defaultTargets.push_back(core::ShapeRef::body(r.owner));
    };
    if (ref("face").isValid()) addOwner(ref("face"));
    for (const auto& r : prop("points").refs()) addOwner(r);
  }
  std::vector<core::ShapeRef>& targets = defaultTargets.empty() ? prop("targets").refs() : defaultTargets;
  if (!applyTool(ctx, tool, Operation::Cut, targets, names, out)) return;
  if (tap == Tapped) applyThreads(ctx, *iso, centres, axisDir, p.diameter / 2, out);
}

void HoleFeature::applyThreads(core::RecomputeContext& ctx, const IsoThread& iso, const std::vector<gp_Pnt>& centres,
                               const gp_Dir& axisDir, double radius, Outcome& out) {
  const bool modeled = flag("threadModeled"), full = flag("threadFullDepth");
  const double depth = ctx.length("threadDepth");
  for (auto& change : out.bodies) {
    if (change.shape.IsNull()) continue;
    std::vector<TopoDS_Shape> cutters;
    for (const gp_Pnt& c : centres) {
      // the hole wall: a full cylindrical face of the tap-drill radius on this hole's axis
      for (const auto& f : facesOf(change.shape)) {
        auto cyl = analyseThreadCylinder(change.shape, f);
        if (!cyl || !cyl->internal || std::abs(cyl->radius - radius) > 1e-4) continue;
        if (cyl->axis.Direction().IsParallel(axisDir, 1e-6) == Standard_False) continue;
        if (gp_Lin(cyl->axis).Distance(c) > 1e-4) continue;
        // start at the end nearest the hole's start point
        gp_Vec oc(cyl->axis.Location(), c);
        double vc = oc.Dot(gp_Vec(cyl->axis.Direction()));
        bool flip = std::abs(vc - cyl->v1) < std::abs(vc - cyl->v0);
        ThreadInfo info;
        TopoDS_Shape cutter;
        std::string err;
        if (!placeThread(*cyl, iso, full, 0.0, depth, flip, false, modeled, info, modeled ? &cutter : nullptr, &err)) {
          ctx.setWarning("thread: " + err);
          continue;
        }
        holeThreads_.push_back(info);
        if (!cutter.IsNull()) cutters.push_back(cutter);
      }
    }
    if (cutters.empty()) continue;
    BoolResult r = booleanOp(Operation::Cut, change.shape, cutters);
    if (!r) {
      ctx.setError("modelled thread failed: " + (r.error.empty() ? std::string("empty result") : r.error));
      return;
    }
    NameMap mapped = mapNames(out.names, History::of(r.history), r.shape);
    addEdgeNames(mapped, r.shape);
    out.names = mapped;
    change.shape = r.shape;
  }
  if (holeThreads_.empty()) ctx.setWarning("no hole wall found for the thread");
}

}  // namespace fabricad::part
