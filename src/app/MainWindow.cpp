// FabriCAD application shell.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainWindow.h"

#include "app/AppActions.h"
#include "app/AppTools.h"
#include "app/BrowserTree.h"
#include "app/CommandDialog.h"
#include "app/CommandLine.h"
#include "app/CommandSearch.h"
#include "app/Icons.h"
#include "app/MarkingMenu.h"
#include "app/MarkingMenuModel.h"
#include "app/NavBar.h"
#include "app/Ribbon.h"
#include "app/ScriptTools.h"
#include "app/ShortcutRouter.h"
#include "app/SketchPalette.h"
#include "app/TimelineWidget.h"
#include "app/ParametersDialog.h"
#include "app/PreferencesDialog.h"
#include "assembly/AssemblyModule.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "drawing/DrawingModule.h"
#include "part/PartModule.h"
#include "sketch/SketchModule.h"
#include "sketch/SketchSession.h"
#include "viewer/ViewerWidget.h"

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

  QSettings s;
  if (s.contains("window/geometry")) restoreGeometry(s.value("window/geometry").toByteArray());
  if (s.contains("window/state")) restoreState(s.value("window/state").toByteArray());
  browserDockWanted_ = !browserDock_->isHidden();
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
  // --- top: application bar + ribbon (menu widget area) -------------------------------
  auto* top = new QWidget(this);
  auto* topLayout = new QVBoxLayout(top);
  topLayout->setContentsMargins(0, 0, 0, 0);
  topLayout->setSpacing(0);
  topLayout->addWidget(buildAppBar());
  setMenuWidget(top);

  // --- central: one page per tab (viewer or drawing workspace) --------------------------
  central_ = new QStackedWidget(this);
  setCentralWidget(central_);

  // --- browser (left dock) --------------------------------------------------------------------
  browserDock_ = new QDockWidget("BROWSER", this);
  browserDock_->setObjectName("BrowserDock");
  browserDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
  browserStack_ = new QStackedWidget(browserDock_);
  browserDock_->setWidget(browserStack_);
  addDockWidget(Qt::LeftDockWidgetArea, browserDock_);
  resizeDocks({browserDock_}, {260}, Qt::Horizontal);

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
  ribbon_ = new Ribbon(m, top);
  topLayout->addWidget(ribbon_);
  commandLine_ = new CommandLine(m, statusBar());
  commandLine_->setVisible(QSettings().value("ui/commandLine", true).toBool());
  statusBar()->addPermanentWidget(commandLine_, 1);
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
  auto* h = new QHBoxLayout(bar);
  h->setContentsMargins(6, 2, 6, 2);
  h->setSpacing(2);

  auto* fileButton = appBarButton(bar, "file-menu", "File");
  fileButton->setPopupMode(QToolButton::InstantPopup);
  auto* fileMenu = new QMenu(fileButton);
  buildFileMenu(fileMenu);
  fileButton->setMenu(fileMenu);
  h->addWidget(fileButton);
  auto* saveButton = appBarButton(bar, "save", "Save (Ctrl+S)");
  connect(saveButton, &QToolButton::clicked, this, [this] { save(); });
  h->addWidget(saveButton);
  undoButton_ = appBarButton(bar, "undo", "Undo (Ctrl+Z)");
  connect(undoButton_, &QToolButton::clicked, this, [this] { manager_->run("app.undo"); });
  h->addWidget(undoButton_);
  redoButton_ = appBarButton(bar, "redo", "Redo (Ctrl+Y)");
  connect(redoButton_, &QToolButton::clicked, this, [this] { manager_->run("app.redo"); });
  h->addWidget(redoButton_);
  h->addSpacing(16);
  // document tabs (Fusion: one tab per open design / drawing, "+" for a new design)
  tabBar_ = new QTabBar(bar);
  tabBar_->setObjectName("DocumentTabs");
  tabBar_->setTabsClosable(true);
  tabBar_->setMovable(true);
  tabBar_->setExpanding(false);
  tabBar_->setDocumentMode(true);
  tabBar_->setDrawBase(false);
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
  h->addWidget(tabBar_, 1);
  auto* newTab = appBarButton(bar, "new", "New Design (Ctrl+N)");
  newTab->setObjectName("NewTabButton");
  connect(newTab, &QToolButton::clicked, this, [this] { manager_->run("app.new"); });
  h->addWidget(newTab);
  h->addStretch(1);
  auto* searchButton = appBarButton(bar, "search", "Search commands (S)");
  connect(searchButton, &QToolButton::clicked, this, [this] { openCommandSearch(); });
  h->addWidget(searchButton);
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
    timelineToggle->setChecked(timelineDockWanted_);
    timelineToggle->setEnabled(design);
  });
  connect(browserToggle, &QAction::triggered, this, [this](bool on) { setBrowserVisible(on); });
  connect(timelineToggle, &QAction::triggered, this, [this](bool on) {
    timelineDockWanted_ = on;
    timelineDock_->setVisible(on && active_ && active_->isDesign());
  });
  m->addSeparator();
  m->addAction("Exit", this, [this] { close(); });
}

void MainWindow::buildStatusBar() {
  QStatusBar* sb = statusBar();
  sb->setSizeGripEnabled(false);
  promptLabel_ = new QLabel(sb);
  promptLabel_->setObjectName("StatusPrompt");
  messageLabel_ = new QLabel(sb);
  selectionLabel_ = new QLabel(sb);
  selectionLabel_->setObjectName("StatusDim");
  sketchLabel_ = new QLabel(sb);
  sketchLabel_->hide();
  unitsLabel_ = new QLabel(sb);
  unitsLabel_->setObjectName("StatusDim");
  // G2 APP-03: clicking the units opens Preferences > Document (UNITS command)
  unitsLabel_->setCursor(Qt::PointingHandCursor);
  unitsLabel_->setToolTip("Document units (click to change)");
  installClickHandler(unitsLabel_, [this] { runCommand("app.units"); });
  sb->addWidget(promptLabel_, 2);
  sb->addWidget(messageLabel_, 2);
  sb->addPermanentWidget(selectionLabel_);
  sb->addPermanentWidget(sketchLabel_);
  sb->addPermanentWidget(unitsLabel_);
  messageTimer_ = new QTimer(this);
  messageTimer_->setSingleShot(true);
  connect(messageTimer_, &QTimer::timeout, this, [this] { messageLabel_->clear(); });
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
  DocumentTab::Hosts hosts{central_, browserStack_, timelineStack_};
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
  timelineDock_->setVisible(tab->isDesign() && timelineDockWanted_);
  if (tab->isDesign() && glEnabled_ && isVisible()) tab->initializeViewer();

  const core::Workspace ws = tab->workspace();
  const bool workspaceChanged = ws != workspace_ || !previous;
  workspace_ = ws;
  if (workspaceChanged) {
    ribbon_->rebuild(ws);
    if (previous) echoCommand(ws == core::Workspace::Design ? "workspace DESIGN" : "workspace DRAWING");
  }
  ribbon_->setContextualTab("SKETCH", false);
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
}

void MainWindow::syncActivePointers() {
  document_ = active_ ? &active_->document() : nullptr;
  manager_ = active_ ? &active_->manager() : nullptr;
  viewer_ = active_ ? active_->viewer() : nullptr;
  browser_ = active_ ? active_->browser() : nullptr;
  timeline_ = active_ ? active_->timeline() : nullptr;
  dialog_ = active_ ? active_->dialog() : nullptr;
  navBar_ = active_ ? active_->navBar() : nullptr;
  palette_ = active_ ? active_->palette() : nullptr;
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
  connect(ribbon_, &Ribbon::toolTriggered, this, [this](const std::string& id) { runCommand(id); });
  connect(ribbon_, &Ribbon::workspaceRequested, this, [this](core::Workspace ws) { setWorkspace(ws); });
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
    if (tab == active_) updateTitle();
  });
  connect(d, &core::Document::unitsChanged, this, whenActive([this] { updateTitle(); }));
  connect(d, &core::Document::recomputeFinished, this, whenActive([this] {
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
      if (timeline_) timeline_->scrollToFeature(id);
    });
    connect(browser, &BrowserTree::suppressRequested, this, [this](ObjectId id, bool s) {
      if (auto c = makeSuppress(*document_, id, s)) document_->undoStack().push(std::move(c));
    });
  }
  if (TimelineWidget* timeline = tab->timeline()) {
    TimelineStrip* strip = timeline->strip();
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
  dialog_->present(core::qs(tool->info().name), toolIcon(tool->info()), body, tool->wantsDialogButtons());
  dialog_->setAcceptEnabled(keep);
  layoutOverlays();
}

void MainWindow::layoutOverlays() {
  if (palette_ && palette_->isVisible()) palette_->reposition(dialog_);
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
  clearTimelineFrameUnlessSelected();
  dialog_->dismiss();
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
  showMessage(msg, core::MessageLevel::Error);
  failureDoc_ = document_;
  failureFeature_ = first->id();
  failureText_ = messageLabel_->text();
}

void MainWindow::clearStatusMessage() {
  messageLabel_->clear();
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
    updateUndoButtons();
  });
}

// =========================================================================================
// IAppHost
// =========================================================================================

void MainWindow::setPrompt(const std::string& text) {
  promptLabel_->setText(core::qs(text));
  commandLine_->setPrompt(core::qs(text));
}

void MainWindow::showMessage(const std::string& text, core::MessageLevel level) {
  const char* color = level == core::MessageLevel::Error ? "#D93025" : level == core::MessageLevel::Warning ? "#B07C00" : "#2B2E33";
  const QString q = core::qs(text);
  const QString firstLine = q.section('\n', 0, 0);
  messageLabel_->setStyleSheet(QString("color: %1;").arg(color));
  messageLabel_->setText(firstLine);
  messageLevel_ = level;
  messageLabel_->setToolTip(q);
  messageTimer_->start(level == core::MessageLevel::Info ? 5000 : 9000);
  commandLine_->appendHistory(q);
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
  // fully constrained sketches read black, under-defined blue, problems red (Fusion colours)
  const QString lower = core::qs(t).toLower();
  const char* color = lower.contains("over") || lower.contains("conflict") ? "#D93025"
                      : lower.contains("fully")                             ? "#2B2E33"
                                                                            : "#1E6FD9";
  sketchLabel_->setStyleSheet(QString("color: %1; font-weight: 600;").arg(color));
  sketchLabel_->show();
}

QString MainWindow::promptText() const { return promptLabel_->text(); }
QString MainWindow::selectionText() const { return selectionLabel_->text(); }
QString MainWindow::sketchStatusText() const { return sketchLabel_->isHidden() ? QString() : sketchLabel_->text(); }
QString MainWindow::messageText() const { return messageLabel_->text(); }

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
  s.setValue("window/state", saveState());
  s.setValue("search/recent", recentCommands_);
  s.setValue("ui/commandLine", commandLine_->isVisible());
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
  if (!cmd.valid() || !manager_->run(cmd.toolId, cmd.args))
    showMessage("'" + f->name() + "' cannot be edited", core::MessageLevel::Warning);
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
  search_->popup(QCursor::pos());
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
  if (timeline_ && (focus == timeline_ || timeline_->isAncestorOf(focus))) return true;
  if (QWidget* page = active_ ? active_->drawingPage() : nullptr; page && (focus == page || page->isAncestorOf(focus)))
    return !isTextInput(focus);
  return false;
}

bool MainWindow::handleKeyPress(int key, int modifiers, const QString& text, bool autoRepeat, QWidget* focus) {
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
