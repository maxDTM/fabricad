// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Shell.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/ExtraUtils.h"
#include "part/PartUtils.h"

#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_Mode.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

namespace {
constexpr auto kPlaneKinds = core::kPlaneKinds | core::ShapeKind::Sketch;
constexpr double kOffsetTol = 1e-6;
}  // namespace

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

ShellFeature::ShellFeature() : PartFeature(kType) {
  addRefs("faces", core::mask(core::ShapeKind::Face)).setLabel("Faces to remove");
  addRefs("bodies", core::mask(core::ShapeKind::Body)).setLabel("Bodies").setTooltip("Hollow whole bodies (no openings)");
  addLength("thickness", 2.0).setLabel("Thickness");
  addEnum("direction", {"Inside", "Outside"}, Inside).setLabel("Direction");
  addEnum("corner", {"Sharp", "Rounded"}, 0).setLabel("Corner type");
}

void ShellFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  double t = ctx.length("thickness");
  if (t <= 1e-9) {
    ctx.setError("thickness must be positive");
    return;
  }
  const double offset = enumIndex("direction") == Inside ? -t : t;
  const GeomAbs_JoinType join = enumIndex("corner") == 1 ? GeomAbs_Arc : GeomAbs_Intersection;

  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("faces").refs(), core::ShapeKind::Face);
  // whole bodies without openings
  for (auto& bref : prop("bodies").refs()) {
    if (bref.kind != core::ShapeKind::Body) continue;
    if (ctx.resolve(bref).IsNull()) continue;
    bool have = false;
    for (const auto& g : groups)
      if (g.body == bref.owner) have = true;
    if (!have) groups.push_back({bref.owner, ctx.bodyShape(bref.owner), {}, {}});
  }
  if (groups.empty()) {
    ctx.setError("select faces to remove or bodies to hollow");
    return;
  }
  for (const ResolvedGroup& g : groups) {
    try {
      TopoDS_Shape result;
      History hist = History::none();
      if (!g.shapes.empty()) {
        TopTools_ListOfShape faces;
        for (const auto& f : g.shapes) faces.Append(f);
        BRepOffsetAPI_MakeThickSolid mk;
        mk.MakeThickSolidByJoin(g.bodyShape, faces, offset, kOffsetTol, BRepOffset_Skin, Standard_False, Standard_False, join);
        if (!mk.IsDone()) {
          ctx.setError("shell failed (thickness too large for the geometry?)");
          return;
        }
        result = mk.Shape();
        hist = History::of(mk);
        if (!replaceBody(ctx, g.body, result, hist, {}, out)) return;
      } else {
        // hollow body: offset the whole solid and subtract (inside) or fuse-difference (outside)
        BRepOffsetAPI_MakeOffsetShape mk;
        mk.PerformByJoin(g.bodyShape, offset, kOffsetTol, BRepOffset_Skin, Standard_False, Standard_False, join);
        if (!mk.IsDone() || mk.Shape().IsNull()) {
          ctx.setError("hollowing failed (thickness too large?)");
          return;
        }
        TopoDS_Shape off = mk.Shape();
        BoolResult r = offset < 0 ? booleanOp(Operation::Cut, g.bodyShape, {off}, false)
                                  : booleanOp(Operation::Cut, off, {g.bodyShape}, false);
        if (!r) {
          ctx.setError("hollowing failed: " + r.error);
          return;
        }
        if (!replaceBody(ctx, g.body, r.shape, History::of(r.history), {}, out)) return;
      }
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("shell failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Draft
// ---------------------------------------------------------------------------

DraftFeature::DraftFeature() : PartFeature(kType) {
  addRefs("faces", core::mask(core::ShapeKind::Face)).setLabel("Faces");
  addRef("plane", kPlaneKinds).setLabel("Neutral plane").setTooltip("Plane the faces pivot about (pull direction = its normal)");
  addAngle("angle", 10.0).setLabel("Angle");
  addBool("flip", false).setLabel("Flip direction");
}

void DraftFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::optional<gp_Ax3> plane;
  {
    const core::ShapeRef& r = ref("plane");
    if (r.kind == core::ShapeKind::Face) plane = core::facePlane(resolveRefProperty(ctx, prop("plane")));
    else plane = ctx.plane(r);
  }
  if (!plane) {
    ctx.setError("select a neutral plane");
    return;
  }
  double angle = ctx.angle("angle") * M_PI / 180.0;
  if (std::abs(angle) < 1e-9) {
    ctx.setError("angle must not be zero");
    return;
  }
  gp_Dir pull = plane->Direction();
  if (flag("flip")) pull.Reverse();
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("faces").refs(), core::ShapeKind::Face);
  if (groups.empty()) {
    ctx.setError("select faces to draft");
    return;
  }
  for (const ResolvedGroup& g : groups) {
    try {
      BRepOffsetAPI_DraftAngle mk(g.bodyShape);
      int added = 0;
      for (const auto& f : g.shapes) {
        mk.Add(TopoDS::Face(f), pull, angle, gp_Pln(*plane), Standard_True);
        if (!mk.AddDone()) {
          mk.Remove(TopoDS::Face(f));
          ctx.setWarning("a face could not be drafted and was skipped");
          continue;
        }
        ++added;
      }
      if (added == 0) {
        ctx.setError("none of the faces can be drafted with this plane");
        return;
      }
      mk.Build();
      if (!mk.IsDone()) {
        ctx.setError("draft failed");
        return;
      }
      if (!replaceBody(ctx, g.body, mk.Shape(), History::of(mk), {}, out)) return;
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("draft failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// OffsetFace (Press Pull)
// ---------------------------------------------------------------------------

OffsetFaceFeature::OffsetFaceFeature() : PartFeature(kType) {
  addRefs("faces", core::mask(core::ShapeKind::Face)).setLabel("Faces");
  addLength("distance", 5.0).setLabel("Distance").setTooltip("Positive = outward (add material), negative = inward");
}

void OffsetFaceFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  double d = ctx.length("distance");
  if (std::abs(d) < 1e-9) {
    ctx.setError("distance must not be zero");
    return;
  }
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("faces").refs(), core::ShapeKind::Face);
  if (groups.empty()) {
    ctx.setError("select faces to offset");
    return;
  }
  const std::string base = name().empty() ? std::string(kType) : name();
  for (const ResolvedGroup& g : groups) {
    std::vector<TopoDS_Shape> tools;
    NameMap toolNames;
    int k = 0;
    for (const auto& f : g.shapes) {
      ++k;
      auto n = faceNormal(f);
      if (!n) {
        ctx.setError("Press Pull supports planar faces (use Shell/Offset for curved faces)");
        return;
      }
      gp_Dir dir = *n;
      if (d < 0) dir.Reverse();
      std::string err;
      TopoDS_Shape prism = prismFace(TopoDS::Face(f), dir, std::abs(d), 0.0, std::format("{}/F{}/", base, k), toolNames, &err);
      if (prism.IsNull()) {
        ctx.setError(err);
        return;
      }
      tools.push_back(prism);
    }
    BoolResult r = booleanOp(d > 0 ? Operation::Join : Operation::Cut, g.bodyShape, tools);
    if (!r.error.empty()) {
      ctx.setError("offset face failed: " + r.error);
      return;
    }
    if (r.shape.IsNull()) {
      ctx.setError("offset removes the whole body");
      return;
    }
    NameMap names = upstreamNames(ctx, g.body);
    names.insert(toolNames.begin(), toolNames.end());
    NameMap mapped = mapNames(names, History::of(r.history), r.shape);
    addEdgeNames(mapped, r.shape);
    out.addModified(g.body, r.shape);
    out.names.insert(mapped.begin(), mapped.end());
    out.tool = compoundOf(tools);
    out.operation = d > 0 ? Operation::Join : Operation::Cut;
    for (const auto& t : tools) out.addPreview(t, out.operation);
  }
}

// ---------------------------------------------------------------------------
// Combine
// ---------------------------------------------------------------------------

CombineFeature::CombineFeature() : PartFeature(kType) {
  addRef("target", core::mask(core::ShapeKind::Body)).setLabel("Target body");
  addRefs("tools", core::mask(core::ShapeKind::Body)).setLabel("Tool bodies");
  addEnum("operation", {"Join", "Cut", "Intersect"}, 0).setLabel("Operation");
  addBool("keepTools", false).setLabel("Keep tools");
}

void CombineFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  TopoDS_Shape target = resolveRefProperty(ctx, prop("target"));
  if (target.IsNull()) {
    ctx.setError("select a target body");
    return;
  }
  core::ObjectId targetId = ref("target").owner;
  std::vector<TopoDS_Shape> tools;
  std::vector<core::ObjectId> toolIds;
  for (auto& r : prop("tools").refs()) {
    if (r.kind != core::ShapeKind::Body || r.owner == targetId) continue;
    TopoDS_Shape s = ctx.resolve(r);
    if (s.IsNull()) continue;
    tools.push_back(s);
    toolIds.push_back(r.owner);
  }
  if (tools.empty()) {
    ctx.setError("select at least one tool body");
    return;
  }
  Operation op = operationFromIndex(enumIndex("operation"));
  BoolResult r = booleanOp(op, target, tools);
  if (!r.error.empty()) {
    ctx.setError("combine failed: " + r.error);
    return;
  }
  if (r.shape.IsNull()) {
    ctx.setError("the combination is empty");
    return;
  }
  NameMap names = upstreamNames(ctx, targetId);
  for (core::ObjectId id : toolIds) {
    NameMap n = upstreamNames(ctx, id);
    names.insert(n.begin(), n.end());
  }
  NameMap mapped = mapNames(names, History::of(r.history), r.shape);
  addEdgeNames(mapped, r.shape);
  out.addModified(targetId, r.shape);
  out.names = mapped;
  out.operation = op;
  if (!flag("keepTools"))
    for (core::ObjectId id : toolIds) out.addModified(id, TopoDS_Shape());
  out.addPreview(r.shape, op);
}

// ---------------------------------------------------------------------------
// MoveBody
// ---------------------------------------------------------------------------

gp_Trsf MoveBodyFeature::freeMoveTransform(const gp_Pnt& pivot, double rxDeg, double ryDeg, double rzDeg,
                                           const gp_Vec& translation) {
  gp_Trsf rx, ry, rz, tr;
  rx.SetRotation(gp_Ax1(pivot, gp::DX()), rxDeg * M_PI / 180.0);
  ry.SetRotation(gp_Ax1(pivot, gp::DY()), ryDeg * M_PI / 180.0);
  rz.SetRotation(gp_Ax1(pivot, gp::DZ()), rzDeg * M_PI / 180.0);
  tr.SetTranslation(translation);
  return tr * rz * ry * rx;  // X first, then Y, then Z, then translate
}

MoveBodyFeature::MoveBodyFeature() : PartFeature(kType) {
  addRefs("bodies", core::mask(core::ShapeKind::Body)).setLabel("Bodies");
  addEnum("moveType", {"Translate", "Rotate", "Point to Point", "Free Move"}, Translate).setLabel("Move type");
  addLength("dx", 0.0).setLabel("X distance");
  addLength("dy", 0.0).setLabel("Y distance");
  addLength("dz", 0.0).setLabel("Z distance");
  addAngle("rx", 0.0).setLabel("X angle").setTooltip("Free Move: rotation about the pivot's X axis (applied first)");
  addAngle("ry", 0.0).setLabel("Y angle").setTooltip("Free Move: rotation about the pivot's Y axis (second)");
  addAngle("rz", 0.0).setLabel("Z angle").setTooltip("Free Move: rotation about the pivot's Z axis (third), then the translation");
  addEnum("pivotType", {"Selection Centre", "Origin", "Coordinates"}, 0)
      .setLabel("Pivot")
      .setTooltip("Free Move rotation centre: bounding-box centre of the selected bodies, the origin, or typed coordinates");
  addLength("px", 0.0).setLabel("Pivot X");
  addLength("py", 0.0).setLabel("Pivot Y");
  addLength("pz", 0.0).setLabel("Pivot Z");
  addRef("axis", core::kAxisKinds).setLabel("Axis");
  addAngle("angle", 0.0).setLabel("Angle");
  addRef("fromPoint", core::kPointKinds).setLabel("Origin point");
  addRef("toPoint", core::kPointKinds).setLabel("Target point");
  addBool("createCopy", false).setLabel("Create copy");
  updatePropertyFlags();
}

void MoveBodyFeature::updatePropertyFlags() {
  int t = enumIndex("moveType");
  for (const char* n : {"dx", "dy", "dz"}) prop(n).flags().hidden = t != Translate && t != Free;
  for (const char* n : {"rx", "ry", "rz", "pivotType"}) prop(n).flags().hidden = t != Free;
  for (const char* n : {"px", "py", "pz"}) prop(n).flags().hidden = t != Free || enumIndex("pivotType") != 2;
  prop("axis").flags().hidden = t != Rotate;
  prop("angle").flags().hidden = t != Rotate;
  prop("fromPoint").flags().hidden = t != PointToPoint;
  prop("toPoint").flags().hidden = t != PointToPoint;
}

void MoveBodyFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  gp_Trsf trsf;
  switch (enumIndex("moveType")) {
    case Translate: trsf.SetTranslation(gp_Vec(ctx.length("dx"), ctx.length("dy"), ctx.length("dz"))); break;
    case Rotate: {
      if (ref("axis").kind == core::ShapeKind::Edge || ref("axis").kind == core::ShapeKind::Face) resolveRefProperty(ctx, prop("axis"));
      auto ax = ctx.axis(ref("axis"));
      if (!ax) {
        ctx.setError("select a rotation axis");
        return;
      }
      trsf.SetRotation(*ax, ctx.angle("angle") * M_PI / 180.0);
      break;
    }
    case PointToPoint: {
      if (ref("fromPoint").isBrep()) resolveRefProperty(ctx, prop("fromPoint"));
      if (ref("toPoint").isBrep()) resolveRefProperty(ctx, prop("toPoint"));
      auto a = ctx.point(ref("fromPoint"));
      auto b = ctx.point(ref("toPoint"));
      if (!a || !b) {
        ctx.setError("select two points");
        return;
      }
      trsf.SetTranslation(gp_Vec(*a, *b));
      break;
    }
    case Free: {
      gp_Pnt pivot(0, 0, 0);
      int pt = enumIndex("pivotType");
      if (pt == 2) {
        pivot = gp_Pnt(ctx.length("px"), ctx.length("py"), ctx.length("pz"));
      } else if (pt == 0) {
        std::vector<TopoDS_Shape> sel;
        for (auto& r : prop("bodies").refs())
          if (r.kind == core::ShapeKind::Body && !ctx.bodyShape(r.owner).IsNull()) sel.push_back(ctx.bodyShape(r.owner));
        core::Vec3 mn, mx;
        if (!sel.empty() && core::bbox(compoundOf(sel), mn, mx))
          pivot = gp_Pnt((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2);
      }
      trsf = freeMoveTransform(pivot, ctx.angle("rx"), ctx.angle("ry"), ctx.angle("rz"),
                               gp_Vec(ctx.length("dx"), ctx.length("dy"), ctx.length("dz")));
      break;
    }
  }
  bool copy = flag("createCopy");
  int n = 0;
  for (auto& r : prop("bodies").refs()) {
    if (r.kind != core::ShapeKind::Body) continue;
    TopoDS_Shape s = ctx.resolve(r);
    if (s.IsNull()) continue;
    ++n;
    TopoDS_Shape moved = core::transformed(s, trsf);  // shares geometry, new location
    if (copy) {
      const core::Body* b = ctx.document().body(r.owner);
      out.addNew(copyTransformed(s, trsf), b ? b->name + " copy" : std::string(), b ? b->component : core::kInvalidId);
    } else {
      out.addModified(r.owner, moved);
      // the names move with the shape: locations change, so rebuild via mapNames' identity path
      NameMap names = upstreamNames(ctx, r.owner);
      NameMap mapped;
      for (const auto& [name, sub] : names) mapped[name] = core::transformed(sub, trsf);
      pruneNames(mapped, moved);
      out.names.insert(mapped.begin(), mapped.end());
    }
    out.addPreview(moved, copy ? Operation::NewBody : Operation::Join);
  }
  if (n == 0) ctx.setError("select bodies to move");
}

// ---------------------------------------------------------------------------
// SplitBody
// ---------------------------------------------------------------------------

static void sortSplitPieces(std::vector<TopoDS_Shape>& pieces, const std::optional<gp_Ax3>& plane) {
  if (!plane) {
    std::sort(pieces.begin(), pieces.end(), shapeLess);
    return;
  }
  // Planar tool: order by the signed distance of the centroid along the plane normal, so the
  // piece behind the plane keeps the original body (for the YZ plane: x < 0 first).
  const gp_Pnt o = plane->Location();
  const gp_Vec n(plane->Direction());
  auto key = [&](const TopoDS_Shape& s) { return gp_Vec(o, core::centroid(s).toPnt()).Dot(n); };
  std::stable_sort(pieces.begin(), pieces.end(), [&](const TopoDS_Shape& a, const TopoDS_Shape& b) {
    double ka = key(a), kb = key(b);
    if (std::abs(ka - kb) > 1e-6) return ka < kb;
    return shapeLess(a, b);
  });
}

SplitBodyFeature::SplitBodyFeature() : PartFeature(kType) {
  addRefs("bodies", core::mask(core::ShapeKind::Body)).setLabel("Bodies to split");
  addRef("splitTool", kPlaneKinds | core::ShapeKind::Body).setLabel("Splitting tool");
  addBool("extendTool", true).setLabel("Extend splitting tool").setTooltip("Extend a face tool to its full surface");
}

void SplitBodyFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  const core::ShapeRef& toolRef = ref("splitTool");
  // Older documents have no "extendTool": faces were always extended (planes) / used as is.
  const core::Property* ext = property("extendTool");
  std::optional<gp_Ax3> splitPlane;
  TopoDS_Shape tool = resolveSplitTool(ctx, prop("splitTool"), ext ? ext->toBool() : true, &splitPlane);
  if (tool.IsNull()) return;
  int n = 0;
  for (auto& r : prop("bodies").refs()) {
    if (r.kind != core::ShapeKind::Body || r.owner == toolRef.owner) continue;
    TopoDS_Shape s = ctx.resolve(r);
    if (s.IsNull()) continue;
    ++n;
    try {
      BRepAlgoAPI_Splitter sp;
      TopTools_ListOfShape args, tools;
      args.Append(s);
      tools.Append(tool);
      sp.SetArguments(args);
      sp.SetTools(tools);
      sp.SetFuzzyValue(kFuzzy);
      sp.Build();
      if (sp.HasErrors() || sp.Shape().IsNull()) {
        ctx.setError("split failed");
        return;
      }
      std::vector<TopoDS_Shape> pieces = solidsOf(sp.Shape());
      if (pieces.size() < 2) {
        ctx.setWarning("the tool does not split the body");
        out.addModified(r.owner, s);
        continue;
      }
      sortSplitPieces(pieces, splitPlane);
      NameMap names = upstreamNames(ctx, r.owner);
      const core::Body* b = ctx.document().body(r.owner);
      for (size_t i = 0; i < pieces.size(); ++i) {
        TopoDS_Shape piece = pieces[i];
        if (!core::isValidShape(piece)) {
          ctx.setError("split produced an invalid piece");
          return;
        }
        if (i == 0) {
          out.addModified(r.owner, piece);
          NameMap mapped = mapNames(names, History::of(sp), piece);
          addEdgeNames(mapped, piece);
          out.names.insert(mapped.begin(), mapped.end());
        } else {
          out.addNew(piece, b ? std::format("{} ({})", b->name, i + 1) : std::string(), b ? b->component : core::kInvalidId);
        }
        out.addPreview(piece, i % 2 ? Operation::NewBody : Operation::Join);
      }
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("split failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
      return;
    }
  }
  if (n == 0) ctx.setError("select bodies to split");
}

}  // namespace fabricad::part
