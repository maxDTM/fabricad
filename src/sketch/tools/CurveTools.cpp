// FabriCAD sketch tools: ellipse, elliptical arc, fit-point / control-point splines and sketch
// text (SKETCH > CREATE), with Fusion-style rubber-banding, inference and typed input.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchCurves.h"
#include "sketch/SketchText.h"
#include "sketch/tools/SketchToolBase.h"
#include "sketch/tools/SketchTools.h"

#include "core/Document.h"
#include "core/Units.h"

#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QWidget>
#include <QtCore/Qt>

#include <cmath>
#include <format>
#include <memory>

namespace fabricad::sketch {

using core::ToolContext;
using core::ToolInfo;
using core::ViewerKeyEvent;
using core::ViewerMouseEvent;

namespace {

InferenceResult pointsOnly(InferenceResult r) {
  r.horizontal = r.vertical = false;
  r.parallelTo.reset();
  r.perpendicularTo.reset();
  return r;
}

double lengthFactor(const core::Document* doc) {
  core::UnitSystem u = doc ? doc->units() : core::UnitSystem{};
  return core::lengthUnitToMm(u.length);
}

/// Common part of the click-N-points curve tools in this file.
class ClickTool : public SketchTool {
 public:
  using SketchTool::SketchTool;

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    pts_.clear();
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c) return false;
    InferenceResult r = inferAt(*c, e);
    cursor_ = r.point;
    clearTemp();
    showInference(r);
    preview(ctx, r.point);
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c) return true;
    addPoint(ctx, inferAt(*c, e));
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    TypedInput in = parseTypedInput(text);
    const double f = lengthFactor(document());
    std::optional<Pt2> last = pts_.empty() ? std::nullopt : std::optional<Pt2>(pts_.back().point);
    switch (in.kind) {
      case TypedInput::Kind::Pair: addPoint(ctx, inferExact({in.a * f, in.b * f}, last)); return true;
      case TypedInput::Kind::Relative:
        if (!last) return false;
        addPoint(ctx, inferExact(*last + Pt2{in.a * f, in.b * f}, last));
        return true;
      case TypedInput::Kind::Polar:
        if (!last) return false;
        addPoint(ctx, inferExact(*last + Pt2::polar(in.a * f, in.b * kPi / 180.0), last));
        return true;
      case TypedInput::Kind::Number:
      case TypedInput::Kind::Text: {
        std::string err;
        auto v = evaluateTyped(ctx, text, false, &err);
        if (!v) {
          ctx.showMessage(err, true);
          return true;
        }
        return typedNumber(ctx, *v);
      }
      default: return false;
    }
  }

 protected:
  virtual std::string prompt() const = 0;
  virtual void preview(ToolContext& ctx, Pt2 cursor) = 0;
  /// Called after each added point; return true when the curve is complete (create() runs).
  virtual bool complete() const = 0;
  virtual void create(ToolContext& ctx) = 0;
  virtual bool typedNumber(ToolContext&, double) { return false; }

  virtual InferenceResult inferAt(Pt2 c, const ViewerMouseEvent& e) {
    InferenceInput in;
    in.cursor = c;
    if (!pts_.empty()) in.from = pts_.back().point;
    return pointsOnly(infer(in, snapSuppressed(e)));
  }

  void addPoint(ToolContext& ctx, const InferenceResult& r) {
    if (!pts_.empty() && r.point.distance(pts_.back().point) < 1e-9) return;
    pts_.push_back(r);
    if (complete()) {
      create(ctx);
      pts_.clear();
      clearTemp();
    }
    pointsChanged();
    ctx.setPrompt(prompt());
  }

  /// Hook after a point was added (and the curve possibly created).
  virtual void pointsChanged() {}

  std::vector<Pt2> points() const {
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    return p;
  }

  std::vector<InferenceResult> pts_;
  Pt2 cursor_;
};

// ---------------------------------------------------------------------------
// Ellipse / elliptical arc
// ---------------------------------------------------------------------------

/// Minor radius of the ellipse (centre c, major end m) that passes through q; nullopt if q lies
/// on or beyond the major axis extent.
std::optional<double> minorFor(Pt2 c, Pt2 m, Pt2 q) {
  double a = c.distance(m);
  if (a < 1e-9) return std::nullopt;
  Pt2 u = (m - c) / a;
  double x = (q - c).dot(u), y = (q - c).dot(u.perp());
  double t = 1 - (x * x) / (a * a);
  if (t <= 1e-9) return std::abs(y) > 1e-9 ? std::optional<double>(std::abs(y)) : std::nullopt;
  double b = std::abs(y) / std::sqrt(t);
  return b > 1e-9 ? std::optional<double>(b) : std::nullopt;
}

class EllipseTool : public ClickTool {
 public:
  explicit EllipseTool(bool arc)
      : ClickTool(arc ? ToolInfo{.id = "sketch.ellipse-arc",
                                 .name = "Elliptical Arc",
                                 .tooltip = "Elliptical arc: centre, major axis end, start point (sets the minor radius), end point",
                                 .icon = "sketch-ellipse-arc",
                                 .aliases = {"ELLIPTICALARC", "ELA"},
                                 .tab = "SKETCH",
                                 .group = "CREATE",
                                 .order = 12}
                      : ToolInfo{.id = "sketch.ellipse",
                                 .name = "Ellipse",
                                 .tooltip = "Ellipse: centre, major axis end, a point on the minor axis (type radii)",
                                 .icon = "sketch-ellipse",
                                 .aliases = {"ELLIPSE", "EL"},
                                 .tab = "SKETCH",
                                 .group = "CREATE",
                                 .order = 12}),
        arc_(arc) {}

  void activate(ToolContext& ctx) override {
    ClickTool::activate(ctx);
    // on-canvas value boxes like Line / Circle (SK-08): typed keys go here, not to shortcuts
    std::vector<ValueField> f = {ValueField{.id = "major", .label = "Major radius", .kind = ValueKind::Length},
                                 ValueField{.id = "angle", .label = "Angle", .kind = ValueKind::Angle},
                                 ValueField{.id = "minor", .label = "Minor radius", .kind = ValueKind::Length}};
    if (arc_) f.push_back(ValueField{.id = "sweep", .label = "Sweep", .kind = ValueKind::Angle});
    defineValues(std::move(f));
    pointsChanged();
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button == core::MouseButton::Left && values().isTyping()) values().lockActive();  // a click applies the typed value
    return ClickTool::mousePress(ctx, e);
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    TypedInput in = parseTypedInput(text);
    bool toBoxes = !pts_.empty() && values().visibleCount() > 0 &&
                   (in.kind == TypedInput::Kind::Number || in.kind == TypedInput::Kind::Text ||
                    (in.kind == TypedInput::Kind::Pair && values().visibleCount() >= 2));
    if (toBoxes && typedValuesToBoxes(ctx, text)) return true;
    return ClickTool::commandLineInput(ctx, text);
  }

 protected:
  void pointsChanged() override {
    if (pts_.empty()) values().resetInput();
    setValueVisible("major", pts_.size() == 1);
    setValueVisible("angle", pts_.size() == 1);
    setValueVisible("minor", pts_.size() == 2);
    if (arc_) setValueVisible("sweep", pts_.size() == 3);
  }

  /// Frame of the ellipse being drawn once centre and major end are known (minor radius `b`).
  std::optional<EllipseFrame> frameWith(double b) const {
    if (pts_.size() < 2) return std::nullopt;
    Pt2 c = pts_[0].point, m = pts_[1].point;
    if (c.distance(m) < 1e-9 || b <= 1e-9) return std::nullopt;
    EllipseFrame f;
    f.center = c;
    f.major = (m - c).normalized();
    f.minor = f.major.perp();
    f.a = c.distance(m);
    f.b = b;
    return f;
  }

  /// The cursor point moved so that it honours the locked boxes of the current phase.
  Pt2 applyLocks(Pt2 p) const {
    if (pts_.size() == 1) {
      auto R = lockedValue("major");
      auto A = lockedValue("angle");
      if (!R && !A) return p;
      Pt2 c = pts_[0].point, d = p - c;
      double len = R ? *R : d.length();
      double ang = A ? *A * kPi / 180.0 : (d.length() < 1e-9 ? 0.0 : d.angle());
      return c + Pt2::polar(len, ang);
    }
    if (pts_.size() == 2) {
      auto B = lockedValue("minor");
      if (!B) return p;
      Pt2 c = pts_[0].point, u = (pts_[1].point - c).normalized();
      if (!arc_) {
        double side = (p - c).dot(u.perp()) >= 0 ? 1.0 : -1.0;
        return c + u.perp() * (*B * side);
      }
      auto f = frameWith(*B);
      return f ? f->value(f->closestParam(p)) : p;
    }
    if (pts_.size() == 3 && arc_) {
      auto S = lockedValue("sweep");
      auto b = minorFor(pts_[0].point, pts_[1].point, pts_[2].point);
      auto f = b ? frameWith(*b) : std::nullopt;
      if (!S || !f) return p;
      double u0 = f->closestParam(pts_[2].point);
      return f->value(u0 + *S * kPi / 180.0);
    }
    return p;
  }

  InferenceResult inferAt(Pt2 c, const ViewerMouseEvent& e) override {
    InferenceResult r = ClickTool::inferAt(c, e);
    if (pts_.empty() || !values().anyLocked()) return r;
    Pt2 p = applyLocks(r.point);
    if (p.near(r.point, 1e-9)) return r;
    return inferExact(p, pts_.back().point);
  }

  void valuesChanged(ToolContext& ctx) override {
    if (pts_.empty()) return;
    Pt2 p = applyLocks(cursor_);
    clearTemp();
    preview(ctx, p);
  }

  bool commitValues(ToolContext& ctx) override {
    if (pts_.empty() || !values().anyLocked()) return false;
    Pt2 p = applyLocks(cursor_);
    if (pts_.size() == 1 && p.distance(pts_[0].point) < 1e-9) p = pts_[0].point + Pt2{1, 0} * cursor_.distance(pts_[0].point);
    addPoint(ctx, inferExact(p, pts_.back().point));
    return true;
  }

  std::string prompt() const override {
    switch (pts_.size()) {
      case 0: return "Specify the ellipse centre";
      case 1: return "Specify the major axis end (or type the major radius)";
      case 2: return arc_ ? "Specify the arc start (sets the minor radius)" : "Specify the minor radius (or type it)";
      default: return "Specify the arc end";
    }
  }

  bool complete() const override { return pts_.size() >= (arc_ ? 4u : 3u); }

  std::optional<Geometry> build(const std::vector<Pt2>& p) const {
    if (p.size() < 3) return std::nullopt;
    Pt2 c = p[0], m = p[1];
    double a = c.distance(m);
    auto b = minorFor(c, m, p[2]);
    if (a < 1e-9 || !b) return std::nullopt;
    double ang = (m - c).angle();
    if (!arc_) return Geometry::makeEllipse(c, a, *b, ang);
    EllipseFrame f = Geometry::makeEllipse(c, a, *b, ang).ellipse();
    // a < b swaps the frame; parameters are measured in the resulting frame
    double u0 = f.closestParam(p[2]);
    double u1 = p.size() >= 4 ? f.closestParam(p[3]) : u0 + kPi;
    if (std::abs(normalizeAngle(u1 - u0)) < 1e-9) return std::nullopt;
    return Geometry::makeEllipseArc(f.center, f.a, f.b, f.major.angle(), u0, u1);
  }

  void preview(ToolContext& ctx, Pt2 c) override {
    previewShapes(ctx, c);
    std::vector<Pt2> p = points();
    if (p.size() == 1) {
      setValue("major", p[0].distance(c), (p[0] + c) * 0.5);
      setValue("angle", deg360((c - p[0]).angle()), c);
    } else if (p.size() == 2) {
      if (auto b = minorFor(p[0], p[1], c)) setValue("minor", *b, c);
    } else if (p.size() == 3 && arc_) {
      if (auto b = minorFor(p[0], p[1], p[2])) {
        if (auto f = frameWith(*b)) {
          double u0 = f->closestParam(p[2]);
          setValue("sweep", deg360(f->closestParam(c) - u0), c);
        }
      }
    }
    if (!p.empty()) showValueBoxes(c);
  }

  static double deg360(double rad) { return normalizeAngle(rad) * 180.0 / kPi; }

  void previewShapes(ToolContext&, Pt2 c) {
    std::vector<Pt2> p = points();
    if (p.empty()) return;
    if (p.size() == 1) {
      previewLines({p[0], c}, guideStyle());
      return;
    }
    p.push_back(c);
    if (p.size() == 3 && arc_) {
      // show the full ellipse dashed while picking the start point
      std::vector<Pt2> q = p;
      if (auto b = minorFor(q[0], q[1], q[2])) {
        previewGeometry(Geometry::makeEllipse(q[0], q[0].distance(q[1]), *b, (q[1] - q[0]).angle()), guideStyle());
      }
      return;
    }
    if (auto g = build(p)) previewGeometry(*g, rubberStyle());
  }

  bool typedNumber(ToolContext& ctx, double v) override {
    if (v <= 0) return false;
    if (pts_.size() == 1) {
      Pt2 d = cursor_ - pts_[0].point;
      if (d.length() < 1e-9) d = {1, 0};
      addPoint(ctx, inferExact(pts_[0].point + d.normalized() * v, pts_[0].point));
      return true;
    }
    if (pts_.size() == 2) {
      Pt2 c = pts_[0].point, m = pts_[1].point;
      Pt2 u = (m - c).normalized();
      double side = (cursor_ - c).dot(u.perp()) >= 0 ? 1.0 : -1.0;
      addPoint(ctx, inferExact(c + u.perp() * (v * side), c));
      return true;
    }
    return false;
  }

  void create(ToolContext& ctx) override {
    Sketch* sk = sketch();
    auto g = build(points());
    if (!sk || !session() || !g) {
      ctx.showMessage("Degenerate ellipse ignored", true);
      return;
    }
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(*g));
    applyInference(*sk, pts_[0], {id, PointPos::Center}, -1);
    if (arc_) {
      if (pts_[2].onPoint) sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {id, PointPos::Start}, .b = *pts_[2].onPoint});
      if (pts_[3].onPoint) sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {id, PointPos::End}, .b = *pts_[3].onPoint});
    } else {
      for (size_t i = 1; i < pts_.size(); ++i)
        if (pts_[i].onPoint) sk->addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = *pts_[i].onPoint, .b = {id}});
    }
    // typed radii become driving dimensions (a < b swaps the axes of the result)
    const Geometry* made = sk->geometry(id);
    bool swapped = made && pts_[0].point.distance(pts_[1].point) < made->majorRadius() - 1e-9;
    for (const char* box : {"major", "minor"}) {
      const ValueField* f = values().field(box);
      if (!f || !f->locked) continue;
      bool major = (std::string(box) == "major") != swapped;
      addLockedDimension(ctx, *f, Constraint{.type = major ? ConstraintType::MajorRadius : ConstraintType::MinorRadius, .a = {id}});
    }
    session()->commitEdit(arc_ ? "Elliptical Arc" : "Ellipse");
  }

 private:
  bool arc_;
};

// ---------------------------------------------------------------------------
// Splines (fit point / control point)
// ---------------------------------------------------------------------------

class SplineTool : public ClickTool {
 public:
  explicit SplineTool(bool fit)
      : ClickTool(fit ? ToolInfo{.id = "sketch.spline-fit",
                                 .name = "Fit Point Spline",
                                 .tooltip = "Spline through the clicked points; double-click, Enter or a click on the first point ends it",
                                 .icon = "sketch-spline-fit",
                                 .aliases = {"SPLINE", "SPL"},
                                 .tab = "SKETCH",
                                 .group = "CREATE",
                                 .order = 13}
                      : ToolInfo{.id = "sketch.spline-control",
                                 .name = "Control Point Spline",
                                 .tooltip = "Spline from control points (argument: degree, default 3); double-click or Enter ends it",
                                 .icon = "sketch-spline-control",
                                 .aliases = {"CSPLINE", "CSPL"},
                                 .tab = "SKETCH",
                                 .group = "CREATE",
                                 .order = 13}),
        fit_(fit) {}

  void activate(ToolContext& ctx) override {
    degree_ = 3;
    if (!fit_ && !ctx.args.empty()) {
      try {
        degree_ = std::clamp(std::stoi(ctx.args[0]), 1, 9);
      } catch (...) {
      }
    }
    ClickTool::activate(ctx);
  }

  bool mouseDoubleClick(ToolContext& ctx, const ViewerMouseEvent&) override {
    finishSpline(ctx);
    return true;
  }

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
      if (pts_.size() >= 2) finishSpline(ctx);
      else ctx.finish();
      return true;
    }
    if (e.key == Qt::Key_Escape) {
      if (pts_.size() >= 2) finishSpline(ctx);
      else if (!pts_.empty()) {
        pts_.clear();
        clearTemp();
        ctx.setPrompt(prompt());
      } else {
        cancel(ctx);
      }
      return true;
    }
    return SketchTool::keyPress(ctx, e);
  }

  void accept(ToolContext& ctx) override {
    if (pts_.size() >= 2) finishSpline(ctx);
    ctx.finish();
  }

 protected:
  std::string prompt() const override {
    if (pts_.empty()) return fit_ ? "Specify the first fit point" : "Specify the first control point";
    return std::format("Specify point {} (double-click / Enter to finish)", pts_.size() + 1);
  }

  /// Clicking the first point again closes the spline (fit splines end there).
  bool complete() const override {
    if (pts_.size() < 3) return false;
    return pts_.back().point.near(pts_.front().point, 1e-9);
  }

  Geometry build(const std::vector<Pt2>& p) const {
    return fit_ ? Geometry::makeFitSpline(p) : Geometry::makeControlSpline(p, degree_);
  }

  void preview(ToolContext&, Pt2 c) override {
    std::vector<Pt2> p = points();
    if (p.empty()) return;
    p.push_back(c);
    if (!fit_) previewPolyline(p, guideStyle());
    if (p.size() >= 2 && !p[p.size() - 1].near(p[p.size() - 2], 1e-9)) previewGeometry(build(p), rubberStyle());
  }

  void create(ToolContext& ctx) override { finishSpline(ctx); }

  /// The first point of the spline being drawn is not sketch geometry yet, so ordinary snapping
  /// does not see it: with 3+ points, a cursor within snap tolerance of it lands exactly on it,
  /// and that click closes the spline (SK-04).
  InferenceResult inferAt(Pt2 c, const ViewerMouseEvent& e) override {
    InferenceResult r = ClickTool::inferAt(c, e);
    if (pts_.size() >= 3 && !snapSuppressed(e)) {
      Pt2 first = pts_.front().point;
      if (c.distance(first) <= tol(c) || r.point.distance(first) <= tol(c)) {
        r = InferenceResult{};
        r.point = first;
        r.glyph = InferenceGlyph::Coincident;
      }
    }
    return r;
  }

 private:
  void finishSpline(ToolContext& ctx) {
    Sketch* sk = sketch();
    if (!sk || !session() || pts_.size() < 2) {
      pts_.clear();
      return;
    }
    std::vector<Pt2> p = points();
    bool closed = p.size() >= 3 && p.back().near(p.front(), 1e-9);
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(build(p)));
    const Geometry* g = sk->geometry(id);
    int n = g ? g->vertexCount() : 0;
    // coincidences inferred at the clicked points
    for (size_t i = 0; i < pts_.size() && n > 0; ++i) {
      if (!pts_[i].onPoint) continue;
      GeoRef ref{id, PointPos::Vertex, static_cast<int>(i)};
      if (i == 0) ref = {id, PointPos::Start};
      else if (static_cast<int>(i) == n - 1) ref = {id, PointPos::End};
      if (closed && static_cast<int>(i) == n - 1) continue;
      sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = ref, .b = *pts_[i].onPoint});
    }
    if (closed) sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {id, PointPos::End}, .b = {id, PointPos::Start}});
    session()->commitEdit(fit_ ? "Fit Point Spline" : "Control Point Spline");
    pts_.clear();
    clearTemp();
    ctx.setPrompt(prompt());
  }

  bool fit_;
  int degree_ = 3;
};

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

class TextTool : public SketchTool {
 public:
  TextTool()
      : SketchTool(ToolInfo{.id = "sketch.text",
                            .name = "Text",
                            .tooltip = "Sketch text: click the baseline start, type the text (arguments: text, height, angle, font). "
                                       "Closed glyphs become extrudable profiles",
                            .icon = "sketch-text",
                            .aliases = {"SKTEXT", "STEXT"},
                            .tab = "SKETCH",
                            .group = "CREATE",
                            .order = 16}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    anchor_.reset();
    text_.clear();
    angle_ = 0;
    if (ctx.args.size() > 0) text_ = ctx.args[0];
    if (ctx.args.size() > 1)
      if (auto v = evaluateTyped(ctx, ctx.args[1], false); v && *v > 0) height_ = lastHeight_ = *v;
    if (ctx.args.size() > 2)
      if (auto v = evaluateTyped(ctx, ctx.args[2], true)) angle_ = *v * kPi / 180.0;
    if (ctx.args.size() > 3) font_ = ctx.args[3];
    edit_ = nullptr;
    ctx.setPrompt(prompt());
  }

  void deactivate(ToolContext& ctx) override {
    edit_ = nullptr;
    SketchTool::deactivate(ctx);
  }

  /// Typing in the viewport goes into the text (SK-08): printable keys append, Backspace deletes;
  /// they never reach the shortcut router.
  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
      accept(ctx);
      return true;
    }
    if (!(e.modifiers & (core::ModCtrl | core::ModAlt))) {
      if (e.key == Qt::Key_Backspace) {
        if (!text_.empty()) {
          // drop one UTF-8 character
          size_t n = text_.size() - 1;
          while (n > 0 && (static_cast<unsigned char>(text_[n]) & 0xC0) == 0x80) --n;
          text_.erase(n);
        }
        textChanged(ctx);
        return true;
      }
      bool printable = !e.text.empty();
      for (char ch : e.text)
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f) printable = false;
      if (printable) {
        text_ += e.text;
        textChanged(ctx);
        return true;
      }
    }
    return SketchTool::keyPress(ctx, e);
  }

  QWidget* createDialog(ToolContext& ctx, QWidget* parent) override {
    auto* w = new QWidget(parent);
    auto* form = new QFormLayout(w);
    auto* edit = new QLineEdit(QString::fromStdString(text_), w);
    edit->setObjectName("sketchTextEdit");
    edit->setPlaceholderText("Text");
    auto* height = new QDoubleSpinBox(w);
    height->setObjectName("sketchTextHeight");
    height->setRange(0.01, 10000);
    height->setDecimals(3);
    height->setValue(height_);
    auto* font = new QFontComboBox(w);
    font->setObjectName("sketchTextFont");
    font->setCurrentFont(QFont(QString::fromStdString(font_.empty() ? defaultTextFont() : font_)));
    form->addRow("Text", edit);
    form->addRow("Height", height);
    form->addRow("Font", font);
    QObject::connect(edit, &QLineEdit::textChanged, w, [this](const QString& t) {
      text_ = t.toStdString();
      refreshPreview();
    });
    // Enter in the field places the text (once positioned), instead of re-running an older
    // command-line entry (SK-09)
    core::ToolManager* mgr = ctx.manager;
    QObject::connect(edit, &QLineEdit::returnPressed, w, [this, mgr] {
      if (!mgr || !mgr->isActive() || mgr->activeToolId() != info().id) return;
      if (anchor_ && !text_.empty()) mgr->runCommandLine(text_);
    });
    edit_ = edit;
    QObject::connect(height, &QDoubleSpinBox::valueChanged, w, [this](double v) {
      height_ = lastHeight_ = v;
      refreshPreview();
    });
    QObject::connect(font, &QFontComboBox::currentFontChanged, w, [this](const QFont& f) {
      font_ = f.family().toStdString();
      refreshPreview();
    });
    return w;
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c) return false;
    if (!anchor_) {
      InferenceInput in;
      in.cursor = *c;
      cursorInf_ = pointsOnly(infer(in, snapSuppressed(e)));
      cursor_ = cursorInf_.point;
    }
    refreshPreview();
    if (!anchor_) showInference(cursorInf_);
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c) return true;
    InferenceInput in;
    in.cursor = *c;
    cursorInf_ = pointsOnly(infer(in, snapSuppressed(e)));
    place(ctx, cursorInf_);
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    if (!anchor_) {
      TypedInput in = parseTypedInput(text);
      if (in.kind == TypedInput::Kind::Pair) {
        const double f = lengthFactor(document());
        place(ctx, inferExact({in.a * f, in.b * f}));
        return true;
      }
      text_ = text;  // text first, then click the position
      textChanged(ctx);
      return true;
    }
    text_ = text;
    create(ctx);
    return true;
  }

  void accept(ToolContext& ctx) override {
    if (anchor_ && !text_.empty()) create(ctx);
    ctx.finish();
  }

 private:
  std::string prompt() const {
    if (!anchor_) return text_.empty() ? "Specify the text position (baseline start)" : "Specify the position of \"" + text_ + "\"";
    return "Type the text";
  }

  Geometry build(Pt2 at) const {
    Geometry g = Geometry::makeText(at, text_.empty() ? std::string("Text") : text_, height_, angle_, font_);
    return g;
  }

  /// One source for the text: the dialog field follows text typed elsewhere.
  void textChanged(ToolContext& ctx) {
    if (edit_ && edit_->text().toStdString() != text_) {
      QSignalBlocker block(edit_.data());
      edit_->setText(QString::fromStdString(text_));
    }
    refreshPreview();
    ctx.setPrompt(prompt());
  }

  void refreshPreview() {
    clearTemp();
    if (!session()) return;
    previewGeometry(build(anchor_.value_or(cursor_)), rubberStyle());
  }

  void place(ToolContext& ctx, const InferenceResult& r) {
    anchor_ = r.point;
    anchorInf_ = r;
    if (!text_.empty()) {
      create(ctx);
      return;
    }
    ctx.setPrompt(prompt());
  }

  void create(ToolContext& ctx) {
    Sketch* sk = sketch();
    if (!sk || !session() || !anchor_ || text_.empty()) return;
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(build(*anchor_)));
    if (anchorInf_.onPoint) sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {id, PointPos::Start}, .b = *anchorInf_.onPoint});
    session()->commitEdit("Text");
    anchor_.reset();
    text_.clear();
    clearTemp();
    if (edit_) {
      QSignalBlocker block(edit_.data());
      edit_->clear();
    }
    ctx.setPrompt(prompt());
  }

  inline static double lastHeight_ = 5.0;
  double height_ = lastHeight_;
  double angle_ = 0;
  std::string font_;
  std::string text_;
  std::optional<Pt2> anchor_;
  InferenceResult anchorInf_, cursorInf_;
  Pt2 cursor_;
  QPointer<QLineEdit> edit_;  ///< the dialog's Text field (rebuilt with the dialog)
};

}  // namespace

void registerCurveTools(core::ToolRegistry& reg) {
  reg.registerTool(std::make_unique<EllipseTool>(false));
  reg.registerTool(std::make_unique<EllipseTool>(true));
  reg.registerTool(std::make_unique<SplineTool>(true));
  reg.registerTool(std::make_unique<SplineTool>(false));
  reg.registerTool(std::make_unique<TextTool>());
}

}  // namespace fabricad::sketch
