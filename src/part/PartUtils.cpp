// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/PartUtils.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepTools.hxx>
#include <BRepTools_History.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace fabricad::part {

// ---------------------------------------------------------------------------
// topology helpers
// ---------------------------------------------------------------------------

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& s) {
  std::vector<TopoDS_Shape> out;
  if (s.IsNull()) return out;
  if (s.ShapeType() == TopAbs_SOLID) {
    out.push_back(s);
    return out;
  }
  for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) out.push_back(ex.Current());
  return out;
}

std::vector<TopoDS_Shape> facesOf(const TopoDS_Shape& s) { return core::subShapes(s, TopAbs_FACE); }
std::vector<TopoDS_Shape> edgesOf(const TopoDS_Shape& s) { return core::subShapes(s, TopAbs_EDGE); }

bool hasSolid(const TopoDS_Shape& s) {
  if (s.IsNull()) return false;
  TopExp_Explorer ex(s, TopAbs_SOLID);
  return ex.More();
}

TopoDS_Shape solidsToShape(const std::vector<TopoDS_Shape>& solids) {
  if (solids.empty()) return {};
  if (solids.size() == 1) return solids.front();
  return compoundOf(solids);
}

TopoDS_Shape compoundOf(const std::vector<TopoDS_Shape>& shapes) { return core::makeCompound(shapes); }

std::optional<gp_Dir> faceNormal(const TopoDS_Shape& face) {
  auto pl = core::facePlane(face);
  if (!pl) return std::nullopt;
  return pl->Direction();
}

std::optional<gp_Ax3> planeOf(const TopoDS_Shape& face) { return core::facePlane(face); }

TopoDS_Wire outerWire(const TopoDS_Face& f) { return BRepTools::OuterWire(f); }

TopoDS_Wire wireFromEdges(const std::vector<TopoDS_Shape>& edges, std::string* error) {
  TopTools_ListOfShape list;
  for (const auto& e : edges)
    for (const auto& ed : collectEdges(e)) list.Append(ed);
  if (list.IsEmpty()) {
    if (error) *error = "no edges";
    return {};
  }
  try {
    BRepBuilderAPI_MakeWire mw;
    mw.Add(list);
    if (!mw.IsDone()) {
      if (error) *error = "edges do not form a connected path";
      return {};
    }
    return mw.Wire();
  } catch (const Standard_Failure& e) {
    if (error) *error = e.GetMessageString() ? e.GetMessageString() : "wire construction failed";
    return {};
  }
}

std::vector<TopoDS_Shape> collectEdges(const TopoDS_Shape& shape) {
  std::vector<TopoDS_Shape> out;
  if (shape.IsNull()) return out;
  if (shape.ShapeType() == TopAbs_EDGE) {
    out.push_back(shape);
    return out;
  }
  for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) out.push_back(ex.Current());
  return out;
}

double sceneSize(const core::RecomputeContext& ctx, const std::vector<TopoDS_Shape>& extra) {
  Bnd_Box box;
  for (const auto& [id, shape] : ctx.state())
    if (!shape.IsNull()) BRepBndLib::Add(shape, box, false);
  for (const auto& s : extra)
    if (!s.IsNull()) BRepBndLib::Add(s, box, false);
  if (box.IsVoid()) return 1.0;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  double d = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
  return std::max(1.0, d);
}

double throughAllLength(const core::RecomputeContext& ctx, const std::vector<TopoDS_Shape>& extra) {
  return 2.0 * sceneSize(ctx, extra) + 10.0;
}

TopoDS_Face planeDisplayFace(const gp_Ax3& plane, double halfSize) { return core::planeFace(plane, halfSize); }

TopoDS_Shape axisDisplayEdge(const gp_Ax1& axis, double halfLength) {
  gp_Vec v(axis.Direction());
  gp_Pnt a = axis.Location().Translated(v * -halfLength);
  gp_Pnt b = axis.Location().Translated(v * halfLength);
  return BRepBuilderAPI_MakeEdge(a, b).Edge();
}

TopoDS_Shape pointDisplayVertex(const gp_Pnt& p) { return BRepBuilderAPI_MakeVertex(p).Vertex(); }

// ---------------------------------------------------------------------------
// booleans
// ---------------------------------------------------------------------------

TopoDS_Shape unifyWithHistory(const TopoDS_Shape& s, Handle(BRepTools_History)& history) {
  if (s.IsNull()) return s;
  try {
    ShapeUpgrade_UnifySameDomain u(s, true, true, false);
    u.Build();
    TopoDS_Shape r = u.Shape();
    if (r.IsNull()) return s;
    Handle(BRepTools_History) h = u.History();
    if (!h.IsNull()) {
      if (history.IsNull()) history = h;
      else history->Merge(h);
    }
    return r;
  } catch (const Standard_Failure&) {
    return s;
  }
}

namespace {

BoolResult runBoolean(BRepAlgoAPI_BooleanOperation& op, const TopoDS_Shape& target, const std::vector<TopoDS_Shape>& tools,
                      bool unify) {
  BoolResult r;
  TopTools_ListOfShape args, tl;
  args.Append(target);
  for (const auto& t : tools)
    if (!t.IsNull()) tl.Append(t);
  if (tl.IsEmpty()) {
    r.error = "no tool shape";
    return r;
  }
  try {
    op.SetArguments(args);
    op.SetTools(tl);
    op.SetFuzzyValue(kFuzzy);
    op.SetRunParallel(Standard_True);
    op.SetNonDestructive(Standard_True);
    op.Build();
    if (op.HasErrors() || !op.IsDone()) {
      r.error = "boolean operation failed";
      return r;
    }
    TopoDS_Shape s = op.Shape();
    if (s.IsNull()) {
      r.error = "boolean produced an empty result";
      return r;
    }
    r.history = new BRepTools_History();
    r.history->Merge(op.History());
    // Keep only solids (booleans on solids may leave stray faces/edges in the compound).
    std::vector<TopoDS_Shape> solids = solidsOf(s);
    if (solids.empty()) {
      r.shape = {};
      return r;  // empty but not an error (caller decides)
    }
    s = solidsToShape(solids);
    if (unify) s = unifyWithHistory(s, r.history);
    if (!core::isValidShape(s)) {
      r.error = "boolean result is not a valid shape";
      return r;
    }
    r.shape = s;
  } catch (const Standard_Failure& e) {
    r.error = std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "boolean failed");
  }
  return r;
}

}  // namespace

BoolResult booleanOp(Operation op, const TopoDS_Shape& target, const std::vector<TopoDS_Shape>& tools, bool unify) {
  switch (op) {
    case Operation::Join: {
      BRepAlgoAPI_Fuse f;
      return runBoolean(f, target, tools, unify);
    }
    case Operation::Cut: {
      BRepAlgoAPI_Cut c;
      return runBoolean(c, target, tools, unify);
    }
    case Operation::Intersect: {
      BRepAlgoAPI_Common c;
      return runBoolean(c, target, tools, unify);
    }
    default: {
      BoolResult r;
      r.error = "no boolean for New Body";
      return r;
    }
  }
}

BoolResult fuseAll(const std::vector<TopoDS_Shape>& shapes, bool unify) {
  BoolResult r;
  std::vector<TopoDS_Shape> list;
  for (const auto& s : shapes)
    if (!s.IsNull()) list.push_back(s);
  if (list.empty()) {
    r.error = "nothing to fuse";
    return r;
  }
  if (list.size() == 1) {
    r.shape = list.front();
    r.history = new BRepTools_History();
    return r;
  }
  std::vector<TopoDS_Shape> tools(list.begin() + 1, list.end());
  return booleanOp(Operation::Join, list.front(), tools, unify);
}

bool shapesTouch(const TopoDS_Shape& a, const TopoDS_Shape& b, double tol) {
  if (a.IsNull() || b.IsNull()) return false;
  Bnd_Box ba, bb;
  BRepBndLib::Add(a, ba, false);
  BRepBndLib::Add(b, bb, false);
  ba.Enlarge(tol);
  if (ba.IsOut(bb)) return false;
  try {
    BRepExtrema_DistShapeShape d(a, b);
    if (!d.IsDone()) return true;  // be permissive
    return d.Value() <= tol;
  } catch (const Standard_Failure&) {
    return true;
  }
}

// ---------------------------------------------------------------------------
// applyTool / replaceBody
// ---------------------------------------------------------------------------

std::vector<core::ObjectId> bodiesInComponentTree(const core::Document& doc, core::ObjectId comp,
                                                  const std::vector<core::ObjectId>& candidates) {
  if (comp == core::kInvalidId || comp == core::kRootComponentId || !doc.component(comp)) return candidates;
  std::vector<core::ObjectId> tree{comp};
  for (size_t i = 0; i < tree.size(); ++i)
    if (const core::Component* c = doc.component(tree[i]))
      for (core::ObjectId child : c->children) tree.push_back(child);
  std::vector<core::ObjectId> out;
  for (core::ObjectId id : candidates) {
    const core::Body* b = doc.body(id);
    if (b && std::find(tree.begin(), tree.end(), b->component) != tree.end()) out.push_back(id);
  }
  return out;
}

std::vector<core::ObjectId> bodiesInScope(core::RecomputeContext& ctx) {
  return bodiesInComponentTree(ctx.document(), ctx.self().component(), ctx.bodies());
}

namespace {

/// Participants: explicit targets, else the bodies in the feature's component scope that the tool touches.
std::vector<core::ObjectId> participants(core::RecomputeContext& ctx, const TopoDS_Shape& tool,
                                         std::vector<core::ShapeRef>& targets) {
  std::vector<core::ObjectId> out;
  if (!targets.empty()) {
    for (auto& t : targets) {
      if (t.kind != core::ShapeKind::Body) continue;
      TopoDS_Shape s = ctx.resolve(t);
      if (!s.IsNull() && std::find(out.begin(), out.end(), t.owner) == out.end()) out.push_back(t.owner);
    }
    return out;
  }
  for (core::ObjectId id : bodiesInScope(ctx))
    if (shapesTouch(ctx.bodyShape(id), tool)) out.push_back(id);
  return out;
}

}  // namespace

bool applyTool(core::RecomputeContext& ctx, const TopoDS_Shape& tool, Operation op, std::vector<core::ShapeRef>& targets,
               const NameMap& toolNames, Outcome& out, const std::string& newBodyName) {
  if (tool.IsNull()) {
    ctx.setError("the feature produced no geometry");
    return false;
  }
  out.tool = tool;
  out.operation = op;
  out.addPreview(tool, op);

  if (op == Operation::NewBody) {
    if (!core::isValidShape(tool)) {
      ctx.setError("resulting shape is not valid");
      return false;
    }
    out.addNew(tool, newBodyName);
    out.names = toolNames;
    addEdgeNames(out.names, tool);
    return true;
  }

  std::vector<core::ObjectId> parts = participants(ctx, tool, targets);
  if (parts.empty()) {
    if (op == Operation::Join) {
      // Fusion: a join that touches nothing becomes a new body.
      if (!core::isValidShape(tool)) {
        ctx.setError("resulting shape is not valid");
        return false;
      }
      out.addNew(tool, newBodyName);
      out.names = toolNames;
      addEdgeNames(out.names, tool);
      return true;
    }
    ctx.setError(op == Operation::Cut ? "the tool does not intersect any body" : "nothing to intersect with");
    return false;
  }

  if (op == Operation::Join) {
    std::vector<TopoDS_Shape> shapes;
    NameMap names = toolNames;
    for (core::ObjectId id : parts) {
      shapes.push_back(ctx.bodyShape(id));
      NameMap n = upstreamNames(ctx, id);
      names.insert(n.begin(), n.end());
    }
    std::vector<TopoDS_Shape> tools = shapes;
    tools.push_back(tool);
    BoolResult r = booleanOp(Operation::Join, shapes.front(), std::vector<TopoDS_Shape>(tools.begin() + 1, tools.end()));
    if (!r) {
      ctx.setError(r.error.empty() ? "join produced nothing" : "join failed: " + r.error);
      return false;
    }
    out.addModified(parts.front(), r.shape);
    for (size_t i = 1; i < parts.size(); ++i) out.addModified(parts[i], TopoDS_Shape());  // merged away
    out.names = mapNames(names, History::of(r.history), r.shape);
    addEdgeNames(out.names, r.shape);
    return true;
  }

  // Cut / Intersect: each participant separately
  for (core::ObjectId id : parts) {
    const TopoDS_Shape& base = ctx.bodyShape(id);
    NameMap names = upstreamNames(ctx, id);
    names.insert(toolNames.begin(), toolNames.end());
    BoolResult r = booleanOp(op, base, {tool});
    if (!r.error.empty()) {
      ctx.setError((op == Operation::Cut ? "cut failed: " : "intersect failed: ") + r.error);
      return false;
    }
    if (r.shape.IsNull()) {
      ctx.setWarning(std::format("body {} was removed completely", id));
      out.addModified(id, TopoDS_Shape());
      continue;
    }
    out.addModified(id, r.shape);
    NameMap mapped = mapNames(names, History::of(r.history), r.shape);
    addEdgeNames(mapped, r.shape);
    out.names.insert(mapped.begin(), mapped.end());
  }
  return true;
}

bool replaceBody(core::RecomputeContext& ctx, core::ObjectId bodyId, const TopoDS_Shape& newShape, const History& history,
                 const NameMap& extraNames, Outcome& out, bool unify) {
  if (newShape.IsNull()) {
    ctx.setError("operation produced no geometry");
    return false;
  }
  TopoDS_Shape s = newShape;
  Handle(BRepTools_History) uh;
  if (unify) s = unifyWithHistory(s, uh);
  if (!core::isValidShape(s)) {
    ctx.setError("resulting shape is not valid");
    return false;
  }
  NameMap names = upstreamNames(ctx, bodyId);
  NameMap mapped = mapNames(names, history, s);
  for (const auto& [n, sh] : extraNames) mapped[n] = sh;
  if (!uh.IsNull()) mapped = mapNames(mapped, History::of(uh), s);
  pruneNames(mapped, s);
  addEdgeNames(mapped, s);
  out.addModified(bodyId, s);
  out.names.insert(mapped.begin(), mapped.end());
  out.addPreview(s, Operation::Join);
  return true;
}

std::vector<ResolvedGroup> resolveByBody(core::RecomputeContext& ctx, std::vector<core::ShapeRef>& refs,
                                         core::ShapeKind expectedKind, bool expandFaceToEdges) {
  std::vector<ResolvedGroup> groups;
  auto groupFor = [&](core::ObjectId body) -> ResolvedGroup& {
    for (auto& g : groups)
      if (g.body == body) return g;
    ResolvedGroup g;
    g.body = body;
    g.bodyShape = ctx.bodyShape(body);
    groups.push_back(g);
    return groups.back();
  };
  for (auto& ref : refs) {
    if (!ref.isBrep() || ref.kind == core::ShapeKind::Body) continue;
    TopoDS_Shape s = resolveRef(ctx, ref);
    if (s.IsNull()) continue;
    ResolvedGroup& g = groupFor(ref.owner);
    if (ref.kind == core::ShapeKind::Face && expectedKind == core::ShapeKind::Edge && expandFaceToEdges) {
      NameMap names = upstreamNames(ctx, ref.owner);
      for (const auto& e : edgesOf(s)) {
        bool dup = false;
        for (const auto& have : g.shapes)
          if (have.IsSame(e)) dup = true;
        if (dup) continue;
        g.shapes.push_back(e);
        g.names.push_back(nameOf(names, e));
      }
      continue;
    }
    if (s.ShapeType() != core::toTopAbs(expectedKind)) continue;
    bool dup = false;
    for (const auto& have : g.shapes)
      if (have.IsSame(s)) dup = true;
    if (dup) continue;
    g.shapes.push_back(s);
    g.names.push_back(ref.name);
  }
  return groups;
}

std::vector<TopoDS_Face> profileFaces(core::RecomputeContext& ctx, std::vector<core::ShapeRef>& refs, std::string* error) {
  std::vector<TopoDS_Face> faces;
  for (auto& ref : refs) {
    if (ref.kind == core::ShapeKind::Profile) {
      TopoDS_Face f = ctx.profileFace(ref);
      if (f.IsNull()) {
        if (error) *error = std::format("profile not found: {}", ref.toString());
        continue;
      }
      faces.push_back(f);
    } else if (ref.kind == core::ShapeKind::Face) {
      TopoDS_Shape s = resolveRef(ctx, ref);
      if (s.IsNull() || s.ShapeType() != TopAbs_FACE) {
        if (error) *error = std::format("face not found: {}", ref.toString());
        continue;
      }
      if (!core::facePlane(s)) {
        if (error) *error = "only planar faces can be used as profiles";
        continue;
      }
      faces.push_back(TopoDS::Face(s));
    } else if (ref.kind == core::ShapeKind::Sketch) {
      // whole sketch: every profile of it
      const core::IProfileSource* ps = ctx.profileSource(ref);
      if (!ps) {
        if (error) *error = "sketch not found";
        continue;
      }
      for (const auto& p : ps->profiles()) {
        TopoDS_Face f = ps->profileFace(p.ref);
        if (!f.IsNull()) faces.push_back(f);
      }
    }
  }
  return faces;
}

TopoDS_Face faceFromWire(const TopoDS_Wire& w) {
  if (w.IsNull()) return {};
  try {
    BRepBuilderAPI_MakeFace mf(w, Standard_True);
    if (!mf.IsDone()) return {};
    return mf.Face();
  } catch (const Standard_Failure&) {
    return {};
  }
}

TopoDS_Shape copyTransformed(const TopoDS_Shape& s, const gp_Trsf& t) {
  if (s.IsNull()) return s;
  BRepBuilderAPI_Transform tr(s, t, Standard_True);
  return tr.Shape();
}

gp_Dir anyPerpendicular(const gp_Dir& axis) {
  gp_Dir cand = std::abs(axis.Z()) < 0.9 ? gp_Dir(0, 0, 1) : gp_Dir(1, 0, 0);
  gp_Vec v = gp_Vec(axis).Crossed(gp_Vec(cand));
  return gp_Dir(v);
}

}  // namespace fabricad::part

// ---------------------------------------------------------------------------
// prisms / revolutions
// ---------------------------------------------------------------------------

#include <BRepAdaptor_Curve.hxx>
#include <BRepLib_MakeFace.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>

namespace fabricad::part {

namespace {

/// Edges of a face in deterministic order (outer wire first, then by centroid).
std::vector<TopoDS_Shape> orderedFaceEdges(const TopoDS_Face& face) {
  std::vector<TopoDS_Shape> edges = edgesOf(face);
  std::sort(edges.begin(), edges.end(), shapeLess);
  return edges;
}

void nameSweepResult(BRepPrimAPI_MakeSweep& mk, const TopoDS_Face& face, const std::string& prefix, NameMap& names,
                     bool startEnd) {
  if (startEnd) {
    TopoDS_Shape first = mk.FirstShape();
    TopoDS_Shape last = mk.LastShape();
    if (!first.IsNull() && first.ShapeType() == TopAbs_FACE) names[prefix + "Start"] = first;
    if (!last.IsNull() && last.ShapeType() == TopAbs_FACE) names[prefix + "End"] = last;
  }
  int k = 0;
  for (const auto& e : orderedFaceEdges(face)) {
    ++k;
    const TopTools_ListOfShape& gen = mk.Generated(e);
    for (const auto& g : gen)
      if (g.ShapeType() == TopAbs_FACE) names[std::format("{}Side/{}", prefix, k)] = g;
  }
}

}  // namespace

void stabilizeSideNames(const core::RecomputeContext& ctx, const core::ShapeRef& profileRef, const std::string& prefix,
                        NameMap& names, const gp_Trsf& toSketch) {
  if (profileRef.kind != core::ShapeKind::Profile) return;
  const core::IProfileSource* src = ctx.profileSource(profileRef);
  if (!src) return;
  // curve ids bounding the region ("1,2,5")
  std::vector<std::pair<int, TopoDS_Shape>> curves;
  {
    std::string tok;
    auto flush = [&] {
      if (tok.empty()) return;
      int id = std::atoi(tok.c_str());
      TopoDS_Shape e = src->curveEdge(id);
      if (!e.IsNull()) curves.emplace_back(id, e);
      tok.clear();
    };
    for (char c : profileRef.name) {
      if (c == ',') flush();
      else if (std::isdigit(static_cast<unsigned char>(c))) tok.push_back(c);
    }
    flush();
  }
  if (curves.empty()) return;
  const gp_Ax3 plane = src->sketchPlane();
  const gp_Dir n = plane.Direction();
  auto onSketch = [&](const gp_Pnt& p) {
    gp_Pnt q = p.Transformed(toSketch);
    const double h = gp_Vec(plane.Location(), q).Dot(n);
    return q.Translated(gp_Vec(n) * -h);
  };
  const double tol = 1e-3;
  auto distanceTo = [&](const gp_Pnt& p, const TopoDS_Shape& edge) {
    BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p).Vertex(), edge);
    return d.IsDone() && d.NbSolution() > 0 ? d.Value() : 1e300;
  };
  // which sketch curve generated a side face: one of its edges lies on the curve (not a lateral edge)
  auto curveOf = [&](const TopoDS_Shape& face) -> int {
    for (const TopoDS_Shape& e : edgesOf(face)) {
      BRepAdaptor_Curve c(TopoDS::Edge(e));
      const gp_Pnt a = onSketch(c.Value(c.FirstParameter())), b = onSketch(c.Value(c.LastParameter()));
      const gp_Pnt m = onSketch(c.Value(0.5 * (c.FirstParameter() + c.LastParameter())));
      if (a.Distance(b) < tol && a.Distance(m) < tol) continue;  // lateral edge: projects to a point
      for (const auto& [id, ce] : curves)
        if (distanceTo(a, ce) < tol && distanceTo(b, ce) < tol && distanceTo(m, ce) < tol) return id;
    }
    return -1;
  };
  // collect prefix(2/)?Side/<digits> keys
  std::map<int, std::vector<std::pair<std::string, TopoDS_Shape>>> byCurve;  // id -> (old key, face)
  std::vector<std::string> keys;
  for (const auto& [key, shape] : names) {
    if (key.rfind(prefix, 0) != 0 || shape.ShapeType() != TopAbs_FACE) continue;
    std::string rest = key.substr(prefix.size());
    if (rest.rfind("2/", 0) == 0) rest = rest.substr(2);
    if (rest.rfind("Side/", 0) != 0) continue;
    rest = rest.substr(5);
    if (rest.empty() || !std::all_of(rest.begin(), rest.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
    keys.push_back(key);
  }
  if (keys.empty()) return;
  std::map<std::string, std::string> rename;
  for (const std::string& key : keys) {
    const TopoDS_Shape face = names[key];
    const int id = curveOf(face);
    if (id < 0) continue;
    byCurve[id].emplace_back(key, face);
  }
  for (auto& [id, list] : byCurve) {
    // keep the two sides of a two-sided extrusion apart ("2/" prefix) and order pieces deterministically
    std::map<std::string, std::vector<std::pair<std::string, TopoDS_Shape>>> bySide;
    for (auto& item : list) bySide[item.first.substr(0, item.first.find("Side/"))].push_back(item);
    for (auto& [sidePrefix, items] : bySide) {
      std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return shapeLess(a.second, b.second); });
      for (size_t i = 0; i < items.size(); ++i) {
        std::string label = std::format("{}Side/c{}", sidePrefix, id);
        if (items.size() > 1) label += std::format("#{}", i + 1);
        rename[items[i].first] = label;
      }
    }
  }
  for (const auto& [oldKey, newKey] : rename) {
    TopoDS_Shape shape = names[oldKey];
    names.erase(oldKey);
    names[newKey] = shape;
  }
}

TopoDS_Shape prismFace(const TopoDS_Face& face, const gp_Dir& dir, double length, double taperDeg,
                       const std::string& prefix, NameMap& names, std::string* error) {
  if (face.IsNull() || length <= Precision::Confusion()) {
    if (error) *error = "invalid prism length";
    return {};
  }
  try {
    BRepPrimAPI_MakePrism mk(face, gp_Vec(dir) * length, Standard_False);
    mk.Build();
    if (!mk.IsDone()) {
      if (error) *error = "extrusion failed";
      return {};
    }
    TopoDS_Shape prism = mk.Shape();
    NameMap local;
    nameSweepResult(mk, face, prefix, local, true);
    if (std::abs(taperDeg) > 1e-9) {
      auto pl = core::facePlane(face);
      if (!pl) {
        if (error) *error = "taper needs a planar profile";
        return {};
      }
      gp_Pln neutral(*pl);
      BRepOffsetAPI_DraftAngle draft(prism);
      // OCCT's draft angle is positive when the face tilts towards the pull direction (the
      // solid narrows); FabriCAD/Fusion use positive = outward, so negate.
      double ang = -taperDeg * M_PI / 180.0;
      int added = 0;
      for (const auto& [n, s] : local) {
        if (n.find("Side/") == std::string::npos || s.ShapeType() != TopAbs_FACE) continue;
        draft.Add(TopoDS::Face(s), dir, ang, neutral, Standard_True);
        if (!draft.AddDone()) {
          draft.Remove(TopoDS::Face(s));
          continue;
        }
        ++added;
      }
      if (added == 0) {
        if (error) *error = "taper angle cannot be applied";
        return {};
      }
      draft.Build();
      if (!draft.IsDone()) {
        if (error) *error = "taper failed (angle too large?)";
        return {};
      }
      TopoDS_Shape tapered = draft.Shape();
      local = mapNames(local, History::of(draft), tapered);
      prism = tapered;
    }
    if (!core::isValidShape(prism)) {
      if (error) *error = "extrusion produced an invalid shape";
      return {};
    }
    names.insert(local.begin(), local.end());
    return prism;
  } catch (const Standard_Failure& e) {
    if (error) *error = e.GetMessageString() ? e.GetMessageString() : "extrusion failed";
    return {};
  }
}

TopoDS_Shape revolveFace(const TopoDS_Face& face, const gp_Ax1& axis, double angleDeg, const std::string& prefix,
                         NameMap& names, std::string* error) {
  if (face.IsNull() || angleDeg <= 1e-9) {
    if (error) *error = "invalid revolve angle";
    return {};
  }
  bool full = angleDeg >= 360.0 - 1e-9;
  try {
    double ang = std::min(angleDeg, 360.0) * M_PI / 180.0;
    BRepPrimAPI_MakeRevol mk(face, axis, ang, Standard_False);
    mk.Build();
    if (!mk.IsDone()) {
      if (error) *error = "revolve failed";
      return {};
    }
    TopoDS_Shape s = mk.Shape();
    if (!core::isValidShape(s)) {
      if (error) *error = "revolve produced an invalid shape (profile crossing the axis?)";
      return {};
    }
    nameSweepResult(mk, face, prefix, names, !full);
    return s;
  } catch (const Standard_Failure& e) {
    if (error) *error = e.GetMessageString() ? e.GetMessageString() : "revolve failed";
    return {};
  }
}

std::optional<double> distanceToPlane(const gp_Pnt& from, const gp_Dir& dir, const gp_Pln& target) {
  gp_Dir n = target.Axis().Direction();
  double denom = gp_Vec(dir).Dot(gp_Vec(n));
  if (std::abs(denom) < 1e-9) return std::nullopt;
  gp_Vec toPlane(from, target.Location());
  return toPlane.Dot(gp_Vec(n)) / denom;
}

TopoDS_Shape halfSpaceOf(const TopoDS_Face& face, const gp_Pnt& inside, double size) {
  if (face.IsNull()) return {};
  try {
    Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
    if (surf.IsNull()) return {};
    double u0, u1, v0, v1;
    surf->Bounds(u0, u1, v0, v1);
    auto clamp = [size](double& a, double& b) {
      if (Precision::IsInfinite(a)) a = -size;
      if (Precision::IsInfinite(b)) b = size;
    };
    clamp(u0, u1);
    clamp(v0, v1);
    BRepLib_MakeFace mf(surf, u0, u1, v0, v1, Precision::Confusion());
    if (!mf.IsDone()) return {};
    TopoDS_Face big = mf.Face();
    BRepPrimAPI_MakeHalfSpace hs(big, inside);
    return hs.Solid();
  } catch (const Standard_Failure&) {
    return {};
  }
}

}  // namespace fabricad::part
