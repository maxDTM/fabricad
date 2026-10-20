// FabriCAD drawing: DrawingToolBase / PreviewRunner implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/tools/DrawingToolBase.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "drawing/Sheet.h"
#include "drawing/SheetRenderer.h"

#include <QComboBox>
#include <QFormLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fabricad::drawing {

using core::ObjectId;

// ---------------------------------------------------------------------------------------
// PreviewRunner
// ---------------------------------------------------------------------------------------

PreviewRunner::PreviewRunner(QObject* parent) : QObject(parent) {}

PreviewRunner::~PreviewRunner() { cancel(); }

void PreviewRunner::cancel() {
  ++generation_;
  if (watcher_) {
    watcher_->disconnect(this);
    watcher_->waitForFinished();
    delete watcher_;
    watcher_ = nullptr;
  }
  busy_ = false;
}

void PreviewRunner::request(ViewJob job, std::function<void(const ViewJobResult&)> done) {
  const int gen = ++generation_;
  job.generation = gen;
  if (synchronous_) {
    ViewJobResult r = runViewJob(job);
    if (gen == generation_ && done) done(r);
    return;
  }
  busy_ = true;
  // let a running job finish in the background; its result is dropped by the generation check
  if (watcher_) {
    watcher_->disconnect(this);
    QFutureWatcher<ViewJobResult>* old = watcher_;
    if (old->isFinished()) old->deleteLater();
    else connect(old, &QFutureWatcher<ViewJobResult>::finished, old, &QObject::deleteLater);
    watcher_ = nullptr;
  }
  watcher_ = new QFutureWatcher<ViewJobResult>(this);
  connect(watcher_, &QFutureWatcher<ViewJobResult>::finished, this, [this, gen, done]() {
    QFutureWatcher<ViewJobResult>* w = watcher_;
    if (!w) return;
    ViewJobResult r = w->result();
    watcher_ = nullptr;
    w->deleteLater();
    busy_ = false;
    if (gen == generation_ && done) done(r);
  });
  watcher_->setFuture(QtConcurrent::run(runViewJob, job));
}

// ---------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------

core::ToolInfo drawingToolInfo(const std::string& id, const std::string& name, const std::string& tooltip,
                               const std::string& group, int order, bool promoted, const std::string& shortcut,
                               std::vector<std::string> aliases) {
  core::ToolInfo i;
  i.id = id;
  i.name = name;
  i.tooltip = tooltip;
  i.icon = id;
  i.shortcut = shortcut;
  i.aliases = std::move(aliases);
  i.workspace = core::Workspace::Drawing;
  i.tab = "DRAWING";
  i.group = group;
  i.order = order;
  i.promoted = promoted;
  return i;
}

DrawingToolBase::DrawingToolBase(core::ToolInfo info) : core::ToolBase(std::move(info)) {}

DrawingToolBase::~DrawingToolBase() { detachCanvas(); }

bool DrawingToolBase::isEnabled(const core::ToolContext& ctx) const {
  if (!ctx.document) return false;
  if (!anyWorkspace_ && ctx.host && ctx.host->workspace() != core::Workspace::Drawing) return false;
  return true;
}

void DrawingToolBase::deactivate(core::ToolContext& ctx) {
  (void)ctx;
  if (previews_) previews_->cancel();
  detachCanvas();
  ctx_ = nullptr;
}

void DrawingToolBase::cancel(core::ToolContext& ctx) { ctx.cancel(); }

DrawingModel* DrawingToolBase::model(core::ToolContext& ctx) const {
  return ctx.document ? DrawingModel::forDocument(*ctx.document) : nullptr;
}

DrawingCanvas* DrawingToolBase::canvas(core::ToolContext& ctx) const {
  return DrawingWorkspaceWidget::activeCanvas(ctx.document);
}

Sheet* DrawingToolBase::activeSheet(core::ToolContext& ctx, bool create) {
  DrawingModel* m = model(ctx);
  if (!m) return nullptr;
  if (DrawingCanvas* c = canvas(ctx))
    if (Sheet* s = m->sheet(c->sheetId())) return s;
  if (Sheet* s = m->activeSheet()) return s;
  if (!create) return nullptr;
  SheetFormat fmt = sheetFormatByName("A3").value_or(SheetFormat{});
  return &m->addSheet(fmt, ProjectionStandard::FirstAngle);
}

DrawingView* DrawingToolBase::pickedView(core::ToolContext& ctx, Sheet& sheet) const {
  if (DrawingCanvas* c = canvas(ctx)) {
    for (ObjectId id : c->selectedViews())
      if (DrawingView* v = sheet.view(id)) return v;
  }
  for (const auto& r : ctx.preselection)
    if (r.kind == core::ShapeKind::DrawingView)
      if (DrawingView* v = sheet.view(r.owner)) return v;
  if (ctx.selection)
    for (const auto& r : ctx.selection->items())
      if (r.kind == core::ShapeKind::DrawingView)
        if (DrawingView* v = sheet.view(r.owner)) return v;
  for (const auto& a : ctx.args) {
    // numeric id or a view label
    bool numeric = !a.empty() && std::all_of(a.begin(), a.end(), [](unsigned char ch) { return std::isdigit(ch); });
    if (numeric)
      if (DrawingView* v = sheet.view(static_cast<ObjectId>(std::stoull(a)))) return v;
    for (auto& v : sheet.views)
      if (lower(v.label) == lower(a)) return &v;
  }
  return nullptr;
}

Point2 DrawingToolBase::fallbackPlacement(core::ToolContext& ctx) const {
  if (DrawingCanvas* c = canvas(ctx)) return c->defaultPlacement();
  DrawingModel* m = model(ctx);
  const Sheet* s = m ? m->activeSheet() : nullptr;
  if (!s) return {150, 100};
  BBox2 f = s->frameRect();
  double bottom = s->titleBlock.visible ? f.max.y - s->titleBlock.height : f.max.y;
  return {(f.min.x + f.max.x) / 2, (f.min.y + bottom) / 2};
}

void DrawingToolBase::attachCanvas(core::ToolContext& ctx) {
  ctx_ = &ctx;
  DrawingCanvas* c = canvas(ctx);
  if (c == canvas_) return;
  detachCanvas();
  canvas_ = c;
  if (canvas_) {
    canvas_->setInteraction(this);
    connect(canvas_, &QObject::destroyed, this, [this]() { canvas_ = nullptr; });
  }
}

void DrawingToolBase::detachCanvas() {
  if (canvas_) {
    disconnect(canvas_, nullptr, this, nullptr);
    if (canvas_->interaction() == this) canvas_->clearInteraction();
    canvas_->setHighlighted({});
  }
  canvas_ = nullptr;
}

void DrawingToolBase::refreshCanvas() {
  if (canvas_) canvas_->updatePreview();
}

void DrawingToolBase::setCanvasHighlights(std::vector<ObjectId> ids) {
  if (canvas_) canvas_->setHighlighted(std::move(ids));
}

bool DrawingToolBase::hasDialogShell(core::ToolContext& ctx) const { return ctx.host && ctx.host->mainWindow() != nullptr; }

bool DrawingToolBase::parsePoint(const std::string& text, Point2& out) {
  std::string t = text;
  std::replace(t.begin(), t.end(), ';', ',');
  size_t comma = t.find(',');
  if (comma == std::string::npos) return false;
  try {
    size_t n1 = 0, n2 = 0;
    double x = std::stod(t.substr(0, comma), &n1);
    double y = std::stod(t.substr(comma + 1), &n2);
    if (n1 == 0 || n2 == 0) return false;
    out = {x, y};
    return true;
  } catch (...) {
    return false;
  }
}

bool DrawingToolBase::parseKeyValue(const std::string& text, std::string& key, std::string& value) {
  size_t eq = text.find('=');
  if (eq == std::string::npos) return false;
  key = lower(text.substr(0, eq));
  value = text.substr(eq + 1);
  return !key.empty();
}

std::string DrawingToolBase::lower(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

PreviewRunner& DrawingToolBase::previews(core::ToolContext& ctx) {
  if (!previews_) previews_ = std::make_unique<PreviewRunner>();
  DrawingModel* m = model(ctx);
  previews_->setSynchronous(m && m->isSynchronous());
  return *previews_;
}

void DrawingToolBase::addPreviewView(DisplayList& out, const DrawingView& v, const Point2& position) {
  DrawingView tmp = v;
  tmp.position = position;
  for (const auto& c : tmp.curves) {
    Curve2D sc = c.transformed(tmp.position, tmp.scale, true);
    if (sc.cls == LineClass::Hidden) sc.cls = LineClass::Hidden;
    out.curves.push_back(sc);
  }
}

void DrawingToolBase::addPlaceholder(DisplayList& out, const BBox2& r) {
  if (!r.isValid()) return;
  out.curves.push_back(Curve2D::polyline({r.min, {r.max.x, r.min.y}, r.max, {r.min.x, r.max.y}, r.min}, LineClass::Detail));
}

QComboBox* DrawingToolBase::addOrientationCombo(QFormLayout* form, ViewOrientation current) {
  auto* combo = new QComboBox();
  for (ViewOrientation o : standardOrientations()) {
    combo->addItem(QString::fromStdString(viewOrientationName(o)), static_cast<int>(o));
    if (o == current) combo->setCurrentIndex(combo->count() - 1);
  }
  form->addRow(QObject::tr("Orientation"), combo);
  return combo;
}

QComboBox* DrawingToolBase::addScaleCombo(QFormLayout* form, double current) {
  auto* combo = new QComboBox();
  combo->setEditable(true);
  int match = -1;
  for (const auto& p : scalePresets()) {
    combo->addItem(QString::fromUtf8(p.label), p.value);
    if (std::abs(p.value - current) < 1e-9) match = combo->count() - 1;
  }
  if (match >= 0) combo->setCurrentIndex(match);
  else combo->setEditText(QString::fromStdString(scaleLabel(current)));
  form->addRow(QObject::tr("Scale"), combo);
  return combo;
}

double DrawingToolBase::comboScale(const QComboBox* combo, double fallback) {
  double v = 0;
  if (parseScale(combo->currentText().toStdString(), v) && v > 0) return v;
  int idx = combo->currentIndex();
  if (idx >= 0) {
    double d = combo->itemData(idx).toDouble();
    if (d > 0) return d;
  }
  return fallback;
}

}  // namespace fabricad::drawing
