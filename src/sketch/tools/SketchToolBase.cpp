// FabriCAD sketch tools: SketchTool base implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/tools/SketchToolBase.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Expression.h"
#include "core/Units.h"

#include <QtCore/Qt>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace fabricad::sketch {

using core::PreviewStyle;
using core::Vec3;

// ---------------------------------------------------------------------------
// typed input
// ---------------------------------------------------------------------------

namespace {
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool parseNumber(const std::string& s, double& out) {
  std::string t = trim(s);
  if (t.empty()) return false;
  char* end = nullptr;
  out = std::strtod(t.c_str(), &end);
  return end && *end == '\0';
}
}  // namespace

TypedInput parseTypedInput(const std::string& raw) {
  TypedInput in;
  std::string s = trim(raw);
  in.text = s;
  if (s.empty()) return in;
  bool relative = s[0] == '@';
  std::string body = relative ? s.substr(1) : s;
  size_t lt = body.find('<');
  size_t comma = body.find(',');
  double a = 0, b = 0;
  if (relative && lt != std::string::npos) {
    if (parseNumber(body.substr(0, lt), a) && parseNumber(body.substr(lt + 1), b)) {
      in.kind = TypedInput::Kind::Polar;
      in.a = a;
      in.b = b;
      return in;
    }
  } else if (comma != std::string::npos) {
    if (parseNumber(body.substr(0, comma), a) && parseNumber(body.substr(comma + 1), b)) {
      in.kind = relative ? TypedInput::Kind::Relative : TypedInput::Kind::Pair;
      in.a = a;
      in.b = b;
      return in;
    }
  } else if (!relative && parseNumber(body, a)) {
    in.kind = TypedInput::Kind::Number;
    in.a = a;
    return in;
  }
  in.kind = TypedInput::Kind::Text;
  return in;
}

std::optional<double> evaluateTyped(const core::ToolContext& ctx, const std::string& text, bool angle, std::string* error) {
  std::string s = trim(text);
  if (s.empty()) {
    if (error) *error = "empty value";
    return std::nullopt;
  }
  core::ExpressionContext ectx;
  if (ctx.document) ectx = ctx.document->expressionContext();
  core::ParameterLookup docLookup = ectx.lookup;
  const Sketch* sk = SketchSession::current() ? SketchSession::current()->sketch() : nullptr;
  ectx.lookup = [sk, docLookup](const std::string& name) -> std::optional<core::Quantity> {
    if (sk) {
      if (const Constraint* d = sk->dimensionByName(name)) {
        double v = d->value;
        if (d->driven)
          if (auto m = sk->measure(*d)) v = *m;
        return d->type == ConstraintType::Angle ? core::Quantity::deg(v) : core::Quantity::mm(v);
      }
    }
    return docLookup ? docLookup(name) : std::nullopt;
  };
  core::EvalResult r = core::evaluateExpression(s, ectx, angle ? core::Dimension::Angle() : core::Dimension::Length());
  if (!r.ok) {
    if (error) *error = r.error;
    return std::nullopt;
  }
  return r.value.value;
}

std::optional<Geometry> tangentArc(Pt2 p, Pt2 t, Pt2 q, bool* startIsP) {
  Pt2 d = q - p;
  double len = d.length();
  if (len < 1e-9) return std::nullopt;
  Pt2 tn = t.normalized();
  Pt2 n = tn.perp();
  double denom = 2.0 * d.dot(n);
  if (std::abs(denom) < 1e-12 * std::max(1.0, len)) return std::nullopt;  // q on the tangent line
  double s = d.dot(d) / denom;
  Pt2 c = p + n * s;
  double r = std::abs(s);
  double a0 = (p - c).angle(), a1 = (q - c).angle();
  bool ccw = tn.cross(d) > 0;
  Geometry arc = ccw ? Geometry::makeArc(c, r, a0, a1) : Geometry::makeArc(c, r, a1, a0);
  if (startIsP) *startIsP = ccw;
  return arc;
}

// ---------------------------------------------------------------------------
// SketchTool
// ---------------------------------------------------------------------------

bool SketchTool::isEnabled(const core::ToolContext& ctx) const {
  return ctx.document != nullptr && SketchSession::current() != nullptr && SketchSession::current()->document() == ctx.document;
}

void SketchTool::activate(core::ToolContext& ctx) {
  viewer_ = ctx.viewer;
  if (viewer_) viewer_->setCursor(core::CursorShape::Cross);
}

void SketchTool::deactivate(core::ToolContext& ctx) {
  clearTemp();
  (void)ctx;
}

bool SketchTool::keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) {
  if (e.key == Qt::Key_Escape) {
    cancel(ctx);
    return true;
  }
  if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
    accept(ctx);
    return true;
  }
  return false;
}

Sketch* SketchTool::sketch() const { return session() ? session()->sketch() : nullptr; }
SketchFeature* SketchTool::feature() const { return session() ? session()->feature() : nullptr; }
core::Document* SketchTool::document() const { return session() ? session()->document() : nullptr; }

std::optional<Pt2> SketchTool::cursor(const core::ViewerMouseEvent& e) const {
  return session() ? session()->cursor(e) : std::nullopt;
}

double SketchTool::tol(Pt2 at, double px) const { return session() ? session()->tolerance(at, px) : px; }

bool SketchTool::snapSuppressed(const core::ViewerMouseEvent& e) const {
  if (e.modifiers & core::ModCtrl) return true;
  return session() && !session()->snapEnabled();
}

InferenceResult SketchTool::infer(InferenceInput in, bool suppress) const {
  const Sketch* sk = sketch();
  if (!sk) {
    InferenceResult r;
    r.point = in.cursor;
    return r;
  }
  in.tolerance = tol(in.cursor);
  in.suppress = suppress;
  return sketch::infer(*sk, in);
}

InferenceResult SketchTool::inferExact(Pt2 p, std::optional<Pt2> from) const {
  const Sketch* sk = sketch();
  InferenceResult r;
  r.point = p;
  if (!sk) return r;
  InferenceInput in;
  in.cursor = p;
  in.from = from;
  in.tolerance = 1e-6;
  in.allowAlignment = false;
  return sketch::infer(*sk, in);
}

std::optional<SketchHit> SketchTool::hitAt(Pt2 p, double px) const {
  const Sketch* sk = sketch();
  if (!sk) return std::nullopt;
  return sk->hit(p, tol(p, px));
}

// --- previews --------------------------------------------------------------------------------

void SketchTool::clearTemp() {
  if (viewer_)
    for (core::PreviewHandle h : temp_) viewer_->removePreview(h);
  temp_.clear();
}

namespace {
std::vector<Vec3> toWorld(const SketchSession* s, const std::vector<Pt2>& pts) {
  std::vector<Vec3> out;
  out.reserve(pts.size());
  for (const Pt2& p : pts) out.push_back(s->toWorld(p));
  return out;
}
}  // namespace

void SketchTool::previewPolyline(const std::vector<Pt2>& pts, const PreviewStyle& st) {
  if (!viewer_ || !session() || pts.size() < 2) return;
  core::PreviewHandle h = viewer_->showPreviewPolyline(toWorld(session(), pts), st);
  if (h != core::kInvalidPreview) temp_.insert(h);
}

void SketchTool::previewLines(const std::vector<Pt2>& pts, const PreviewStyle& st) {
  if (!viewer_ || !session() || pts.size() < 2) return;
  core::PreviewHandle h = viewer_->showPreviewLines(toWorld(session(), pts), st);
  if (h != core::kInvalidPreview) temp_.insert(h);
}

void SketchTool::previewPoints(const std::vector<Pt2>& pts, const PreviewStyle& st) {
  if (!viewer_ || !session() || pts.empty()) return;
  core::PreviewHandle h = viewer_->showPreviewPoints(toWorld(session(), pts), st);
  if (h != core::kInvalidPreview) temp_.insert(h);
}

void SketchTool::previewText(Pt2 at, const std::string& text, const PreviewStyle& st) {
  if (!viewer_ || !session() || text.empty()) return;
  core::PreviewHandle h = viewer_->showPreviewText(session()->toWorld(at), text, st);
  if (h != core::kInvalidPreview) temp_.insert(h);
}

void SketchTool::previewGeometry(const Geometry& g, const PreviewStyle& st) {
  if (g.type == GeomType::Point) {
    previewPoints({g.center()}, st);
    return;
  }
  previewPolyline(g.tessellate(g.type == GeomType::Line ? 1 : 64), st);
}

void SketchTool::showInference(const InferenceResult& r) {
  if (!session()) return;
  const double off = tol(r.point, 12.0);
  for (const auto& [a, b] : r.guides) previewLines({a, b}, guideStyle());
  if (r.onPoint || r.onCurve) previewPoints({r.point}, markerStyle());
  const char* glyph = inferenceGlyphText(r.glyph);
  if (glyph && *glyph) previewText(r.point + Pt2{off, off}, glyph, glyphStyle());
}

void SketchTool::showDynamicText(Pt2 near, const std::string& text) {
  if (!session()) return;
  const double off = tol(near, 16.0);
  PreviewStyle st;
  st.color = {0.15f, 0.15f, 0.15f, 1.0f};
  st.textHeight = 12.0f;
  st.onTop = true;
  previewText(near + Pt2{off, -off}, text, st);
}

// --- styles --------------------------------------------------------------------------------------

PreviewStyle SketchTool::rubberStyle() { return PreviewStyle::sketchLine(); }

PreviewStyle SketchTool::guideStyle() { return PreviewStyle::inference(); }

PreviewStyle SketchTool::glyphStyle() {
  PreviewStyle s;
  s.color = {0.25f, 0.25f, 0.25f, 1.0f};
  s.textHeight = 11.0f;
  s.onTop = true;
  return s;
}

PreviewStyle SketchTool::markerStyle() {
  PreviewStyle s;
  s.color = {0.95f, 0.55f, 0.1f, 1.0f};
  s.pointSize = 9.0f;
  s.onTop = true;
  return s;
}

PreviewStyle SketchTool::highlightStyle() {
  PreviewStyle s;
  s.color = {0.95f, 0.55f, 0.1f, 1.0f};
  s.lineWidth = 3.0f;
  s.shaded = false;
  s.onTop = true;
  return s;
}

PreviewStyle SketchTool::removeStyle() {
  PreviewStyle s;
  s.color = colors::kTrim;
  s.lineWidth = 3.5f;
  s.shaded = false;
  s.onTop = true;
  return s;
}

PreviewStyle SketchTool::constructionStyle() {
  PreviewStyle s;
  s.color = colors::kConstruction;
  s.lineWidth = 1.5f;
  s.shaded = false;
  s.dashed = true;
  s.onTop = true;
  return s;
}

// --- helpers --------------------------------------------------------------------------------------

Geometry SketchTool::asCreated(Geometry g) const {
  if (session() && session()->constructionMode()) g.construction = true;
  return g;
}

std::string SketchTool::formatLength(double mm) const {
  core::UnitSystem u = document() ? document()->units() : core::UnitSystem{};
  return core::formatQuantity(core::Quantity::mm(mm), u, true);
}

std::string SketchTool::formatAngle(double deg) const {
  core::UnitSystem u = document() ? document()->units() : core::UnitSystem{};
  return core::formatQuantity(core::Quantity::deg(deg), u, false) + "°";
}

int SketchTool::addCheckedConstraint(core::ToolContext& ctx, Constraint c) {
  Sketch* sk = sketch();
  if (!sk) return -1;
  size_t before = sk->constraints().size();
  int id = sk->addConstraint(c);
  if (sk->constraints().size() == before) return id;  // already existed
  SolveResult r = sk->solve();
  bool bad = std::count(r.conflicting.begin(), r.conflicting.end(), id) > 0 || r.status == SolveResult::Status::Failed;
  if (!bad && r.status == SolveResult::Status::Conflicting) bad = true;
  if (bad) {
    sk->removeConstraint(id);
    sk->solve();
    ctx.showMessage(std::format("{} constraint conflicts with existing constraints", constraintTypeName(c.type)), true);
    return -1;
  }
  if (std::count(r.redundant.begin(), r.redundant.end(), id) > 0)
    ctx.showMessage(std::format("{} constraint is redundant", constraintTypeName(c.type)));
  return id;
}

}  // namespace fabricad::sketch
