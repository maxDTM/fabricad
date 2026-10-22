// FabriCAD application shell.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainWindow.h"

#include "app/ThemeManager.h"
#include "viewer/ViewerPalette.h"

#include "app/AppActions.h"
#include "app/AppTools.h"
#include "app/BrowserTree.h"
#include "app/CardSlot.h"
#include "app/CommandCard.h"
#include "app/CommandDialog.h"
#include "app/CommandLine.h"
#include "app/CommandSearch.h"
#include "app/Icons.h"
#include "app/MarkingMenu.h"
#include "app/MarkingMenuModel.h"
#include "app/NavBar.h"
#include "app/MainToolbar.h"
#include "app/StatusCells.h"
#include "app/TitleBar.h"
#include "app/UiSettings.h"
#include "app/ScriptTools.h"
#include "app/ShortcutRouter.h"
#include "app/SketchPalette.h"
#include "app/TimelineWidget.h"
#include "app/FeatureListWidget.h"
#include "app/SidebarHost.h"
#include "app/SidebarPanel.h"
#include "app/TimelineActions.h"
#include "app/ParametersDialog.h"
#include "app/PreferencesDialog.h"
#include "assembly/AssemblyModule.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "drawing/DrawingModule.h"
#include "part/PartModule.h"
#include "sketch/SketchModule.h"
#include "sketch/SketchSession.h"
#include "viewer/OverlayHost.h"
#include "viewer/ViewerWidget.h"
#include "viewer/ViewportOverlays.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QElapsedTimer>
#include <QToolBar>
#include <Standard_Version.hxx>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTextEdit>
#include <QDateTime>
#include <QJsonDocument>
#include <QLockFile>
#include <QUuid>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeRef;

namespace {

MainWindow::WorkspaceFactory& drawingFactory() {
  static MainWindow::WorkspaceFactory f;
  return f;
}

QToolButton* appBarButton(QWidget* parent, const char* icon, const QString& tip) {
  auto* b = new QToolButton(parent);
  b->setIcon(resourceIcon(icon));
  b->setIconSize(QSize(18, 18));
  b->setToolTip(tip);
  b->setAutoRaise(true);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

int coreModifiers(int qtMods) {
  int m = core::ModNone;
  if (qtMods & Qt::ShiftModifier) m |= core::ModShift;
  if (qtMods & Qt::ControlModifier) m |= core::ModCtrl;
  if (qtMods & Qt::AltModifier) m |= core::ModAlt;
  return m;
}

bool isTextInput(QWidget* w) {
  if (!w) return false;
  if (qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QTextEdit*>(w) ||
      qobject_cast<QPlainTextEdit*>(w))
    return true;
  if (auto* c = qobject_cast<QComboBox*>(w)) return c->isEditable();
  return false;
}

}  // namespace

void MainWindow::setDrawingWorkspaceFactory(WorkspaceFactory f) { drawingFactory() = std::move(f); }

// =========================================================================================
// construction
// =========================================================================================

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowIcon(resourceIcon("fabricad"));
  setObjectName("FabriCADMainWindow");
  resize(1400, 860);

  registerModules();
  buildUi();  // also creates the first (empty) design tab
  connectShellSignals();
  activateTab(tabs_.front().get());

  commandLine_->refreshCompletions();
  recent_.load();
  recentCommands_ = QSettings().value("search/recent").toStringList();
  navBar_->applySettings();
  qApp->installEventFilter(this);
  // live theme switch: viewports take their colours from the tokens too
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](theme::Theme th) {
    const viewer::ViewerPalette pal = viewer::ViewerPalette::fromTokens(th);
    for (const auto& t : tabs_)
      if (t->viewer()) t->viewer()->setViewerPalette(pal);
  });

  QSettings s;
  if (s.contains("window/geometry")) restoreGeometry(s.value("window/geometry").toByteArray());
  // a layout stored by another shell version (the ribbon era) is ignored: defaults (6.5)
  if (s.contains("window/state")) restoreState(s.value("window/state").toByteArray(), kLayoutVersion);
  toolbarHost_->show();  // never hidden by a restored state
  browserDockWanted_ = !browserDock_->isHidden();
  timelineDockWanted_ = s.value("window/stripVisible", true).toBool();
  // phase 2: sidebar side and timeline style from ui/* (the dock state may say otherwise)
  applySidebarSide();
  applyTimelineStyle();
  // p0-check 4: a restored zero-height timeline was "shown" but invisible
  QTimer::singleShot(0, this, [this] { ensureTimelineHeight(); });

  // ui/* settings apply live (Preferences > Appearance / Layout, THEME command)
  connect(&UiSettings::instance(), &UiSettings::changed, this, &MainWindow::applyUiSetting);
}

void MainWindow::applyUiSetting(const QString& key) {
  UiSettings& ui = UiSettings::instance();
  if (key == "ui/designFilter") {
    DesignFilter f = DesignFilter::Part;
    parseDesignFilter(ui.string(key).toStdString(), &f);
    ribbon_->setDesignFilter(f);
    updateWorkspaceSelector();
  } else if (key == "ui/commandLine") {
    commandLine_->setVisible(ui.flag(key));
  } else if (key == "ui/sidebarSide") {
    applySidebarSide();
    for (const auto& t : tabs_)
      if (t->dialog()) applyCardSettings(t->dialog());
  } else if (key == "ui/timelineStyle") {
    applyTimelineStyle();
  } else if (key == "ui/cardPlacement" || key == "ui/cardFreePos") {
    for (const auto& t : tabs_)
      if (t->dialog()) applyCardSettings(t->dialog());
  } else if (key == "ui/orientation") {
    for (const auto& t : tabs_) applyViewportSettings(t.get());
    layoutOverlays();
  }
  // ui/toolbarArea and quickToolbar are stored; the toolbar stays at the top for now
}

void MainWindow::applySidebarSide() {
  const Qt::DockWidgetArea area =
      UiSettings::instance().string("ui/sidebarSide") == "right" ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
  const int width = browserDock_->width() >= SidebarHost::kMinWidth ? browserDock_->width() : SidebarHost::kDefaultWidth;
  const bool moved = dockWidgetArea(browserDock_) != area;
  if (moved) {
    const bool wanted = browserDockWanted_;
    removeDockWidget(browserDock_);
    addDockWidget(area, browserDock_);
    browserDock_->setVisible(wanted && active_ && active_->isDesign());
  }
  browserDock_->setProperty("side", area == Qt::RightDockWidgetArea ? "right" : "left");
  browserDock_->style()->unpolish(browserDock_);
  browserDock_->style()->polish(browserDock_);
  if (moved || browserDock_->width() < SidebarHost::kMinWidth) resizeDocks({browserDock_}, {width}, Qt::Horizontal);
}

void MainWindow::applyTimelineStyle() {
  const bool strip = UiSettings::instance().string("ui/timelineStyle") == "strip";
  sidebar_->setTimelineStyle(strip ? SidebarHost::TimelineStyle::Strip : SidebarHost::TimelineStyle::FeatureList);
  updateTimelineDock();
}

void MainWindow::updateTimelineDock() {
  const bool strip = sidebar_->timelineStyle() == SidebarHost::TimelineStyle::Strip;
  timelineDock_->setVisible(strip && active_ && active_->isDesign() && timelineDockWanted_);
  if (strip) QTimer::singleShot(0, this, [this] { ensureTimelineHeight(); });
}

void MainWindow::updateCardDockAvailability() {
  sidebar_->setCardDockAllowed(active_ && active_->isDesign() && browserDockWanted_ && !browserDock_->isHidden());
}

void MainWindow::updateLiveRow() {
  if (!featureList_ || !manager_) return;
  core::ObjectId live = core::kInvalidId;
  std::optional<FeatureListModel::PendingLive> pending;
  if (manager_->isActive()) {
    if (editingFeature_ != core::kInvalidId && document_->feature(editingFeature_)) live = editingFeature_;
    if (sketch::SketchSession* s = sketch::SketchSession::current(); live == core::kInvalidId && s && s->manager() == manager_)
      live = s->sketchId();
    const core::ITool* t = manager_->activeTool();
    if (live == core::kInvalidId && t && !isIdleTool()) {
      const core::ToolInfo& i = t->info();
      const bool creates = !i.hidden && !i.keepsActiveTool && !i.checked && i.workspace == core::Workspace::Design &&
                           i.tab != "SKETCH" && i.tab != "TOOLS" && i.tab != "INSPECT" && i.group != "INSPECT" &&
                           !i.id.starts_with("app.") && !i.id.starts_with("view.");
      if (creates) pending = FeatureListModel::PendingLive{core::qs(i.name), i.icon, {}};
    }
  }
  featureList_->setLiveFeature(live);
  featureList_->setPendingLive(pending);
  updateBreadcrumb();
}

void MainWindow::applyViewportSettings(DocumentTab* tab) {
  if (!tab || !tab->viewer()) return;
  const bool cube = UiSettings::instance().string("ui/orientation") == "viewCube";
  tab->viewer()->setOrientationWidget(cube ? viewer::OrientationWidget::ViewCube : viewer::OrientationWidget::HexDial);
  if (NavBar* nb = tab->navBar()) nb->reposition();
}

void MainWindow::updateBreadcrumb() {
  // viewport breadcrumb (5.3): document > active component, then the gold chip of the live
  // operation: EDITING <feature> while a feature or sketch is edited, else the command name
  if (!viewer_ || !document_) return;
  viewer::Breadcrumb* bc = viewer_->breadcrumb();
  if (!bc) return;
  QStringList crumbs{core::qs(document_->name())};
  if (const core::Component* c = document_->component(document_->activeComponent()); c && !c->isRoot())
    crumbs << core::qs(c->name);
  else if (const auto bodies = document_->liveBodies(true); bodies.size() == 1)
    if (const core::Body* b = document_->body(bodies.front())) crumbs << core::qs(b->name);
  bc->setCrumbs(crumbs);
  QString caption, name;
  if (manager_ && manager_->isActive() && !isIdleTool()) {
    core::ObjectId live = editingFeature_;
    if (sketch::SketchSession* s = sketch::SketchSession::current(); live == core::kInvalidId && s && s->manager() == manager_)
      live = s->sketchId();
    if (const core::Feature* f = live != core::kInvalidId ? document_->feature(live) : nullptr) {
      caption = "EDITING";
      name = core::qs(f->name());
    } else if (const core::ITool* t = manager_->activeTool(); t && !t->info().hidden) {
      caption = core::qs(t->info().name);
    }
  }
  bc->setLiveChip(caption, name);
  viewer_->layoutOverlayWidgets();
}

void MainWindow::applyCardSettings(CommandCard* card) {
  UiSettings& ui = UiSettings::instance();
  const viewer::CardPlacement p = viewer::parsePlacement(ui.string("ui/cardPlacement")).value_or(viewer::CardPlacement::FloatRight);
  card->setSidebarOnLeft(ui.string("ui/sidebarSide") != "right");
  card->setPlacement(p, viewer::FreePos::fromString(ui.string("ui/cardFreePos")));
}

void MainWindow::storeCardPlacement(int placement, const QString& freePos) {
  UiSettings& ui = UiSettings::instance();
  const auto p = static_cast<viewer::CardPlacement>(placement);
  // a dragged position is only kept when "remember dragged card" is on (6.1 #1b); the card
  // keeps it for this session either way
  if (p == viewer::CardPlacement::Free && !ui.flag("ui/cardRememberDrag")) return;
  if (p == viewer::CardPlacement::Free) ui.set("ui/cardFreePos", freePos);
  ui.set("ui/cardPlacement", QString::fromLatin1(viewer::placementName(p)));
}

void MainWindow::setCardSlotHost(CardSlotHost* host) {
  cardSlotHost_ = host;
  for (const auto& t : tabs_)
    if (t->dialog()) t->dialog()->setSlotHost(host);
}

QString MainWindow::workspaceCaption() const {
  if (!active_) return {};
  if (!active_->isDesign()) return "Drawing";
  return UiSettings::instance().string("ui/designFilter") == "assembly" ? "Assembly" : "Part";
}

void MainWindow::ensureTimelineHeight() {
  if (!timelineDock_ || timelineDock_->isHidden()) return;
  if (timelineDock_->height() < 60) resizeDocks({timelineDock_}, {120}, Qt::Vertical);
}

MainWindow::~MainWindow() {
  qApp->removeEventFilter(this);
  // End every command while the host (dialog, status bar) is still alive: tools call back into
  // IAppHost (refreshToolDialog, showMessage) from cancel(), e.g. a dimension still being typed.
  for (auto& tab : tabs_) {
    if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->manager() == &tab->manager())
      sketch::SketchSession::end();
    if (tab->manager().isActive()) tab->manager().cancel();
  }
  // tabs own documents, managers and widgets; drop the shortcuts to them first
  active_ = nullptr;
  syncActivePointers();
  tabs_.clear();
}

void MainWindow::registerModules() {
  core::ModuleContext ctx{core::FeatureFactory::instance(), registry_};
  sketch::registerModule(ctx);
  part::registerModule(ctx);
  assembly::registerModule(ctx);
  drawing::registerModule(ctx);
  registerAppTools(*this, registry_);
  registerScriptTools(*this, registry_);
}

void MainWindow::buildUi() {
  // --- top: title bar (menu widget area); the toolbar goes into a tool bar area below ---------
  setMenuWidget(buildAppBar());

  // --- central: one page per tab (viewer or drawing workspace) --------------------------
  central_ = new QStackedWidget(this);
  setCentralWidget(central_);

  // --- browser (left dock) --------------------------------------------------------------------
  // --- sidebar (redesign 6.2): FEATURES, MODEL, SELECTION and the card slot in #BrowserDock;
  // no title bar, not floatable; left or right from ui/sidebarSide (applySidebarSide)
  browserDock_ = new QDockWidget("SIDEBAR", this);
  browserDock_->setObjectName("BrowserDock");
  browserDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  browserDock_->setTitleBarWidget(new QWidget(browserDock_));
  sidebar_ = new SidebarHost(browserDock_);
  browserStack_ = sidebar_->modelStack();
  browserDock_->setWidget(sidebar_);
  cardSlotAdapter_ = std::make_unique<SidebarCardSlotAdapter<SidebarHost>>(sidebar_);
  setCardSlotHost(cardSlotAdapter_.get());
  // a docked card falls back to floating while the sidebar cannot take it (hidden, drawing tab;
  // 6.4), and comes back
  connect(sidebar_, &SidebarHost::cardDockAvailabilityChanged, this, [this](bool) {
    QTimer::singleShot(0, this, [this] {
      if (dialog_) dialog_->reposition();
    });
  });
  addDockWidget(Qt::LeftDockWidgetArea, browserDock_);
  resizeDocks({browserDock_}, {SidebarHost::kDefaultWidth}, Qt::Horizontal);
  connect(sidebar_, &SidebarHost::masterEyeRequested, this, [this] {
    if (!document_) return;
    bool anyVisible = false;
    for (const core::Body* b : document_->bodies())
      if (b->visible && !document_->bodyShape(b->id).IsNull()) anyVisible = true;
    if (!anyVisible) {
      showAllObjects();
      return;
    }
    auto macro = std::make_unique<core::MacroCommand>("Hide all bodies");
    for (const core::Body* b : document_->bodies())
      if (b->visible) macro->add(core::makeBodyEditCommand(*document_, b->id, [](core::Body& body) { body.visible = false; }));
    if (!macro->empty()) document_->undoStack().push(std::move(macro));
  });

  // --- timeline (bottom dock spanning the full width) -------------------------------------------
  timelineDock_ = new QDockWidget("TIMELINE", this);
  timelineDock_->setObjectName("TimelineDock");
  timelineDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  timelineDock_->setTitleBarWidget(new QWidget(timelineDock_));
  timelineStack_ = new QStackedWidget(timelineDock_);
  timelineDock_->setWidget(timelineStack_);
  addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);
  setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  buildStatusBar();

  // The shared shell widgets are bound to the active tab's ToolManager (setToolManager on every
  // tab switch); the first tab provides the initial one.
  DocumentTab* first = createTab(DocumentTab::Kind::Design, nullptr);
  core::ToolManager& m = first->manager();
  // main toolbar in a fixed tool bar (ui/toolbarArea: top in phase 1; left/right come with the
  // vertical layout of phase 4). Qt keeps tool bars outside the dock areas.
  ribbon_ = new MainToolbar(m, this);
  {
    DesignFilter f = DesignFilter::Part;
    parseDesignFilter(UiSettings::instance().string("ui/designFilter").toStdString(), &f);
    ribbon_->setDesignFilter(f);
  }
  toolbarHost_ = new QToolBar("Toolbar", this);
  toolbarHost_->setObjectName("MainToolbarHost");
  toolbarHost_->setMovable(false);
  toolbarHost_->setFloatable(false);
  toolbarHost_->setContextMenuPolicy(Qt::PreventContextMenu);
  toolbarHost_->toggleViewAction()->setVisible(false);
  toolbarHost_->addWidget(ribbon_);
  addToolBar(Qt::TopToolBarArea, toolbarHost_);
  commandLine_ = new CommandLine(m, statusBar());
  commandLine_->setVisible(UiSettings::instance().flag("ui/commandLine"));
  addStatusCells();
  // G2 APP-06: empty Enter in the box with a tool active behaves like Enter on the canvas
  connect(commandLine_, &CommandLine::emptyEnter, this,
          [this] { handleKeyPress(Qt::Key_Return, 0, "\r", false, viewer_); });
  search_ = new CommandSearch(m, this);
  marking_ = new MarkingMenu(this);
}

QWidget* MainWindow::buildAppBar() {
  auto* bar = new QWidget(this);
  bar->setObjectName("AppBar");
  bar->setAttribute(Qt::WA_StyledBackground);
  bar->setFixedHeight(DocumentTabBar::kHeight);
  auto* h = new QHBoxLayout(bar);
  h->setContentsMargins(8, 0, 8, 0);
  h->setSpacing(4);

  // logo + FABRICAD: the application (File) menu
  auto* appMenu = new QToolButton(bar);
  appMenu->setObjectName("AppMenuButton");
  appMenu->setProperty("textStyle", "header");
  appMenu->setIcon(resourceIcon("fabricad"));
  appMenu->setIconSize(QSize(18, 18));
  appMenu->setText("FabriCAD");
  appMenu->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  appMenu->setPopupMode(QToolButton::InstantPopup);
  appMenu->setToolTip("File");
  appMenu->setFocusPolicy(Qt::NoFocus);
  appMenu->setAutoRaise(true);
  auto* fileMenu = new QMenu(appMenu);
  buildFileMenu(fileMenu);
  appMenu->setMenu(fileMenu);
  h->addWidget(appMenu);
  h->addSpacing(4);

  workspaceSelector_ = new WorkspaceSelector(bar);
  connect(workspaceSelector_, &WorkspaceSelector::requested, this, [this](WorkspaceChoice c) {
    setWorkspaceChoice(c);
  });
  h->addWidget(workspaceSelector_, 0, Qt::AlignVCenter);
  h->addSpacing(8);

  // document tabs (one per open design / drawing, "+" for a new design): angled, painted (7.1)
  auto* tabs = new DocumentTabBar(bar);
  tabBar_ = tabs;
  tabBar_->setTabsClosable(true);
  tabBar_->setMovable(true);
  tabBar_->setElideMode(Qt::ElideRight);
  tabBar_->setUsesScrollButtons(true);
  tabBar_->setFocusPolicy(Qt::NoFocus);
  connect(tabBar_, &QTabBar::currentChanged, this, [this](int index) {
    if (switchingTabs_ || index < 0) return;
    activateTab(index);
  });
  connect(tabBar_, &QTabBar::tabCloseRequested, this, [this](int index) { closeTab(index); });
  connect(tabBar_, &QTabBar::tabMoved, this, [this](int from, int to) {
    if (from < 0 || to < 0 || from >= tabCount() || to >= tabCount()) return;
    std::unique_ptr<DocumentTab> t = std::move(tabs_[static_cast<size_t>(from)]);
    tabs_.erase(tabs_.begin() + from);
    tabs_.insert(tabs_.begin() + to, std::move(t));
    scheduleTabBarRefresh();  // labels, tooltips and the active tab once the move settles (DOC-11)
  });
  h->addWidget(tabBar_, 0, Qt::AlignBottom);
  auto* newTab = new QToolButton(bar);
  newTab->setObjectName("NewTabButton");
  newTab->setText("+");
  newTab->setToolTip("New Design (Ctrl+N)");
  newTab->setAutoRaise(true);
  newTab->setFocusPolicy(Qt::NoFocus);
  connect(newTab, &QToolButton::clicked, this, [this] { manager_->run("app.new"); });
  h->addWidget(newTab);
  h->addStretch(1);

  // command search box: opens the S-box (#SearchPopup)
  auto* searchBox = new QToolButton(bar);
  searchBox_ = searchBox;
  searchBox->setObjectName("SearchBox");
  searchBox->setFocusPolicy(Qt::NoFocus);
  searchBox->setToolTip("Search commands (S)");
  searchBox->setFixedSize(260, 26);
  {
    auto* sh = new QHBoxLayout(searchBox);
    sh->setContentsMargins(8, 0, 4, 0);
    sh->setSpacing(6);
    auto* glyph = new QLabel(">_", searchBox);
    glyph->setObjectName("SearchGlyph");
    auto* text = new QLabel("Search commands", searchBox);
    text->setObjectName("SearchBoxText");
    auto* key = new Keycap("S", searchBox);
    for (QWidget* w : {static_cast<QWidget*>(glyph), static_cast<QWidget*>(text), static_cast<QWidget*>(key)})
      w->setAttribute(Qt::WA_TransparentForMouseEvents);
    sh->addWidget(glyph);
    sh->addWidget(text, 1);
    sh->addWidget(key);
  }
  connect(searchBox, &QToolButton::clicked, this, [this] { openCommandSearch(); });
  h->addWidget(searchBox, 0, Qt::AlignVCenter);
  h->addSpacing(8);
  undoButton_ = appBarButton(bar, "undo", "Undo (Ctrl+Z)");
  connect(undoButton_, &QToolButton::clicked, this, [this] { manager_->run("app.undo"); });
  h->addWidget(undoButton_);
  redoButton_ = appBarButton(bar, "redo", "Redo (Ctrl+Y)");
  connect(redoButton_, &QToolButton::clicked, this, [this] { manager_->run("app.redo"); });
  h->addWidget(redoButton_);
  auto* saveButton = appBarButton(bar, "save", "Save (Ctrl+S)");
  connect(saveButton, &QToolButton::clicked, this, [this] { save(); });
  h->addWidget(saveButton);
  auto* help = appBarButton(bar, "help", "Help");
  help->setPopupMode(QToolButton::InstantPopup);
  auto* helpMenu = new QMenu(help);
  helpMenu->addAction("Keyboard Shortcuts", this, [this] { showShortcutsHelp(); });
  helpMenu->addAction("3D View Information", this, [this] {
    const QString info = viewer_ ? viewer_->glInfo() : QString();
    QMessageBox::information(this, "3D View", info.isEmpty() ? QString("Not initialised") : info);
  });
  helpMenu->addSeparator();
  helpMenu->addAction("About FabriCAD", this, [this] {
    QMessageBox::about(this, "About FabriCAD",
                       QString("<b>FabriCAD %1</b><br>Open-source parametric 3D CAD.<br>"
                               "LGPL-2.1-or-later. Built on Qt %2 and Open CASCADE Technology.")
                           .arg(QApplication::applicationVersion(), qVersion()));
  });
  help->setMenu(helpMenu);
  h->addWidget(help);
  return bar;
}

void MainWindow::addStatusCells() {
  QStatusBar* sb = statusBar();
  sb->addPermanentWidget(commandLine_);
  sb->addPermanentWidget(sketchLabel_);
  sb->addPermanentWidget(occtCell_);
  sb->addPermanentWidget(unitsLabel_);
  sb->addPermanentWidget(gridCell_);
  sb->addPermanentWidget(snapCell_);
  sb->addPermanentWidget(rebuildMeter_);
  sb->addPermanentWidget(selectionLabel_);
  updateKeyHints();
  updateViewCells();
}

void MainWindow::buildFileMenu(QMenu* m) {
  auto add = [&](const char* icon, const QString& text, const char* toolId) {
    QAction* a = m->addAction(resourceIcon(icon), text);
    connect(a, &QAction::triggered, this, [this, toolId] { manager_->run(toolId); });
    return a;
  };
  add("new", "New Design\tCtrl+N", "app.new");
  add("open", "Open...\tCtrl+O", "app.open");
  QMenu* recent = m->addMenu("Open Recent");
  connect(recent, &QMenu::aboutToShow, this, [this, recent] {
    recent->clear();
    recent->setToolTipsVisible(true);
    // file names that occur more than once get their folder (DOC-11)
    std::map<QString, int> names;
    for (const QString& p : recent_.list()) ++names[QFileInfo(p).fileName()];
    for (const QString& p : recent_.list())
      recent->addAction(names[QFileInfo(p).fileName()] > 1
                            ? QString("%1 (%2)").arg(QFileInfo(p).fileName(), QFileInfo(QFileInfo(p).absolutePath()).fileName())
                            : QFileInfo(p).fileName(),
                        this, [this, p] {
        QString err;
        if (!openFile(p, &err)) {
          recent_.remove(p);
          recent_.save();
          showMessage("Cannot open " + p.toStdString() + ": " + err.toStdString(), core::MessageLevel::Error);
        }
      })->setToolTip(p);
    if (recent_.list().isEmpty()) recent->addAction("(none)")->setEnabled(false);
    else {
      recent->addSeparator();
      recent->addAction("Clear Recent", this, [this] {
        recent_.clear();
        recent_.save();
      });
    }
  });
  m->addSeparator();
  add("save", "Save\tCtrl+S", "app.save");
  add("save-as", "Save As...\tCtrl+Shift+S", "app.save-as");
  add("close", "Close Tab\tCtrl+W", "app.close");
  m->addSeparator();
  // import/export tools from the modules (anything whose id says import/export)
  QMenu* imp = m->addMenu("Insert / Import");
  QMenu* exp = m->addMenu("Export");
  connect(m, &QMenu::aboutToShow, this, [this, imp, exp] {
    imp->clear();
    exp->clear();
    const core::ToolContext ctx = manager_->makeContext();
    for (core::ITool* t : registry_.tools()) {
      const core::ToolInfo& i = t->info();
      if (i.hidden) continue;
      QMenu* target = i.id.find("export") != std::string::npos ? exp
                      : i.id.find("import") != std::string::npos ? imp
                                                                  : nullptr;
      if (!target) continue;
      QAction* a = target->addAction(toolIcon(i), core::qs(i.name));
      a->setEnabled(t->isEnabled(ctx));
      connect(a, &QAction::triggered, this, [this, id = i.id] { manager_->run(id); });
    }
    imp->setEnabled(!imp->isEmpty());
    exp->setEnabled(!exp->isEmpty());
  });
  m->addSeparator();
  add("parameters", "Change Parameters...", "app.parameters");
  m->addSeparator();
  // the panels can always be brought back, also after closing the browser with its x (DOC-02)
  QMenu* panels = m->addMenu("Show Panels");
  panels->setObjectName("ShowPanelsMenu");
  QAction* browserToggle = panels->addAction("Browser");
  browserToggle->setObjectName("ShowBrowserAction");
  browserToggle->setCheckable(true);
  QAction* timelineToggle = panels->addAction("Timeline");
  timelineToggle->setObjectName("ShowTimelineAction");
  timelineToggle->setCheckable(true);
  connect(panels, &QMenu::aboutToShow, this, [this, browserToggle, timelineToggle] {
    const bool design = active_ && active_->isDesign();
    browserToggle->setChecked(design ? !browserDock_->isHidden() : browserDockWanted_);
    timelineToggle->setChecked(sidebar_->timelineStyle() == SidebarHost::TimelineStyle::FeatureList
                                   ? !browserDock_->isHidden() && !sidebar_->isPanelCollapsed(SidebarHost::Panel::Features)
                                   : timelineDockWanted_);
    timelineToggle->setEnabled(design);
  });
  connect(browserToggle, &QAction::triggered, this, [this](bool on) { setBrowserVisible(on); });
  connect(timelineToggle, &QAction::triggered, this, [this](bool on) {
    // feature-list style: the FEATURES panel opens or folds; strip style: the bottom dock
    if (sidebar_->timelineStyle() == SidebarHost::TimelineStyle::FeatureList) {
      if (on) setBrowserVisible(true);
      sidebar_->setPanelCollapsed(SidebarHost::Panel::Features, !on);
      return;
    }
    timelineDockWanted_ = on;
    updateTimelineDock();
  });
  m->addSeparator();
  m->addAction("Exit", this, [this] { close(); });
}

void MainWindow::buildStatusBar() {
  QStatusBar* sb = statusBar();
  sb->setSizeGripEnabled(false);
  sb->setFixedHeight(28);
  // left: active command | prompt | key hints | message (the command line and the fixed cells
  // are added by addStatusCells() once the command line exists)
  statusCommand_ = new StatusCommandCell(sb);
  promptLabel_ = new ElideLabel(sb);
  promptLabel_->setObjectName("StatusPrompt");
  keyHints_ = new KeyHints(sb);
  messageLabel_ = new ElideLabel(sb);
  messageLabel_->setObjectName("StatusMessage");  // as typed: paths and names are user data
  messageLabel_->installEventFilter(this);         // click: the full text in the command history
  selectionLabel_ = new QLabel(sb);
  selectionLabel_->setObjectName("StatusDim");
  sketchLabel_ = new QLabel(sb);
  sketchLabel_->hide();
  unitsLabel_ = new QLabel(sb);
  unitsLabel_->setObjectName("StatusDim");
  unitsLabel_->setProperty("textCase", "as-is");  // units are lowercase (3.3)
  // G2 APP-03: clicking the units opens Preferences > Document (UNITS command)
  unitsLabel_->setCursor(Qt::PointingHandCursor);
  unitsLabel_->setToolTip("Document units (click to change)");
  installClickHandler(unitsLabel_, [this] { runCommand("app.units"); });
  occtCell_ = new StatusCell("OCCT", sb);
  occtCell_->setValue(QString(OCC_VERSION_STRING));
  occtCell_->setToolTip(QString("Open CASCADE Technology %1").arg(OCC_VERSION_COMPLETE));
  gridCell_ = new StatusCell("Grid", sb);
  gridCell_->setToolTip("Layout grid (click to show or hide)");
  gridCell_->setClickAction([this] { runCommand("view.grid"); QTimer::singleShot(0, this, &MainWindow::updateViewCells); });
  snapCell_ = new StatusCell("Snap", sb);
  snapCell_->setToolTip("Snap to grid in sketches (click to toggle)");
  snapCell_->setClickAction([this] { runCommand("view.snap"); QTimer::singleShot(0, this, &MainWindow::updateViewCells); });
  rebuildMeter_ = new RebuildMeter(sb);
  sb->addWidget(statusCommand_);
  sb->addWidget(promptLabel_);  // sized to its text, elided when the bar is short
  sb->addWidget(keyHints_);
  sb->addWidget(messageLabel_, 2);
  messageLabel_->hide();
  messageTimer_ = new QTimer(this);
  messageTimer_->setSingleShot(true);
  connect(messageTimer_, &QTimer::timeout, this, [this] {
    messageLabel_->clear();
    messageLabel_->hide();  // the prompt gets the room back
  });
  autosaveTimer_ = new QTimer(this);
  autosaveTimer_->setSingleShot(true);
  autosaveTimer_->setInterval(5000);
  connect(autosaveTimer_, &QTimer::timeout, this, [this] { autosaveNow(); });
  autosavePeriodic_ = new QTimer(this);
  autosavePeriodic_->setInterval(120000);
  connect(autosavePeriodic_, &QTimer::timeout, this, [this] { autosaveNow(); });
  autosavePeriodic_->start();
}

// =========================================================================================
// document tabs
// =========================================================================================

DocumentTab* MainWindow::tabAt(int index) const {
  if (index < 0 || index >= tabCount()) return nullptr;
  return tabs_[static_cast<size_t>(index)].get();
}

int MainWindow::indexOfTab(const DocumentTab* tab) const {
  for (int i = 0; i < tabCount(); ++i)
    if (tabs_[static_cast<size_t>(i)].get() == tab) return i;
  return -1;
}

DocumentTab* MainWindow::tabForDocument(const core::Document& doc, DocumentTab::Kind kind) const {
  for (const auto& t : tabs_)
    if (&t->document() == &doc && t->kind() == kind) return t.get();
  return nullptr;
}

DocumentTab* MainWindow::findTab(const std::string& spec) const {
  if (spec.empty()) return nullptr;
  try {
    size_t used = 0;
    const int n = std::stoi(spec, &used);
    if (used == spec.size()) return tabAt(n - 1);
  } catch (...) {
  }
  const QString q = core::qs(spec);
  for (const auto& t : tabs_) {
    QString title = t->title();
    if (title.endsWith(" *")) title.chop(2);
    if (title.compare(q, Qt::CaseInsensitive) == 0) return t.get();
  }
  for (const auto& t : tabs_)
    if (core::qs(t->document().name()).compare(q, Qt::CaseInsensitive) == 0 && t->isDesign()) return t.get();
  return nullptr;
}

DocumentTab* MainWindow::createTab(DocumentTab::Kind kind, std::shared_ptr<core::Document> doc) {
  DocumentTab::Hosts hosts{central_, browserStack_, timelineStack_, sidebar_->featureStack()};
  auto tab = std::make_unique<DocumentTab>(kind, std::move(doc), registry_, hosts, drawingFactory(), this);
  DocumentTab* raw = tab.get();
  raw->manager().setHost(this);
  tabs_.push_back(std::move(tab));
  connectTabSignals(*raw);
  switchingTabs_ = true;
  const int index = tabBar_->addTab(raw->title());
  tabBar_->setTabToolTip(index, raw->tooltip());
  switchingTabs_ = false;
  if (glEnabled_ && isVisible()) raw->initializeViewer();
  return raw;
}

DocumentTab* MainWindow::addDesignTab(std::shared_ptr<core::Document> doc) {
  DocumentTab* tab = createTab(DocumentTab::Kind::Design, std::move(doc));
  activateTab(tab);
  return tab;
}

DocumentTab* MainWindow::openDrawingTab(DocumentTab& design) {
  DocumentTab* existing = tabForDocument(design.document(), DocumentTab::Kind::Drawing);
  if (!existing) existing = createTab(DocumentTab::Kind::Drawing, design.documentPtr());
  activateTab(existing);
  return existing;
}

void MainWindow::activateTab(DocumentTab* tab) {
  if (!tab || tab == active_) return;
  DocumentTab* previous = active_;
  if (previous) {
    // Leave the previous tab like Fusion leaves a document: the running command is cancelled
    // and an open sketch is finished (the sketch session is a per-process singleton). Deferred,
    // because a tool may be switching tabs from inside its own activate()/accept().
    core::ToolManager* pm = &previous->manager();
    QTimer::singleShot(0, this, [this, pm] {
      for (const auto& t : tabs_)
        if (&t->manager() == pm && t.get() != active_) {
          if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->manager() == pm) sketch::SketchSession::end();
          if (pm->isActive()) pm->cancel();
          return;
        }
    });
  }
  active_ = tab;
  syncActivePointers();

  switchingTabs_ = true;
  tabBar_->setCurrentIndex(indexOfTab(tab));
  switchingTabs_ = false;
  central_->setCurrentWidget(tab->centralWidget());
  if (tab->browser()) browserStack_->setCurrentWidget(tab->browser());
  if (tab->timeline()) timelineStack_->setCurrentWidget(tab->timeline());
  // The dock follows the tab being shown, not the change of kind: after the last tab closes
  // there is no previous tab and the new design must still get its browser back (DOC-02).
  if (previous && previous->isDesign()) browserDockWanted_ = !browserDock_->isHidden();
  if (tab->isDesign()) browserDock_->setVisible(browserDockWanted_);
  else browserDock_->hide();
  sidebar_->setActive(tab->isDesign() ? &tab->document() : nullptr, tab->featureList(), tab->browser());
  updateTimelineDock();
  updateCardDockAvailability();
  if (tab->isDesign() && glEnabled_ && isVisible()) tab->initializeViewer();

  const core::Workspace ws = tab->workspace();
  const bool workspaceChanged = ws != workspace_ || !previous;
  workspace_ = ws;
  if (workspaceChanged) {
    ribbon_->rebuild(ws);
    if (previous) echoCommand(ws == core::Workspace::Design ? "workspace DESIGN" : "workspace DRAWING");
  }
  ribbon_->setContextualTab("SKETCH", false);
  updateWorkspaceSelector();
  updateTitle();
  updateUndoButtons();
  updateSelectionStatus();
  setPrompt({});
  scheduleStateRefresh();
  QTimer::singleShot(0, this, &MainWindow::layoutOverlays);
  emit activeTabChanged(tab);
}

void MainWindow::setBrowserVisible(bool on) {
  browserDockWanted_ = on;
  browserDock_->setVisible(on);
  updateCardDockAvailability();
}

void MainWindow::syncActivePointers() {
  if (dialog_) dialog_->setSlotAllowed(false);  // a background tab's card never sits in the sidebar
  document_ = active_ ? &active_->document() : nullptr;
  manager_ = active_ ? &active_->manager() : nullptr;
  viewer_ = active_ ? active_->viewer() : nullptr;
  browser_ = active_ ? active_->browser() : nullptr;
  timeline_ = active_ ? active_->timeline() : nullptr;
  featureList_ = active_ ? active_->featureList() : nullptr;
  dialog_ = active_ ? active_->dialog() : nullptr;
  navBar_ = active_ ? active_->navBar() : nullptr;
  palette_ = active_ ? active_->palette() : nullptr;
  if (dialog_) dialog_->setSlotAllowed(active_ && active_->isDesign());
  if (manager_) {
    ribbon_->setToolManager(manager_);
    commandLine_->setToolManager(manager_);
    search_->setToolManager(manager_);
  }
}

bool MainWindow::tabDragging() const {
  // the button state guards against a release that never reached the tab bar
  return tabDragActive_ && (QGuiApplication::mouseButtons() & Qt::LeftButton);
}

void MainWindow::scheduleTabBarRefresh() {
  if (tabBarRefreshPending_) return;
  tabBarRefreshPending_ = true;
  QTimer::singleShot(0, this, [this] {
    tabBarRefreshPending_ = false;
    if (tabDragging()) {
      tabBarRefreshDeferred_ = true;  // runs on release
      return;
    }
    refreshTabBar();
    switchingTabs_ = true;
    if (active_) tabBar_->setCurrentIndex(indexOfTab(active_));
    switchingTabs_ = false;
    tabBar_->update();
  });
}

void MainWindow::refreshTabBar() {
  // Setting tab texts by index while a tab is being dragged paints the moving tab's label on
  // the wrong slot: wait for the release (DOC-11)
  if (tabDragging()) {
    tabBarRefreshDeferred_ = true;
    return;
  }
  // Same-named tabs get their folder appended so they can be told apart (DOC-11)
  auto plain = [](QString t) {
    if (t.endsWith(" *")) t.chop(2);
    return t;
  };
  std::map<QString, int> counts;
  for (const auto& t : tabs_) ++counts[plain(t->title())];
  switchingTabs_ = true;
  for (int i = 0; i < tabCount(); ++i) {
    const DocumentTab& t = *tabs_[static_cast<size_t>(i)];
    QString text = t.title();
    if (counts[plain(text)] > 1 && !t.document().filePath().empty()) {
      const QString folder = QFileInfo(QFileInfo(core::qs(t.document().filePath())).absolutePath()).fileName();
      text = QString("%1 (%2)%3").arg(plain(text), folder, text.endsWith(" *") ? " *" : "");
    }
    tabBar_->setTabText(i, text);
    tabBar_->setTabToolTip(i, t.tooltip());
  }
  switchingTabs_ = false;
}

bool MainWindow::closeTab(DocumentTab* tab) {
  if (!tab || indexOfTab(tab) < 0) return false;
  // Closing the design closes the document, drawings included (Fusion); closing a drawing
  // closes just that tab unless it is the document's last one. Tabs are counted rather than
  // shared_ptr owners, which anything may hold (DOC-01).
  std::vector<DocumentTab*> group;
  for (const auto& t : tabs_)
    if (&t->document() == &tab->document()) group.push_back(t.get());
  const bool closesDocument = tab->isDesign() || group.size() == 1;
  if (closesDocument && !maybeSave(*tab)) return false;
  if (!closesDocument) group = {tab};
  // the active tab goes last, so the neighbour it hands over to is not about to close as well
  std::stable_partition(group.begin(), group.end(), [this](DocumentTab* t) { return t != active_; });
  if (closesDocument) clearRecoveryFile(*tab);
  for (DocumentTab* t : group)
    if (indexOfTab(t) >= 0) closeOneTab(t);
  echoCommand("close");
  return true;
}

void MainWindow::closeOneTab(DocumentTab* tab) {
  if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->manager() == &tab->manager()) sketch::SketchSession::end();
  if (tab->manager().isActive()) tab->manager().cancel();
  const int index = indexOfTab(tab);
  if (tab == active_) {
    // choose the neighbour (Fusion: the tab to the left)
    DocumentTab* next = tabCount() > 1 ? tabAt(index > 0 ? index - 1 : 1) : nullptr;
    if (!next) {
      destroyTab(tab);
      addDesignTab();
      return;
    }
    activateTab(next);
  }
  destroyTab(tab);
}

void MainWindow::destroyTab(DocumentTab* tab) {
  const int index = indexOfTab(tab);
  if (index < 0) return;
  if (tab == active_) {
    if (tab->isDesign()) browserDockWanted_ = !browserDock_->isHidden();  // closed with its x (DOC-02)
    active_ = nullptr;
    syncActivePointers();
  }
  bool lastOfDocument = true;
  for (const auto& t : tabs_)
    if (t.get() != tab && &t->document() == &tab->document()) lastOfDocument = false;
  if (lastOfDocument) failedFeatures_.erase(&tab->document());
  switchingTabs_ = true;
  tabBar_->removeTab(index);
  switchingTabs_ = false;
  tabs_.erase(tabs_.begin() + index);
}

// =========================================================================================
// autosave / recovery
// =========================================================================================

namespace {
QString sanitizedFileStem(const std::string& name) {
  QString s = core::qs(name.empty() ? std::string("Untitled") : name);
  for (QChar& c : s)
    if (!c.isLetterOrNumber() && c != '-' && c != '_') c = '_';
  return s.left(60);
}
}  // namespace

namespace {
constexpr const char* kRecoveryIdProperty = "fabricadRecoveryId";
constexpr const char* kSessionLockName = "session.lock";

// One id per document (a design and its drawing tabs share the document and its autosave).
QString recoveryIdOf(const core::Document& doc) {
  auto& d = const_cast<core::Document&>(doc);
  QString id = d.property(kRecoveryIdProperty).toString();
  if (id.isEmpty()) {
    id = QUuid::createUuid().toString(QUuid::Id128).left(8);
    d.setProperty(kRecoveryIdProperty, id);
  }
  return id;
}

void removeDirIfEmpty(const QString& path) {
  QDir d(path);
  if (d.exists() && d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
    QDir().rmdir(path);
}
}  // namespace

QString MainWindow::recoveryDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
}

QString MainWindow::sessionRecoveryDirectory() const {
  if (sessionId_.isEmpty()) const_cast<MainWindow*>(this)->sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return recoveryDirectory() + "/" + sessionId_;
}

bool MainWindow::ensureSessionRecoveryDirectory() {
  const QString dir = sessionRecoveryDirectory();
  const QString lockPath = dir + "/" + kSessionLockName;
  if (sessionLock_ && sessionLock_->isLocked() && QFileInfo::exists(lockPath)) return true;
  // first autosave, or the folder was removed under us: (re)create it and take the lock again
  if (sessionLock_) {
    if (QFileInfo::exists(lockPath)) sessionLock_->unlock();
    sessionLock_.reset();
  }
  if (!QDir().mkpath(dir)) return false;
  sessionLock_ = std::make_unique<QLockFile>(lockPath);
  sessionLock_->setStaleLockTime(0);  // stale only when the owning process is gone
  if (!sessionLock_->tryLock(100)) {
    FC_WARN("cannot lock the recovery folder {}", dir.toStdString());
    sessionLock_.reset();
  }
  return true;
}

void MainWindow::releaseSessionRecoveryDirectory() {
  const QString dir = sessionRecoveryDirectory();
  if (sessionLock_) {
    sessionLock_->unlock();
    sessionLock_.reset();
  }
  removeDirIfEmpty(dir);
}

QString MainWindow::recoveryFilePath(const DocumentTab& tab) const {
  // one file per document (name for readability + per-document id); the original path travels
  // inside the file ("recoveryOf")
  return sessionRecoveryDirectory() + "/" + sanitizedFileStem(tab.document().name()) + "-" + recoveryIdOf(tab.document()) +
         ".fcad";
}

QString MainWindow::recoveryFilePath() const { return active_ ? recoveryFilePath(*active_) : QString(); }

void MainWindow::scheduleAutosave() {
  if (autosaveTimer_ && document_ && document_->isModified()) autosaveTimer_->start();
}

bool MainWindow::autosave(DocumentTab& tab) {
  core::Document& doc = tab.document();
  if (!doc.isModified()) return false;
  if (!ensureSessionRecoveryDirectory()) return false;
  emit doc.aboutToSave();  // modules flush their extension data (drawing sheets, UI state)
  QJsonObject root = doc.toJson();
  root["recoveryOf"] = core::qs(doc.filePath());
  root["recoveryTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  const QString path = recoveryFilePath(tab);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  if (!file.commit()) return false;
  if (!tab.lastRecoveryFile.isEmpty() && tab.lastRecoveryFile != path) QFile::remove(tab.lastRecoveryFile);  // renamed document
  tab.lastRecoveryFile = path;
  // the drawing tabs of this document share the file
  for (const auto& t : tabs_)
    if (&t->document() == &doc) t->lastRecoveryFile = path;
  return true;
}

bool MainWindow::autosaveNow() {
  bool any = false;
  std::vector<const core::Document*> done;
  for (const auto& t : tabs_) {
    if (std::find(done.begin(), done.end(), &t->document()) != done.end()) continue;
    done.push_back(&t->document());
    any = autosave(*t) || any;
  }
  return any;
}

void MainWindow::clearRecoveryFile(DocumentTab& tab) {
  if (autosaveTimer_ && &tab == active_) autosaveTimer_->stop();
  QFile::remove(recoveryFilePath(tab));
  if (!tab.lastRecoveryFile.isEmpty()) QFile::remove(tab.lastRecoveryFile);
  for (const auto& t : tabs_)
    if (&t->document() == &tab.document()) t->lastRecoveryFile.clear();
  tab.lastRecoveryFile.clear();
}

void MainWindow::clearRecoveryFile() {
  if (active_) clearRecoveryFile(*active_);
}

QStringList MainWindow::orphanedRecoveryFiles() const {
  QDir root(recoveryDirectory());
  if (!root.exists()) return {};
  QFileInfoList found = root.entryInfoList({"*.fcad"}, QDir::Files);  // legacy flat files
  const QString own = QFileInfo(sessionRecoveryDirectory()).fileName();
  for (const QFileInfo& sub : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (sub.fileName() == own) continue;
    const QString lockPath = sub.absoluteFilePath() + "/" + kSessionLockName;
    if (QFileInfo::exists(lockPath)) {
      // a lock we cannot take belongs to a running instance; a stale one (dead pid) is taken
      // over and released again
      QLockFile probe(lockPath);
      probe.setStaleLockTime(0);
      if (!probe.tryLock(0)) continue;
      probe.unlock();
    }
    found += QDir(sub.absoluteFilePath()).entryInfoList({"*.fcad"}, QDir::Files);
  }
  std::sort(found.begin(), found.end(),
            [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
  QStringList out;
  for (const QFileInfo& fi : found) out << fi.absoluteFilePath();
  return out;
}

namespace {
void removeRecoveryFile(const QString& path) {
  QFile::remove(path);
  const QString dir = QFileInfo(path).absolutePath();
  if (QDir(dir) != QDir(MainWindow::recoveryDirectory())) {
    QFile::remove(dir + "/" + kSessionLockName);  // only reached for orphaned (unlocked) folders
    removeDirIfEmpty(dir);
  }
}

bool readRecoveryFile(const QString& path, QJsonObject* out, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  QJsonParseError perr;
  const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &perr);
  if (perr.error != QJsonParseError::NoError || !jd.isObject()) {
    if (error) *error = "the autosave is damaged";
    return false;
  }
  *out = jd.object();
  return true;
}
}  // namespace

bool MainWindow::recoverFile(const QString& path, QString* error) {
  QJsonObject root;
  QString why;
  if (!readRecoveryFile(path, &root, &why)) {
    if (QFileInfo::exists(path)) removeRecoveryFile(path);
    if (error) *error = why;
    return false;
  }
  const QString name = root["name"].toString("Untitled");
  const QString original = root["recoveryOf"].toString();
  // recover into a fresh tab (or the pristine start-up tab)
  if (!active_ || !active_->isPristine()) addDesignTab();
  if (manager_->isActive()) manager_->cancel();
  if (viewer_) viewer_->setHiddenFeatures({});
  document_->clear();
  std::string err;
  if (!document_->fromJson(root, &err)) {
    if (error) *error = core::qs(err);
    document_->clear();
    afterDocumentReplaced();
    removeRecoveryFile(path);
    return false;
  }
  document_->undoStack().clear();
  document_->setName(name.toStdString());
  document_->setModified(true);  // it only lives in memory: Save (As) stores it for good
  document_->undoStack().resetClean();  // and no undo gets back to an unmodified state (DOC-09)
  afterDocumentReplaced();
  // keep a recovery copy at all times (DOC-02): write this session's copy first, then drop the
  // orphaned one; if the write fails the old file stays and is offered again next time
  if (autosave(*active_)) removeRecoveryFile(path);
  showMessage(original.isEmpty() ? "Recovered autosaved design; use Save As to keep it"
                                 : "Recovered autosaved changes of " + original.toStdString() + "; Save to keep them",
              core::MessageLevel::Warning);
  echoCommand("recover " + QFileInfo(path).fileName().toStdString());
  return true;
}

bool MainWindow::offerRecovery() {
  if (!interactive_ || qEnvironmentVariableIntValue("FABRICAD_NO_RECOVERY") != 0) return false;
  const QStringList files = orphanedRecoveryFiles();
  if (files.isEmpty()) return false;
  std::string list;
  for (const QString& p : files) {
    QJsonObject root;
    const QString when = QFileInfo(p).lastModified().toString("yyyy-MM-dd hh:mm");
    if (!readRecoveryFile(p, &root, nullptr)) {
      list += std::format("\n  - (damaged autosave {}) from {}", QFileInfo(p).fileName().toStdString(), when.toStdString());
      continue;
    }
    const QString name = root["name"].toString("Untitled");
    const QString original = root["recoveryOf"].toString();
    list += std::format("\n  - '{}'{} from {}", name.toStdString(), original.isEmpty() ? "" : " (" + original.toStdString() + ")",
                        when.toStdString());
  }
  const bool one = files.size() == 1;
  std::string question =
      std::format("FabriCAD did not close cleanly last time. {} found:{}\n\nRecover {}? (No deletes {})",
                  one ? "An autosaved copy was" : std::to_string(files.size()) + " autosaved copies were", list,
                  one ? "it" : "them (each opens in its own tab)", one ? "the autosave" : "these autosaves");
  const bool yes = askYesNo("Recover unsaved work", question, true);
  if (!yes) {
    for (const QString& p : files) removeRecoveryFile(p);
    return false;
  }
  int recovered = 0;
  QStringList failed;
  for (const QString& p : files) {
    QString err;
    if (recoverFile(p, &err)) ++recovered;
    else failed << QFileInfo(p).fileName() + ": " + err;
  }
  if (!failed.isEmpty())
    showMessage("Could not recover " + failed.join("; ").toStdString(), core::MessageLevel::Error);
  return recovered > 0;
}

// =========================================================================================
// signals
// =========================================================================================

void MainWindow::connectShellSignals() {
  connect(ribbon_, &MainToolbar::toolTriggered, this, [this](const std::string& id) { runCommand(id); });
  connect(search_, &CommandSearch::runRequested, this, [this](const std::string& id) { runCommand(id); });
  connect(marking_, &MarkingMenu::commandChosen, this, &MainWindow::runPseudoCommand);
}

void MainWindow::connectTabSignals(DocumentTab& tabRef) {
  DocumentTab* tab = &tabRef;
  core::Document* d = &tab->document();
  core::ToolManager* m = &tab->manager();
  auto whenActive = [this, tab](auto fn) {
    return [this, tab, fn](auto&&... args) {
      if (tab == active_) fn(std::forward<decltype(args)>(args)...);
    };
  };

  // tool manager -> command dialog / status
  connect(m, &core::ToolManager::toolActivated, this, whenActive([this](const std::string& id) { onToolActivated(id); }));
  connect(m, &core::ToolManager::toolStarted, this, whenActive([this](const std::string& id) {
            if (manager_->isActive() && manager_->activeToolId() == id && dialogActivation_ != activation_) presentDialog();
          }));
  connect(m, &core::ToolManager::toolFinished, this, [this, tab](const std::string& id, bool ok) {
    if (tab == active_) onToolFinished(id, ok);
    else if (tab->dialog()) tab->dialog()->dismiss();  // a command cancelled in a background tab
  });
  if (CommandDialog* dlg = tab->dialog()) {
    connect(dlg, &CommandDialog::accepted, this, whenActive([this] {
              if (acceptEnabled_) manager_->accept();
            }));
    connect(dlg, &CommandDialog::cancelled, this, whenActive([this] { manager_->cancel(); }));
    // phase 3 card: sidebar slot, overlay regions, placement memory, pin
    dlg->setSlotAllowed(false);  // only the active design tab docks (syncActivePointers)
    dlg->setSlotHost(cardSlotHost_);
    dlg->setOverlayRegions([tab](viewer::OverlayLayout& l) {
      // the view-tool column (right edge, below the orientation widget)
      if (NavBar* nb = tab->navBar(); nb && nb->isVisible()) {
        viewer::OverlayLayout::Item it;
        it.slot = viewer::OverlayLayout::Slot::ViewTools;
        it.fixed = nb->geometry();
        l.add(it);
      }
    });
    applyCardSettings(dlg);
    connect(dlg, &CommandCard::placementChosen, this,
            [this](viewer::CardPlacement p, const viewer::FreePos& f) { storeCardPlacement(static_cast<int>(p), f.toString()); });
    connect(dlg, &CommandCard::placed, this, whenActive([this] { layoutOverlays(); }));
    connect(dlg, &CommandCard::pinnedChanged, this, whenActive([this](bool on) {
              pinnedToolId_ = on && manager_->isActive() && !isIdleTool() ? manager_->activeToolId() : std::string();
            }));
  }
  if (SketchPalette* pal = tab->palette())
    connect(pal, &SketchPalette::commandRequested, this, [this](const std::string& id) { runCommand(id); });

  // document -> title / tab text / status (any tab: the tab bar shows every document's state)
  auto tabText = [this] { refreshTabBar(); };
  connect(d, &core::Document::modifiedChanged, this, [this, tab, tabText](bool) {
    tabText();
    if (tab == active_) updateTitle();
  });
  connect(d, &core::Document::nameChanged, this, [this, tab, tabText] {
    tabText();
    if (tab == active_) {
      updateTitle();
      updateBreadcrumb();
    }
  });
  connect(d, &core::Document::unitsChanged, this, whenActive([this] {
            updateTitle();
            if (viewer_) viewer_->update();  // readout and scale bar units
            if (viewer_ && viewer_->scaleBar()) viewer_->scaleBar()->refresh();
          }));
  connect(d, &core::Document::activeComponentChanged, this, whenActive([this](core::ObjectId) { updateBreadcrumb(); }));
  connect(d, &core::Document::recomputeStarted, this, whenActive([this] {
            if (recomputeStartMs_ < 0) recomputeStartMs_ = QDateTime::currentMSecsSinceEpoch();
          }));
  connect(d, &core::Document::recomputeFinished, this, whenActive([this] {
            if (recomputeStartMs_ >= 0) rebuildMeter_->setMilliseconds(QDateTime::currentMSecsSinceEpoch() - recomputeStartMs_);
            recomputeStartMs_ = -1;
            scheduleStateRefresh();
            scheduleFailureReport();
          }));
  connect(&d->undoStack(), &core::UndoStack::changed, this, whenActive([this] {
            updateUndoButtons();
            scheduleAutosave();
          }));
  connect(&d->selection(), &core::SelectionModel::changed, this, whenActive([this] {
            updateSelectionStatus();
            scheduleStateRefresh();
            // the feature being edited keeps its frame until the command ends
            if (timelineFrameFollowsSelection_ && !(manager_->isActive() && !isIdleTool())) clearTimelineFrameUnlessSelected();
          }));
  // a rename (or a recompute) changes what the status bar says about the selection
  connect(d, &core::Document::bodyChanged, this, whenActive([this](core::ObjectId) { scheduleSelectionStatus(); }));
  connect(d, &core::Document::featureChanged, this, whenActive([this](core::ObjectId) { scheduleSelectionStatus(); }));
  connect(d, &core::Document::componentChanged, this, whenActive([this](core::ObjectId) { scheduleSelectionStatus(); }));
  if (tab->isDesign()) {
    // per-document UI state (hidden features, display mode...) travels with the file
    connect(d, &core::Document::aboutToSave, this, [this, tab] { storeUiState(*tab); });
    connect(d, &core::Document::loaded, this, [this, tab] { applyUiState(*tab); });
  }

  // browser / timeline / viewer (only the visible tab's widgets emit, so no guard needed)
  if (BrowserTree* browser = tab->browser()) {
    connect(browser, &BrowserTree::editFeatureRequested, this, &MainWindow::editFeature);
    connect(browser, &BrowserTree::deleteRequested, this, &MainWindow::deleteRefs);
    connect(browser, &BrowserTree::runToolRequested, this, [this](const std::string& id) { runCommand(id); });
    connect(browser, &BrowserTree::findInTimelineRequested, this, [this](ObjectId id) {
      timelineFrameFollowsSelection_ = false;  // a "find" frame stays until something else happens
      if (sidebar_->timelineStyle() == SidebarHost::TimelineStyle::FeatureList && featureList_) {
        sidebar_->setPanelCollapsed(SidebarHost::Panel::Features, false);
        featureList_->scrollToFeature(id);
        featureList_->setFocus();
      } else if (timeline_) {
        timeline_->scrollToFeature(id);
      }
    });
    connect(browser, &BrowserTree::suppressRequested, this, [this](ObjectId id, bool s) {
      if (auto c = makeSuppress(*document_, id, s)) document_->undoStack().push(std::move(c));
    });
  }
  if (FeatureListWidget* list = tab->featureList()) {
    connect(list, &FeatureListWidget::editRequested, this, &MainWindow::editFeature);
    connect(list, &FeatureListWidget::deleteRequested, this, [this](const std::vector<ObjectId>& ids) {
      std::vector<ShapeRef> refs;
      for (ObjectId id : ids)
        if (const core::Feature* f = document_->feature(id)) refs.push_back(featureRef(*f));
      deleteRefs(refs);
    });
    connect(list, &FeatureListWidget::findInBrowserRequested, this, [this](ObjectId id) {
      browserDock_->show();
      sidebar_->setPanelCollapsed(SidebarHost::Panel::Model, false);
      if (browser_) browser_->scrollToFeature(id);
    });
    // + INSERT FEATURE AT ROLLBACK: the command search; new features go in at the rollback bar
    connect(list, &FeatureListWidget::insertRequested, this, [this] {
      search_->setRecent(recentCommands_);
      FeatureListWidget* l = featureList_;
      const QRect r = l ? l->rowRect(l->model().insertRow()) : QRect();
      search_->popup(l && !r.isNull() ? l->viewport()->mapToGlobal(r.bottomLeft()) : QCursor::pos());
    });
    connect(list, &FeatureListWidget::messageRequested, this, [this](const QString& text, int level) {
      showMessage(text.toStdString(), static_cast<core::MessageLevel>(level));
    });
  }
  if (TimelineWidget* timeline = tab->timeline()) {
    TimelineStrip* strip = timeline->strip();
    connect(strip->actions(), &TimelineActions::deleteRequested, this, [this](const std::vector<ObjectId>& ids) {
      std::vector<ShapeRef> refs;
      for (ObjectId id : ids)
        if (const core::Feature* f = document_->feature(id)) refs.push_back(featureRef(*f));
      deleteRefs(refs);
    });
    connect(strip->actions(), &TimelineActions::messageRequested, this, [this](const QString& text, int level) {
      showMessage(text.toStdString(), static_cast<core::MessageLevel>(level));
    });
    connect(strip, &TimelineStrip::editRequested, this, &MainWindow::editFeature);
    connect(strip, &TimelineStrip::deleteRequested, this, [this](ObjectId id) {
      if (const core::Feature* f = document_->feature(id)) deleteRefs({featureRef(*f)});
    });
    connect(strip, &TimelineStrip::suppressRequested, this, [this](ObjectId id, bool s) {
      if (auto c = makeSuppress(*document_, id, s)) document_->undoStack().push(std::move(c));
    });
    connect(strip, &TimelineStrip::findInBrowserRequested, this, [this](ObjectId id) {
      browserDock_->show();
      if (browser_) browser_->scrollToFeature(id);
    });
    connect(strip, &TimelineStrip::renameRequested, this, [this](ObjectId id) {
      browserDock_->show();
      if (!browser_) return;
      browser_->scrollToFeature(id);
      if (QTreeWidgetItem* it = browser_->itemForKey(QString("feat:%1").arg(id))) browser_->editItem(it, 0);
      else if (const core::Feature* f = document_->feature(id); f && interactive_) {
        // timeline-only features (no browser folder): ask for the name
        bool ok = false;
        QDialog dlg(this);
        dlg.setWindowTitle("Rename");
        auto* v = new QVBoxLayout(&dlg);
        auto* e = new QLineEdit(core::qs(f->name()), &dlg);
        v->addWidget(e);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        v->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        ok = dlg.exec() == QDialog::Accepted;
        if (ok)
          if (auto c = makeRenameFeature(*document_, id, e->text().trimmed().toStdString()))
            document_->undoStack().push(std::move(c));
      }
    });
    connect(strip, &TimelineStrip::selectRequested, this, [this](ObjectId id, bool add) {
      const core::Feature* f = document_->feature(id);
      if (!f) return;
      // timeline picks select the feature as a whole (part.edit-feature / Delete use it)
      const ShapeRef r = ShapeRef::feature(id);
      if (add) document_->selection().toggle(r);
      else document_->selection().set(r);
      if (timeline_) timeline_->strip()->setHighlightedFeature(id);
      timelineFrameFollowsSelection_ = true;
    });
  }
  if (viewer::ViewerWidget* viewer = tab->viewer()) {
    connect(viewer, &viewer::ViewerWidget::markingMenuRequested, this,
            [this](QPoint press, QPoint release, bool dragged) {
              if (dragged) markingGesture(press, release);
              else showMarkingMenu(press);
            });
    connect(viewer, &viewer::ViewerWidget::viewCubeMenuRequested, this, &MainWindow::viewCubeMenu);
    // phase 4 overlays: the card, palette and view-tool column follow the viewer's overlays
    connect(viewer, &viewer::ViewerWidget::overlayGeometryChanged, this, [this, tab] {
      if (NavBar* nb = tab->navBar()) nb->reposition();
      if (tab == active_) {
        if (dialog_ && dialog_->isVisible() && !dialog_->isDocked()) dialog_->reposition();
        layoutOverlays();
      }
    });
    applyViewportSettings(tab);
    connect(viewer, &viewer::ViewerWidget::entityDoubleClicked, this, [this](const ShapeRef& r) {
      if (core::hasKind(core::ShapeKind::SketchCurve | core::ShapeKind::SketchPoint | core::ShapeKind::Profile |
                            core::ShapeKind::Sketch,
                        r.kind) &&
          viewer_ && !viewer_->inSketchMode())
        editFeature(r.owner);
      else if (ObjectId f = featureOfRef(r); f != core::kInvalidId)
        editFeature(f);
    });
    connect(viewer, &viewer::ViewerWidget::viewInitFailed, this, whenActive([this](const QString& why) {
              showMessage(why.toStdString(), core::MessageLevel::Warning);
            }));
  }
}

void MainWindow::initializeViewer() {
  glEnabled_ = true;
  if (active_) active_->initializeViewer();
}

// =========================================================================================
// tool lifecycle / command dialog
// =========================================================================================

bool MainWindow::isIdleTool() const {
  const core::ITool* t = manager_ ? manager_->activeTool() : nullptr;
  // the sketch module keeps "sketch.select" running between commands
  return t && t->info().hidden && t->info().id.ends_with(".select");
}

void MainWindow::onToolActivated(const std::string& id) {
  const quint64 serial = ++activation_;
  // a new command starts from a clean status bar: an old error or warning would read as its
  // own; a message the tool posts while activating comes after this and stays (DOC-08)
  if (messageLevel_ != core::MessageLevel::Info && !messageLabel_->text().isEmpty()) clearStatusMessage();
  acceptEnabled_ = true;
  dialog_->dismiss();
  // ToolManager emits toolActivated before ITool::activate(); toolStarted (after it) builds the
  // dialog. The deferred call is a fallback for tools that become active without toolStarted.
  QTimer::singleShot(0, this, [this, id, serial] {
    if (serial != activation_ || !manager_->isActive() || manager_->activeToolId() != id) return;
    if (dialogActivation_ == activation_) return;  // already built on toolStarted
    presentDialog();
  });
  if (const core::ITool* t = registry_.tool(id); t && !t->info().hidden) {
    recentCommands_.removeAll(core::qs(id));
    recentCommands_.prepend(core::qs(id));
    while (recentCommands_.size() > 12) recentCommands_.removeLast();
  }
  scheduleStateRefresh();
}

void MainWindow::presentDialog() {
  if (!manager_ || !dialog_) return;
  core::ITool* tool = manager_->activeTool();
  core::ToolContext* ctx = manager_->activeContext();
  if (!tool || !ctx) return;
  dialogActivation_ = activation_;
  QWidget* body = tool->createDialog(*ctx, dialog_);
  if (!body) {
    dialog_->dismiss();
    return;
  }
  const bool keep = acceptEnabled_;
  const core::ToolInfo& info = tool->info();
  dialog_->setShortcut(core::qs(info.shortcut));
  dialog_->setContext(workspaceCaption(), document_ ? core::qs(document_->name()) : QString());
  dialog_->setLive(tool->wantsDialogButtons());  // an operation with Apply previews live (7.1)
  if (pinnedToolId_ != info.id) dialog_->setPinned(false);
  dialog_->present(core::qs(info.name), toolIcon(info), body, tool->wantsDialogButtons());
  dialog_->setAcceptEnabled(keep);
  layoutOverlays();
  updateKeyHints();
}

void MainWindow::layoutOverlays() {
  // One OverlayLayout for the active viewport (6.3): the card placed itself (priority 4); the
  // sketch palette (5) stacks below it on a shared side or takes the free side. The result is
  // published on the viewport's OverlayHost for other overlays and the tutorial bubble.
  if (!viewer_) {
    if (palette_ && palette_->isVisible()) palette_->reposition(nullptr);
    return;
  }
  using viewer::OverlayLayout;
  OverlayLayout l;
  l.setViewport(viewer_->rect());
  OverlayLayout::Options o;
  if (dialog_) o.card = dialog_->effectivePlacement();
  l.setOptions(o);
  auto fixed = [&l](OverlayLayout::Slot s, const QRect& r) {
    OverlayLayout::Item it;
    it.slot = s;
    it.fixed = r;
    l.add(it);
  };
  viewer_->addOverlayRegions(l);  // orientation widget, breadcrumb + filter row, readout, scale bar
  if (navBar_ && navBar_->isVisible()) fixed(OverlayLayout::Slot::ViewTools, navBar_->geometry());
  const bool cardFloats = dialog_ && dialog_->isVisible() && !dialog_->isDocked() && dialog_->parentWidget() == viewer_;
  if (cardFloats) fixed(OverlayLayout::Slot::Card, dialog_->geometry());
  if (palette_ && palette_->isVisible()) {
    palette_->adjustSize();
    OverlayLayout::Item it;
    it.slot = OverlayLayout::Slot::SketchPalette;
    it.size = palette_->size();
    it.userMoved = palette_->isUserMoved();
    if (it.userMoved) it.fixed = palette_->geometry();
    l.add(it);
  }
  l.compute();
  if (palette_ && palette_->isVisible()) {
    if (palette_->isUserMoved()) palette_->reposition(nullptr);  // clamps where the user put it
    else if (const QRect r = l.rect(OverlayLayout::Slot::SketchPalette); r.isValid()) {
      palette_->move(r.topLeft());
      palette_->raise();
    }
  }
  // Fit / Home keep the model clear of the overlays (the viewer uses top and bottom today)
  viewer_->setViewportInsets(l.insets());
  viewer::OverlayHost::of(viewer_)->publish(l);
}

void MainWindow::clearTimelineFrameUnlessSelected() {
  // the timeline frame marks the feature picked or being edited; it goes with it (DOC-12)
  if (!timeline_ || !document_) return;
  const ObjectId framed = timeline_->strip()->highlightedFeature();
  if (framed == core::kInvalidId) return;
  for (const ShapeRef& r : document_->selection().items())
    if (r.owner == framed) return;
  timeline_->strip()->setHighlightedFeature(core::kInvalidId);
}

void MainWindow::onToolFinished(const std::string& id, bool accepted) {
  ++activation_;  // invalidate a pending dialog build
  editingFeature_ = core::kInvalidId;
  clearTimelineFrameUnlessSelected();
  dialog_->dismiss();
  // a pinned card stays for repeats: Apply re-runs the command (5.4); Cancel ends the pin
  if (!pinnedToolId_.empty()) {
    if (accepted && id == pinnedToolId_) {
      QTimer::singleShot(0, this, [this, id] {
        if (pinnedToolId_ == id && manager_ && (!manager_->isActive() || isIdleTool())) runCommand(id);
      });
    } else {
      pinnedToolId_.clear();
    }
  }
  layoutOverlays();
  acceptEnabled_ = true;
  scheduleStateRefresh();
  scheduleFailureReport();
  // Fusion: "New Drawing" from a design opens the drawing document (its tab here)
  if (accepted && id == "drawing.create" && active_ && active_->isDesign()) {
    QTimer::singleShot(0, this, [this] {
      if (active_ && active_->isDesign()) openDrawingTab(*active_);
    });
  }
}

void MainWindow::refreshToolDialog() {
  if (!manager_ || !manager_->isActive()) return;  // null while a tab is being torn down
  presentDialog();
}

void MainWindow::setToolAcceptEnabled(bool enabled) {
  acceptEnabled_ = enabled;
  if (dialog_) dialog_->setAcceptEnabled(enabled);
}

bool MainWindow::acceptActiveTool() {
  if (!manager_->isActive() || isIdleTool()) {
    showMessage("OK: no command is active", core::MessageLevel::Warning);
    return false;
  }
  if (!acceptEnabled_) {
    std::string why = "OK is disabled for " + manager_->activeTool()->info().name;
    showMessage(why, core::MessageLevel::Error);
    return false;
  }
  manager_->accept();
  return true;
}

void MainWindow::scheduleFailureReport() {
  if (failureReportPending_) return;
  failureReportPending_ = true;
  failureScheduleSeq_ = messageSeq_;
  QTimer::singleShot(0, this, [this] {
    failureReportPending_ = false;
    reportNewFailures(true);
  });
}

void MainWindow::reportNewFailures(bool announce) {
  if (!document_) return;
  // a running command recomputes its preview over and over: wait for its result (DOC-08)
  if (announce && manager_ && manager_->isActive() && !isIdleTool()) return;
  std::set<ObjectId> now;
  const core::Feature* first = nullptr;
  std::set<ObjectId>& before = failedFeatures_[document_];
  for (int i = 0; i < document_->featureCount(); ++i) {
    const core::Feature* f = document_->featureAt(i);
    if (!f || f->status() != core::FeatureStatus::Error) continue;
    now.insert(f->id());
    if (!first && !before.count(f->id())) first = f;
  }
  // the failure on display is fixed (undo, edit): its text goes too (DOC-08)
  if (failureDoc_ == document_ && failureFeature_ != core::kInvalidId && !now.count(failureFeature_)) {
    if (messageLabel_->text() == failureText_) clearStatusMessage();
    failureFeature_ = core::kInvalidId;
  }
  before = std::move(now);
  if (!announce || !first) return;
  std::string msg = first->name() + " failed";
  if (!first->statusMessage().empty()) msg += ": " + first->statusMessage();
  // the command that caused it (SUPPRESS, DELETE, an edit) reported its result just now: keep
  // that result in the cell and add the failure (p1-check 3)
  const bool recentResult = !lastResultText_.isEmpty() && lastResultSeq_ == messageSeq_ &&
                            messageLabel_->text() == lastResultText_ &&
                            (lastResultSeq_ > failureScheduleSeq_ || QDateTime::currentMSecsSinceEpoch() - lastResultMs_ < 1000);
  const QString result = lastResultText_;
  showMessage(msg, core::MessageLevel::Error);
  if (recentResult) setStatusMessage(result + QString::fromUtf8(" \u00B7 ") + core::qs(msg), core::MessageLevel::Warning);
  failureDoc_ = document_;
  failureFeature_ = first->id();
  failureText_ = messageLabel_->text();
}

void MainWindow::clearStatusMessage() {
  lastResultText_.clear();
  messageLabel_->clear();
  messageLabel_->hide();
  messageLabel_->setToolTip({});
  messageTimer_->stop();
  messageLevel_ = core::MessageLevel::Info;
}

void MainWindow::scheduleStateRefresh() {
  if (stateRefreshPending_) return;
  stateRefreshPending_ = true;
  QTimer::singleShot(0, this, [this] {
    stateRefreshPending_ = false;
    if (!active_) return;
    ribbon_->refreshStates();
    updateSketchStatus();
    updateLiveRow();
    updateUndoButtons();
    updateKeyHints();
    updateViewCells();
    if (dialog_ && dialog_->isVisible()) dialog_->syncControls();
    // decision 6: FEATURES expands while the docked card's feature-selection input is active
    if (cardSlotHost_)
      cardSlotHost_->setFeatureSelectionActive(dialog_ && dialog_->isDocked() && viewer_ && manager_->isActive() &&
                                               !isIdleTool() &&
                                               core::hasKind(viewer_->effectiveFilter(), core::ShapeKind::Feature));
  });
}

// =========================================================================================
// IAppHost
// =========================================================================================

void MainWindow::setPrompt(const std::string& text) {
  promptLabel_->setText(core::qs(text));
  commandLine_->setPrompt(core::qs(text));
}

void MainWindow::setStatusMessage(const QString& q, core::MessageLevel level) {
  QString shown = q.section('\n', 0, 0);
  // the cell has room for one line: the rest is in the command history (p1-check 2)
  const qsizetype more = q.count('\n');
  if (more > 0 && !shown.isEmpty())
    shown += QString::fromUtf8("  \u2026 +%1 line%2 (history)").arg(more).arg(more == 1 ? "" : "s");
  // coloured by the theme QSS (QLabel[messageLevel=...]); repolish so the new value applies
  messageLabel_->setProperty("messageLevel", level == core::MessageLevel::Error     ? "error"
                                             : level == core::MessageLevel::Warning ? "warning"
                                                                                    : "info");
  messageLabel_->style()->unpolish(messageLabel_);
  messageLabel_->style()->polish(messageLabel_);
  messageLabel_->setText(shown);
  messageLabel_->setVisible(!shown.isEmpty());
  messageLabel_->setCursor(more > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
  messageLevel_ = level;
  messageLabel_->setToolTip(more > 0 ? q + "\n\n(click: command history)" : q);
  // long output stays readable longer
  messageTimer_->start(more > 0 ? 15000 : level == core::MessageLevel::Info ? 5000 : 9000);
}

void MainWindow::showMessage(const std::string& text, core::MessageLevel level) {
  const QString q = core::qs(text);
  setStatusMessage(q, level);
  ++messageSeq_;
  if (level == core::MessageLevel::Info) {
    lastResultText_ = messageLabel_->text();
    lastResultSeq_ = messageSeq_;
    lastResultMs_ = QDateTime::currentMSecsSinceEpoch();
  }
  commandLine_->appendOutput(q);
  if (level == core::MessageLevel::Error) FC_ERROR("{}", text);
  else if (level == core::MessageLevel::Warning) FC_WARN("{}", text);
  emit messageShown(static_cast<int>(level), q);
}

bool MainWindow::askYesNo(const std::string& title, const std::string& question, bool defaultAnswer) {
  if (!interactive_) return defaultAnswer;
  const auto def = defaultAnswer ? QMessageBox::Yes : QMessageBox::No;
  return QMessageBox::question(this, core::qs(title), core::qs(question), QMessageBox::Yes | QMessageBox::No, def) ==
         QMessageBox::Yes;
}

void MainWindow::setWorkspace(core::Workspace ws) {
  if (!active_ || ws == workspace_) return;
  if (ws == core::Workspace::Drawing) {
    openDrawingTab(*active_);
  } else {
    // back to the design this drawing belongs to (reopen it when its tab was closed)
    DocumentTab* design = tabForDocument(*document_, DocumentTab::Kind::Design);
    if (!design) design = createTab(DocumentTab::Kind::Design, active_->documentPtr());
    activateTab(design);
  }
}

void MainWindow::setWorkspaceChoice(WorkspaceChoice c) {
  if (c == WorkspaceChoice::Drawing) {
    setWorkspace(core::Workspace::Drawing);
    updateWorkspaceSelector();
    return;
  }
  const DesignFilter f = c == WorkspaceChoice::Assembly ? DesignFilter::Assembly : DesignFilter::Part;
  UiSettings::instance().set("ui/designFilter", designFilterName(f));  // applyUiSetting rebuilds
  ribbon_->setDesignFilter(f);
  if (workspace_ != core::Workspace::Design) setWorkspace(core::Workspace::Design);
  updateWorkspaceSelector();
}

WorkspaceChoice MainWindow::workspaceChoice() const {
  if (workspace_ == core::Workspace::Drawing) return WorkspaceChoice::Drawing;
  return ribbon_ && ribbon_->designFilter() == DesignFilter::Assembly ? WorkspaceChoice::Assembly : WorkspaceChoice::Part;
}

void MainWindow::updateWorkspaceSelector() {
  if (workspaceSelector_ && ribbon_) workspaceSelector_->setChoice(workspaceChoice());
}

void MainWindow::setContextualTab(const std::string& tabName, bool active) {
  ribbon_->setContextualTab(tabName, active);
  scheduleStateRefresh();
}

void MainWindow::echoCommand(const std::string& text) {
  echo_.push_back(text);
  if (echo_.size() > 1000) echo_.erase(echo_.begin(), echo_.begin() + 500);
  commandLine_->appendHistory("> " + core::qs(text));
}

// =========================================================================================
// status bar
// =========================================================================================

void MainWindow::updateTitle() {
  if (!active_) return;
  const QString name = core::qs(document_->name());
  const bool mod = document_->isModified();
  setWindowTitle(QString("%1%2%3 - FabriCAD").arg(name, active_->isDesign() ? "" : " Drawing", mod ? " *" : ""));
  refreshTabBar();
  unitsLabel_->setText(core::lengthUnitSymbol(document_->units().length));
}

void MainWindow::updateUndoButtons() {
  if (!document_) return;
  const core::UndoStack& u = document_->undoStack();
  undoButton_->setEnabled(u.canUndo());
  redoButton_->setEnabled(u.canRedo());
  undoButton_->setToolTip(u.canUndo() ? QString("Undo %1 (Ctrl+Z)").arg(core::qs(u.undoText())) : "Undo (Ctrl+Z)");
  redoButton_->setToolTip(u.canRedo() ? QString("Redo %1 (Ctrl+Y)").arg(core::qs(u.redoText())) : "Redo (Ctrl+Y)");
}

void MainWindow::scheduleSelectionStatus() {
  if (selectionStatusPending_) return;
  selectionStatusPending_ = true;
  QTimer::singleShot(0, this, [this] {
    selectionStatusPending_ = false;
    updateSelectionStatus();
  });
}

void MainWindow::updateSelectionStatus() {
  if (!document_) return;
  selectionLabel_->setText(core::qs(describeSelection(*document_, document_->selection().items())));
}

void MainWindow::updateSketchStatus() {
  sketch::SketchSession* s = sketch::SketchSession::current();
  if (s != sketchSession_.data()) {
    QObject::disconnect(sketchConn_);
    sketchSession_ = s;
    if (s) sketchConn_ = connect(s, &sketch::SketchSession::changed, this, &MainWindow::updateSketchStatus);
  }
  if (palette_) palette_->sync();
  layoutOverlays();
  if (!s || s->manager() != manager_) {
    sketchLabel_->hide();
    return;
  }
  const std::string t = s->statusText();
  sketchLabel_->setText(core::qs(t));
  // fully constrained sketches read in the primary text colour, under-defined in sk.free, problems
  // in sk.conflict (theme QSS: QLabel[sketchState=...])
  const QString lower = core::qs(t).toLower();
  const char* state = lower.contains("over") || lower.contains("conflict") ? "conflict"
                      : lower.contains("fully")                             ? "full"
                                                                            : "free";
  if (sketchLabel_->property("sketchState").toString() != QLatin1StringView(state)) {
    sketchLabel_->setProperty("sketchState", state);
    sketchLabel_->style()->unpolish(sketchLabel_);
    sketchLabel_->style()->polish(sketchLabel_);
  }
  sketchLabel_->show();
}

QString MainWindow::promptText() const { return promptLabel_->text(); }
QString MainWindow::selectionText() const { return selectionLabel_->text(); }
QString MainWindow::sketchStatusText() const { return sketchLabel_->isHidden() ? QString() : sketchLabel_->text(); }
QString MainWindow::messageText() const { return messageLabel_->text(); }
QString MainWindow::statusCommandText() const { return statusCommand_->command(); }
QString MainWindow::keyHintsText() const { return keyHints_->text(); }

void MainWindow::updateKeyHints() {
  if (!keyHints_ || !manager_) return;
  const core::ITool* t = manager_->isActive() && !isIdleTool() ? manager_->activeTool() : nullptr;
  statusCommand_->setCommand(t ? core::qs(t->info().name) : QString());
  std::vector<KeyHints::Hint> hints;
  const bool sketching = viewer_ && viewer_->inSketchMode();
  if (t) {
    if (dialog_ && dialog_->isVisible()) {
      hints.push_back({"Return", "Apply"});
      hints.push_back({"Escape", "Cancel"});
      hints.push_back({"Tab", "Next field"});
    } else {
      hints.push_back({"Escape", "Cancel"});
    }
  } else if (sketching) {
    hints.push_back({"Ctrl+Return", "Finish sketch"});
    hints.push_back({"S", "Search"});
  } else {
    hints.push_back({"S", "Search"});
    hints.push_back({":", "Command"});
  }
  keyHints_->setHints(std::move(hints));
  applyResponsiveChrome();
}

void MainWindow::updateViewCells() {
  if (!gridCell_) return;
  const bool haveViewer = viewer_ != nullptr;
  gridCell_->setVisible(haveViewer);
  snapCell_->setVisible(haveViewer);
  if (!haveViewer || !document_) return;
  gridCell_->setOn(viewer_->isGridVisible());
  const QString spacing = core::qs(core::formatQuantity(core::Quantity::mm(viewer_->gridSpacing()), document_->units()));
  gridCell_->setValue(spacing);
  // the spacing adapts to the zoom: say so, it is not a setting (p1-check 12)
  gridCell_->setToolTip(QString("Layout grid, %1 at this zoom (the spacing follows the zoom). Click to show or hide").arg(spacing));
  bool snap = viewer_->isGridSnap();
  if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->manager() == manager_) snap = s->snapEnabled();
  snapCell_->setOn(snap);
}

// =========================================================================================
// file operations
// =========================================================================================

void MainWindow::storeUiState(DocumentTab& tab) {
  viewer::ViewerWidget* v = tab.viewer();
  if (!v) return;
  UiState s;
  s.hiddenFeatures = v->hiddenFeatures();
  s.showOrigin = v->isOriginVisible();
  s.showSketches = v->isFolderVisible("Sketches");
  s.showConstruction = v->isFolderVisible("Construction");
  s.showJoints = v->isFolderVisible("Joints");
  s.displayMode = v->displayMode();
  s.projection = v->projection();
  tab.document().setExtensionData("ui", s.toJson());
}

void MainWindow::applyUiState(DocumentTab& tab) {
  viewer::ViewerWidget* v = tab.viewer();
  if (!v) return;
  const UiState s = UiState::fromJson(tab.document().extensionData("ui"));
  v->setHiddenFeatures(s.hiddenFeatures);
  v->setOriginVisible(s.showOrigin);
  v->setFolderVisible("Sketches", s.showSketches);
  v->setFolderVisible("Construction", s.showConstruction);
  v->setFolderVisible("Joints", s.showJoints);
  v->setDisplayMode(s.displayMode);
  v->setProjection(s.projection);
}

void MainWindow::afterDocumentReplaced() {
  if (viewer_ && viewer_->inSketchMode()) viewer_->exitSketchMode();
  ribbon_->setContextualTab("SKETCH", false);
  updateTitle();
  updateUndoButtons();
  updateSelectionStatus();
  scheduleStateRefresh();
  viewer::ViewerWidget* v = viewer_;
  QTimer::singleShot(0, this, [this, v] {
    for (const auto& t : tabs_)
      if (t->viewer() == v && v) {
        v->syncNow();
        v->fitAll(false);
      }
  });
}

bool MainWindow::maybeSave(DocumentTab& tab) {
  core::Document& doc = tab.document();
  if (!doc.isModified()) return true;
  if (!interactive_) return true;
  const auto r = QMessageBox::warning(this, "FabriCAD", QString("Save changes to %1?").arg(core::qs(doc.name())),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (r == QMessageBox::Cancel) return false;
  if (r == QMessageBox::Save) {
    DocumentTab* was = active_;
    activateTab(&tab);
    const bool ok = save();
    if (was && indexOfTab(was) >= 0) activateTab(was);
    return ok;
  }
  return true;
}

bool MainWindow::maybeSave() { return active_ ? maybeSave(*active_) : true; }

bool MainWindow::newDocument() {
  // "Untitled", "Untitled 2", ...: the lowest number no open document uses (DOC-10)
  std::set<std::string> used;
  for (const auto& t : tabs_) used.insert(t->document().name());
  std::string name = "Untitled";
  for (int n = 2; used.count(name); ++n) name = "Untitled " + std::to_string(n);
  addDesignTab();
  if (document_->name() != name) {
    document_->setName(name);
    document_->setModified(false);
  }
  // G2 APP-03: Preferences > General > default units for new designs
  if (auto u = defaultUnitsPreference(); u && !(*u == document_->units())) {
    document_->setUnits(*u);
    document_->setModified(false);
  }
  if (viewer_) {
    viewer_->setStandardView(core::StandardView::Home, false);
    viewer_->setOriginVisible(false);
  }
  echoCommand("new");
  return true;
}

bool MainWindow::open() {
  if (!interactive_) return false;
  QSettings s;
  const QString dir = s.value("file/lastDir").toString();
  const QString path = QFileDialog::getOpenFileName(
      this, "Open", dir, "All supported (*.fcad *.step *.stp *.stl);;FabriCAD design (*.fcad);;STEP (*.step *.stp);;STL (*.stl)");
  if (path.isEmpty()) return false;
  s.setValue("file/lastDir", QFileInfo(path).absolutePath());
  QString err;
  if (!openFile(path, &err)) {
    QMessageBox::warning(this, "Open", QString("Cannot open %1:\n%2").arg(path, err));
    return false;
  }
  return true;
}

namespace {
// One spelling per file on disk (DOC-03): symlinks and "..", resolved; for a file that does not
// exist yet (Save As) the folder is resolved and the name appended.
QString canonicalPath(const QString& p) {
  if (p.isEmpty()) return p;
  const QFileInfo fi(p);
  if (const QString c = fi.canonicalFilePath(); !c.isEmpty()) return c;
  const QString dir = QFileInfo(fi.absolutePath()).canonicalFilePath();
  return dir.isEmpty() ? fi.absoluteFilePath() : dir + "/" + fi.fileName();
}

// Features that did not compute after a load (errors, unknown types), for a notice (DOC-07).
std::string failedFeatureSummary(const core::Document& doc) {
  std::vector<std::string> names;
  for (int i = 0; i < doc.featureCount(); ++i) {
    const core::Feature* f = doc.featureAt(i);
    if (f && f->status() == core::FeatureStatus::Error) names.push_back(f->name());
  }
  if (names.empty()) return {};
  std::string list;
  for (size_t i = 0; i < names.size() && i < 5; ++i) list += (i ? ", " : "") + names[i];
  if (names.size() > 5) list += std::format(" and {} more", names.size() - 5);
  return std::format("{} feature{} failed to compute: {}", names.size(), names.size() == 1 ? "" : "s", list);
}
}  // namespace

bool MainWindow::openFile(const QString& path, QString* error) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (!QFileInfo::exists(path)) {
    if (error) *error = "file not found";
    return false;
  }
  if (suffix == "step" || suffix == "stp" || suffix == "stl") {
    if (!registry_.has("part.import-file")) {
      if (error) *error = "import is not available";
      return false;
    }
    // Fusion: Open on a STEP/STL makes a new design; an empty start-up tab is used as is
    if (!active_ || !active_->isDesign()) addDesignTab();
    const bool ok = manager_->run("part.import-file", {path.toStdString()});
    if (ok) {
      echoCommand("import " + path.toStdString());
      viewer::ViewerWidget* v = viewer_;
      QTimer::singleShot(0, this, [this, v] {
        for (const auto& t : tabs_)
          if (t->viewer() == v && v) {
            v->syncNow();
            v->fitAll(true);
          }
      });
    }
    return ok;
  }
  // already open in another tab: just go there (Fusion keeps one tab per document)
  const QString canonical = canonicalPath(path);
  for (const auto& t : tabs_)
    if (t->isDesign() && !t->document().filePath().empty() && canonicalPath(core::qs(t->document().filePath())) == canonical) {
      activateTab(t.get());
      return true;
    }
  DocumentTab* created = nullptr;
  if (!active_ || !active_->isPristine()) created = addDesignTab();
  if (manager_->isActive()) manager_->cancel();
  clearRecoveryFile();
  std::string err;
  if (viewer_) viewer_->setHiddenFeatures({});
  if (!document_->load(canonical.toStdString(), &err)) {
    if (error) *error = core::qs(err);
    document_->clear();
    document_->setModified(false);
    afterDocumentReplaced();
    if (created && tabCount() > 1) closeTab(created);  // do not leave an empty tab behind
    return false;
  }
  if (isAutoDocumentName(document_->name())) document_->setName(QFileInfo(path).completeBaseName().toStdString());
  document_->setModified(false);
  recent_.add(canonical);
  recent_.save();
  afterDocumentReplaced();
  refreshTabBar();  // the tooltip shows the path now (DOC-11)
  reportNewFailures(false);
  echoCommand("open " + path.toStdString());
  if (const std::string failed = failedFeatureSummary(*document_); !failed.empty())
    showMessage("Opened " + QFileInfo(path).fileName().toStdString() + " with problems: " + failed, core::MessageLevel::Warning);
  return true;
}

bool MainWindow::save() {
  if (document_->filePath().empty()) return saveAs();
  QString err;
  if (!saveTo(core::qs(document_->filePath()), &err)) {
    if (interactive_) QMessageBox::warning(this, "Save", "Cannot save:\n" + err);
    return false;
  }
  return true;
}

bool MainWindow::saveAs() {
  if (!interactive_) return false;
  QSettings s;
  QString start = document_->filePath().empty()
                      ? QDir(s.value("file/lastDir").toString()).filePath(core::qs(document_->name()) + ".fcad")
                      : core::qs(document_->filePath());
  QString path = QFileDialog::getSaveFileName(this, "Save As", start, "FabriCAD design (*.fcad)");
  if (path.isEmpty()) return false;
  QString err;
  if (!saveTo(path, &err)) {
    QMessageBox::warning(this, "Save As", "Cannot save:\n" + err);
    return false;
  }
  s.setValue("file/lastDir", QFileInfo(path).absolutePath());  // only a folder that worked (DOC-04)
  return true;
}

bool MainWindow::saveTo(const QString& p, QString* error) {
  QString path = p;
  if (QFileInfo(path).suffix().isEmpty()) path += ".fcad";
  path = canonicalPath(path);
  // The document name follows the file (Fusion) unless the user gave it a name of its own: an
  // automatic name is "Untitled" or the stem of the file it came from (DOC-06).
  // The name is set before writing (the file stores it) and put back when the write fails, with
  // the modified flag the rename touched (DOC-04).
  const std::string oldName = document_->name();
  const bool wasModified = document_->isModified();
  const std::string& name = oldName;
  const std::string oldStem =
      document_->filePath().empty() ? std::string() : QFileInfo(core::qs(document_->filePath())).completeBaseName().toStdString();
  if (isAutoDocumentName(name) || (!oldStem.empty() && name == oldStem))
    document_->setName(QFileInfo(path).completeBaseName().toStdString());
  std::string err;
  if (!document_->save(path.toStdString(), &err)) {
    if (error) *error = core::qs(err);
    if (document_->name() != oldName) document_->setName(oldName);
    document_->setModified(wasModified);
    updateTitle();
    return false;
  }
  recent_.add(path);
  recent_.save();
  updateTitle();  // also refreshes the tab tooltips (path)
  clearRecoveryFile();
  showMessage("Saved " + path.toStdString(), core::MessageLevel::Info);
  return true;
}

void MainWindow::resizeEvent(QResizeEvent* e) {
  QMainWindow::resizeEvent(e);
  applyResponsiveChrome();
}

void MainWindow::applyResponsiveChrome() {
  // narrow windows drop the least important chrome first (status cells, then the search text);
  // the toolbar folds on its own (MainToolbar::fit)
  const int w = width();
  if (occtCell_) occtCell_->setVisible(w >= 1500);
  if (rebuildMeter_) rebuildMeter_->setVisible(w >= 1150);
  if (keyHints_) keyHints_->setVisible(w >= 1350 && !keyHints_->hints().empty());
  if (searchBox_) {
    const bool compact = w < 1100;
    searchBox_->setFixedWidth(compact ? 64 : 260);
    if (QWidget* t = searchBox_->findChild<QWidget*>("SearchBoxText")) t->setVisible(!compact);
  }
  if (commandLine_) commandLine_->setMinimumWidth(w < 1100 ? 200 : 300);
}

void MainWindow::closeEvent(QCloseEvent* e) {
  // one prompt per modified document (a design and its drawing share one)
  std::vector<const core::Document*> asked;
  for (const auto& t : tabs_) {
    if (std::find(asked.begin(), asked.end(), &t->document()) != asked.end()) continue;
    asked.push_back(&t->document());
    if (!maybeSave(*t)) {
      e->ignore();
      return;
    }
  }
  if (sketch::SketchSession::current()) sketch::SketchSession::end();
  for (const auto& t : tabs_)
    if (t->manager().isActive()) t->manager().cancel();
  QSettings s;
  s.setValue("window/geometry", saveGeometry());
  // a drawing tab hides the browser only for itself: store the design layout (DOC-02)
  if (active_ && !active_->isDesign() && browserDockWanted_) browserDock_->show();
  s.setValue("window/state", saveState(kLayoutVersion));
  // the strip dock is hidden in the feature-list style: its own wish is stored apart
  s.setValue("window/stripVisible", timelineDockWanted_);
  sidebar_->saveState();
  s.setValue("search/recent", recentCommands_);
  UiSettings::instance().set("ui/commandLine", commandLine_->isVisible());
  recent_.save();
  for (const auto& t : tabs_) clearRecoveryFile(*t);  // a clean close leaves nothing to recover
  releaseSessionRecoveryDirectory();
  e->accept();
}

// =========================================================================================
// commands
// =========================================================================================

void MainWindow::editFeature(ObjectId id) {
  const core::Feature* f = document_->feature(id);
  if (!f) return;
  const EditCommand cmd = editCommandFor(registry_, *f);
  editingFeature_ = id;  // the FEATURES list shows it as the live row while the edit runs
  if (!cmd.valid() || !manager_->run(cmd.toolId, cmd.args)) {
    editingFeature_ = core::kInvalidId;
    showMessage("'" + f->name() + "' cannot be edited", core::MessageLevel::Warning);
  }
  scheduleStateRefresh();
}

void MainWindow::deleteRefs(const std::vector<ShapeRef>& refs) {
  const DeletePlan plan = planDelete(*document_, refs);
  if (plan.isEmpty()) {
    if (!refs.empty()) showMessage("Nothing to delete in the selection", core::MessageLevel::Info);
    return;
  }
  if (!plan.bodyNotes.empty() || !plan.dependents.empty()) {
    std::string q = "Delete " + plan.summary(*document_) + "?";
    if (!plan.bodyNotes.empty()) {
      q += "\n\nDeleting a body removes the feature that created it:";
      for (const std::string& n : plan.bodyNotes) q += "\n  " + n;
    }
    if (!plan.dependents.empty()) {
      q += "\n\nThese later features use it and may fail:";
      for (size_t i = 0; i < plan.dependents.size() && i < 8; ++i) q += "\n  " + plan.dependents[i];
      if (plan.dependents.size() > 8) q += std::format("\n  and {} more", plan.dependents.size() - 8);
    }
    if (!askYesNo("Delete", q, true)) return;
  }
  for (ObjectId id : plan.features) document_->selection().removeOwner(id);
  // ASM-02: a deleted component's bodies are kept (moved to the parent): say so
  int keptBodies = 0;
  for (ObjectId c : plan.components)
    for (ObjectId b : document_->bodiesOf(c))
      if (!document_->bodyShape(b).IsNull()) ++keptBodies;
  if (auto cmd = makeDeleteCommand(*document_, plan)) {
    const std::string summary = plan.summary(*document_);
    cmd->setText("Delete " + summary);
    document_->selection().clear();
    document_->undoStack().push(std::move(cmd));
    if (keptBodies > 0)
      showMessage(std::format("Deleted {}; kept {} bod{} (moved to the parent component)", summary, keptBodies,
                              keptBodies == 1 ? "y" : "ies"),
                  core::MessageLevel::Warning);
  }
}

void MainWindow::deleteSelection() {
  if (sketch::SketchSession* s = sketch::SketchSession::current(); s && viewer_ && viewer_->inSketchMode()) {
    if (s->deleteSelection()) return;
  }
  deleteRefs(document_->selection().items());
}

void MainWindow::toggleSelectionVisibility() {
  if (auto cmd = toggleVisibility(*document_, viewer_, document_->selection().items()))
    document_->undoStack().push(std::move(cmd));
}

void MainWindow::showAllObjects() {
  if (auto cmd = showAll(*document_, viewer_)) document_->undoStack().push(std::move(cmd));
}

void MainWindow::showParameters() {
  auto* dlg = findChild<ParametersDialog*>();
  if (!dlg) {
    dlg = new ParametersDialog(*document_, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
  }
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

void MainWindow::openCommandSearch() {
  search_->setRecent(recentCommands_);
  // under the title-bar search box, inside the window (p1-check 5); the cursor without one
  if (searchBox_ && searchBox_->isVisible()) {
    const QRect anchor(searchBox_->mapToGlobal(QPoint(0, 0)), searchBox_->size());
    search_->popupBelow(anchor, QRect(mapToGlobal(QPoint(0, 0)), size()));
  } else {
    search_->popup(QCursor::pos());
  }
}

void MainWindow::toggleCommandLine() {
  commandLine_->setVisible(!commandLine_->isVisible());
  if (commandLine_->isVisible()) commandLine_->edit()->setFocus();
}

void MainWindow::focusCommandLine() {
  commandLine_->show();
  commandLine_->focusForTyping();  // G2 APP-07: drops the ':' that triggered this
}

void MainWindow::showShortcutsHelp() {
  auto* dlg = new QDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle("Keyboard Shortcuts");
  dlg->resize(700, 520);
  auto* v = new QVBoxLayout(dlg);
  auto* tree = new QTreeWidget(dlg);
  tree->setColumnCount(3);
  tree->setHeaderLabels({"Command", "Shortcut", "Aliases"});
  tree->setRootIsDecorated(false);
  tree->setUniformRowHeights(true);
  tree->setAlternatingRowColors(true);
  tree->setIconSize(QSize(16, 16));
  // rows without a tool icon get a blank one so every name starts in the same column
  QPixmap blank(16, 16);
  blank.fill(Qt::transparent);
  const QIcon blankIcon(blank);
  for (core::ITool* t : registry_.tools()) {
    const core::ToolInfo& i = t->info();
    if (i.hidden && i.shortcut.empty()) continue;
    if (i.shortcut.empty() && i.aliases.empty()) continue;
    QStringList aliases;
    for (const auto& a : i.aliases) aliases << core::qs(a);
    auto* it = new QTreeWidgetItem(tree, {core::qs(i.name), core::qs(i.shortcut), aliases.join(", ")});
    const QIcon icon = toolIcon(i);
    it->setIcon(0, icon.isNull() ? blankIcon : icon);
  }
  for (const auto& [k, d] : std::initializer_list<std::pair<const char*, const char*>>{
           {"S", "Command search"}, {"Esc", "Cancel command / clear selection"},
           {"Enter / Space", "Repeat last command (OK while a command is active)"},
           {":", "Focus the command line"}, {"Middle mouse", "Pan (Fusion preset)"},
           {"Shift + middle mouse", "Orbit"}, {"Wheel", "Zoom at cursor"}, {"Right click", "Marking menu"}})
    (new QTreeWidgetItem(tree, {d, k, ""}))->setIcon(0, blankIcon);
  tree->sortItems(0, Qt::AscendingOrder);
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  v->addWidget(tree);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
  connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
  v->addWidget(bb);
  dlg->show();
}

void MainWindow::runPseudoCommand(const std::string& cmd) {
  if (cmd == kCmdOk) {
    if (acceptEnabled_) manager_->accept();
  } else if (cmd == kCmdCancel) {
    manager_->cancel();
  } else if (cmd == kCmdRepeat) {
    manager_->repeatLast();
  } else if (!cmd.empty()) {
    runCommand(cmd);
  }
}

void MainWindow::addDirectAction(const std::string& id, std::function<void()> fn) { directActions_[id] = std::move(fn); }

bool MainWindow::runCommand(const std::string& id, std::vector<std::string> args) {
  if (auto it = directActions_.find(id); it != directActions_.end() && args.empty() && manager_->isActive()) {
    echoCommand(id);
    it->second();
    return true;
  }
  return manager_->run(id, std::move(args));
}

namespace {
MarkingContext markingContext(core::ToolManager& m, core::Document& doc, bool idle, bool inSketch) {
  MarkingContext c;
  c.toolActive = m.isActive() && !idle;
  if (c.toolActive) c.activeToolName = m.activeTool()->info().name;
  c.lastToolId = m.lastToolId();
  c.lastModelToolId = m.lastModelToolId();
  c.inSketch = inSketch;
  for (const ShapeRef& r : doc.selection().items()) c.selectionKinds.push_back(r.kind);
  return c;
}
}  // namespace

void MainWindow::showMarkingMenu(const QPoint& globalPos) {
  const MarkingMenuModel model =
      buildMarkingMenu(registry_, markingContext(*manager_, *document_, isIdleTool(), viewer_ && viewer_->inSketchMode()));
  if (model.isEmpty()) return;
  marking_->popup(model, globalPos);
}

void MainWindow::markingGesture(const QPoint& press, const QPoint& release) {
  const MarkingMenuModel model =
      buildMarkingMenu(registry_, markingContext(*manager_, *document_, isIdleTool(), viewer_ && viewer_->inSketchMode()));
  const QPoint d = release - press;
  const int slot = slotForOffset(d.x(), d.y(), 15.0);
  if (slot < 0) return;
  const std::string& cmd = model.radial[static_cast<size_t>(slot)].command;
  if (!cmd.empty()) runPseudoCommand(cmd);
}

void MainWindow::viewCubeMenu(const QPoint& globalPos) {
  if (!viewer_) return;
  QMenu m(this);
  m.addAction(resourceIcon("home"), "Go Home", this, [this] { viewer_->setStandardView(core::StandardView::Home, true); });
  m.addSeparator();
  QAction* ortho = m.addAction("Orthographic", this, [this] { viewer_->setProjection(core::ProjectionMode::Orthographic); });
  QAction* persp = m.addAction("Perspective", this, [this] { viewer_->setProjection(core::ProjectionMode::Perspective); });
  ortho->setCheckable(true);
  persp->setCheckable(true);
  ortho->setChecked(viewer_->projection() == core::ProjectionMode::Orthographic);
  persp->setChecked(!ortho->isChecked());
  m.addSeparator();
  m.addAction("Set Current View as Home", this, [this] { viewer_->setCurrentViewAsHome(); });
  m.addAction("Reset Home", this, [this] { viewer_->resetHome(); });
  m.exec(globalPos);
}

// =========================================================================================
// keyboard routing
// =========================================================================================

bool MainWindow::isCanvasFocus(QWidget* focus) const {
  if (!focus || focus == this) return true;
  if (dialog_ && (focus == dialog_ || dialog_->isAncestorOf(focus))) return false;
  if (viewer_ && (focus == viewer_ || viewer_->isAncestorOf(focus))) return true;
  if (browser_ && (focus == browser_ || browser_->isAncestorOf(focus))) return !isTextInput(focus);
  if (featureList_ && (focus == featureList_ || featureList_->isAncestorOf(focus))) return !isTextInput(focus);
  if (timeline_ && (focus == timeline_ || timeline_->isAncestorOf(focus))) return true;
  if (QWidget* page = active_ ? active_->drawingPage() : nullptr; page && (focus == page || page->isAncestorOf(focus)))
    return !isTextInput(focus);
  return false;
}

bool MainWindow::handleKeyPress(int key, int modifiers, const QString& text, bool autoRepeat, QWidget* focus) {
  // the FEATURES list keeps its own keys (arrows, F2, Space = suppress, Enter = edit, Del)
  if (featureList_ && focus && (focus == featureList_ || featureList_->isAncestorOf(focus)) && !isTextInput(focus) &&
      FeatureListWidget::handlesKey(key, modifiers))
    return false;
  KeyContext kc;
  kc.canvasFocus = isCanvasFocus(focus);
  kc.textInput = isTextInput(focus);
  kc.toolActive = manager_->isActive();
  const KeyRoute route = routeKey(key, modifiers, kc);
  if (route.action == KeyAction::None) return false;

  if (route.toolFirst && manager_->isActive()) {
    core::ViewerKeyEvent ev;
    ev.key = key;
    ev.modifiers = coreModifiers(modifiers);
    ev.text = text.toStdString();
    ev.autoRepeat = autoRepeat;
    if (manager_->keyPress(ev)) return true;
  }
  const bool idle = isIdleTool();
  switch (route.action) {
    case KeyAction::None: return false;
    case KeyAction::ForwardOnly: return true;
    case KeyAction::Cancel:
      if (viewer_ && viewer_->leftButtonNavigation() != viewer::NavAction::None) {
        viewer_->setLeftButtonNavigation(viewer::NavAction::None);
        if (navBar_) navBar_->refresh();
        return true;
      }
      if (manager_->isActive() && !idle) manager_->cancel();
      else document_->selection().clear();
      return true;
    case KeyAction::AcceptOrRepeat:
      if (manager_->isActive() && !idle) {
        if (acceptEnabled_) manager_->accept();
      } else {
        manager_->repeatLast();
      }
      return true;
    case KeyAction::Repeat:
      if (!manager_->isActive() || idle) manager_->repeatLast();
      return true;
    case KeyAction::OpenSearch: openCommandSearch(); return true;
    case KeyAction::FocusCommandLine: focusCommandLine(); return true;
    case KeyAction::Shortcut: {
      if (autoRepeat) return false;
      // match on normalised text so "Delete"/"Del", "ctrl+z"/"Ctrl+Z" are equivalent
      for (core::ITool* t : registry_.tools()) {
        const std::string& sc = t->info().shortcut;
        if (!sc.empty() && normalizeShortcut(sc) == route.sequence) {
          if (manager_->isActive() && directActions_.count(t->info().id) && t->isEnabled(manager_->makeContext()))
            return runCommand(t->info().id);
          if (manager_->triggerShortcut(sc)) return true;
          break;
        }
      }
      return false;
    }
  }
  return false;
}

namespace {
// The open application menus (File / Help and their submenus), innermost last.
std::vector<QMenu*> openMenus() {
  std::vector<QMenu*> out;
  for (QWidget* w : QApplication::topLevelWidgets())
    if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible()) out.push_back(m);
  return out;
}

QMenu* menuOf(QObject* o) {
  for (auto* w = qobject_cast<QWidget*>(o); w; w = w->parentWidget())
    if (auto* m = qobject_cast<QMenu*>(w)) return m;
  return nullptr;
}

bool insideMenu(QObject* o) { return menuOf(o) != nullptr; }

// The window a menu was opened from (through its parent menus and the button/widget owning it).
QWidget* menuOwnerWindow(QMenu* m) {
  QWidget* w = m;
  while (auto* menu = qobject_cast<QMenu*>(w)) {
    w = menu->parentWidget();
    if (!w) return nullptr;
    if (!qobject_cast<QMenu*>(w)) return w->window();
  }
  return w ? w->window() : nullptr;
}

void closeMenus(const std::vector<QMenu*>& menus) {
  for (QMenu* m : menus) m->close();
}
}  // namespace

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
  if (o == messageLabel_ && e->type() == QEvent::MouseButtonRelease &&
      static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton && commandLine_ && commandLine_->isVisible()) {
    commandLine_->showHistoryPane();
    return true;
  }
  if (o == tabBar_) {
    if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton)
      tabDragActive_ = true;
    else if (e->type() == QEvent::MouseButtonRelease && tabDragActive_) {
      tabDragActive_ = false;
      if (tabBarRefreshDeferred_) {
        tabBarRefreshDeferred_ = false;
        scheduleTabBarRefresh();
      }
    }
  }
  if (viewer_ && o == viewer_ && e->type() == QEvent::Resize) QTimer::singleShot(0, this, &MainWindow::layoutOverlays);
  // DOC-12: without a pointer/keyboard grab (XWayland, no window manager) a click on the main
  // window or a key typed into it does not reach the open menu, so the menu would stay up and
  // keep acting as the active popup. A click outside every menu closes them; with a menu open,
  // Escape just closes it and any other key closes it before it is handled as usual.
  // Ctrl/Alt shortcuts typed while one of this window's menus is open (the menu has the keyboard)
  // would be swallowed: their menu entries only show the key as text. Close the menus and run
  // the shortcut as if typed on the canvas (DOC-12).
  if (e->type() == QEvent::KeyPress && active_) {
    auto* k = static_cast<QKeyEvent*>(e);
    QMenu* menu = menuOf(o);
    if (menu && menuOwnerWindow(menu) == this && !k->isAutoRepeat() &&
        (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
        !(k == lastKeyEvent_ && k->timestamp() == lastKeyTimestamp_)) {
      lastKeyEvent_ = k;
      lastKeyTimestamp_ = k->timestamp();
      closeMenus(openMenus());
      QWidget* focus = viewer_ ? static_cast<QWidget*>(viewer_) : this;
      handleKeyPress(k->key(), static_cast<int>(k->modifiers()), k->text(), false, focus);
      return true;
    }
  }
  if ((e->type() == QEvent::MouseButtonPress || e->type() == QEvent::KeyPress) && o->isWidgetType() && !insideMenu(o)) {
    auto* w = static_cast<QWidget*>(o);
    if (w->window() == this) {
      if (const std::vector<QMenu*> menus = openMenus(); !menus.empty()) {
        closeMenus(menus);
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) return true;
      }
    }
  }
  if (e->type() == QEvent::KeyPress && active_) {
    auto* w = qobject_cast<QWidget*>(o);
    auto* k = static_cast<QKeyEvent*>(e);
    // only keys delivered inside this window (not popups/dialogs); each event once
    if (w && w->window() == this && !(k == lastKeyEvent_ && k->timestamp() == lastKeyTimestamp_)) {
      lastKeyEvent_ = k;
      lastKeyTimestamp_ = k->timestamp();
      // No active window (e.g. no window manager): focusWidget() is null although the key is
      // aimed at a field. Use the receiver then, so a key typed into a dialog field never
      // counts as a canvas key (UX-33).
      QWidget* focus = QApplication::focusWidget();
      if (!focus) focus = w->focusWidget() ? w->focusWidget() : w;
      if (handleKeyPress(k->key(), static_cast<int>(k->modifiers()), k->text(), k->isAutoRepeat(), focus)) {
        k->accept();
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(o, e);
}

}  // namespace fabricad::app
