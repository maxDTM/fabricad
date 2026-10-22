// FabriCAD sketch: SketchFeature implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchFeature.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/ShapeUtils.h"

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>

#include <algorithm>
#include <format>
#include <map>
#include <sstream>

namespace fabricad::sketch {

using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

SketchFeature::SketchFeature() : Feature(kType) {
  addRef("plane", core::kPlaneKinds).setLabel("Plane");
  core::Property& v = addBool("visible", true);
  v.flags().hidden = true;
}

// ---------------------------------------------------------------------------
// recompute
// ---------------------------------------------------------------------------

std::vector<ObjectId> SketchFeature::extraDependencies() const {
  std::vector<ObjectId> out;
  for (const Geometry& g : sketch_.geometries())
    if (g.projected && g.source.isValid() && std::find(out.begin(), out.end(), g.source.owner) == out.end())
      out.push_back(g.source.owner);
  return out;
}

void SketchFeature::updateProjected(core::RecomputeContext& ctx) {
  for (Geometry& g : sketch_.geometries()) {
    if (!g.projected || !g.source.isValid()) continue;
    TopoDS_Shape s;
    if (g.source.isBrep() && g.source.kind != ShapeKind::Body) {
      // Projections follow the body like Fusion's: the same sub-shape index with the same
      // curve type is accepted first (topology unchanged, only sizes moved), the geometric
      // signature is the fallback after topology changes. The stored signature is refreshed
      // so the reference stays current without a permanent Warning on the sketch.
      const TopoDS_Shape& body = ctx.bodyShape(g.source.owner);
      if (body.IsNull()) {
        ctx.setWarning(std::format("projected curve {}: body not found", g.id));
        continue;
      }
      TopoDS_Shape byIndex = core::subShapeAt(body, core::toTopAbs(g.source.kind), g.source.index);
      if (!byIndex.IsNull()) {
        core::GeomSignature sig = core::computeSignature(byIndex);
        if (!g.source.signature.isValid() || sig.geomType == g.source.signature.geomType) {
          s = byIndex;
          g.source.signature = sig;
        }
      }
      if (s.IsNull()) {
        core::ResolveResult rr = core::resolveSubShape(body, g.source, ctx.modelSize());
        if (!rr) {
          ctx.setWarning(std::format("projected curve {}: reference lost", g.id));
          continue;
        }
        if (rr.ambiguous) ctx.setWarning(std::format("projected curve {}: reference is ambiguous", g.id));
        g.source.index = rr.index;
        g.source.signature = core::computeSignature(rr.shape);
        s = rr.shape;
      }
    } else {
      ShapeRef src = g.source;
      s = ctx.resolve(src);
      g.source = src;
    }
    if (s.IsNull()) continue;
    std::string warning;
    auto ng = projectShape(s, plane_, &warning);
    if (!ng) continue;
    if (!warning.empty()) ctx.setWarning(std::format("projected curve {}: {}", g.id, warning));
    ng->id = g.id;
    ng->construction = g.construction;
    ng->projected = true;
    ng->source = g.source;
    g = *ng;
  }
}

SolveResult SketchFeature::updateGeometry(const Sketch::ExternalEval& eval) {
  if (eval) sketch_.evaluateExpressions(eval);
  SolveResult r = sketch_.solve();
  rebuildOutput();
  return r;
}

void SketchFeature::recompute(core::RecomputeContext& ctx) {
  ShapeRef planeRef = ref("plane");
  if (planeRef.isValid()) {
    std::optional<gp_Ax3> pl;
    if (planeRef.kind == core::ShapeKind::Face) {
      // through the context: history name first, index/signature refreshed on a re-match
      TopoDS_Shape face = ctx.resolve(planeRef);
      if (!face.IsNull()) pl = core::facePlane(face);
      if (pl) pl = alignToStoredAxes(*pl);
      if (pl) prop("plane").setRef(planeRef);  // keeps the refreshed index / signature / name
    } else {
      pl = ctx.plane(planeRef);
    }
    if (!pl) {
      ctx.setError(std::format("sketch plane not found: {}", planeRef.toString()));
      return;
    }
    plane_ = *pl;
  }
  updateProjected(ctx);

  // dimension expressions: sketch dimensions first, then document parameters
  const core::Document& doc = ctx.document();
  core::ExpressionContext ectx = doc.expressionContext();
  core::ParameterLookup docLookup = ectx.lookup;
  const Sketch* sk = &sketch_;
  ectx.lookup = [sk, docLookup](const std::string& name) -> std::optional<core::Quantity> {
    if (const Constraint* d = sk->dimensionByName(name))
      return d->type == ConstraintType::Angle ? core::Quantity::deg(d->value) : core::Quantity::mm(d->value);
    return docLookup ? docLookup(name) : std::nullopt;
  };
  auto eval = [&](const std::string& expr, bool angle, std::string* err) -> std::optional<double> {
    core::EvalResult r = core::evaluateExpression(expr, ectx, angle ? core::Dimension::Angle() : core::Dimension::Length());
    if (!r.ok) {
      if (err) *err = r.error;
      return std::nullopt;
    }
    return r.value.value;
  };
  for (const std::string& e : sketch_.evaluateExpressions(eval)) ctx.setWarning(e);

  SolveResult r = sketch_.solve();
  if (r.status == SolveResult::Status::Failed) ctx.setWarning("sketch solver failed: " + r.message);
  if (r.status == SolveResult::Status::Conflicting) {
    std::string ids;
    for (int id : r.conflicting) ids += (ids.empty() ? "" : ", ") + std::to_string(id);
    ctx.setWarning("conflicting constraints: " + ids);
  }
  rebuildOutput();
}

void SketchFeature::rebuildOutput() {
  setOutputShape(makeEdgesCompound(sketch_, plane_, true));
  rebuildDisplayShapes();
  invalidateProfiles();
}

void SketchFeature::rebuildDisplayShapes() {
  // Viewer convention (src/viewer/SceneModel.h): namedShapes "display:<style>" compounds are
  // drawn in their style colour instead of the plain outputShape().
  auto& named = namedShapes();
  for (auto it = named.begin(); it != named.end();) {
    if (it->first.rfind("display:", 0) == 0) it = named.erase(it);
    else ++it;
  }
  std::map<std::string, std::vector<TopoDS_Shape>> groups;
  std::vector<TopoDS_Shape> points;
  for (const Geometry& g : sketch_.geometries()) {
    if (g.type == GeomType::Point) {
      points.push_back(makeVertex(g, plane_));
      continue;
    }
    GeometryStatus st = sketch_.statusOf(g.id);
    const char* key = "display:free";
    if (st == GeometryStatus::Conflicting) key = "display:conflict";
    else if (g.construction) key = "display:construction";
    else {
      switch (st) {
        case GeometryStatus::Free: key = "display:free"; break;
        case GeometryStatus::FullyConstrained: key = "display:defined"; break;
        case GeometryStatus::Fixed: key = "display:fixed"; break;
        case GeometryStatus::Projected: key = "display:projected"; break;
        case GeometryStatus::Conflicting: key = "display:conflict"; break;
      }
    }
    TopoDS_Shape e = makeEdge(g, plane_);
    if (!e.IsNull()) groups[key].push_back(e);
    for (const GeoRef& r : g.pointRefs())
      if (r.pos != PointPos::Mid) points.push_back(BRepBuilderAPI_MakeVertex(toWorld(g.point(r.pos, r.index))).Vertex());
  }
  for (auto& [key, shapes] : groups) {
    TopoDS_Shape c = core::makeCompound(shapes);
    if (!c.IsNull()) named[key] = c;
  }
  if (!points.empty()) {
    TopoDS_Shape c = core::makeCompound(points);
    if (!c.IsNull()) named["display:points"] = c;
  }
}

// ---------------------------------------------------------------------------
// profiles
// ---------------------------------------------------------------------------

const std::vector<ProfileRegion>& SketchFeature::regions() const {
  if (!profilesValid_) {
    try {
      regions_ = computeProfiles(sketch_, plane_);
    } catch (const Standard_Failure& f) {
      FC_WARN("profile detection failed: {}", f.GetMessageString() ? f.GetMessageString() : "?");
      regions_.clear();
    }
    profilesValid_ = true;
  }
  return regions_;
}

const ProfileRegion* SketchFeature::regionAt(Pt2 p) const {
  gp_Pnt w = toWorld(p);
  const ProfileRegion* best = nullptr;
  for (const ProfileRegion& r : regions())
    if (faceContains(r.face, w) && (!best || r.area < best->area)) best = &r;
  return best;
}

ShapeRef SketchFeature::profileRef(const ProfileRegion& r) const {
  return ShapeRef::profile(id(), toWorldVec(r.seed), r.curveIds);
}

std::vector<core::ProfileInfo> SketchFeature::profiles() const {
  std::vector<core::ProfileInfo> out;
  for (const ProfileRegion& r : regions()) {
    core::ProfileInfo info;
    info.ref = profileRef(r);
    info.area = r.area;
    info.curveIds = r.curveIds;
    out.push_back(std::move(info));
  }
  return out;
}

namespace {
std::vector<int> parseIds(const std::string& s) {
  std::vector<int> ids;
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    try {
      if (!tok.empty()) ids.push_back(std::stoi(tok));
    } catch (...) {
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

double overlap(const std::vector<int>& a, const std::vector<int>& b) {
  if (a.empty() && b.empty()) return 1.0;
  std::vector<int> inter;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(inter));
  std::vector<int> uni;
  std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(uni));
  return uni.empty() ? 0.0 : static_cast<double>(inter.size()) / static_cast<double>(uni.size());
}
}  // namespace

TopoDS_Face SketchFeature::profileFace(const ShapeRef& ref) const {
  if (ref.kind != ShapeKind::Profile) return {};
  const auto& regs = regions();
  if (regs.empty()) return {};
  std::vector<int> wanted = parseIds(ref.name);
  // 1. regions containing the seed point
  const ProfileRegion* best = nullptr;
  double bestScore = -1;
  if (ref.hasPoint) {
    gp_Pnt seed = ref.point.toPnt();
    for (const ProfileRegion& r : regs) {
      if (!faceContains(r.face, seed)) continue;
      double sc = overlap(wanted, r.curveIds);
      if (sc > bestScore) {
        bestScore = sc;
        best = &r;
      }
    }
    if (best) return best->face;
  }
  // 2. best curve-id overlap
  for (const ProfileRegion& r : regs) {
    double sc = overlap(wanted, r.curveIds);
    if (sc > bestScore) {
      bestScore = sc;
      best = &r;
    }
  }
  if (best && bestScore >= 0.5) return best->face;
  return {};
}

TopoDS_Shape SketchFeature::curveEdge(int curveId) const {
  const Geometry* g = sketch_.geometry(curveId);
  if (!g) return {};
  if (g->type == GeomType::Point) return makeVertex(*g, plane_);
  return makeEdge(*g, plane_);
}

TopoDS_Shape SketchFeature::allEdges(bool includeConstruction) const {
  return makeEdgesCompound(sketch_, plane_, includeConstruction);
}

std::optional<gp_Pnt> SketchFeature::sketchPoint(const ShapeRef& ref) const {
  if (ref.kind != ShapeKind::SketchPoint) return std::nullopt;
  const Geometry* g = sketch_.geometry(ref.index);
  if (!g) return std::nullopt;
  if (g->type == GeomType::Point) return toWorld(g->center());
  auto p = sketch_.pointOf(fromSubIndex(ref.index, ref.subIndex));
  if (!p) return std::nullopt;
  return toWorld(*p);
}

// ---------------------------------------------------------------------------
// display
// ---------------------------------------------------------------------------

std::vector<SketchDisplayItem> SketchFeature::displayItems() const {
  std::vector<SketchDisplayItem> out;
  for (const Geometry& g : sketch_.geometries()) {
    // text yields one item per glyph contour
    for (const auto& pl : g.polylines(64)) {
      SketchDisplayItem item;
      item.geoId = g.id;
      item.construction = g.construction;
      item.status = sketch_.statusOf(g.id);
      item.isPoint = g.type == GeomType::Point;
      for (const Pt2& p : pl) item.points.push_back(toWorldVec(p));
      out.push_back(std::move(item));
    }
  }
  return out;
}

std::string SketchFeature::statusText() const {
  const SolveResult& r = sketch_.lastResult();
  if (!r.conflicting.empty()) return "Over-constrained";
  if (r.status == SolveResult::Status::Failed) return "Solver failed";
  if (r.dofs == 0 && !sketch_.isEmpty()) return r.redundant.empty() ? "Fully constrained" : "Fully constrained (redundant)";
  return std::format("{} DOF", std::max(0, r.dofs));
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

void SketchFeature::storePlaneAxes(const gp_Ax3& p) { planeAxes_ = PlaneAxes{p.Direction(), p.XDirection(), p.Direct()}; }

gp_Ax3 SketchFeature::alignToStoredAxes(const gp_Ax3& p) {
  if (!planeAxes_) {
    storePlaneAxes(p);
    return p;
  }
  gp_Dir n = p.Direction();
  const double d = n.Dot(planeAxes_->normal);
  if (std::abs(d) < 0.999) {  // another face (the plane reference was changed): start over
    storePlaneAxes(p);
    return p;
  }
  if (d < 0) n.Reverse();  // same face, other orientation: keep the side the sketch was made on
  // the stored X projected on the (possibly tilted) plane; the plane's own X if degenerate
  gp_Vec x(planeAxes_->x);
  x -= gp_Vec(n) * x.Dot(gp_Vec(n));
  gp_Dir xd = p.XDirection();
  if (x.Magnitude() > 1e-6) xd = gp_Dir(x);  // else the plane's X (perpendicular to +-n)
  gp_Ax3 out(p.Location(), n, xd);
  if (!planeAxes_->direct) out.YReverse();  // same handedness as stored
  return out;
}

QJsonObject SketchFeature::toJson() const {
  QJsonObject o = Feature::toJson();
  if (planeAxes_) {
    const gp_Dir& n = planeAxes_->normal;
    const gp_Dir& x = planeAxes_->x;
    o["planeAxes"] = QJsonArray{n.X(), n.Y(), n.Z(), x.X(), x.Y(), x.Z()};
    if (!planeAxes_->direct) o["planeAxesMirrored"] = true;
  }
  QJsonObject sk = sketch_.toJson();
  o["geometry"] = sk["geometry"];
  o["constraints"] = sk["constraints"];
  o["nextGeoId"] = sk["nextGeoId"];
  o["nextConstraintId"] = sk["nextConstraintId"];
  return o;
}

void SketchFeature::fromJson(const QJsonObject& o) {
  Feature::fromJson(o);
  sketch_.fromJson(o);
  // older files have no axes: the first recompute stores the ones it computes
  if (const QJsonArray a = o["planeAxes"].toArray(); a.size() == 6) {
    const gp_Vec n(a[0].toDouble(), a[1].toDouble(), a[2].toDouble());
    gp_Vec x(a[3].toDouble(), a[4].toDouble(), a[5].toDouble());
    if (n.Magnitude() > 1e-9) {
      x -= n.Normalized() * x.Dot(n.Normalized());
      if (x.Magnitude() > 1e-9) planeAxes_ = PlaneAxes{gp_Dir(n), gp_Dir(x), !o["planeAxesMirrored"].toBool()};
    }
  }
  invalidateProfiles();
}

}  // namespace fabricad::sketch
