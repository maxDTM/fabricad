// FabriCAD drawing: DrawingWorkspaceWidget implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingWorkspaceWidget.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Tool.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/Sheet.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::drawing {

using core::ObjectId;

namespace {
std::vector<DrawingWorkspaceWidget*>& registry() {
  static std::vector<DrawingWorkspaceWidget*> r;
  return r;
}
}  // namespace

DrawingWorkspaceWidget::DrawingWorkspaceWidget(QWidget* parent) : QWidget(parent) {
  setObjectName("fabricad.drawing.workspace");
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setChildrenCollapsible(false);

  // --- sheet browser -------------------------------------------------------------------
  browser_ = new QWidget(splitter);
  auto* bl = new QVBoxLayout(browser_);
  bl->setContentsMargins(4, 4, 4, 4);
  bl->setSpacing(2);
  auto* title = new QLabel(tr("SHEETS"), browser_);
  title->setStyleSheet("font-weight: bold; color: #555; padding: 2px;");
  bl->addWidget(title);
  sheetList_ = new QListWidget(browser_);
  sheetList_->setSelectionMode(QAbstractItemView::SingleSelection);
  bl->addWidget(sheetList_, 1);
  auto* buttons = new QHBoxLayout();
  addSheet_ = new QToolButton(browser_);
  addSheet_->setText("+");
  addSheet_->setToolTip(tr("New sheet"));
  removeSheet_ = new QToolButton(browser_);
  removeSheet_->setText("-");
  removeSheet_->setToolTip(tr("Delete sheet"));
  buttons->addWidget(addSheet_);
  buttons->addWidget(removeSheet_);
  buttons->addStretch(1);
  bl->addLayout(buttons);
  browser_->setMinimumWidth(120);
  browser_->setMaximumWidth(320);
  splitter->addWidget(browser_);

  // --- canvas + bottom bar ---------------------------------------------------------------
  auto* centre = new QWidget(splitter);
  auto* cl = new QVBoxLayout(centre);
  cl->setContentsMargins(0, 0, 0, 0);
  cl->setSpacing(0);
  canvas_ = new DrawingCanvas(centre);
  cl->addWidget(canvas_, 1);
  auto* bottom = new QHBoxLayout();
  bottom->setContentsMargins(4, 0, 4, 0);
  tabs_ = new QTabBar(centre);
  tabs_->setExpanding(false);
  tabs_->setDocumentMode(true);
  tabs_->setMovable(false);
  bottom->addWidget(tabs_, 0);
  auto* plus = new QToolButton(centre);
  plus->setText("+");
  plus->setToolTip(tr("New sheet"));
  plus->setAutoRaise(true);
  bottom->addWidget(plus, 0);
  bottom->addStretch(1);
  status_ = new QLabel(centre);
  status_->setStyleSheet("color: #666;");
  coords_ = new QLabel(centre);
  coords_->setStyleSheet("color: #666;");
  coords_->setMinimumWidth(170);
  bottom->addWidget(status_, 0);
  bottom->addSpacing(12);
  bottom->addWidget(coords_, 0);
  cl->addLayout(bottom);
  splitter->addWidget(centre);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({180, 900});
  root->addWidget(splitter, 1);

  connect(sheetList_, &QListWidget::currentRowChanged, this, &DrawingWorkspaceWidget::onSheetTabChanged);
  connect(tabs_, &QTabBar::currentChanged, this, &DrawingWorkspaceWidget::onSheetTabChanged);
  connect(addSheet_, &QToolButton::clicked, this, &DrawingWorkspaceWidget::onAddSheet);
  connect(plus, &QToolButton::clicked, this, &DrawingWorkspaceWidget::onAddSheet);
  connect(removeSheet_, &QToolButton::clicked, this, &DrawingWorkspaceWidget::onRemoveSheet);
  connect(canvas_, &DrawingCanvas::zoomChanged, this, [this](double) { updateStatus(); });
  connect(canvas_, &DrawingCanvas::statusMessage, this, [this](const QString& t) {
    busyText_ = t;
    updateStatus();
  });
  connect(canvas_, &DrawingCanvas::cursorMoved, this, [this](Point2 p) {
    coords_->setText(QStringLiteral("X %1  Y %2 mm").arg(p.x, 0, 'f', 1).arg(p.y, 0, 'f', 1));
  });
  connect(sheetList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
    if (manager_) manager_->run("drawing.edit-sheet");
  });

  registry().push_back(this);
  refreshSheets();
  updateStatus();
}

DrawingWorkspaceWidget::~DrawingWorkspaceWidget() {
  auto& r = registry();
  r.erase(std::remove(r.begin(), r.end(), this), r.end());
  for (auto& c : modelConnections_) QObject::disconnect(c);
}

const std::vector<DrawingWorkspaceWidget*>& DrawingWorkspaceWidget::instances() { return registry(); }

DrawingWorkspaceWidget* DrawingWorkspaceWidget::forDocument(const core::Document* doc) {
  if (!doc) return nullptr;
  // prefer the most recently created widget bound to this document
  auto& r = registry();
  for (auto it = r.rbegin(); it != r.rend(); ++it)
    if ((*it)->document() == doc) return *it;
  return nullptr;
}

DrawingCanvas* DrawingWorkspaceWidget::activeCanvas(const core::Document* doc) {
  DrawingWorkspaceWidget* w = forDocument(doc);
  return w ? w->canvas() : nullptr;
}

void DrawingWorkspaceWidget::setDocument(core::Document* doc) {
  if (doc == doc_) return;
  for (auto& c : modelConnections_) QObject::disconnect(c);
  modelConnections_.clear();
  doc_ = doc;
  model_ = doc_ ? DrawingModel::forDocument(*doc_) : nullptr;
  canvas_->setModel(model_);
  if (model_) {
    modelConnections_.push_back(connect(model_, &DrawingModel::sheetsChanged, this, &DrawingWorkspaceWidget::refreshSheets));
    modelConnections_.push_back(connect(model_, &DrawingModel::activeSheetChanged, this, [this](ObjectId) { refreshSheets(); }));
    modelConnections_.push_back(connect(model_, &DrawingModel::sheetChanged, this, [this](ObjectId) { refreshSheets(); }));
    modelConnections_.push_back(connect(model_, &DrawingModel::busyChanged, this, [this](bool) { updateStatus(); }));
    modelConnections_.push_back(connect(model_, &QObject::destroyed, this, [this]() {
      modelConnections_.clear();
      model_ = nullptr;
      doc_ = nullptr;
      refreshSheets();
    }));
  }
  refreshSheets();
  updateStatus();
}

void DrawingWorkspaceWidget::setToolManager(core::ToolManager* manager) {
  manager_ = manager;
  canvas_->setToolManager(manager);
}

ObjectId DrawingWorkspaceWidget::activeSheetId() const { return canvas_->sheetId(); }

void DrawingWorkspaceWidget::setActiveSheet(ObjectId id) {
  if (!model_ || !model_->sheet(id)) return;
  canvas_->setSheet(id);
  refreshSheets();
  emit sheetActivated(id);
}

void DrawingWorkspaceWidget::setBrowserVisible(bool visible) { browser_->setVisible(visible); }

void DrawingWorkspaceWidget::refreshSheets() {
  if (refreshing_) return;
  refreshing_ = true;
  sheetList_->clear();
  while (tabs_->count() > 0) tabs_->removeTab(0);
  int current = -1;
  if (model_) {
    const auto& sheets = model_->sheets();
    for (size_t i = 0; i < sheets.size(); ++i) {
      const Sheet& s = sheets[i];
      QString label = QString::fromStdString(s.name);
      auto* item = new QListWidgetItem(QStringLiteral("%1  (%2, %3 views)")
                                           .arg(label, QString::fromStdString(s.format.name))
                                           .arg(static_cast<int>(s.views.size())));
      item->setData(Qt::UserRole, static_cast<qulonglong>(s.id));
      sheetList_->addItem(item);
      tabs_->addTab(label);
      tabs_->setTabData(static_cast<int>(i), static_cast<qulonglong>(s.id));
      if (s.id == canvas_->sheetId()) current = static_cast<int>(i);
    }
  }
  if (current >= 0) {
    sheetList_->setCurrentRow(current);
    tabs_->setCurrentIndex(current);
  }
  removeSheet_->setEnabled(model_ && model_->sheets().size() > 1);
  refreshing_ = false;
}

void DrawingWorkspaceWidget::onSheetTabChanged(int index) {
  if (refreshing_ || !model_ || index < 0) return;
  QObject* src = sender();
  qulonglong id = 0;
  if (src == tabs_) id = tabs_->tabData(index).toULongLong();
  else if (QListWidgetItem* item = sheetList_->item(index)) id = item->data(Qt::UserRole).toULongLong();
  if (id == 0 || id == canvas_->sheetId()) return;
  setActiveSheet(static_cast<ObjectId>(id));
}

void DrawingWorkspaceWidget::onAddSheet() {
  if (manager_ && manager_->registry().has("drawing.new-sheet")) {
    manager_->run("drawing.new-sheet");
    return;
  }
  if (!model_) return;
  SheetFormat fmt;
  ProjectionStandard std = ProjectionStandard::FirstAngle;
  if (Sheet* s = model_->activeSheet()) {
    fmt = s->format;
    std = s->standard;
  }
  ObjectId newId = core::kInvalidId;
  model_->edit("New sheet", &model_->document().undoStack(), [&]() { newId = model_->addSheet(fmt, std).id; });
  if (newId != core::kInvalidId) setActiveSheet(newId);
}

void DrawingWorkspaceWidget::onRemoveSheet() {
  if (manager_ && manager_->registry().has("drawing.delete-sheet")) {
    manager_->run("drawing.delete-sheet");
    return;
  }
  if (!model_ || model_->sheets().size() <= 1) return;
  ObjectId id = canvas_->sheetId();
  model_->edit("Delete sheet", &model_->document().undoStack(), [&]() { model_->removeSheet(id); });
}

void DrawingWorkspaceWidget::updateStatus() {
  QString s = QStringLiteral("%1%").arg(static_cast<int>(canvas_->zoom() * 25.4 / 96.0 * 100.0 + 0.5));
  if (model_ && model_->isBusy()) s = tr("Updating views...  ") + s;
  else if (!busyText_.isEmpty()) s = busyText_ + "  " + s;
  status_->setText(s);
}

}  // namespace fabricad::drawing
