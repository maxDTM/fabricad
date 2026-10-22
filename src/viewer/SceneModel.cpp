// FabriCAD viewer: GL-free display model.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/SceneModel.h"

#include "core/Body.h"
#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <functional>

namespace fabricad::viewer {

using core::Document;
using core::Feature;
using core::FeatureStatus;
using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

namespace {

std::size_t combine(std::size_t a, std::size_t b) { return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2)); }

std::size_t hashOf(const TopoDS_Shape& s, SceneStyle style) {
  return combine(core::shapeHash(s), static_cast<std::size_t>(style) + 1);
}

bool isComponentActive(const SceneOptions& opt, ObjectId comp) {
  if (opt.activeComponents.empty()) return true;
  return std::find(opt.activeComponents.begin(), opt.activeComponents.end(), comp) != opt.activeComponents.end();
}

/// First sub-shape type found in a shape (FACE before EDGE before VERTEX).
TopAbs_ShapeEnum dominantType(const TopoDS_Shape& s) {
  if (s.IsNull()) return TopAbs_SHAPE;
  for (TopAbs_ShapeEnum t : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX}) {
    TopExp_Explorer ex(s, t);
    if (ex.More()) return t;
  }
  return TopAbs_SHAPE;
}

gp_Pnt edgeMid(const TopoDS_Edge& e) {
  BRepAdaptor_Curve c(e);
  return c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
}

}  // namespace

const char* sceneStyleName(SceneStyle s) {
  switch (s) {
    case SceneStyle::Body: return "body";
    case SceneStyle::SketchFree: return "free";
    case SceneStyle::SketchDefined: return "defined";
    case SceneStyle::SketchConstruction: return "construction";
    case SceneStyle::SketchFixed: return "fixed";
    case SceneStyle::SketchProjected: return "projected";
    case SceneStyle::SketchConflict: return "conflict";
    case SceneStyle::SketchPoints: return "points";
    case SceneStyle::Profile: return "profile";
    case SceneStyle::ConstructionPlane: return "construction-plane";
    case SceneStyle::ConstructionAxis: return "construction-axis";
    case SceneStyle::ConstructionPoint: return "construction-point";
    case SceneStyle::Joint: return "joint";
    case SceneStyle::OriginPlane: return "origin-plane";
    case SceneStyle::OriginAxis: return "origin-axis";
    case SceneStyle::OriginPoint: return "origin-point";
    case SceneStyle::ProfileShade: return "profile-shade";
  }
  return "?";
}

std::optional<SceneStyle> sketchStyleFromDisplayKey(const std::string& key) {
  static const std::string kPrefix = "display:";
  if (key.rfind(kPrefix, 0) != 0) return std::nullopt;
  const std::string s = key.substr(kPrefix.size());
  if (s == "free") return SceneStyle::SketchFree;
  if (s == "defined") return SceneStyle::SketchDefined;
  if (s == "construction") return SceneStyle::SketchConstruction;
  if (s == "fixed") return SceneStyle::SketchFixed;
  if (s == "projected") return SceneStyle::SketchProjected;
  if (s == "conflict") return SceneStyle::SketchConflict;
  if (s == "points") return SceneStyle::SketchPoints;
  return std::nullopt;
}

std::array<float, 4> styleColor(SceneStyle s, const ViewerPalette& p) {
  switch (s) {
    case SceneStyle::Body: return p.bodyDefault;
    case SceneStyle::SketchFree: return p.skFree;
    case SceneStyle::SketchDefined: return p.skConstrained;
    case SceneStyle::SketchConstruction: return p.skConstruction;
    case SceneStyle::SketchFixed: return p.skFixed;
    case SceneStyle::SketchProjected: return p.skProjected;
    case SceneStyle::SketchConflict: return p.skConflict;
    case SceneStyle::SketchPoints: return p.skFree;
    case SceneStyle::Profile: return p.profilePick;
    case SceneStyle::ProfileShade: return p.profileFill;
    case SceneStyle::ConstructionPlane: return p.constructionFill;
    case SceneStyle::ConstructionAxis: return p.construction;
    case SceneStyle::ConstructionPoint: return p.construction;
    case SceneStyle::Joint: return p.joint;
    case SceneStyle::OriginPlane: return p.originFill;
    case SceneStyle::OriginAxis: return p.originEdge;
    case SceneStyle::OriginPoint: return p.originPoint;
  }
  return p.wire;
}

std::array<float, 4> bodyColor(const std::array<float, 4>& c, const ViewerPalette& pal) {
  static const std::array<float, 4> kDocDefault = core::Body().color;
  for (int i = 0; i < 4; ++i)
    if (std::abs(c[static_cast<size_t>(i)] - kDocDefault[static_cast<size_t>(i)]) > 1e-4f) return c;
  return pal.bodyDefault;
}

std::vector<int> probeCurveIds(const core::IProfileSource& src, int maxId, int maxGap) {
  std::vector<int> out;
  int gap = 0;
  for (int id = 0; id <= maxId; ++id) {
    TopoDS_Shape e = src.curveEdge(id);
    if (!e.IsNull()) {
      out.push_back(id);
      gap = 0;
    } else if (++gap > maxGap) {
      break;
    }
  }
  return out;
}

bool SceneModel::featureVisible(const Document& doc, const Feature& f, const SceneOptions& opt) {
  const int idx = doc.indexOf(f.id());
  if (idx < 0 || idx >= doc.rollbackIndex()) return false;
  if (f.isSuppressed()) return false;
  if (f.status() != FeatureStatus::Ok && f.status() != FeatureStatus::Warning) return false;
  auto ov = opt.visibilityOverrides.find(f.id());
  if (ov != opt.visibilityOverrides.end() && ov->second.has_value()) return *ov->second;
  if (opt.hiddenFeatures.count(f.id())) return false;
  const std::string folder = f.browserFolder();
  if (folder == "Sketches") return opt.showSketches;
  if (folder == "Construction") return opt.showConstruction;
  if (folder == "Joints") return opt.showJoints;
  return false;  // other features' outputShape() is a tool solid, not for display
}

std::vector<SceneItem> SceneModel::collect(const Document& doc, const SceneOptions& opt) {
  std::vector<SceneItem> items;

  // --- bodies ---------------------------------------------------------------------
  for (ObjectId id : doc.liveBodies(true)) {
    const core::Body* b = doc.body(id);
    if (!b) continue;
    SceneItem it;
    it.key = "body:" + std::to_string(id);
    it.style = SceneStyle::Body;
    it.ref = ShapeRef::body(id);
    it.shape = doc.bodyShape(id);
    it.placement = doc.worldPlacement(b->component);
    it.color = bodyColor(b->color, opt.palette);
    it.ghosted = !isComponentActive(opt, b->component);
    it.geomHash = hashOf(it.shape, it.style);
    items.push_back(std::move(it));
  }

  // --- occurrences: components with a definition show the definition's bodies ---------
  for (const core::Component* c : doc.components()) {
    if (!c || !c->isOccurrence() || !c->visible) continue;
    // hidden when any ancestor is hidden
    bool hidden = false;
    for (const core::Component* p = doc.component(c->parent); p; p = doc.component(p->parent))
      if (!p->visible) { hidden = true; break; }
    if (hidden) continue;
    for (ObjectId bid : doc.displayedBodies(c->id)) {
      const core::Body* b = doc.body(bid);
      if (!b || !b->visible || doc.bodyShape(bid).IsNull()) continue;
      SceneItem it;
      it.key = "occ:" + std::to_string(c->id) + ":body:" + std::to_string(bid);
      it.style = SceneStyle::Body;
      it.ref = ShapeRef::component(c->id);
      it.shape = doc.bodyShape(bid);
      it.placement = doc.worldPlacement(c->id);
      it.color = bodyColor(b->color, opt.palette);
      it.ghosted = !isComponentActive(opt, c->id);
      it.geomHash = hashOf(it.shape, it.style);
      items.push_back(std::move(it));
    }
  }

  // --- feature outputs (sketches, construction, joints) -------------------------------
  std::set<ObjectId> liveSketches;
  for (const auto& fp : doc.features()) {
    const Feature& f = *fp;
    if (!featureVisible(doc, f, opt)) continue;
    const std::string folder = f.browserFolder();
    const std::string base = "feat:" + std::to_string(f.id());
    if (folder == "Sketches") {
      liveSketches.insert(f.id());
      bool styled = false;
      for (const auto& [name, shape] : f.namedShapes()) {
        auto st = sketchStyleFromDisplayKey(name);
        if (!st || shape.IsNull()) continue;
        styled = true;
        SceneItem it;
        it.key = base + "/" + name;
        it.style = *st;
        it.ref = ShapeRef::sketch(f.id());
        it.shape = shape;
        it.color = styleColor(*st, opt.palette);
        it.geomHash = hashOf(shape, *st);
        items.push_back(std::move(it));
      }
      if (!styled && !f.outputShape().IsNull()) {
        SceneItem it;
        it.key = base;
        it.style = SceneStyle::SketchFree;
        it.ref = ShapeRef::sketch(f.id());
        it.shape = f.outputShape();
        it.color = styleColor(it.style, opt.palette);
        it.geomHash = hashOf(it.shape, it.style);
        items.push_back(std::move(it));
      }
      continue;
    }
    if (f.outputShape().IsNull()) continue;
    SceneItem it;
    it.key = base;
    it.shape = f.outputShape();
    if (folder == "Joints") {
      it.style = SceneStyle::Joint;
      it.ref = ShapeRef::feature(f.id(), ShapeKind::Joint);
    } else {
      ShapeKind kind = ShapeKind::ConstructionPlane;
      if (f.asPlane()) kind = ShapeKind::ConstructionPlane;
      else if (f.asAxis()) kind = ShapeKind::ConstructionAxis;
      else if (f.asPoint()) kind = ShapeKind::ConstructionPoint;
      else {
        switch (dominantType(it.shape)) {
          case TopAbs_EDGE: kind = ShapeKind::ConstructionAxis; break;
          case TopAbs_VERTEX: kind = ShapeKind::ConstructionPoint; break;
          default: kind = ShapeKind::ConstructionPlane; break;
        }
      }
      it.style = kind == ShapeKind::ConstructionAxis    ? SceneStyle::ConstructionAxis
                 : kind == ShapeKind::ConstructionPoint ? SceneStyle::ConstructionPoint
                                                        : SceneStyle::ConstructionPlane;
      it.ref = ShapeRef::feature(f.id(), kind);
    }
    it.color = styleColor(it.style, opt.palette);
    it.geomHash = hashOf(it.shape, it.style);
    items.push_back(std::move(it));
  }

  // --- profiles: pickable while a tool picks them, else a light tint of the closed regions of
  // visible, finished sketches (SK-12: Fusion shades them after Finish Sketch) ----------------
  if (opt.showProfiles || opt.shadeProfiles) {
    const SceneStyle profileStyle = opt.showProfiles ? SceneStyle::Profile : SceneStyle::ProfileShade;
    for (ObjectId sid : liveSketches) {
      const Feature* f = doc.feature(sid);
      const core::IProfileSource* src = f ? f->profileSource() : nullptr;
      if (!src) continue;
      const std::size_t h = core::shapeHash(f->outputShape());
      ProfileCache& pc = profiles_[sid];
      if (pc.hash != h || pc.items.empty()) {
        pc.hash = h;
        pc.items.clear();
        int n = 0;
        for (const core::ProfileInfo& p : src->profiles()) {
          TopoDS_Face face = src->profileFace(p.ref);
          if (face.IsNull()) continue;
          SceneItem it;
          it.key = "profile:" + std::to_string(sid) + ":" + (p.ref.name.empty() ? std::to_string(n) : p.ref.name);
          ++n;
          it.style = SceneStyle::Profile;
          it.ref = p.ref;
          it.shape = face;
          it.geomHash = hashOf(face, it.style);
          pc.items.push_back(std::move(it));
        }
      }
      for (SceneItem it : pc.items) {
        it.style = profileStyle;
        it.color = styleColor(profileStyle, opt.palette);
        items.push_back(std::move(it));
      }
    }
  }
  for (auto it = profiles_.begin(); it != profiles_.end();)
    it = liveSketches.count(it->first) ? std::next(it) : profiles_.erase(it);

  // --- origin -----------------------------------------------------------------------------
  if (opt.showOrigin || opt.showOriginPlanes) {
    for (const char* name : {"XY", "XZ", "YZ"}) {
      SceneItem it;
      it.key = std::string("origin:") + name;
      it.style = SceneStyle::OriginPlane;
      it.ref = ShapeRef::originPlane(name);
      // origin planes sit in the positive quadrant like Fusion, offset from the axes
      gp_Ax3 ax = core::originPlane(name);
      const double h = opt.originSize;
      gp_Pnt c = ax.Location().Translated(gp_Vec(ax.XDirection()) * h + gp_Vec(ax.YDirection()) * h);
      gp_Ax3 shifted(c, ax.Direction(), ax.XDirection());
      it.shape = core::planeFace(shifted, h * 0.9);
      it.color = styleColor(it.style, opt.palette);
      it.geomHash = combine(hashOf(it.shape, it.style), static_cast<std::size_t>(h * 1000));
      items.push_back(std::move(it));
    }
  }
  if (opt.showOrigin) {
    for (const char* name : {"X", "Y", "Z"}) {
      gp_Ax1 ax = core::originAxis(name);
      SceneItem it;
      it.key = std::string("origin:") + name;
      it.style = SceneStyle::OriginAxis;
      it.ref = ShapeRef::originAxis(name);
      it.shape = BRepBuilderAPI_MakeEdge(ax.Location(), ax.Location().Translated(gp_Vec(ax.Direction()) * opt.originSize * 2.2)).Edge();
      const int axisIndex = name[0] - 'X';
      it.color = ViewerPalette::withAlpha(opt.palette.axis[static_cast<size_t>(axisIndex)], opt.palette.originAxisAlpha);
      it.geomHash = combine(static_cast<std::size_t>(it.style), static_cast<std::size_t>(opt.originSize * 1000) + name[0]);
      items.push_back(std::move(it));
    }
    SceneItem it;
    it.key = "origin:O";
    it.style = SceneStyle::OriginPoint;
    it.ref = ShapeRef::originPoint();
    it.shape = BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, 0)).Vertex();
    it.color = styleColor(it.style, opt.palette);
    it.geomHash = static_cast<std::size_t>(it.style) * 7919u;
    items.push_back(std::move(it));
  }

  // keep sketch caches only for live sketches
  for (auto it = sketches_.begin(); it != sketches_.end();)
    it = doc.feature(it->first) ? std::next(it) : sketches_.erase(it);
  return items;
}

SceneModel::SketchCache& SceneModel::sketchCache(const Document& doc, ObjectId sketchId) {
  SketchCache& c = sketches_[sketchId];
  const Feature* f = doc.feature(sketchId);
  const std::size_t h = f ? core::shapeHash(f->outputShape()) : 0;
  if (c.hash == h && h != 0) return c;
  c = SketchCache{};
  c.hash = h;
  const core::IProfileSource* src = f ? f->profileSource() : nullptr;
  if (!src) return c;
  for (int id : probeCurveIds(*src)) {
    TopoDS_Shape s = src->curveEdge(id);
    if (s.ShapeType() != TopAbs_EDGE) continue;
    SketchCurveInfo info;
    info.id = id;
    info.edge = TopoDS::Edge(s);
    info.mid = edgeMid(info.edge);
    c.curves.push_back(info);
  }
  return c;
}

int SceneModel::sketchCurveForEdge(const Document& doc, ObjectId sketchId, const TopoDS_Shape& edge) {
  if (edge.IsNull() || edge.ShapeType() != TopAbs_EDGE) return -1;
  SketchCache& c = sketchCache(doc, sketchId);
  for (const SketchCurveInfo& ci : c.curves)
    if (ci.edge.IsSame(edge)) return ci.id;
  const gp_Pnt m = edgeMid(TopoDS::Edge(edge));
  int best = -1;
  double bestD = 1e-4;
  for (const SketchCurveInfo& ci : c.curves) {
    const double d = ci.mid.Distance(m);
    if (d < bestD) {
      bestD = d;
      best = ci.id;
    }
  }
  return best;
}

const std::vector<SceneModel::PointCandidate>& SceneModel::sketchPoints(const Document& doc, ObjectId sketchId) {
  SketchCache& c = sketchCache(doc, sketchId);
  if (c.pointsBuilt) return c.points;
  c.pointsBuilt = true;
  const Feature* f = doc.feature(sketchId);
  const core::IProfileSource* src = f ? f->profileSource() : nullptr;
  if (!src) return c.points;
  int maxId = 0;
  for (const SketchCurveInfo& ci : c.curves) {
    maxId = std::max(maxId, ci.id);
    for (auto role : {core::SketchPointRole::Start, core::SketchPointRole::End, core::SketchPointRole::Center}) {
      ShapeRef r = ShapeRef::sketchPoint(sketchId, ci.id, role);
      if (auto p = src->sketchPoint(r)) c.points.push_back({r, *p});
    }
  }
  // standalone points share the geometry id space with curves
  for (int id = 0; id <= maxId + 64; ++id) {
    ShapeRef r = ShapeRef::sketchPoint(sketchId, id, core::SketchPointRole::Standalone);
    if (auto p = src->sketchPoint(r)) c.points.push_back({r, *p});
  }
  return c.points;
}

}  // namespace fabricad::viewer
