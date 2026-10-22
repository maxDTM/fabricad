// FabriCAD sketch tools: MODIFY group (fillet, trim with power-trim, extend, break, offset,
// mirror, project 3D geometry, construction toggle) and the dimension tools (create, edit).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/tools/SketchToolBase.h"
#include "sketch/tools/SketchTools.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"
#include "core/Units.h"

#include <QApplication>
#include <QLineEdit>
#include <QTimer>
#include <QPointer>
#include <QtCore/Qt>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <set>

namespace fabricad::sketch {

using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
using core::ViewerKeyEvent;
using core::ViewerMouseEvent;

namespace {

std::string trimmed(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

/// Set a dimension from typed text (number or expression). Reverts on solver failure.
bool applyDimensionText(ToolContext& ctx, Sketch& sk, int id, const std::string& text, std::string* err) {
  Constraint* c = sk.constraint(id);
  if (!c || !c->isDimensional()) {
    if (err) *err = "not a dimension";
    return false;
  }
  if (c->driven) {
    if (err) *err = "driven dimensions are measured, not set";
    return false;
  }
  auto v = evaluateTyped(ctx, text, c->type == ConstraintType::Angle, err);
  if (!v) return false;
  const double oldValue = c->value;
  const std::string oldExpr = c->expression;
  TypedInput ti = parseTypedInput(text);
  c->value = *v;
  c->expression = ti.kind == TypedInput::Kind::Number ? std::string() : trimmed(text);
  SolveResult r = sk.solve();
  if (!r.ok()) {
    c = sk.constraint(id);
    c->value = oldValue;
    c->expression = oldExpr;
    sk.solve();
    if (err) *err = "the sketch cannot satisfy that value: " + r.message;
    return false;
  }
  return true;
}

std::string dimensionEditText(const Constraint& c, const core::UnitSystem& units) {
  if (!c.expression.empty()) return c.expression;
  if (c.type == ConstraintType::Angle) return core::formatQuantity(core::Quantity::deg(c.value), units, false);
  return core::formatQuantity(core::Quantity::mm(c.value), units, false);
}

/// Give a dimension value field keyboard focus with its text selected, on the next event-loop
/// turn (after the dialog is shown). Leaves the focus alone while another text field (the
/// command line) has it, so a value typed there still goes to the command line.
void focusValueField(QLineEdit* edit) {
  QPointer<QLineEdit> guard(edit);
  QTimer::singleShot(0, edit, [guard] {
    if (!guard) return;
    QWidget* f = QApplication::focusWidget();
    if (f && f != guard && qobject_cast<QLineEdit*>(f)) return;
    guard->setFocus(Qt::OtherFocusReason);
    guard->selectAll();
  });
}

/// Typing into a dimension value while its field does not have the focus (keys reach the
/// viewer): printable keys and Backspace go to the field (or to `buffer` when there is no
/// field, e.g. headless). Returns true when the key was consumed.
bool forwardValueKey(QLineEdit* edit, std::string& buffer, const ViewerKeyEvent& e) {
  if (e.modifiers & (core::ModCtrl | core::ModAlt)) return false;
  if (e.key == Qt::Key_Backspace) {
    if (edit) {
      edit->setFocus(Qt::OtherFocusReason);
      edit->backspace();
      edit->setModified(true);
    }
    if (!buffer.empty()) buffer.pop_back();
    return true;
  }
  if (e.text.empty()) return false;
  const unsigned char ch = static_cast<unsigned char>(e.text[0]);
  if (ch < 0x20 || ch == 0x7f) return false;
  if (edit) {
    edit->setFocus(Qt::OtherFocusReason);
    edit->insert(QString::fromStdString(e.text));  // replaces the selected measured value
    edit->setModified(true);
  }
  buffer += e.text;
  return true;
}

/// The typed value for Enter: the field text when it was edited, else the forwarded keys.
std::string typedValue(QLineEdit* edit, const std::string& buffer) {
  if (edit && edit->isModified()) return trimmed(edit->text().toStdString());
  return trimmed(buffer);
}

// ---------------------------------------------------------------------------
// Fillet
// ---------------------------------------------------------------------------

class FilletTool : public SketchTool {
 public:
  FilletTool()
      : SketchTool(ToolInfo{.id = "sketch.fillet",
                            .name = "Fillet",
                            .tooltip = "Round the corner between two curves (type a radius to change it)",
                            .icon = "sketch-fillet",
                            .aliases = {"FILLET", "SFILLET"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 1,
                            .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    picks_.clear();
    radius_ = lastRadius_;
    if (!ctx.args.empty())
      if (auto v = evaluateTyped(ctx, ctx.args[0], false)) radius_ = lastRadius_ = *v;
    if (session() && sketch()) {
      for (int id : session()->selectedCurves()) {
        const Geometry* g = sketch()->geometry(id);
        if (g && picks_.size() < 2) picks_.push_back({id, g->midpoint()});
      }
      if (ctx.selection) ctx.selection->clear();
      if (picks_.size() == 2) apply(ctx);
    }
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
      if (const Geometry* g = sk->geometry(hit->ref.geo)) previewGeometry(*g, highlightStyle());
      if (picks_.size() == 1 && hit->ref.geo != picks_[0].first) {
        Sketch copy = *sk;
        if (auto arc = copy.fillet(picks_[0].first, picks_[0].second, hit->ref.geo, hit->point, radius_))
          previewGeometry(*copy.geometry(*arc), rubberStyle());
      }
    }
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return true;
    if (auto hit = sk->hitCurve(*c, tol(*c))) {
      if (picks_.empty() || picks_[0].first != hit->ref.geo) picks_.push_back({hit->ref.geo, hit->point});
      if (picks_.size() == 2) apply(ctx);
      ctx.setPrompt(prompt());
    }
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    std::string err;
    auto v = evaluateTyped(ctx, text, false, &err);
    if (!v || *v <= 0) {
      ctx.showMessage(err.empty() ? "radius must be positive" : err, true);
      return true;
    }
    radius_ = lastRadius_ = *v;
    ctx.setPrompt(prompt());
    return true;
  }

 private:
  std::string prompt() const {
    return std::format("Select the {} curve (radius {})", picks_.empty() ? "first" : "second", formatLength(radius_));
  }
  void apply(ToolContext& ctx) {
    Sketch* sk = sketch();
    if (!sk || !session() || picks_.size() < 2) return;
    session()->beginEdit();
    auto arc = sk->fillet(picks_[0].first, picks_[0].second, picks_[1].first, picks_[1].second, radius_);
    SolveResult solved;
    if (arc) solved = sk->solve();
    if (!arc) {
      session()->cancelEdit();
      ctx.showMessage("Cannot fillet these curves with that radius", true);
    } else if (!solved.ok()) {
      // never commit an over-constrained / collapsed sketch: roll back (UX-22)
      sk->takeDemotedDimensions();
      sk->takeModifyNotes();
      session()->cancelEdit();
      ctx.showMessage("Fillet cancelled: the sketch constraints cannot hold it (" + solved.message + ")", true);
    } else {
      session()->commitEdit("Fillet");
      for (const std::string& n : sk->takeDemotedDimensions())
        ctx.showMessage(n + " is now a driven dimension (its corner was filleted)");
      for (const std::string& n : sk->takeModifyNotes()) ctx.showMessage("Fillet: " + n);
    }
    picks_.clear();
    clearTemp();
  }

  inline static double lastRadius_ = 5.0;
  double radius_ = 5.0;
  std::vector<std::pair<int, Pt2>> picks_;
};

// ---------------------------------------------------------------------------
// Trim (hover preview, click, drag for power trim)
// ---------------------------------------------------------------------------

class TrimTool : public SketchTool {
 public:
  TrimTool()
      : SketchTool(ToolInfo{.id = "sketch.trim",
                            .name = "Trim",
                            .tooltip = "Remove the piece of a curve between intersections (drag across curves to trim several)",
                            .icon = "sketch-trim",
                            .shortcut = "T",
                            .aliases = {"TRIM", "TR"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 2,
                            .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    pressed_ = false;
    ctx.setPrompt("Select the piece of a curve to remove");
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    auto hit = sk->hitCurve(*c, tol(*c));
    if (!hit) return true;
    if (pressed_ && (e.buttons & 1)) {
      trim(ctx, hit->ref.geo, *c);
      return true;
    }
    if (auto piece = sk->trimPiece(hit->ref.geo, *c)) previewGeometry(*piece, removeStyle());
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return true;
    pressed_ = true;
    if (auto hit = sk->hitCurve(*c, tol(*c))) trim(ctx, hit->ref.geo, *c);
    return true;
  }

  bool mouseRelease(ToolContext&, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    pressed_ = false;
    return true;
  }

 private:
  void trim(ToolContext& ctx, int id, Pt2 at) {
    Sketch* sk = sketch();
    if (!sk || !session()) return;
    clearTemp();
    session()->beginEdit();
    sk->trim(id, at);
    session()->commitEdit("Trim");
    ctx.setPrompt(session()->statusText());
  }
  bool pressed_ = false;
};

// ---------------------------------------------------------------------------
// Extend
// ---------------------------------------------------------------------------

class ExtendTool : public SketchTool {
 public:
  ExtendTool()
      : SketchTool(ToolInfo{.id = "sketch.extend",
                            .name = "Extend",
                            .tooltip = "Extend a curve to the next curve",
                            .icon = "sketch-extend",
                            .aliases = {"EXTEND", "EX"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 3}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    ctx.setPrompt("Select the end of a curve to extend");
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    auto hit = sk->hitCurve(*c, tol(*c));
    if (!hit) return true;
    PointPos which = PointPos::None;
    auto target = sk->extendTarget(hit->ref.geo, *c, &which);
    const Geometry* g = sk->geometry(hit->ref.geo);
    if (!target || !g) return true;
    if (g->type == GeomType::Line) {
      previewLines({g->point(which), *target}, rubberStyle());
    } else {
      double ang = (*target - g->center()).angle();
      Geometry ext = which == PointPos::End ? Geometry::makeArc(g->center(), g->radius(), g->endAngle(), ang)
                                            : Geometry::makeArc(g->center(), g->radius(), ang, g->startAngle());
      previewGeometry(ext, rubberStyle());
    }
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk || !session()) return true;
    if (auto hit = sk->hitCurve(*c, tol(*c))) {
      clearTemp();
      session()->beginEdit();
      if (sk->extend(hit->ref.geo, *c)) session()->commitEdit("Extend");
      else {
        session()->cancelEdit();
        ctx.showMessage("Nothing to extend to", true);
      }
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Break (split at a point)
// ---------------------------------------------------------------------------

class BreakTool : public SketchTool {
 public:
  BreakTool()
      : SketchTool(ToolInfo{.id = "sketch.break",
                            .name = "Break",
                            .tooltip = "Split a curve at a point",
                            .icon = "sketch-break",
                            .aliases = {"BREAK", "BR"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 4}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    ctx.setPrompt("Select the point on a curve where to break it");
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    if (auto hit = sk->hitCurve(*c, tol(*c))) {
      if (const Geometry* g = sk->geometry(hit->ref.geo)) previewGeometry(*g, highlightStyle());
      previewPoints({hit->point}, markerStyle());
    }
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk || !session()) return true;
    if (auto hit = sk->hitCurve(*c, tol(*c))) {
      clearTemp();
      session()->beginEdit();
      if (sk->split(hit->ref.geo, hit->point)) session()->commitEdit("Break");
      else {
        session()->cancelEdit();
        ctx.showMessage("Cannot break here", true);
      }
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Offset
// ---------------------------------------------------------------------------

class OffsetTool : public SketchTool {
 public:
  OffsetTool()
      : SketchTool(ToolInfo{.id = "sketch.offset",
                            .name = "Offset",
                            .tooltip = "Offset a chain of curves: select curves, then click the offset side or type a distance",
                            .icon = "sketch-offset",
                            .shortcut = "O",
                            .aliases = {"OFFSET", "O"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 5,
                            .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    ids_.clear();
    cursor_.reset();
    if (session()) {
      ids_ = session()->selectedCurves();
      if (ctx.selection) ctx.selection->clear();
    }
    defineValues({ValueField{.id = "distance", .label = "Distance", .kind = ValueKind::Length, .visible = !ids_.empty()}});
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    for (int id : ids_)
      if (const Geometry* g = sk->geometry(id)) previewGeometry(*g, highlightStyle());
    auto hit = sk->hitCurve(*c, tol(*c));
    if (hit && (ids_.empty() || std::count(ids_.begin(), ids_.end(), hit->ref.geo))) {
      for (int id : sk->chain(hit->ref.geo))
        if (const Geometry* g = sk->geometry(id)) previewGeometry(*g, highlightStyle());
      return true;
    }
    if (ids_.empty()) return true;
    cursor_ = *c;
    previewOffset(*c);
    return true;
  }

  /// Rubber-band offset at the cursor side; a locked distance box overrides the cursor distance.
  void previewOffset(Pt2 c) {
    Sketch* sk = sketch();
    if (!sk || ids_.empty()) return;
    double d = lockedValue("distance").value_or(distanceTo(c));
    if (d < 1e-9) return;
    Sketch copy = *sk;
    for (int id : copy.offset(ids_, d, c))
      if (const Geometry* g = copy.geometry(id)) previewGeometry(*g, rubberStyle());
    setValue("distance", d, c);
    showValueBoxes(c);
  }

  void valuesChanged(ToolContext&) override {
    if (!cursor_) return;
    clearTemp();
    previewOffset(*cursor_);
  }

  bool commitValues(ToolContext& ctx) override {
    auto d = lockedValue("distance");
    if (!d || ids_.empty()) return false;
    apply(ctx, *d, cursor_);
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return true;
    auto hit = sk->hitCurve(*c, tol(*c));
    if (hit && (ids_.empty() || (e.modifiers & core::ModCtrl) || std::count(ids_.begin(), ids_.end(), hit->ref.geo))) {
      for (int id : sk->chain(hit->ref.geo)) {
        auto it = std::find(ids_.begin(), ids_.end(), id);
        if (it == ids_.end()) ids_.push_back(id);
        else ids_.erase(it);
      }
      setValueVisible("distance", !ids_.empty());
      ctx.setPrompt(prompt());
      return true;
    }
    if (ids_.empty()) return true;
    if (values().isTyping()) values().lockActive();
    cursor_ = *c;
    apply(ctx, lockedValue("distance").value_or(distanceTo(*c)), *c);
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    if (ids_.empty()) {
      ctx.showMessage("Select curves to offset first", true);
      return true;
    }
    return typedValuesToBoxes(ctx, text);
  }

  void accept(ToolContext& ctx) override {
    if (!ids_.empty() && cursor_) apply(ctx, distanceTo(*cursor_), *cursor_);
    else ctx.finish();
  }

 private:
  std::string prompt() const {
    return ids_.empty() ? "Select curves to offset" : "Click the offset side (or type a distance; Ctrl+click adds curves)";
  }
  double distanceTo(Pt2 p) const {
    const Sketch* sk = sketch();
    double best = std::numeric_limits<double>::infinity();
    for (int id : ids_)
      if (const Geometry* g = sk->geometry(id)) best = std::min(best, g->distanceTo(p));
    return std::isfinite(best) ? best : 0.0;
  }
  void apply(ToolContext& ctx, double d, std::optional<Pt2> side) {
    Sketch* sk = sketch();
    if (!sk || !session() || std::abs(d) < 1e-9) return;
    clearTemp();
    session()->beginEdit();
    auto out = sk->offset(ids_, d, side);
    if (out.empty()) {
      session()->cancelEdit();
      ctx.showMessage("Offset produced no geometry", true);
      return;
    }
    // a typed distance becomes a driving dimension between the first source line and its copy
    if (const ValueField* f = values().field("distance"); f && f->locked) {
      bool added = false;
      for (int src : ids_) {
        const Geometry* gs = sk->geometry(src);
        if (!gs || gs->type != GeomType::Line) continue;
        for (int o : out) {
          const Geometry* go = sk->geometry(o);
          if (!go || go->type != GeomType::Line) continue;
          if (std::abs(gs->direction().cross(go->direction())) > 1e-6) continue;
          double dist = std::abs(signedDistanceToLine(gs->start(), gs->direction(), go->start()));
          if (std::abs(dist - std::abs(d)) > 1e-6) continue;
          addLockedDimension(ctx, *f, Constraint{.type = ConstraintType::Distance, .a = {src}, .b = {o}});
          added = true;
          break;
        }
        if (added) break;
      }
      if (!added) ctx.showMessage("Offset of curved geometry: no offset dimension added");
    }
    session()->commitEdit("Offset");
    values().resetInput();
    if (ctx.selection && session()) {
      std::vector<ShapeRef> refs;
      for (int id : out) refs.push_back(session()->toShapeRef({id, PointPos::None}));
      ctx.selection->set(refs);
    }
    ctx.finish();
  }

  std::vector<int> ids_;
  std::optional<Pt2> cursor_;
};

// ---------------------------------------------------------------------------
// Mirror
// ---------------------------------------------------------------------------

class MirrorTool : public SketchTool {
 public:
  MirrorTool()
      : SketchTool(ToolInfo{.id = "sketch.mirror",
                            .name = "Mirror",
                            .tooltip = "Mirror curves about a line (select objects, Enter, then the mirror line)",
                            .icon = "sketch-mirror",
                            .aliases = {"MIRROR", "MI"},
                            .tab = "SKETCH",
                            .group = "CREATE",
                            .order = 14}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    ids_.clear();
    if (session()) {
      ids_ = session()->selectedCurves();
      if (ctx.selection) ctx.selection->clear();
    }
    lineMode_ = !ids_.empty();
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    for (int id : ids_)
      if (const Geometry* g = sk->geometry(id)) previewGeometry(*g, highlightStyle());
    if (auto hit = sk->hitCurve(*c, tol(*c))) {
      const Geometry* g = sk->geometry(hit->ref.geo);
      if (!g) return true;
      if (lineMode_ && g->type == GeomType::Line) {
        previewGeometry(*g, highlightStyle());
        Sketch copy = *sk;
        for (int id : copy.mirror(ids_, hit->ref.geo, false))
          if (const Geometry* m = copy.geometry(id)) previewGeometry(*m, rubberStyle());
      } else if (!lineMode_) {
        previewGeometry(*g, highlightStyle());
      }
    }
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk || !session()) return true;
    auto hit = sk->hitCurve(*c, tol(*c));
    if (!hit) return true;
    if (!lineMode_) {
      auto it = std::find(ids_.begin(), ids_.end(), hit->ref.geo);
      if (it == ids_.end()) ids_.push_back(hit->ref.geo);
      else ids_.erase(it);
      ctx.setPrompt(prompt());
      return true;
    }
    const Geometry* g = sk->geometry(hit->ref.geo);
    if (!g || g->type != GeomType::Line) {
      ctx.showMessage("Select a line as the mirror axis", true);
      return true;
    }
    clearTemp();
    session()->beginEdit();
    auto out = sk->mirror(ids_, hit->ref.geo, true);
    if (out.empty()) {
      session()->cancelEdit();
      ctx.showMessage("Nothing to mirror", true);
      return true;
    }
    session()->commitEdit("Mirror");
    ctx.finish();
    return true;
  }

  void accept(ToolContext& ctx) override {
    if (!lineMode_ && !ids_.empty()) {
      lineMode_ = true;
      ctx.setPrompt(prompt());
      return;
    }
    ctx.finish();
  }

 private:
  std::string prompt() const {
    if (!lineMode_) return ids_.empty() ? "Select curves to mirror" : "Select more curves, or press Enter to pick the mirror line";
    return "Select the mirror line";
  }
  std::vector<int> ids_;
  bool lineMode_ = false;
};

// ---------------------------------------------------------------------------
// Project 3D geometry
// ---------------------------------------------------------------------------

class ProjectTool : public SketchTool {
 public:
  ProjectTool()
      : SketchTool(ToolInfo{.id = "sketch.project",
                            .name = "Project",
                            .tooltip = "Project edges, vertices or faces of bodies onto the sketch plane",
                            .icon = "sketch-project",
                            .shortcut = "P",
                            .aliases = {"PROJECT", "PROJ"},
                            .tab = "SKETCH",
                            .group = "CREATE",
                            .order = 15,
                            .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    const core::ShapeKindMask kinds = ShapeKind::Edge | ShapeKind::Vertex | ShapeKind::Face | ShapeKind::Body;
    if (ctx.selection) ctx.selection->setFilter(kinds);
    if (viewer_) viewer_->setSelectionFilter(kinds);
    for (const ShapeRef& r : ctx.preselection)
      if (r.isBrep()) project(ctx, r);
    if (ctx.selection) ctx.selection->clear();
    ctx.setPrompt("Select edges, vertices, faces or bodies to project (Enter to finish)");
  }

  void selectionChanged(ToolContext& ctx) override {
    if (!ctx.selection || ctx.selection->isEmpty()) return;
    std::vector<ShapeRef> items = ctx.selection->items();
    for (const ShapeRef& r : items)
      if (r.isBrep()) project(ctx, r);
    ctx.selection->clear();
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button == core::MouseButton::Left && e.hasPick && e.pick.isBrep()) {
      project(ctx, e.pick);
      return true;
    }
    return false;
  }

 private:
  void project(ToolContext& ctx, const ShapeRef& ref) {
    Sketch* sk = sketch();
    core::Document* doc = document();
    SketchFeature* f = feature();
    if (!sk || !doc || !f || !session()) return;
    TopoDS_Shape shape = core::resolveShape(*doc, ref);
    if (shape.IsNull()) {
      ctx.showMessage("Cannot resolve the selected geometry", true);
      return;
    }
    std::vector<std::pair<TopoDS_Shape, ShapeRef>> items;
    if (shape.ShapeType() == TopAbs_EDGE || shape.ShapeType() == TopAbs_VERTEX) {
      items.push_back({shape, ref});
    } else {
      const TopoDS_Shape& body = doc->bodyShape(ref.owner);
      for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
        ShapeRef sub = body.IsNull() ? ref : ShapeRef::subShape(ref.owner, body, ex.Current());
        items.push_back({ex.Current(), sub});
      }
    }
    session()->beginEdit();
    int added = 0;
    std::string warnings;
    for (const auto& [sub, subRef] : items) {
      bool exists = false;
      for (const Geometry& g : sk->geometries())
        if (g.projected && g.source == subRef) exists = true;
      if (exists) continue;
      std::string warn;
      auto g = projectShape(sub, f->plane(), &warn);
      if (!g) continue;
      g->projected = true;
      g->source = subRef;
      g->construction = session()->constructionMode();
      sk->addGeometry(*g);
      ++added;
      if (!warn.empty() && warnings.empty()) warnings = warn;
    }
    if (added) {
      session()->commitEdit("Project");
      ctx.showMessage(std::format("Projected {} item(s){}", added, warnings.empty() ? "" : " (" + warnings + ")"));
    } else {
      session()->cancelEdit();
    }
  }
};

// ---------------------------------------------------------------------------
// Construction toggle (X)
// ---------------------------------------------------------------------------

class ConstructionTool : public SketchTool {
 public:
  ConstructionTool()
      : SketchTool(ToolInfo{.id = "sketch.construction",
                            .name = "Construction",
                            .tooltip = "Toggle construction on the selected curves, or the construction mode for new curves",
                            .icon = "sketch-construction",
                            .shortcut = "X",
                            .aliases = {"CONSTRUCTION", "X"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 6,
                            .promoted = true,
                            .repeatable = false}) {}
  bool isInstant() const override { return true; }
  void activate(ToolContext& ctx) override {
    SketchSession* s = session();
    Sketch* sk = sketch();
    if (!s || !sk) return;
    std::vector<int> ids = s->selectedCurves();
    for (const GeoRef& r : s->selectedRefs()) {
      const Geometry* g = sk->geometry(r.geo);
      if (g && g->type == GeomType::Point && !std::count(ids.begin(), ids.end(), r.geo)) ids.push_back(r.geo);
    }
    if (!ids.empty()) {
      s->beginEdit();
      sk->toggleConstruction(ids);
      s->commitEdit("Construction");
      return;
    }
    s->setConstructionMode(!s->constructionMode());
    ctx.showMessage(s->constructionMode() ? "Construction mode on: new curves are construction" : "Construction mode off");
  }
};

// ---------------------------------------------------------------------------
// Dimension (D)
// ---------------------------------------------------------------------------

struct DimCandidate {
  bool ok = false;
  ConstraintType type = ConstraintType::Distance;
  GeoRef a, b;
};

class DimensionTool : public SketchTool {
 public:
  DimensionTool()
      : SketchTool(ToolInfo{.id = "sketch.dimension",
                            .name = "Sketch Dimension",
                            .tooltip = "Dimension: pick one or two entities, place the text, then type a value or expression",
                            .icon = "sketch-dimension",
                            .shortcut = "D",
                            .aliases = {"DIMENSION", "DIM", "D"},
                            .tab = "SKETCH",
                            .group = "CREATE",
                            .order = 16,
                            .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    reset();
    if (session()) {
      for (const GeoRef& r : session()->selectedRefs())
        if (picks_.size() < 2) picks_.push_back(normalize(r));
      if (ctx.selection) ctx.selection->clear();
      if (!picks_.empty() && candidate(session()->toSketch({0, 0, 0})).ok) phase_ = Phase::Place;
    }
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext&, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    const Sketch* sk = sketch();
    if (!c || !sk) return false;
    clearTemp();
    cursor_ = *c;
    if (phase_ == Phase::Value) return true;
    for (const GeoRef& r : picks_) highlight(r);
    if (phase_ == Phase::Place) {
      DimCandidate cand = candidate(*c);
      if (cand.ok) {
        Constraint tmp{.type = cand.type, .a = cand.a, .b = cand.b};
        auto m = sk->measure(tmp);
        if (m) {
          tmp.value = *m;
          previewText(*c, session()->dimensionText(tmp), glyphStyle());
          auto pa = anchor(cand);
          if (pa) previewLines({*pa, *c}, guideStyle());
        }
      }
    }
    if (auto hit = sk->hit(*c, tol(*c))) highlight(normalize(hit->ref));
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    Sketch* sk = sketch();
    if (!c || !sk || !session()) return true;
    if (phase_ == Phase::Value) finishValue(ctx);
    auto hit = sk->hit(*c, tol(*c));
    if (hit && hit->ref.pos == PointPos::Mid) hit = sk->hitCurve(*c, tol(*c));  // mid of a curve = the curve
    if (hit) {
      GeoRef r = normalize(hit->ref);
      bool samePick = std::find(picks_.begin(), picks_.end(), r) != picks_.end();
      bool sameGeo = !picks_.empty() && picks_[0].geo == r.geo && !r.isPoint();
      if (picks_.size() < 2 && !samePick && !(phase_ == Phase::Place && sameGeo)) {
        picks_.push_back(r);
        phase_ = candidate(*c).ok ? Phase::Place : Phase::Pick;
        ctx.setPrompt(prompt());
        return true;
      }
    }
    if (phase_ == Phase::Place) place(ctx, *c);
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    if (phase_ != Phase::Value) return false;
    std::string err;
    Sketch* sk = sketch();
    if (sk && applyDimensionText(ctx, *sk, pendingId_, text, &err)) finishValue(ctx);
    else ctx.showMessage(err, true);
    return true;
  }

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (phase_ == Phase::Value) {
      if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
        accept(ctx);
        return true;
      }
      if (forwardValueKey(edit_, typed_, e)) return true;
    }
    return SketchTool::keyPress(ctx, e);
  }

  void accept(ToolContext& ctx) override {
    if (phase_ == Phase::Value) {
      if (std::string text = typedValue(edit_, typed_); !text.empty()) {
        std::string err;
        Sketch* sk = sketch();
        if (!sk || !applyDimensionText(ctx, *sk, pendingId_, text, &err)) {
          ctx.showMessage(err, true);
          return;
        }
      }
      finishValue(ctx);
      return;
    }
    if (phase_ == Phase::Place) {
      reset();
      ctx.setPrompt(prompt());
      return;
    }
    ctx.finish();
  }

  void cancel(ToolContext& ctx) override {
    if (phase_ == Phase::Value) {
      finishValue(ctx);  // Esc keeps the measured value (Fusion behaviour)
      return;
    }
    if (!picks_.empty()) {
      reset();
      clearTemp();
      ctx.setPrompt(prompt());
      return;
    }
    ctx.cancel();
  }

  QWidget* createDialog(ToolContext& ctx, QWidget* parent) override {
    if (phase_ != Phase::Value || !sketch()) return nullptr;
    const Constraint* c = sketch()->constraint(pendingId_);
    if (!c) return nullptr;
    auto* edit = new QLineEdit(parent);
    core::UnitSystem u = document() ? document()->units() : core::UnitSystem{};
    edit->setText(QString::fromStdString(dimensionEditText(*c, u)));
    edit->selectAll();
    edit->setModified(false);
    core::ToolManager* mgr = ctx.manager;
    QObject::connect(edit, &QLineEdit::returnPressed, edit, [mgr, edit] {
      if (mgr) mgr->runCommandLine(edit->text().toStdString());
    });
    edit_ = edit;
    focusValueField(edit);
    return edit;
  }

 private:
  enum class Phase { Pick, Place, Value };

  void reset() {
    picks_.clear();
    phase_ = Phase::Pick;
    pendingId_ = -1;
    edit_ = nullptr;
    typed_.clear();
  }

  std::string prompt() const {
    switch (phase_) {
      case Phase::Pick: return picks_.empty() ? "Select a curve or point to dimension" : "Select a second entity";
      case Phase::Place: return "Place the dimension (or select a second entity)";
      case Phase::Value: return "Enter the dimension value (Enter keeps the measured value)";
    }
    return {};
  }

  GeoRef normalize(GeoRef r) const {
    const Sketch* sk = sketch();
    const Geometry* g = sk ? sk->geometry(r.geo) : nullptr;
    if (g && g->type == GeomType::Point) r.pos = PointPos::Center;
    return r;
  }

  void highlight(const GeoRef& r) {
    const Sketch* sk = sketch();
    const Geometry* g = sk ? sk->geometry(r.geo) : nullptr;
    if (!g) return;
    if (r.pos == PointPos::None || g->type == GeomType::Point) previewGeometry(*g, highlightStyle());
    else if (auto p = sk->pointOf(r)) previewPoints({*p}, markerStyle());
  }

  std::optional<GeoRef> asPoint(const GeoRef& r) const {
    const Sketch* sk = sketch();
    const Geometry* g = sk->geometry(r.geo);
    if (!g) return std::nullopt;
    if (g->type == GeomType::Point) return GeoRef{r.geo, PointPos::Center};
    if (r.isPoint()) return r;
    return std::nullopt;
  }

  ConstraintType linearTypeFor(const GeoRef& pa, const GeoRef& pb, Pt2 label) const {
    const Sketch* sk = sketch();
    auto A = sk->pointOf(pa), B = sk->pointOf(pb);
    if (!A || !B) return ConstraintType::Distance;
    Pt2 d = *B - *A;
    if (d.length() < 1e-9) return ConstraintType::Distance;
    if (std::abs(d.y) < 1e-9) return ConstraintType::DistanceX;
    if (std::abs(d.x) < 1e-9) return ConstraintType::DistanceY;
    Pt2 m = (*A + *B) * 0.5;
    Pt2 v = label - m;
    if (v.length() < 1e-9) return ConstraintType::Distance;
    Pt2 n = d.normalized().perp();
    if (std::abs(v.dot(n)) > 0.7 * v.length()) return ConstraintType::Distance;  // placed perpendicular: aligned
    return std::abs(v.y) >= std::abs(v.x) ? ConstraintType::DistanceX : ConstraintType::DistanceY;
  }

  DimCandidate candidate(Pt2 label) const {
    DimCandidate c;
    const Sketch* sk = sketch();
    if (!sk || picks_.empty()) return c;
    if (picks_.size() == 1) {
      const GeoRef& r = picks_[0];
      const Geometry* g = sk->geometry(r.geo);
      if (!g || r.isPoint() || g->type == GeomType::Point) return c;
      c.a = {r.geo};
      if (g->type == GeomType::Line) c.type = ConstraintType::Distance;
      else if (g->type == GeomType::Circle) c.type = ConstraintType::Diameter;
      else c.type = ConstraintType::Radius;
      c.ok = true;
      return c;
    }
    const GeoRef r1 = picks_[0], r2 = picks_[1];
    const Geometry* g1 = sk->geometry(r1.geo);
    const Geometry* g2 = sk->geometry(r2.geo);
    if (!g1 || !g2) return c;
    auto p1 = asPoint(r1), p2 = asPoint(r2);
    // circles/arcs paired with anything act through their centre
    if (!p1 && g1->isCircular()) p1 = GeoRef{r1.geo, PointPos::Center};
    if (!p2 && g2->isCircular()) p2 = GeoRef{r2.geo, PointPos::Center};
    if (p1 && p2) {
      c.a = *p1;
      c.b = *p2;
      c.type = linearTypeFor(*p1, *p2, label);
      c.ok = true;
      return c;
    }
    if (p1 && g2->type == GeomType::Line) {
      c.a = *p1;
      c.b = {r2.geo};
      c.type = ConstraintType::Distance;
      c.ok = true;
      return c;
    }
    if (p2 && g1->type == GeomType::Line) {
      c.a = *p2;
      c.b = {r1.geo};
      c.type = ConstraintType::Distance;
      c.ok = true;
      return c;
    }
    if (g1->type == GeomType::Line && g2->type == GeomType::Line) {
      c.a = {r1.geo};
      c.b = {r2.geo};
      bool parallel = std::abs(g1->direction().cross(g2->direction())) < 1e-6;
      c.type = parallel ? ConstraintType::Distance : ConstraintType::Angle;
      c.ok = true;
      return c;
    }
    return c;
  }

  std::optional<Pt2> anchor(const DimCandidate& cand) const {
    const Sketch* sk = sketch();
    if (cand.a.isPoint()) return sk->pointOf(cand.a);
    if (const Geometry* g = sk->geometry(cand.a.geo)) return g->midpoint();
    return std::nullopt;
  }

  void place(ToolContext& ctx, Pt2 p) {
    Sketch* sk = sketch();
    DimCandidate cand = candidate(p);
    if (!sk || !session() || !cand.ok) return;
    Constraint c{.type = cand.type, .a = cand.a, .b = cand.b};
    auto m = sk->measure(c);
    if (!m) {
      ctx.showMessage("Cannot measure that combination", true);
      reset();
      return;
    }
    c.value = *m;
    if (c.type == ConstraintType::Angle && c.value < 0) {
      std::swap(c.a, c.b);
      c.value = -c.value;
    }
    c.labelPos = p;
    c.hasLabelPos = true;
    clearTemp();
    session()->beginEdit();
    int id = sk->addConstraint(c);
    SolveResult r = sk->solve();
    bool over = r.status == SolveResult::Status::Conflicting || r.status == SolveResult::Status::Failed ||
                std::count(r.redundant.begin(), r.redundant.end(), id) > 0 || std::count(r.conflicting.begin(), r.conflicting.end(), id) > 0;
    if (over) {
      bool driven = ctx.host ? ctx.host->askYesNo("Sketch Dimension",
                                                 "This dimension would over-constrain the sketch.\nMake it a driven (reference) dimension?", true)
                             : true;
      if (!driven) {
        sk->removeConstraint(id);
        sk->solve();
        session()->cancelEdit();
        ctx.showMessage("Dimension not added: it would over-constrain the sketch", true);
        reset();
        ctx.setPrompt(prompt());
        return;
      }
      sk->constraint(id)->driven = true;
      sk->solve();
    }
    session()->refresh();
    pendingId_ = id;
    phase_ = Phase::Value;
    const Constraint* placed = sk->constraint(id);
    ctx.setPrompt(std::format("{} = {}: enter a value or expression (Enter keeps it)", placed->name, session()->dimensionText(*placed)));
    if (ctx.host) ctx.host->refreshToolDialog();
  }

  void finishValue(ToolContext& ctx) {
    if (phase_ != Phase::Value) return;
    if (session()) {
      session()->commitEdit("Dimension");
      ctx.showMessage(session()->statusText());
    }
    reset();
    ctx.setPrompt(prompt());
    if (ctx.host) ctx.host->refreshToolDialog();
  }

  std::vector<GeoRef> picks_;
  Phase phase_ = Phase::Pick;
  int pendingId_ = -1;
  Pt2 cursor_;
  QPointer<QLineEdit> edit_;
  std::string typed_;  ///< value keys typed while the field had no focus
};

// ---------------------------------------------------------------------------
// Dimension edit (double-click on a dimension label)
// ---------------------------------------------------------------------------

class DimensionEditTool : public SketchTool {
 public:
  DimensionEditTool()
      : SketchTool(ToolInfo{.id = "sketch.dimension-edit",
                            .name = "Edit Dimension",
                            .tooltip = "Change the value or expression of a dimension (arguments: id|name [value])",
                            .icon = "sketch-dimension",
                            .aliases = {"DIMEDIT"},
                            .tab = "SKETCH",
                            .group = "MODIFY",
                            .order = 20,
                            .repeatable = false,
                            .hidden = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    id_ = -1;
    typed_.clear();
    edit_ = nullptr;
    Sketch* sk = sketch();
    if (!sk || !session()) {
      ctx.cancel();
      return;
    }
    if (!ctx.args.empty()) {
      // constraint id, or a dimension name ("d1")
      if (const Constraint* byName = sk->dimensionByName(ctx.args[0])) {
        id_ = byName->id;
      } else {
        try {
          id_ = std::stoi(ctx.args[0]);
        } catch (...) {
          id_ = -1;
        }
      }
    }
    if (id_ < 0 && session()->selectedConstraints().size() == 1) id_ = *session()->selectedConstraints().begin();
    const Constraint* c = sk->constraint(id_);
    if (!c || !c->isDimensional()) {
      ctx.showMessage("Select a dimension to edit", true);
      ctx.cancel();
      return;
    }
    session()->selectConstraint(id_, false);
    if (ctx.args.size() >= 2) {
      std::string value;
      for (size_t i = 1; i < ctx.args.size(); ++i) value += (i > 1 ? " " : "") + ctx.args[i];
      if (!commandLineInput(ctx, value)) ctx.cancel();
      return;
    }
    ctx.setPrompt(std::format("{} = {}: enter a new value or expression", c->name, session()->dimensionText(*c)));
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    Sketch* sk = sketch();
    if (!sk || !session()) return false;
    std::string err;
    session()->beginEdit();
    if (!applyDimensionText(ctx, *sk, id_, text, &err)) {
      session()->cancelEdit();
      ctx.showMessage(err, true);
      return true;
    }
    session()->commitEdit("Edit dimension");
    ctx.showMessage(session()->statusText());
    ctx.finish();
    return true;
  }

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
      accept(ctx);
      return true;
    }
    if (forwardValueKey(edit_, typed_, e)) return true;
    return SketchTool::keyPress(ctx, e);
  }

  void accept(ToolContext& ctx) override {
    if (std::string text = typedValue(edit_, typed_); !text.empty()) {
      commandLineInput(ctx, text);
      return;
    }
    ctx.finish();
  }

  QWidget* createDialog(ToolContext& ctx, QWidget* parent) override {
    const Sketch* sk = sketch();
    const Constraint* c = sk ? sk->constraint(id_) : nullptr;
    if (!c) return nullptr;
    auto* edit = new QLineEdit(parent);
    core::UnitSystem u = document() ? document()->units() : core::UnitSystem{};
    edit->setText(QString::fromStdString(dimensionEditText(*c, u)));
    edit->selectAll();
    edit->setModified(false);
    core::ToolManager* mgr = ctx.manager;
    QObject::connect(edit, &QLineEdit::returnPressed, edit, [mgr, edit] {
      if (mgr) mgr->runCommandLine(edit->text().toStdString());
    });
    edit_ = edit;
    focusValueField(edit);
    return edit;
  }

 private:
  int id_ = -1;
  QPointer<QLineEdit> edit_;
  std::string typed_;  ///< value keys typed while the field had no focus
};

}  // namespace

void registerModifyTools(core::ToolRegistry& reg) {
  reg.registerTool(std::make_unique<FilletTool>());
  reg.registerTool(std::make_unique<TrimTool>());
  reg.registerTool(std::make_unique<ExtendTool>());
  reg.registerTool(std::make_unique<BreakTool>());
  reg.registerTool(std::make_unique<OffsetTool>());
  reg.registerTool(std::make_unique<MirrorTool>());
  reg.registerTool(std::make_unique<ProjectTool>());
  reg.registerTool(std::make_unique<ConstructionTool>());
  reg.registerTool(std::make_unique<DimensionTool>());
  reg.registerTool(std::make_unique<DimensionEditTool>());
}

}  // namespace fabricad::sketch
