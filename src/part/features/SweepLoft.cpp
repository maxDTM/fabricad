// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/SweepLoft.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepAdaptor_CompCurve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_TransitionMode.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

namespace {

/// Edges for a path from refs: Edge / SketchCurve / Sketch (all its edges) / ConstructionAxis.
std::vector<TopoDS_Shape> pathEdges(core::RecomputeContext& ctx, std::vector<core::ShapeRef>& refs) {
  std::vector<TopoDS_Shape> edges;
  for (auto& r : refs) {
    TopoDS_Shape s;
    if (r.kind == core::ShapeKind::Edge) s = resolveRef(ctx, r);
    else s = ctx.resolve(r);
    if (s.IsNull()) continue;
    for (const auto& e : collectEdges(s)) edges.push_back(e);
  }
  return edges;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sweep
// ---------------------------------------------------------------------------

SweepFeature::SweepFeature() : PartFeature(kType) {
  addRefs("profiles", core::ShapeKind::Profile | core::ShapeKind::Face | core::ShapeKind::Sketch).setLabel("Profiles");
  addRefs("path", core::ShapeKind::Edge | core::ShapeKind::SketchCurve | core::ShapeKind::Sketch).setLabel("Path");
  addEnum("orientation", {"Perpendicular", "Parallel"}, Perpendicular).setLabel("Orientation");
  addOperationProperties(*this, Operation::Join);
}

void SweepFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::string err;
  std::vector<TopoDS_Face> faces = profileFaces(ctx, prop("profiles").refs(), &err);
  if (faces.empty()) {
    ctx.setError(err.empty() ? "select a profile to sweep" : err);
    return;
  }
  std::vector<TopoDS_Shape> edges = pathEdges(ctx, prop("path").refs());
  if (edges.empty()) {
    ctx.setError("select a path (edges or sketch curves)");
    return;
  }
  TopoDS_Wire spine = wireFromEdges(edges, &err);
  if (spine.IsNull()) {
    ctx.setError("path: " + err);
    return;
  }
  std::vector<TopoDS_Shape> solids;
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int pi = 0;
  for (const TopoDS_Face& face : faces) {
    ++pi;
    std::string prefix = faces.size() > 1 ? std::format("{}/P{}/", base, pi) : base + "/";
    try {
      BRepOffsetAPI_MakePipeShell mk(spine);
      if (enumIndex("orientation") == Parallel) {
        // fixed trihedron: the profile keeps its orientation while translating along the path
        BRepAdaptor_CompCurve cc(spine);
        gp_Pnt p;
        gp_Vec t;
        cc.D1(cc.FirstParameter(), p, t);
        auto n = faceNormal(face);
        gp_Dir tdir = t.Magnitude() > 1e-9 ? gp_Dir(t) : gp_Dir(0, 0, 1);
        gp_Dir ndir = n ? *n : anyPerpendicular(tdir);
        if (std::abs(gp_Vec(tdir).Dot(gp_Vec(ndir))) > 0.999) ndir = anyPerpendicular(tdir);
        mk.SetMode(gp_Ax2(p, tdir, ndir));
      } else {
        mk.SetMode(Standard_True);  // corrected Frenet
      }
      mk.SetTransitionMode(BRepBuilderAPI_RightCorner);
      mk.Add(outerWire(face), Standard_False, Standard_False);
      // inner wires (holes in the profile) are swept too
      for (TopExp_Explorer ex(face, TopAbs_WIRE); ex.More(); ex.Next()) {
        if (ex.Current().IsSame(outerWire(face))) continue;
        mk.Add(TopoDS::Wire(ex.Current()), Standard_False, Standard_False);
      }
      mk.Build();
      if (!mk.IsDone()) {
        ctx.setError("sweep failed (profile not perpendicular to the path, or self-intersecting result?)");
        return;
      }
      if (!mk.MakeSolid()) {
        ctx.setError("sweep could not be closed into a solid (open path with an open profile?)");
        return;
      }
      TopoDS_Shape s = mk.Shape();
      if (!core::isValidShape(s)) {
        ctx.setError("sweep produced an invalid shape");
        return;
      }
      TopoDS_Shape first = mk.FirstShape(), last = mk.LastShape();
      if (!first.IsNull())
        for (const auto& f : facesOf(first)) names[prefix + "Start"] = f;
      if (!last.IsNull())
        for (const auto& f : facesOf(last)) names[prefix + "End"] = f;
      int k = 0;
      std::vector<TopoDS_Shape> profileEdges = edgesOf(face);
      std::sort(profileEdges.begin(), profileEdges.end(), shapeLess);
      for (const auto& e : profileEdges) {
        ++k;
        int j = 0;
        for (const auto& g : mk.Generated(e))
          if (g.ShapeType() == TopAbs_FACE) names[std::format("{}Side/{}/{}", prefix, k, ++j)] = g;
      }
      solids.push_back(s);
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("sweep failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
      return;
    }
  }
  TopoDS_Shape tool = solids.front();
  if (solids.size() > 1) {
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

// ---------------------------------------------------------------------------
// Loft
// ---------------------------------------------------------------------------

LoftFeature::LoftFeature() : PartFeature(kType) {
  addRefs("profiles", core::ShapeKind::Profile | core::ShapeKind::Face | core::kPointKinds).setLabel("Profiles")
      .setTooltip("Sections in order; a point may start or end the loft");
  addBool("ruled", false).setLabel("Ruled").setTooltip("Straight (ruled) surfaces between sections");
  addBool("closed", false).setLabel("Closed").setTooltip("Connect the last section back to the first");
  addOperationProperties(*this, Operation::Join);
}

void LoftFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::vector<core::ShapeRef>& refs = prop("profiles").refs();
  struct Section {
    TopoDS_Wire wire;
    TopoDS_Shape vertex;
    TopoDS_Face face;
  };
  std::vector<Section> sections;
  for (auto& r : refs) {
    Section s;
    if (r.kind == core::ShapeKind::Profile || r.kind == core::ShapeKind::Face) {
      std::vector<core::ShapeRef> one{r};
      std::string err;
      std::vector<TopoDS_Face> faces = profileFaces(ctx, one, &err);
      r = one.front();  // index write-back
      if (faces.empty()) {
        ctx.setError(err.empty() ? "profile not found" : err);
        return;
      }
      s.face = faces.front();
      s.wire = outerWire(s.face);
    } else {
      if (r.isBrep()) resolveRef(ctx, r);
      auto p = ctx.point(r);
      if (!p) {
        ctx.setError("point not found");
        return;
      }
      s.vertex = BRepBuilderAPI_MakeVertex(*p).Vertex();
    }
    sections.push_back(s);
  }
  if (sections.size() < 2) {
    ctx.setError("select at least two profiles");
    return;
  }
  try {
    BRepOffsetAPI_ThruSections mk(Standard_True, flag("ruled") ? Standard_True : Standard_False, 1e-6);
    mk.CheckCompatibility(Standard_False);
    if (flag("closed")) mk.SetContinuity(GeomAbs_C1);
    for (size_t i = 0; i < sections.size(); ++i) {
      const Section& s = sections[i];
      if (!s.wire.IsNull()) mk.AddWire(s.wire);
      else if (!s.vertex.IsNull()) {
        if (i != 0 && i + 1 != sections.size()) {
          ctx.setError("a point section may only be the first or the last");
          return;
        }
        mk.AddVertex(TopoDS::Vertex(s.vertex));
      }
    }
    if (flag("closed") && !sections.front().wire.IsNull()) mk.AddWire(sections.front().wire);
    mk.Build();
    if (!mk.IsDone()) {
      ctx.setError("loft failed (sections incompatible?)");
      return;
    }
    TopoDS_Shape s = mk.Shape();
    if (!hasSolid(s)) {
      ctx.setError("loft did not produce a solid");
      return;
    }
    if (!core::isValidShape(s)) {
      ctx.setError("loft produced an invalid shape");
      return;
    }
    NameMap names;
    const std::string base = name().empty() ? std::string(kType) : name();
    TopoDS_Shape first = mk.FirstShape(), last = mk.LastShape();
    if (!first.IsNull() && first.ShapeType() == TopAbs_FACE) names[base + "/Start"] = first;
    if (!last.IsNull() && last.ShapeType() == TopAbs_FACE) names[base + "/End"] = last;
    int k = 0;
    if (!sections.front().wire.IsNull()) {
      std::vector<TopoDS_Shape> edges = edgesOf(sections.front().wire);
      std::sort(edges.begin(), edges.end(), shapeLess);
      for (const auto& e : edges) {
        ++k;
        int j = 0;
        for (const auto& g : mk.Generated(e))
          if (g.ShapeType() == TopAbs_FACE) names[std::format("{}/Side/{}/{}", base, k, ++j)] = g;
      }
    }
    applyTool(ctx, s, operation(), prop("targets").refs(), names, out);
  } catch (const Standard_Failure& e) {
    ctx.setError(std::format("loft failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
  }
}

}  // namespace fabricad::part
