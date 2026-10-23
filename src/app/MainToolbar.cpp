// FabriCAD app: main toolbar (sections, captions, MORE, collapse).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainToolbar.h"

#include "app/Icons.h"
#include "app/ThemeManager.h"
#include "theme/Tokens.h"
#include "core/Json.h"

#include <QAction>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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

/// A tool's dropdown entry. Toggles (Snap, Sketch Grid, Show Profile, filters) are checkable and
/// carry no icon, so the menu draws their check mark in the icon column (p1-check 7).
QAction* addToolMenuAction(QMenu* menu, const core::ToolInfo& info) {
  if (info.checked) {
    QAction* a = menu->addAction(menuText(info));
    a->setCheckable(true);
    return a;
  }
  return menu->addAction(toolIcon(info), menuText(info));
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
  /// Collapsed section: one icon button in place of the button row, opening the caption's
  /// dropdown. The caption itself keeps its place so all captions share one baseline (p1-check 6).
  QToolButton* collapsedButton = nullptr;
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
  // an explicit minimum: the pages' layouts must not hold the window wide (fit() folds them)
  setMinimumWidth(120);
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
  b->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
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

void MainToolbar::addVariantMenu(Page& p, QToolButton* b, const ToolbarSection& model, core::ITool* tool) {
  auto it = model.variants.find(tool);
  if (it == model.variants.end()) return;
  auto* menu = new QMenu(b);
  menu->setToolTipsVisible(true);
  for (core::ITool* t : it->second) {
    QAction* a = addToolMenuAction(menu, t->info());
    a->setToolTip(core::qs(t->info().tooltip));
    connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
    addAction(p, t->info().id, a);
  }
  b->setMenu(menu);
  b->setPopupMode(QToolButton::MenuButtonPopup);
  b->setProperty("splitButton", true);
  connect(menu, &QMenu::aboutToShow, this, &MainToolbar::refreshStates);
}

void MainToolbar::addCaptionMenu(Page& p, Section& s, QToolButton* caption) {
  const ToolbarSection& model = s.model;
  // the dropdown lists every tool of the section (MORE: grouped by registry tab)
  auto* menu = new QMenu(caption);
  menu->setToolTipsVisible(true);
  if (model.kind == SectionKind::More) {
    for (const MoreEntry& e : model.more) {
      addMenuHeader(menu, core::qs(e.label));
      for (const auto& [group, tools] : e.groups)
        for (core::ITool* t : tools) {
          QAction* a = addToolMenuAction(menu, t->info());
          a->setToolTip(core::qs(t->info().tooltip));
          connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
          addAction(p, t->info().id, a);
        }
    }
  } else {
    for (core::ITool* t : model.all) {
      QAction* a = addToolMenuAction(menu, t->info());
      a->setToolTip(core::qs(t->info().tooltip));
      connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
      addAction(p, t->info().id, a);
    }
  }
  caption->setMenu(menu);
}

MainToolbar::Page* MainToolbar::buildPage(bool sketch) {
  if (vertical_) return buildVerticalPage(sketch);
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
      b->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
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
          b->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
          b->setFixedHeight(kButtonHeight);
          b->setMinimumWidth(kMinButtonWidth);
          b->setToolTip(core::qs(e.label));
          auto* menu = new QMenu(b);
          menu->setToolTipsVisible(true);
          for (const auto& [group, tools] : e.groups) {
            if (e.groups.size() > 1 || group != e.label) addMenuHeader(menu, core::qs(group));
            for (core::ITool* t : tools) {
              QAction* a = addToolMenuAction(menu, t->info());
              a->setToolTip(core::qs(t->info().tooltip));
              connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
              addAction(p, t->info().id, a);
            }
          }
          b->setMenu(menu);
          connect(menu, &QMenu::aboutToShow, this, &MainToolbar::refreshStates);
          h->addWidget(b);
          s.buttons.push_back(b);
        }
      } else {
        for (core::ITool* t : model.promoted) {
          QToolButton* b = toolButton(t, s.buttonRow, false);
          addAction(p, t->info().id, b);
          addVariantMenu(p, b, model, t);
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
    caption->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
    caption->setPopupMode(QToolButton::InstantPopup);
    caption->setAutoRaise(true);
    caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    caption->setFixedHeight(kCaptionHeight);
    addCaptionMenu(p, s, caption);
    QMenu* menu = caption->menu();
    auto* folded = new QToolButton(box);
    folded->setObjectName("RibbonCollapsedTool");
    folded->setProperty("toolbarTab", core::qs(model.toolbarTab));
    folded->setProperty("toolbarGroup", core::qs(model.toolbarGroup));
    folded->setToolTip(core::qs(model.name));
    folded->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
    folded->setPopupMode(QToolButton::InstantPopup);
    folded->setAutoRaise(true);
    folded->setToolButtonStyle(Qt::ToolButtonIconOnly);
    folded->setIconSize(QSize(kIcon, kIcon));
    folded->setMinimumHeight(kButtonHeight);  // takes the button row's height, caption below it
    folded->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    folded->setMenu(menu);
    folded->hide();
    v->addWidget(folded, 1);
    s.collapsedButton = folded;
    v->addWidget(caption);
    s.caption = caption;
    // menus show live states (toggle check marks, enabled) when they open
    connect(menu, &QMenu::aboutToShow, this, &MainToolbar::refreshStates);
    row->addWidget(box);
    p.sections.push_back(std::move(s));
  }
  if (!stretched) row->addStretch(1);
  stack_->addWidget(page->widget);
  Page* raw = page.get();
  (sketch ? sketchPage_ : defaultPage_) = std::move(page);
  return raw;
}

// ---- vertical layout (toolbar left / right) -----------------------------------------------------

namespace {
constexpr int kVButtonH = 40;      // icon button of the vertical toolbar (5.1)
constexpr int kVIcon = 20;
constexpr int kVMargin = 4;        // left / right inside the 64 px column
constexpr int kVSectionGap = 4;    // below each section's last button
}  // namespace

MainToolbar::Page* MainToolbar::buildVerticalPage(bool sketch) {
  auto page = std::make_unique<Page>();
  page->sketch = sketch;
  page->widget = new QWidget(stack_);
  auto* col = new QVBoxLayout(page->widget);
  col->setContentsMargins(0, 0, 0, 4);
  col->setSpacing(0);
  Page& p = *page;
  bool stretched = false;
  const int buttonW = kVerticalWidth - 2 * kVMargin;
  // a label is shown under the icon when it fits the button at 9 px (caption style)
  const QFont labelFont = theme::font(theme::TextStyle::Caption);
  const QFontMetrics fm(labelFont);
  // long names (PRESS PULL, INTERFERE, MIDPLANE: p4-check L3) keep their label: the caption
  // without its tracking, on two lines at a space or hyphen when one line is still too wide
  QFont tight = labelFont;
  tight.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
  const QFontMetrics tfm(tight);
  const int room = buttonW - 6;
  auto twoLines = [&tfm, room](const QString& label) -> QString {
    const QString up = label.toUpper();
    for (int i = up.size() / 2, d = 0; d <= up.size() / 2; ++d)
      for (int at : {i - d, i + d}) {
        if (at <= 0 || at >= up.size() - 1 || (up[at] != ' ' && up[at] != '-')) continue;
        const QString a = up.left(up[at] == '-' ? at + 1 : at), b = up.mid(at + 1);
        if (tfm.horizontalAdvance(a) <= room && tfm.horizontalAdvance(b) <= room) return label.left(a.size()) + '\n' + label.mid(at + 1);
      }
    return {};
  };
  auto styleButton = [&](QToolButton* b, const QString& label) {
    b->setIconSize(QSize(kVIcon, kVIcon));
    b->setFixedSize(buttonW, kVButtonH);
    b->setProperty("vertical", true);
    if (!label.isEmpty() && fm.horizontalAdvance(label.toUpper()) <= room) {
      b->setProperty("textStyle", "caption");
      b->setText(label);
      b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    } else if (!label.isEmpty() && tfm.horizontalAdvance(label.toUpper()) <= room) {
      b->setFont(tight);  // a font set here wins over the theme's text style
      b->setText(label);
      b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    } else if (const QString wrapped = label.isEmpty() ? QString() : twoLines(label); !wrapped.isEmpty()) {
      b->setFont(tight);
      b->setText(wrapped);
      b->setProperty("fullLabel", label);
      b->setFixedSize(buttonW, kVButtonH + tfm.lineSpacing());
      b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    } else {
      b->setText(label);  // kept for tests and the tooltip
      b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
  };
  for (ToolbarSection& model : buildToolbarSections(manager_->registry(), ws_, filter_, sketch)) {
    Section s;
    s.model = model;
    const bool bottom = model.kind == SectionKind::Document || model.kind == SectionKind::Finish;
    if (bottom && !stretched) {
      col->addStretch(1);
      stretched = true;
    }
    auto* box = new QWidget(page->widget);
    box->setObjectName("RibbonGroup");
    box->setAttribute(Qt::WA_StyledBackground);
    box->setProperty("toolbarTab", core::qs(model.toolbarTab));
    box->setProperty("toolbarGroup", core::qs(model.toolbarGroup));
    box->setProperty("section", core::qs(model.name));
    box->setProperty("vertical", true);
    if (model.kind == SectionKind::Document) box->setProperty("document", true);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, kVSectionGap);
    v->setSpacing(2);
    s.box = box;
    if (model.kind == SectionKind::Finish) {
      box->setProperty("finish", true);
      auto* b = new QToolButton(box);
      b->setObjectName("FinishSketchButton");
      b->setIcon(resourceIcon("finish-sketch"));
      b->setToolTip(core::qs(toolTooltip(model.all.front()->info())));
      b->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
      styleButton(b, "Finish");
      connect(b, &QToolButton::clicked, this, [this] { emit toolTriggered("sketch.finish"); });
      addAction(p, "sketch.finish", b);
      v->addWidget(b, 0, Qt::AlignHCenter);
      s.buttons.push_back(b);
      col->addWidget(box);
      p.sections.push_back(std::move(s));
      continue;
    }
    // caption bar first (straight rule above it), the icon column below
    auto* caption = new QToolButton(box);
    caption->setObjectName("RibbonGroupLabel");
    caption->setProperty("toolbarTab", core::qs(model.toolbarTab));
    caption->setProperty("toolbarGroup", core::qs(model.toolbarGroup));
    caption->setProperty("vertical", true);
    caption->setToolTip(core::qs(model.name));
    caption->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
    caption->setPopupMode(QToolButton::InstantPopup);
    caption->setAutoRaise(true);
    caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    caption->setFixedHeight(kCaptionHeight);
    {
      // the section name in 64 px: with the arrow when it fits, else the name alone, elided
      const QFontMetrics cfm(theme::font(theme::TextStyle::Caption));
      const QString name = core::qs(model.name);
      const int room = kVerticalWidth - 8;
      const QString withArrow = name + QString::fromUtf8(" ▾");
      caption->setText(cfm.horizontalAdvance(withArrow.toUpper()) <= room ? withArrow : cfm.elidedText(name, Qt::ElideRight, room));
    }
    addCaptionMenu(p, s, caption);
    connect(caption->menu(), &QMenu::aboutToShow, this, &MainToolbar::refreshStates);
    v->addWidget(caption);
    s.caption = caption;
    s.buttonRow = new QWidget(box);
    if (model.flat) {
      // CONSTRAINTS: small icons, two per row
      auto* grid = new QGridLayout(s.buttonRow);
      grid->setContentsMargins(kVMargin + 2, 1, kVMargin + 2, 1);
      grid->setSpacing(1);
      int i = 0;
      for (core::ITool* t : model.promoted) {
        QToolButton* b = toolButton(t, s.buttonRow, true);
        addAction(p, t->info().id, b);
        grid->addWidget(b, i / 2, i % 2);
        ++i;
      }
    } else {
      auto* h = new QVBoxLayout(s.buttonRow);
      h->setContentsMargins(kVMargin, 0, kVMargin, 0);
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
          b->setPopupMode(QToolButton::InstantPopup);
          b->setAutoRaise(true);
          b->setFocusPolicy(Qt::TabFocus);  // P5-KEYBOARD-5: reachable by Tab, a click keeps the canvas focus
          b->setToolTip(core::qs(e.label));
          styleButton(b, core::qs(e.label));
          auto* menu = new QMenu(b);
          menu->setToolTipsVisible(true);
          for (const auto& [group, tools] : e.groups) {
            if (e.groups.size() > 1 || group != e.label) addMenuHeader(menu, core::qs(group));
            for (core::ITool* t : tools) {
              QAction* a = addToolMenuAction(menu, t->info());
              a->setToolTip(core::qs(t->info().tooltip));
              connect(a, &QAction::triggered, this, [this, id = t->info().id] { emit toolTriggered(id); });
              addAction(p, t->info().id, a);
            }
          }
          b->setMenu(menu);
          connect(menu, &QMenu::aboutToShow, this, &MainToolbar::refreshStates);
          h->addWidget(b);
          s.buttons.push_back(b);
        }
      } else {
        for (core::ITool* t : model.promoted) {
          QToolButton* b = toolButton(t, s.buttonRow, false);
          styleButton(b, core::qs(shortToolLabel(t->info())));
          addAction(p, t->info().id, b);
          addVariantMenu(p, b, model, t);
          h->addWidget(b);
          s.buttons.push_back(b);
        }
      }
    }
    v->addWidget(s.buttonRow);
    col->addWidget(box);
    p.sections.push_back(std::move(s));
  }
  if (!stretched) col->addStretch(1);
  stack_->addWidget(page->widget);
  Page* raw = page.get();
  (sketch ? sketchPage_ : defaultPage_) = std::move(page);
  return raw;
}

void MainToolbar::fitVertical() {
  Page* p = currentPage();
  if (!p) return;
  // the fit model works on one axis: here a "width" is a height (buttons stacked, the caption
  // bar and the section gap are the padding, so a collapsed section keeps its caption)
  const int padding = kCaptionHeight + 2 + kVSectionGap;
  std::vector<FitSection> in;
  for (Section& s : p->sections) {
    FitSection f;
    f.collapsePriority = s.model.collapsePriority;
    f.startCollapsed = s.model.startCollapsed;
    if (s.model.kind == SectionKind::Finish) {
      f.fixed = true;
      f.buttonWidths = {kVButtonH + kVSectionGap - padding};
    } else if (s.model.flat) {
      s.buttonRow->ensurePolished();
      f.buttonWidths = {s.buttonRow->sizeHint().height()};
    } else {
      for (QToolButton* b : s.buttons) f.buttonWidths.push_back(b->height());  // two-line labels are taller
    }
    in.push_back(f);
  }
  const QMargins m = p->widget->layout()->contentsMargins();
  const int available = height() - m.top() - m.bottom();
  int natural = 0;
  for (const FitSection& f : in)
    natural += fitSectionWidth(f, static_cast<int>(f.buttonWidths.size()), false, kButtonSpacing, padding);
  p->naturalWidth = natural + m.top() + m.bottom();
  const std::vector<FitResult> r = fitToolbar(in, available, kButtonSpacing, padding);
  for (size_t i = 0; i < p->sections.size(); ++i) {
    Section& s = p->sections[i];
    if (s.model.kind == SectionKind::Finish) continue;
    s.collapsed = r[i].collapsed;
    s.visible = r[i].visibleButtons;
    s.box->setProperty("collapsed", s.collapsed);
    s.buttonRow->setVisible(!s.collapsed);
    if (!s.model.flat)
      for (size_t b = 0; b < s.buttons.size(); ++b) s.buttons[b]->setVisible(static_cast<int>(b) < s.visible);
    if (s.caption) {
      s.caption->style()->unpolish(s.caption);
      s.caption->style()->polish(s.caption);
    }
  }
  updateGeometry();
}

void MainToolbar::setOrientation(Qt::Orientation o) {
  const bool vertical = o == Qt::Vertical;
  if (vertical == vertical_ && (defaultPage_ || sketchPage_)) return;
  vertical_ = vertical;
  setProperty("vertical", vertical);
  if (vertical) {
    setMinimumHeight(120);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setFixedWidth(kVerticalWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  } else {
    setMinimumWidth(120);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setFixedHeight(kRowHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  style()->unpolish(this);
  style()->polish(this);
  clearPages();
  showPage(sketching_ && ws_ == core::Workspace::Design);
  updateGeometry();
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

QSize MainToolbar::sizeHint() const {
  if (vertical_) return QSize(kVerticalWidth, std::max(naturalWidth(), 400));
  return QSize(std::max(naturalWidth(), 400), kRowHeight);
}

QSize MainToolbar::minimumSizeHint() const { return vertical_ ? QSize(kVerticalWidth, 120) : QSize(120, kRowHeight); }

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
  if (vertical_) {
    fitVertical();
    return;
  }
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
    // collapsed: one icon button (the section's first icon) opens the dropdown; the caption
    // stays a caption under it, on the same line as the other sections' captions
    if (s.collapsedButton) {
      if (s.collapsed && s.collapsedButton->icon().isNull()) {
        QIcon icon;
        if (s.model.kind == SectionKind::More && !s.model.more.empty()) icon = moreIcon(s.model.more.front());
        else if (!s.model.promoted.empty()) icon = toolIcon(s.model.promoted.front()->info());
        else if (!s.model.all.empty()) icon = toolIcon(s.model.all.front()->info());
        s.collapsedButton->setIcon(icon);
      }
      s.collapsedButton->setVisible(s.collapsed);
    }
    s.box->setMinimumWidth(fitSectionWidth(in[i], s.visible, s.collapsed, kButtonSpacing, kSectionPadding) - 1);
  }
  updateGeometry();
}

void MainToolbar::refreshStates() {
  Page* p = currentPage();
  if (!p) return;
  const core::ToolContext ctx = manager_->makeContext();
  // the caption of the section holding the running command is highlighted (mockup v6 CREATE)
  const std::string activeId = manager_->activeToolId();
  for (Section& sec : p->sections) {
    if (!sec.caption) continue;
    const bool holds = !activeId.empty() && std::any_of(sec.model.all.begin(), sec.model.all.end(),
                                                         [&](const core::ITool* t) { return t->info().id == activeId; });
    if (sec.caption->property("activeSection").toBool() != holds) {
      sec.caption->setProperty("activeSection", holds);
      sec.caption->style()->unpolish(sec.caption);
      sec.caption->style()->polish(sec.caption);
    }
  }
  std::map<std::string, bool> cache;
  for (auto& [id, obj] : p->actions) {
    auto it = cache.find(id);
    if (it == cache.end()) {
      core::ITool* t = manager_->registry().tool(id);
      it = cache.emplace(id, t && t->isEnabled(ctx)).first;
    }
    const bool active = manager_->activeToolId() == id;
    // toggles (snap, grid, show profile, selection filters): their own state (p1-check 7)
    core::ITool* tool = manager_->registry().tool(id);
    const bool toggle = tool && tool->info().checked;
    const bool on = toggle && it->second && tool->info().checked();  // disabled: no state to show
    // state-dependent labels (Ground / Unground, NEW-ASM-CORE-2)
    if (tool && tool->info().dynamicName) {
      const std::string dyn = tool->info().dynamicName(ctx);
      const QString text = core::qs(dyn.empty() ? tool->info().name : dyn);
      if (auto* b = qobject_cast<QToolButton*>(obj)) {
        if (b->toolButtonStyle() != Qt::ToolButtonIconOnly)
          b->setText(dyn.empty() ? core::qs(shortToolLabel(tool->info())) : text);
        b->setToolTip(dyn.empty() ? core::qs(toolTooltip(tool->info())) : text);
      } else if (auto* a = qobject_cast<QAction*>(obj)) {
        a->setText(text);
      }
    }
    if (auto* b = qobject_cast<QToolButton*>(obj)) {
      b->setEnabled(it->second);
      if (b->objectName() == QLatin1StringView("RibbonTool") && (!b->menu() || b->popupMode() == QToolButton::MenuButtonPopup)) {
        b->setCheckable(true);
        b->setChecked(active || on);
      }
    } else if (auto* a = qobject_cast<QAction*>(obj)) {
      a->setEnabled(it->second);
      if (toggle) {
        a->setCheckable(true);
        a->setChecked(on);
      }
    }
  }
}

}  // namespace fabricad::app
