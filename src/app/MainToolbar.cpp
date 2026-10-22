// FabriCAD app: main toolbar (sections, captions, MORE, collapse).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainToolbar.h"

#include "app/Icons.h"
#include "app/ThemeManager.h"
#include "core/Json.h"

#include <QAction>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace fabricad::app {

namespace {
constexpr int kIcon = 24;          // tool icon (5.1)
constexpr int kSmallIcon = 18;     // flat grids (CONSTRAINTS)
constexpr int kButtonHeight = 50;  // icon + gap + label.sm
constexpr int kCaptionHeight = 16;
constexpr int kRowHeight = 70;     // 52 button row + 16 caption + border
constexpr int kMinButtonWidth = 48;
constexpr int kButtonSpacing = 2;
constexpr int kSectionMargin = 4;  // left and right inside a section
constexpr int kSectionPadding = 2 * kSectionMargin + 1;  // + the 1 px divider

QString menuText(const core::ToolInfo& i) {
  QString t = core::qs(i.name);
  if (!i.shortcut.empty()) t += "\t" + core::qs(i.shortcut);
  return t;
}

/// Icon of a MORE entry: the registry tab's own icon when there is one, else its first tool's.
QIcon moreIcon(const MoreEntry& e) {
  static const std::map<std::string, const char*> kIcons = {
      {"TOOLS", "app-options"}, {"INSERT", "part-import"}, {"SELECT", "select"},
      {"OPTIONS", "app-options"}, {"SURFACE", "part-thicken"}, {"EXPORT", "part-export-step"}};
  if (auto it = kIcons.find(e.label); it != kIcons.end())
    if (QIcon i = resourceIcon(it->second); !i.isNull()) return i;
  for (const auto& g : e.groups)
    if (!g.second.empty()) return toolIcon(g.second.front()->info());
  return {};
}
}  // namespace

struct MainToolbar::Section {
  ToolbarSection model;
  QWidget* box = nullptr;
  QWidget* buttonRow = nullptr;
  QToolButton* caption = nullptr;
  std::vector<QToolButton*> buttons;  ///< fold from the right
  int visible = 0;
  bool collapsed = false;
};

struct MainToolbar::Page {
  QWidget* widget = nullptr;
  bool sketch = false;
  std::vector<Section> sections;
  std::vector<std::pair<std::string, QObject*>> actions;  ///< (tool id, button or action)
  int naturalWidth = 0;
};

MainToolbar::MainToolbar(core::ToolManager& manager, QWidget* parent) : QWidget(parent), manager_(&manager) {
  setObjectName("RibbonRow");
  setAttribute(Qt::WA_StyledBackground);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(kRowHeight);
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);
  stack_ = new QStackedWidget(this);
  l->addWidget(stack_);
  // fonts change with the theme and ui/fontScale: measure again
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { scheduleFit(); });
}

MainToolbar::~MainToolbar() = default;

MainToolbar::Page* MainToolbar::currentPage() const {
  return sketching_ && sketchPage_ ? sketchPage_.get() : defaultPage_.get();
}

void MainToolbar::addAction(Page& page, const std::string& id, QObject* obj) {
  obj->setProperty("toolId", core::qs(id));
  page.actions.emplace_back(id, obj);
}

QToolButton* MainToolbar::toolButton(core::ITool* tool, QWidget* parent, bool small) {
  const core::ToolInfo& info = tool->info();
  auto* b = new QToolButton(parent);
  b->setObjectName("RibbonTool");
  b->setIcon(toolIcon(info));
  b->setToolTip(core::qs(toolTooltip(info)));
  b->setAutoRaise(true);
  b->setFocusPolicy(Qt::NoFocus);
  if (small) {
    b->setIconSize(QSize(kSmallIcon, kSmallIcon));
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setFixedSize(24, 24);
  } else {
    b->setIconSize(QSize(kIcon, kIcon));
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setText(core::qs(shortToolLabel(info)));
    b->setFixedHeight(kButtonHeight);
    b->setMinimumWidth(kMinButtonWidth);
  }
  const std::string id = info.id;
  connect(b, &QToolButton::clicked, this, [this, id] {
    emit toolTriggered(id);
    // G3 PART-09: the click toggled the check state; a rejected, instant or already finished
    // command must not leave the button highlighted
    QTimer::singleShot(0, this, [this] { refreshStates(); });
  });
  return b;
}

MainToolbar::Page* MainToolbar::buildPage(bool sketch) {
  auto page = std::make_unique<Page>();
  page->sketch = sketch;
  page->widget = new QWidget(stack_);
  auto* row = new QHBoxLayout(page->widget);
  row->setContentsMargins(4, 0, 4, 0);
  row->setSpacing(0);
  Page& p = *page;
  bool stretched = false;
  for (ToolbarSection& model : buildToolbarSections(manager_->registry(), ws_, filter_, sketch)) {
    Section s;
    s.model = model;
    const bool rightAligned = model.kind == SectionKind::Document || model.kind == SectionKind::Finish;
    if (rightAligned && !stretched) {
      row->addStretch(1);
      stretched = true;
    }
    if (model.kind == SectionKind::Finish) {
      // FINISH SKETCH: the green button at the right end of the sketch page (no caption)
      auto* box = new QWidget(page->widget);
      box->setObjectName("RibbonGroup");
      box->setProperty("toolbarTab", core::qs(model.toolbarTab));
      box->setProperty("toolbarGroup", core::qs(model.toolbarGroup));
      box->setProperty("finish", true);
      auto* v = new QVBoxLayout(box);
      v->setContentsMargins(8, 0, 8, 0);
      auto* b = new QToolButton(box);
      b->setObjectName("FinishSketchButton");
      b->setIcon(resourceIcon("finish-sketch"));
      b->setIconSize(QSize(20, 20));
      b->setText("Finish Sketch");
      b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      b->setToolTip(core::qs(toolTooltip(model.all.front()->info())));
      b->setFocusPolicy(Qt::NoFocus);
      connect(b, &QToolButton::clicked, this, [this] { emit toolTriggered("sketch.finish"); });
      addAction(p, "sketch.finish", b);
      v->addWidget(b, 0, Qt::AlignVCenter);
      s.box = box;
      s.buttons.push_back(b);
      row->addWidget(box);
      p.sections.push_back(std::move(s));
      continue;
    }
    auto* box = new QWidget(page->widget);
    box->setObjectName("RibbonGroup");
    box->setAttribute(Qt::WA_StyledBackground);
    box->setProperty("toolbarTab", core::qs(model.toolbarTab));
    box->setProperty("toolbarGroup", core::qs(model.toolbarGroup));
    box->setProperty("section", core::qs(model.name));
    if (model.kind == SectionKind::Document) box->setProperty("document", true);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(kSectionMargin, 2, kSectionMargin, 0);
    v->setSpacing(0);
    s.box = box;
    s.buttonRow = new QWidget(box);
    if (model.flat) {
      // CONSTRAINTS: every tool as a small icon, two rows
      auto* grid = new QGridLayout(s.buttonRow);
      grid->setContentsMargins(0, 1, 0, 1);
      grid->setSpacing(1);
      const int cols = std::max<int>(1, (static_cast<int>(model.promoted.size()) + 1) / 2);
      int i = 0;
      for (core::ITool* t : model.promoted) {
        QToolButton* b = toolButton(t, s.buttonRow, true);
        addAction(p, t->info().id, b);
        grid->addWidget(b, i / cols, i % cols);
        ++i;
      }
    } else {
      auto* h = new QHBoxLayout(s.buttonRow);
      h->setContentsMargins(0, 0, 0, 0);
      h->setSpacing(kButtonSpacing);
      if (model.kind == SectionKind::More) {
        for (const MoreEntry& e : model.more) {
          auto* b = new QToolButton(s.buttonRow);
          b->setObjectName("RibbonTool");
          b->setProperty("toolbarTab", core::qs(e.tab));
          b->setProperty("moreEntry", core::qs(e.label));
          QStringList groups;
          for (const auto& g : e.groups) groups << core::qs(g.first);
          b->setProperty("toolbarGroups", groups.join(','));
          b->setIcon(moreIcon(e));
          b->setIconSize(QSize(kIcon, kIcon));
          b->setText(core::qs(e.label));
          b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
          b->setPopupMode(QToolButton::InstantPopup);
          b->setAutoRaise(true);
          b->setFocusPolicy(Qt::NoFocus);
          b->setFixedHeight(kButtonHeight);
          b->setMinimumWidth(kMinButtonWidth);
          b->setToolTip(core::qs(e.label));
          auto* menu = new QMenu(b);
          menu->setToolTipsVisible(true);
          for (const auto& [group, tools] : e.groups) {
            if (e.groups.size() > 1 || group != e.label) addMenuHeader(menu, core::qs(group));
            for (core::ITool* t : tools) {
              QAction* a = menu->addAction(toolIcon(t->info()), menuText(t->info()));
              a->setToolTip(core::qs(t->info().tooltip));
              connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
              addAction(p, t->info().id, a);
            }
          }
          b->setMenu(menu);
          h->addWidget(b);
          s.buttons.push_back(b);
        }
      } else {
        for (core::ITool* t : model.promoted) {
          QToolButton* b = toolButton(t, s.buttonRow, false);
          addAction(p, t->info().id, b);
          h->addWidget(b);
          s.buttons.push_back(b);
        }
      }
      h->addStretch(1);
    }
    v->addWidget(s.buttonRow, 1);
    auto* caption = new QToolButton(box);
    caption->setObjectName("RibbonGroupLabel");
    caption->setProperty("toolbarTab", core::qs(model.toolbarTab));
    caption->setProperty("toolbarGroup", core::qs(model.toolbarGroup));
    caption->setText(core::qs(model.name) + QString::fromUtf8(" ▾"));
    caption->setToolTip(core::qs(model.name));
    caption->setFocusPolicy(Qt::NoFocus);
    caption->setPopupMode(QToolButton::InstantPopup);
    caption->setAutoRaise(true);
    caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    caption->setFixedHeight(kCaptionHeight);
    // the dropdown lists every tool of the section (MORE: grouped by registry tab)
    auto* menu = new QMenu(caption);
    menu->setToolTipsVisible(true);
    if (model.kind == SectionKind::More) {
      for (const MoreEntry& e : model.more) {
        addMenuHeader(menu, core::qs(e.label));
        for (const auto& [group, tools] : e.groups)
          for (core::ITool* t : tools) {
            QAction* a = menu->addAction(toolIcon(t->info()), menuText(t->info()));
            a->setToolTip(core::qs(t->info().tooltip));
            connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
            addAction(p, t->info().id, a);
          }
      }
    } else {
      for (core::ITool* t : model.all) {
        QAction* a = menu->addAction(toolIcon(t->info()), menuText(t->info()));
        a->setToolTip(core::qs(t->info().tooltip));
        connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
        addAction(p, t->info().id, a);
      }
    }
    caption->setMenu(menu);
    v->addWidget(caption);
    s.caption = caption;
    row->addWidget(box);
    p.sections.push_back(std::move(s));
  }
  if (!stretched) row->addStretch(1);
  stack_->addWidget(page->widget);
  Page* raw = page.get();
  (sketch ? sketchPage_ : defaultPage_) = std::move(page);
  return raw;
}

void MainToolbar::clearPages() {
  for (std::unique_ptr<Page>* p : {&defaultPage_, &sketchPage_})
    if (*p) {
      stack_->removeWidget((*p)->widget);
      (*p)->widget->deleteLater();
      p->reset();
    }
}

void MainToolbar::showPage(bool sketch) {
  sketching_ = sketch;
  Page* p = sketch ? sketchPage_.get() : defaultPage_.get();
  if (!p) p = buildPage(sketch);
  stack_->setCurrentWidget(p->widget);
  fit();
  refreshStates();
}

void MainToolbar::rebuild(core::Workspace ws) {
  ws_ = ws;
  clearPages();
  showPage(sketching_ && ws == core::Workspace::Design);
}

void MainToolbar::setDesignFilter(DesignFilter f) {
  if (f == filter_ && defaultPage_) return;
  filter_ = f;
  if (ws_ == core::Workspace::Design) rebuild(ws_);
}

void MainToolbar::setContextualTab(const std::string& name, bool visible) {
  if (visible) contextualShown_.insert(name);
  else contextualShown_.erase(name);
  if (name != "SKETCH") return;
  if (visible == sketching_ && currentPage()) return;
  showPage(visible);
}

bool MainToolbar::isTabVisible(const std::string& name) const {
  if (isContextualTab(name)) return contextualShown_.count(name) > 0;
  const std::vector<std::string> tabs = tabNames();
  return std::find(tabs.begin(), tabs.end(), name) != tabs.end();
}

std::string MainToolbar::currentTab() const {
  if (sketching_) return "SKETCH";
  for (const std::string& t : tabNames())
    if (!isContextualTab(t)) return t;
  return {};
}

void MainToolbar::setCurrentTab(const std::string& name) {
  // one page per context: only the sketch page can be chosen, and only while it is shown
  if (name == "SKETCH" && contextualShown_.count(name)) showPage(true);
  else if (!isContextualTab(name) && sketching_ && !contextualShown_.count("SKETCH")) showPage(false);
}

std::vector<std::string> MainToolbar::tabNames() const {
  std::vector<std::string> out;
  for (const ToolbarTab& t : buildToolbarModel(manager_->registry(), ws_)) out.push_back(t.name);
  return out;
}

int MainToolbar::toolActionCount() const {
  const Page* p = currentPage();
  return p ? static_cast<int>(p->actions.size()) : 0;
}

std::vector<QWidget*> MainToolbar::sectionWidgets() const {
  std::vector<QWidget*> out;
  if (const Page* p = currentPage())
    for (const Section& s : p->sections) out.push_back(s.box);
  return out;
}

bool MainToolbar::isSectionCollapsed(const std::string& name) const {
  if (const Page* p = currentPage())
    for (const Section& s : p->sections)
      if (s.model.name == name) return s.collapsed;
  return false;
}

int MainToolbar::visibleButtonCount(const std::string& name) const {
  if (const Page* p = currentPage())
    for (const Section& s : p->sections)
      if (s.model.name == name) return s.collapsed ? 0 : (s.model.flat ? static_cast<int>(s.model.promoted.size()) : s.visible);
  return 0;
}

int MainToolbar::naturalWidth() const {
  const Page* p = currentPage();
  return p ? p->naturalWidth : 0;
}

QSize MainToolbar::sizeHint() const { return QSize(std::max(naturalWidth(), 400), kRowHeight); }

QSize MainToolbar::minimumSizeHint() const { return QSize(120, kRowHeight); }

void MainToolbar::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  fit();
}

bool MainToolbar::event(QEvent* e) {
  if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange) scheduleFit();
  return QWidget::event(e);
}

void MainToolbar::scheduleFit() {
  if (fitPending_) return;
  fitPending_ = true;
  QTimer::singleShot(0, this, [this] {
    fitPending_ = false;
    fit();
  });
}

void MainToolbar::fit() {
  Page* p = currentPage();
  if (!p) return;
  std::vector<FitSection> in;
  in.reserve(p->sections.size());
  for (Section& s : p->sections) {
    FitSection f;
    f.collapsePriority = s.model.collapsePriority;
    f.startCollapsed = s.model.startCollapsed;
    if (s.model.kind == SectionKind::Finish) {
      f.fixed = true;
      s.buttons.front()->ensurePolished();
      f.buttonWidths = {s.buttons.front()->sizeHint().width() + 16 - kSectionPadding};
      in.push_back(f);
      continue;
    }
    if (s.model.flat) {
      s.buttonRow->ensurePolished();
      f.buttonWidths = {s.buttonRow->sizeHint().width()};
    } else {
      for (QToolButton* b : s.buttons) {
        // width budget (3.4): max(48, label + 12), plus the arrow of dropdown buttons
        b->ensurePolished();
        const int w = std::max(kMinButtonWidth, QFontMetrics(b->font()).horizontalAdvance(b->text()) + 12 +
                                                    (b->menu() ? 8 : 0));
        if (b->width() != w || b->minimumWidth() != w) b->setFixedWidth(w);
        f.buttonWidths.push_back(w);
      }
    }
    s.caption->ensurePolished();
    const QFontMetrics fm(s.caption->font());
    f.captionWidth = fm.horizontalAdvance(s.caption->text()) + 12;
    f.collapsedWidth = std::max(kMinButtonWidth + 8, f.captionWidth);
    in.push_back(f);
  }
  const QMargins m = p->widget->layout()->contentsMargins();
  const int available = width() - m.left() - m.right();
  int natural = 0;
  for (const FitSection& f : in)
    natural += fitSectionWidth(f, static_cast<int>(f.buttonWidths.size()), false, kButtonSpacing, kSectionPadding);
  p->naturalWidth = natural + m.left() + m.right();
  const std::vector<FitResult> r = fitToolbar(in, available, kButtonSpacing, kSectionPadding);
  for (size_t i = 0; i < p->sections.size(); ++i) {
    Section& s = p->sections[i];
    if (s.model.kind == SectionKind::Finish) continue;
    s.collapsed = r[i].collapsed;
    s.visible = r[i].visibleButtons;
    s.box->setProperty("collapsed", s.collapsed);
    s.buttonRow->setVisible(!s.collapsed);
    if (!s.model.flat)
      for (size_t b = 0; b < s.buttons.size(); ++b) s.buttons[b]->setVisible(static_cast<int>(b) < s.visible);
    // caption only: the caption becomes a tall dropdown button with the section's first icon
    if (s.collapsed) {
      QIcon icon;
      if (s.model.kind == SectionKind::More && !s.model.more.empty()) icon = moreIcon(s.model.more.front());
      else if (!s.model.promoted.empty()) icon = toolIcon(s.model.promoted.front()->info());
      s.caption->setIcon(icon);
      s.caption->setIconSize(QSize(kIcon, kIcon));
      s.caption->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      s.caption->setFixedHeight(kRowHeight - 4);
    } else {
      s.caption->setIcon(QIcon());
      s.caption->setToolButtonStyle(Qt::ToolButtonTextOnly);
      s.caption->setFixedHeight(kCaptionHeight);
    }
    s.box->setMinimumWidth(fitSectionWidth(in[i], s.visible, s.collapsed, kButtonSpacing, kSectionPadding) - 1);
  }
  updateGeometry();
}

void MainToolbar::refreshStates() {
  Page* p = currentPage();
  if (!p) return;
  const core::ToolContext ctx = manager_->makeContext();
  std::map<std::string, bool> cache;
  for (auto& [id, obj] : p->actions) {
    auto it = cache.find(id);
    if (it == cache.end()) {
      core::ITool* t = manager_->registry().tool(id);
      it = cache.emplace(id, t && t->isEnabled(ctx)).first;
    }
    const bool active = manager_->activeToolId() == id;
    if (auto* b = qobject_cast<QToolButton*>(obj)) {
      b->setEnabled(it->second);
      if (b->objectName() == QLatin1StringView("RibbonTool")) {
        b->setCheckable(true);
        b->setChecked(active);
      }
    } else if (auto* a = qobject_cast<QAction*>(obj)) {
      a->setEnabled(it->second);
    }
  }
}

}  // namespace fabricad::app
