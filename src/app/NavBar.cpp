// FabriCAD app: viewport navigation bar.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/NavBar.h"

#include "app/Icons.h"
#include "core/Tool.h"
#include "viewer/ViewerWidget.h"

#include <QActionGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QSettings>
#include <QToolButton>

namespace fabricad::app {

using viewer::NavAction;
using viewer::NavPreset;

NavBar::NavBar(viewer::ViewerWidget& viewer, core::ToolManager& manager)
    : QFrame(viewer.overlayParent()), viewer_(viewer), manager_(manager) {
  setObjectName("NavBar");
  setAttribute(Qt::WA_StyledBackground);
  auto* h = new QHBoxLayout(this);
  h->setContentsMargins(4, 2, 4, 2);
  h->setSpacing(1);
  orbit_ = button("orbit", "Orbit (Shift + middle mouse)");
  pan_ = button("pan", "Pan (middle mouse)");
  zoom_ = button("zoom", "Zoom (mouse wheel)");
  for (QToolButton* b : {orbit_, pan_, zoom_}) b->setCheckable(true);
  auto mode = [this](QToolButton* b, NavAction a) {
    connect(b, &QToolButton::toggled, this, [this, b, a](bool on) {
      if (on) {
        for (QToolButton* o : {orbit_, pan_, zoom_})
          if (o != b) o->setChecked(false);
        viewer_.setLeftButtonNavigation(a);
      } else if (viewer_.leftButtonNavigation() == a) {
        viewer_.setLeftButtonNavigation(NavAction::None);
      }
    });
  };
  mode(orbit_, NavAction::Orbit);
  mode(pan_, NavAction::Pan);
  mode(zoom_, NavAction::Zoom);
  h->addWidget(orbit_);
  h->addWidget(pan_);
  h->addWidget(zoom_);
  QToolButton* fit = button("fit", "Fit (F6)");
  connect(fit, &QToolButton::clicked, this, [this] { viewer_.fitAll(true); });
  h->addWidget(fit);
  QToolButton* look = button("look-at", "Look At selected face/plane (Ctrl+8)");
  connect(look, &QToolButton::clicked, this, [this] { runTool("view.look-at"); });
  h->addWidget(look);
  auto* sep = new QFrame(this);
  sep->setFrameShape(QFrame::VLine);
  sep->setStyleSheet("color: #D3D6DB;");
  h->addWidget(sep);
  auto menuButton = [&](const char* icon, const QString& tip, QMenu* menu) {
    QToolButton* b = button(icon, tip);
    b->setMenu(menu);
    b->setPopupMode(QToolButton::InstantPopup);
    h->addWidget(b);
  };
  display_ = buildDisplayMenu();
  grid_ = buildGridMenu();
  nav_ = buildNavMenu();
  menuButton("display", "Display settings", display_);
  menuButton("grid", "Grid and snaps", grid_);
  menuButton("views", "Views", buildViewsMenu());
  menuButton("settings", "Navigation preferences", nav_);
  viewer_.overlayParent()->installEventFilter(this);
  connect(&viewer_, &viewer::ViewerWidget::displaySettingsChanged, this, &NavBar::refresh);
  connect(&viewer_, &viewer::ViewerWidget::featureVisibilityChanged, this, &NavBar::refresh);
  adjustSize();
  reposition();
  show();
}

QToolButton* NavBar::button(const char* icon, const QString& tip) {
  auto* b = new QToolButton(this);
  b->setIcon(resourceIcon(icon));
  b->setIconSize(QSize(18, 18));
  b->setToolTip(tip);
  b->setAutoRaise(true);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

void NavBar::runTool(const char* id) {
  if (manager_.registry().has(id)) manager_.run(id);
}

QMenu* NavBar::buildDisplayMenu() {
  auto* m = new QMenu(this);
  m->addSection("Visual Style");
  auto* styles = new QActionGroup(m);
  const std::pair<const char*, core::DisplayMode> modes[] = {{"Shaded", core::DisplayMode::Shaded},
                                                             {"Shaded with Visible Edges", core::DisplayMode::ShadedEdges},
                                                             {"Wireframe", core::DisplayMode::Wireframe},
                                                             {"Hidden Line", core::DisplayMode::HiddenLine}};
  for (const auto& [name, mode] : modes) {
    QAction* a = m->addAction(name);
    a->setCheckable(true);
    a->setData(static_cast<int>(mode));
    styles->addAction(a);
    connect(a, &QAction::triggered, this, [this, mode = mode] { viewer_.setDisplayMode(mode); });
  }
  m->addSection("Camera");
  auto* proj = new QActionGroup(m);
  for (auto [name, mode] : {std::pair{"Orthographic", core::ProjectionMode::Orthographic},
                            std::pair{"Perspective", core::ProjectionMode::Perspective}}) {
    QAction* a = m->addAction(name);
    a->setCheckable(true);
    a->setData(100 + static_cast<int>(mode));
    proj->addAction(a);
    connect(a, &QAction::triggered, this, [this, mode] { viewer_.setProjection(mode); });
  }
  m->addSection("Object Visibility");
  auto vis = [&](const char* name, const char* key, auto get, auto set) {
    QAction* a = m->addAction(name);
    a->setCheckable(true);
    a->setProperty("visKey", key);
    connect(a, &QAction::toggled, this, [set](bool on) { set(on); });
    connect(m, &QMenu::aboutToShow, a, [a, get] {
      const QSignalBlocker b(a);
      a->setChecked(get());
    });
  };
  vis("Origin", "origin", [this] { return viewer_.isOriginVisible(); }, [this](bool v) { viewer_.setOriginVisible(v); });
  for (const char* f : {"Sketches", "Construction", "Joints"})
    vis(f, f, [this, f] { return viewer_.isFolderVisible(f); }, [this, f](bool v) { viewer_.setFolderVisible(f, v); });
  connect(m, &QMenu::aboutToShow, this, &NavBar::refresh);
  return m;
}

QMenu* NavBar::buildGridMenu() {
  auto* m = new QMenu(this);
  QAction* grid = m->addAction("Layout Grid");
  grid->setCheckable(true);
  connect(grid, &QAction::toggled, this, [this](bool on) { viewer_.setGridVisible(on); });
  QAction* snap = m->addAction("Snap to Grid");
  snap->setCheckable(true);
  connect(snap, &QAction::toggled, this, [this](bool on) { viewer_.setGridSnap(on, 0.0); });
  m->addSection("Grid Spacing");
  auto* spacing = new QActionGroup(m);
  for (double s : {0.0, 1.0, 2.0, 5.0, 10.0, 25.0}) {
    QAction* a = m->addAction(s == 0.0 ? QString("Adaptive") : QString("%1 mm").arg(s));
    a->setCheckable(true);
    a->setChecked(s == 0.0);
    spacing->addAction(a);
    connect(a, &QAction::triggered, this, [this, s] { viewer_.setGridSnap(viewer_.isGridSnap(), s); });
  }
  connect(m, &QMenu::aboutToShow, this, [this, grid, snap] {
    const QSignalBlocker b1(grid), b2(snap);
    grid->setChecked(viewer_.isGridVisible());
    snap->setChecked(viewer_.isGridSnap());
  });
  return m;
}

QMenu* NavBar::buildViewsMenu() {
  auto* m = new QMenu(this);
  const std::pair<const char*, core::StandardView> views[] = {
      {"Home\tHome", core::StandardView::Home}, {"Front\tCtrl+1", core::StandardView::Front},
      {"Back\tCtrl+2", core::StandardView::Back}, {"Left\tCtrl+3", core::StandardView::Left},
      {"Right\tCtrl+4", core::StandardView::Right}, {"Top\tCtrl+5", core::StandardView::Top},
      {"Bottom\tCtrl+6", core::StandardView::Bottom}, {"Isometric\tCtrl+7", core::StandardView::Iso}};
  for (const auto& [name, v] : views)
    m->addAction(name, this, [this, v = v] { viewer_.setStandardView(v, true); });
  m->addSeparator();
  m->addAction("Set Current View as Home", this, [this] { viewer_.setCurrentViewAsHome(); });
  m->addAction("Reset Home", this, [this] { viewer_.resetHome(); });
  return m;
}

QMenu* NavBar::buildNavMenu() {
  auto* m = new QMenu(this);
  m->addSection("Pan, Zoom, Orbit Shortcuts");
  auto* presets = new QActionGroup(m);
  for (NavPreset p : {NavPreset::Fusion, NavPreset::SolidWorks, NavPreset::AutoCAD, NavPreset::Inventor}) {
    QAction* a = m->addAction(viewer::navPresetName(p));
    a->setCheckable(true);
    a->setData(static_cast<int>(p));
    presets->addAction(a);
    connect(a, &QAction::triggered, this, [this, p] {
      viewer_.setNavigationPreset(p);
      QSettings().setValue("view/navPreset", viewer::navPresetName(p));
    });
  }
  m->addSeparator();
  QAction* rev = m->addAction("Reverse Zoom Direction");
  rev->setCheckable(true);
  connect(rev, &QAction::toggled, this, [this](bool on) {
    viewer_.setReverseZoom(on);
    QSettings().setValue("view/reverseZoom", on);
  });
  connect(m, &QMenu::aboutToShow, this, [this, m, rev] {
    for (QAction* a : m->actions())
      if (a->isCheckable() && a != rev) a->setChecked(a->data().toInt() == static_cast<int>(viewer_.navigationPreset()));
    const QSignalBlocker b(rev);
    rev->setChecked(viewer_.reverseZoom());
  });
  return m;
}

void NavBar::applySettings() {
  QSettings s;
  viewer_.setNavigationPreset(viewer::navPresetFromName(s.value("view/navPreset", "Fusion").toString().toStdString()));
  viewer_.setReverseZoom(s.value("view/reverseZoom", false).toBool());
}

void NavBar::refresh() {
  for (QAction* a : display_->actions()) {
    if (!a->isCheckable() || !a->data().isValid()) continue;
    const int d = a->data().toInt();
    const QSignalBlocker b(a);
    if (d >= 100) a->setChecked(d - 100 == static_cast<int>(viewer_.projection()));
    else a->setChecked(d == static_cast<int>(viewer_.displayMode()));
  }
  const NavAction lmb = viewer_.leftButtonNavigation();
  const QSignalBlocker b1(orbit_), b2(pan_), b3(zoom_);
  orbit_->setChecked(lmb == NavAction::Orbit);
  pan_->setChecked(lmb == NavAction::Pan);
  zoom_->setChecked(lmb == NavAction::Zoom);
}

void NavBar::reposition() {
  QWidget* p = parentWidget();
  if (!p) return;
  adjustSize();
  move((p->width() - width()) / 2, p->height() - height() - 8);
  raise();
}

bool NavBar::eventFilter(QObject* o, QEvent* e) {
  if (o == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) reposition();
  return QFrame::eventFilter(o, e);
}

}  // namespace fabricad::app
