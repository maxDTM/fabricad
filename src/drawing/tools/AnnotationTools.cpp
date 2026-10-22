// FabriCAD drawing: annotation tools implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/tools/AnnotationTools.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/UndoStack.h"
#include "drawing/Sheet.h"
#include "drawing/SheetRenderer.h"
#include "drawing/tools/ViewTools.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace fabricad::drawing {

using core::ObjectId;

namespace {

std::vector<std::string> split(const std::string& line) {
  std::istringstream in(line);
  std::vector<std::string> out;
  std::string t;
  while (in >> t) out.push_back(t);
  return out;
}

/// Geometry hit for tools, with or without a canvas (headless: 1 mm tolerance over all views).
CanvasHit hitGeometry(DrawingCanvas* canvas, const Sheet& sheet, const Point2& at) {
  if (canvas) {
    CanvasHit h = canvas->hitTest(at, true);
    if (h.hasGeometry()) return h;
    // an annotation may sit on top of the geometry: search the views directly below
  }
  const double tolSheet = canvas ? canvas->pickTolerance() : 1.0;
  CanvasHit best;
  best.sheetPos = at;
  double bestD = 1e300;
  for (const auto& v : sheet.views) {
    if (!v.geometryValid) continue;
    Point2 local = v.fromSheet(at);
    double tol = tolSheet / std::max(1e-9, v.scale);
    size_t idx = 0;
    const Curve2D* c = v.pickCurve(local, tol, &idx);
    const Vertex2D* vx = v.pickVertex(local, tol * 1.2);
    if (!c && !vx) continue;
    double d = c ? c->distanceTo(local) * v.scale : vx->p.distance(local) * v.scale;
    if (d < bestD) {
      bestD = d;
      best = CanvasHit{};
      best.kind = HitKind::View;
      best.id = best.viewId = v.id;
      best.sheetPos = at;
      best.viewLocal = local;
      if (c && c->cls != LineClass::Hatch && c->cls != LineClass::Detail) {
        best.onCurve = true;
        best.curveIndex = idx;
        best.curve = *c;
      }
      if (vx) {
        best.onVertex = true;
        best.vertex = *vx;
      }
    }
  }
  return best;
}

bool parseDimType(const std::string& text, DimensionType& out) {
  std::string t = DrawingToolBase::lower(text);
  static const DimensionType all[] = {DimensionType::Linear,  DimensionType::Horizontal, DimensionType::Vertical,
                                      DimensionType::Aligned, DimensionType::Radius,     DimensionType::Diameter,
                                      DimensionType::Angle};
  for (DimensionType d : all)
    if (DrawingToolBase::lower(dimensionTypeName(d)) == t) {
      out = d;
      return true;
    }
  if (t == "h" || t == "hor") { out = DimensionType::Horizontal; return true; }
  if (t == "v" || t == "ver") { out = DimensionType::Vertical; return true; }
  if (t == "r" || t == "rad") { out = DimensionType::Radius; return true; }
  if (t == "d" || t == "dia") { out = DimensionType::Diameter; return true; }
  if (t == "a" || t == "ang") { out = DimensionType::Angle; return true; }
  return false;
}

/// Axis-aligned box of a display text (rough width estimate, quarter turns only).
BBox2 textBox(const DisplayText& t) {
  const double w = SheetRenderer::textWidth(t.text, t.height), h = t.height;
  const bool vertical = std::abs(std::remainder(t.angle, 180.0)) > 45.0;
  const double bw = vertical ? h : w, bh = vertical ? w : h;
  double x0 = t.pos.x - bw / 2, y0 = t.pos.y - bh / 2;
  if (!vertical) {
    if (t.halign == HAlign::Left) x0 = t.pos.x;
    else if (t.halign == HAlign::Right) x0 = t.pos.x - bw;
    if (t.valign == VAlign::Top) y0 = t.pos.y;
    else if (t.valign == VAlign::Bottom) y0 = t.pos.y - bh;
  }
  BBox2 b;
  b.add(Point2{x0, y0});
  b.add(Point2{x0 + bw, y0 + bh});
  return b;
}

bool overlap(const BBox2& a, const BBox2& b) {
  return a.isValid() && b.isValid() && a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

}  // namespace

/// UX-43: move a new dimension's text outward (away from the measured geometry) until it
/// no longer overlaps section-line letters, view labels or other dimension texts.
Point2 declutterDimensionText(const DrawingModel& model, const Sheet& sheet, const DrawingView& view, Dimension d) {
  SheetRenderer r(model.style(), model.document().units());
  DisplayList obstacles;
  for (const DrawingView& v : sheet.views) r.addViewDecorations(obstacles, sheet, v);
  for (const Dimension& o : sheet.dimensions)
    if (const DrawingView* ov = sheet.view(o.viewId)) r.addDimension(obstacles, *ov, o);
  std::vector<BBox2> boxes;
  for (const DisplayText& t : obstacles.texts) boxes.push_back(textBox(t).expanded(0.5));
  if (boxes.empty()) return d.textPos;
  updateDimension(d, view);
  DimGeometry g;
  Point2 dir{0, -1};
  if (measureDimension(d, view, g)) {
    const Point2 mid = d.type == DimensionType::Angle ? view.toSheet(g.apex) : (view.toSheet(g.p1) + view.toSheet(g.p2)) * 0.5;
    if (const Point2 off = d.textPos - mid; off.length() > 1e-6) dir = off.normalized();
  }
  const double step = model.style().dimTextHeight * 1.5;
  const Point2 start = d.textPos;
  for (int i = 0; i < 12; ++i) {
    DisplayList dl;
    r.addDimension(dl, view, d);
    bool clear = true;
    for (const DisplayText& t : dl.texts)
      for (const BBox2& b : boxes)
        if (overlap(textBox(t), b)) clear = false;
    if (clear) return d.textPos;
    d.textPos = start + dir * (step * (i + 1));
  }
  return start;
}

// ---------------------------------------------------------------------------------------
// DimensionTool
// ---------------------------------------------------------------------------------------

DimensionTool::DimensionTool()
    : DrawingToolBase(drawingToolInfo("drawing.dimension", "Dimension",
                                      "Linear, angular, radius and diameter dimensions (type inferred from the picks)",
                                      "DIMENSIONS", 10, true, "D", {"DIMENSION", "DIM"})) {}

void DimensionTool::reset() {
  picks_.clear();
  viewId_ = core::kInvalidId;
}

void DimensionTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  reset();
  created_ = 0;
  forceType_ = false;
  haveCursor_ = false;
  if (!activeSheet(ctx)) {
    ctx.showMessage("No sheet", true);
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  bool anyPoint = false;
  for (const auto& a : ctx.args) {
    DimensionType t;
    Point2 p;
    if (parseDimType(a, t)) {
      forcedType_ = t;
      forceType_ = true;
    } else if (parsePoint(a, p)) {
      anyPoint = true;
      if (!pickAt(ctx, p) && !picks_.empty()) placeAt(ctx, p);
    }
  }
  ctx.setPrompt("Select an edge, circle or point (a second one for distances/angles), then click to place the text");
  if (anyPoint && !canvas_) ctx.finish();
}

QWidget* DimensionTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  typeCombo_ = new QComboBox(w);
  typeCombo_->addItem(tr("Auto"), -1);
  for (DimensionType d : {DimensionType::Linear, DimensionType::Horizontal, DimensionType::Vertical, DimensionType::Aligned,
                          DimensionType::Radius, DimensionType::Diameter, DimensionType::Angle})
    typeCombo_->addItem(QString::fromStdString(dimensionTypeName(d)), static_cast<int>(d));
  if (forceType_) typeCombo_->setCurrentIndex(typeCombo_->findData(static_cast<int>(forcedType_)));
  form->addRow(tr("Type"), typeCombo_);
  connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
    int d = typeCombo_ ? typeCombo_->currentData().toInt() : -1;
    forceType_ = d >= 0;
    if (forceType_) forcedType_ = static_cast<DimensionType>(d);
    refreshCanvas();
  });
  form->addRow(new QLabel(tr("Esc clears the current picks; Esc again ends the command."), w));
  return w;
}

bool DimensionTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  bool any = false;
  for (const auto& t : split(text)) {
    DimensionType d;
    Point2 p;
    if (parseDimType(t, d)) {
      forcedType_ = d;
      forceType_ = true;
      any = true;
    } else if (parsePoint(t, p)) {
      // point on geometry = pick, elsewhere = placement
      if (!pickAt(ctx, p) && !picks_.empty()) placeAt(ctx, p);
      any = true;
    }
  }
  return any;
}

bool DimensionTool::pickAt(core::ToolContext& ctx, const Point2& sheet) {
  Sheet* s = activeSheet(ctx);
  if (!s) return false;
  CanvasHit h = hitGeometry(canvas(ctx), *s, sheet);
  if (!h.hasGeometry()) return false;
  if (!picks_.empty() && h.viewId != viewId_) {
    ctx.showMessage("Both picks must be in the same view", true);
    return false;
  }
  if (picks_.size() >= 2) return false;
  viewId_ = h.viewId;
  DimPick p;
  // prefer vertices for the second pick of a point-to-point distance; curves otherwise
  const bool wantVertex = h.onVertex && (!h.onCurve || (!picks_.empty() && picks_[0].isVertex) ||
                                         (h.onCurve && h.curve.type == CurveType::Segment && h.vertex.p.distance(h.viewLocal) < 1e-9));
  if (wantVertex) {
    p.isVertex = true;
    p.vertex = h.vertex;
  } else {
    p.isVertex = false;
    p.curve = h.curve;
  }
  picks_.push_back(p);
  setCanvasHighlights({viewId_});
  ctx.setPrompt(picks_.size() == 1 ? "Select a second entity or click to place the dimension text"
                                    : "Click to place the dimension text");
  return true;
}

bool DimensionTool::buildDimension(Dimension& out, const Point2& textPos) const {
  if (picks_.empty()) return false;
  if (!inferDimension(picks_, out)) {
    // a single vertex pick cannot be dimensioned
    return false;
  }
  if (forceType_) {
    bool radial = forcedType_ == DimensionType::Radius || forcedType_ == DimensionType::Diameter;
    bool inferredRadial = out.type == DimensionType::Radius || out.type == DimensionType::Diameter;
    if (radial == inferredRadial && forcedType_ != DimensionType::Angle) out.type = forcedType_;
    else if (forcedType_ == DimensionType::Angle && out.refs.size() == 2) out.type = forcedType_;
  }
  out.viewId = viewId_;
  out.textPos = textPos;
  return true;
}

bool DimensionTool::placeAt(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  Dimension d;
  if (!buildDimension(d, sheet)) return false;
  if (const DrawingView* v = s->view(d.viewId)) d.textPos = declutterDimensionText(*m, *s, *v, d);
  ObjectId sheetId = s->id, created = core::kInvalidId;
  m->edit("Dimension", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    created = m->addDimension(*sh, d).id;
  });
  if (created == core::kInvalidId) return false;
  ++created_;
  reset();
  ctx.setPrompt("Select an edge, circle or point for the next dimension (Esc to finish)");
  return true;
}

void DimensionTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
  haveCursor_ = true;
}

void DimensionTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left || !ctx_) return;
  if (pickAt(*ctx_, sheet)) return;
  if (!picks_.empty()) placeAt(*ctx_, sheet);
}

bool DimensionTool::canvasKey(DrawingCanvas& c, int key, int modifiers) {
  (void)c; (void)modifiers;
  if (key == Qt::Key_Escape && !picks_.empty()) {
    reset();
    if (ctx_) ctx_->setPrompt("Select an edge, circle or point");
    return true;
  }
  return false;
}

void DimensionTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  const Sheet* s = c.sheet();
  if (!s) return;
  // highlight picked geometry
  if (const DrawingView* v = s->view(viewId_)) {
    for (const auto& p : picks_) {
      if (p.isVertex) {
        out.curves.push_back(Curve2D::circle(v->toSheet(p.vertex.p), 1.2, LineClass::Dimension));
      } else {
        Curve2D sc = p.curve.transformed(v->position, v->scale, true);
        sc.cls = LineClass::Visible;
        out.curves.push_back(sc);
      }
    }
    if (!picks_.empty() && haveCursor_) {
      Dimension d;
      if (buildDimension(d, cursor_) && c.model()) {
        updateDimension(d, *v);
        SheetRenderer r(c.model()->style(), c.model()->document().units());
        r.addDimension(out, *v, d);
      }
    }
  }
  if (picks_.empty() && haveCursor_) {
    CanvasHit h = c.hitTest(cursor_, true);
    if (h.hasGeometry())
      if (const DrawingView* v = s->view(h.viewId)) {
        if (h.onCurve) {
          Curve2D sc = h.curve.transformed(v->position, v->scale, true);
          sc.cls = LineClass::Visible;
          out.curves.push_back(sc);
        }
        if (h.onVertex) out.curves.push_back(Curve2D::circle(v->toSheet(h.vertex.p), 1.2, LineClass::Dimension));
      }
  }
}

void DimensionTool::accept(core::ToolContext& ctx) {
  if (!picks_.empty() && haveCursor_) placeAt(ctx, cursor_);
  ctx.finish();
}

// ---------------------------------------------------------------------------------------
// EditDimensionTool
// ---------------------------------------------------------------------------------------

EditDimensionTool::EditDimensionTool()
    : DrawingToolBase(drawingToolInfo("drawing.edit-dimension", "Edit Dimension", "Text override, prefix/suffix, precision",
                                      "DIMENSIONS", 20, false, {}, {"EDITDIM"})) {}

void EditDimensionTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.cancel();
    return;
  }
  dimId_ = core::kInvalidId;
  for (ObjectId id : targetObjects(ctx, canvas(ctx), *s))
    if (s->dimension(id)) {
      dimId_ = id;
      break;
    }
  if (dimId_ == core::kInvalidId) {
    ctx.showMessage("Select a dimension to edit", true);
    ctx.cancel();
    return;
  }
  before_ = m->toJson();
  attachCanvas(ctx);
  bool any = false;
  for (const auto& a : ctx.args) {
    std::string k, v;
    if (parseKeyValue(a, k, v)) any = applySetting(ctx, k, v) || any;
  }
  ctx.setPrompt("Edit the dimension, then OK");
  if (any || !hasDialogShell(ctx)) accept(ctx);
}

bool EditDimensionTool::applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheetOf(dimId_) : nullptr;
  Dimension* d = s ? s->dimension(dimId_) : nullptr;
  if (!d) return false;
  bool ok = false;
  DimensionType t;
  if (key == "text") { d->overrideText = value; ok = true; }
  else if (key == "prefix") { d->prefix = value; ok = true; }
  else if (key == "suffix") { d->suffix = value; ok = true; }
  else if (key == "precision") { try { d->precision = std::stoi(value); ok = true; } catch (...) {} }
  else if (key == "type" && parseDimType(value, t)) { d->type = t; ok = true; }
  else if (key == "tolerance" || key == "tol") { d->tolerance.mode = toleranceModeFromName(value); ok = true; }
  else if (key == "upper" || key == "lower" || key == "pm") {
    // deviations in display units (lengths) / degrees (angles)
    try {
      double v = std::stod(value);
      if (d->type != DimensionType::Angle) v = core::fromDisplayValue(v, core::Dimension::Length(), m->document().units()).value;
      if (key == "upper") d->tolerance.upper = v;
      else if (key == "lower") d->tolerance.lower = v;
      else {
        d->tolerance.mode = ToleranceMode::Symmetric;
        d->tolerance.upper = std::abs(v);
        d->tolerance.lower = -std::abs(v);
      }
      if (d->tolerance.mode == ToleranceMode::None) d->tolerance.mode = ToleranceMode::Deviation;
      ok = true;
    } catch (...) {}
  }
  else if (key == "fit") {
    d->tolerance.fit = value;
    if (!value.empty()) d->tolerance.mode = ToleranceMode::Fit;
    ok = true;
  }
  else if (key == "fitdisplay") { d->tolerance.fitDisplay = fitDisplayFromName(value); ok = true; }
  else if (key == "tolprecision") { try { d->tolerance.precision = std::stoi(value); ok = true; } catch (...) {} }
  if (ok) {
    if (const DrawingView* v = s->view(d->viewId)) updateDimension(*d, *v);
    m->notifyChanged(s->id);
  }
  return ok;
}

bool EditDimensionTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  std::string k, v;
  if (parseKeyValue(text, k, v)) return applySetting(ctx, k, v);
  return applySetting(ctx, "text", text);
}

QWidget* EditDimensionTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheetOf(dimId_) : nullptr;
  const Dimension* d = s ? s->dimension(dimId_) : nullptr;
  if (!d) return nullptr;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  QString measured = QString::fromStdString(formatDimensionText(Dimension{*d}, m->document().units()));
  form->addRow(tr("Measured"), new QLabel(measured, w));
  typeCombo_ = new QComboBox(w);
  bool radial = d->type == DimensionType::Radius || d->type == DimensionType::Diameter;
  std::vector<DimensionType> types = radial ? std::vector<DimensionType>{DimensionType::Radius, DimensionType::Diameter}
                                     : d->type == DimensionType::Angle ? std::vector<DimensionType>{DimensionType::Angle}
                                     : std::vector<DimensionType>{DimensionType::Linear, DimensionType::Horizontal,
                                                                  DimensionType::Vertical, DimensionType::Aligned};
  for (DimensionType t : types) {
    typeCombo_->addItem(QString::fromStdString(dimensionTypeName(t)), static_cast<int>(t));
    if (t == d->type) typeCombo_->setCurrentIndex(typeCombo_->count() - 1);
  }
  form->addRow(tr("Type"), typeCombo_);
  textEdit_ = new QLineEdit(QString::fromStdString(d->overrideText), w);
  textEdit_->setPlaceholderText(tr("<measured>"));
  form->addRow(tr("Text override"), textEdit_);
  prefixEdit_ = new QLineEdit(QString::fromStdString(d->prefix), w);
  form->addRow(tr("Prefix"), prefixEdit_);
  suffixEdit_ = new QLineEdit(QString::fromStdString(d->suffix), w);
  form->addRow(tr("Suffix"), suffixEdit_);
  precisionSpin_ = new QSpinBox(w);
  precisionSpin_->setRange(-1, 6);
  precisionSpin_->setSpecialValueText(tr("Document"));
  precisionSpin_->setValue(d->precision);
  form->addRow(tr("Precision"), precisionSpin_);
  // tolerance (Fusion "Tolerance" section): mode, deviations / ISO 286 fit
  tolCombo_ = new QComboBox(w);
  for (ToleranceMode tm : {ToleranceMode::None, ToleranceMode::Symmetric, ToleranceMode::Deviation, ToleranceMode::Limits,
                           ToleranceMode::Fit, ToleranceMode::Basic, ToleranceMode::Reference}) {
    tolCombo_->addItem(QString::fromStdString(toleranceModeName(tm)), static_cast<int>(tm));
    if (tm == d->tolerance.mode) tolCombo_->setCurrentIndex(tolCombo_->count() - 1);
  }
  form->addRow(tr("Tolerance"), tolCombo_);
  const bool isAngle = d->type == DimensionType::Angle;
  auto devText = [&](double v) {
    double shown = isAngle ? v : core::toDisplayValue(core::Quantity::mm(v), m->document().units());
    return QString::number(shown, 'g', 6);
  };
  upperEdit_ = new QLineEdit(devText(d->tolerance.upper), w);
  form->addRow(tr("Upper / ±"), upperEdit_);
  lowerEdit_ = new QLineEdit(devText(d->tolerance.lower), w);
  form->addRow(tr("Lower"), lowerEdit_);
  fitEdit_ = new QLineEdit(QString::fromStdString(d->tolerance.fit), w);
  fitEdit_->setPlaceholderText(tr("H7, g6 or H7/g6"));
  form->addRow(tr("Fit (ISO 286)"), fitEdit_);
  auto apply = [this]() { applyDialog(); };
  connect(tolCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(upperEdit_, &QLineEdit::textChanged, this, [apply](const QString&) { apply(); });
  connect(lowerEdit_, &QLineEdit::textChanged, this, [apply](const QString&) { apply(); });
  connect(fitEdit_, &QLineEdit::textChanged, this, [apply](const QString&) { apply(); });
  connect(typeCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(textEdit_, &QLineEdit::textChanged, this, [apply](const QString&) { apply(); });
  connect(prefixEdit_, &QLineEdit::textChanged, this, [apply](const QString&) { apply(); });
  connect(suffixEdit_, &QLineEdit::textChanged, this, [apply](const QString&) { apply(); });
  connect(precisionSpin_, &QSpinBox::valueChanged, this, [apply](int) { apply(); });
  return w;
}

void EditDimensionTool::applyDialog() {
  if (!ctx_) return;
  if (typeCombo_) applySetting(*ctx_, "type", dimensionTypeName(static_cast<DimensionType>(typeCombo_->currentData().toInt())));
  if (textEdit_) applySetting(*ctx_, "text", textEdit_->text().toStdString());
  if (prefixEdit_) applySetting(*ctx_, "prefix", prefixEdit_->text().toStdString());
  if (suffixEdit_) applySetting(*ctx_, "suffix", suffixEdit_->text().toStdString());
  if (precisionSpin_) applySetting(*ctx_, "precision", std::to_string(precisionSpin_->value()));
  if (upperEdit_ && !upperEdit_->text().isEmpty()) applySetting(*ctx_, "upper", upperEdit_->text().toStdString());
  if (lowerEdit_ && !lowerEdit_->text().isEmpty()) applySetting(*ctx_, "lower", lowerEdit_->text().toStdString());
  if (fitEdit_) applySetting(*ctx_, "fit", fitEdit_->text().toStdString());
  if (tolCombo_)
    applySetting(*ctx_, "tolerance", toleranceModeName(static_cast<ToleranceMode>(tolCombo_->currentData().toInt())));
}

void EditDimensionTool::accept(core::ToolContext& ctx) {
  applyDialog();
  if (DrawingModel* m = model(ctx)) m->commitSnapshot("Edit dimension", ctx.undo, before_);
  ctx.finish();
}

void EditDimensionTool::cancel(core::ToolContext& ctx) {
  if (DrawingModel* m = model(ctx)) m->revertTo(before_);
  ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// CenterMarkTool
// ---------------------------------------------------------------------------------------

CenterMarkTool::CenterMarkTool()
    : DrawingToolBase(drawingToolInfo("drawing.center-mark", "Center Mark", "Centre mark on circles and arcs", "GEOMETRY",
                                      10, true, {}, {"CENTERMARK", "CM"})) {}

void CenterMarkTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  haveHover_ = false;
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  bool all = false;
  for (const auto& a : ctx.args)
    if (lower(a) == "all") all = true;
  if (all) {
    DrawingView* v = pickedView(ctx, *s);
    int n = 0;
    if (v) n = markAllCircles(ctx, v->id);
    else
      for (auto& view : s->views) n += markAllCircles(ctx, view.id);
    ctx.showMessage(std::to_string(n) + " centre mark(s) added");
    ctx.finish();
    return;
  }
  std::vector<Point2> pts;
  for (const auto& a : ctx.args) {
    Point2 p;
    if (parsePoint(a, p)) markAt(ctx, p);
  }
  ctx.setPrompt("Select circles or arcs (Esc to finish)");
  if (!canvas_ && !ctx.args.empty()) ctx.finish();
}

int CenterMarkTool::markAllCircles(core::ToolContext& ctx, ObjectId viewId) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheetOf(viewId) : nullptr;
  const DrawingView* v = s ? s->view(viewId) : nullptr;
  if (!v) return 0;
  std::vector<CenterMark> marks;
  for (const auto& c : v->curves) {
    if (!c.isCircle() || c.cls == LineClass::Hatch || c.cls == LineClass::Detail) continue;
    bool dup = false;
    for (const auto& e : s->centerMarks)
      if (e.viewId != viewId || e.ref.body != c.sourceBody) continue;
      else if (!c.sourceName.empty() ? e.ref.name == c.sourceName : (e.ref.edge == c.sourceEdge && c.sourceEdge != 0)) dup = true;
    for (const auto& e : marks)
      if (e.ref.fallback.distance(c.center) < 1e-6) dup = true;
    if (dup) continue;
    CenterMark cm;
    cm.viewId = viewId;
    cm.ref = DimRef::toCurve(c);
    marks.push_back(cm);
  }
  if (marks.empty()) return 0;
  ObjectId sheetId = s->id;
  m->edit("Center marks", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId))
      for (const auto& cm : marks) m->addCenterMark(*sh, cm);
  });
  return static_cast<int>(marks.size());
}

bool CenterMarkTool::markAt(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  CanvasHit h = hitGeometry(canvas(ctx), *s, sheet);
  if (!h.onCurve || h.curve.type != CurveType::Arc) return false;
  CenterMark cm;
  cm.viewId = h.viewId;
  cm.ref = DimRef::toCurve(h.curve);
  ObjectId sheetId = s->id;
  m->edit("Center mark", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId)) m->addCenterMark(*sh, cm);
  });
  return true;
}

void CenterMarkTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)buttons; (void)modifiers;
  hover_ = c.hitTest(sheet, true);
  haveHover_ = hover_.onCurve && hover_.curve.type == CurveType::Arc;
}

void CenterMarkTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left || !ctx_) return;
  if (!markAt(*ctx_, sheet)) ctx_->showMessage("Select a circle or arc", false);
}

void CenterMarkTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  if (!haveHover_) return;
  const Sheet* s = c.sheet();
  const DrawingView* v = s ? s->view(hover_.viewId) : nullptr;
  if (!v) return;
  Curve2D sc = hover_.curve.transformed(v->position, v->scale, true);
  sc.cls = LineClass::Visible;
  out.curves.push_back(sc);
}

// ---------------------------------------------------------------------------------------
// CenterLineTool
// ---------------------------------------------------------------------------------------

CenterLineTool::CenterLineTool()
    : DrawingToolBase(drawingToolInfo("drawing.centerline", "Centerline", "Centre line between two edges", "GEOMETRY", 20,
                                      true, {}, {"CENTERLINE", "CL"})) {}

void CenterLineTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  first_ = false;
  haveHover_ = false;
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  for (const auto& a : ctx.args) {
    Point2 p;
    if (parsePoint(a, p)) pickAt(ctx, p);
  }
  ctx.setPrompt("Select the first edge");
  if (!canvas_ && !ctx.args.empty()) ctx.finish();
}

bool CenterLineTool::pickAt(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  CanvasHit h = hitGeometry(canvas(ctx), *s, sheet);
  if (!h.onCurve) return false;
  if (!first_) {
    first_ = true;
    viewId_ = h.viewId;
    firstCurve_ = h.curve;
    setCanvasHighlights({viewId_});
    ctx.setPrompt("Select the second edge");
    return true;
  }
  if (h.viewId != viewId_) {
    ctx.showMessage("Both edges must be in the same view", true);
    return false;
  }
  CenterLine cl;
  cl.viewId = viewId_;
  cl.a = DimRef::toCurve(firstCurve_);
  cl.b = DimRef::toCurve(h.curve);
  ObjectId sheetId = s->id;
  m->edit("Centerline", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId)) m->addCenterLine(*sh, cl);
  });
  first_ = false;
  ctx.setPrompt("Select the first edge (Esc to finish)");
  return true;
}

void CenterLineTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)buttons; (void)modifiers;
  hover_ = c.hitTest(sheet, true);
  haveHover_ = hover_.onCurve;
}

void CenterLineTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left || !ctx_) return;
  pickAt(*ctx_, sheet);
}

bool CenterLineTool::canvasKey(DrawingCanvas& c, int key, int modifiers) {
  (void)c; (void)modifiers;
  if (key == Qt::Key_Escape && first_) {
    first_ = false;
    if (ctx_) ctx_->setPrompt("Select the first edge");
    return true;
  }
  return false;
}

void CenterLineTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  const Sheet* s = c.sheet();
  if (!s) return;
  if (first_)
    if (const DrawingView* v = s->view(viewId_)) {
      Curve2D sc = firstCurve_.transformed(v->position, v->scale, true);
      sc.cls = LineClass::Visible;
      out.curves.push_back(sc);
    }
  if (haveHover_)
    if (const DrawingView* v = s->view(hover_.viewId)) {
      Curve2D sc = hover_.curve.transformed(v->position, v->scale, true);
      sc.cls = LineClass::Visible;
      out.curves.push_back(sc);
    }
}

// ---------------------------------------------------------------------------------------
// TextTool
// ---------------------------------------------------------------------------------------

TextTool::TextTool(bool leader)
    : DrawingToolBase(leader ? drawingToolInfo("drawing.leader", "Leader", "Note with a leader arrow", "TEXT", 20, true, "L",
                                               {"LEADER"})
                             : drawingToolInfo("drawing.text", "Text", "Text note", "TEXT", 10, true, "T", {"TEXT", "NOTE"})),
      leader_(leader) {}

void TextTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  editing_ = false;
  noteId_ = core::kInvalidId;
  havePos_ = haveTip_ = false;
  text_.clear();
  height_ = model(ctx) ? model(ctx)->style().noteTextHeight : 3.5;
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  // --edit [<id>]: edit an existing note (explicit id, else the selected note)
  std::vector<std::string> rest;
  for (size_t i = 0; i < ctx.args.size(); ++i) {
    if (ctx.args[i] == "--edit") {
      editing_ = true;
      if (i + 1 < ctx.args.size()) {
        const std::string& next = ctx.args[i + 1];
        bool numeric = !next.empty() && std::all_of(next.begin(), next.end(), [](unsigned char c) { return std::isdigit(c); });
        if (numeric) {
          noteId_ = static_cast<ObjectId>(std::stoull(next));
          ++i;
        }
      }
      continue;
    }
    rest.push_back(ctx.args[i]);
  }
  if (editing_ && noteId_ == core::kInvalidId) {
    for (ObjectId id : targetObjects(ctx, canvas(ctx), *s))
      if (s->note(id)) {
        noteId_ = id;
        break;
      }
  }
  if (editing_) {
    TextNote* n = s->note(noteId_);
    if (!n) {
      ctx.showMessage("Select a note to edit", true);
      ctx.cancel();
      return;
    }
    text_ = n->text;
    height_ = n->height;
    pos_ = n->position;
    havePos_ = true;
    tip_ = n->leaderTo;
    haveTip_ = n->leader;
    before_ = model(ctx)->toJson();
  }
  // arguments: points then the text
  std::vector<Point2> pts;
  std::string words;
  for (const auto& a : rest) {
    Point2 p;
    if (words.empty() && parsePoint(a, p)) pts.push_back(p);
    else words += (words.empty() ? "" : " ") + a;
  }
  if (!pts.empty()) {
    if (leader_ && pts.size() >= 2) {
      tip_ = pts[0];
      haveTip_ = true;
      pos_ = pts[1];
    } else {
      pos_ = pts.back();
    }
    havePos_ = true;
  }
  if (!words.empty()) {
    text_ = words;
    if (editing_) applyDialog();
  }
  if (editing_) ctx.setPrompt("Edit the note text, then OK");
  else ctx.setPrompt(leader_ ? "Click the arrow tip" : "Click the text position, then type the text and OK");
  const bool autoCommit = !hasDialogShell(ctx) && (editing_ ? !words.empty() : (havePos_ && !text_.empty()));
  if (autoCommit) accept(ctx);
}

QWidget* TextTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  textEdit_ = new QPlainTextEdit(QString::fromStdString(text_), w);
  textEdit_->setPlaceholderText(tr("Note text (multi-line)"));
  textEdit_->setMinimumHeight(60);
  form->addRow(tr("Text"), textEdit_);
  heightSpin_ = new QDoubleSpinBox(w);
  heightSpin_->setRange(1.0, 50.0);
  heightSpin_->setSingleStep(0.5);
  heightSpin_->setSuffix(" mm");
  heightSpin_->setValue(height_);
  form->addRow(tr("Height"), heightSpin_);
  connect(textEdit_, &QPlainTextEdit::textChanged, this, [this]() { applyDialog(); });
  connect(heightSpin_, &QDoubleSpinBox::valueChanged, this, [this](double) { applyDialog(); });
  return w;
}

void TextTool::applyDialog() {
  if (textEdit_) text_ = textEdit_->toPlainText().toStdString();
  if (heightSpin_) height_ = heightSpin_->value();
  if (editing_ && ctx_) {
    DrawingModel* m = model(*ctx_);
    Sheet* s = m ? m->sheetOf(noteId_) : nullptr;
    TextNote* n = s ? s->note(noteId_) : nullptr;
    if (n) {
      n->text = text_;
      n->height = height_;
      m->notifyChanged(s->id);
    }
  }
  refreshCanvas();
}

bool TextTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  (void)ctx;
  Point2 p;
  if (parsePoint(text, p)) {
    if (leader_ && !haveTip_) {
      tip_ = p;
      haveTip_ = true;
    } else {
      pos_ = p;
      havePos_ = true;
    }
    return true;
  }
  text_ = text;
  if (textEdit_) textEdit_->setPlainText(QString::fromStdString(text_));
  applyDialog();
  return true;
}

void TextTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
}

void TextTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left) return;
  if (editing_) {
    // reposition the note being edited
    pos_ = sheet;
    if (ctx_) {
      DrawingModel* m = model(*ctx_);
      Sheet* s = m ? m->sheetOf(noteId_) : nullptr;
      TextNote* n = s ? s->note(noteId_) : nullptr;
      if (n) {
        n->position = pos_;
        m->notifyChanged(s->id);
      }
    }
    return;
  }
  if (leader_ && !haveTip_) {
    tip_ = sheet;
    haveTip_ = true;
    if (ctx_) ctx_->setPrompt("Click the text position");
    return;
  }
  pos_ = sheet;
  havePos_ = true;
  if (ctx_) {
    ctx_->setPrompt("Type the note text, then OK");
    if (!hasDialogShell(*ctx_) && !text_.empty()) accept(*ctx_);
  }
}

void TextTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  if (editing_ || !c.model()) return;
  SheetRenderer r(c.model()->style(), c.model()->document().units());
  TextNote n;
  n.text = text_.empty() ? "Text" : text_;
  n.height = height_;
  n.position = havePos_ ? pos_ : cursor_;
  n.leader = leader_ && haveTip_;
  n.leaderTo = tip_;
  if (leader_ && !haveTip_) {
    out.curves.push_back(Curve2D::circle(cursor_, 1.0, LineClass::Dimension));
    return;
  }
  r.addNote(out, n);
}

void TextTool::accept(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  if (!m) {
    ctx.cancel();
    return;
  }
  if (textEdit_) text_ = textEdit_->toPlainText().toStdString();
  if (heightSpin_) height_ = heightSpin_->value();
  if (editing_) {
    applyDialog();
    if (text_.empty()) {
      m->revertTo(before_);
      ctx.cancel();
      return;
    }
    m->commitSnapshot("Edit text", ctx.undo, before_);
    ctx.finish();
    return;
  }
  if (text_.empty()) {
    ctx.showMessage("Enter the note text", true);
    if (!hasDialogShell(ctx)) ctx.cancel();
    return;
  }
  if (!havePos_) pos_ = fallbackPlacement(ctx);
  Sheet* s = activeSheet(ctx, true);
  if (!s) {
    ctx.cancel();
    return;
  }
  TextNote n;
  n.text = text_;
  n.height = height_;
  n.position = pos_;
  n.leader = leader_ && haveTip_;
  n.leaderTo = tip_;
  ObjectId sheetId = s->id, created = core::kInvalidId;
  m->edit(leader_ ? "Leader" : "Text", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId)) created = m->addNote(*sh, n).id;
  });
  if (created != core::kInvalidId)
    if (DrawingCanvas* c = canvas(ctx)) c->setSelection({created});
  ctx.finish();
}

void TextTool::cancel(core::ToolContext& ctx) {
  if (editing_)
    if (DrawingModel* m = model(ctx)) m->revertTo(before_);
  ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// PartsListTool
// ---------------------------------------------------------------------------------------

PartsListTool::PartsListTool()
    : DrawingToolBase(drawingToolInfo("drawing.parts-list", "Parts List", "Bill of materials table bound to the assembly",
                                      "TABLES", 10, true, {}, {"PARTSLIST", "BOM"})) {}

Point2 PartsListTool::defaultPosition(const Sheet& sheet, const PartsList& list) {
  BBox2 f = sheet.frameRect();
  BBox2 tb = sheet.titleBlockRect();
  double bottom = sheet.titleBlock.visible ? tb.min.y : f.max.y;
  return {f.max.x - list.width(), bottom - list.height()};
}

Point2 PartsListTool::snapPosition(const Sheet& sheet, const PartsList& list, const Point2& pos) {
  BBox2 f = sheet.frameRect();
  const double w = list.width(), h = list.height();
  Point2 p = pos;
  if (w < f.width()) p.x = std::clamp(p.x, f.min.x, f.max.x - w);
  if (h < f.height()) p.y = std::clamp(p.y, f.min.y, f.max.y - h);
  if (sheet.titleBlock.visible) {
    BBox2 tb = sheet.titleBlockRect();
    bool overlaps = p.x < tb.max.x && tb.min.x < p.x + w && p.y < tb.max.y && tb.min.y < p.y + h;
    if (overlaps) return defaultPosition(sheet, list);
  }
  return p;
}

void PartsListTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  haveCursor_ = false;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  // --edit <id>: reposition an existing list
  for (size_t i = 0; i + 1 < ctx.args.size(); ++i)
    if (ctx.args[i] == "--edit") {
      ctx.showMessage("Drag the parts list to move it; its rows follow the assembly automatically");
      ctx.cancel();
      return;
    }
  DrawingView* v = pickedView(ctx, *s);
  viewId_ = v ? v->id : core::kInvalidId;
  preview_ = PartsList{};
  preview_.viewId = viewId_;
  preview_.rows = m->bom(v);
  Point2 p;
  for (const auto& a : ctx.args)
    if (parsePoint(a, p)) {
      commit(ctx, p);
      return;
    }
  ctx.setPrompt("Click the top-left corner of the parts list (Enter: default position above the title block)");
  if (!canvas_) accept(ctx);
}

bool PartsListTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  Point2 p;
  if (!parsePoint(text, p)) return false;
  commit(ctx, p);
  return true;
}

void PartsListTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
  haveCursor_ = true;
}

void PartsListTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left || !ctx_) return;
  commit(*ctx_, sheet);
}

void PartsListTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  if (!haveCursor_ || !c.model()) return;
  SheetRenderer r(c.model()->style(), c.model()->document().units());
  PartsList tmp = preview_;
  tmp.position = cursor_;
  if (const Sheet* s = c.sheet()) fitPartsListInFrame(*s, tmp);  // the preview shows where it lands
  r.addPartsList(out, tmp);
}

void PartsListTool::commit(core::ToolContext& ctx, const Point2& pos) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.cancel();
    return;
  }
  PartsList pl;
  pl.viewId = viewId_;
  pl.rows = preview_.rows;
  pl.position = snapPosition(*s, pl, pos);
  ObjectId sheetId = s->id, created = core::kInvalidId;
  m->edit("Parts list", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId)) created = m->addPartsList(*sh, pl).id;
  });
  if (created != core::kInvalidId)
    if (DrawingCanvas* c = canvas(ctx)) c->setSelection({created});
  ctx.finish();
}

void PartsListTool::accept(core::ToolContext& ctx) {
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.cancel();
    return;
  }
  PartsList tmp = preview_;
  commit(ctx, defaultPosition(*s, tmp));
}

// ---------------------------------------------------------------------------------------
// BalloonTool
// ---------------------------------------------------------------------------------------

BalloonTool::BalloonTool()
    : DrawingToolBase(drawingToolInfo("drawing.balloon", "Balloon", "Item balloon pointing at a component", "TABLES", 20,
                                      true, {}, {"BALLOON"})) {}

void BalloonTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  haveAttach_ = haveCursor_ = false;
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  bool all = false;
  std::vector<Point2> pts;
  for (const auto& a : ctx.args) {
    Point2 p;
    if (lower(a) == "all") all = true;
    else if (parsePoint(a, p)) pts.push_back(p);
  }
  if (all) {
    DrawingView* v = pickedView(ctx, *s);
    int n = 0;
    if (v) n = balloonAll(ctx, v->id);
    else
      for (auto& view : s->views) n += balloonAll(ctx, view.id);
    ctx.showMessage(std::to_string(n) + " balloon(s) added");
    ctx.finish();
    return;
  }
  for (size_t i = 0; i + 1 < pts.size(); i += 2) {
    if (attachAt(ctx, pts[i])) placeAt(ctx, pts[i + 1]);
  }
  ctx.setPrompt("Select the geometry of a component, then click the balloon position (Esc to finish)");
  if (!canvas_ && !ctx.args.empty()) ctx.finish();
}

bool BalloonTool::attachAt(core::ToolContext& ctx, const Point2& sheet) {
  Sheet* s = activeSheet(ctx);
  if (!s || !ctx.document) return false;
  CanvasHit h = hitGeometry(canvas(ctx), *s, sheet);
  if (!h.onCurve || h.curve.sourceBody == core::kInvalidId) return false;
  const bool perBody = ctx.document->rootComponent().children.empty();
  ObjectId key = bomKeyForBody(*ctx.document, h.curve.sourceBody, perBody);
  if (key == core::kInvalidId) return false;
  viewId_ = h.viewId;
  itemKey_ = key;
  attachLocal_ = h.viewLocal;
  // snap the tip onto the curve
  const DrawingView* v = s->view(viewId_);
  if (v) {
    double bestD = 1e300;
    for (double t = 0; t <= 1.0001; t += 0.05) {
      Point2 p = h.curve.pointAt(t);
      double d = p.distance(h.viewLocal);
      if (d < bestD) {
        bestD = d;
        attachLocal_ = p;
      }
    }
  }
  attachRef_ = DimRef::toCurve(h.curve);
  attachT_ = nearestCurveParam(h.curve, attachLocal_);
  haveAttach_ = true;
  setCanvasHighlights({viewId_});
  ctx.setPrompt("Click the balloon position");
  return true;
}

bool BalloonTool::placeAt(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s || !haveAttach_) return false;
  Balloon b;
  b.viewId = viewId_;
  b.itemKey = itemKey_;
  b.attach = attachLocal_;
  b.hasRef = true;
  b.ref = attachRef_;
  b.refT = attachT_;
  b.position = sheet;
  ObjectId sheetId = s->id;
  m->edit("Balloon", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId)) m->addBalloon(*sh, b);
  });
  haveAttach_ = false;
  ctx.setPrompt("Select the geometry of the next component (Esc to finish)");
  return true;
}

int BalloonTool::balloonAll(core::ToolContext& ctx, ObjectId viewId) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheetOf(viewId) : nullptr;
  const DrawingView* v = s ? s->view(viewId) : nullptr;
  if (!v || !v->geometryValid || !ctx.document) return 0;
  const bool perBody = ctx.document->rootComponent().children.empty();
  std::vector<BomRow> rows = m->bom(v);
  std::vector<Balloon> balloons;
  BBox2 sb = v->sheetBBox();
  Point2 centre = sb.center();
  double radius = std::max(sb.width(), sb.height()) / 2 + 14.0;
  for (const auto& row : rows) {
    bool already = false;
    for (const auto& b : s->balloons)
      if (b.viewId == viewId && std::find(row.keys.begin(), row.keys.end(), b.itemKey) != row.keys.end()) already = true;
    if (already) continue;
    // first visible curve belonging to this row
    const Curve2D* pick = nullptr;
    for (const auto& c : v->curves) {
      if (c.cls != LineClass::Visible || c.sourceBody == core::kInvalidId) continue;
      ObjectId key = bomKeyForBody(*ctx.document, c.sourceBody, perBody);
      if (std::find(row.keys.begin(), row.keys.end(), key) != row.keys.end()) {
        pick = &c;
        break;
      }
    }
    if (!pick) continue;
    Balloon b;
    b.viewId = viewId;
    b.itemKey = bomKeyForBody(*ctx.document, pick->sourceBody, perBody);
    b.attach = pick->midpoint();
    b.hasRef = true;
    b.ref = DimRef::toCurve(*pick);
    b.refT = nearestCurveParam(*pick, b.attach);
    Point2 tipSheet = v->toSheet(b.attach);
    Point2 dir = (tipSheet - centre).normalized();
    if (dir.length() < 0.5) dir = Point2::polar(-90.0 + 30.0 * static_cast<double>(balloons.size()), 1.0);
    b.position = centre + dir * radius;
    balloons.push_back(b);
  }
  if (balloons.empty()) return 0;
  ObjectId sheetId = s->id;
  m->edit("Balloons", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId))
      for (const auto& b : balloons) m->addBalloon(*sh, b);
  });
  return static_cast<int>(balloons.size());
}

bool BalloonTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  Point2 p;
  if (!parsePoint(text, p)) return false;
  if (!haveAttach_) return attachAt(ctx, p);
  return placeAt(ctx, p);
}

void BalloonTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
  haveCursor_ = true;
}

void BalloonTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left || !ctx_) return;
  if (!haveAttach_) {
    if (!attachAt(*ctx_, sheet)) ctx_->showMessage("Select an edge of a component", false);
    return;
  }
  placeAt(*ctx_, sheet);
}

bool BalloonTool::canvasKey(DrawingCanvas& c, int key, int modifiers) {
  (void)c; (void)modifiers;
  if (key == Qt::Key_Escape && haveAttach_) {
    haveAttach_ = false;
    if (ctx_) ctx_->setPrompt("Select the geometry of a component");
    return true;
  }
  return false;
}

void BalloonTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  if (!haveAttach_ || !haveCursor_ || !c.model()) return;
  const Sheet* s = c.sheet();
  const DrawingView* v = s ? s->view(viewId_) : nullptr;
  if (!v) return;
  Balloon b;
  b.viewId = viewId_;
  b.itemKey = itemKey_;
  b.attach = attachLocal_;
  b.position = cursor_;
  b.itemNumber = bomItemFor(c.model()->bom(v), itemKey_);
  SheetRenderer r(c.model()->style(), c.model()->document().units());
  r.addBalloon(out, *v, b);
}

}  // namespace fabricad::drawing
