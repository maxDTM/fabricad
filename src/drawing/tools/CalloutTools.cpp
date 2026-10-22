// FabriCAD drawing: callout / symbol / table / complex-section / break-view tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/tools/CalloutTools.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "drawing/CalloutRenderer.h"
#include "drawing/Projection.h"
#include "drawing/Sheet.h"
#include "drawing/SheetRenderer.h"
#include "drawing/tools/ViewTools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace fabricad::drawing {

using core::ObjectId;

namespace {

bool truthy(const std::string& v) {
  std::string l = DrawingToolBase::lower(v);
  return l == "1" || l == "true" || l == "yes" || l == "on";
}

bool toDouble(const std::string& v, double& out) {
  try {
    size_t n = 0;
    out = std::stod(v, &n);
    return n > 0;
  } catch (...) {
    return false;
  }
}

core::ToolInfo hiddenInfo(core::ToolInfo i) {
  i.hidden = true;
  return i;
}

std::vector<std::string> splitSettings(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == ';') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(cur);
  for (auto& s : out) {
    size_t b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
    s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
  }
  out.erase(std::remove(out.begin(), out.end(), std::string()), out.end());
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// ClickSequenceTool
// ---------------------------------------------------------------------------------------

void ClickSequenceTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  created_ = 0;
  createdIds_.clear();
  finished_ = false;
  haveCursor_ = false;
  reset();
  if (!activeSheet(ctx)) {
    ctx.showMessage("No sheet: create a drawing first", true);
    ctx.cancel();
    finished_ = true;
    return;
  }
  attachCanvas(ctx);
  begin(ctx);
  if (finished_) return;
  bool anyInput = false;
  for (const auto& a : ctx.args) {
    if (finished_) return;
    std::string k, v;
    Point2 p;
    if (parsePoint(a, p)) {
      anyInput = true;
      cursor_ = p;
      haveCursor_ = true;
      if (!onClick(ctx, p)) ctx.showMessage("Nothing usable at " + a, true);
    } else if (lower(a) == "done") {
      anyInput = true;
      onDone(ctx);
    } else if (parseKeyValue(a, k, v)) {
      setOption(lower(k), v);
    }
  }
  if (finished_) return;
  updatePrompt();
  if (!canvas_ && anyInput) {
    // headless: the arguments were the whole interaction
    if (expectsMore()) {
      ctx.showMessage("Incomplete input", true);
      finished_ = true;
      ctx.cancel();
    } else {
      onDone(ctx);
    }
  }
}

void ClickSequenceTool::finishNow(core::ToolContext& ctx) {
  if (finished_) return;
  finished_ = true;
  ctx.finish();
}

void ClickSequenceTool::accept(core::ToolContext& ctx) { onDone(ctx); }

bool ClickSequenceTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  Point2 p;
  if (parsePoint(text, p)) {
    cursor_ = p;
    haveCursor_ = true;
    bool ok = onClick(ctx, p);
    if (!finished_) updatePrompt();
    return ok;
  }
  if (lower(text) == "done") {
    onDone(ctx);
    return true;
  }
  std::string k, v;
  if (parseKeyValue(text, k, v)) return setOption(lower(k), v);
  return false;
}

void ClickSequenceTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
  haveCursor_ = true;
}

void ClickSequenceTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)modifiers;
  if (button != core::MouseButton::Left || !ctx_) return;
  Point2 p = sheet;
  (void)c;
  cursor_ = p;
  haveCursor_ = true;
  onClick(*ctx_, p);
  if (!finished_) updatePrompt();
}

bool ClickSequenceTool::canvasKey(DrawingCanvas& c, int key, int modifiers) {
  (void)c; (void)modifiers;
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && ctx_) {
    onDone(*ctx_);
    return true;
  }
  return false;
}

void ClickSequenceTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  (void)c;
  preview(out);
}

void ClickSequenceTool::updatePrompt() {
  if (!ctx_) return;
  std::string p = prompt();
  if (!p.empty()) ctx_->setPrompt(p);
}

CanvasHit ClickSequenceTool::geometryAt(const Point2& at) const {
  if (canvas_) {
    CanvasHit h = canvas_->hitTest(at, true);
    if (h.hasGeometry()) return h;
  }
  CanvasHit best;
  best.sheetPos = at;
  DrawingModel* m = ctx_ ? model(*ctx_) : nullptr;
  const Sheet* sheet = m ? m->activeSheet() : nullptr;
  if (!sheet) return best;
  const double tolSheet = canvas_ ? canvas_->pickTolerance() : 1.0;
  double bestD = 1e300;
  for (const auto& v : sheet->views) {
    if (!v.geometryValid) continue;
    Point2 local = v.fromSheet(at);
    double tol = tolSheet / std::max(1e-9, v.scale);
    size_t idx = 0;
    const Curve2D* c = v.pickCurve(local, tol, &idx);
    const Vertex2D* vx = v.pickVertex(local, tol * 1.2);
    if (!c && !vx) continue;
    double d = vx ? vx->p.distance(local) * v.scale : c->distanceTo(local) * v.scale;
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

bool ClickSequenceTool::refOf(const CanvasHit& h, DimRef& out, Point2* local) {
  if (h.onVertex) {
    out = DimRef::toVertex(h.vertex);
    if (local) *local = h.vertex.p;
    return true;
  }
  if (h.onCurve) {
    out = DimRef::toCurve(h.curve);
    if (local) *local = h.curve.type == CurveType::Arc ? h.curve.center : h.curve.midpoint();
    return true;
  }
  return false;
}

void ClickSequenceTool::commit(const std::string& text, const std::function<void(Sheet&)>& fn) {
  if (!ctx_) return;
  DrawingModel* m = model(*ctx_);
  Sheet* s = activeSheet(*ctx_);
  if (!m || !s) return;
  ObjectId sheetId = s->id;
  m->edit(text, ctx_->undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    fn(*sh);
    m->refreshAnnotations(*sh);
    m->notifyChanged(sh->id);
  });
  refreshCanvas();
}

// ---------------------------------------------------------------------------------------
// HoleCalloutTool
// ---------------------------------------------------------------------------------------

HoleCalloutTool::HoleCalloutTool(bool thread)
    : ClickSequenceTool(thread ? drawingToolInfo("drawing.thread-callout", "Thread Callout",
                                                 "Thread designation and depth of a tapped hole", "SYMBOLS", 20, false, {},
                                                 {"THREADCALLOUT", "THREADNOTE"})
                               : drawingToolInfo("drawing.hole-callout", "Hole Callout",
                                                 "Diameter, depth, counterbore / countersink of a hole", "SYMBOLS", 10, true,
                                                 {}, {"HOLECALLOUT", "HOLENOTE"})),
      thread_(thread) {}

void HoleCalloutTool::reset() {
  havePick_ = false;
  viewId_ = core::kInvalidId;
  proto_ = HoleCallout{};
  proto_.thread = thread_;
}

bool HoleCalloutTool::setOption(const std::string& key, const std::string& value) {
  if (key == "thread") proto_.threadOverride = value;
  else if (key == "class") proto_.threadClass = value;
  else if (key == "count") proto_.countOverride = std::max(0, std::atoi(value.c_str()));
  else if (key == "text") proto_.overrideText = value;
  else if (key == "precision") proto_.precision = std::atoi(value.c_str());
  else return false;
  return true;
}

HoleCallout HoleCalloutTool::pending() const {
  HoleCallout c = proto_;
  c.viewId = viewId_;
  c.position = cursor_;
  return c;
}

bool HoleCalloutTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  if (!havePick_) {
    CanvasHit h = geometryAt(sheet);
    if (!h.onCurve || h.curve.type != CurveType::Arc) {
      ctx.showMessage("Select a hole (circle)", true);
      return false;
    }
    viewId_ = h.viewId;
    proto_.ref = DimRef::toCurve(h.curve);
    if (DrawingModel* m = model(ctx)) {
      proto_.info = analyzeHole(m->document(), h.curve);
      if (!proto_.info.ok) {
        proto_.info.ok = true;
        proto_.info.diameter = 2 * h.curve.radius;
      }
      proto_.lines = holeCalloutLines(proto_.info, proto_, m->document().units());
    }
    havePick_ = true;
    return true;
  }
  HoleCallout c = proto_;
  c.viewId = viewId_;
  c.position = sheet;
  DrawingModel* m = model(ctx);
  if (!m) return false;
  c.id = m->allocateId();
  commit(thread_ ? "Thread callout" : "Hole callout", [&](Sheet& s) { s.callouts.holeCallouts.push_back(c); });
  ++created_;
  createdIds_.push_back(c.id);
  HoleCallout keep = proto_;
  reset();
  // options persist for the next callout (Fusion keeps the command running)
  proto_.threadOverride = keep.threadOverride;
  proto_.threadClass = keep.threadClass;
  proto_.countOverride = keep.countOverride;
  proto_.precision = keep.precision;
  return true;
}

void HoleCalloutTool::preview(DisplayList& out) const {
  if (!havePick_ || !haveCursor_ || !ctx_) return;
  DrawingModel* m = model(*ctx_);
  const Sheet* s = m ? m->activeSheet() : nullptr;
  if (!s) return;
  CalloutRenderer(m->style(), m->document().units()).addHoleCallout(out, *s, pending());
}

std::string HoleCalloutTool::prompt() const {
  return havePick_ ? "Click where the callout goes" : (thread_ ? "Select a threaded hole" : "Select a hole (circle edge)");
}

// ---------------------------------------------------------------------------------------
// FeatureControlFrameTool
// ---------------------------------------------------------------------------------------

FeatureControlFrameTool::FeatureControlFrameTool()
    : ClickSequenceTool(drawingToolInfo("drawing.feature-control-frame", "Feature Control Frame",
                                        "GD&T tolerance frame with leader", "SYMBOLS", 30, true, {},
                                        {"FCF", "GDT", "TOLERANCEFRAME"})) {}

void FeatureControlFrameTool::reset() {
  haveAnchor_ = false;
  if (!rowsSet_) frame_ = FeatureControlFrame{};
}

bool FeatureControlFrameTool::setOption(const std::string& key, const std::string& value) {
  if (key == "row" || key == "row1" || key == "row2") {
    FcfRow r;
    if (!parseFcfRow(value, r)) {
      if (ctx_) ctx_->showMessage("Cannot parse frame row: " + value, true);
      return false;
    }
    if (!rowsSet_ || key == "row1") frame_.rows.clear();
    if (key == "row1" || frame_.rows.empty()) frame_.rows.insert(frame_.rows.begin(), r);
    else frame_.rows.push_back(r);
    rowsSet_ = true;
    return true;
  }
  if (key == "leader") frame_.leader = truthy(value);
  else if (key == "above") frame_.above = value;
  else if (key == "below") frame_.below = value;
  else return false;
  return true;
}

void FeatureControlFrameTool::readDialog() {
  if (!rowEdit_) return;
  std::string text = rowEdit_->text().toStdString();
  rowsSet_ = false;
  for (const auto& part : splitSettings(text)) setOption("row", part);
}

bool FeatureControlFrameTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  if (frame_.leader && !haveAnchor_) {
    CanvasHit h = geometryAt(sheet);
    const DrawingView* v = h.hasGeometry() ? s->view(h.viewId) : nullptr;
    Point2 tip = h.onVertex && v ? v->toSheet(h.vertex.p) : sheet;
    frame_.anchor = v ? AnchorPoint::onView(*v, tip) : AnchorPoint::free(tip);
    haveAnchor_ = true;
    return true;
  }
  readDialog();
  FeatureControlFrame f = frame_;
  f.position = sheet;
  if (!f.leader) f.anchor = AnchorPoint::free(sheet);
  f.id = m->allocateId();
  commit("Feature control frame", [&](Sheet& sh) { sh.callouts.featureFrames.push_back(f); });
  ++created_;
  createdIds_.push_back(f.id);
  haveAnchor_ = false;
  return true;
}

void FeatureControlFrameTool::preview(DisplayList& out) const {
  if (!haveCursor_ || !ctx_) return;
  DrawingModel* m = model(*ctx_);
  const Sheet* s = m ? m->activeSheet() : nullptr;
  if (!s || (frame_.leader && !haveAnchor_)) return;
  FeatureControlFrame f = frame_;
  f.position = cursor_;
  if (!f.leader) f.anchor = AnchorPoint::free(cursor_);
  CalloutRenderer(m->style(), m->document().units()).addFeatureFrame(out, *s, f);
}

std::string FeatureControlFrameTool::prompt() const {
  return frame_.leader && !haveAnchor_ ? "Select the toleranced feature (arrow tip)" : "Click to place the frame";
}

QWidget* FeatureControlFrameTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  rowEdit_ = new QLineEdit(w);
  rowEdit_->setPlaceholderText(tr("position dia 0.05 M A B C ; position dia 0.02 A"));
  if (rowsSet_) {
    std::string spec;
    for (const auto& r : frame_.rows) {
      if (!spec.empty()) spec += " ; ";
      spec += gdtCharacteristicName(r.characteristic);
      if (r.diameterZone) spec += " dia";
      spec += " " + formatValue(r.tolerance, 4);
      if (r.modifier != GdtModifier::None) spec += std::string(" ") + gdtModifierLetter(r.modifier);
      for (const auto& d : r.datums) {
        spec += " " + d.letter;
        if (d.modifier != GdtModifier::None) spec += std::string(" ") + gdtModifierLetter(d.modifier);
      }
    }
    rowEdit_->setText(QString::fromStdString(spec));
  }
  form->addRow(tr("Frame"), rowEdit_);
  form->addRow(new QLabel(tr("Characteristic, [dia] tolerance, [M|L], datums with modifiers; ';' = composite row"), w));
  return w;
}

// ---------------------------------------------------------------------------------------
// DatumTool
// ---------------------------------------------------------------------------------------

DatumTool::DatumTool()
    : ClickSequenceTool(drawingToolInfo("drawing.datum", "Datum Identifier", "Datum feature symbol (letter and triangle)",
                                        "SYMBOLS", 40, false, {}, {"DATUM"})) {}

void DatumTool::reset() { haveAnchor_ = false; }

bool DatumTool::setOption(const std::string& key, const std::string& value) {
  if (key != "letter" || value.empty()) return false;
  letter_ = value;
  for (auto& c : letter_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return true;
}

std::string DatumTool::nextLetter(const Sheet& sheet) const {
  if (!letter_.empty()) return letter_;
  // ASME Y14.5 avoids I, O and Q
  for (char c = 'A'; c <= 'Z'; ++c) {
    if (c == 'I' || c == 'O' || c == 'Q') continue;
    std::string l(1, c);
    bool used = false;
    for (const auto& d : sheet.callouts.datums)
      if (d.letter == l) used = true;
    if (!used) return l;
  }
  return "A";
}

bool DatumTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  if (!haveAnchor_) {
    CanvasHit h = geometryAt(sheet);
    const DrawingView* v = h.hasGeometry() ? s->view(h.viewId) : nullptr;
    datum_ = DatumFeature{};
    datum_.anchor = v ? AnchorPoint::onView(*v, sheet) : AnchorPoint::free(sheet);
    haveAnchor_ = true;
    return true;
  }
  DatumFeature d = datum_;
  d.letter = nextLetter(*s);
  d.position = sheet;
  d.id = m->allocateId();
  commit("Datum identifier", [&](Sheet& sh) { sh.callouts.datums.push_back(d); });
  ++created_;
  createdIds_.push_back(d.id);
  letter_.clear();
  haveAnchor_ = false;
  return true;
}

void DatumTool::preview(DisplayList& out) const {
  if (!haveAnchor_ || !haveCursor_ || !ctx_) return;
  DrawingModel* m = model(*ctx_);
  const Sheet* s = m ? m->activeSheet() : nullptr;
  if (!s) return;
  DatumFeature d = datum_;
  d.letter = nextLetter(*s);
  d.position = cursor_;
  CalloutRenderer(m->style(), m->document().units()).addDatum(out, *s, d);
}

std::string DatumTool::prompt() const {
  return haveAnchor_ ? "Click to place the datum letter" : "Select the datum feature (edge)";
}

// ---------------------------------------------------------------------------------------
// SurfaceTextureTool
// ---------------------------------------------------------------------------------------

SurfaceTextureTool::SurfaceTextureTool()
    : ClickSequenceTool(drawingToolInfo("drawing.surface-texture", "Surface Texture", "Surface finish symbol (ISO 1302)",
                                        "SYMBOLS", 50, true, {}, {"SURFACE", "SURFACEFINISH", "ROUGHNESS"})) {}

void SurfaceTextureTool::reset() { haveAnchor_ = false; }

bool SurfaceTextureTool::setOption(const std::string& key, const std::string& value) {
  if (key == "roughness" || key == "ra") symbol_.roughness = key == "ra" && !value.empty() && std::isdigit(static_cast<unsigned char>(value[0])) ? "Ra " + value : value;
  else if (key == "process") symbol_.process = surfaceProcessFromName(value);
  else if (key == "lay") symbol_.lay = value;
  else if (key == "method") symbol_.method = value;
  else if (key == "secondary") symbol_.secondary = value;
  else if (key == "allowance") symbol_.allowance = value;
  else if (key == "allaround") symbol_.allAround = truthy(value);
  else return false;
  return true;
}

void SurfaceTextureTool::place(const Point2& pos) {
  if (!ctx_) return;
  DrawingModel* m = model(*ctx_);
  Sheet* s = activeSheet(*ctx_);
  if (!m || !s) return;
  SurfaceFinish sf = symbol_;
  Point2 a = sf.anchor.resolve(*s);
  sf.leader = pos.distance(a) > 3.0;
  sf.position = pos;
  sf.id = m->allocateId();
  commit("Surface texture", [&](Sheet& sh) { sh.callouts.surfaceFinishes.push_back(sf); });
  ++created_;
  createdIds_.push_back(sf.id);
  haveAnchor_ = false;
}

bool SurfaceTextureTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  Sheet* s = activeSheet(ctx);
  if (!s) return false;
  if (!haveAnchor_) {
    CanvasHit h = geometryAt(sheet);
    const DrawingView* v = h.hasGeometry() ? s->view(h.viewId) : nullptr;
    symbol_.anchor = v ? AnchorPoint::onView(*v, sheet) : AnchorPoint::free(sheet);
    haveAnchor_ = true;
    return true;
  }
  place(sheet);
  return true;
}

void SurfaceTextureTool::onDone(core::ToolContext& ctx) {
  if (haveAnchor_)
    if (Sheet* s = activeSheet(ctx)) place(symbol_.anchor.resolve(*s));
  finishNow(ctx);
}

void SurfaceTextureTool::preview(DisplayList& out) const {
  if (!haveCursor_ || !ctx_) return;
  DrawingModel* m = model(*ctx_);
  const Sheet* s = m ? m->activeSheet() : nullptr;
  if (!s) return;
  SurfaceFinish sf = symbol_;
  if (!haveAnchor_) sf.anchor = AnchorPoint::free(cursor_);
  sf.leader = haveAnchor_ && cursor_.distance(sf.anchor.resolve(*s)) > 3.0;
  sf.position = cursor_;
  CalloutRenderer(m->style(), m->document().units()).addSurfaceFinish(out, *s, sf);
}

std::string SurfaceTextureTool::prompt() const {
  return haveAnchor_ ? "Click to place the symbol (away from the surface adds a leader; Enter: on the surface)"
                     : "Select the surface (edge)";
}

// ---------------------------------------------------------------------------------------
// OrdinateTool
// ---------------------------------------------------------------------------------------

OrdinateTool::OrdinateTool()
    : ClickSequenceTool(drawingToolInfo("drawing.dimension-ordinate", "Ordinate Dimension",
                                        "Ordinate dimensions from an origin", "DIMENSIONS", 30, false, {},
                                        {"ORDINATE", "DIMORDINATE"})) {}

void OrdinateTool::reset() {
  step_ = 0;
  viewId_ = ordinateId_ = core::kInvalidId;
  pending_ = OrdinateDimension{};
  committed_ = false;
}

void OrdinateTool::onDone(core::ToolContext& ctx) {
  if (step_ != 0 && !committed_) ctx.showMessage("Ordinate dimension discarded: no features were picked");
  ctx.finish();
}

bool OrdinateTool::setOption(const std::string& key, const std::string& value) {
  if (key != "axis") return false;
  std::string l = lower(value);
  forcedAxis_ = l == "x" || l == "horizontal" ? 0 : (l == "y" || l == "vertical" ? 1 : -1);
  return true;
}

bool OrdinateTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  if (step_ == 0) {
    CanvasHit h = geometryAt(sheet);
    Point2 local;
    if (!refOf(h, origin_, &local)) {
      ctx.showMessage("Select the origin (a vertex, circle or edge)", true);
      return false;
    }
    viewId_ = h.viewId;
    const DrawingView* v = s->view(viewId_);
    originSheet_ = v ? v->toSheet(local) : sheet;
    step_ = 1;
    return true;
  }
  if (step_ == 1) {
    OrdinateDimension o;
    o.id = m->allocateId();
    o.viewId = viewId_;
    Point2 d = sheet - originSheet_;
    bool xAxis = forcedAxis_ >= 0 ? forcedAxis_ == 0 : std::abs(d.y) >= std::abs(d.x);
    o.axis = xAxis ? OrdinateAxis::X : OrdinateAxis::Y;
    o.origin = origin_;
    o.originText = xAxis ? Point2{originSheet_.x, sheet.y} : Point2{sheet.x, originSheet_.y};
    // not on the sheet yet: a sequence ended before any feature was picked leaves nothing behind
    pending_ = o;
    committed_ = false;
    ordinateId_ = o.id;
    step_ = 2;
    return true;
  }
  CanvasHit h = geometryAt(sheet);
  DimRef ref;
  Point2 local;
  if (!refOf(h, ref, &local) || h.viewId != viewId_) {
    ctx.showMessage("Select a feature of the same view", true);
    return false;
  }
  const DrawingView* v = s->view(viewId_);
  const OrdinateDimension* o = committed_ ? s->callouts.ordinate(ordinateId_) : &pending_;
  if (!v || !o) return false;
  Point2 P = v->toSheet(local);
  OrdinateMember mem;
  mem.ref = ref;
  mem.textPos = o->axis == OrdinateAxis::X ? Point2{P.x, o->originText.y} : Point2{o->originText.x, P.y};
  if (!committed_) {
    OrdinateDimension first = pending_;
    first.members.push_back(mem);
    commit("Ordinate dimension", [&](Sheet& sh) { sh.callouts.ordinates.push_back(first); });
    committed_ = true;
    ++created_;
    createdIds_.push_back(first.id);
    return true;
  }
  ObjectId oid = ordinateId_;
  commit("Ordinate dimension", [&](Sheet& sh) {
    if (OrdinateDimension* od = sh.callouts.ordinate(oid)) od->members.push_back(mem);
  });
  return true;
}

void OrdinateTool::preview(DisplayList& out) const {
  if (!haveCursor_) return;
  if (step_ == 1) {
    Point2 d = cursor_ - originSheet_;
    bool xAxis = forcedAxis_ >= 0 ? forcedAxis_ == 0 : std::abs(d.y) >= std::abs(d.x);
    Point2 end = xAxis ? Point2{originSheet_.x, cursor_.y} : Point2{cursor_.x, originSheet_.y};
    out.curves.push_back(Curve2D::segment(originSheet_, end, LineClass::Dimension));
    out.texts.push_back({end, "0", 3.5, xAxis ? 90.0 : 0.0, HAlign::Center, VAlign::Middle, LineClass::Dimension});
  }
  if (step_ == 2 && !committed_) {
    // the origin label waits for the first member: show it as a preview until then
    const Point2 end = pending_.originText;
    const bool xAxis = pending_.axis == OrdinateAxis::X;
    out.curves.push_back(Curve2D::segment(originSheet_, end, LineClass::Dimension));
    out.texts.push_back({end, "0", 3.5, xAxis ? 90.0 : 0.0, HAlign::Center, VAlign::Middle, LineClass::Dimension});
  }
}

std::string OrdinateTool::prompt() const {
  switch (step_) {
    case 0: return "Select the ordinate origin";
    case 1: return "Click to place the origin label (up/down: X ordinates, left/right: Y ordinates)";
    default: return "Select features to dimension (Enter to finish)";
  }
}

// ---------------------------------------------------------------------------------------
// BaselineChainTool
// ---------------------------------------------------------------------------------------

BaselineChainTool::BaselineChainTool(bool chain)
    : ClickSequenceTool(chain ? drawingToolInfo("drawing.dimension-chain", "Chain Dimension",
                                                "Continue a linear dimension end to end", "DIMENSIONS", 50, false, {},
                                                {"CHAIN", "DIMCONTINUE"})
                              : drawingToolInfo("drawing.dimension-baseline", "Baseline Dimension",
                                                "Stacked dimensions from the base of a linear dimension", "DIMENSIONS",
                                                40, false, {}, {"BASELINE", "DIMBASELINE"})),
      chain_(chain) {}

void BaselineChainTool::reset() {
  haveBase_ = false;
  index_ = 1;
}

bool BaselineChainTool::setOption(const std::string& key, const std::string& value) {
  if (key != "spacing") return false;
  double v = 0;
  if (!toDouble(value, v) || v <= 0) return false;
  spacing_ = v;
  return true;
}

bool BaselineChainTool::setBase(const Sheet& sheet, ObjectId dimId) {
  const Dimension* d = sheet.dimension(dimId);
  if (!d || d->refs.size() != 2) return false;
  if (d->type != DimensionType::Linear && d->type != DimensionType::Horizontal && d->type != DimensionType::Vertical) return false;
  const DrawingView* v = sheet.view(d->viewId);
  DimGeometry g;
  if (!v || !measureDimension(*d, *v, g)) return false;
  DimensionType t = d->type;
  if (t == DimensionType::Linear) {
    // same rule as the dimension layout: horizontal/vertical from where the text sits
    Point2 p1 = v->toSheet(g.p1), p2 = v->toSheet(g.p2), dd = p2 - p1, off = d->textPos - (p1 + p2) * 0.5;
    if (std::abs(dd.x) < 1e-9) t = DimensionType::Vertical;
    else if (std::abs(dd.y) < 1e-9) t = DimensionType::Horizontal;
    else t = std::abs(off.y) >= std::abs(off.x) ? DimensionType::Horizontal : DimensionType::Vertical;
  }
  type_ = t;
  viewId_ = d->viewId;
  base_ = d->refs[0];
  last_ = d->refs[1];
  line_ = d->textPos;
  index_ = 1;
  haveBase_ = true;
  occupied_.clear();
  {
    const bool horiz = t != DimensionType::Vertical;
    Point2 a = v->toSheet(g.p1), b = v->toSheet(g.p2);
    double lo = horiz ? std::min(a.x, b.x) : std::min(a.y, b.y), hi = horiz ? std::max(a.x, b.x) : std::max(a.y, b.y);
    occupied_.push_back({lo, hi});
  }
  return true;
}

void BaselineChainTool::begin(core::ToolContext& ctx) {
  Sheet* s = activeSheet(ctx);
  if (!s) return;
  for (ObjectId id : targetObjects(ctx, canvas(ctx), *s))
    if (setBase(*s, id)) break;
}

bool BaselineChainTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  if (!haveBase_) {
    if (canvas_) {
      CanvasHit h = canvas_->hitTest(sheet, false);
      if (h.kind == HitKind::Dimension && setBase(*s, h.id)) return true;
    }
    ctx.showMessage("Select a linear dimension first", true);
    return false;
  }
  const DrawingView* v = s->view(viewId_);
  CanvasHit h = geometryAt(sheet);
  DimRef ref;
  if (!v || !refOf(h, ref) || h.viewId != viewId_) {
    ctx.showMessage("Select a feature of the same view", true);
    return false;
  }
  std::vector<Dimension> dims;
  if (chain_) {
    dims = makeChainDimensions(*v, {last_, ref}, type_, line_);
    // a segment overlapping one already on this line (e.g. a point inside the base span) goes
    // to the next line out, so the texts never print over each other
    const bool horiz = type_ != DimensionType::Vertical;
    auto spanOf = [&](const Dimension& d, double& lo, double& hi) {
      DimGeometry g;
      if (!measureDimension(d, *v, g)) return false;
      Point2 a = v->toSheet(g.p1), b = v->toSheet(g.p2);
      lo = horiz ? std::min(a.x, b.x) : std::min(a.y, b.y);
      hi = horiz ? std::max(a.x, b.x) : std::max(a.y, b.y);
      return true;
    };
    double lo = 0, hi = 0;
    if (!dims.empty() && spanOf(dims.front(), lo, hi)) {
      bool overlaps = false;
      for (const auto& [olo, ohi] : occupied_)
        if (std::min(hi, ohi) - std::max(lo, olo) > 0.5) overlaps = true;
      if (overlaps) {
        Point2 c = v->sheetBBox().center();
        double sgn = horiz ? (line_.y >= c.y ? 1.0 : -1.0) : (line_.x >= c.x ? 1.0 : -1.0);
        line_ += horiz ? Point2{0, sgn * spacing_} : Point2{sgn * spacing_, 0};
        occupied_.clear();
        dims = makeChainDimensions(*v, {last_, ref}, type_, line_);
      }
      occupied_.push_back({lo, hi});
    }
  } else {
    const bool horiz = type_ != DimensionType::Vertical;
    Point2 c = v->sheetBBox().center();
    double sgn = horiz ? (line_.y >= c.y ? 1.0 : -1.0) : (line_.x >= c.x ? 1.0 : -1.0);
    Point2 first = line_ + (horiz ? Point2{0, sgn * spacing_ * index_} : Point2{sgn * spacing_ * index_, 0});
    dims = makeBaselineDimensions(*v, base_, {ref}, type_, first, spacing_);
  }
  if (dims.empty()) {
    ctx.showMessage("Nothing to dimension there", true);
    return false;
  }
  commit(chain_ ? "Chain dimension" : "Baseline dimension", [&](Sheet& sh) {
    for (auto& d : dims) createdIds_.push_back(m->addDimension(sh, d).id);
  });
  created_ += static_cast<int>(dims.size());
  if (chain_) last_ = ref;
  ++index_;
  return true;
}

std::string BaselineChainTool::prompt() const {
  if (!haveBase_) return "Select a linear dimension";
  return chain_ ? "Select the next point of the chain (Enter to finish)" : "Select points to dimension from the base (Enter to finish)";
}

// ---------------------------------------------------------------------------------------
// BreakViewTool
// ---------------------------------------------------------------------------------------

BreakViewTool::BreakViewTool()
    : ClickSequenceTool(drawingToolInfo("drawing.break-view", "Break View", "Remove a band of a long view", "MODIFY", 60,
                                        false, {}, {"BREAKVIEW", "BREAK"})) {}

void BreakViewTool::reset() {
  viewId_ = core::kInvalidId;
  haveFirst_ = false;
}

void BreakViewTool::begin(core::ToolContext& ctx) {
  Sheet* s = activeSheet(ctx);
  if (!s) return;
  if (DrawingView* v = pickedView(ctx, *s)) viewId_ = v->id;
}

bool BreakViewTool::setOption(const std::string& key, const std::string& value) {
  std::string l = lower(value);
  if (key == "orientation") orientation_ = (l == "vertical" || l == "v") ? 1 : 0;
  else if (key == "gap") return toDouble(value, gap_);
  else if (key == "style") style_ = l == "curved" ? 1 : 0;
  else return false;
  return true;
}

bool BreakViewTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  if (viewId_ == core::kInvalidId) {
    CanvasHit h = canvas_ ? canvas_->hitTest(sheet, false) : geometryAt(sheet);
    if (h.kind != HitKind::View || !s->view(h.id)) return false;
    viewId_ = h.id;
    setCanvasHighlights({viewId_});
    return true;
  }
  if (!haveFirst_) {
    first_ = sheet;
    haveFirst_ = true;
    return true;
  }
  DrawingView* v = s->view(viewId_);
  if (!v) return false;
  Point2 d = sheet - first_;
  bool horizontal = orientation_ >= 0 ? orientation_ == 0 : std::abs(d.x) >= std::abs(d.y);
  Point2 a = v->fromSheet(first_), b = v->fromSheet(sheet);
  ViewBreak br;
  br.orientation = horizontal ? BreakOrientation::Horizontal : BreakOrientation::Vertical;
  br.from = horizontal ? std::min(a.x, b.x) : std::min(a.y, b.y);
  br.to = horizontal ? std::max(a.x, b.x) : std::max(a.y, b.y);
  if (br.to - br.from < 1e-6) return false;
  ObjectId vid = viewId_;
  double gap = gap_;
  int style = style_;
  commit("Break view", [&](Sheet& sh) {
    if (DrawingView* view = sh.view(vid)) {
      view->breaks.push_back(br);
      view->breaks = normalizedBreaks(view->breaks);
      if (gap > 0) view->breakGap = gap;
      if (style >= 0) view->breakStyle = style == 1 ? BreakStyle::Curved : BreakStyle::Structural;
    }
  });
  ++created_;
  haveFirst_ = false;
  finishNow(ctx);
  return true;
}

void BreakViewTool::preview(DisplayList& out) const {
  if (!haveCursor_ || viewId_ == core::kInvalidId || !ctx_) return;
  DrawingModel* m = model(*ctx_);
  const DrawingView* v = m ? m->view(viewId_) : nullptr;
  if (!v) return;
  BBox2 b = v->sheetBBox();
  Point2 d = haveFirst_ ? cursor_ - first_ : Point2{1, 0};
  bool horizontal = orientation_ >= 0 ? orientation_ == 0 : std::abs(d.x) >= std::abs(d.y);
  for (Point2 p : haveFirst_ ? std::vector<Point2>{first_, cursor_} : std::vector<Point2>{cursor_}) {
    if (horizontal) out.curves.push_back(Curve2D::segment({p.x, b.min.y - 3}, {p.x, b.max.y + 3}, LineClass::Center));
    else out.curves.push_back(Curve2D::segment({b.min.x - 3, p.y}, {b.max.x + 3, p.y}, LineClass::Center));
  }
}

std::string BreakViewTool::prompt() const {
  if (viewId_ == core::kInvalidId) return "Select the view to break";
  return haveFirst_ ? "Click the second break line" : "Click the first break line";
}

// ---------------------------------------------------------------------------------------
// ComplexSectionTool
// ---------------------------------------------------------------------------------------

ComplexSectionTool::ComplexSectionTool(bool aligned)
    : ClickSequenceTool(aligned ? drawingToolInfo("drawing.section-view-aligned", "Aligned Section View",
                                                  "Section along two arms; the inclined arm is revolved into the plane",
                                                  "CREATE", 32, false, {}, {"ALIGNEDSECTION"})
                                : drawingToolInfo("drawing.section-view-stepped", "Stepped Section View",
                                                  "Offset section along a stepped cut line", "CREATE", 31, false, {},
                                                  {"STEPPEDSECTION", "OFFSETSECTION"})),
      aligned_(aligned) {}

void ComplexSectionTool::reset() {
  parent_ = core::kInvalidId;
  path_.clear();
  pathDone_ = false;
}

void ComplexSectionTool::begin(core::ToolContext& ctx) {
  Sheet* s = activeSheet(ctx);
  if (!s) return;
  DrawingView* parent = pickedView(ctx, *s);
  if (!parent) {
    std::vector<DrawingView*> candidates;
    for (auto& v : s->views)
      if (v.type != ViewType::Detail) candidates.push_back(&v);
    if (candidates.size() == 1) parent = candidates.front();
  }
  if (parent && parent->type != ViewType::Detail) {
    parent_ = parent->id;
    setCanvasHighlights({parent_});
  }
}

std::vector<ObjectId> ComplexSectionTool::canvasHighlights() const {
  if (parent_ == core::kInvalidId) return {};
  return {parent_};
}

Point2 ComplexSectionTool::snap(const Point2& in) const {
  Point2 p = in;
  if (canvas_) {
    Point2 snapped;
    if (canvas_->snapToVertex(in, snapped)) p = snapped;
  }
  if (path_.empty()) return p;
  const Point2 last = path_.back();
  if (aligned_) return p;
  // stepped: every segment parallel or perpendicular to the first one (sheet axes for the first)
  Point2 u = path_.size() >= 2 ? (path_[1] - path_[0]).normalized() : Point2{1, 0};
  if (path_.size() == 1) {
    Point2 d = p - last;
    u = std::abs(d.x) >= std::abs(d.y) ? Point2{1, 0} : Point2{0, 1};
  }
  Point2 w = u.perp();
  Point2 d = p - last;
  double a = d.dot(u), b = d.dot(w);
  return std::abs(a) >= std::abs(b) ? last + u * a : last + w * b;
}

bool ComplexSectionTool::onClick(core::ToolContext& ctx, const Point2& sheet) {
  Sheet* s = activeSheet(ctx);
  if (!s) return false;
  if (parent_ == core::kInvalidId) {
    CanvasHit h = canvas_ ? canvas_->hitTest(sheet, false) : geometryAt(sheet);
    const DrawingView* v = h.kind == HitKind::View ? s->view(h.id) : nullptr;
    if (!v || v->type == ViewType::Detail) return false;
    parent_ = v->id;
    setCanvasHighlights({parent_});
    return true;
  }
  if (!pathDone_) {
    Point2 p = snap(sheet);
    if (!path_.empty() && p.distance(path_.back()) < 0.5) return false;
    path_.push_back(p);
    if (aligned_ && path_.size() == 3) pathDone_ = true;
    return true;
  }
  return place(ctx, sheet);
}

void ComplexSectionTool::onDone(core::ToolContext& ctx) {
  if (!pathDone_ && path_.size() >= 2) {
    pathDone_ = true;
    updatePrompt();
    return;
  }
  if (pathDone_ && haveCursor_ && place(ctx, cursor_)) return;
  if (!finished_) {
    finished_ = true;
    ctx.cancel();
  }
}

bool ComplexSectionTool::place(core::ToolContext& ctx, const Point2& cursor) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  const DrawingView* parent = s ? s->view(parent_) : nullptr;
  if (!m || !s || !parent || path_.size() < 2) return false;
  Point2 d = (path_[1] - path_[0]).normalized();
  if (d.length() < 0.5) return false;
  // viewing direction and placement side as in the straight section tool
  Point2 nSheet = d.perp();
  Point2 mid = (path_.front() + path_.back()) * 0.5;
  double side = (cursor - mid).dot(nSheet);
  if (std::abs(side) < 1e-9) side = 1.0;
  double sign = side > 0 ? 1.0 : -1.0;
  Point2 nLocal{nSheet.x, -nSheet.y};
  Point2 arrow = (s->standard == ProjectionStandard::ThirdAngle ? nLocal * (-sign) : nLocal * sign).normalized();
  SectionLine line;
  line.p1 = parent->fromSheet(path_[0]);
  line.p2 = parent->fromSheet(path_[1]);
  line.arrow = arrow;
  line.aligned = aligned_ && path_.size() >= 3;
  if (path_.size() >= 3)
    for (const auto& p : path_) line.points.push_back(parent->fromSheet(p));
  Point2 pos = mid + nSheet * side;
  ObjectId sheetId = s->id, parentId = parent_, createdId = core::kInvalidId;
  m->edit(aligned_ ? "Aligned section view" : "Stepped section view", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    if (DrawingView* v = m->addSectionView(*sh, parentId, line, pos)) createdId = v->id;
  });
  if (createdId == core::kInvalidId) return false;
  ++created_;
  createdIds_.push_back(createdId);
  if (DrawingCanvas* c = canvas(ctx)) c->setSelection({createdId});
  finishNow(ctx);
  return true;
}

void ComplexSectionTool::preview(DisplayList& out) const {
  std::vector<Point2> pts = path_;
  if (!pathDone_ && haveCursor_ && !pts.empty()) pts.push_back(snap(cursor_));
  for (size_t i = 0; i + 1 < pts.size(); ++i) out.curves.push_back(Curve2D::segment(pts[i], pts[i + 1], LineClass::Center));
  for (const auto& p : path_) out.curves.push_back(Curve2D::circle(p, 0.8, LineClass::Dimension));
}

std::string ComplexSectionTool::prompt() const {
  if (parent_ == core::kInvalidId) return "Select the parent view";
  if (!pathDone_)
    return aligned_ ? "Click the end of the first arm, the bend (centre) and the end of the second arm"
                    : "Click the points of the stepped cut line (Enter to finish the line)";
  return "Click to place the section view";
}

// ---------------------------------------------------------------------------------------
// Revision table
// ---------------------------------------------------------------------------------------

RevisionTableTool::RevisionTableTool()
    : ClickSequenceTool(drawingToolInfo("drawing.revision-table", "Revision Table", "Table of drawing revisions", "TABLES",
                                        30, false, {}, {"REVTABLE", "REVISIONTABLE"})) {}

bool RevisionTableTool::create(core::ToolContext& ctx, const Point2* pos) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return false;
  if (!s->callouts.revisionTables.empty()) {
    ctx.showMessage("The sheet already has a revision table");
    if (DrawingCanvas* c = canvas(ctx)) c->setSelection({s->callouts.revisionTables.front().id});
    finishNow(ctx);
    return false;
  }
  RevisionTable t;
  t.id = m->allocateId();
  t.position = pos ? *pos : defaultRevisionTablePosition(*s, t);
  commit("Revision table", [&](Sheet& sh) { sh.callouts.revisionTables.push_back(t); });
  ++created_;
  createdIds_.push_back(t.id);
  finishNow(ctx);
  return true;
}

bool RevisionTableTool::onClick(core::ToolContext& ctx, const Point2& sheet) { return create(ctx, &sheet); }

void RevisionTableTool::onDone(core::ToolContext& ctx) { create(ctx, nullptr); }

void RevisionTableTool::preview(DisplayList& out) const {
  if (!haveCursor_ || !ctx_) return;
  DrawingModel* m = model(*ctx_);
  if (!m) return;
  RevisionTable t;
  t.position = cursor_;
  CalloutRenderer(m->style(), m->document().units()).addRevisionTable(out, t);
}

AddRevisionTool::AddRevisionTool()
    : DrawingToolBase(drawingToolInfo("drawing.revision", "Add Revision", "Append a row to the revision table", "TABLES", 31,
                                      false, {}, {"REVISION", "REV"})) {}

void AddRevisionTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.showMessage("No sheet", true);
    ctx.cancel();
    return;
  }
  RevisionRow row;
  std::string rev;
  for (const auto& a : ctx.args) {
    std::string k, v;
    if (!parseKeyValue(a, k, v)) {
      if (row.description.empty()) row.description = a;
      continue;
    }
    k = lower(k);
    if (k == "description" || k == "desc") row.description = v;
    else if (k == "approved" || k == "by") row.approved = v;
    else if (k == "date") row.date = v;
    else if (k == "rev") rev = v;
  }
  if (row.date.empty()) row.date = QDate::currentDate().toString(Qt::ISODate).toStdString();
  ObjectId sheetId = s->id;
  std::string added;
  m->edit("Add revision", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    if (sh->callouts.revisionTables.empty()) {
      RevisionTable t;
      t.id = m->allocateId();
      t.position = defaultRevisionTablePosition(*sh, t);
      sh->callouts.revisionTables.push_back(t);
    }
    RevisionTable& t = sh->callouts.revisionTables.front();
    RevisionRow r = row;
    r.rev = rev.empty() ? t.nextRev() : rev;
    added = r.rev;
    t.rows.push_back(r);
    m->notifyChanged(sh->id);
  });
  ctx.showMessage("Revision " + added + " added");
  ctx.finish();
}

// ---------------------------------------------------------------------------------------
// EditCalloutTool
// ---------------------------------------------------------------------------------------

EditCalloutTool::EditCalloutTool()
    : DrawingToolBase(hiddenInfo(drawingToolInfo("drawing.edit-callout", "Edit Annotation",
                                      "Edit a callout, symbol, ordinate set or revision table", "SYMBOLS", 90, false, {},
                                      {"EDITCALLOUT"}))) {}

void EditCalloutTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  id_ = core::kInvalidId;
  if (m && s)
    for (ObjectId id : targetObjects(ctx, canvas(ctx), *s))
      if (s->callouts.contains(id)) {
        id_ = id;
        break;
      }
  if (id_ == core::kInvalidId) {
    ctx.showMessage("Select a callout or symbol to edit", true);
    ctx.cancel();
    return;
  }
  before_ = m->toJson();
  attachCanvas(ctx);
  bool any = false;
  for (const auto& a : ctx.args) {
    std::string k, v;
    if (parseKeyValue(a, k, v)) any = applySetting(ctx, lower(k), v) || any;
  }
  ctx.setPrompt("Edit the annotation (key=value; ...), then OK");
  if (any || !hasDialogShell(ctx)) accept(ctx);
}

bool EditCalloutTool::applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheetOf(id_) : nullptr;
  if (!s) return false;
  CalloutSet& cs = s->callouts;
  bool ok = false;
  double d = 0;
  if (HoleCallout* c = cs.holeCallout(id_)) {
    ok = true;
    if (key == "text") c->overrideText = value;
    else if (key == "thread") {
      if (value == "0" || lower(value) == "false") c->thread = false;
      else {
        c->thread = true;
        if (!truthy(value)) c->threadOverride = value;
      }
    } else if (key == "class") c->threadClass = value;
    else if (key == "count") c->countOverride = std::max(0, std::atoi(value.c_str()));
    else if (key == "precision") c->precision = std::atoi(value.c_str());
    else ok = false;
  } else if (FeatureControlFrame* f = cs.featureFrame(id_)) {
    ok = true;
    FcfRow row;
    if ((key == "row" || key == "row1") && parseFcfRow(value, row)) f->rows[0] = row;
    else if (key == "row2" && parseFcfRow(value, row)) {
      if (f->rows.size() < 2) f->rows.push_back(row);
      else f->rows[1] = row;
    } else if (key == "tolerance" && toDouble(value, d)) f->rows[0].tolerance = d;
    else if (key == "dia" || key == "diameter") f->rows[0].diameterZone = truthy(value);
    else if (key == "modifier") f->rows[0].modifier = gdtModifierFromName(value);
    else if (key == "characteristic") ok = gdtCharacteristicFromName(value, f->rows[0].characteristic);
    else if (key == "datums") {
      std::istringstream in(value);
      std::string t;
      f->rows[0].datums.clear();
      while (in >> t && f->rows[0].datums.size() < 3) f->rows[0].datums.push_back({t, GdtModifier::None});
    } else if (key == "leader") f->leader = truthy(value);
    else if (key == "above") f->above = value;
    else if (key == "below") f->below = value;
    else ok = false;
  } else if (DatumFeature* df = cs.datum(id_)) {
    ok = key == "letter" && !value.empty();
    if (ok) df->letter = value;
  } else if (SurfaceFinish* sf = cs.surfaceFinish(id_)) {
    ok = true;
    if (key == "roughness" || key == "ra") sf->roughness = value;
    else if (key == "process") sf->process = surfaceProcessFromName(value);
    else if (key == "lay") sf->lay = value;
    else if (key == "method") sf->method = value;
    else if (key == "secondary") sf->secondary = value;
    else if (key == "allowance") sf->allowance = value;
    else if (key == "allaround") sf->allAround = truthy(value);
    else ok = false;
  } else if (OrdinateDimension* o = cs.ordinate(id_)) {
    ok = true;
    if (key == "precision") o->precision = std::atoi(value.c_str());
    else ok = false;
  } else if (RevisionTable* t = cs.revisionTable(id_)) {
    ok = !t->rows.empty();
    if (ok && key == "row") {
      // row=<rev letter> or row=<1-based index>: which row the following keys edit
      int idx = -1;
      for (size_t i = 0; i < t->rows.size(); ++i)
        if (lower(t->rows[i].rev) == lower(value)) idx = static_cast<int>(i);
      if (idx < 0) {
        int n = std::atoi(value.c_str());
        if (n >= 1 && n <= static_cast<int>(t->rows.size())) idx = n - 1;
      }
      if (idx < 0) return false;
      revRow_ = idx;
      return true;
    }
    if (ok) {
      const int ri = revRow_ >= 0 && revRow_ < static_cast<int>(t->rows.size()) ? revRow_ : static_cast<int>(t->rows.size()) - 1;
      RevisionRow& r = t->rows[static_cast<size_t>(ri)];
      if (key == "description") r.description = value;
      else if (key == "approved") r.approved = value;
      else if (key == "date") r.date = value;
      else if (key == "rev") r.rev = value;
      else ok = false;
    }
  }
  if (ok) {
    refreshCallouts(m->document(), *s);
    m->notifyChanged(s->id);
  }
  return ok;
}

bool EditCalloutTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  bool any = false;
  for (const auto& part : splitSettings(text)) {
    std::string k, v;
    if (parseKeyValue(part, k, v)) any = applySetting(ctx, lower(k), v) || any;
  }
  return any;
}

void EditCalloutTool::addFrameFields(QWidget* w, QFormLayout* form, const FeatureControlFrame& f) {
  const FcfRow& row = f.rows.front();
  auto* chr = new QComboBox(w);
  for (GdtCharacteristic c : gdtCharacteristics()) {
    chr->addItem(QString::fromStdString(gdtCharacteristicName(c)), QString::fromStdString(gdtCharacteristicName(c)));
    if (c == row.characteristic) chr->setCurrentIndex(chr->count() - 1);
  }
  form->addRow(tr("Characteristic"), chr);
  auto* tol = new QLineEdit(QString::number(row.tolerance), w);
  form->addRow(tr("Tolerance"), tol);
  auto* dia = new QCheckBox(tr("\u00D8 zone"), w);
  dia->setChecked(row.diameterZone);
  form->addRow(QString(), dia);
  auto* mod = new QComboBox(w);
  for (GdtModifier mm : {GdtModifier::None, GdtModifier::MMC, GdtModifier::LMC, GdtModifier::FreeState, GdtModifier::TangentPlane,
                         GdtModifier::ProjectedZone}) {
    mod->addItem(QString::fromStdString(gdtModifierName(mm)), QString::fromStdString(gdtModifierName(mm)));
    if (mm == row.modifier) mod->setCurrentIndex(mod->count() - 1);
  }
  form->addRow(tr("Modifier"), mod);
  std::string datums;
  for (const auto& d : row.datums) datums += (datums.empty() ? "" : " ") + d.letter;
  auto* dat = new QLineEdit(QString::fromStdString(datums), w);
  dat->setPlaceholderText(tr("A B C"));
  form->addRow(tr("Datums"), dat);
  auto apply = [this](const std::string& k, const QString& v) {
    if (ctx_) applySetting(*ctx_, k, v.toStdString());
  };
  connect(chr, &QComboBox::currentIndexChanged, this, [apply, chr](int) { apply("characteristic", chr->currentData().toString()); });
  connect(mod, &QComboBox::currentIndexChanged, this, [apply, mod](int) { apply("modifier", mod->currentData().toString()); });
  connect(tol, &QLineEdit::editingFinished, this, [apply, tol]() { apply("tolerance", tol->text()); });
  connect(dia, &QCheckBox::toggled, this, [apply](bool on) { apply("dia", on ? "1" : "0"); });
  connect(dat, &QLineEdit::editingFinished, this, [apply, dat]() { apply("datums", dat->text()); });
}

QWidget* EditCalloutTool::createRevisionForm(QWidget* parent, const RevisionTable& t) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  rowCombo_ = new QComboBox(w);
  for (size_t i = 0; i < t.rows.size(); ++i)
    rowCombo_->addItem(QString::fromStdString(t.rows[i].rev.empty() ? std::to_string(i + 1) : t.rows[i].rev), static_cast<int>(i));
  rowCombo_->setCurrentIndex(static_cast<int>(t.rows.size()) - 1);
  form->addRow(tr("Revision"), rowCombo_);
  revEdit_ = new QLineEdit(w);
  descEdit_ = new QLineEdit(w);
  dateEdit_ = new QLineEdit(w);
  approvedEdit_ = new QLineEdit(w);
  form->addRow(tr("Rev"), revEdit_);
  form->addRow(tr("Description"), descEdit_);
  form->addRow(tr("Date"), dateEdit_);
  form->addRow(tr("Approved"), approvedEdit_);
  revRow_ = rowCombo_->currentIndex();
  loadRevisionRow();
  connect(rowCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (i < 0) return;
    storeRevisionRow();
    revRow_ = i;
    loadRevisionRow();
  });
  for (QLineEdit* e : {revEdit_.data(), descEdit_.data(), dateEdit_.data(), approvedEdit_.data()})
    connect(e, &QLineEdit::editingFinished, this, [this]() { storeRevisionRow(); });
  return w;
}

void EditCalloutTool::loadRevisionRow() {
  if (!ctx_) return;
  DrawingModel* m = model(*ctx_);
  Sheet* s = m ? m->sheetOf(id_) : nullptr;
  const RevisionTable* t = s ? s->callouts.revisionTable(id_) : nullptr;
  if (!t || revRow_ < 0 || revRow_ >= static_cast<int>(t->rows.size())) return;
  const RevisionRow& r = t->rows[static_cast<size_t>(revRow_)];
  if (revEdit_) revEdit_->setText(QString::fromStdString(r.rev));
  if (descEdit_) descEdit_->setText(QString::fromStdString(r.description));
  if (dateEdit_) dateEdit_->setText(QString::fromStdString(r.date));
  if (approvedEdit_) approvedEdit_->setText(QString::fromStdString(r.approved));
}

void EditCalloutTool::storeRevisionRow() {
  if (!ctx_ || !revEdit_ || revRow_ < 0) return;
  applySetting(*ctx_, "row", std::to_string(revRow_ + 1));
  applySetting(*ctx_, "rev", revEdit_->text().toStdString());
  applySetting(*ctx_, "description", descEdit_->text().toStdString());
  applySetting(*ctx_, "date", dateEdit_->text().toStdString());
  applySetting(*ctx_, "approved", approvedEdit_->text().toStdString());
  if (rowCombo_ && revRow_ < rowCombo_->count()) rowCombo_->setItemText(revRow_, revEdit_->text());
}

QWidget* EditCalloutTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  {
    DrawingModel* m = model(ctx);
    Sheet* s = m ? m->sheetOf(id_) : nullptr;
    if (const RevisionTable* t = s ? s->callouts.revisionTable(id_) : nullptr; t && !t->rows.empty())
      return createRevisionForm(parent, *t);
  }
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  {
    DrawingModel* m = model(ctx);
    Sheet* s = m ? m->sheetOf(id_) : nullptr;
    if (const FeatureControlFrame* f = s ? s->callouts.featureFrame(id_) : nullptr) addFrameFields(w, form, *f);
  }
  edit_ = new QLineEdit(w);
  edit_->setPlaceholderText(tr("key=value; key=value"));
  form->addRow(tr("Settings"), edit_);
  form->addRow(new QLabel(tr("Hole: text, thread, class, count, precision. Frame: row, row2, tolerance, dia, "
                             "modifier, datums, leader, above, below. Datum: letter. Surface: roughness, process, "
                             "lay, method, secondary, allowance, allaround. Revision: row, description, approved, date, rev."),
                          w));
  return w;
}

void EditCalloutTool::accept(core::ToolContext& ctx) {
  storeRevisionRow();
  if (edit_) commandLineInput(ctx, edit_->text().toStdString());
  if (DrawingModel* m = model(ctx)) m->commitSnapshot("Edit annotation", ctx.undo, before_);
  ctx.finish();
}

void EditCalloutTool::cancel(core::ToolContext& ctx) {
  if (DrawingModel* m = model(ctx)) m->revertTo(before_);
  ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------

void registerCalloutTools(core::ToolRegistry& tools) {
  tools.registerTool(std::make_unique<OrdinateTool>());
  tools.registerTool(std::make_unique<BaselineChainTool>(false));
  tools.registerTool(std::make_unique<BaselineChainTool>(true));
  tools.registerTool(std::make_unique<HoleCalloutTool>(false));
  tools.registerTool(std::make_unique<HoleCalloutTool>(true));
  tools.registerTool(std::make_unique<FeatureControlFrameTool>());
  tools.registerTool(std::make_unique<DatumTool>());
  tools.registerTool(std::make_unique<SurfaceTextureTool>());
  tools.registerTool(std::make_unique<EditCalloutTool>());
  tools.registerTool(std::make_unique<RevisionTableTool>());
  tools.registerTool(std::make_unique<AddRevisionTool>());
  tools.registerTool(std::make_unique<ComplexSectionTool>(false));
  tools.registerTool(std::make_unique<ComplexSectionTool>(true));
  tools.registerTool(std::make_unique<BreakViewTool>());
}

}  // namespace fabricad::drawing
