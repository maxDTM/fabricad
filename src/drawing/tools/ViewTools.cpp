// FabriCAD drawing: sheet and view tools implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/tools/ViewTools.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "drawing/Projection.h"
#include "drawing/SheetRenderer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>
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

bool parseOrientation(const std::string& text, ViewOrientation& out) {
  std::string t = DrawingToolBase::lower(text);
  t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return c == ' ' || c == '-' || c == '_'; }), t.end());
  if (t == "iso" || t == "isometric") t = "isotopright";
  for (ViewOrientation o : standardOrientations())
    if (DrawingToolBase::lower(viewOrientationName(o)) == t) {
      out = o;
      return true;
    }
  return false;
}

bool parseStandard(const std::string& text, ProjectionStandard& out) {
  std::string t = DrawingToolBase::lower(text);
  if (t == "iso" || t == "first" || t == "firstangle" || t == "first-angle") { out = ProjectionStandard::FirstAngle; return true; }
  if (t == "asme" || t == "ansi" || t == "third" || t == "thirdangle" || t == "third-angle") { out = ProjectionStandard::ThirdAngle; return true; }
  return false;
}

bool parseBool(const std::string& text, bool& out) {
  std::string t = DrawingToolBase::lower(text);
  if (t == "on" || t == "1" || t == "true" || t == "yes" || t == "full") { out = true; return true; }
  if (t == "off" || t == "0" || t == "false" || t == "no") { out = false; return true; }
  return false;
}

bool isNumber(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

void fillStyleCombo(QComboBox* combo, bool hidden) {
  combo->addItem(QObject::tr("Visible Edges"), 0);
  combo->addItem(QObject::tr("Visible and Hidden Edges"), 1);
  combo->setCurrentIndex(hidden ? 1 : 0);
}

void fillTangentCombo(QComboBox* combo, TangentEdgeMode mode) {
  combo->addItem(QObject::tr("Off"), 0);
  combo->addItem(QObject::tr("Full"), 1);
  combo->setCurrentIndex(mode == TangentEdgeMode::Off ? 0 : 1);
}

void fillStandardCombo(QComboBox* combo, ProjectionStandard s) {
  combo->addItem(QObject::tr("ISO (first angle)"), 0);
  combo->addItem(QObject::tr("ASME (third angle)"), 1);
  combo->setCurrentIndex(s == ProjectionStandard::FirstAngle ? 0 : 1);
}

void fillFormatCombo(QComboBox* combo, const std::string& current) {
  for (const auto& f : sheetFormats()) {
    combo->addItem(QString::fromStdString(sheetFormatLabel(f)), QString::fromStdString(f.name));
    if (f.name == current) combo->setCurrentIndex(combo->count() - 1);
  }
}

}  // namespace

std::vector<ObjectId> targetObjects(core::ToolContext& ctx, DrawingCanvas* canvas, const Sheet& sheet) {
  std::vector<ObjectId> ids;
  auto add = [&](ObjectId id) {
    if (id != core::kInvalidId && sheet.contains(id) && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
  };
  if (canvas)
    for (ObjectId id : canvas->selection()) add(id);
  if (ids.empty())
    for (const auto& r : ctx.preselection)
      if (r.kind == core::ShapeKind::DrawingView || r.kind == core::ShapeKind::DrawingEntity) add(r.owner);
  if (ids.empty())
    for (const auto& a : ctx.args)
      if (isNumber(a)) add(static_cast<ObjectId>(std::stoull(a)));
  return ids;
}

// ---------------------------------------------------------------------------------------
// NewDrawingTool
// ---------------------------------------------------------------------------------------

NewDrawingTool::NewDrawingTool()
    : DrawingToolBase(drawingToolInfo("drawing.create", "New Drawing", "Create a drawing sheet from the design",
                                      "CREATE", 1, false, {}, {"NEWDRAWING", "DRAWING"})) {
  setAnyWorkspace(true);
  format_ = sheetFormatByName("A3").value_or(SheetFormat{});
}

void NewDrawingTool::applyArgs(const std::vector<std::string>& args) {
  for (const auto& a : args) {
    ProjectionStandard s;
    double sc;
    if (auto f = sheetFormatByName(a)) format_ = *f;
    else if (parseStandard(a, s)) standard_ = s;
    else if (parseScale(a, sc)) scale_ = sc;
    else if (lower(a) == "portrait") format_.landscape = false;
  }
  if (!format_.landscape && format_.width > format_.height) std::swap(format_.width, format_.height);
}

void NewDrawingTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  if (DrawingModel* m = model(ctx))
    if (Sheet* s = m->activeSheet()) {
      format_ = s->format;
      standard_ = s->standard;
      scale_ = s->scale;
    }
  applyArgs(ctx.args);
  ctx.setPrompt("Choose the standard, sheet size and scale, then OK");
  if (!hasDialogShell(ctx)) accept(ctx);
}

QWidget* NewDrawingTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  standardCombo_ = new QComboBox(w);
  fillStandardCombo(standardCombo_, standard_);
  form->addRow(tr("Standard"), standardCombo_);
  formatCombo_ = new QComboBox(w);
  fillFormatCombo(formatCombo_, format_.name);
  form->addRow(tr("Sheet Size"), formatCombo_);
  scaleCombo_ = addScaleCombo(form, scale_);
  return w;
}

void NewDrawingTool::accept(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  if (!m) {
    ctx.cancel();
    return;
  }
  if (standardCombo_) standard_ = standardCombo_->currentIndex() == 0 ? ProjectionStandard::FirstAngle : ProjectionStandard::ThirdAngle;
  if (formatCombo_)
    if (auto f = sheetFormatByName(formatCombo_->currentData().toString().toStdString())) format_ = *f;
  if (scaleCombo_) scale_ = comboScale(scaleCombo_, scale_);
  ObjectId sheetId = core::kInvalidId;
  m->edit("New drawing", ctx.undo, [&]() {
    Sheet& s = m->addSheet(format_, standard_);
    s.scale = scale_;
    sheetId = s.id;
    m->notifyChanged(s.id);  // repaint the title block with the chosen scale (DRW-ASM-OUTPUT-7)
  });
  m->setActiveSheet(sheetId);
  if (ctx.host) ctx.host->setWorkspace(core::Workspace::Drawing);
  ctx.showMessage("Sheet " + std::string(m->sheet(sheetId) ? m->sheet(sheetId)->name : "") + " created. Use Base View to place the first view.");
  ctx.finish();
}

// ---------------------------------------------------------------------------------------
// NewSheetTool / DeleteSheetTool
// ---------------------------------------------------------------------------------------

NewSheetTool::NewSheetTool()
    : DrawingToolBase(drawingToolInfo("drawing.new-sheet", "New Sheet", "Add a sheet to the drawing", "CREATE", 5, false,
                                      {}, {"NEWSHEET", "SHEET"})) {}

void NewSheetTool::activate(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  if (!m) return;
  SheetFormat fmt = sheetFormatByName("A3").value_or(SheetFormat{});
  ProjectionStandard std = ProjectionStandard::FirstAngle;
  double scale = 1.0;
  if (Sheet* s = m->activeSheet()) {
    fmt = s->format;
    std = s->standard;
    scale = s->scale;
  }
  for (const auto& a : ctx.args) {
    ProjectionStandard s;
    if (auto f = sheetFormatByName(a)) fmt = *f;
    else if (parseStandard(a, s)) std = s;
    else if (lower(a) == "portrait") fmt.landscape = false;
    else if (lower(a) == "landscape") fmt.landscape = true;
  }
  if (!fmt.landscape && fmt.width > fmt.height) std::swap(fmt.width, fmt.height);
  if (fmt.landscape && fmt.width < fmt.height) std::swap(fmt.width, fmt.height);
  ObjectId id = core::kInvalidId;
  m->edit("New sheet", ctx.undo, [&]() {
    Sheet& s = m->addSheet(fmt, std);
    s.scale = scale;
    id = s.id;
    m->notifyChanged(s.id);
  });
  m->setActiveSheet(id);
  if (DrawingCanvas* c = canvas(ctx)) c->setSheet(id);
}

DeleteSheetTool::DeleteSheetTool()
    : DrawingToolBase(drawingToolInfo("drawing.delete-sheet", "Delete Sheet", "Delete the active sheet", "MODIFY", 60,
                                      false, {}, {"DELETESHEET"})) {}

void DeleteSheetTool::activate(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return;
  if (m->sheets().size() <= 1) {
    ctx.showMessage("The last sheet cannot be deleted", true);
    return;
  }
  if (ctx.host && !ctx.host->askYesNo("Delete sheet", "Delete sheet '" + s->name + "' and all its views?", true)) return;
  ObjectId id = s->id;
  m->edit("Delete sheet", ctx.undo, [&]() { m->removeSheet(id); });
}

// ---------------------------------------------------------------------------------------
// BaseViewTool
// ---------------------------------------------------------------------------------------

BaseViewTool::BaseViewTool()
    : DrawingToolBase(drawingToolInfo("drawing.base-view", "Base View", "Place a view of the design on the sheet",
                                      "CREATE", 10, true, {}, {"BASEVIEW", "BV"})) {}

void BaseViewTool::activate(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  if (!m) {
    ctx.cancel();
    return;
  }
  sources_.clear();
  for (const auto& r : ctx.preselection)
    if (r.kind == core::ShapeKind::Body || r.kind == core::ShapeKind::Component) sources_.push_back(r);
  orientation_ = ViewOrientation::Front;
  hidden_ = false;
  tangent_ = TangentEdgeMode::Off;
  placed_ = havePos_ = false;
  preview_ = DrawingView{};
  Sheet* s = activeSheet(ctx, false);
  scale_ = s ? s->scale : 1.0;
  for (const auto& a : ctx.args) applyToken(a);
  if (!s && ctx.document->liveBodies(true).empty()) {
    ctx.showMessage("The design has no bodies to draw", true);
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  ctx.setPrompt(canvas_ ? "Click to place the base view (options in the dialog), then OK" : "Base view: OK to place at the sheet centre");
  requestPreview();
  if (!canvas_ && !hasDialogShell(ctx)) accept(ctx);
}

bool BaseViewTool::applyToken(const std::string& token) {
  ViewOrientation o;
  double sc;
  Point2 p;
  std::string k, v;
  if (parseOrientation(token, o)) { orientation_ = o; return true; }
  if (parseScale(token, sc)) { scale_ = sc; return true; }
  if (parsePoint(token, p)) { pos_ = p; havePos_ = placed_ = true; return true; }
  if (parseKeyValue(token, k, v)) {
    bool b;
    if (k == "hidden" && parseBool(v, b)) { hidden_ = b; return true; }
    if (k == "tangent" && parseBool(v, b)) { tangent_ = b ? TangentEdgeMode::Full : TangentEdgeMode::Off; return true; }
    if (k == "scale" && parseScale(v, sc)) { scale_ = sc; return true; }
    if (k == "orientation" && parseOrientation(v, o)) { orientation_ = o; return true; }
  }
  std::string t = lower(token);
  if (t == "hidden") { hidden_ = true; return true; }
  if (t == "tangent") { tangent_ = TangentEdgeMode::Full; return true; }
  return false;
}

bool BaseViewTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  bool any = false;
  for (const auto& t : split(text)) any = applyToken(t) || any;
  if (!any) return false;
  if (orientationCombo_) {
    int idx = orientationCombo_->findData(static_cast<int>(orientation_));
    if (idx >= 0) orientationCombo_->setCurrentIndex(idx);
  }
  if (scaleCombo_) scaleCombo_->setEditText(QString::fromStdString(scaleLabel(scale_)));
  requestPreview();
  (void)ctx;
  return true;
}

QWidget* BaseViewTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  std::string src = sources_.empty() ? "All visible bodies" : std::to_string(sources_.size()) + " selected";
  form->addRow(tr("Design"), new QLabel(QString::fromStdString(src), w));
  orientationCombo_ = addOrientationCombo(form, orientation_);
  styleCombo_ = new QComboBox(w);
  fillStyleCombo(styleCombo_, hidden_);
  form->addRow(tr("Style"), styleCombo_);
  scaleCombo_ = addScaleCombo(form, scale_);
  tangentCombo_ = new QComboBox(w);
  fillTangentCombo(tangentCombo_, tangent_);
  form->addRow(tr("Tangent Edges"), tangentCombo_);
  connect(orientationCombo_, &QComboBox::currentIndexChanged, this, [this](int) { syncFromDialog(); });
  connect(styleCombo_, &QComboBox::currentIndexChanged, this, [this](int) { syncFromDialog(); });
  connect(tangentCombo_, &QComboBox::currentIndexChanged, this, [this](int) { syncFromDialog(); });
  connect(scaleCombo_, &QComboBox::currentTextChanged, this, [this](const QString&) { syncFromDialog(); });
  return w;
}

void BaseViewTool::syncFromDialog() {
  bool geometryChanged = false;
  if (orientationCombo_) {
    auto o = static_cast<ViewOrientation>(orientationCombo_->currentData().toInt());
    if (o != orientation_) { orientation_ = o; geometryChanged = true; }
  }
  if (styleCombo_) {
    bool h = styleCombo_->currentData().toInt() == 1;
    if (h != hidden_) { hidden_ = h; geometryChanged = true; }
  }
  if (tangentCombo_) {
    auto t = tangentCombo_->currentData().toInt() == 1 ? TangentEdgeMode::Full : TangentEdgeMode::Off;
    if (t != tangent_) { tangent_ = t; geometryChanged = true; }
  }
  if (scaleCombo_) scale_ = comboScale(scaleCombo_, scale_);
  if (geometryChanged) requestPreview();
  else refreshCanvas();
}

void BaseViewTool::requestPreview() {
  if (!ctx_) return;
  DrawingModel* m = model(*ctx_);
  if (!m) return;
  DrawingView tmp;
  tmp.type = ViewType::Base;
  tmp.sources = sources_;
  tmp.orientation = orientation_;
  tmp.customFrame = frameForOrientation(orientation_);
  tmp.hiddenLines = hidden_;
  tmp.tangentEdges = tangent_;
  tmp.scale = scale_;
  ViewJob job = m->makeJob(tmp, false);
  if (job.projection.sources.empty()) {
    preview_.geometryValid = false;
    previewPending_ = false;
    refreshCanvas();
    return;
  }
  previewPending_ = true;
  preview_.geometryValid = false;
  refreshCanvas();
  previews(*ctx_).request(job, [this](const ViewJobResult& r) {
    previewPending_ = false;
    if (r.error.empty()) {
      preview_.curves = r.projection.curves;
      preview_.bbox = r.projection.bbox;
      preview_.geometryValid = true;
    }
    refreshCanvas();
  });
}

void BaseViewTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  if (placed_) return;
  pos_ = sheet;
  havePos_ = true;
}

void BaseViewTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)c; (void)modifiers;
  if (button != core::MouseButton::Left) return;
  pos_ = sheet;
  havePos_ = true;
  placed_ = true;
  if (ctx_) {
    ctx_->setPrompt("View placed. Adjust options in the dialog, then OK (Enter)");
    if (!hasDialogShell(*ctx_)) accept(*ctx_);
  }
}

void BaseViewTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  (void)c;
  if (!havePos_) return;
  preview_.scale = scale_;
  if (preview_.geometryValid) {
    addPreviewView(out, preview_, pos_);
  } else {
    BBox2 r;
    r.add(pos_ - Point2{15, 15});
    r.add(pos_ + Point2{15, 15});
    addPlaceholder(out, r);
  }
}

void BaseViewTool::accept(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  if (!m) {
    ctx.cancel();
    return;
  }
  if (!havePos_) pos_ = fallbackPlacement(ctx);
  ObjectId newId = core::kInvalidId;
  m->edit("Base view", ctx.undo, [&]() {
    Sheet* s = activeSheet(ctx, true);
    if (!s) return;
    DrawingView& v = m->addBaseView(*s, sources_, orientation_, scale_, pos_);
    v.hiddenLines = hidden_;
    v.tangentEdges = tangent_;
    newId = v.id;
  });
  if (newId == core::kInvalidId) {
    ctx.showMessage("Could not create the view", true);
    ctx.cancel();
    return;
  }
  if (DrawingCanvas* c = canvas(ctx)) {
    c->setSheet(m->sheetOf(newId)->id);
    c->setSelection({newId});
  }
  ctx.finish();
}

// ---------------------------------------------------------------------------------------
// ProjectedViewTool
// ---------------------------------------------------------------------------------------

ProjectedViewTool::ProjectedViewTool()
    : DrawingToolBase(drawingToolInfo("drawing.projected-view", "Projected View",
                                      "Project orthographic/isometric views from a parent view", "CREATE", 20, true, "P",
                                      {"PROJECTEDVIEW", "PROJ", "PV"})) {}

bool ProjectedViewTool::parseDirection(const std::string& text, ProjectionDirection& out) {
  std::string t = DrawingToolBase::lower(text);
  t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return c == ' ' || c == '-' || c == '_'; }), t.end());
  if (t == "top" || t == "up") t = "above";
  if (t == "bottom" || t == "down") t = "below";
  if (t == "iso" || t == "isometric") t = "aboveright";
  static const ProjectionDirection all[] = {ProjectionDirection::Left,      ProjectionDirection::Right,
                                            ProjectionDirection::Above,     ProjectionDirection::Below,
                                            ProjectionDirection::AboveLeft, ProjectionDirection::AboveRight,
                                            ProjectionDirection::BelowLeft, ProjectionDirection::BelowRight};
  for (ProjectionDirection d : all)
    if (DrawingToolBase::lower(projectionDirectionName(d)) == t) {
      out = d;
      return true;
    }
  return false;
}

Point2 ProjectedViewTool::autoPosition(const DrawingView& parent, ProjectionDirection dir, double gap) {
  BBox2 b = parent.sheetBBox();
  double w = std::max(b.width(), 20.0), h = std::max(b.height(), 20.0);
  double dx = (w + h) / 2 + gap;  // the projected depth is unknown; use the mean extent
  double dy = dx;
  Point2 p = parent.position;
  switch (dir) {
    case ProjectionDirection::Left: return {p.x - dx, p.y};
    case ProjectionDirection::Right: return {p.x + dx, p.y};
    case ProjectionDirection::Above: return {p.x, p.y - dy};
    case ProjectionDirection::Below: return {p.x, p.y + dy};
    case ProjectionDirection::AboveLeft: return {p.x - dx, p.y - dy};
    case ProjectionDirection::AboveRight: return {p.x + dx, p.y - dy};
    case ProjectionDirection::BelowLeft: return {p.x - dx, p.y + dy};
    case ProjectionDirection::BelowRight: return {p.x + dx, p.y + dy};
  }
  return p;
}

bool ProjectedViewTool::setParent(ObjectId id) {
  if (!ctx_) return false;
  DrawingModel* m = model(*ctx_);
  Sheet* s = activeSheet(*ctx_);
  if (!m || !s) return false;
  DrawingView* v = s->view(id);
  if (!v || v->type == ViewType::Detail) return false;
  parent_ = id;
  parentCopy_ = *v;
  standard_ = s->standard;
  previews_.clear();
  pending_.clear();
  hoverDir_.reset();
  setCanvasHighlights({parent_});
  ctx_->setPrompt("Move to a side of the parent and click to place a projected view; Enter/OK to finish");
  return true;
}

void ProjectedViewTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  parent_ = core::kInvalidId;
  pending_.clear();
  previews_.clear();
  hoverDir_.reset();
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.showMessage("No sheet: create a base view first", true);
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  DrawingView* parent = pickedView(ctx, *s);
  if (!parent) {
    // a single candidate view needs no pick
    std::vector<DrawingView*> candidates;
    for (auto& v : s->views)
      if (v.type != ViewType::Detail) candidates.push_back(&v);
    if (candidates.size() == 1) parent = candidates.front();
  }
  if (parent) setParent(parent->id);
  else ctx.setPrompt("Select the parent view");
  // command line: directions given as arguments
  std::vector<ProjectionDirection> dirs;
  for (const auto& a : ctx.args) {
    ProjectionDirection d;
    if (parseDirection(a, d)) dirs.push_back(d);
  }
  if (parent_ != core::kInvalidId && !dirs.empty()) {
    for (ProjectionDirection d : dirs) pending_.push_back({d, autoPosition(parentCopy_, d)});
    accept(ctx);
  } else if (parent_ == core::kInvalidId && !canvas_) {
    ctx.showMessage("Select a parent view first", true);
    ctx.cancel();
  }
}

bool ProjectedViewTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  (void)ctx;
  if (parent_ == core::kInvalidId) return false;
  bool any = false;
  for (const auto& t : split(text)) {
    ProjectionDirection d;
    if (parseDirection(t, d)) {
      pending_.push_back({d, autoPosition(parentCopy_, d)});
      any = true;
    }
  }
  refreshCanvas();
  return any;
}

Point2 ProjectedViewTool::alignedFor(ProjectionDirection dir, const Point2& cursor) const {
  Point2 p = cursor;
  if (dir == ProjectionDirection::Left || dir == ProjectionDirection::Right) p.y = parentCopy_.position.y;
  if (dir == ProjectionDirection::Above || dir == ProjectionDirection::Below) p.x = parentCopy_.position.x;
  return p;
}

void ProjectedViewTool::ensurePreview(ProjectionDirection dir) {
  if (!ctx_) return;
  Preview& pv = previews_[dir];
  if (pv.valid || pv.pending) return;
  DrawingModel* m = model(*ctx_);
  if (!m) return;
  pv.view.type = ViewType::Projected;
  pv.view.sources = parentCopy_.sources;
  pv.view.orientation = ViewOrientation::Custom;
  gp_Ax2 pf = parentCopy_.geometryValid ? parentCopy_.frame : parentCopy_.definitionFrame();
  pv.view.customFrame = projectedFrame(pf, dir, standard_);
  pv.view.scale = parentCopy_.scale;
  pv.view.hiddenLines = parentCopy_.hiddenLines;
  pv.view.tangentEdges = parentCopy_.tangentEdges;
  pv.pending = true;
  ViewJob job = m->makeJob(pv.view, false);
  previews(*ctx_).request(job, [this, dir](const ViewJobResult& r) {
    auto it = previews_.find(dir);
    if (it == previews_.end()) return;
    it->second.pending = false;
    if (r.error.empty()) {
      it->second.view.curves = r.projection.curves;
      it->second.view.bbox = r.projection.bbox;
      it->second.valid = true;
    }
    refreshCanvas();
  });
}

void ProjectedViewTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  if (parent_ == core::kInvalidId) return;
  Point2 off = sheet - parentCopy_.position;
  if (off.length() < 1.0) {
    hoverDir_.reset();
    return;
  }
  ProjectionDirection d = classifyDirection(off);
  hoverDir_ = d;
  hoverPos_ = alignedFor(d, sheet);
  ensurePreview(d);
}

void ProjectedViewTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)modifiers;
  if (button != core::MouseButton::Left) return;
  if (parent_ == core::kInvalidId) {
    CanvasHit h = c.hitTest(sheet, false);
    if (h.kind == HitKind::View) setParent(h.id);
    return;
  }
  Point2 off = sheet - parentCopy_.position;
  if (off.length() < 1.0) return;
  ProjectionDirection d = classifyDirection(off);
  pending_.push_back({d, alignedFor(d, sheet)});
  ensurePreview(d);
  if (ctx_) ctx_->setPrompt(std::to_string(pending_.size()) + " view(s) placed. Click for more or Enter/OK to finish");
}

void ProjectedViewTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  (void)c;
  auto draw = [&](ProjectionDirection d, const Point2& pos) {
    auto it = previews_.find(d);
    if (it != previews_.end() && it->second.valid) {
      addPreviewView(out, it->second.view, pos);
    } else {
      BBox2 b = parentCopy_.sheetBBox();
      BBox2 r;
      r.add(pos - Point2{b.width() / 2, b.height() / 2});
      r.add(pos + Point2{b.width() / 2, b.height() / 2});
      addPlaceholder(out, r);
    }
  };
  for (const auto& p : pending_) draw(p.dir, p.pos);
  if (hoverDir_) {
    draw(*hoverDir_, hoverPos_);
    // alignment guide
    Point2 a = parentCopy_.position, b = hoverPos_;
    out.curves.push_back(Curve2D::segment(a, b, LineClass::Center));
  }
}

std::vector<ObjectId> ProjectedViewTool::canvasHighlights() const {
  if (parent_ == core::kInvalidId) return {};
  return {parent_};
}

void ProjectedViewTool::accept(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s || parent_ == core::kInvalidId || pending_.empty()) {
    ctx.cancel();
    return;
  }
  std::vector<ObjectId> created;
  ObjectId sheetId = s->id;
  m->edit(pending_.size() == 1 ? "Projected view" : "Projected views", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    for (const auto& p : pending_)
      if (DrawingView* v = m->addProjectedView(*sh, parent_, p.dir, p.pos)) created.push_back(v->id);
  });
  if (DrawingCanvas* c = canvas(ctx)) c->clearSelection();  // UX-43: no lingering selected (cyan) views
  ctx.finish();
}

// ---------------------------------------------------------------------------------------
// SectionViewTool
// ---------------------------------------------------------------------------------------

SectionViewTool::SectionViewTool()
    : DrawingToolBase(drawingToolInfo("drawing.section-view", "Section View",
                                      "Cut a view along a line and place the hatched section", "CREATE", 30, true, {},
                                      {"SECTIONVIEW", "SECTION"})) {}

bool SectionViewTool::setParent(ObjectId id) {
  if (!ctx_) return false;
  Sheet* s = activeSheet(*ctx_);
  if (!s) return false;
  DrawingView* v = s->view(id);
  if (!v || v->type == ViewType::Detail) return false;
  parent_ = id;
  parentCopy_ = *v;
  standard_ = s->standard;
  step_ = Step::FirstPoint;
  setCanvasHighlights({parent_});
  ctx_->setPrompt("Click the first point of the section line");
  return true;
}

void SectionViewTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  step_ = Step::Parent;
  parent_ = core::kInvalidId;
  havePos_ = false;
  previewValid_ = false;
  preview_ = DrawingView{};
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.showMessage("No sheet: create a base view first", true);
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  DrawingView* parent = pickedView(ctx, *s);
  if (!parent) {
    std::vector<DrawingView*> candidates;
    for (auto& v : s->views)
      if (v.type != ViewType::Detail) candidates.push_back(&v);
    if (candidates.size() == 1) parent = candidates.front();
  }
  if (parent) setParent(parent->id);
  else ctx.setPrompt("Select the parent view");
  // command line: three points "x1,y1 x2,y2 x3,y3" (sheet mm) = line + placement
  std::vector<Point2> pts;
  for (const auto& a : ctx.args) {
    Point2 p;
    if (parsePoint(a, p)) pts.push_back(p);
  }
  if (parent_ != core::kInvalidId && pts.size() >= 3) {
    p1_ = pts[0];
    p2_ = pts[1];
    step_ = Step::Place;
    computePlacement(pts[2]);
    commit(ctx);
  } else if (parent_ == core::kInvalidId && !canvas_) {
    ctx.showMessage("Select a parent view first", true);
    ctx.cancel();
  }
}

bool SectionViewTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  Point2 p;
  if (!parsePoint(text, p) || parent_ == core::kInvalidId) return false;
  switch (step_) {
    case Step::FirstPoint:
      p1_ = cursor_ = p;
      step_ = Step::SecondPoint;
      ctx.setPrompt("Second point of the section line");
      return true;
    case Step::SecondPoint:
      if (p.distance(p1_) < 0.5) return false;
      p2_ = p;
      step_ = Step::Place;
      computePlacement(p + (p2_ - p1_).perp().normalized());
      requestPreview();
      ctx.setPrompt("Placement point of the section view");
      return true;
    case Step::Place:
      computePlacement(p);
      commit(ctx);
      return true;
    default:
      return false;
  }
}

Point2 SectionViewTool::snapPoint(const DrawingCanvas* c, const Point2& sheet, int modifiers) const {
  Point2 p = sheet;
  if (c && !(modifiers & core::ModAlt)) {
    Point2 snapped;
    if (c->snapToVertex(sheet, snapped)) p = snapped;
  }
  if (step_ == Step::SecondPoint && !(modifiers & core::ModShift)) {
    // ortho snap within ~10 degrees
    Point2 d = p - p1_;
    if (d.length() > 1e-9) {
      if (std::abs(d.y) < std::abs(d.x) * std::tan(deg2rad(10.0))) p.y = p1_.y;
      else if (std::abs(d.x) < std::abs(d.y) * std::tan(deg2rad(10.0))) p.x = p1_.x;
    }
  }
  return p;
}

void SectionViewTool::computePlacement(const Point2& cursor) {
  Point2 mid = (p1_ + p2_) * 0.5;
  Point2 d = (p2_ - p1_).normalized();
  if (d.length() < 0.5) return;
  Point2 nSheet = d.perp();  // sheet-space normal
  double side = (cursor - mid).dot(nSheet);
  if (std::abs(side) < 1e-9) side = 1.0;
  double sign = side > 0 ? 1.0 : -1.0;
  pos_ = mid + nSheet * side;
  havePos_ = true;
  // local (Y up) normal; viewing direction depends on the standard (see header notes)
  Point2 nLocal{nSheet.x, -nSheet.y};
  arrowLocal_ = (standard_ == ProjectionStandard::ThirdAngle) ? nLocal * (-sign) : nLocal * sign;
  arrowLocal_ = arrowLocal_.normalized();
}

void SectionViewTool::requestPreview() {
  if (!ctx_ || parent_ == core::kInvalidId) return;
  DrawingModel* m = model(*ctx_);
  if (!m) return;
  preview_.type = ViewType::Section;
  preview_.sources = parentCopy_.sources;
  preview_.orientation = ViewOrientation::Custom;
  gp_Ax2 pf = parentCopy_.geometryValid ? parentCopy_.frame : parentCopy_.definitionFrame();
  preview_.customFrame = sectionFrame(pf, parentCopy_.fromSheet(p1_), parentCopy_.fromSheet(p2_), arrowLocal_);
  preview_.scale = parentCopy_.scale;
  preview_.hatch = true;
  preview_.tangentEdges = parentCopy_.tangentEdges;
  previewValid_ = false;
  ViewJob job = m->makeJob(preview_, false);
  previews(*ctx_).request(job, [this](const ViewJobResult& r) {
    if (r.error.empty()) {
      preview_.curves = r.projection.curves;
      preview_.bbox = r.projection.bbox;
      previewValid_ = true;
    }
    refreshCanvas();
  });
}

void SectionViewTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)buttons;
  cursor_ = snapPoint(&c, sheet, modifiers);
  if (step_ == Step::Place) {
    Point2 oldArrow = arrowLocal_;
    computePlacement(sheet);
    if ((oldArrow - arrowLocal_).length() > 1e-6) requestPreview();
  }
}

void SectionViewTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  if (button != core::MouseButton::Left) return;
  switch (step_) {
    case Step::Parent: {
      CanvasHit h = c.hitTest(sheet, false);
      if (h.kind == HitKind::View) setParent(h.id);
      break;
    }
    case Step::FirstPoint:
      p1_ = snapPoint(&c, sheet, modifiers);
      cursor_ = p1_;
      step_ = Step::SecondPoint;
      if (ctx_) ctx_->setPrompt("Click the second point of the section line (Shift: free angle)");
      break;
    case Step::SecondPoint: {
      Point2 p = snapPoint(&c, sheet, modifiers);
      if (p.distance(p1_) < 0.5) return;
      p2_ = p;
      step_ = Step::Place;
      computePlacement(sheet + (p2_ - p1_).perp().normalized() * 1.0);
      requestPreview();
      if (ctx_) ctx_->setPrompt("Move to the side where the section view goes and click to place it");
      break;
    }
    case Step::Place:
      computePlacement(sheet);
      if (ctx_) commit(*ctx_);
      break;
    case Step::Done:
      break;
  }
}

bool SectionViewTool::canvasKey(DrawingCanvas& c, int key, int modifiers) {
  (void)c; (void)modifiers;
  if (key == Qt::Key_Escape && step_ == Step::SecondPoint) {
    step_ = Step::FirstPoint;
    if (ctx_) ctx_->setPrompt("Click the first point of the section line");
    return true;
  }
  return false;
}

void SectionViewTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  (void)c;
  if (step_ == Step::SecondPoint) {
    out.curves.push_back(Curve2D::segment(p1_, cursor_, LineClass::Center));
    out.curves.push_back(Curve2D::circle(p1_, 1.0, LineClass::Dimension));
  } else if (step_ == Step::Place && havePos_) {
    // cut line with arrows
    Point2 d = (p2_ - p1_).normalized();
    Point2 arrowSheet{arrowLocal_.x, -arrowLocal_.y};
    Point2 a0 = p1_ - d * 4.0, b0 = p2_ + d * 4.0;
    out.curves.push_back(Curve2D::segment(a0, b0, LineClass::Center));
    for (Point2 e : {a0, b0}) {
      Point2 tail = e - arrowSheet * 6.0;
      out.curves.push_back(Curve2D::segment(tail, e, LineClass::Dimension));
      Point2 tip = e;
      Point2 back = tip - arrowSheet * 3.0;
      out.curves.push_back(Curve2D::segment(tip, back + arrowSheet.perp() * 1.0, LineClass::Dimension));
      out.curves.push_back(Curve2D::segment(tip, back - arrowSheet.perp() * 1.0, LineClass::Dimension));
    }
    if (previewValid_) {
      addPreviewView(out, preview_, pos_);
    } else {
      BBox2 b = parentCopy_.sheetBBox();
      BBox2 r;
      r.add(pos_ - Point2{b.width() / 2, b.height() / 2});
      r.add(pos_ + Point2{b.width() / 2, b.height() / 2});
      addPlaceholder(out, r);
    }
  } else if (step_ == Step::FirstPoint) {
    out.curves.push_back(Curve2D::circle(cursor_, 1.0, LineClass::Dimension));
  }
}

std::vector<ObjectId> SectionViewTool::canvasHighlights() const {
  if (parent_ == core::kInvalidId) return {};
  return {parent_};
}

void SectionViewTool::commit(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s || parent_ == core::kInvalidId || !havePos_) {
    ctx.cancel();
    return;
  }
  SectionLine line;
  line.p1 = parentCopy_.fromSheet(p1_);
  line.p2 = parentCopy_.fromSheet(p2_);
  line.arrow = arrowLocal_;
  ObjectId sheetId = s->id, created = core::kInvalidId;
  m->edit("Section view", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    if (DrawingView* v = m->addSectionView(*sh, parent_, line, pos_)) created = v->id;
  });
  step_ = Step::Done;
  if (created != core::kInvalidId)
    if (DrawingCanvas* c = canvas(ctx)) c->clearSelection();  // UX-43: no lingering selected (cyan) views
  ctx.finish();
}

void SectionViewTool::accept(core::ToolContext& ctx) {
  if (step_ == Step::Place && havePos_) commit(ctx);
  else ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// DetailViewTool
// ---------------------------------------------------------------------------------------

DetailViewTool::DetailViewTool()
    : DrawingToolBase(drawingToolInfo("drawing.detail-view", "Detail View", "Magnified circular detail of a view",
                                      "CREATE", 40, true, {}, {"DETAILVIEW", "DETAIL"})) {}

bool DetailViewTool::setParent(ObjectId id) {
  if (!ctx_) return false;
  Sheet* s = activeSheet(*ctx_);
  if (!s) return false;
  DrawingView* v = s->view(id);
  if (!v) return false;
  parent_ = id;
  parentCopy_ = *v;
  step_ = Step::Center;
  setCanvasHighlights({parent_});
  ctx_->setPrompt("Click the centre of the detail circle");
  return true;
}

void DetailViewTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  step_ = Step::Parent;
  parent_ = core::kInvalidId;
  radiusLocal_ = 0;
  havePos_ = false;
  scale_ = 0;
  Sheet* s = activeSheet(ctx);
  if (!s) {
    ctx.showMessage("No sheet: create a base view first", true);
    ctx.cancel();
    return;
  }
  attachCanvas(ctx);
  DrawingView* parent = pickedView(ctx, *s);
  if (!parent && s->views.size() == 1) parent = &s->views.front();
  if (parent) setParent(parent->id);
  else ctx.setPrompt("Select the parent view");
  // command line: "cx,cy r px,py [scale]"
  std::vector<Point2> pts;
  double radius = 0;
  for (const auto& a : ctx.args) {
    Point2 p;
    double sc;
    if (parsePoint(a, p)) pts.push_back(p);
    else if (parseScale(a, sc)) {
      if (radius <= 0 && pts.size() == 1) radius = sc;
      else scale_ = sc;
    }
  }
  if (parent_ != core::kInvalidId && pts.size() >= 2 && radius > 0) {
    centerLocal_ = parentCopy_.fromSheet(pts[0]);
    radiusLocal_ = radius / parentCopy_.scale;
    pos_ = pts[1];
    havePos_ = true;
    step_ = Step::Place;
    commit(ctx);
  } else if (parent_ == core::kInvalidId && !canvas_) {
    ctx.showMessage("Select a parent view first", true);
    ctx.cancel();
  }
}

bool DetailViewTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  (void)ctx;
  Point2 p;
  double r;
  if (step_ == Step::Radius && parseScale(text, r) && r > 0) {
    radiusLocal_ = r / parentCopy_.scale;
    step_ = Step::Place;
    return true;
  }
  if (!parsePoint(text, p) || !canvas_) return false;
  canvasPress(*canvas_, p, core::MouseButton::Left, 0);
  return true;
}

void DetailViewTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
  if (step_ == Step::Radius) radiusLocal_ = parentCopy_.fromSheet(sheet).distance(centerLocal_);
  if (step_ == Step::Place) {
    pos_ = sheet;
    havePos_ = true;
  }
}

void DetailViewTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  (void)modifiers;
  if (button != core::MouseButton::Left) return;
  switch (step_) {
    case Step::Parent: {
      CanvasHit h = c.hitTest(sheet, false);
      if (h.kind == HitKind::View) setParent(h.id);
      break;
    }
    case Step::Center:
      centerLocal_ = parentCopy_.fromSheet(sheet);
      step_ = Step::Radius;
      if (ctx_) ctx_->setPrompt("Drag/click to set the detail radius");
      break;
    case Step::Radius: {
      double r = parentCopy_.fromSheet(sheet).distance(centerLocal_);
      if (r < 0.5) return;
      radiusLocal_ = r;
      step_ = Step::Place;
      if (ctx_) ctx_->setPrompt("Click to place the detail view");
      break;
    }
    case Step::Place:
      pos_ = sheet;
      havePos_ = true;
      if (ctx_) commit(*ctx_);
      break;
    case Step::Done:
      break;
  }
}

void DetailViewTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  (void)c;
  if (parent_ == core::kInvalidId) return;
  Point2 cs = parentCopy_.toSheet(centerLocal_);
  if (step_ == Step::Radius || step_ == Step::Place) {
    out.curves.push_back(Curve2D::circle(cs, std::max(0.5, radiusLocal_ * parentCopy_.scale), LineClass::Detail));
  }
  if (step_ == Step::Place && havePos_) {
    double sc = scale_ > 0 ? scale_ : parentCopy_.scale * 2.0;
    DrawingView tmp;
    tmp.scale = sc;
    for (const auto& curve : parentCopy_.curves)
      for (auto& piece : clipToCircle(curve, centerLocal_, radiusLocal_)) tmp.curves.push_back(piece.transformed(-centerLocal_, 1.0, false));
    tmp.curves.push_back(Curve2D::circle({0, 0}, radiusLocal_, LineClass::Detail));
    addPreviewView(out, tmp, pos_);
  }
}

std::vector<ObjectId> DetailViewTool::canvasHighlights() const {
  if (parent_ == core::kInvalidId) return {};
  return {parent_};
}

void DetailViewTool::commit(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s || parent_ == core::kInvalidId || !havePos_ || radiusLocal_ <= 0) {
    ctx.cancel();
    return;
  }
  DetailSpec spec;
  spec.center = centerLocal_;
  spec.radius = radiusLocal_;
  ObjectId sheetId = s->id, created = core::kInvalidId;
  double sc = scale_ > 0 ? scale_ : parentCopy_.scale * 2.0;
  m->edit("Detail view", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    if (DrawingView* v = m->addDetailView(*sh, parent_, spec, sc, pos_)) created = v->id;
  });
  step_ = Step::Done;
  if (created != core::kInvalidId)
    if (DrawingCanvas* c = canvas(ctx)) c->clearSelection();  // UX-43: no lingering selected (cyan) views
  ctx.finish();
}

void DetailViewTool::accept(core::ToolContext& ctx) {
  if (step_ == Step::Place && havePos_) commit(ctx);
  else ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// EditViewTool
// ---------------------------------------------------------------------------------------

EditViewTool::EditViewTool()
    : DrawingToolBase(drawingToolInfo("drawing.edit-view", "Edit View", "Change orientation, scale and style of a view",
                                      "MODIFY", 10, true, {}, {"EDITVIEW", "VIEW"})) {}

void EditViewTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  committed_ = false;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.cancel();
    return;
  }
  DrawingView* v = pickedView(ctx, *s);
  if (!v) {
    ctx.showMessage("Select a view to edit", true);
    ctx.cancel();
    return;
  }
  viewId_ = v->id;
  before_ = m->toJson();
  attachCanvas(ctx);
  setCanvasHighlights({viewId_});
  bool anySetting = false;
  for (const auto& a : ctx.args) {
    std::string k, val;
    if (parseKeyValue(a, k, val)) anySetting = applySetting(ctx, k, val) || anySetting;
  }
  ctx.setPrompt("Edit " + v->label + ", then OK");
  if (anySetting || !hasDialogShell(ctx)) accept(ctx);
}

bool EditViewTool::applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value) {
  DrawingModel* m = model(ctx);
  if (!m) return false;
  Sheet* s = nullptr;
  DrawingView* v = m->view(viewId_, &s);
  if (!v) return false;
  bool b;
  double d;
  ViewOrientation o;
  bool ok = false;
  if (key == "scale" && parseScale(value, d)) { v->scale = d; ok = true; }
  else if (key == "orientation" && v->type == ViewType::Base && parseOrientation(value, o)) { v->orientation = o; v->customFrame = frameForOrientation(o); ok = true; }
  else if ((key == "hidden" || key == "style") && parseBool(value, b)) { v->hiddenLines = b; ok = true; }
  else if (key == "style" && lower(value).find("hidden") != std::string::npos) { v->hiddenLines = true; ok = true; }
  else if (key == "tangent" && parseBool(value, b)) { v->tangentEdges = b ? TangentEdgeMode::Full : TangentEdgeMode::Off; ok = true; }
  else if (key == "label") { if (v->type == ViewType::Section) v->section.label = value; else if (v->type == ViewType::Detail) v->detail.label = value; else v->label = value; ok = true; }
  else if (key == "showlabel" && parseBool(value, b)) { v->showLabel = b; ok = true; }
  else if (key == "hatch" && parseBool(value, b)) { v->hatch = b; ok = true; }
  else if (key == "exact" && parseBool(value, b)) { v->exact = b; ok = true; }
  else if (key == "radius" && v->type == ViewType::Detail && parseScale(value, d)) { v->detail.radius = d; ok = true; }
  if (ok) {
    m->viewDefinitionChanged(viewId_);
    m->updateGeometry();
  }
  return ok;
}

bool EditViewTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  bool any = false;
  for (const auto& t : split(text)) {
    std::string k, v;
    if (parseKeyValue(t, k, v)) any = applySetting(ctx, k, v) || any;
  }
  return any;
}

QWidget* EditViewTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  DrawingModel* m = model(ctx);
  const DrawingView* v = m ? m->view(viewId_) : nullptr;
  if (!v) return nullptr;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  labelEdit_ = new QLineEdit(QString::fromStdString(v->type == ViewType::Section ? v->section.label
                                                    : v->type == ViewType::Detail ? v->detail.label : v->label), w);
  form->addRow(tr("Label"), labelEdit_);
  labelCheck_ = new QCheckBox(tr("Show label"), w);
  labelCheck_->setChecked(v->showLabel || v->type == ViewType::Section || v->type == ViewType::Detail);
  form->addRow(QString(), labelCheck_);
  if (v->type == ViewType::Base) orientationCombo_ = addOrientationCombo(form, v->orientation);
  styleCombo_ = new QComboBox(w);
  fillStyleCombo(styleCombo_, v->hiddenLines);
  form->addRow(tr("Style"), styleCombo_);
  scaleCombo_ = addScaleCombo(form, v->scale);
  tangentCombo_ = new QComboBox(w);
  fillTangentCombo(tangentCombo_, v->tangentEdges);
  form->addRow(tr("Tangent Edges"), tangentCombo_);
  if (v->type == ViewType::Section) {
    hatchCheck_ = new QCheckBox(tr("Hatch"), w);
    hatchCheck_->setChecked(v->hatch);
    form->addRow(QString(), hatchCheck_);
  }
  if (v->type == ViewType::Detail) {
    radiusSpin_ = new QDoubleSpinBox(w);
    radiusSpin_->setRange(0.1, 10000);
    radiusSpin_->setSuffix(" mm");
    radiusSpin_->setValue(v->detail.radius);
    form->addRow(tr("Radius"), radiusSpin_);
  }
  exactCheck_ = new QCheckBox(tr("Exact hidden-line removal (slower)"), w);
  exactCheck_->setChecked(v->exact);
  form->addRow(QString(), exactCheck_);

  auto apply = [this]() { applyDialog(); };
  connect(labelEdit_, &QLineEdit::editingFinished, this, apply);
  connect(labelCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
  if (orientationCombo_) connect(orientationCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(styleCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(scaleCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(scaleCombo_->lineEdit(), &QLineEdit::editingFinished, this, apply);
  connect(tangentCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  if (hatchCheck_) connect(hatchCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
  if (radiusSpin_) connect(radiusSpin_, &QDoubleSpinBox::valueChanged, this, [apply](double) { apply(); });
  connect(exactCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
  return w;
}

void EditViewTool::applyDialog() {
  if (!ctx_) return;
  DrawingModel* m = model(*ctx_);
  DrawingView* v = m ? m->view(viewId_) : nullptr;
  if (!v) return;
  bool changed = false;
  auto set = [&](auto& field, const auto& value) {
    if (field != value) {
      field = value;
      changed = true;
    }
  };
  if (labelEdit_) {
    std::string l = labelEdit_->text().toStdString();
    if (!l.empty()) {
      if (v->type == ViewType::Section) set(v->section.label, l);
      else if (v->type == ViewType::Detail) set(v->detail.label, l);
      else set(v->label, l);
    }
  }
  if (labelCheck_) set(v->showLabel, labelCheck_->isChecked());
  if (orientationCombo_) {
    auto o = static_cast<ViewOrientation>(orientationCombo_->currentData().toInt());
    if (o != v->orientation) {
      v->orientation = o;
      v->customFrame = frameForOrientation(o);
      changed = true;
    }
  }
  if (styleCombo_) set(v->hiddenLines, styleCombo_->currentData().toInt() == 1);
  if (scaleCombo_) set(v->scale, comboScale(scaleCombo_, v->scale));
  if (tangentCombo_) set(v->tangentEdges, tangentCombo_->currentData().toInt() == 1 ? TangentEdgeMode::Full : TangentEdgeMode::Off);
  if (hatchCheck_) set(v->hatch, hatchCheck_->isChecked());
  if (radiusSpin_) set(v->detail.radius, radiusSpin_->value());
  if (exactCheck_) set(v->exact, exactCheck_->isChecked());
  if (changed) {
    m->viewDefinitionChanged(viewId_);
    m->updateGeometry();
  }
}

void EditViewTool::accept(core::ToolContext& ctx) {
  applyDialog();
  if (DrawingModel* m = model(ctx)) {
    m->commitSnapshot("Edit view", ctx.undo, before_);
    // after a scale change the automatic layout keeps the views apart; when they no longer fit
    // the sheet, say so rather than leaving it to be noticed (DRW-VIEWS-ANNOT-8)
    m->waitForIdle(30000);
    if (Sheet* s = m->sheetOf(viewId_)) {
      const BBox2 f = s->frameRect();
      for (const auto& v : s->views)
        if (v.geometryValid) {
          const BBox2 b = v.sheetBBox();
          if (b.min.x < f.min.x - 0.5 || b.max.x > f.max.x + 0.5 || b.min.y < f.min.y - 0.5 || b.max.y > f.max.y + 0.5) {
            ctx.showMessage("The views no longer fit inside the frame: use a smaller scale or a larger sheet", true);
            break;
          }
        }
    }
  }
  committed_ = true;
  ctx.finish();
}

void EditViewTool::cancel(core::ToolContext& ctx) {
  if (!committed_)
    if (DrawingModel* m = model(ctx)) m->revertTo(before_);
  ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// EditSheetTool
// ---------------------------------------------------------------------------------------

EditSheetTool::EditSheetTool()
    : DrawingToolBase(drawingToolInfo("drawing.edit-sheet", "Edit Sheet", "Sheet name, size, standard and scale",
                                      "MODIFY", 20, false, {}, {"EDITSHEET", "SHEETSETTINGS"})) {}

void EditSheetTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.cancel();
    return;
  }
  sheetId_ = s->id;
  before_ = m->toJson();
  bool any = false;
  for (const auto& a : ctx.args) {
    std::string k, v;
    if (parseKeyValue(a, k, v)) any = applySetting(ctx, k, v) || any;
  }
  ctx.setPrompt("Edit sheet settings, then OK");
  if (any || !hasDialogShell(ctx)) accept(ctx);
}

bool EditSheetTool::applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheet(sheetId_) : nullptr;
  if (!s) return false;
  bool ok = false, b;
  double d;
  ProjectionStandard st;
  if (key == "name" && !value.empty()) { s->name = value; ok = true; }
  else if ((key == "size" || key == "format")) {
    if (auto f = sheetFormatByName(value)) {
      SheetFormat nf = *f;
      nf.landscape = s->format.landscape;
      if (!nf.landscape) std::swap(nf.width, nf.height);
      m->changeSheetFormat(*s, nf);  // carries views and tables over (DRW-ASM-OUTPUT-2)
      ok = true;
    }
  } else if (key == "orientation") {
    std::string v = lower(value);
    if (v == "portrait" || v == "landscape") {
      bool landscape = v == "landscape";
      if (landscape != s->format.landscape) {
        SheetFormat nf = s->format;
        nf.landscape = landscape;
        std::swap(nf.width, nf.height);
        m->changeSheetFormat(*s, nf);
      }
      ok = true;
    }
  } else if (key == "standard" && parseStandard(value, st)) {
    m->changeSheetStandard(*s, st);  // re-arranges projected views (DRW-VIEWS-ANNOT-7)
    ok = true;
  }
  else if (key == "scale" && parseScale(value, d)) { s->scale = d; ok = true; }
  else if (key == "zones" && parseBool(value, b)) { s->showZones = b; ok = true; }
  else if (key == "titleblock" && parseBool(value, b)) { s->titleBlock.visible = b; ok = true; }
  if (ok) {
    m->notifyChanged(sheetId_);
    emit m->sheetsChanged();
  }
  return ok;
}

bool EditSheetTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  bool any = false;
  for (const auto& t : split(text)) {
    std::string k, v;
    if (parseKeyValue(t, k, v)) any = applySetting(ctx, k, v) || any;
  }
  // keep the open dialog in step, or its OK would re-apply the old values
  if (any)
    if (DrawingModel* m = model(ctx))
      if (const Sheet* s = m->sheet(sheetId_)) {
        auto sync = [](QWidget* w, auto fn) {
          if (!w) return;
          const bool b = w->blockSignals(true);
          fn();
          w->blockSignals(b);
        };
        sync(standardCombo_, [&] { standardCombo_->setCurrentIndex(s->standard == ProjectionStandard::FirstAngle ? 0 : 1); });
        sync(orientationCombo_, [&] { orientationCombo_->setCurrentIndex(s->format.landscape ? 0 : 1); });
        sync(formatCombo_, [&] {
          int i = formatCombo_->findData(QString::fromStdString(s->format.name));
          if (i >= 0) formatCombo_->setCurrentIndex(i);
        });
        sync(nameEdit_, [&] { nameEdit_->setText(QString::fromStdString(s->name)); });
        sync(zonesCheck_, [&] { zonesCheck_->setChecked(s->showZones); });
        sync(titleCheck_, [&] { titleCheck_->setChecked(s->titleBlock.visible); });
        sync(scaleCombo_, [&] { scaleCombo_->setEditText(QString::fromStdString(scaleLabel(s->scale))); });
      }
  return any;
}

QWidget* EditSheetTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  DrawingModel* m = model(ctx);
  const Sheet* s = m ? m->sheet(sheetId_) : nullptr;
  if (!s) return nullptr;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  nameEdit_ = new QLineEdit(QString::fromStdString(s->name), w);
  form->addRow(tr("Name"), nameEdit_);
  formatCombo_ = new QComboBox(w);
  fillFormatCombo(formatCombo_, s->format.name);
  form->addRow(tr("Sheet Size"), formatCombo_);
  orientationCombo_ = new QComboBox(w);
  orientationCombo_->addItem(tr("Landscape"), 1);
  orientationCombo_->addItem(tr("Portrait"), 0);
  orientationCombo_->setCurrentIndex(s->format.landscape ? 0 : 1);
  form->addRow(tr("Orientation"), orientationCombo_);
  standardCombo_ = new QComboBox(w);
  fillStandardCombo(standardCombo_, s->standard);
  form->addRow(tr("Standard"), standardCombo_);
  scaleCombo_ = addScaleCombo(form, s->scale);
  zonesCheck_ = new QCheckBox(tr("Zone letters/numbers"), w);
  zonesCheck_->setChecked(s->showZones);
  form->addRow(QString(), zonesCheck_);
  titleCheck_ = new QCheckBox(tr("Title block"), w);
  titleCheck_->setChecked(s->titleBlock.visible);
  form->addRow(QString(), titleCheck_);
  auto apply = [this]() { applyDialog(); };
  connect(nameEdit_, &QLineEdit::editingFinished, this, apply);
  connect(formatCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(orientationCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(standardCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(scaleCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
  connect(scaleCombo_->lineEdit(), &QLineEdit::editingFinished, this, apply);
  connect(zonesCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
  connect(titleCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
  return w;
}

void EditSheetTool::applyDialog() {
  if (!ctx_) return;
  if (nameEdit_) applySetting(*ctx_, "name", nameEdit_->text().toStdString());
  if (formatCombo_) applySetting(*ctx_, "size", formatCombo_->currentData().toString().toStdString());
  if (orientationCombo_) applySetting(*ctx_, "orientation", orientationCombo_->currentData().toInt() == 1 ? "landscape" : "portrait");
  if (standardCombo_) applySetting(*ctx_, "standard", standardCombo_->currentIndex() == 0 ? "iso" : "asme");
  if (scaleCombo_) applySetting(*ctx_, "scale", scaleLabel(comboScale(scaleCombo_, 1.0)));
  if (zonesCheck_) applySetting(*ctx_, "zones", zonesCheck_->isChecked() ? "on" : "off");
  if (titleCheck_) applySetting(*ctx_, "titleblock", titleCheck_->isChecked() ? "on" : "off");
}

void EditSheetTool::accept(core::ToolContext& ctx) {
  applyDialog();
  if (DrawingModel* m = model(ctx)) m->commitSnapshot("Edit sheet", ctx.undo, before_);
  if (DrawingCanvas* c = canvas(ctx)) c->zoomToFit();
  ctx.finish();
}

void EditSheetTool::cancel(core::ToolContext& ctx) {
  if (DrawingModel* m = model(ctx)) m->revertTo(before_);
  ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// EditTitleBlockTool
// ---------------------------------------------------------------------------------------

EditTitleBlockTool::EditTitleBlockTool()
    : DrawingToolBase(drawingToolInfo("drawing.edit-title-block", "Edit Title Block", "Title block fields", "MODIFY", 30,
                                      false, {}, {"TITLEBLOCK"})) {}

void EditTitleBlockTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    ctx.cancel();
    return;
  }
  sheetId_ = s->id;
  before_ = m->toJson();
  bool any = false;
  for (const auto& a : ctx.args) {
    std::string k, v;
    if (parseKeyValue(a, k, v)) {
      // match field names case-insensitively
      for (const auto& name : TitleBlock::fieldNames())
        if (lower(name) == k) {
          m->setTitleBlockField(*s, name, v);
          any = true;
        }
    }
  }
  if (any) m->notifyChanged(sheetId_);
  ctx.setPrompt("Edit the title block fields, then OK");
  if (any || !hasDialogShell(ctx)) accept(ctx);
}

bool EditTitleBlockTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  DrawingModel* m = model(ctx);
  Sheet* s = m ? m->sheet(sheetId_) : nullptr;
  if (!s) return false;
  std::string k, v;
  if (!parseKeyValue(text, k, v)) return false;
  for (const auto& name : TitleBlock::fieldNames())
    if (lower(name) == k) {
      m->setTitleBlockField(*s, name, v);
      if (auto it = edits_.find(name); it != edits_.end() && it->second) it->second->setText(QString::fromStdString(v));
      return true;
    }
  return false;
}

QWidget* EditTitleBlockTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  DrawingModel* m = model(ctx);
  const Sheet* s = m ? m->sheet(sheetId_) : nullptr;
  if (!s) return nullptr;
  auto values = titleBlockValues(*m, *s);
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  edits_.clear();
  for (const auto& name : TitleBlock::fieldNames()) {
    auto* e = new QLineEdit(QString::fromStdString(s->titleBlock.field(name)), w);
    e->setPlaceholderText(QString::fromStdString(values[name]));
    form->addRow(QString::fromStdString(name), e);
    edits_[name] = e;
    connect(e, &QLineEdit::editingFinished, this, [this]() { applyDialog(); });
  }
  return w;
}

void EditTitleBlockTool::applyDialog() {
  if (!ctx_) return;
  DrawingModel* m = model(*ctx_);
  Sheet* s = m ? m->sheet(sheetId_) : nullptr;
  if (!s) return;
  bool changed = false;
  for (auto& [name, edit] : edits_) {
    if (!edit) continue;
    std::string v = edit->text().toStdString();
    if (s->titleBlock.field(name) != v) {
      m->setTitleBlockField(*s, name, v);
      changed = true;
    }
  }
  if (changed) m->notifyChanged(sheetId_);
}

void EditTitleBlockTool::accept(core::ToolContext& ctx) {
  applyDialog();
  if (DrawingModel* m = model(ctx)) m->commitSnapshot("Edit title block", ctx.undo, before_);
  ctx.finish();
}

void EditTitleBlockTool::cancel(core::ToolContext& ctx) {
  if (DrawingModel* m = model(ctx)) m->revertTo(before_);
  ctx.cancel();
}

// ---------------------------------------------------------------------------------------
// ToggleViewOptionTool
// ---------------------------------------------------------------------------------------

ToggleViewOptionTool::ToggleViewOptionTool(Option option)
    : DrawingToolBase(option == Option::HiddenLines
                          ? drawingToolInfo("drawing.toggle-hidden-lines", "Hidden Lines", "Toggle hidden lines of the selected views",
                                            "MODIFY", 40, false, {}, {"HIDDENLINES", "HIDDEN"})
                          : drawingToolInfo("drawing.toggle-tangent-edges", "Tangent Edges", "Toggle tangent edges of the selected views",
                                            "MODIFY", 50, false, {}, {"TANGENTEDGES", "TANGENT"})),
      option_(option) {}

void ToggleViewOptionTool::activate(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return;
  std::vector<ObjectId> ids;
  for (ObjectId id : targetObjects(ctx, canvas(ctx), *s))
    if (s->view(id)) ids.push_back(id);
  if (ids.empty())
    for (const auto& v : s->views) ids.push_back(v.id);
  if (ids.empty()) return;
  ObjectId sheetId = s->id;
  m->edit(option_ == Option::HiddenLines ? "Toggle hidden lines" : "Toggle tangent edges", ctx.undo, [&]() {
    Sheet* sh = m->sheet(sheetId);
    if (!sh) return;
    // all on -> off, otherwise on
    bool allOn = true;
    for (ObjectId id : ids)
      if (const DrawingView* v = sh->view(id))
        allOn = allOn && (option_ == Option::HiddenLines ? v->hiddenLines : v->tangentEdges != TangentEdgeMode::Off);
    for (ObjectId id : ids) {
      DrawingView* v = sh->view(id);
      if (!v) continue;
      if (option_ == Option::HiddenLines) v->hiddenLines = !allOn;
      else v->tangentEdges = allOn ? TangentEdgeMode::Off : TangentEdgeMode::Full;
      m->viewDefinitionChanged(id);
    }
  });
}

// ---------------------------------------------------------------------------------------
// MoveTool
// ---------------------------------------------------------------------------------------

MoveTool::MoveTool()
    : DrawingToolBase(drawingToolInfo("drawing.move", "Move", "Move views and annotations", "CREATE", 50, false, "M",
                                      {"MOVE", "M"})) {}

void MoveTool::collectIds(core::ToolContext& ctx) {
  ids_.clear();
  Sheet* s = activeSheet(ctx);
  if (!s) return;
  ids_ = targetObjects(ctx, canvas(ctx), *s);
}

void MoveTool::activate(core::ToolContext& ctx) {
  ctx_ = &ctx;
  haveBase_ = false;
  collectIds(ctx);
  attachCanvas(ctx);
  Point2 delta;
  for (const auto& a : ctx.args)
    if (parsePoint(a, delta) && !ids_.empty()) {
      commit(ctx, delta);
      return;
    }
  if (!canvas_) {
    ctx.showMessage("Move: select objects and give a delta 'dx,dy'", true);
    ctx.cancel();
    return;
  }
  ctx.setPrompt(ids_.empty() ? "Select the objects to move" : "Click the base point");
}

bool MoveTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  Point2 p;
  if (!parsePoint(text, p)) return false;
  if (ids_.empty()) collectIds(ctx);
  if (ids_.empty()) return false;
  if (!haveBase_) {
    // "@dx,dy" relative; plain point = base point
    if (!text.empty() && text[0] == '@') {
      Point2 d;
      if (parsePoint(text.substr(1), d)) commit(ctx, d);
      return true;
    }
    base_ = p;
    haveBase_ = true;
    return true;
  }
  commit(ctx, p - base_);
  return true;
}

void MoveTool::canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
  (void)c; (void)buttons; (void)modifiers;
  cursor_ = sheet;
}

void MoveTool::canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
  if (button != core::MouseButton::Left) return;
  if (ids_.empty()) {
    CanvasHit h = c.hitTest(sheet, false);
    if (h.isObject() && h.kind != HitKind::TitleBlock) {
      c.select(h.id, (modifiers & core::ModShift) != 0);
      ids_ = c.selection();
      if (ctx_) ctx_->setPrompt("Click the base point");
    }
    return;
  }
  if (!haveBase_) {
    base_ = sheet;
    haveBase_ = true;
    if (ctx_) ctx_->setPrompt("Click the destination");
    return;
  }
  if (ctx_) commit(*ctx_, sheet - base_);
}

void MoveTool::canvasPreview(const DrawingCanvas& c, DisplayList& out) {
  if (!haveBase_) return;
  out.curves.push_back(Curve2D::segment(base_, cursor_, LineClass::Center));
  Point2 delta = cursor_ - base_;
  const Sheet* s = c.sheet();
  if (!s) return;
  for (ObjectId id : ids_)
    if (const DrawingView* v = s->view(id)) {
      BBox2 b = c.viewRect(*v);
      BBox2 r;
      r.add(b.min + delta);
      r.add(b.max + delta);
      addPlaceholder(out, r);
    }
}

void MoveTool::commit(core::ToolContext& ctx, const Point2& delta) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s || ids_.empty()) {
    ctx.cancel();
    return;
  }
  ObjectId sheetId = s->id;
  std::vector<ObjectId> ids = ids_;
  m->edit("Move", ctx.undo, [&]() {
    if (Sheet* sh = m->sheet(sheetId)) m->moveObjects(*sh, ids, delta);
  });
  ctx.finish();
}

// ---------------------------------------------------------------------------------------
// DeleteTool
// ---------------------------------------------------------------------------------------

DeleteTool::DeleteTool()
    : DrawingToolBase(drawingToolInfo("drawing.delete", "Delete", "Delete the selected views and annotations", "CREATE",
                                      60, false, "Del", {"DELETE", "ERASE", "DEL"})) {}
// "Del" (DRW-VIEWS-ANNOT-5): the window routes Delete as a shortcut before the canvas sees it;
// in the Drawing workspace this tool outranks the design's app.delete (same key).

void DeleteTool::activate(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) return;
  std::vector<ObjectId> ids = targetObjects(ctx, canvas(ctx), *s);
  if (ids.empty()) {
    ctx.showMessage("Nothing selected", true);
    return;
  }
  // DRW-VIEWS-ANNOT-17: deleting a parent view also removes its projected/section/detail views
  // and everything attached to them. Say so (and ask) instead of cascading silently.
  std::set<ObjectId> doomedViews, doomedEntities;
  std::function<void(ObjectId)> collect = [&](ObjectId vid) {
    if (!doomedViews.insert(vid).second) return;
    for (ObjectId e : s->entitiesOf(vid)) doomedEntities.insert(e);
    for (ObjectId c : s->childrenOf(vid)) collect(c);
  };
  std::string firstLabel;
  for (ObjectId id : ids)
    if (const DrawingView* v = s->view(id)) {
      if (firstLabel.empty()) firstLabel = v->label;
      collect(id);
    }
  size_t dependentViews = 0;
  for (ObjectId v : doomedViews)
    if (std::find(ids.begin(), ids.end(), v) == ids.end()) ++dependentViews;
  size_t dependentEntities = 0;
  for (ObjectId e : doomedEntities)
    if (std::find(ids.begin(), ids.end(), e) == ids.end()) ++dependentEntities;
  if (dependentViews > 0 || dependentEntities > 0) {
    std::string what;
    if (dependentViews) what += std::to_string(dependentViews) + (dependentViews == 1 ? " dependent view" : " dependent views");
    if (dependentEntities) {
      if (!what.empty()) what += " and ";
      what += std::to_string(dependentEntities) + (dependentEntities == 1 ? " annotation" : " annotations");
    }
    const std::string subject = firstLabel.empty() ? std::string("the selection") : firstLabel;
    if (ctx.host && !ctx.host->askYesNo("Delete", "Deleting " + subject + " also deletes " + what + ". Continue?", true)) {
      ctx.showMessage("Delete cancelled");
      return;
    }
    ctx.showMessage("Deleted " + subject + " with " + what + " (Ctrl+Z restores)");
  }
  if (DrawingCanvas* c = canvas(ctx)) c->clearSelection();
  m->edit(ids.size() == 1 ? "Delete" : "Delete objects", ctx.undo, [&]() {
    for (ObjectId id : ids) m->removeObject(id);
  });
}

}  // namespace fabricad::drawing
