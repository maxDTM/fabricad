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
#include <TColgp_Array1OfPnt2d.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt2d.hxx>

#include <QJsonArray>

#include <algorithm>

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
  addEnum("radiusType", {"Constant", "Variable"}, Constant).setLabel("Radius type");
  addLength("endRadius", 4.0).setLabel("End radius").setTooltip("Variable: radius at the end of each edge chain (Radius = start)");
  core::Property points("radiusPoints", core::PropertyType::Json);
  points.setJson(QJsonArray());
  points.setLabel("Intermediate radii");
  points.setTooltip("Variable: [[position 0..1 along the chain, radius mm], ...] between start and end");
  points.flags().hidden = true;
  addProperty(std::move(points));
  addBool("flipVariable", false).setLabel("Flip start/end").setTooltip("Variable: swap which end of the chain gets Radius");
  updatePropertyFlags();
}

void FilletFeature::updatePropertyFlags() {
  bool var = enumIndex("radiusType") == Variable;
  prop("endRadius").flags().hidden = !var;
  prop("flipVariable").flags().hidden = !var;
}

std::vector<std::pair<double, double>> FilletFeature::radiusLaw(double startRadius, double endRadius,
                                                                const QJsonArray& points, bool flip, std::string* error) {
  std::vector<std::pair<double, double>> law{{0.0, startRadius}};
  for (const auto& v : points) {
    QJsonArray a = v.toArray();
    if (a.size() != 2) continue;
    double t = a[0].toDouble(), r = a[1].toDouble();
    if (t <= 1e-6 || t >= 1 - 1e-6) continue;  // the ends come from Radius / End radius
    law.emplace_back(t, r);
  }
  law.emplace_back(1.0, endRadius);
  std::stable_sort(law.begin(), law.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (size_t i = 1; i < law.size(); ++i)
    if (law[i].first - law[i - 1].first < 1e-6) {
      if (error) *error = "two intermediate radii at the same position";
      return {};
    }
  for (const auto& [t, r] : law)
    if (r <= 1e-9) {
      if (error) *error = "all radii must be positive";
      return {};
    }
  if (flip) {
    for (auto& [t, r] : law) t = 1.0 - t;
    std::reverse(law.begin(), law.end());
  }
  return law;
}

void FilletFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  double radius = ctx.length("radius");
  if (radius <= 1e-9) {
    ctx.setError("radius must be positive");
    return;
  }
  // older documents have no radiusType: constant
  const bool variable = property("radiusType") && enumIndex("radiusType") == Variable;
  std::vector<std::pair<double, double>> law;
  if (variable) {
    std::string err;
    law = radiusLaw(radius, ctx.length("endRadius"), prop("radiusPoints").json().toArray(), flag("flipVariable"), &err);
    if (law.empty()) {
      ctx.setError(err);
      return;
    }
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
      for (const auto& e : g.shapes) {
        if (!variable) {
          mk.Add(radius, TopoDS::Edge(e));
          continue;
        }
        const TopoDS_Edge& edge = TopoDS::Edge(e);
        if (mk.Contour(edge) > 0) continue;  // already part of a tangent chain
        if (law.size() == 2) {
          mk.Add(law.front().second, law.back().second, edge);  // linear along the whole chain
          continue;
        }
        mk.Add(edge);
        const int ic = mk.NbContours();
        if (mk.NbEdges(ic) != 1) {
          // intermediate points need a single-edge chain (edge parameters are per edge)
          mk.Remove(edge);
          mk.Add(law.front().second, law.back().second, edge);
          ctx.setWarning("intermediate radii apply to single-edge chains only; a linear law was used");
          continue;
        }
        TColgp_Array1OfPnt2d ur(1, static_cast<int>(law.size()));
        for (size_t k = 0; k < law.size(); ++k) ur(static_cast<int>(k) + 1) = gp_Pnt2d(law[k].first, law[k].second);
        mk.SetRadius(ur, ic, 1);
      }
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
