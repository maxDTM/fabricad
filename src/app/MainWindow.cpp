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
#include "app/ShortcutRouter.h"
#include "app/SketchPalette.h"
#include "app/TimelineWidget.h"
#include "app/ParametersDialog.h"
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
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QDateTime>
#include <QJsonDocument>
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      document_(std::make_unique<core::Document>()),
      manager_(std::make_unique<core::ToolManager>(registry_)) {
  setWindowIcon(resourceIcon("fabricad"));
  setObjectName("FabriCADMainWindow");
  resize(1400, 860);

  registerModules();
  buildUi();

  manager_->setDocument(document_.get());
  manager_->setViewer(viewer_);
  manager_->setHost(this);

  connectSignals();
  ribbon_->rebuild(workspace_);
  commandLine_->refreshCompletions();
  recent_.load();
  recentCommands_ = QSettings().value("search/recent").toStringList();
  navBar_->applySettings();
  updateTitle();
  updateUndoButtons();
  updateSelectionStatus();
  qApp->installEventFilter(this);

  QSettings s;
  if (s.contains("window/geometry")) restoreGeometry(s.value("window/geometry").toByteArray());
  if (s.contains("window/state")) restoreState(s.value("window/state").toByteArray());
}

MainWindow::~MainWindow() {
  qApp->removeEventFilter(this);
  if (manager_ && manager_->isActive()) manager_->cancel();
  // the viewer is a child widget: detach it from the manager before widgets go away
  if (manager_) manager_->setViewer(nullptr);
}

void MainWindow::registerModules() {
  core::ModuleContext ctx{core::FeatureFactory::instance(), registry_};
  sketch::registerModule(ctx);
  part::registerModule(ctx);
  assembly::registerModule(ctx);
  drawing::registerModule(ctx);
  registerAppTools(*this, registry_);
}

void MainWindow::buildUi() {
  // --- top: application bar + ribbon (menu widget area) -------------------------------
  auto* top = new QWidget(this);
  auto* topLayout = new QVBoxLayout(top);
  topLayout->setContentsMargins(0, 0, 0, 0);
  topLayout->setSpacing(0);
  topLayout->addWidget(buildAppBar());
  ribbon_ = new Ribbon(*manager_, top);
  topLayout->addWidget(ribbon_);
  setMenuWidget(top);

  // --- central: viewport / drawing workspace --------------------------------------------
  central_ = new QStackedWidget(this);
  viewer_ = new viewer::ViewerWidget(central_);
  viewer_->setDocument(document_.get());
  viewer_->setToolManager(manager_.get());
  central_->addWidget(viewer_);
  setCentralWidget(central_);
  dialog_ = new CommandDialog(viewer_->overlayParent());
  navBar_ = new NavBar(*viewer_, *manager_);
  palette_ = new SketchPalette(*viewer_, *manager_);

  // --- browser (left dock) --------------------------------------------------------------------
  browserDock_ = new QDockWidget("BROWSER", this);
  browserDock_->setObjectName("BrowserDock");
  browserDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
  browser_ = new BrowserTree(*document_, viewer_, browserDock_);
  browserDock_->setWidget(browser_);
  addDockWidget(Qt::LeftDockWidgetArea, browserDock_);
  resizeDocks({browserDock_}, {260}, Qt::Horizontal);

  // --- timeline (bottom dock spanning the full width) -------------------------------------------
  timelineDock_ = new QDockWidget("TIMELINE", this);
  timelineDock_->setObjectName("TimelineDock");
  timelineDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
  timelineDock_->setTitleBarWidget(new QWidget(timelineDock_));
  timeline_ = new TimelineWidget(*document_, timelineDock_);
  timelineDock_->setWidget(timeline_);
  addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);
  setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  buildStatusBar();
  search_ = new CommandSearch(*manager_, this);
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
  docTab_ = new QLabel(bar);
  docTab_->setObjectName("DocumentTab");
  h->addWidget(docTab_);
  h->addStretch(1);
  auto* searchButton = appBarButton(bar, "search", "Search commands (S)");
  connect(searchButton, &QToolButton::clicked, this, [this] { openCommandSearch(); });
  h->addWidget(searchButton);
  auto* help = appBarButton(bar, "help", "Help");
  help->setPopupMode(QToolButton::InstantPopup);
  auto* helpMenu = new QMenu(help);
  helpMenu->addAction("Keyboard Shortcuts", this, [this] { showShortcutsHelp(); });
  helpMenu->addAction("3D View Information", this, [this] {
    QMessageBox::information(this, "3D View", viewer_->glInfo().isEmpty() ? QString("Not initialised") : viewer_->glInfo());
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
    for (const QString& p : recent_.list())
      recent->addAction(QFileInfo(p).fileName(), this, [this, p] {
        if (maybeSave()) {
          QString err;
          if (!openFile(p, &err)) {
            recent_.remove(p);
            recent_.save();
            showMessage("Cannot open " + p.toStdString() + ": " + err.toStdString(), core::MessageLevel::Error);
          }
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
  commandLine_ = new CommandLine(*manager_, sb);
  commandLine_->setVisible(QSettings().value("ui/commandLine", true).toBool());
  sb->addWidget(promptLabel_, 2);
  sb->addWidget(messageLabel_, 2);
  sb->addPermanentWidget(selectionLabel_);
  sb->addPermanentWidget(sketchLabel_);
  sb->addPermanentWidget(unitsLabel_);
  sb->addPermanentWidget(commandLine_, 1);
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

QString MainWindow::recoveryDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
}

QString MainWindow::recoveryFilePath() const {
  // one file per document name; the original path travels inside the file ("recoveryOf")
  return recoveryDirectory() + "/" + sanitizedFileStem(document_->name()) + ".fcad";
}

void MainWindow::scheduleAutosave() {
  if (autosaveTimer_ && document_->isModified()) autosaveTimer_->start();
}

bool MainWindow::autosaveNow() {
  if (!document_->isModified()) return false;
  QDir().mkpath(recoveryDirectory());
  emit document_->aboutToSave();  // modules flush their extension data (drawing sheets, UI state)
  QJsonObject root = document_->toJson();
  root["recoveryOf"] = core::qs(document_->filePath());
  root["recoveryTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  const QString path = recoveryFilePath();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  if (!file.commit()) return false;
  if (!lastRecoveryFile_.isEmpty() && lastRecoveryFile_ != path) QFile::remove(lastRecoveryFile_);  // renamed document
  lastRecoveryFile_ = path;
  return true;
}

void MainWindow::clearRecoveryFile() {
  if (autosaveTimer_) autosaveTimer_->stop();
  QFile::remove(recoveryFilePath());
  if (!lastRecoveryFile_.isEmpty()) QFile::remove(lastRecoveryFile_);
  lastRecoveryFile_.clear();
}

bool MainWindow::offerRecovery() {
  if (!interactive_ || qEnvironmentVariableIntValue("FABRICAD_NO_RECOVERY") != 0) return false;
  QDir dir(recoveryDirectory());
  if (!dir.exists()) return false;
  QFileInfoList files = dir.entryInfoList({"*.fcad"}, QDir::Files, QDir::Time);
  if (files.isEmpty()) return false;
  const QFileInfo newest = files.front();
  QFile f(newest.absoluteFilePath());
  if (!f.open(QIODevice::ReadOnly)) return false;
  QJsonParseError perr;
  QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &perr);
  f.close();
  if (perr.error != QJsonParseError::NoError || !jd.isObject()) {
    QFile::remove(newest.absoluteFilePath());
    return false;
  }
  QJsonObject root = jd.object();
  const QString name = root["name"].toString("Untitled");
  const QString original = root["recoveryOf"].toString();
  const QString when = newest.lastModified().toString("yyyy-MM-dd hh:mm");
  std::string question = std::format("FabriCAD did not close cleanly last time. An autosaved copy of '{}'{} from {} exists.\n\n"
                                     "Recover it? (No deletes the autosave{})",
                                     name.toStdString(), original.isEmpty() ? "" : " (" + original.toStdString() + ")",
                                     when.toStdString(), files.size() > 1 ? "s" : "");
  const bool yes = askYesNo("Recover unsaved work", question, true);
  if (!yes) {
    for (const QFileInfo& fi : files) QFile::remove(fi.absoluteFilePath());
    return false;
  }
  if (manager_->isActive()) manager_->cancel();
  viewer_->setHiddenFeatures({});
  document_->clear();
  std::string err;
  if (!document_->fromJson(root, &err)) {
    showMessage("Recovery failed: " + err, core::MessageLevel::Error);
    document_->clear();
    afterDocumentReplaced();
    return false;
  }
  document_->undoStack().clear();
  document_->setName(name.toStdString());
  document_->setModified(true);  // it only lives in memory: Save (As) stores it for good
  afterDocumentReplaced();
  QFile::remove(newest.absoluteFilePath());
  showMessage(original.isEmpty() ? "Recovered autosaved design; use Save As to keep it"
                                 : "Recovered autosaved changes of " + original.toStdString() + "; Save to keep them",
              core::MessageLevel::Warning);
  echoCommand("recover " + newest.fileName().toStdString());
  return true;
}

void MainWindow::connectSignals() {
  core::Document* d = document_.get();
  connect(manager_.get(), &core::ToolManager::toolActivated, this, &MainWindow::onToolActivated);
  connect(manager_.get(), &core::ToolManager::toolStarted, this, [this](const std::string& id) {
    if (manager_->isActive() && manager_->activeToolId() == id && dialogActivation_ != activation_) presentDialog();
  });
  connect(manager_.get(), &core::ToolManager::toolFinished, this, &MainWindow::onToolFinished);
  connect(ribbon_, &Ribbon::toolTriggered, this, [this](const std::string& id) { runCommand(id); });
  connect(ribbon_, &Ribbon::workspaceRequested, this, [this](core::Workspace ws) { setWorkspace(ws); });
  connect(search_, &CommandSearch::runRequested, this, [this](const std::string& id) { runCommand(id); });
  connect(marking_, &MarkingMenu::commandChosen, this, &MainWindow::runPseudoCommand);
  connect(palette_, &SketchPalette::commandRequested, this, [this](const std::string& id) { runCommand(id); });
  connect(dialog_, &CommandDialog::accepted, this, [this] {
    if (acceptEnabled_) manager_->accept();
  });
  connect(dialog_, &CommandDialog::cancelled, this, [this] { manager_->cancel(); });

  connect(d, &core::Document::modifiedChanged, this, [this](bool) { updateTitle(); });
  connect(d, &core::Document::nameChanged, this, &MainWindow::updateTitle);
  connect(d, &core::Document::unitsChanged, this, &MainWindow::updateTitle);
  connect(d, &core::Document::aboutToSave, this, &MainWindow::storeUiState);
  connect(d, &core::Document::loaded, this, &MainWindow::applyUiState);
  connect(d, &core::Document::recomputeFinished, this, &MainWindow::scheduleStateRefresh);
  connect(&d->undoStack(), &core::UndoStack::changed, this, &MainWindow::updateUndoButtons);
  connect(&d->undoStack(), &core::UndoStack::changed, this, &MainWindow::scheduleAutosave);
  connect(&d->selection(), &core::SelectionModel::changed, this, [this] {
    updateSelectionStatus();
    scheduleStateRefresh();
  });

  connect(browser_, &BrowserTree::editFeatureRequested, this, &MainWindow::editFeature);
  connect(browser_, &BrowserTree::deleteRequested, this, &MainWindow::deleteRefs);
  connect(browser_, &BrowserTree::runToolRequested, this, [this](const std::string& id) { runCommand(id); });
  connect(browser_, &BrowserTree::findInTimelineRequested, this, [this](ObjectId id) { timeline_->scrollToFeature(id); });
  connect(browser_, &BrowserTree::suppressRequested, this, [this](ObjectId id, bool s) {
    if (auto c = makeSuppress(*document_, id, s)) document_->undoStack().push(std::move(c));
  });
  TimelineStrip* strip = timeline_->strip();
  connect(strip, &TimelineStrip::editRequested, this, &MainWindow::editFeature);
  connect(strip, &TimelineStrip::deleteRequested, this, [this](ObjectId id) {
    if (const core::Feature* f = document_->feature(id)) deleteRefs({featureRef(*f)});
  });
  connect(strip, &TimelineStrip::suppressRequested, this, [this](ObjectId id, bool s) {
    if (auto c = makeSuppress(*document_, id, s)) document_->undoStack().push(std::move(c));
  });
  connect(strip, &TimelineStrip::findInBrowserRequested, this, [this](ObjectId id) {
    browserDock_->show();
    browser_->scrollToFeature(id);
  });
  connect(strip, &TimelineStrip::renameRequested, this, [this](ObjectId id) {
    browserDock_->show();
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
    timeline_->strip()->setHighlightedFeature(id);
  });

  connect(viewer_, &viewer::ViewerWidget::markingMenuRequested, this,
          [this](QPoint press, QPoint release, bool dragged) {
            if (dragged) markingGesture(press, release);
            else showMarkingMenu(press);
          });
  connect(viewer_, &viewer::ViewerWidget::viewCubeMenuRequested, this, &MainWindow::viewCubeMenu);
  connect(viewer_, &viewer::ViewerWidget::entityDoubleClicked, this, [this](const ShapeRef& r) {
    if (core::hasKind(core::ShapeKind::SketchCurve | core::ShapeKind::SketchPoint | core::ShapeKind::Profile |
                          core::ShapeKind::Sketch,
                      r.kind) &&
        !viewer_->inSketchMode())
      editFeature(r.owner);
    else if (ObjectId f = featureOfRef(r); f != core::kInvalidId)
      editFeature(f);
  });
  connect(viewer_, &viewer::ViewerWidget::viewInitFailed, this, [this](const QString& why) {
    showMessage(why.toStdString(), core::MessageLevel::Warning);
  });
}

void MainWindow::initializeViewer() {
  if (viewerInitialized_) return;
  viewerInitialized_ = true;
  viewer_->initializeView();
  navBar_->reposition();
  dialog_->raise();
}

// =========================================================================================
// tool lifecycle / command dialog
// =========================================================================================

bool MainWindow::isIdleTool() const {
  const core::ITool* t = manager_->activeTool();
  // the sketch module keeps "sketch.select" running between commands
  return t && t->info().hidden && t->info().id.ends_with(".select");
}

void MainWindow::onToolActivated(const std::string& id) {
  const quint64 serial = ++activation_;
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
  if (palette_->isVisible()) palette_->reposition(dialog_);
}

void MainWindow::onToolFinished(const std::string& id, bool accepted) {
  (void)id;
  (void)accepted;
  ++activation_;  // invalidate a pending dialog build
  dialog_->dismiss();
  layoutOverlays();
  acceptEnabled_ = true;
  scheduleStateRefresh();
}

void MainWindow::refreshToolDialog() {
  if (!manager_->isActive()) return;
  presentDialog();
}

void MainWindow::setToolAcceptEnabled(bool enabled) {
  acceptEnabled_ = enabled;
  dialog_->setAcceptEnabled(enabled);
}

void MainWindow::scheduleStateRefresh() {
  if (stateRefreshPending_) return;
  stateRefreshPending_ = true;
  QTimer::singleShot(0, this, [this] {
    stateRefreshPending_ = false;
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
  messageLabel_->setStyleSheet(QString("color: %1;").arg(color));
  messageLabel_->setText(core::qs(text));
  messageLabel_->setToolTip(core::qs(text));
  messageTimer_->start(level == core::MessageLevel::Info ? 5000 : 9000);
  commandLine_->appendHistory(core::qs(text));
  if (level == core::MessageLevel::Error) FC_ERROR("{}", text);
  else if (level == core::MessageLevel::Warning) FC_WARN("{}", text);
}

bool MainWindow::askYesNo(const std::string& title, const std::string& question, bool defaultAnswer) {
  if (!interactive_) return defaultAnswer;
  const auto def = defaultAnswer ? QMessageBox::Yes : QMessageBox::No;
  return QMessageBox::question(this, core::qs(title), core::qs(question), QMessageBox::Yes | QMessageBox::No, def) ==
         QMessageBox::Yes;
}

void MainWindow::setWorkspace(core::Workspace ws) {
  if (ws == workspace_) return;
  if (manager_->isActive()) manager_->cancel();
  workspace_ = ws;
  if (ws == core::Workspace::Drawing) {
    if (!drawingPage_) {
      if (drawingFactory()) drawingPage_ = drawingFactory()(*document_, *manager_, central_);
      if (!drawingPage_) {
        auto* l = new QLabel("The Drawing workspace is not available in this build.", central_);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet("background: #FFFFFF; color: #6B7079;");
        drawingPage_ = l;
      }
      central_->addWidget(drawingPage_);
    }
    central_->setCurrentWidget(drawingPage_);
  } else {
    central_->setCurrentWidget(viewer_);
  }
  timelineDock_->setVisible(ws == core::Workspace::Design);
  ribbon_->rebuild(ws);
  echoCommand(ws == core::Workspace::Design ? "workspace DESIGN" : "workspace DRAWING");
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
  const QString name = core::qs(document_->name());
  const bool mod = document_->isModified();
  setWindowTitle(QString("%1%2 - FabriCAD").arg(name, mod ? " *" : ""));
  docTab_->setText(name + (mod ? " *" : ""));
  docTab_->setToolTip(document_->filePath().empty() ? QString("Not saved yet") : core::qs(document_->filePath()));
  unitsLabel_->setText(core::lengthUnitSymbol(document_->units().length));
}

void MainWindow::updateUndoButtons() {
  const core::UndoStack& u = document_->undoStack();
  undoButton_->setEnabled(u.canUndo());
  redoButton_->setEnabled(u.canRedo());
  undoButton_->setToolTip(u.canUndo() ? QString("Undo %1 (Ctrl+Z)").arg(core::qs(u.undoText())) : "Undo (Ctrl+Z)");
  redoButton_->setToolTip(u.canRedo() ? QString("Redo %1 (Ctrl+Y)").arg(core::qs(u.redoText())) : "Redo (Ctrl+Y)");
}

void MainWindow::updateSelectionStatus() {
  selectionLabel_->setText(core::qs(describeSelection(*document_, document_->selection().items())));
}

void MainWindow::updateSketchStatus() {
  sketch::SketchSession* s = sketch::SketchSession::current();
  if (s != sketchSession_.data()) {
    QObject::disconnect(sketchConn_);
    sketchSession_ = s;
    if (s) sketchConn_ = connect(s, &sketch::SketchSession::changed, this, &MainWindow::updateSketchStatus);
  }
  palette_->sync();
  layoutOverlays();
  if (!s) {
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

void MainWindow::storeUiState() {
  UiState s;
  s.hiddenFeatures = viewer_->hiddenFeatures();
  s.showOrigin = viewer_->isOriginVisible();
  s.showSketches = viewer_->isFolderVisible("Sketches");
  s.showConstruction = viewer_->isFolderVisible("Construction");
  s.showJoints = viewer_->isFolderVisible("Joints");
  s.displayMode = viewer_->displayMode();
  s.projection = viewer_->projection();
  document_->setExtensionData("ui", s.toJson());
}

void MainWindow::applyUiState() {
  const UiState s = UiState::fromJson(document_->extensionData("ui"));
  viewer_->setHiddenFeatures(s.hiddenFeatures);
  viewer_->setOriginVisible(s.showOrigin);
  viewer_->setFolderVisible("Sketches", s.showSketches);
  viewer_->setFolderVisible("Construction", s.showConstruction);
  viewer_->setFolderVisible("Joints", s.showJoints);
  viewer_->setDisplayMode(s.displayMode);
  viewer_->setProjection(s.projection);
}

void MainWindow::afterDocumentReplaced() {
  if (viewer_->inSketchMode()) viewer_->exitSketchMode();
  ribbon_->setContextualTab("SKETCH", false);
  updateTitle();
  updateUndoButtons();
  updateSelectionStatus();
  scheduleStateRefresh();
  QTimer::singleShot(0, this, [this] {
    viewer_->syncNow();
    viewer_->fitAll(false);
  });
}

bool MainWindow::maybeSave() {
  if (!document_->isModified()) return true;
  if (!interactive_) return true;
  const auto r = QMessageBox::warning(this, "FabriCAD",
                                      QString("Save changes to %1?").arg(core::qs(document_->name())),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (r == QMessageBox::Cancel) return false;
  if (r == QMessageBox::Save) return save();
  return true;
}

bool MainWindow::newDocument() {
  if (!maybeSave()) return false;
  clearRecoveryFile();
  if (manager_->isActive()) manager_->cancel();
  viewer_->setHiddenFeatures({});
  viewer_->setOriginVisible(false);
  document_->clear();
  afterDocumentReplaced();
  viewer_->setStandardView(core::StandardView::Home, false);
  echoCommand("new");
  return true;
}

bool MainWindow::open() {
  if (!interactive_ || !maybeSave()) return false;
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
    const bool ok = manager_->run("part.import-file", {path.toStdString()});
    if (ok) {
      echoCommand("import " + path.toStdString());
      QTimer::singleShot(0, this, [this] {
        viewer_->syncNow();
        viewer_->fitAll(true);
      });
    }
    return ok;
  }
  if (manager_->isActive()) manager_->cancel();
  clearRecoveryFile();
  std::string err;
  viewer_->setHiddenFeatures({});
  if (!document_->load(path.toStdString(), &err)) {
    if (error) *error = core::qs(err);
    document_->clear();
    afterDocumentReplaced();
    return false;
  }
  if (document_->name().empty() || document_->name() == "Untitled") document_->setName(QFileInfo(path).completeBaseName().toStdString());
  document_->setModified(false);
  recent_.add(path);
  recent_.save();
  afterDocumentReplaced();
  echoCommand("open " + path.toStdString());
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
  s.setValue("file/lastDir", QFileInfo(path).absolutePath());
  QString err;
  if (!saveTo(path, &err)) {
    QMessageBox::warning(this, "Save As", "Cannot save:\n" + err);
    return false;
  }
  return true;
}

bool MainWindow::saveTo(const QString& p, QString* error) {
  QString path = p;
  if (QFileInfo(path).suffix().isEmpty()) path += ".fcad";
  if (document_->name() == "Untitled") document_->setName(QFileInfo(path).completeBaseName().toStdString());
  std::string err;
  if (!document_->save(path.toStdString(), &err)) {
    if (error) *error = core::qs(err);
    return false;
  }
  recent_.add(path);
  recent_.save();
  updateTitle();
  clearRecoveryFile();
  showMessage("Saved " + path.toStdString(), core::MessageLevel::Info);
  return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (!maybeSave()) {
    e->ignore();
    return;
  }
  if (manager_->isActive()) manager_->cancel();
  QSettings s;
  s.setValue("window/geometry", saveGeometry());
  s.setValue("window/state", saveState());
  s.setValue("search/recent", recentCommands_);
  s.setValue("ui/commandLine", commandLine_->isVisible());
  recent_.save();
  clearRecoveryFile();  // a clean close leaves nothing to recover
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
  if (!plan.bodyNotes.empty()) {
    std::string q = "Delete " + plan.summary(*document_) + "?\n\nDeleting a body removes the feature that created it:";
    for (const std::string& n : plan.bodyNotes) q += "\n  " + n;
    if (!askYesNo("Delete", q, true)) return;
  }
  for (ObjectId id : plan.features) document_->selection().removeOwner(id);
  if (auto cmd = makeDeleteCommand(*document_, plan)) {
    cmd->setText("Delete " + plan.summary(*document_));
    document_->selection().clear();
    document_->undoStack().push(std::move(cmd));
  }
}

void MainWindow::deleteSelection() {
  if (sketch::SketchSession* s = sketch::SketchSession::current(); s && viewer_->inSketchMode()) {
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
  commandLine_->edit()->setFocus();
}

void MainWindow::showShortcutsHelp() {
  auto* dlg = new QDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle("Keyboard Shortcuts");
  dlg->resize(560, 520);
  auto* v = new QVBoxLayout(dlg);
  auto* tree = new QTreeWidget(dlg);
  tree->setColumnCount(3);
  tree->setHeaderLabels({"Command", "Shortcut", "Aliases"});
  tree->setRootIsDecorated(false);
  for (core::ITool* t : registry_.tools()) {
    const core::ToolInfo& i = t->info();
    if (i.hidden && i.shortcut.empty()) continue;
    if (i.shortcut.empty() && i.aliases.empty()) continue;
    QStringList aliases;
    for (const auto& a : i.aliases) aliases << core::qs(a);
    auto* it = new QTreeWidgetItem(tree, {core::qs(i.name), core::qs(i.shortcut), aliases.join(", ")});
    it->setIcon(0, toolIcon(i));
  }
  for (const auto& [k, d] : std::initializer_list<std::pair<const char*, const char*>>{
           {"S", "Command search"}, {"Esc", "Cancel command / clear selection"},
           {"Enter / Space", "Repeat last command (OK while a command is active)"},
           {":", "Focus the command line"}, {"Middle mouse", "Pan (Fusion preset)"},
           {"Shift + middle mouse", "Orbit"}, {"Wheel", "Zoom at cursor"}, {"Right click", "Marking menu"}})
    new QTreeWidgetItem(tree, {d, k, ""});
  tree->sortItems(0, Qt::AscendingOrder);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
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
  c.inSketch = inSketch;
  for (const ShapeRef& r : doc.selection().items()) c.selectionKinds.push_back(r.kind);
  return c;
}
}  // namespace

void MainWindow::showMarkingMenu(const QPoint& globalPos) {
  const MarkingMenuModel model =
      buildMarkingMenu(registry_, markingContext(*manager_, *document_, isIdleTool(), viewer_->inSketchMode()));
  if (model.isEmpty()) return;
  marking_->popup(model, globalPos);
}

void MainWindow::markingGesture(const QPoint& press, const QPoint& release) {
  const MarkingMenuModel model =
      buildMarkingMenu(registry_, markingContext(*manager_, *document_, isIdleTool(), viewer_->inSketchMode()));
  const QPoint d = release - press;
  const int slot = slotForOffset(d.x(), d.y(), 15.0);
  if (slot < 0) return;
  const std::string& cmd = model.radial[static_cast<size_t>(slot)].command;
  if (!cmd.empty()) runPseudoCommand(cmd);
}

void MainWindow::viewCubeMenu(const QPoint& globalPos) {
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
  if (focus == viewer_ || viewer_->isAncestorOf(focus)) return true;
  if (focus == browser_ || browser_->isAncestorOf(focus)) return !isTextInput(focus);
  if (focus == timeline_ || timeline_->isAncestorOf(focus)) return true;
  if (focus == drawingPage_ || (drawingPage_ && drawingPage_->isAncestorOf(focus))) return !isTextInput(focus);
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
      if (viewer_->leftButtonNavigation() != viewer::NavAction::None) {
        viewer_->setLeftButtonNavigation(viewer::NavAction::None);
        navBar_->refresh();
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

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && e->type() == QEvent::Resize) QTimer::singleShot(0, this, &MainWindow::layoutOverlays);
  if (e->type() == QEvent::KeyPress) {
    auto* w = qobject_cast<QWidget*>(o);
    auto* k = static_cast<QKeyEvent*>(e);
    // only keys delivered inside this window (not popups/dialogs); each event once
    if (w && w->window() == this && !(k == lastKeyEvent_ && k->timestamp() == lastKeyTimestamp_)) {
      lastKeyEvent_ = k;
      lastKeyTimestamp_ = k->timestamp();
      if (handleKeyPress(k->key(), static_cast<int>(k->modifiers()), k->text(), k->isAutoRepeat(),
                         QApplication::focusWidget())) {
        k->accept();
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(o, e);
}

}  // namespace fabricad::app
