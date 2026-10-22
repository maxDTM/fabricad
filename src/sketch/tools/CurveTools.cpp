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
    ctx.setPrompt(prompt());
  }

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

 protected:
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

  void preview(ToolContext&, Pt2 c) override {
    std::vector<Pt2> p = points();
    if (p.empty()) return;
    if (p.size() == 1) {
      previewLines({p[0], c}, guideStyle());
      showDynamicText(c, "a " + formatLength(p[0].distance(c)));
      return;
    }
    p.push_back(c);
    if (p.size() == 3 && arc_) {
      // show the full ellipse dashed while picking the start point
      std::vector<Pt2> q = p;
      if (auto b = minorFor(q[0], q[1], q[2])) {
        previewGeometry(Geometry::makeEllipse(q[0], q[0].distance(q[1]), *b, (q[1] - q[0]).angle()), guideStyle());
        showDynamicText(c, "b " + formatLength(*b));
      }
      return;
    }
    if (auto g = build(p)) {
      previewGeometry(*g, rubberStyle());
      if (!arc_) showDynamicText(c, std::format("{} x {}", formatLength(g->majorRadius()), formatLength(g->minorRadius())));
    }
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
    ctx.setPrompt(prompt());
  }

  QWidget* createDialog(ToolContext&, QWidget* parent) override {
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
      ctx.setPrompt(prompt());
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
