// FabriCAD app: tabbed toolbar.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/Ribbon.h"

#include "app/Icons.h"
#include "core/Json.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace fabricad::app {

namespace {
constexpr int kBigIcon = 30;

QString workspaceLabel(core::Workspace ws) { return ws == core::Workspace::Design ? "DESIGN" : "DRAWING"; }

QString menuText(const core::ToolInfo& i) {
  QString t = core::qs(i.name);
  if (!i.shortcut.empty()) t += "\t" + core::qs(i.shortcut);
  return t;
}
}  // namespace

Ribbon::Ribbon(core::ToolManager& manager, QWidget* parent) : QWidget(parent), manager_(&manager) {
  setObjectName("RibbonRow");
  setAttribute(Qt::WA_StyledBackground);
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(4, 0, 4, 0);
  row->setSpacing(4);

  switcher_ = new QToolButton(this);
  switcher_->setObjectName("WorkspaceSwitcher");
  switcher_->setPopupMode(QToolButton::InstantPopup);
  switcher_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  auto* wsMenu = new QMenu(switcher_);
  wsMenu->addAction(resourceIcon("workspace-design"), "DESIGN", this,
                    [this] { emit workspaceRequested(core::Workspace::Design); });
  wsMenu->addAction(resourceIcon("workspace-drawing"), "DRAWING", this,
                    [this] { emit workspaceRequested(core::Workspace::Drawing); });
  switcher_->setMenu(wsMenu);
  switcher_->setToolTip("Workspace");
  row->addWidget(switcher_, 0, Qt::AlignVCenter);

  auto* right = new QWidget(this);
  right->setObjectName("RibbonTabs");
  auto* col = new QVBoxLayout(right);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(0);
  tabs_ = new QTabBar(right);
  tabs_->setDrawBase(false);
  tabs_->setExpanding(false);
  tabs_->setFocusPolicy(Qt::NoFocus);
  // The tab labels are bold 8pt (theme-light.qss sets it on the bar itself, so tabs are
  // measured with the font they are drawn with). No scroll arrows and no eliding: the few
  // ribbon tabs always fit, and SOLID must never be clipped when SKETCH is shown.
  tabs_->setUsesScrollButtons(false);
  tabs_->setElideMode(Qt::ElideNone);
  pages_ = new QStackedWidget(right);
  col->addWidget(tabs_, 0, Qt::AlignLeft);
  col->addWidget(pages_);
  row->addWidget(right, 1);
  connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
    if (i >= 0 && i < pages_->count()) pages_->setCurrentIndex(i);
    updateTabStyle();
  });
}

QToolButton* Ribbon::bigButton(core::ITool* tool, QWidget* parent) {
  const core::ToolInfo& info = tool->info();
  auto* b = new QToolButton(parent);
  b->setObjectName("RibbonTool");
  b->setIcon(toolIcon(info));
  b->setIconSize(QSize(kBigIcon, kBigIcon));
  b->setToolButtonStyle(Qt::ToolButtonIconOnly);
  b->setToolTip(core::qs(toolTooltip(info)));
  b->setAutoRaise(true);
  b->setFocusPolicy(Qt::NoFocus);
  const std::string id = info.id;
  connect(b, &QToolButton::clicked, this, [this, id] {
    emit toolTriggered(id);
    // G3 PART-09: the click toggled the check state; a rejected, instant or already finished
    // command must not leave the button highlighted
    QTimer::singleShot(0, this, [this] { refreshStates(); });
  });
  actions_.emplace_back(id, b);
  return b;
}

QWidget* Ribbon::buildPage(const ToolbarTab& tab) {
  auto* page = new QWidget(pages_);
  auto* row = new QHBoxLayout(page);
  row->setContentsMargins(2, 2, 2, 0);
  row->setSpacing(0);
  for (const ToolbarGroup& g : tab.groups) {
    auto* box = new QWidget(page);
    box->setObjectName("RibbonGroup");
    box->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(6, 0, 6, 0);
    v->setSpacing(0);
    auto* icons = new QHBoxLayout();
    icons->setSpacing(1);
    // constraint-style groups show every tool as a small icon (ux.md 2.2)
    const bool flat = g.name == "CONSTRAINTS";
    const std::vector<core::ITool*>& shown = flat ? g.all : g.promoted;
    for (core::ITool* t : shown) {
      QToolButton* b = bigButton(t, box);
      if (flat) b->setIconSize(QSize(20, 20));
      icons->addWidget(b);
    }
    v->addLayout(icons);
    auto* label = new QToolButton(box);
    label->setObjectName("RibbonGroupLabel");
    label->setText(core::qs(g.name));
    label->setFocusPolicy(Qt::NoFocus);
    label->setPopupMode(QToolButton::InstantPopup);
    label->setAutoRaise(true);
    auto* menu = new QMenu(label);
    for (core::ITool* t : g.all) {
      const core::ToolInfo& info = t->info();
      QAction* a = menu->addAction(toolIcon(info), menuText(info));
      a->setToolTip(core::qs(info.tooltip));
      const std::string id = info.id;
      connect(a, &QAction::triggered, this, [this, id] { emit toolTriggered(id); });
      actions_.emplace_back(id, a);
    }
    label->setMenu(menu);
    v->addWidget(label, 0, Qt::AlignHCenter);
    row->addWidget(box);
  }
  if (tab.name == "SKETCH") {
    // Finish Sketch: big green button at the right end of the contextual tab
    if (core::ITool* fin = manager_->registry().tool("sketch.finish")) {
      row->addSpacing(12);
      auto* b = new QToolButton(page);
      b->setObjectName("FinishSketchButton");
      b->setIcon(resourceIcon("finish-sketch"));
      b->setIconSize(QSize(20, 20));
      b->setText("FINISH SKETCH");
      b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      b->setToolTip(core::qs(toolTooltip(fin->info())));
      b->setFocusPolicy(Qt::NoFocus);
      connect(b, &QToolButton::clicked, this, [this] { emit toolTriggered("sketch.finish"); });
      actions_.emplace_back("sketch.finish", b);
      row->addWidget(b, 0, Qt::AlignVCenter);
    }
  }
  row->addStretch(1);
  return page;
}

void Ribbon::rebuild(core::Workspace ws) {
  const std::string keep = currentTab();
  ws_ = ws;
  switcher_->setText(workspaceLabel(ws));
  switcher_->setIcon(resourceIcon(ws == core::Workspace::Design ? "workspace-design" : "workspace-drawing"));
  actions_.clear();
  const QSignalBlocker block(tabs_);
  while (tabs_->count()) tabs_->removeTab(0);
  while (pages_->count()) {
    QWidget* w = pages_->widget(0);
    pages_->removeWidget(w);
    w->deleteLater();
  }
  tabNames_.clear();
  contextual_.clear();
  for (const ToolbarTab& t : buildToolbarModel(manager_->registry(), ws)) {
    tabNames_.push_back(t.name);
    if (t.contextual) contextual_.insert(t.name);
    tabs_->addTab(core::qs(t.name));
    pages_->addWidget(buildPage(t));
  }
  for (int i = 0; i < static_cast<int>(tabNames_.size()); ++i) {
    const std::string& n = tabNames_[static_cast<size_t>(i)];
    tabs_->setTabVisible(i, !contextual_.count(n) || contextualShown_.count(n));
  }
  int idx = 0;
  for (int i = 0; i < static_cast<int>(tabNames_.size()); ++i)
    if (tabs_->isTabVisible(i) && (tabNames_[static_cast<size_t>(i)] == keep || keep.empty())) {
      idx = i;
      break;
    }
  if (!tabs_->isTabVisible(idx))
    for (int i = 0; i < tabs_->count(); ++i)
      if (tabs_->isTabVisible(i)) {
        idx = i;
        break;
      }
  tabs_->setCurrentIndex(idx);
  pages_->setCurrentIndex(idx);
  tabs_->updateGeometry();
  updateTabStyle();
  refreshStates();
}

void Ribbon::setContextualTab(const std::string& name, bool visible) {
  if (visible) {
    if (!contextualShown_.count(name)) tabBeforeContextual_ = currentTab();
    contextualShown_.insert(name);
  } else {
    contextualShown_.erase(name);
  }
  for (int i = 0; i < static_cast<int>(tabNames_.size()); ++i) {
    if (tabNames_[static_cast<size_t>(i)] != name) continue;
    const bool wasCurrent = currentTab() == name;
    tabs_->setTabVisible(i, visible || !contextual_.count(name));
    if (visible) tabs_->setCurrentIndex(i);
    else if (wasCurrent || !tabs_->isTabVisible(tabs_->currentIndex()))
      setCurrentTab(tabBeforeContextual_.empty() || tabBeforeContextual_ == name ? tabNames_.front()
                                                                                  : tabBeforeContextual_);
  }
  tabs_->adjustSize();
  tabs_->updateGeometry();
  updateTabStyle();
}

bool Ribbon::isTabVisible(const std::string& name) const {
  for (int i = 0; i < static_cast<int>(tabNames_.size()); ++i)
    if (tabNames_[static_cast<size_t>(i)] == name) return tabs_->isTabVisible(i);
  return false;
}

std::string Ribbon::currentTab() const {
  const int i = tabs_ ? tabs_->currentIndex() : -1;
  return i >= 0 && i < static_cast<int>(tabNames_.size()) ? tabNames_[static_cast<size_t>(i)] : std::string();
}

void Ribbon::setCurrentTab(const std::string& name) {
  for (int i = 0; i < static_cast<int>(tabNames_.size()); ++i)
    if (tabNames_[static_cast<size_t>(i)] == name && tabs_->isTabVisible(i)) {
      tabs_->setCurrentIndex(i);
      return;
    }
}

std::vector<std::string> Ribbon::tabNames() const { return tabNames_; }

void Ribbon::updateTabStyle() {
  // contextual tabs use the sketch accent colour
  for (int i = 0; i < static_cast<int>(tabNames_.size()); ++i)
    tabs_->setTabTextColor(i, contextual_.count(tabNames_[static_cast<size_t>(i)]) ? QColor("#3BA55C") : QColor());
}

void Ribbon::refreshStates() {
  const core::ToolContext ctx = manager_->makeContext();
  std::map<std::string, bool> cache;
  for (auto& [id, obj] : actions_) {
    auto it = cache.find(id);
    if (it == cache.end()) {
      core::ITool* t = manager_->registry().tool(id);
      it = cache.emplace(id, t && t->isEnabled(ctx)).first;
    }
    const bool active = manager_->activeToolId() == id;
    if (auto* b = qobject_cast<QToolButton*>(obj)) {
      b->setEnabled(it->second);
      b->setCheckable(true);
      b->setChecked(active);
    } else if (auto* a = qobject_cast<QAction*>(obj)) {
      a->setEnabled(it->second);
    }
  }
}

}  // namespace fabricad::app
