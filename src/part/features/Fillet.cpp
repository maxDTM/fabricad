// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Fillet.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <ShapeFix_Shape.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

namespace {

constexpr core::ShapeKindMask kEdgeOrFace = core::ShapeKind::Edge | core::ShapeKind::Face;

/// Try ShapeFix on a dress-up result that fails the validity check (FreeCAD does this too).
TopoDS_Shape healed(const TopoDS_Shape& s) {
  if (core::isValidShape(s)) return s;
  try {
    ShapeFix_Shape fix(s);
    fix.Perform();
    TopoDS_Shape r = fix.Shape();
    return core::isValidShape(r) ? r : TopoDS_Shape();
  } catch (const Standard_Failure&) {
    return {};
  }
}

std::string edgeLabel(const std::string& name, size_t index) {
  return name.empty() ? std::format("edge{}", index + 1) : name;
}

}  // namespace

// ---------------------------------------------------------------------------
// Fillet
// ---------------------------------------------------------------------------

FilletFeature::FilletFeature() : PartFeature(kType) {
  addRefs("edges", kEdgeOrFace).setLabel("Edges").setTooltip("Edges to round (a face adds all of its edges)");
  addLength("radius", 2.0).setLabel("Radius");
  addBool("tangentChain", true).setLabel("Tangent chain").setTooltip("Fillets propagate along tangent-continuous edges");
}

void FilletFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  double radius = ctx.length("radius");
  if (radius <= 1e-9) {
    ctx.setError("radius must be positive");
    return;
  }
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("edges").refs(), core::ShapeKind::Edge, true);
  if (groups.empty()) {
    ctx.setError("select edges to fillet");
    return;
  }
  const std::string base = name().empty() ? std::string(kType) : name();
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    try {
      BRepFilletAPI_MakeFillet mk(g.bodyShape);
      for (const auto& e : g.shapes) mk.Add(radius, TopoDS::Edge(e));
      mk.Build();
      if (!mk.IsDone()) {
        ctx.setError("fillet failed (radius too large for the geometry?)");
        return;
      }
      TopoDS_Shape r = healed(mk.Shape());
      if (r.IsNull()) {
        ctx.setError("fillet produced an invalid shape");
        return;
      }
      NameMap extra;
      for (size_t i = 0; i < g.shapes.size(); ++i) {
        const TopTools_ListOfShape& gen = mk.Generated(g.shapes[i]);
        std::vector<TopoDS_Shape> faces = sortedShapes(gen);
        std::string label = edgeLabel(g.names[i], i);
        if (faces.size() == 1) extra[std::format("{}/{}", base, label)] = faces.front();
        else
          for (size_t k = 0; k < faces.size(); ++k) extra[std::format("{}/{}#{}", base, label, k + 1)] = faces[k];
      }
      if (!replaceBody(ctx, g.body, r, History::of(mk), extra, out)) return;
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("fillet failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Chamfer
// ---------------------------------------------------------------------------

ChamferFeature::ChamferFeature() : PartFeature(kType) {
  addRefs("edges", kEdgeOrFace).setLabel("Edges");
  addEnum("chamferType", {"Equal Distance", "Two Distances", "Distance and Angle"}, EqualDistance).setLabel("Type");
  addLength("distance", 1.0).setLabel("Distance");
  addLength("distance2", 1.0).setLabel("Distance 2");
  addAngle("angle", 45.0).setLabel("Angle");
  addBool("flip", false).setLabel("Flip").setTooltip("Swap the two sides of an asymmetric chamfer");
  updatePropertyFlags();
}

void ChamferFeature::updatePropertyFlags() {
  int t = enumIndex("chamferType");
  prop("distance2").flags().hidden = t != TwoDistances;
  prop("angle").flags().hidden = t != DistanceAngle;
  prop("flip").flags().hidden = t == EqualDistance;
}

void ChamferFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  double d1 = ctx.length("distance");
  double d2 = ctx.length("distance2");
  double angle = ctx.angle("angle");
  int type = enumIndex("chamferType");
  if (d1 <= 1e-9 || (type == TwoDistances && d2 <= 1e-9)) {
    ctx.setError("chamfer distance must be positive");
    return;
  }
  if (type == DistanceAngle && (angle <= 0 || angle >= 90)) {
    ctx.setError("chamfer angle must be between 0 and 90 degrees");
    return;
  }
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("edges").refs(), core::ShapeKind::Edge, true);
  if (groups.empty()) {
    ctx.setError("select edges to chamfer");
    return;
  }
  const bool flip = flag("flip");
  const std::string base = name().empty() ? std::string(kType) : name();
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    try {
      TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
      TopExp::MapShapesAndAncestors(g.bodyShape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
      BRepFilletAPI_MakeChamfer mk(g.bodyShape);
      for (const auto& e : g.shapes) {
        TopoDS_Edge edge = TopoDS::Edge(e);
        if (type == EqualDistance) {
          mk.Add(d1, edge);
          continue;
        }
        const TopTools_ListOfShape* faces = edgeFaces.Seek(edge);
        if (!faces || faces->Extent() < 2) {
          ctx.setError("chamfer edge has no two adjacent faces");
          return;
        }
        // reference face: deterministic choice (sorted), flipped on request
        std::vector<TopoDS_Shape> fl = sortedShapes(*faces);
        TopoDS_Face refFace = TopoDS::Face(flip ? fl[1] : fl[0]);
        if (type == TwoDistances) mk.Add(d1, d2, edge, refFace);
        else mk.AddDA(d1, angle * M_PI / 180.0, edge, refFace);
      }
      mk.Build();
      if (!mk.IsDone()) {
        ctx.setError("chamfer failed (distance too large for the geometry?)");
        return;
      }
      TopoDS_Shape r = healed(mk.Shape());
      if (r.IsNull()) {
        ctx.setError("chamfer produced an invalid shape");
        return;
      }
      NameMap extra;
      for (size_t i = 0; i < g.shapes.size(); ++i) {
        std::vector<TopoDS_Shape> faces = sortedShapes(mk.Generated(g.shapes[i]));
        std::string label = edgeLabel(g.names[i], i);
        if (faces.size() == 1) extra[std::format("{}/{}", base, label)] = faces.front();
        else
          for (size_t k = 0; k < faces.size(); ++k) extra[std::format("{}/{}#{}", base, label, k + 1)] = faces[k];
      }
      if (!replaceBody(ctx, g.body, r, History::of(mk), extra, out)) return;
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("chamfer failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
      return;
    }
  }
}

}  // namespace fabricad::part
