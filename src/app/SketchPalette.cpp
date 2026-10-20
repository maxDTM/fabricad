// FabriCAD app: sketch palette.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/SketchPalette.h"

#include "app/Icons.h"
#include "app/ToolbarModel.h"
#include "core/Json.h"
#include "core/Tool.h"
#include "sketch/SketchSession.h"
#include "viewer/ViewerWidget.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

namespace {
constexpr int kWidth = 230;
constexpr int kRightMargin = 12;
constexpr int kTop = 150;
}  // namespace

SketchPalette::SketchPalette(viewer::ViewerWidget& viewer, core::ToolManager& manager)
    : QFrame(viewer.overlayParent()), viewer_(viewer), manager_(manager) {
  setObjectName("CommandDialog");  // same frame style as command dialogs
  setAttribute(Qt::WA_StyledBackground);
  setFixedWidth(kWidth);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 8);
  v->setSpacing(3);
  auto* title = new QWidget(this);
  title->setObjectName("CommandDialogTitle");
  title->setAttribute(Qt::WA_StyledBackground);
  auto* th = new QHBoxLayout(title);
  th->setContentsMargins(8, 5, 8, 5);
  auto* ti = new QLabel(title);
  ti->setPixmap(resourceIcon("sketch").pixmap(16, 16));
  th->addWidget(ti);
  th->addWidget(new QLabel("SKETCH PALETTE", title), 1);
  v->addWidget(title);

  auto* opts = new QWidget(this);
  auto* ov = new QVBoxLayout(opts);
  ov->setContentsMargins(10, 2, 10, 2);
  ov->setSpacing(2);
  auto* head = new QLabel("Options", opts);
  head->setObjectName("StatusDim");
  ov->addWidget(head);
  construction_ = option("Construction linetype (X)", "New curves are construction geometry");
  ov->addWidget(construction_);
  auto* look = new QPushButton(resourceIcon("look-at"), "Look At", opts);
  look->setFocusPolicy(Qt::NoFocus);
  connect(look, &QPushButton::clicked, this, [this] {
    if (viewer_.inSketchMode()) viewer_.lookAt(viewer_.sketchPlane(), true);
  });
  grid_ = option("Sketch Grid", "Show the grid on the sketch plane");
  snap_ = option("Snap", "Snap to grid and geometry");
  profiles_ = option("Show Profile", "Shade closed regions");
  points_ = option("Show Points", "Show sketch points");
  dimensions_ = option("Show Dimensions", "Show dimension labels");
  constraints_ = option("Show Constraints", "Show constraint glyphs");
  for (QCheckBox* c : {grid_, snap_, profiles_, points_, dimensions_, constraints_}) ov->addWidget(c);
  ov->addWidget(look);
  v->addWidget(opts);

  constraintBox_ = new QWidget(this);
  auto* cv = new QVBoxLayout(constraintBox_);
  cv->setContentsMargins(10, 4, 10, 2);
  auto* ch = new QLabel("Constraints", constraintBox_);
  ch->setObjectName("StatusDim");
  cv->addWidget(ch);
  constraintGrid_ = new QGridLayout();
  constraintGrid_->setSpacing(2);
  cv->addLayout(constraintGrid_);
  v->addWidget(constraintBox_);

  auto* finish = new QToolButton(this);
  finish->setObjectName("FinishSketchButton");
  finish->setText("FINISH SKETCH");
  finish->setIcon(resourceIcon("finish-sketch"));
  finish->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  finish->setFocusPolicy(Qt::NoFocus);
  connect(finish, &QToolButton::clicked, this, [this] { emit commandRequested("sketch.finish"); });
  v->addWidget(finish, 0, Qt::AlignHCenter);

  auto apply = [this](auto fn) {
    return [this, fn](bool on) {
      if (updating_) return;
      if (sketch::SketchSession* s = sketch::SketchSession::current()) fn(*s, on);
    };
  };
  connect(construction_, &QCheckBox::toggled, this, apply([](sketch::SketchSession& s, bool on) { s.setConstructionMode(on); }));
  connect(snap_, &QCheckBox::toggled, this, apply([this](sketch::SketchSession& s, bool on) {
            s.setSnapEnabled(on);
            viewer_.setGridSnap(on, 0.0);
          }));
  connect(profiles_, &QCheckBox::toggled, this, apply([](sketch::SketchSession& s, bool on) { s.setShowProfiles(on); }));
  connect(points_, &QCheckBox::toggled, this, apply([](sketch::SketchSession& s, bool on) { s.setShowPoints(on); }));
  connect(dimensions_, &QCheckBox::toggled, this, apply([](sketch::SketchSession& s, bool on) { s.setShowDimensions(on); }));
  connect(constraints_, &QCheckBox::toggled, this, apply([](sketch::SketchSession& s, bool on) { s.setShowConstraints(on); }));
  connect(grid_, &QCheckBox::toggled, this, [this](bool on) {
    if (!updating_) viewer_.setGridVisible(on);
  });
  hide();
}

QCheckBox* SketchPalette::option(const QString& text, const QString& tip) {
  auto* c = new QCheckBox(text, this);
  c->setToolTip(tip);
  c->setFocusPolicy(Qt::NoFocus);
  return c;
}

void SketchPalette::rebuildConstraintButtons() {
  const auto tools = manager_.registry().toolsFor(core::Workspace::Design, "SKETCH", "CONSTRAINTS");
  if (tools.size() == toolCount_) return;
  toolCount_ = tools.size();
  while (QLayoutItem* it = constraintGrid_->takeAt(0)) {
    delete it->widget();
    delete it;
  }
  int i = 0;
  for (core::ITool* t : tools) {
    auto* b = new QToolButton(constraintBox_);
    b->setIcon(toolIcon(t->info()));
    b->setIconSize(QSize(20, 20));
    b->setToolTip(core::qs(toolTooltip(t->info())));
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, [this, id = t->info().id] { emit commandRequested(id); });
    constraintGrid_->addWidget(b, i / 6, i % 6);
    ++i;
  }
  constraintBox_->setVisible(!tools.empty());
}

void SketchPalette::sync() {
  sketch::SketchSession* s = sketch::SketchSession::current();
  if (!s || !viewer_.inSketchMode()) {
    hide();
    return;
  }
  rebuildConstraintButtons();
  updating_ = true;
  construction_->setChecked(s->constructionMode());
  snap_->setChecked(s->snapEnabled());
  profiles_->setChecked(s->showProfiles());
  points_->setChecked(s->showPoints());
  dimensions_->setChecked(s->showDimensions());
  constraints_->setChecked(s->showConstraints());
  grid_->setChecked(viewer_.isGridVisible());
  updating_ = false;
  if (!isVisible()) {
    adjustSize();
    show();
  }
}

void SketchPalette::reposition(const QWidget* above) {
  QWidget* p = parentWidget();
  if (!p) return;
  adjustSize();
  int y = kTop;
  if (above && above->isVisible()) y = above->geometry().bottom() + 8;
  // not enough room below the dialog: move to the left of it instead
  int x = p->width() - width() - kRightMargin;
  if (y + height() > p->height() - 50 && above && above->isVisible()) {
    y = kTop;
    x = above->geometry().left() - width() - 8;
  }
  move(std::max(0, x), std::max(0, y));
  raise();
}

}  // namespace fabricad::app
