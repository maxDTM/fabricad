// FabriCAD sketch tools: Move/Copy, Rotate, Scale, Rectangular Pattern, Circular Pattern
// (SKETCH > CREATE / MODIFY) and Sketch Chamfer (MODIFY). Select the entities (or preselect),
// Enter, then pick base points / type values like AutoCAD; previews follow the cursor.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchTransform.h"
#include "sketch/tools/SketchToolBase.h"
#include "sketch/tools/SketchTools.h"

#include "core/Document.h"
#include "core/Selection.h"
#include "core/Units.h"

#include <QtCore/Qt>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace fabricad::sketch {

using core::ToolContext;
using core::ToolInfo;
using core::ViewerKeyEvent;
using core::ViewerMouseEvent;

namespace {

double lengthFactorOf(const core::Document* doc) {
  core::UnitSystem u = doc ? doc->units() : core::UnitSystem{};
  return core::lengthUnitToMm(u.length);
}

bool hasArg(const ToolContext& ctx, const char* a) {
  return std::find(ctx.args.begin(), ctx.args.end(), std::string(a)) != ctx.args.end();
}

/// Numeric arguments of the tool (non-numeric ones such as "copy" are skipped).
std::vector<double> numericArgs(const ToolContext& ctx) {
  std::vector<double> out;
  for (const auto& a : ctx.args) {
    TypedInput in = parseTypedInput(a);
    if (in.kind == TypedInput::Kind::Number) out.push_back(in.a);
  }
  return out;
}

/// Shared first stage: pick the entities (preselection, clicks toggle), Enter continues.
class SelectFirstTool : public SketchTool {
 public:
  using SketchTool::SketchTool;

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    ids_.clear();
    if (session() && sketch()) {
      for (const GeoRef& r : session()->selectedRefs()) {
        const Geometry* g = sketch()->geometry(r.geo);
        if (!g) continue;
        if ((r.pos == PointPos::None || g->type == GeomType::Point) && std::find(ids_.begin(), ids_.end(), r.geo) == ids_.end())
          ids_.push_back(r.geo);
      }
      if (ctx.selection) ctx.selection->clear();
    }
    selecting_ = ids_.empty();
    started(ctx);
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    for (int id : ids_)
      if (const Geometry* g = sk->geometry(id)) previewGeometry(*g, highlightStyle());
    if (selecting_) {
      if (auto h = hitAt(*c))
        if (const Geometry* g = sk->geometry(h->ref.geo)) previewGeometry(*g, highlightStyle());
      return true;
    }
    stageMove(ctx, *c, e);
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return true;
    if (selecting_) {
      if (auto h = hitAt(*c)) {
        auto it = std::find(ids_.begin(), ids_.end(), h->ref.geo);
        if (it == ids_.end()) ids_.push_back(h->ref.geo);
        else ids_.erase(it);
      }
      ctx.setPrompt(prompt());
      return true;
    }
    stageClick(ctx, *c, e);
    ctx.setPrompt(prompt());
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    if (selecting_) return false;
    bool r = stageTyped(ctx, text);
    ctx.setPrompt(prompt());
    return r;
  }

  void accept(ToolContext& ctx) override {
    if (selecting_) {
      if (ids_.empty()) {
        ctx.finish();
        return;
      }
      selecting_ = false;
      selectionDone(ctx);
      ctx.setPrompt(prompt());
      return;
    }
    if (!stageAccept(ctx)) ctx.finish();
  }

 protected:
  virtual std::string stagePrompt() const = 0;
  virtual void started(ToolContext&) {}
  virtual void selectionDone(ToolContext&) {}
  virtual void stageMove(ToolContext&, Pt2, const ViewerMouseEvent&) {}
  virtual void stageClick(ToolContext&, Pt2, const ViewerMouseEvent&) {}
  virtual bool stageTyped(ToolContext&, const std::string&) { return false; }
  /// Enter after the selection stage; return false to finish the tool.
  virtual bool stageAccept(ToolContext&) { return false; }

  std::string prompt() const {
    if (selecting_) return ids_.empty() ? "Select sketch entities" : std::format("{} selected: select more or press Enter", ids_.size());
    return stagePrompt();
  }

  /// Point snapped to existing sketch points (base points, centres).
  Pt2 snap(Pt2 c, const ViewerMouseEvent& e) const {
    InferenceInput in;
    in.cursor = c;
    InferenceResult r = infer(in, snapSuppressed(e));
    return r.onPoint ? r.point : c;
  }

  std::optional<Pt2> typedPoint(const std::string& text, std::optional<Pt2> relativeTo) const {
    TypedInput in = parseTypedInput(text);
    double f = lengthFactorOf(document());
    if (in.kind == TypedInput::Kind::Pair) return Pt2{in.a * f, in.b * f};
    if (relativeTo && in.kind == TypedInput::Kind::Relative) return *relativeTo + Pt2{in.a * f, in.b * f};
    if (relativeTo && in.kind == TypedInput::Kind::Polar) return *relativeTo + Pt2::polar(in.a * f, in.b * kPi / 180.0);
    return std::nullopt;
  }

  /// Preview the result of an operation on a scratch copy of the sketch.
  template <typename Fn>
  void previewOn(Fn&& op) {
    Sketch* sk = sketch();
    if (!sk) return;
    Sketch copy = *sk;
    for (int id : op(copy))
      if (const Geometry* g = copy.geometry(id)) previewGeometry(*g, rubberStyle());
  }

  /// Apply an operation as one undo step.
  template <typename Fn>
  bool commit(ToolContext& ctx, const std::string& text, Fn&& op) {
    Sketch* sk = sketch();
    if (!sk || !session()) return false;
    session()->beginEdit();
    std::vector<int> out = op(*sk);
    if (out.empty()) {
      session()->cancelEdit();
      ctx.showMessage("Nothing changed", true);
      return false;
    }
    session()->commitEdit(text);
    clearTemp();
    return true;
  }

  std::vector<int> ids_;
  bool selecting_ = true;
};

// ---------------------------------------------------------------------------
// Move / Copy
// ---------------------------------------------------------------------------

class MoveTool : public SelectFirstTool {
 public:
  MoveTool()
      : SelectFirstTool(ToolInfo{.id = "sketch.move",
                                 .name = "Move/Copy",
                                 .tooltip = "Move or copy sketch entities: select, Enter, base point, target point (C toggles copy; "
                                            "argument \"copy\")",
                                 .icon = "sketch-move",
                                 .aliases = {"SMOVE", "SKMOVE", "SCOPY"},
                                 .tab = "SKETCH",
                                 .group = "MODIFY",
                                 .order = 20}) {}

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_C) {
      copy_ = !copy_;
      ctx.setPrompt(prompt());
      return true;
    }
    return SelectFirstTool::keyPress(ctx, e);
  }

 protected:
  void started(ToolContext& ctx) override {
    copy_ = hasArg(ctx, "copy");
    base_.reset();
  }
  std::string stagePrompt() const override {
    std::string mode = copy_ ? " [copy]" : "";
    return base_ ? "Specify the target point (or @dx,dy)" + mode : "Specify the base point" + mode;
  }
  void stageMove(ToolContext&, Pt2 c, const ViewerMouseEvent& e) override {
    if (!base_) return;
    Pt2 t = snap(c, e);
    previewLines({*base_, t}, guideStyle());
    previewOn([&](Sketch& s) { return transformEntities(s, ids_, SketchTransform::translate(t - *base_), true); });
    showDynamicText(t, formatLength(t.distance(*base_)));
  }
  void stageClick(ToolContext& ctx, Pt2 c, const ViewerMouseEvent& e) override { point(ctx, snap(c, e)); }
  bool stageTyped(ToolContext& ctx, const std::string& text) override {
    auto p = typedPoint(text, base_);
    if (!p) return false;
    point(ctx, *p);
    return true;
  }

 private:
  void point(ToolContext& ctx, Pt2 p) {
    if (!base_) {
      base_ = p;
      return;
    }
    Pt2 d = p - *base_;
    if (commit(ctx, copy_ ? "Copy" : "Move", [&](Sketch& s) { return transformEntities(s, ids_, SketchTransform::translate(d), copy_); }))
      ctx.finish();
  }
  bool copy_ = false;
  std::optional<Pt2> base_;
};

// ---------------------------------------------------------------------------
// Rotate
// ---------------------------------------------------------------------------

class RotateTool : public SelectFirstTool {
 public:
  RotateTool()
      : SelectFirstTool(ToolInfo{.id = "sketch.rotate",
                                 .name = "Rotate",
                                 .tooltip = "Rotate (or copy rotated) sketch entities: select, Enter, centre, then click or type the "
                                            "angle in degrees",
                                 .icon = "sketch-rotate",
                                 .aliases = {"SROTATE", "SKROTATE"},
                                 .tab = "SKETCH",
                                 .group = "MODIFY",
                                 .order = 21}) {}

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_C) {
      copy_ = !copy_;
      ctx.setPrompt(prompt());
      return true;
    }
    return SelectFirstTool::keyPress(ctx, e);
  }

 protected:
  void started(ToolContext& ctx) override {
    copy_ = hasArg(ctx, "copy");
    center_.reset();
  }
  std::string stagePrompt() const override {
    return center_ ? "Specify the rotation angle (click or type degrees)" : "Specify the centre of rotation";
  }
  void stageMove(ToolContext&, Pt2 c, const ViewerMouseEvent&) override {
    if (!center_ || c.distance(*center_) < 1e-9) return;
    double a = (c - *center_).angle();
    previewLines({*center_, c}, guideStyle());
    previewOn([&](Sketch& s) { return transformEntities(s, ids_, SketchTransform::rotate(*center_, a), true); });
    showDynamicText(c, formatAngle(a * 180.0 / kPi));
  }
  void stageClick(ToolContext& ctx, Pt2 c, const ViewerMouseEvent& e) override {
    if (!center_) {
      center_ = snap(c, e);
      return;
    }
    if (c.distance(*center_) < 1e-9) return;
    apply(ctx, (c - *center_).angle());
  }
  bool stageTyped(ToolContext& ctx, const std::string& text) override {
    if (!center_) {
      auto p = typedPoint(text, std::nullopt);
      if (!p) return false;
      center_ = *p;
      return true;
    }
    std::string err;
    auto v = evaluateTyped(ctx, text, true, &err);
    if (!v) {
      ctx.showMessage(err, true);
      return true;
    }
    apply(ctx, *v * kPi / 180.0);
    return true;
  }

 private:
  void apply(ToolContext& ctx, double angle) {
    if (commit(ctx, copy_ ? "Copy Rotate" : "Rotate",
               [&](Sketch& s) { return transformEntities(s, ids_, SketchTransform::rotate(*center_, angle), copy_); }))
      ctx.finish();
  }
  bool copy_ = false;
  std::optional<Pt2> center_;
};

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

class ScaleTool : public SelectFirstTool {
 public:
  ScaleTool()
      : SelectFirstTool(ToolInfo{.id = "sketch.scale",
                                 .name = "Scale",
                                 .tooltip = "Scale sketch entities about a base point (select, Enter, base point, type the factor); "
                                            "length dimensions scale with them",
                                 .icon = "sketch-scale",
                                 .aliases = {"SSCALE", "SKSCALE"},
                                 .tab = "SKETCH",
                                 .group = "MODIFY",
                                 .order = 22}) {}

 protected:
  void started(ToolContext& ctx) override {
    base_.reset();
    auto n = numericArgs(ctx);
    factor_ = n.empty() ? 0.0 : n[0];
  }
  std::string stagePrompt() const override { return base_ ? "Type the scale factor" : "Specify the base point"; }
  void stageClick(ToolContext& ctx, Pt2 c, const ViewerMouseEvent& e) override {
    if (base_) return;
    base_ = snap(c, e);
    if (factor_ > 0) apply(ctx, factor_);
  }
  void stageMove(ToolContext&, Pt2, const ViewerMouseEvent&) override {
    if (base_ && factor_ > 0) previewOn([&](Sketch& s) { return transformEntities(s, ids_, SketchTransform::scaling(*base_, factor_), true); });
  }
  bool stageTyped(ToolContext& ctx, const std::string& text) override {
    if (!base_) {
      auto p = typedPoint(text, std::nullopt);
      if (!p) return false;
      base_ = *p;
      if (factor_ > 0) apply(ctx, factor_);
      return true;
    }
    TypedInput in = parseTypedInput(text);
    if (in.kind != TypedInput::Kind::Number || in.a <= 0) {
      ctx.showMessage("the scale factor must be a positive number", true);
      return true;
    }
    apply(ctx, in.a);
    return true;
  }

 private:
  void apply(ToolContext& ctx, double k) {
    if (commit(ctx, "Scale", [&](Sketch& s) { return transformEntities(s, ids_, SketchTransform::scaling(*base_, k), false); }))
      ctx.finish();
  }
  std::optional<Pt2> base_;
  double factor_ = 0;
};

// ---------------------------------------------------------------------------
// Rectangular pattern
// ---------------------------------------------------------------------------

class RectPatternTool : public SelectFirstTool {
 public:
  RectPatternTool()
      : SelectFirstTool(ToolInfo{.id = "sketch.pattern-rectangular",
                                 .name = "Rectangular Pattern",
                                 .tooltip = "Rectangular sketch pattern: select, Enter, type count,spacing for direction 1 and "
                                            "again for direction 2 (click a line to set direction 1), Enter applies. "
                                            "Arguments: count1 spacing1 [count2 spacing2]",
                                 .icon = "sketch-pattern-rectangular",
                                 .aliases = {"SRECPATTERN", "SARRAY"},
                                 .tab = "SKETCH",
                                 .group = "CREATE",
                                 .order = 15}) {}

 protected:
  void started(ToolContext& ctx) override {
    spec_ = RectangularPatternSpec{};
    spec_.count1 = 2;
    spec_.count2 = 1;
    dirsSet_ = 0;
    auto n = numericArgs(ctx);
    if (n.size() >= 2) {
      spec_.count1 = std::max(1, static_cast<int>(std::lround(n[0])));
      spec_.spacing1 = n[1] * lengthFactorOf(document());
      dirsSet_ = 1;
    }
    if (n.size() >= 4) {
      spec_.count2 = std::max(1, static_cast<int>(std::lround(n[2])));
      spec_.spacing2 = n[3] * lengthFactorOf(document());
      dirsSet_ = 2;
    }
    if (!selecting_ && n.size() >= 2) {
      // preselection + arguments: apply at once
      if (commit(ctx, "Rectangular Pattern", [&](Sketch& s) { return rectangularPattern(s, ids_, spec_); })) ctx.finish();
    }
  }
  std::string stagePrompt() const override {
    return std::format("Direction {}: type count,spacing (now {} x {} / {} x {}); Enter applies", dirsSet_ >= 1 ? 2 : 1,
                       spec_.count1, formatLength(spec_.spacing1), spec_.count2, formatLength(spec_.spacing2));
  }
  void stageMove(ToolContext&, Pt2, const ViewerMouseEvent&) override {
    previewOn([&](Sketch& s) { return rectangularPattern(s, ids_, spec_); });
  }
  void stageClick(ToolContext&, Pt2 c, const ViewerMouseEvent&) override {
    Sketch* sk = sketch();
    if (!sk) return;
    if (auto h = sk->hitCurve(c, tol(c))) {
      const Geometry* g = sk->geometry(h->ref.geo);
      if (g && g->type == GeomType::Line) {
        spec_.direction1 = g->direction();
        spec_.direction2 = g->direction().perp();
      }
    }
  }
  bool stageTyped(ToolContext& ctx, const std::string& text) override {
    TypedInput in = parseTypedInput(text);
    double f = lengthFactorOf(document());
    int count = 0;
    double spacing = 0;
    if (in.kind == TypedInput::Kind::Pair) {
      count = static_cast<int>(std::lround(in.a));
      spacing = in.b * f;
    } else if (in.kind == TypedInput::Kind::Number) {
      count = static_cast<int>(std::lround(in.a));
      spacing = dirsSet_ >= 1 ? spec_.spacing2 : spec_.spacing1;
    } else {
      return false;
    }
    if (count < 1) {
      ctx.showMessage("the count must be at least 1", true);
      return true;
    }
    if (dirsSet_ == 0) {
      spec_.count1 = count;
      spec_.spacing1 = spacing;
      dirsSet_ = 1;
    } else {
      spec_.count2 = count;
      spec_.spacing2 = spacing;
      dirsSet_ = 2;
    }
    return true;
  }
  bool stageAccept(ToolContext& ctx) override {
    if (commit(ctx, "Rectangular Pattern", [&](Sketch& s) { return rectangularPattern(s, ids_, spec_); })) ctx.finish();
    return true;
  }

 private:
  RectangularPatternSpec spec_;
  int dirsSet_ = 0;
};

// ---------------------------------------------------------------------------
// Circular pattern
// ---------------------------------------------------------------------------

class CircPatternTool : public SelectFirstTool {
 public:
  CircPatternTool()
      : SelectFirstTool(ToolInfo{.id = "sketch.pattern-circular",
                                 .name = "Circular Pattern",
                                 .tooltip = "Circular sketch pattern: select, Enter, pick the centre, type the count (or "
                                            "count,angle). Arguments: count [angle]",
                                 .icon = "sketch-pattern-circular",
                                 .aliases = {"SCIRCPATTERN", "SPOLAR"},
                                 .tab = "SKETCH",
                                 .group = "CREATE",
                                 .order = 15}) {}

 protected:
  void started(ToolContext& ctx) override {
    center_.reset();
    spec_ = CircularPatternSpec{};
    auto n = numericArgs(ctx);
    argCount_ = n.size() >= 1 ? static_cast<int>(std::lround(n[0])) : 0;
    if (n.size() >= 2) spec_.totalAngle = n[1];
  }
  std::string stagePrompt() const override {
    return center_ ? "Type the number of instances (or count,total angle)" : "Specify the pattern centre";
  }
  void stageMove(ToolContext&, Pt2 c, const ViewerMouseEvent& e) override {
    CircularPatternSpec s = spec_;
    s.center = center_.value_or(snap(c, e));
    if (s.count < 2) return;
    previewOn([&](Sketch& sk) { return circularPattern(sk, ids_, s); });
  }
  void stageClick(ToolContext& ctx, Pt2 c, const ViewerMouseEvent& e) override {
    if (center_) return;
    center_ = snap(c, e);
    if (argCount_ >= 2) apply(ctx, argCount_);
  }
  bool stageTyped(ToolContext& ctx, const std::string& text) override {
    if (!center_) {
      auto p = typedPoint(text, std::nullopt);
      if (!p) return false;
      center_ = *p;
      if (argCount_ >= 2) apply(ctx, argCount_);
      return true;
    }
    TypedInput in = parseTypedInput(text);
    if (in.kind == TypedInput::Kind::Pair) {
      spec_.totalAngle = in.b;
      apply(ctx, static_cast<int>(std::lround(in.a)));
      return true;
    }
    if (in.kind == TypedInput::Kind::Number) {
      apply(ctx, static_cast<int>(std::lround(in.a)));
      return true;
    }
    return false;
  }

 private:
  void apply(ToolContext& ctx, int count) {
    if (count < 2) {
      ctx.showMessage("a circular pattern needs at least 2 instances", true);
      return;
    }
    spec_.count = count;
    spec_.center = *center_;
    if (commit(ctx, "Circular Pattern", [&](Sketch& s) { return circularPattern(s, ids_, spec_); })) ctx.finish();
  }
  std::optional<Pt2> center_;
  CircularPatternSpec spec_;
  int argCount_ = 0;
};

// ---------------------------------------------------------------------------
// Chamfer
// ---------------------------------------------------------------------------

class ChamferTool : public SketchTool {
 public:
  ChamferTool()
      : SketchTool(ToolInfo{.id = "sketch.chamfer",
                            .name = "Chamfer",
                            .tooltip = "Chamfer the corner between two lines (type a distance, or d1,d2 for two distances)",
                            .icon = "sketch-chamfer",
                            .aliases = {"SCHAMFER", "SKCHAMFER"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 2}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    picks_.clear();
    auto n = numericArgs(ctx);
    if (!n.empty()) d1_ = d2_ = n[0] * lengthFactorOf(document());
    if (n.size() >= 2) d2_ = n[1] * lengthFactorOf(document());
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    for (const auto& [id, at] : picks_)
      if (const Geometry* g = sk->geometry(id)) previewGeometry(*g, highlightStyle());
    if (auto hit = sk->hitCurve(*c, tol(*c))) {
      const Geometry* g = sk->geometry(hit->ref.geo);
      if (g && g->type == GeomType::Line) previewGeometry(*g, highlightStyle());
      if (g && picks_.size() == 1 && hit->ref.geo != picks_[0].first) {
        Sketch copy = *sk;
        if (auto ch = chamfer(copy, picks_[0].first, picks_[0].second, hit->ref.geo, hit->point, d1_, d2_))
          previewGeometry(*copy.geometry(*ch), rubberStyle());
      }
    }
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return true;
    auto hit = sk->hitCurve(*c, tol(*c));
    const Geometry* g = hit ? sk->geometry(hit->ref.geo) : nullptr;
    if (!g || g->type != GeomType::Line) {
      if (hit) ctx.showMessage("Chamfer works on lines", true);
      return true;
    }
    if (picks_.empty() || picks_[0].first != hit->ref.geo) picks_.push_back({hit->ref.geo, hit->point});
    if (picks_.size() == 2) apply(ctx);
    ctx.setPrompt(prompt());
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    TypedInput in = parseTypedInput(text);
    double f = lengthFactorOf(document());
    if (in.kind == TypedInput::Kind::Pair && in.a > 0 && in.b > 0) {
      d1_ = in.a * f;
      d2_ = in.b * f;
    } else {
      std::string err;
      auto v = evaluateTyped(ctx, text, false, &err);
      if (!v || *v <= 0) {
        ctx.showMessage(err.empty() ? "distance must be positive" : err, true);
        return true;
      }
      d1_ = d2_ = *v;
    }
    ctx.setPrompt(prompt());
    return true;
  }

 private:
  std::string prompt() const {
    std::string d = std::abs(d1_ - d2_) < 1e-12 ? formatLength(d1_) : formatLength(d1_) + " x " + formatLength(d2_);
    return std::format("Select the {} line (chamfer {})", picks_.empty() ? "first" : "second", d);
  }
  void apply(ToolContext& ctx) {
    Sketch* sk = sketch();
    if (!sk || !session() || picks_.size() < 2) return;
    session()->beginEdit();
    auto ch = chamfer(*sk, picks_[0].first, picks_[0].second, picks_[1].first, picks_[1].second, d1_, d2_);
    if (!ch) {
      session()->cancelEdit();
      ctx.showMessage("Cannot chamfer these lines with that distance", true);
    } else {
      session()->commitEdit("Chamfer");
    }
    picks_.clear();
    clearTemp();
  }
  inline static double d1_ = 2.0, d2_ = 2.0;
  std::vector<std::pair<int, Pt2>> picks_;
};

}  // namespace

void registerTransformTools(core::ToolRegistry& reg) {
  reg.registerTool(std::make_unique<MoveTool>());
  reg.registerTool(std::make_unique<RotateTool>());
  reg.registerTool(std::make_unique<ScaleTool>());
  reg.registerTool(std::make_unique<RectPatternTool>());
  reg.registerTool(std::make_unique<CircPatternTool>());
  reg.registerTool(std::make_unique<ChamferTool>());
}

}  // namespace fabricad::sketch
