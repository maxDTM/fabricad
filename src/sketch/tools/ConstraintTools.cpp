// FabriCAD sketch tools: the CONSTRAINTS group. One generic tool per constraint type that
// collects the required entities (preselection first, then clicks) and applies the constraint
// when the set is complete; it stays active for the next one until Esc.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/tools/SketchToolBase.h"
#include "sketch/tools/SketchTools.h"

#include "core/Selection.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace fabricad::sketch {

using core::ToolContext;
using core::ToolInfo;
using core::ViewerMouseEvent;

namespace {

enum class ConstraintKind {
  HorizontalVertical,
  Coincident,
  Tangent,
  Equal,
  Parallel,
  Perpendicular,
  Fix,
  Midpoint,
  Concentric,
  Collinear,
  Symmetric,
};

struct KindInfo {
  ConstraintKind kind;
  const char* id;
  const char* name;
  const char* tooltip;
  const char* alias;
  int order;
};

const KindInfo kKinds[] = {
    {ConstraintKind::HorizontalVertical, "sketch.constraint.horizontal-vertical", "Horizontal/Vertical",
     "Make a line (or two points) horizontal or vertical, whichever is closer", "HV", 1},
    {ConstraintKind::Coincident, "sketch.constraint.coincident", "Coincident", "Make two points coincident, or a point lie on a curve", "COINCIDENT", 2},
    {ConstraintKind::Tangent, "sketch.constraint.tangent", "Tangent", "Make two curves tangent", "TANGENT", 3},
    {ConstraintKind::Equal, "sketch.constraint.equal", "Equal", "Equal length (lines) or equal radius (circles/arcs)", "EQUAL", 4},
    {ConstraintKind::Parallel, "sketch.constraint.parallel", "Parallel", "Make two lines parallel", "PARALLEL", 5},
    {ConstraintKind::Perpendicular, "sketch.constraint.perpendicular", "Perpendicular", "Make two lines perpendicular", "PERPENDICULAR", 6},
    {ConstraintKind::Fix, "sketch.constraint.fix", "Fix/UnFix", "Lock or unlock a point or curve in place", "FIX", 7},
    {ConstraintKind::Midpoint, "sketch.constraint.midpoint", "Midpoint", "Put a point at the midpoint of a line or arc", "MIDPOINT", 8},
    {ConstraintKind::Concentric, "sketch.constraint.concentric", "Concentric", "Share the centre of circles and arcs", "CONCENTRIC", 9},
    {ConstraintKind::Collinear, "sketch.constraint.collinear", "Collinear", "Put two lines on the same infinite line", "COLLINEAR", 10},
    {ConstraintKind::Symmetric, "sketch.constraint.symmetric", "Symmetry", "Make two points symmetric about a line (pick the line last)", "SYMMETRIC", 11},
};

class ConstraintTool : public SketchTool {
 public:
  explicit ConstraintTool(const KindInfo& k)
      : SketchTool(ToolInfo{.id = k.id,
                            .name = k.name,
                            .tooltip = k.tooltip,
                            .icon = k.id,
                            .aliases = {k.alias},
                            .tab = "SKETCH",
                            .group = "CONSTRAINTS",
                            .order = k.order,
                            .promoted = true}),
        kind_(k.kind) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    picks_.clear();
    if (session()) {
      for (const GeoRef& r : session()->selectedRefs()) addPick(ctx, normalize(r), false);
      if (ctx.selection) ctx.selection->clear();
      session()->render();
    }
    tryApply(ctx, true);
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c) return false;
    clearTemp();
    const Sketch* sk = sketch();
    if (!sk) return true;
    for (const GeoRef& r : picks_) {
      if (const Geometry* g = sk->geometry(r.geo)) {
        if (r.pos == PointPos::None || g->type == GeomType::Point) previewGeometry(*g, highlightStyle());
        else if (auto p = sk->pointOf(r)) previewPoints({*p}, markerStyle());
      }
    }
    if (auto hit = pickAt(*c)) {
      const Geometry* g = sk->geometry(hit->geo);
      if (g && (hit->pos == PointPos::None || g->type == GeomType::Point)) previewGeometry(*g, highlightStyle());
      else if (auto p = sk->pointOf(*hit)) previewPoints({*p}, markerStyle());
    }
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c) return true;
    if (auto hit = pickAt(*c)) {
      addPick(ctx, *hit, true);
      tryApply(ctx, false);
      // the picks were used: drop their highlight now, else the old orange curve stays drawn next
      // to the solved one until the mouse moves (functional retest SK-02)
      if (picks_.empty()) clearTemp();
      ctx.setPrompt(prompt());
    }
    return true;
  }

  /// Enter applies what has been collected so far (Equal/Concentric with 2+ picks).
  void accept(ToolContext& ctx) override {
    if (!tryApply(ctx, true)) ctx.finish();
    else {
      clearTemp();
      ctx.setPrompt(prompt());
    }
  }

 private:
  bool wantsPointsFirst() const {
    return kind_ == ConstraintKind::Coincident || kind_ == ConstraintKind::Midpoint || kind_ == ConstraintKind::Symmetric ||
           kind_ == ConstraintKind::Fix || kind_ == ConstraintKind::HorizontalVertical;
  }

  /// Standalone points are referenced as their centre.
  GeoRef normalize(GeoRef r) const {
    const Sketch* sk = sketch();
    const Geometry* g = sk ? sk->geometry(r.geo) : nullptr;
    if (g && g->type == GeomType::Point) r.pos = PointPos::Center;
    return r;
  }

  std::optional<GeoRef> pickAt(Pt2 p) const {
    const Sketch* sk = sketch();
    if (!sk) return std::nullopt;
    const double t = tol(p);
    if (kind_ == ConstraintKind::Symmetric && picks_.size() == 2) {
      if (auto h = sk->hitCurve(p, t)) return h->ref;
      return std::nullopt;
    }
    if (wantsPointsFirst()) {
      auto h = sk->hit(p, t);
      // Horizontal/Vertical and Fix: a click near the middle of a curve means the curve
      if ((kind_ == ConstraintKind::HorizontalVertical || kind_ == ConstraintKind::Fix) && h && h->ref.pos == PointPos::Mid)
        h = sk->hitCurve(p, t);
      if (h) return normalize(h->ref);
      return std::nullopt;
    }
    if (auto h = sk->hitCurve(p, t)) return h->ref;
    // allow picking a point geometry (not a curve's point) for curve-based constraints such as Fix
    if (auto h = sk->hitPoint(p, t)) {
      const Geometry* g = sk->geometry(h->ref.geo);
      if (g && g->type == GeomType::Point) return normalize(h->ref);
    }
    return std::nullopt;
  }

  void addPick(ToolContext& ctx, const GeoRef& r, bool interactive) {
    const Sketch* sk = sketch();
    const Geometry* g = sk ? sk->geometry(r.geo) : nullptr;
    if (!g) return;
    if (std::find(picks_.begin(), picks_.end(), r) != picks_.end()) return;
    if (!accepts(r, *g)) {
      if (interactive) ctx.showMessage(std::format("{} cannot use that entity", info().name), true);
      return;
    }
    picks_.push_back(r);
  }

  bool isPointRef(const GeoRef& r, const Geometry& g) const { return g.type == GeomType::Point || r.pos != PointPos::None; }

  bool accepts(const GeoRef& r, const Geometry& g) const {
    const bool pt = isPointRef(r, g);
    switch (kind_) {
      case ConstraintKind::HorizontalVertical: return pt || g.type == GeomType::Line;
      case ConstraintKind::Coincident: return pt || g.isCurve();
      case ConstraintKind::Tangent: return !pt && g.isCurve();
      case ConstraintKind::Equal: return !pt && g.isCurve();
      case ConstraintKind::Parallel:
      case ConstraintKind::Perpendicular:
      case ConstraintKind::Collinear: return !pt && g.type == GeomType::Line;
      case ConstraintKind::Fix: return true;
      case ConstraintKind::Midpoint: return pt || g.type == GeomType::Line || g.type == GeomType::Arc;
      case ConstraintKind::Concentric: return !pt && g.isCircular();
      case ConstraintKind::Symmetric: return pt || g.type == GeomType::Line;
    }
    return false;
  }

  std::string prompt() const {
    switch (kind_) {
      case ConstraintKind::HorizontalVertical: return picks_.empty() ? "Select a line, or two points" : "Select the second point";
      case ConstraintKind::Coincident: return picks_.empty() ? "Select a point" : "Select a point or curve";
      case ConstraintKind::Tangent: return picks_.empty() ? "Select the first curve" : "Select the second curve";
      case ConstraintKind::Equal: return picks_.empty() ? "Select curves to make equal" : "Select another curve (Enter to apply)";
      case ConstraintKind::Parallel: return picks_.empty() ? "Select the first line" : "Select the second line";
      case ConstraintKind::Perpendicular: return picks_.empty() ? "Select the first line" : "Select the second line";
      case ConstraintKind::Collinear: return picks_.empty() ? "Select the first line" : "Select the second line";
      case ConstraintKind::Fix: return "Select points or curves to fix / unfix";
      case ConstraintKind::Midpoint: return picks_.empty() ? "Select a point" : "Select the line or arc";
      case ConstraintKind::Concentric: return picks_.empty() ? "Select circles or arcs" : "Select another circle or arc (Enter to apply)";
      case ConstraintKind::Symmetric:
        if (picks_.empty()) return "Select the first point";
        if (picks_.size() == 1) return "Select the second point";
        return "Select the symmetry line";
    }
    return {};
  }

  /// Apply when the collected picks are sufficient. `flush` also applies partial multi-sets.
  bool tryApply(ToolContext& ctx, bool flush) {
    Sketch* sk = sketch();
    if (!sk || !session() || picks_.empty()) return false;
    std::vector<Constraint> toAdd;
    bool done = false;
    auto geo = [&](const GeoRef& r) { return sk->geometry(r.geo); };
    using CT = ConstraintType;
    switch (kind_) {
      case ConstraintKind::HorizontalVertical: {
        const Geometry* g0 = geo(picks_[0]);
        if (!isPointRef(picks_[0], *g0)) {
          Pt2 d = g0->direction();
          toAdd.push_back(Constraint{.type = std::abs(d.x) >= std::abs(d.y) ? CT::Horizontal : CT::Vertical, .a = {picks_[0].geo}});
          done = true;
        } else if (picks_.size() >= 2 && isPointRef(picks_[1], *geo(picks_[1]))) {
          auto a = sk->pointOf(picks_[0]), b = sk->pointOf(picks_[1]);
          if (a && b) {
            Pt2 d = *b - *a;
            toAdd.push_back(Constraint{.type = std::abs(d.x) >= std::abs(d.y) ? CT::Horizontal : CT::Vertical, .a = picks_[0], .b = picks_[1]});
          }
          done = true;
        } else if (picks_.size() >= 2) {
          // point then line: the line wins
          Pt2 d = geo(picks_[1])->direction();
          toAdd.push_back(Constraint{.type = std::abs(d.x) >= std::abs(d.y) ? CT::Horizontal : CT::Vertical, .a = {picks_[1].geo}});
          done = true;
        }
        break;
      }
      case ConstraintKind::Coincident: {
        if (picks_.size() < 2) break;
        const Geometry* g0 = geo(picks_[0]);
        const Geometry* g1 = geo(picks_[1]);
        bool p0 = isPointRef(picks_[0], *g0), p1 = isPointRef(picks_[1], *g1);
        if (p0 && p1) toAdd.push_back(Constraint{.type = CT::Coincident, .a = picks_[0], .b = picks_[1]});
        else if (p0) toAdd.push_back(Constraint{.type = CT::PointOnCurve, .a = picks_[0], .b = {picks_[1].geo}});
        else if (p1) toAdd.push_back(Constraint{.type = CT::PointOnCurve, .a = picks_[1], .b = {picks_[0].geo}});
        else ctx.showMessage("Coincident needs at least one point", true);
        done = true;
        break;
      }
      case ConstraintKind::Tangent: {
        if (picks_.size() < 2) break;
        GeoRef a = picks_[0], b = picks_[1];
        const Geometry* ga = geo(a);
        const Geometry* gb = geo(b);
        // shared end points -> tangent at that point (smooth join)
        bool found = false;
        if (ga->type != GeomType::Circle && gb->type != GeomType::Circle) {
          for (PointPos pa : {PointPos::Start, PointPos::End})
            for (PointPos pb : {PointPos::Start, PointPos::End})
              if (!found && sk->areCoincident({a.geo, pa}, {b.geo, pb})) {
                toAdd.push_back(Constraint{.type = CT::Tangent, .a = {a.geo, pa}, .b = {b.geo, pb}});
                found = true;
              }
        }
        if (!found) {
          if (ga->type == GeomType::Line && gb->type == GeomType::Line) ctx.showMessage("Tangent needs a circle or arc", true);
          else toAdd.push_back(Constraint{.type = CT::Tangent, .a = {a.geo}, .b = {b.geo}});
        }
        done = true;
        break;
      }
      case ConstraintKind::Equal:
      case ConstraintKind::Concentric: {
        if (picks_.size() < 2 || (!flush && picks_.size() < 2)) break;
        const Geometry* g0 = geo(picks_[0]);
        for (size_t i = 1; i < picks_.size(); ++i) {
          const Geometry* gi = geo(picks_[i]);
          if (kind_ == ConstraintKind::Equal && (g0->type == GeomType::Line) != (gi->type == GeomType::Line)) {
            ctx.showMessage("Equal needs two lines or two circles/arcs", true);
            continue;
          }
          toAdd.push_back(Constraint{.type = kind_ == ConstraintKind::Equal ? CT::Equal : CT::Concentric, .a = {picks_[0].geo}, .b = {picks_[i].geo}});
        }
        // interactive clicks apply pairwise right away; a flush applies everything collected
        done = true;
        break;
      }
      case ConstraintKind::Parallel:
      case ConstraintKind::Perpendicular:
      case ConstraintKind::Collinear: {
        if (picks_.size() < 2) break;
        CT t = kind_ == ConstraintKind::Parallel ? CT::Parallel : kind_ == ConstraintKind::Perpendicular ? CT::Perpendicular : CT::Collinear;
        toAdd.push_back(Constraint{.type = t, .a = {picks_[0].geo}, .b = {picks_[1].geo}});
        done = true;
        break;
      }
      case ConstraintKind::Fix: {
        session()->beginEdit();
        int changed = 0;
        for (const GeoRef& r : picks_) {
          const Geometry* g = geo(r);
          GeoRef target = isPointRef(r, *g) ? r : GeoRef{r.geo, PointPos::None};
          if (g->type == GeomType::Point) target = {r.geo, PointPos::None};
          if (const Constraint* ex = sk->findConstraint(CT::Fix, target)) sk->removeConstraint(ex->id);
          else sk->addConstraint(Constraint{.type = CT::Fix, .a = target});
          ++changed;
        }
        picks_.clear();
        if (changed) session()->commitEdit("Fix");
        else session()->cancelEdit();
        return changed > 0;
      }
      case ConstraintKind::Midpoint: {
        if (picks_.size() < 2) break;
        GeoRef p = picks_[0], c = picks_[1];
        if (!isPointRef(p, *geo(p))) std::swap(p, c);
        if (!isPointRef(p, *geo(p)) || isPointRef(c, *geo(c))) ctx.showMessage("Midpoint needs a point and a line or arc", true);
        else toAdd.push_back(Constraint{.type = CT::Midpoint, .a = p, .b = {c.geo}});
        done = true;
        break;
      }
      case ConstraintKind::Symmetric: {
        if (picks_.size() < 3) break;
        GeoRef a = picks_[0], b = picks_[1], l = picks_[2];
        if (!isPointRef(a, *geo(a)) || !isPointRef(b, *geo(b))) ctx.showMessage("Symmetry needs two points and a line", true);
        else toAdd.push_back(Constraint{.type = CT::Symmetric, .a = a, .b = b, .c = isPointRef(l, *geo(l)) ? l : GeoRef{l.geo, PointPos::None}});
        done = true;
        break;
      }
    }
    if (!done) return false;
    int added = 0;
    if (!toAdd.empty()) {
      session()->beginEdit();
      for (Constraint& c : toAdd)
        if (addCheckedConstraint(ctx, c) >= 0) ++added;
      if (added) {
        session()->commitEdit(std::string("Constraint: ") + info().name);
        ctx.showMessage(added > 1 ? "Constraints added" : "Constraint added");
      } else {
        session()->cancelEdit();
      }
    }
    picks_.clear();
    return added > 0;
  }

  ConstraintKind kind_;
  std::vector<GeoRef> picks_;
};

}  // namespace

void registerConstraintTools(core::ToolRegistry& reg) {
  for (const KindInfo& k : kKinds) reg.registerTool(std::make_unique<ConstraintTool>(k));
}

}  // namespace fabricad::sketch
