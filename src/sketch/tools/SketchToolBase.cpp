// FabriCAD sketch tools: SketchTool base implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/tools/SketchToolBase.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Expression.h"
#include "core/Units.h"
#include "sketch/ValueInputOverlay.h"

#include <QWidget>
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

namespace {
std::optional<double> evaluateTypedIn(core::Document* doc, const std::string& text, core::Dimension dim, std::string* error) {
  std::string s = trim(text);
  if (s.empty()) {
    if (error) *error = "empty value";
    return std::nullopt;
  }
  core::ExpressionContext ectx;
  if (doc) ectx = doc->expressionContext();
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
  core::EvalResult r = core::evaluateExpression(s, ectx, dim);
  if (!r.ok) {
    if (error) *error = r.error;
    return std::nullopt;
  }
  return r.value.value;
}
}  // namespace

std::optional<double> evaluateTyped(const core::ToolContext& ctx, const std::string& text, bool angle, std::string* error) {
  return evaluateTypedIn(ctx.document, text, angle ? core::Dimension::Angle() : core::Dimension::Length(), error);
}

ValueInputModel::Evaluator makeSketchEvaluator(core::Document* doc) {
  return [doc](const std::string& text, ValueKind kind, std::string* error) -> std::optional<double> {
    core::Dimension dim = core::Dimension::Scalar();
    if (kind == ValueKind::Length) dim = core::Dimension::Length();
    else if (kind == ValueKind::Angle) dim = core::Dimension::Angle();
    return evaluateTypedIn(doc, text, dim, error);
  };
}

ValueInputModel::Formatter makeUnitFormatter(core::Document* doc) {
  return [doc](double v, ValueKind kind) -> std::string {
    core::UnitSystem u = doc ? doc->units() : core::UnitSystem{};
    switch (kind) {
      case ValueKind::Length: return core::formatQuantity(core::Quantity::mm(v), u, true);
      case ValueKind::Angle: return core::formatQuantity(core::Quantity::deg(v), u, false) + "°";
      case ValueKind::Count: return std::to_string(static_cast<long long>(std::llround(v)));
      case ValueKind::Scalar: return std::format("{:.3g}", v);
    }
    return {};
  };
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

SketchTool::SketchTool(core::ToolInfo info) : ToolBase(std::move(info)) {}

SketchTool::~SketchTool() = default;

bool SketchTool::isEnabled(const core::ToolContext& ctx) const {
  return ctx.document != nullptr && SketchSession::current() != nullptr && SketchSession::current()->document() == ctx.document;
}

void SketchTool::activate(core::ToolContext& ctx) {
  viewer_ = ctx.viewer;
  manager_ = ctx.manager;
  if (viewer_) viewer_->setCursor(core::CursorShape::Cross);
  overlay_.reset();
  values_.clear();
  values_.setEvaluator(makeSketchEvaluator(ctx.document));
  values_.setFormatter(makeUnitFormatter(ctx.document));
  values_.setChangedCallback([this] {
    if (overlay_) overlay_->sync();
  });
}

void SketchTool::deactivate(core::ToolContext& ctx) {
  clearTemp();
  overlay_.reset();
  values_.setChangedCallback({});
  values_.clear();
  (void)ctx;
}

// --- value boxes --------------------------------------------------------------------------------

void SketchTool::defineValues(std::vector<ValueField> fields) {
  values_.clear();
  for (ValueField& f : fields) values_.addField(std::move(f));
  QWidget* parent = viewer_ ? viewer_->widget() : nullptr;
  if (parent && !overlay_) {
    overlay_ = std::make_unique<ValueInputOverlay>(values_, viewer_, parent);
    core::ToolManager* mgr = manager_;
    overlay_->setKeyForwarder([mgr](int key, int mods, const std::string& text) {
      if (!mgr) return;
      core::ViewerKeyEvent ev;
      ev.key = key;
      ev.modifiers = mods ? core::ModShift : core::ModNone;
      ev.text = text;
      mgr->keyPress(ev);
    });
  }
  if (overlay_) overlay_->sync();
}

void SketchTool::setValue(const std::string& id, double value, Pt2 anchor) {
  values_.setMeasured(id, value);
  if (session()) values_.setAnchor(id, session()->toWorld(anchor));
}

void SketchTool::setValueVisible(const std::string& id, bool visible) { values_.setVisible(id, visible); }

void SketchTool::showValueBoxes(Pt2 near) {
  if (values_.empty() || values_.visibleCount() == 0) return;
  if (overlay_ && overlay_->hasWidgets()) {
    overlay_->setVisible(true);
    return;
  }
  showDynamicText(near, values_.summary());
}

bool SketchTool::typedValuesToBoxes(core::ToolContext& ctx, const std::string& text) {
  if (values_.visibleCount() == 0) return false;
  // split "30,20" / "50<30" at top-level separators into consecutive visible boxes
  std::vector<std::string> parts;
  std::string cur;
  int depth = 0;
  for (char c : text) {
    if (c == '(') ++depth;
    else if (c == ')') --depth;
    if ((c == ',' || c == '<') && depth <= 0) {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  parts.push_back(cur);
  std::vector<int> targets;
  if (parts.size() == 1) {
    if (values_.activeIndex() < 0) return false;
    targets.push_back(values_.activeIndex());
  } else {
    for (int i = 0; i < values_.fieldCount() && targets.size() < parts.size(); ++i)
      if (values_.field(i).visible) targets.push_back(i);
    if (targets.size() < parts.size()) return false;
  }
  for (size_t i = 0; i < parts.size(); ++i) {
    std::string err;
    if (trim(parts[i]).empty()) continue;
    if (!values_.lock(values_.field(targets[i]).id, parts[i], &err)) {
      values_.unlockAll();
      ctx.showMessage(err, true);
      return true;
    }
  }
  if (!commitValues(ctx)) {
    values_.unlockAll();
    return false;
  }
  return true;
}

int SketchTool::addLockedDimension(core::ToolContext& ctx, const ValueField& f, Constraint c) {
  Sketch* sk = sketch();
  if (!sk) return -1;
  c.value = f.value;
  TypedInput ti = parseTypedInput(f.text);
  c.expression = (f.text.empty() || ti.kind == TypedInput::Kind::Number) ? std::string() : trim(f.text);
  int id = sk->addConstraint(c);
  SolveResult r = sk->solve();
  bool bad = !r.ok() || std::count(r.conflicting.begin(), r.conflicting.end(), id) > 0 ||
             std::count(r.redundant.begin(), r.redundant.end(), id) > 0;
  if (bad) {
    if (Constraint* cc = sk->constraint(id)) cc->driven = true;
    sk->solve();
    ctx.showMessage(std::format("{} {} would over-constrain the sketch: added as a driven dimension", f.label, f.text.empty() ? std::to_string(f.value) : f.text),
                    true);
  }
  return id;
}

bool SketchTool::addRightAngleInstead(core::ToolContext& ctx, int lineId, double angleDeg, int referenceLine) {
  Sketch* sk = sketch();
  if (!sk) return false;
  double a = std::fmod(angleDeg, 360.0);
  if (a < 0) a += 360.0;
  const double eps = 1e-6;
  auto nearTo = [&](double target) { return std::abs(a - target) < eps || std::abs(a - target - 360.0) < eps; };
  bool straight = nearTo(0) || nearTo(180);
  bool right = nearTo(90) || nearTo(270);
  if (!straight && !right) return false;
  Constraint c;
  if (referenceLine < 0) {
    c.type = straight ? ConstraintType::Horizontal : ConstraintType::Vertical;
    c.a = {lineId};
  } else {
    c.type = straight ? ConstraintType::Parallel : ConstraintType::Perpendicular;
    c.a = {lineId};
    c.b = {referenceLine};
  }
  addCheckedConstraint(ctx, c);
  return true;
}

bool SketchTool::handleValueKey(core::ToolContext& ctx, const core::ViewerKeyEvent& e) {
  if (values_.empty() || values_.visibleCount() == 0) return false;
  switch (values_.keyPress(e.key, e.modifiers, e.text)) {
    case ValueInputModel::KeyResult::Consumed:
      valuesChanged(ctx);
      if (overlay_) overlay_->sync();
      return true;
    case ValueInputModel::KeyResult::Commit:
      if (commitValues(ctx)) return true;
      values_.unlockAll();
      return false;  // the tool's default Enter handling applies
    case ValueInputModel::KeyResult::Ignored: break;
  }
  return false;
}

bool SketchTool::keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) {
  if (handleValueKey(ctx, e)) return true;
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
  for (const auto& pl : g.polylines(g.type == GeomType::Line ? 1 : 64)) previewPolyline(pl, st);
}

void SketchTool::showInference(const InferenceResult& r) {
  if (!session()) return;
  const double off = tol(r.point, 12.0);
  for (const auto& [a, b] : r.guides) previewLines({a, b}, guideStyle());
  if (r.onPoint || r.onCurve) previewPoints({r.point}, markerStyle());
  // the glyph is drawn from line primitives like the constraint glyphs (UX-28)
  const GlyphShape shape = glyphShapeFor(r.glyph);
  if (shape == GlyphShape::None) return;
  const Pt2 at = r.point + Pt2{off, off};
  const double half = tol(at, 6.0);
  std::vector<Pt2> segs;
  for (const Pt2& q : glyphSegments(shape)) segs.push_back(at + q * half);
  PreviewStyle st = glyphStyle();
  st.shaded = false;
  st.lineWidth = 1.0f;
  previewLines(segs, st);
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
