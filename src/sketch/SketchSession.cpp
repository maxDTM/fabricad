// FabriCAD sketch: edit session implementation (sketch mode, rendering, commits).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchSession.h"

#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Log.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/Units.h"
#include "sketch/SketchInference.h"

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace fabricad::sketch {

using core::PreviewStyle;
using core::ShapeKind;
using core::ShapeRef;
using core::Vec3;

namespace {
std::unique_ptr<SketchSession> g_session;

PreviewStyle lineStyle(const std::array<float, 4>& color, float width, bool dashed) {
  PreviewStyle s;
  s.color = color;
  s.lineWidth = width;
  s.shaded = false;
  s.dashed = dashed;
  s.onTop = true;
  return s;
}
PreviewStyle pointStyle(const std::array<float, 4>& color, float size) {
  PreviewStyle s;
  s.color = color;
  s.pointSize = size;
  s.onTop = true;
  return s;
}
PreviewStyle textStyle(const std::array<float, 4>& color, float height) {
  PreviewStyle s;
  s.color = color;
  s.textHeight = height;
  s.onTop = true;
  return s;
}
}  // namespace

SketchSession* SketchSession::current() { return g_session.get(); }

SketchSession* SketchSession::begin(core::ToolContext& ctx, core::ObjectId sketchId) {
  if (!ctx.document) return nullptr;
  auto* f = ctx.document->featureAs<SketchFeature>(sketchId);
  if (!f) return nullptr;
  if (g_session) {
    if (g_session->sketchId_ == sketchId && g_session->doc_ == ctx.document) return g_session.get();
    end();
  }
  g_session.reset(new SketchSession(ctx, sketchId));
  g_session->enter();
  return g_session.get();
}

void SketchSession::end() {
  if (!g_session) return;
  g_session->leave();
  g_session.reset();
}

SketchSession::SketchSession(core::ToolContext& ctx, core::ObjectId sketchId)
    : doc_(ctx.document),
      selection_(ctx.selection),
      undo_(ctx.undo),
      viewer_(ctx.viewer),
      host_(ctx.host),
      manager_(ctx.manager),
      sketchId_(sketchId) {
  if (auto* f = feature()) plane_ = f->plane();
}

SketchSession::~SketchSession() {
  for (auto& c : connections_) QObject::disconnect(c);
}

SketchFeature* SketchSession::feature() const { return doc_ ? doc_->featureAs<SketchFeature>(sketchId_) : nullptr; }
Sketch* SketchSession::sketch() const {
  SketchFeature* f = feature();
  return f ? &f->sketch() : nullptr;
}

void SketchSession::enter() {
  SketchFeature* f = feature();
  if (!f) return;
  // A freshly created sketch may not have been recomputed yet: make sure the plane is known.
  if (f->status() == core::FeatureStatus::Stale && doc_) doc_->recompute();
  plane_ = f->plane();
  if (viewer_) {
    viewer_->enterSketchMode(plane_, true);
    viewer_->setGridVisible(true);
    viewer_->setFeatureVisibilityOverride(sketchId_, false);  // we draw it ourselves
    viewer_->setSelectionFilter(core::kAllKinds);
  }
  if (host_) host_->setContextualTab("SKETCH", true);
  if (manager_) {
    connections_.push_back(QObject::connect(manager_, &core::ToolManager::toolFinished, this,
                                            [this](const std::string& id, bool ok) { onToolFinished(id, ok); }));
  }
  if (doc_) {
    connections_.push_back(QObject::connect(doc_, &core::Document::recomputeFinished, this, [this] {
      if (!rendering_) render();
    }));
    connections_.push_back(QObject::connect(doc_, &core::Document::featureRemoved, this, [this](core::ObjectId id, int) {
      if (id == sketchId_) QTimer::singleShot(0, [] { SketchSession::end(); });
    }));
    connections_.push_back(QObject::connect(doc_, &core::Document::documentReset, this, [] {
      QTimer::singleShot(0, [] { SketchSession::end(); });
    }));
  }
  render();
  ensureIdleTool();
}

void SketchSession::leave() {
  ending_ = true;
  for (auto& c : connections_) QObject::disconnect(c);
  connections_.clear();
  if (manager_ && manager_->isActive()) manager_->cancel();
  if (viewer_) {
    for (core::PreviewHandle h : handles_) viewer_->removePreview(h);
    handles_.clear();
    viewer_->setFeatureVisibilityOverride(sketchId_, std::nullopt);
    viewer_->exitSketchMode();
  }
  if (host_) {
    host_->setContextualTab("SKETCH", false);
    host_->setPrompt({});
  }
  if (selection_) selection_->clear();
}

void SketchSession::onToolFinished(const std::string& id, bool) {
  if (ending_) return;
  // The ToolManager clears all previews when a tool finishes: redraw the sketch and restart
  // the idle tool unless another tool is taking over.
  QTimer::singleShot(0, this, [this] {
    if (ending_ || !g_session || g_session.get() != this) return;
    render();
    ensureIdleTool();
  });
  (void)id;
}

void SketchSession::ensureIdleTool() {
  if (!manager_ || manager_->isActive()) return;
  if (manager_->registry().has("sketch.select")) manager_->run("sketch.select");
}

// ---------------------------------------------------------------------------
// coordinates
// ---------------------------------------------------------------------------

Pt2 SketchSession::toSketch(const Vec3& world) const { return toLocal(plane_, world.toPnt()); }
Vec3 SketchSession::toWorld(Pt2 p) const { return Vec3::from(sketch::toWorld(plane_, p)); }

std::optional<Pt2> SketchSession::cursor(const core::ViewerMouseEvent& e) const {
  if (e.hasPlanePoint) return toSketch(e.planePoint);
  if (viewer_) {
    gp_Pnt p;
    if (viewer_->screenToPlane(e.x, e.y, gp_Pln(plane_), p)) return toLocal(plane_, p);
  }
  if (!e.rayDir.isZero()) {
    // intersect the ray with the plane ourselves
    Vec3 n = Vec3::from(plane_.Direction());
    Vec3 o = Vec3::from(plane_.Location());
    double denom = n.dot(e.rayDir);
    if (std::abs(denom) > 1e-12) {
      double t = n.dot(o - e.rayOrigin) / denom;
      return toSketch(e.rayOrigin + e.rayDir * t);
    }
  }
  return std::nullopt;
}

double SketchSession::tolerance(Pt2 near, double pixels) const {
  double px = viewer_ ? viewer_->pixelSize(sketch::toWorld(plane_, near)) : 1.0;
  return std::max(1e-6, pixels * px);
}

// ---------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------

void SketchSession::beginEdit() {
  SketchFeature* f = feature();
  if (!f) return;
  if (!editing_) before_ = f->toJson();
  editing_ = true;
}

bool SketchSession::commitEdit(const std::string& text) {
  SketchFeature* f = feature();
  if (!f || !doc_) return false;
  if (!editing_) before_ = f->toJson();
  editing_ = false;
  f->sketch().solve();
  QJsonObject after = f->toJson();
  if (after == before_) {
    refresh();
    return false;
  }
  auto cmd = std::make_unique<core::ModifyFeatureCommand>(*doc_, sketchId_, before_, after, text);
  if (undo_) undo_->push(std::move(cmd));
  else cmd->redo();
  if (host_) host_->showMessage(statusText());
  render();
  emit changed();
  return true;
}

void SketchSession::cancelEdit() {
  SketchFeature* f = feature();
  if (!f || !editing_) return;
  editing_ = false;
  f->fromJson(before_);
  f->updateGeometry();
  render();
}

void SketchSession::refresh() {
  SketchFeature* f = feature();
  if (!f) return;
  f->updateGeometry();
  render();
}

bool SketchSession::deleteSelection() {
  Sketch* sk = sketch();
  if (!sk) return false;
  std::vector<GeoRef> refs = selectedRefs();
  std::set<int> cons = selectedConstraints_;
  if (refs.empty() && cons.empty()) return false;
  beginEdit();
  for (int id : cons) sk->removeConstraint(id);
  std::set<int> geos;
  for (const GeoRef& r : refs)
    if (r.pos == PointPos::None || (sk->geometry(r.geo) && sk->geometry(r.geo)->type == GeomType::Point)) geos.insert(r.geo);
  // deleting a point reference removes the constraints on that point
  for (const GeoRef& r : refs) {
    if (geos.count(r.geo)) continue;
    auto& cs = sk->constraints();
    cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Constraint& c) {
               return (c.a == r) || (c.b == r) || (c.c == r);
             }),
             cs.end());
  }
  for (int id : geos) sk->removeGeometry(id);
  selectedConstraints_.clear();
  if (selection_) selection_->clear();
  return commitEdit("Delete");
}

// ---------------------------------------------------------------------------
// selection
// ---------------------------------------------------------------------------

ShapeRef SketchSession::toShapeRef(const GeoRef& r) const {
  if (r.pos == PointPos::None) {
    const Sketch* sk = sketch();
    const Geometry* g = sk ? sk->geometry(r.geo) : nullptr;
    if (g && g->type == GeomType::Point) return ShapeRef::sketchPoint(sketchId_, r.geo, core::SketchPointRole::Standalone);
    return ShapeRef::sketchCurve(sketchId_, r.geo);
  }
  ShapeRef ref = ShapeRef::sketchPoint(sketchId_, r.geo, toRole(r.pos));
  ref.subIndex = toSubIndex(r);
  return ref;
}

std::optional<GeoRef> SketchSession::fromShapeRef(const ShapeRef& ref) const {
  if (ref.owner != sketchId_) return std::nullopt;
  if (ref.kind == ShapeKind::SketchCurve) return GeoRef{ref.index, PointPos::None};
  if (ref.kind == ShapeKind::SketchPoint) {
    const Sketch* sk = sketch();
    const Geometry* g = sk ? sk->geometry(ref.index) : nullptr;
    if (g && g->type == GeomType::Point) return GeoRef{ref.index, PointPos::Center};
    return fromSubIndex(ref.index, ref.subIndex);
  }
  return std::nullopt;
}

std::vector<GeoRef> SketchSession::selectedRefs() const {
  std::vector<GeoRef> out;
  if (!selection_) return out;
  for (const ShapeRef& r : selection_->items())
    if (auto g = fromShapeRef(r)) out.push_back(*g);
  return out;
}

std::vector<int> SketchSession::selectedCurves() const {
  std::vector<int> out;
  for (const GeoRef& r : selectedRefs())
    if (r.pos == PointPos::None && std::find(out.begin(), out.end(), r.geo) == out.end()) out.push_back(r.geo);
  return out;
}

void SketchSession::selectConstraint(int id, bool add) {
  if (!add) selectedConstraints_.clear();
  selectedConstraints_.insert(id);
  render();
}

void SketchSession::clearConstraintSelection() {
  if (selectedConstraints_.empty()) return;
  selectedConstraints_.clear();
  render();
}

std::optional<SketchHit> SketchSession::hitAt(Pt2 p, double tol) const {
  const Sketch* sk = sketch();
  if (!sk) return std::nullopt;
  return sk->hit(p, tol);
}

std::optional<int> SketchSession::dimensionAt(Pt2 p, double tol) const {
  const Sketch* sk = sketch();
  if (!sk) return std::nullopt;
  std::optional<int> best;
  double bestD = tol * 2.5;  // labels are a few characters wide
  for (const Constraint& c : sk->constraints()) {
    if (!c.isDimensional()) continue;
    Pt2 lp = c.hasLabelPos ? c.labelPos : sk->defaultLabelPos(c);
    double d = lp.distance(p);
    if (auto it = textPos_.find(c.id); it != textPos_.end()) d = std::min(d, it->second.distance(p));  // the drawn text
    if (d < bestD) {
      bestD = d;
      best = c.id;
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

void SketchSession::addLines(const std::vector<Vec3>& pts, const PreviewStyle& st) {
  if (!viewer_ || pts.size() < 2) return;
  core::PreviewHandle h = viewer_->showPreviewLines(pts, st);
  if (h != core::kInvalidPreview) handles_.insert(h);
}
void SketchSession::addPolyline(const std::vector<Vec3>& pts, const PreviewStyle& st) {
  if (!viewer_ || pts.size() < 2) return;
  core::PreviewHandle h = viewer_->showPreviewPolyline(pts, st);
  if (h != core::kInvalidPreview) handles_.insert(h);
}
void SketchSession::addPoints(const std::vector<Vec3>& pts, const PreviewStyle& st) {
  if (!viewer_ || pts.empty()) return;
  core::PreviewHandle h = viewer_->showPreviewPoints(pts, st);
  if (h != core::kInvalidPreview) handles_.insert(h);
}
void SketchSession::addText(const Vec3& at, const std::string& text, const PreviewStyle& st) {
  if (!viewer_ || text.empty()) return;
  core::PreviewHandle h = viewer_->showPreviewText(at, text, st);
  if (h != core::kInvalidPreview) handles_.insert(h);
}

void SketchSession::render() {
  if (!viewer_ || ending_) return;
  rendering_ = true;
  for (core::PreviewHandle h : handles_) viewer_->removePreview(h);
  handles_.clear();
  if (feature()) {
    plane_ = feature()->plane();
    if (showProfiles_) renderProfiles();
    renderGeometry();
    if (showConstraints_) renderConstraints();
    if (showDimensions_) renderDimensions();
  }
  viewer_->requestRedraw();
  rendering_ = false;
}

void SketchSession::renderGeometry() {
  SketchFeature* f = feature();
  if (!f) return;
  const Sketch& sk = f->sketch();
  std::set<int> selCurves;
  std::set<std::pair<int, int>> selPoints;
  for (const GeoRef& r : selectedRefs()) {
    if (r.pos == PointPos::None) selCurves.insert(r.geo);
    else selPoints.insert({r.geo, toSubIndex(r)});
  }
  std::vector<Vec3> freePts, fixedPts, selPts;
  for (const Geometry& g : sk.geometries()) {
    GeometryStatus st = sk.statusOf(g.id);
    std::array<float, 4> color = colors::kFree;
    switch (st) {
      case GeometryStatus::Free: color = colors::kFree; break;
      case GeometryStatus::FullyConstrained: color = colors::kConstrained; break;
      case GeometryStatus::Fixed: color = colors::kFixed; break;
      case GeometryStatus::Projected: color = colors::kProjected; break;
      case GeometryStatus::Conflicting: color = colors::kConflict; break;
    }
    bool dashed = false;
    if (g.construction && st != GeometryStatus::Conflicting) {
      color = colors::kConstruction;
      dashed = true;
    }
    bool selected = selCurves.count(g.id) > 0;
    if (selected) color = colors::kSelected;
    if (g.type == GeomType::Point) {
      Vec3 p = toWorld(g.center());
      if (selected) selPts.push_back(p);
      else if (st == GeometryStatus::FullyConstrained || st == GeometryStatus::Fixed) fixedPts.push_back(p);
      else freePts.push_back(p);
      continue;
    }
    for (const auto& pl : g.polylines(72)) {
      std::vector<Vec3> pts;
      for (const Pt2& p : pl) pts.push_back(toWorld(p));
      addPolyline(pts, lineStyle(color, selected ? 3.0f : 2.0f, dashed));
    }
    if (g.isSpline() && g.type == GeomType::Spline) {
      // control polygon (thin dashed), Fusion shows it for control-point splines
      std::vector<Vec3> poly;
      for (int i = 0; i < g.vertexCount(); ++i) poly.push_back(toWorld(g.vertex(i)));
      addPolyline(poly, lineStyle(colors::kGlyph, 1.0f, true));
    }
    if (showPoints_) {
      for (const GeoRef& pr : g.pointRefs()) {
        if (pr.pos == PointPos::Mid) continue;
        Vec3 p = toWorld(g.point(pr.pos, pr.index));
        bool pointSelected = selPoints.count({g.id, toSubIndex(pr)}) > 0;
        if (pointSelected) selPts.push_back(p);
        else if (st == GeometryStatus::FullyConstrained || st == GeometryStatus::Fixed || st == GeometryStatus::Projected) fixedPts.push_back(p);
        else freePts.push_back(p);
      }
    }
  }
  addPoints(freePts, pointStyle(colors::kFree, 6.0f));
  addPoints(fixedPts, pointStyle(colors::kConstrained, 6.0f));
  addPoints(selPts, pointStyle(colors::kSelected, 8.0f));
  // origin point of the sketch (red, Fusion style)
  addPoints({toWorld({0, 0})}, pointStyle({0.85f, 0.15f, 0.15f, 1.0f}, 7.0f));
}

void SketchSession::renderConstraints() {
  const Sketch* sk = sketch();
  if (!sk) return;
  const double off = tolerance({0, 0}, 14.0);
  const double half = tolerance({0, 0}, 6.0);  // glyph box: 12 px
  // glyphs are drawn from line primitives (UX-28), batched per colour
  std::vector<Vec3> glyphSegs, selectedSegs, fixSegs;
  auto glyphAt = [&](Pt2 p, GlyphShape shape, int cid) {
    std::vector<Vec3>& out = selectedConstraints_.count(cid) ? selectedSegs : (shape == GlyphShape::Fix ? fixSegs : glyphSegs);
    for (const Pt2& q : glyphSegments(shape)) out.push_back(toWorld(p + q * half));
  };
  auto curveLabelPos = [&](int geo, double along) -> Pt2 {
    const Geometry* g = sk->geometry(geo);
    if (!g) return {};
    if (g->type == GeomType::Line) {
      Pt2 p = g->start() + (g->end() - g->start()) * along;
      return p + g->direction().perp() * off;
    }
    if (g->isCircular()) {
      double a = g->startAngle() + g->sweep() * along;
      return g->center() + Pt2::polar(g->radius() + off, a);
    }
    return g->center() + Pt2{off, off};
  };
  std::vector<Vec3> coincidentPts;
  for (const Constraint& c : sk->constraints()) {
    if (c.isDimensional()) continue;
    GlyphShape shape = GlyphShape::None;
    switch (c.type) {
      case ConstraintType::Coincident:
      case ConstraintType::PointOnCurve: {
        if (c.internal && c.type == ConstraintType::PointOnCurve) break;
        auto p = sk->pointOf(c.a);
        if (p) coincidentPts.push_back(toWorld(*p));
        break;
      }
      case ConstraintType::Horizontal: shape = GlyphShape::Horizontal; break;
      case ConstraintType::Vertical: shape = GlyphShape::Vertical; break;
      case ConstraintType::Parallel: shape = GlyphShape::Parallel; break;
      case ConstraintType::Perpendicular: shape = GlyphShape::Perpendicular; break;
      case ConstraintType::Tangent: shape = GlyphShape::Tangent; break;
      case ConstraintType::Equal: shape = GlyphShape::Equal; break;
      case ConstraintType::Concentric: shape = GlyphShape::Concentric; break;
      case ConstraintType::Collinear: shape = GlyphShape::Collinear; break;
      case ConstraintType::Midpoint: shape = GlyphShape::Midpoint; break;
      case ConstraintType::Fix:
        if (isOriginFix(c)) {
          if (auto p = sk->pointOf(c.a)) coincidentPts.push_back(toWorld(*p));
          break;
        }
        shape = GlyphShape::Fix;
        break;
      case ConstraintType::Symmetric: shape = GlyphShape::Symmetric; break;
      default: break;
    }
    if (shape == GlyphShape::None || c.internal) continue;
    if (c.a.isPoint() && c.b.isPoint()) {
      auto pa = sk->pointOf(c.a), pb = sk->pointOf(c.b);
      if (pa && pb) glyphAt((*pa + *pb) * 0.5 + Pt2{off, off}, shape, c.id);
      else if (pa) glyphAt(*pa + Pt2{off, off}, shape, c.id);
      continue;
    }
    if (c.a.isPoint()) {
      auto pa = sk->pointOf(c.a);
      if (pa) glyphAt(*pa + Pt2{off, off}, shape, c.id);
      if (c.b.isValid() && !c.b.isPoint() && c.type != ConstraintType::Midpoint) glyphAt(curveLabelPos(c.b.geo, 0.5), shape, c.id);
      continue;
    }
    glyphAt(curveLabelPos(c.a.geo, 0.35), shape, c.id);
    if (c.b.isValid() && !c.b.isPoint()) glyphAt(curveLabelPos(c.b.geo, 0.35), shape, c.id);
  }
  addLines(glyphSegs, lineStyle(colors::kGlyph, 1.0f, false));
  addLines(fixSegs, lineStyle(colors::kFixed, 1.5f, false));
  addLines(selectedSegs, lineStyle(colors::kSelected, 1.5f, false));
  addPoints(coincidentPts, pointStyle(colors::kGlyph, 4.0f));
}

std::string SketchSession::dimensionText(const Constraint& c) const {
  core::UnitSystem units = doc_ ? doc_->units() : core::UnitSystem{};
  const Sketch* sk = sketch();
  double v = c.value;
  if (c.driven && sk) {
    if (auto m = sk->measure(c)) v = *m;
  }
  std::string s;
  if (c.type == ConstraintType::Angle) {
    s = core::formatQuantity(core::Quantity::deg(v), units, false) + "°";
  } else {
    s = core::formatQuantity(core::Quantity::mm(v), units, false);
    if (c.type == ConstraintType::Radius) s = "R" + s;
    else if (c.type == ConstraintType::Diameter) s = "⌀" + s;
  }
  if (!c.expression.empty()) s = "fx " + s;
  if (c.driven) s = "(" + s + ")";
  return s;
}

void SketchSession::renderDimensions() {
  const Sketch* sk = sketch();
  if (!sk) return;
  for (const Constraint& c : sk->constraints())
    if (c.isDimensional()) drawDimension(c);
}

void SketchSession::drawDimension(const Constraint& c) {
  const Sketch* sk = sketch();
  if (!sk) return;
  const Geometry* ga = sk->geometry(c.a.geo);
  if (!ga) return;
  const Geometry* gb = c.b.isValid() ? sk->geometry(c.b.geo) : nullptr;
  bool selected = selectedConstraints_.count(c.id) > 0;
  PreviewStyle ls = lineStyle(selected ? colors::kSelected : colors::kDimension, 1.0f, false);
  PreviewStyle ts = textStyle(selected ? colors::kSelected : colors::kDimension, 12.0f);
  Pt2 label = c.hasLabelPos ? c.labelPos : sk->defaultLabelPos(c);
  std::vector<Vec3> segs;
  auto seg = [&](Pt2 a, Pt2 b) {
    segs.push_back(toWorld(a));
    segs.push_back(toWorld(b));
  };
  const double tick = tolerance(label, 4.0);
  // the text sits beside its dimension line, not on it (the line would strike it through, UX-28)
  const double textGap = tolerance(label, 9.0);
  Pt2 textShift{0, textGap};
  auto arrow = [&](Pt2 tip, Pt2 dir) {
    Pt2 d = dir.normalized();
    Pt2 n = d.perp();
    seg(tip, tip - d * (tick * 2) + n * tick);
    seg(tip, tip - d * (tick * 2) - n * tick);
  };
  switch (c.type) {
    case ConstraintType::Radius:
    case ConstraintType::Diameter: {
      Pt2 cen = ga->center();
      Pt2 dir = (label - cen);
      if (dir.length() < 1e-9) dir = {1, 0};
      dir = dir.normalized();
      Pt2 onCircle = cen + dir * ga->radius();
      textShift = dir.perp() * (dir.perp().y >= 0 ? textGap : -textGap);
      if (c.type == ConstraintType::Radius) {
        seg(cen, label);
        arrow(onCircle, label.distance(cen) > ga->radius() ? -dir : dir);
      } else {
        Pt2 other = cen - dir * ga->radius();
        seg(other, label.distance(cen) > ga->radius() ? label : onCircle);
        arrow(onCircle, dir);
        arrow(other, -dir);
      }
      break;
    }
    case ConstraintType::Angle: {
      if (gb && gb->type == GeomType::Line) {
        auto x = intersectLines(ga->start(), ga->direction(), gb->start(), gb->direction());
        if (!x) break;
        double r = label.distance(*x);
        if (r > 1e-12) textShift = (label - *x).normalized() * textGap;  // outside the arc
        double a0 = (ga->midpoint() - *x).angle();
        double a1 = (gb->midpoint() - *x).angle();
        double sweep = angleDelta(a0, a1);
        std::vector<Vec3> arc;
        int n = 16;
        for (int i = 0; i <= n; ++i) arc.push_back(toWorld(*x + Pt2::polar(r, a0 + sweep * i / n)));
        addPolyline(arc, ls);
        seg(*x, *x + Pt2::polar(r, a0));
        seg(*x, *x + Pt2::polar(r, a1));
      } else {
        seg(ga->start(), label);
      }
      break;
    }
    default: {
      // linear: two measured points (or point + foot on line)
      std::optional<Pt2> pa, pb;
      Pt2 dir{1, 0};
      if (c.a.isPoint() && c.b.isPoint()) {
        pa = sk->pointOf(c.a);
        pb = sk->pointOf(c.b);
      } else if (c.a.isPoint() && gb && gb->type == GeomType::Line) {
        pa = sk->pointOf(c.a);
        if (pa) {
          double t = projectParam(gb->start(), gb->end() - gb->start(), *pa);
          pb = gb->start() + (gb->end() - gb->start()) * t;
        }
      } else if (c.b.isPoint() && ga->type == GeomType::Line) {
        pb = sk->pointOf(c.b);
        if (pb) {
          double t = projectParam(ga->start(), ga->end() - ga->start(), *pb);
          pa = ga->start() + (ga->end() - ga->start()) * t;
        }
      } else if (ga->type == GeomType::Line && !gb) {
        pa = ga->start();
        pb = ga->end();
      } else if (ga->type == GeomType::Line && gb && gb->type == GeomType::Line) {
        pa = gb->start();
        double t = projectParam(ga->start(), ga->end() - ga->start(), *pa);
        pb = ga->start() + (ga->end() - ga->start()) * t;
      } else if (c.a.isPoint() && !c.b.isValid()) {
        pa = Pt2{0, 0};
        pb = sk->pointOf(c.a);
      }
      if (!pa || !pb) break;
      if (c.type == ConstraintType::DistanceX) dir = {1, 0};
      else if (c.type == ConstraintType::DistanceY) dir = {0, 1};
      else {
        dir = (*pb - *pa);
        if (dir.length() < 1e-9) dir = {1, 0};
        dir = dir.normalized();
      }
      // dimension line through the label, parallel to dir
      Pt2 n = dir.perp();
      double offset = (label - *pa).dot(n);
      Pt2 a2 = *pa + n * offset;
      Pt2 b2 = *pb + n * offset;
      if (c.type == ConstraintType::DistanceX || c.type == ConstraintType::DistanceY) {
        a2 = *pa + n * offset;
        b2 = a2 + dir * ((*pb - *pa).dot(dir));
      }
      seg(*pa, a2 + n * (offset >= 0 ? tick : -tick));
      seg(*pb, b2 + n * (offset >= 0 ? tick : -tick));
      seg(a2, b2);
      arrow(a2, a2 - b2);
      arrow(b2, b2 - a2);
      // away from the measured geometry
      textShift = n * (offset >= 0 ? textGap : -textGap);
      break;
    }
  }
  addLines(segs, ls);
  textPos_[c.id] = label + textShift;
  addText(toWorld(label + textShift), dimensionText(c), ts);
}

void SketchSession::renderProfiles() {
  SketchFeature* f = feature();
  if (!f) return;
  PreviewStyle st;
  st.color = colors::kProfile;
  st.shaded = true;
  for (const ProfileRegion& r : f->regions()) {
    if (!viewer_) break;
    core::PreviewHandle h = viewer_->showPreviewShape(r.face, st);
    if (h != core::kInvalidPreview) handles_.insert(h);
  }
}

std::string SketchSession::statusText() const {
  SketchFeature* f = feature();
  if (!f) return {};
  return std::format("{}: {}", f->name(), f->statusText());
}

}  // namespace fabricad::sketch
