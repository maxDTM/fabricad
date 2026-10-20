// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/BodyOps.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/ExtraUtils.h"
#include "part/PartUtils.h"

#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_GTrsf.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

namespace {

std::string occtMessage(const Standard_Failure& e) { return e.GetMessageString() ? e.GetMessageString() : "OCCT error"; }

/// A positive-volume solid from a (possibly inside-out) closed shape.
TopoDS_Shape orientedSolid(const TopoDS_Shape& s) {
  std::vector<TopoDS_Shape> solids = solidsOf(s);
  for (auto& so : solids) {
    TopoDS_Solid solid = TopoDS::Solid(so);
    BRepLib::OrientClosedSolid(solid);
    so = solid;
  }
  return solidsToShape(solids);
}

}  // namespace

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

ScaleBodyFeature::ScaleBodyFeature() : PartFeature(kType) {
  addRefs("bodies", core::mask(core::ShapeKind::Body)).setLabel("Bodies");
  addRef("point", core::kPointKinds).setLabel("Point").setTooltip("Fixed point of the scale (empty = origin)");
  addEnum("scaleType", {"Uniform", "Non-Uniform"}, Uniform).setLabel("Scale type");
  addDouble("factor", 2.0).setLabel("Scale factor");
  addDouble("factorX", 1.0).setLabel("X scale");
  addDouble("factorY", 1.0).setLabel("Y scale");
  addDouble("factorZ", 1.0).setLabel("Z scale");
  updatePropertyFlags();
}

void ScaleBodyFeature::updatePropertyFlags() {
  bool uni = enumIndex("scaleType") == Uniform;
  prop("factor").flags().hidden = !uni;
  for (const char* n : {"factorX", "factorY", "factorZ"}) prop(n).flags().hidden = uni;
}

void ScaleBodyFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  gp_Pnt pivot(0, 0, 0);
  if (ref("point").isValid()) {
    if (ref("point").isBrep()) resolveRefProperty(ctx, prop("point"));
    auto p = ctx.point(ref("point"));
    if (!p) {
      ctx.setError("scale point not found");
      return;
    }
    pivot = *p;
  }
  const bool uni = enumIndex("scaleType") == Uniform;
  const double f = ctx.number("factor");
  const double fx = ctx.number("factorX"), fy = ctx.number("factorY"), fz = ctx.number("factorZ");
  if (uni ? f <= 1e-9 : (fx <= 1e-9 || fy <= 1e-9 || fz <= 1e-9)) {
    ctx.setError("scale factors must be positive");
    return;
  }
  int n = 0;
  for (auto& r : prop("bodies").refs()) {
    if (r.kind != core::ShapeKind::Body) continue;
    TopoDS_Shape s = ctx.resolve(r);
    if (s.IsNull()) continue;
    ++n;
    try {
      if (uni) {
        gp_Trsf t;
        t.SetScale(pivot, f);
        BRepBuilderAPI_Transform mk(s, t, Standard_True);
        if (!mk.IsDone()) {
          ctx.setError("scale failed");
          return;
        }
        if (!replaceBody(ctx, r.owner, mk.Shape(), History::of(mk), {}, out)) return;
      } else {
        gp_GTrsf g;
        g.SetValue(1, 1, fx);
        g.SetValue(2, 2, fy);
        g.SetValue(3, 3, fz);
        // keep the pivot fixed: x' = S x + (p - S p)
        g.SetTranslationPart(gp_XYZ(pivot.X() * (1 - fx), pivot.Y() * (1 - fy), pivot.Z() * (1 - fz)));
        BRepBuilderAPI_GTransform mk(s, g, Standard_True);
        if (!mk.IsDone()) {
          ctx.setError("non-uniform scale failed");
          return;
        }
        if (!replaceBody(ctx, r.owner, mk.Shape(), History::of(mk), {}, out)) return;
      }
    } catch (const Standard_Failure& e) {
      ctx.setError("scale failed: " + occtMessage(e));
      return;
    }
  }
  if (n == 0) ctx.setError("select bodies to scale");
}

// ---------------------------------------------------------------------------
// Thicken
// ---------------------------------------------------------------------------

ThickenFeature::ThickenFeature() : PartFeature(kType) {
  addRefs("faces", core::mask(core::ShapeKind::Face)).setLabel("Faces");
  addLength("thickness", 2.0).setLabel("Thickness").setTooltip("Positive = along the face normal (outward), negative = inward");
  addEnum("direction", {"One Side", "Symmetric"}, OneSide).setLabel("Direction");
  addOperationProperties(*this, Operation::NewBody);
}

void ThickenFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  const double t = ctx.length("thickness");
  if (std::abs(t) <= 1e-9) {
    ctx.setError("thickness must not be zero");
    return;
  }
  const bool sym = enumIndex("direction") == Symmetric;
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("faces").refs(), core::ShapeKind::Face);
  if (groups.empty()) {
    ctx.setError("select faces to thicken");
    return;
  }
  std::vector<TopoDS_Shape> solids;
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int k = 0;
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    try {
      TopoDS_Shape skin;
      if (g.shapes.size() == 1) {
        skin = g.shapes.front();
      } else {
        BRepBuilderAPI_Sewing sew(1e-6);
        for (const auto& f : g.shapes) sew.Add(f);
        sew.Perform();
        skin = sew.SewedShape();
      }
      if (sym) {
        BRepOffsetAPI_MakeOffsetShape off;
        off.PerformBySimple(skin, -t / 2);
        if (!off.IsDone()) {
          ctx.setError("could not offset the faces for a symmetric thicken");
          return;
        }
        skin = off.Shape();
      }
      BRepOffsetAPI_MakeThickSolid mk;
      mk.MakeThickSolidBySimple(skin, t);
      if (!mk.IsDone()) {
        ctx.setError("thicken failed (thickness too large for the curvature?)");
        return;
      }
      TopoDS_Shape solid = orientedSolid(mk.Shape());
      if (solid.IsNull() || !core::isValidShape(solid)) {
        ctx.setError("thicken produced an invalid solid");
        return;
      }
      solids.push_back(solid);
      for (const auto& f : facesOf(solid)) names[std::format("{}/F{}", base, ++k)] = f;
    } catch (const Standard_Failure& e) {
      ctx.setError("thicken failed: " + occtMessage(e));
      return;
    }
  }
  if (solids.empty()) {
    ctx.setError("select faces to thicken");
    return;
  }
  TopoDS_Shape tool = solids.size() == 1 ? solids.front() : compoundOf(solids);
  applyTool(ctx, tool, operation(), prop("targets").refs(), names, out);
}

// ---------------------------------------------------------------------------
// Rib
// ---------------------------------------------------------------------------

RibFeature::RibFeature() : PartFeature(kType) {
  addRefs("curves", core::ShapeKind::SketchCurve | core::ShapeKind::Edge)
      .setLabel("Curve")
      .setTooltip("Open sketch curves (or edges with a plane) defining the rib's top edge");
  addRef("plane", core::kPlaneKinds | core::ShapeKind::Sketch | core::ShapeKind::Face)
      .setLabel("Plane")
      .setTooltip("Only for model edges: the rib plane (sketch curves use their sketch plane)");
  addEnum("thicknessType", {"Symmetric", "One Side", "Other Side"}, Symmetric).setLabel("Thickness options");
  addLength("thickness", 2.0).setLabel("Thickness");
  addEnum("depthType", {"To Next", "Depth"}, ToNext).setLabel("Depth options");
  addLength("depth", 10.0).setLabel("Depth");
  addBool("flip", false).setLabel("Flip direction").setTooltip("Grow the rib to the other side of the curve");
  addRefs("targets", core::mask(core::ShapeKind::Body)).setLabel("Bodies to join").setTooltip("empty = every body the rib touches");
  updatePropertyFlags();
}

void RibFeature::updatePropertyFlags() { prop("depth").flags().hidden = enumIndex("depthType") != Depth; }

void RibFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  const double t = ctx.length("thickness");
  if (t <= 1e-9) {
    ctx.setError("thickness must be positive");
    return;
  }
  const bool toNext = enumIndex("depthType") == ToNext;
  const double depth = toNext ? throughAllLength(ctx) : ctx.length("depth");
  if (depth <= 1e-9) {
    ctx.setError("depth must be positive");
    return;
  }
  // curves -> edges + plane normal
  std::vector<TopoDS_Shape> edges;
  std::optional<gp_Ax3> plane;
  for (auto& r : prop("curves").refs()) {
    TopoDS_Shape e;
    if (r.kind == core::ShapeKind::SketchCurve) {
      const core::IProfileSource* ps = ctx.profileSource(r);
      if (!ps) continue;
      e = ps->curveEdge(r.index);
      if (!plane) plane = ps->sketchPlane();
    } else {
      e = resolveRef(ctx, r);
    }
    if (!e.IsNull())
      for (const auto& x : collectEdges(e)) edges.push_back(x);
  }
  if (edges.empty()) {
    ctx.setError("select an open sketch curve");
    return;
  }
  if (ref("plane").isValid()) plane = resolvePlaneInput(ctx, prop("plane"));
  if (!plane) {
    ctx.setError("select the rib plane (needed for model edges)");
    return;
  }
  const gp_Dir normal = plane->Direction();
  // in-plane direction perpendicular to the chord of the curve chain
  gp_Pnt a = core::centroid(edges.front()).toPnt(), b = a;
  {
    std::vector<gp_Pnt> ends;
    for (const auto& e : edges)
      for (TopExp_Explorer x(e, TopAbs_VERTEX); x.More(); x.Next()) ends.push_back(core::centroid(x.Current()).toPnt());
    double best = -1;
    for (const auto& p : ends)
      for (const auto& q : ends)
        if (p.Distance(q) > best) best = p.Distance(q), a = p, b = q;
    if (best < 1e-9) {
      ctx.setError("the rib curve must be open");
      return;
    }
  }
  gp_Vec chord(a, b);
  gp_Vec perp = gp_Vec(normal).Crossed(chord);
  if (perp.Magnitude() < 1e-9) {
    ctx.setError("the rib curve must lie in the rib plane");
    return;
  }
  perp.Normalize();
  // thickness offsets along the plane normal
  double lo = -t / 2, hi = t / 2;
  if (enumIndex("thicknessType") == OneSide) lo = 0, hi = t;
  if (enumIndex("thicknessType") == OtherSide) lo = -t, hi = 0;

  auto slabFor = [&](const gp_Vec& dir) -> TopoDS_Shape {
    std::vector<TopoDS_Shape> slabs;
    for (const auto& e : edges) {
      gp_Trsf shift;
      shift.SetTranslation(gp_Vec(normal) * lo);
      TopoDS_Shape e0 = BRepBuilderAPI_Transform(e, shift, Standard_True).Shape();
      TopoDS_Shape face = BRepPrimAPI_MakePrism(e0, dir * depth).Shape();
      slabs.push_back(BRepPrimAPI_MakePrism(face, gp_Vec(normal) * (hi - lo)).Shape());
    }
    if (slabs.size() == 1) return slabs.front();
    BoolResult r = fuseAll(slabs);
    return r ? r.shape : TopoDS_Shape();
  };
  const TopoDS_Shape curveShape = compoundOf(edges);
  auto touchesCurve = [&](const TopoDS_Shape& s) {
    BRepExtrema_DistShapeShape d(s, curveShape);
    return d.IsDone() && d.Value() <= std::max(1e-6, t * 1e-3);
  };

  std::vector<core::ObjectId> bodies = ctx.bodies();
  std::vector<TopoDS_Shape> bodyShapes;
  for (core::ObjectId id : bodies) bodyShapes.push_back(ctx.bodyShape(id));

  // To Next: cut the bodies out of a long slab and keep the piece(s) between curve and body.
  auto ribFor = [&](const gp_Vec& dir, std::string* why) -> TopoDS_Shape {
    TopoDS_Shape slab = slabFor(dir);
    if (slab.IsNull()) {
      *why = "could not build the rib slab";
      return {};
    }
    if (!toNext) return slab;
    if (bodyShapes.empty()) {
      *why = "there is no body for the rib to reach";
      return {};
    }
    BoolResult cut = booleanOp(Operation::Cut, slab, bodyShapes, false);
    if (!cut) {
      *why = cut.error.empty() ? "the rib lies inside the body" : cut.error;
      return {};
    }
    std::vector<TopoDS_Shape> keep;
    for (const auto& piece : solidsOf(cut.shape)) {
      if (!touchesCurve(piece)) continue;
      // a piece that runs to the far end of the slab never met a body
      core::Vec3 mn, mx;
      core::bbox(piece, mn, mx);
      double reach = 0;
      for (double x : {mn.x, mx.x})
        for (double y : {mn.y, mx.y})
          for (double z : {mn.z, mx.z}) reach = std::max(reach, gp_Vec(a, gp_Pnt(x, y, z)).Dot(dir));
      if (reach > depth * 0.9) continue;
      bool touchesBody = false;
      for (const auto& bs : bodyShapes)
        if (shapesTouch(piece, bs, 1e-5)) touchesBody = true;
      if (touchesBody) keep.push_back(piece);
    }
    if (keep.empty()) {
      *why = "the rib does not reach a body on this side of the curve";
      return {};
    }
    return keep.size() == 1 ? keep.front() : compoundOf(keep);
  };

  std::string why1, why2;
  gp_Vec first = flag("flip") ? -perp : perp;
  TopoDS_Shape rib = ribFor(first, &why1);
  if (rib.IsNull() && toNext) rib = ribFor(-first, &why2);  // To Next: the side that meets material
  if (rib.IsNull()) {
    ctx.setError(why1);
    return;
  }
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int k = 0;
  for (const auto& f : facesOf(rib)) names[std::format("{}/F{}", base, ++k)] = f;
  applyTool(ctx, rib, Operation::Join, prop("targets").refs(), names, out);
}

// ---------------------------------------------------------------------------
// Split Face
// ---------------------------------------------------------------------------

SplitFaceFeature::SplitFaceFeature() : PartFeature(kType) {
  addRefs("faces", core::mask(core::ShapeKind::Face)).setLabel("Faces to split");
  addRef("splitTool", core::kPlaneKinds | core::ShapeKind::Sketch | core::ShapeKind::Face | core::ShapeKind::Body)
      .setLabel("Splitting tool");
  addBool("extendTool", true).setLabel("Extend splitting tool");
}

void SplitFaceFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  TopoDS_Shape tool = resolveSplitTool(ctx, prop("splitTool"), flag("extendTool"), nullptr);
  if (tool.IsNull()) return;
  const core::ObjectId toolOwner = ref("splitTool").owner;
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("faces").refs(), core::ShapeKind::Face);
  if (groups.empty()) {
    ctx.setError("select faces to split");
    return;
  }
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    if (g.body == toolOwner && ref("splitTool").kind == core::ShapeKind::Body) {
      ctx.setError("a body cannot split its own faces");
      return;
    }
    try {
      BRepFeat_SplitShape sp(g.bodyShape);
      int added = 0;
      for (const auto& f : g.shapes) {
        BRepAlgoAPI_Section sec(f, tool, Standard_False);
        sec.ComputePCurveOn1(Standard_True);
        sec.Approximation(Standard_True);
        sec.Build();
        if (!sec.IsDone()) continue;
        for (TopExp_Explorer x(sec.Shape(), TopAbs_EDGE); x.More(); x.Next()) {
          sp.Add(TopoDS::Edge(x.Current()), TopoDS::Face(f));
          ++added;
        }
      }
      if (added == 0) {
        ctx.setError("the splitting tool does not cross the selected faces");
        return;
      }
      sp.Build();
      if (!sp.IsDone()) {
        ctx.setError("split face failed");
        return;
      }
      if (!replaceBody(ctx, g.body, sp.Shape(), History::of(sp), {}, out)) return;
    } catch (const Standard_Failure& e) {
      ctx.setError("split face failed: " + occtMessage(e));
      return;
    }
  }
}

}  // namespace fabricad::part
