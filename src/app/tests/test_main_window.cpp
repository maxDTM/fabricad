// Headless integration tests of the application shell (QT_QPA_PLATFORM=offscreen, no GL):
// startup cost, ribbon from the registry, app tools, key routing, command dialog hosting
// (Enter/Esc/OK enable), repeat-last, command line, browser/timeline sync, delete/edit,
// save/open round trip with UI state, workspace switching, marking gestures.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/BrowserTree.h"
#include "app/CommandDialog.h"
#include "app/Icons.h"
#include "app/Theme.h"
#include "app/CommandLine.h"
#include "app/CommandSearch.h"
#include "app/MainWindow.h"
#include "app/MarkingMenu.h"
#include "app/Ribbon.h"
#include "app/TimelineWidget.h"
#include "app/AppActions.h"
#include "core/Commands.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"
#include "viewer/ViewerWidget.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>

#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QMenu>
#include <QtTest>

#include <set>

using namespace fabricad;
using core::ShapeRef;

namespace {

/// Interactive test tool with a dialog body; counts its lifecycle calls.
class DialogTool : public core::ToolBase {
 public:
  explicit DialogTool(std::string id = "test.dialog", std::string tab = "SOLID")
      : ToolBase([&] {
          core::ToolInfo i;
          i.id = id;
          i.name = "Dialog Test";
          i.tab = tab;
          i.group = "CREATE";
          i.aliases = {"DLGTEST"};
          return i;
        }()) {}
  void activate(core::ToolContext& ctx) override {
    ++activated;
    lastArgs = ctx.args;
  }
  void accept(core::ToolContext& ctx) override {
    ++accepted;
    ctx.finish();
  }
  void cancel(core::ToolContext& ctx) override {
    ++cancelled;
    ctx.cancel();
  }
  bool keyPress(core::ToolContext&, const core::ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_Tab) {
      ++tabs;
      return true;
    }
    return false;
  }
  QWidget* createDialog(core::ToolContext&, QWidget* parent) override {
    auto* w = new QWidget(parent);
    auto* e = new QLineEdit(w);
    e->setObjectName("dialogField");
    ++dialogs;
    return w;
  }
  int activated = 0, accepted = 0, cancelled = 0, dialogs = 0, tabs = 0;
  std::vector<std::string> lastArgs;
};

core::ObjectId addBox(core::Document& doc, double dx = 10) {
  auto f = std::make_unique<core::test::BoxFeature>();
  f->prop("dx").setDouble(dx);
  auto cmd = std::make_unique<core::AddFeatureCommand>(doc, std::move(f));
  core::AddFeatureCommand* raw = cmd.get();
  doc.undoStack().push(std::move(cmd));
  return raw->featureId();
}

void settle() {
  for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

}  // namespace

class TestMainWindow : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  DialogTool* tool_ = nullptr;
  QWidget* canvas() { return w_->viewer(); }
  bool key(int k, int mods = 0, QWidget* focus = nullptr) {
    return w_->handleKeyPress(k, mods, QString(), false, focus ? focus : canvas());
  }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_main_window");
    QStandardPaths::setTestModeEnabled(true);  // autosave goes to a test location
    QSettings().clear();
    core::test::registerTestFeatures();
    QElapsedTimer t;
    t.start();
    w_ = std::make_unique<app::MainWindow>();
    const qint64 ctorMs = t.elapsed();
    w_->setInteractive(false);
    w_->show();
    settle();
    qInfo("MainWindow constructed in %lld ms, shown after %lld ms (%zu tools)", static_cast<long long>(ctorMs),
          static_cast<long long>(t.elapsed()), w_->tools().tools().size());
    QVERIFY2(ctorMs < 3000, "startup must stay fast");
    auto tool = std::make_unique<DialogTool>();
    tool_ = tool.get();
    w_->tools().registerTool(std::move(tool));
    w_->ribbon()->rebuild(core::Workspace::Design);
  }

  void cleanupTestCase() { w_.reset(); }

  void init() {
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    settle();
  }

  void appToolsRegistered() {
    for (const char* id : {"app.new", "app.open", "app.save", "app.save-as", "app.undo", "app.redo", "app.delete",
                           "app.toggle-visibility", "app.compute-all", "app.parameters", "app.edit-properties",
                           "view.fit", "view.home", "view.front", "view.iso", "view.look-at", "view.orbit-left",
                           "select.priority-face"})
      QVERIFY2(w_->tools().has(id), id);
    // app-level shortcuts are unique
    std::set<std::string> seen;
    for (core::ITool* t : w_->tools().tools()) {
      const auto& i = t->info();
      if (i.shortcut.empty() || !(i.id.starts_with("app.") || i.id.starts_with("view."))) continue;
      QVERIFY2(seen.insert(i.shortcut).second, i.shortcut.c_str());
    }
    QCOMPARE(w_->tools().byShortcut("F6")->info().id, std::string("view.fit"));
  }

  void ribbonFromRegistry() {
    const auto tabs = w_->ribbon()->tabNames();
    QVERIFY(!tabs.empty());
    QCOMPARE(tabs.front(), std::string("SOLID"));
    QVERIFY(w_->ribbon()->toolActionCount() > 5);
    QCOMPARE(w_->ribbon()->currentTab(), std::string("SOLID"));
    // contextual SKETCH tab: hidden until a sketch is edited
    auto sk = std::make_unique<DialogTool>("test.sketch-tool", "SKETCH");
    w_->tools().registerTool(std::move(sk));
    w_->ribbon()->rebuild(core::Workspace::Design);
    QVERIFY(!w_->ribbon()->isTabVisible("SKETCH"));
    w_->setContextualTab("SKETCH", true);
    QVERIFY(w_->ribbon()->isTabVisible("SKETCH"));
    QCOMPARE(w_->ribbon()->currentTab(), std::string("SKETCH"));
    w_->setContextualTab("SKETCH", false);
    QVERIFY(!w_->ribbon()->isTabVisible("SKETCH"));
    QCOMPARE(w_->ribbon()->currentTab(), std::string("SOLID"));
  }

  void commandDialogLifecycle() {
    tool_->accepted = tool_->cancelled = 0;
    QVERIFY(w_->runCommand("test.dialog"));
    settle();
    QVERIFY(w_->commandDialog()->isVisible());
    QVERIFY(w_->commandDialog()->body() != nullptr);
    QVERIFY(w_->commandDialog()->body()->findChild<QLineEdit*>("dialogField"));
    QCOMPARE(tool_->dialogs > 0, true);
    // OK disabled -> Enter does not accept
    w_->setToolAcceptEnabled(false);
    QVERIFY(!w_->commandDialog()->okButton()->isEnabled());
    QVERIFY(key(Qt::Key_Return));
    QCOMPARE(tool_->accepted, 0);
    w_->setToolAcceptEnabled(true);
    QVERIFY(key(Qt::Key_Return));
    QCOMPARE(tool_->accepted, 1);
    QVERIFY(!w_->toolManager().isActive());
    settle();
    QVERIFY(!w_->commandDialog()->isVisible());
    // Enter when idle repeats the last command; Esc cancels it
    QVERIFY(key(Qt::Key_Return));
    QCOMPARE(w_->toolManager().activeToolId(), std::string("test.dialog"));
    settle();
    QVERIFY(key(Qt::Key_Escape));
    QCOMPARE(tool_->cancelled, 1);
    QVERIFY(!w_->toolManager().isActive());
    // Space repeats too
    QVERIFY(key(Qt::Key_Space));
    QVERIFY(w_->toolManager().isActive());
    // the OK button accepts
    settle();
    w_->commandDialog()->okButton()->click();
    QCOMPARE(tool_->accepted, 2);
  }

  void commandDialogDocksLeftAndKeepsSize() {
    QVERIFY(w_->runCommand("test.dialog"));
    settle();
    app::CommandDialog* d = w_->commandDialog();
    QVERIFY(d->isVisible());
    QVERIFY(d->parentWidget());
    // docked at the top-left of the viewport (UX-17), away from the ViewCube
    QVERIFY(d->x() < d->parentWidget()->width() / 2);
    QVERIFY(d->y() < 40);
    const int h0 = d->height();
    d->setMessage("A long prompt that certainly wraps onto more than one line in the fixed-width command dialog");
    const int h1 = d->height();
    QVERIFY(h1 >= h0);
    d->setMessage({});
    QCOMPARE(d->height(), h1);  // never shrinks while the same command runs
    QVERIFY(d->body()->findChild<QLineEdit*>("dialogField"));
    w_->toolManager().cancel();
    settle();
    QVERIFY(!d->isVisible());
  }

  void themeAndMenuHeaders() {
    const QPalette p = app::lightPalette();
    QCOMPARE(p.color(QPalette::Active, QPalette::Text), QColor("#2B2E33"));
    QCOMPARE(p.color(QPalette::Active, QPalette::Base), QColor("#FFFFFF"));
    QCOMPARE(p.color(QPalette::Disabled, QPalette::Text), QColor("#A9AEB5"));
    QMenu m;
    QAction* h = app::addMenuHeader(&m, "Camera");
    QCOMPARE(m.actions().size(), 1);  // no leading separator
    QVERIFY(!h->isCheckable() && !h->isEnabled() && !h->data().isValid());
    app::addMenuHeader(&m, "More");
    QCOMPARE(m.actions().size(), 3);  // separator + header
  }

  void keysGoToToolFirst() {
    QVERIFY(w_->runCommand("test.dialog"));
    tool_->tabs = 0;
    QVERIFY(key(Qt::Key_Tab));
    QCOMPARE(tool_->tabs, 1);
    // a view shortcut does not interrupt the command
    QVERIFY(key(Qt::Key_F6));
    QCOMPARE(w_->toolManager().activeToolId(), std::string("test.dialog"));
    // typing into a dialog field: letters are not shortcuts, Esc reaches the dialog normally
    settle();
    QLineEdit* field = w_->commandDialog()->body()->findChild<QLineEdit*>("dialogField");
    QVERIFY(field);
    QVERIFY(!key(Qt::Key_E, 0, field));
    QVERIFY(!key(Qt::Key_Escape, 0, field));
    w_->toolManager().cancel();
  }

  void undoRedoShortcuts() {
    core::Document& doc = w_->document();
    const int n = doc.featureCount();
    addBox(doc);
    QCOMPARE(doc.featureCount(), n + 1);
    QVERIFY(key(Qt::Key_Z, Qt::ControlModifier));
    QCOMPARE(doc.featureCount(), n);
    QVERIFY(key(Qt::Key_Y, Qt::ControlModifier));
    QCOMPARE(doc.featureCount(), n + 1);
    QVERIFY(key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier) == false || doc.featureCount() == n + 1);
    // Ctrl+Z inside a text field is the field's undo
    QVERIFY(!key(Qt::Key_Z, Qt::ControlModifier, w_->commandLine()->edit()));
    QCOMPARE(doc.featureCount(), n + 1);
  }

  void commandLineRunsAliases() {
    w_->commandLine()->submit("DLGTEST a b");
    QCOMPARE(w_->toolManager().activeToolId(), std::string("test.dialog"));
    QCOMPARE(tool_->lastArgs, (std::vector<std::string>{"a", "b"}));
    w_->toolManager().cancel();
    w_->commandLine()->submit("FIT");
    QCOMPARE(w_->echoHistory().back(), std::string("view.fit"));
    QVERIFY(w_->commandLine()->history().size() >= 2);
    // empty line repeats the last repeatable command
    w_->commandLine()->submit("");
    QCOMPARE(w_->toolManager().activeToolId(), std::string("test.dialog"));
    w_->toolManager().cancel();
    QVERIFY(!w_->commandLine()->submit("NOSUCHCOMMAND"));
    QVERIFY(w_->messageText().contains("Unknown command"));
  }

  void browserAndTimelineFollowDocument() {
    QVERIFY(w_->newDocument());
    settle();
    core::Document& doc = w_->document();
    const core::ObjectId f1 = addBox(doc);
    const core::ObjectId f2 = addBox(doc, 3);
    settle();
    QCOMPARE(w_->timeline()->strip()->layout().entries().size(), size_t(2));
    const core::ObjectId b1 = doc.feature(f1)->createdBodies().front();
    QTreeWidgetItem* bodyItem = w_->browser()->itemForKey(QString("body:%1").arg(b1));
    QVERIFY(bodyItem);
    QCOMPARE(bodyItem->text(0).toStdString(), doc.body(b1)->name);
    // eye toggles body visibility through an undoable command
    w_->browser()->toggleVisibility(bodyItem);
    QVERIFY(!doc.body(b1)->visible);
    doc.undoStack().undo();
    QVERIFY(doc.body(b1)->visible);
    // selection mirrors between browser and model, status bar describes it
    doc.selection().set(ShapeRef::body(b1));
    settle();
    QCOMPARE(w_->selectionText().toStdString(), doc.body(b1)->name);
    QVERIFY(w_->browser()->itemForKey(QString("body:%1").arg(b1))->isSelected());
    // renaming the selected body refreshes the status bar (UX-20)
    doc.undoStack().push(app::makeRenameBody(doc, b1, "Renamed Seat"));
    settle();
    QCOMPARE(w_->selectionText(), QString("Renamed Seat"));
    doc.undoStack().undo();
    settle();
    QCOMPARE(w_->selectionText().toStdString(), doc.body(b1)->name);
    // rollback via the timeline playback buttons (undoable)
    w_->timeline()->stepBack();
    QCOMPARE(doc.rollbackIndex(), 1);
    settle();
    QVERIFY(!w_->timeline()->strip()->layout().entries()[1].active);
    w_->timeline()->stepToEnd();
    QCOMPARE(doc.rollbackIndex(), 2);
    w_->timeline()->stepToStart();
    QCOMPARE(doc.rollbackIndex(), 0);
    doc.undoStack().undo();
    QCOMPARE(doc.rollbackIndex(), 2);
    (void)f2;
  }

  void deleteAndEdit() {
    QVERIFY(w_->newDocument());
    core::Document& doc = w_->document();
    const core::ObjectId f1 = addBox(doc);
    const core::ObjectId f2 = addBox(doc, 4);
    doc.selection().set(ShapeRef::feature(f2));
    QVERIFY(key(Qt::Key_Delete));
    QCOMPARE(doc.featureCount(), 1);
    QVERIFY(doc.selection().isEmpty());
    doc.undoStack().undo();
    QCOMPARE(doc.featureCount(), 2);
    // deleting a body asks (non-interactive: default yes) and removes its feature
    doc.selection().set(ShapeRef::body(doc.feature(f2)->createdBodies().front()));
    w_->deleteSelection();
    QCOMPARE(doc.featureCount(), 1);
    // edit falls back to the generic property dialog for module-less features
    w_->editFeature(f1);
    QCOMPARE(w_->toolManager().activeToolId(), std::string("app.edit-properties"));
    settle();
    QVERIFY(w_->commandDialog()->isVisible());
    QVERIFY(w_->commandDialog()->body()->findChildren<QLineEdit*>().size() >= 3);
    QVERIFY(key(Qt::Key_Escape));
    QVERIFY(!w_->toolManager().isActive());
  }

  void saveOpenRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(w_->newDocument());
    core::Document& doc = w_->document();
    const core::ObjectId f1 = addBox(doc, 12);
    addBox(doc, 6);
    w_->viewer()->setFeatureHidden(f1, true);
    w_->viewer()->setDisplayMode(core::DisplayMode::Wireframe);
    const QString path = dir.filePath("bracket");  // suffix is added
    QString err;
    QVERIFY2(w_->saveTo(path, &err), qPrintable(err));
    QVERIFY(QFileInfo::exists(path + ".fcad"));
    QVERIFY(!doc.isModified());
    QCOMPARE(doc.name(), std::string("bracket"));
    QVERIFY(w_->windowTitle().startsWith("bracket"));
    QCOMPARE(w_->recentFiles().list().front(), QFileInfo(path + ".fcad").absoluteFilePath());

    // Fusion: New Design opens its own tab, the saved design stays open in its tab
    const int tabs = w_->tabCount();
    app::DocumentTab* savedTab = w_->activeTab();
    QVERIFY(w_->newDocument());
    app::DocumentTab* emptyTab = w_->activeTab();
    QCOMPARE(w_->tabCount(), tabs + 1);
    QVERIFY(&w_->document() != &doc);
    QCOMPARE(w_->document().featureCount(), 0);
    QCOMPARE(doc.featureCount(), 2);
    QVERIFY(!w_->viewer()->isFeatureHidden(f1));
    QVERIFY(w_->viewer() != savedTab->viewer());
    // opening a file that is already open activates its tab instead of loading it twice
    QVERIFY2(w_->openFile(path + ".fcad", &err), qPrintable(err));
    QCOMPARE(w_->activeTab(), savedTab);
    QCOMPARE(w_->tabCount(), tabs + 1);
    // close it (clean, no prompt) and load it from disk into the empty tab (an untouched,
    // unnamed design that is active is reused, as Fusion reuses the start-up document)
    QVERIFY(w_->closeTab(savedTab));
    QCOMPARE(w_->tabCount(), tabs);
    w_->activateTab(emptyTab);
    QVERIFY(emptyTab->isPristine());
    QVERIFY2(w_->openFile(path + ".fcad", &err), qPrintable(err));
    QCOMPARE(w_->tabCount(), tabs);  // the pristine empty tab was reused
    QCOMPARE(w_->activeTab(), emptyTab);
    core::Document& reopened = w_->document();
    QCOMPARE(reopened.featureCount(), 2);
    QCOMPARE(reopened.liveBodies().size(), size_t(2));
    QVERIFY(w_->viewer()->isFeatureHidden(f1));
    QCOMPARE(w_->viewer()->displayMode(), core::DisplayMode::Wireframe);
    QVERIFY(!reopened.undoStack().canUndo());
    QVERIFY(w_->windowTitle().startsWith("bracket"));
    QCOMPARE(w_->tabBar()->tabText(w_->indexOfTab(w_->activeTab())), QString("bracket"));
    w_->viewer()->setDisplayMode(core::DisplayMode::ShadedEdges);
    QVERIFY(!w_->openFile(dir.filePath("missing.fcad"), &err));
    QCOMPARE(w_->tabCount(), tabs);  // a failed open leaves no tab behind
    // the command-line form: SAVE <path>
    QVERIFY(w_->commandLine()->submit("SAVE " + dir.filePath("copy.fcad")));
    QVERIFY(QFileInfo::exists(dir.filePath("copy.fcad")));
  }

  void importStepFile() {
    QTemporaryDir dir;
    const QString step = dir.filePath("part.step");
    {
      STEPControl_Writer wr;
      wr.Transfer(BRepPrimAPI_MakeBox(30, 20, 10).Shape(), STEPControl_AsIs);
      QCOMPARE(wr.Write(step.toStdString().c_str()), IFSelect_RetDone);
    }
    QVERIFY(w_->newDocument());
    QString err;
    QVERIFY2(w_->openFile(step, &err), qPrintable(err));
    settle();
    core::Document& doc = w_->document();
    QCOMPARE(doc.featureCount(), 1);
    QCOMPARE(doc.featureAt(0)->status(), core::FeatureStatus::Ok);
    const auto live = doc.liveBodies(true);
    QCOMPARE(live.size(), size_t(1));
    QVERIFY(doc.body(live.front())->visible);
    w_->viewer()->syncNow();
    bool shown = false;
    for (const auto& item : w_->viewer()->sceneItems()) shown = shown || item.ref == ShapeRef::body(live.front());
    QVERIFY(shown);
    QTreeWidgetItem* it = w_->browser()->itemForKey(QString("body:%1").arg(live.front()));
    QVERIFY(it);
    QVERIFY(it->data(1, Qt::UserRole).toBool());
  }

  void autosaveAndRecovery() {
    QVERIFY(w_->newDocument());
    // autosave covers every open document: close the (modified) tabs of the earlier tests
    for (int i = w_->tabCount() - 1; i >= 0; --i)
      if (w_->tabAt(i) != w_->activeTab()) QVERIFY(w_->closeTab(i));  // non-interactive: discards
    QCOMPARE(w_->tabCount(), 1);
    core::Document& doc = w_->document();
    QDir(app::MainWindow::recoveryDirectory()).removeRecursively();
    QVERIFY(!w_->autosaveNow());  // nothing modified: nothing written
    addBox(doc, 12);
    QVERIFY(doc.isModified());
    QVERIFY(w_->autosaveNow());
    const QString rec = w_->recoveryFilePath();
    QVERIFY2(QFileInfo::exists(rec), qPrintable(rec));
    QVERIFY(rec.startsWith(app::MainWindow::recoveryDirectory()));
    // the copy is a loadable document with the same content; the live document is untouched
    QVERIFY(doc.isModified());
    QVERIFY(doc.filePath().empty());
    core::Document copy;
    std::string err;
    QVERIFY2(copy.load(rec.toStdString(), &err), err.c_str());
    QCOMPARE(copy.featureCount(), doc.featureCount());
    // non-interactive sessions never prompt (and keep the file)
    QVERIFY(!w_->offerRecovery());
    QVERIFY(QFileInfo::exists(rec));
    // a clean save removes the recovery copy
    QTemporaryDir tmp;
    QVERIFY(w_->saveTo(tmp.filePath("autosaved.fcad")));
    QVERIFY(!QFileInfo::exists(rec));
    // the 5 s timer is armed by an undoable change
    addBox(doc, 14);
    QVERIFY(doc.isModified());
    QVERIFY(w_->findChildren<QTimer*>().size() > 0);
    bool armed = false;
    for (QTimer* t : w_->findChildren<QTimer*>())
      if (t->isActive() && t->isSingleShot() && t->interval() == 5000) armed = true;
    QVERIFY(armed);
    doc.setModified(false);
    w_->clearRecoveryFile();
  }

  void workspaceSwitch() {
    // Design -> Drawing opens a drawing tab linked to the design's document
    app::DocumentTab* design = w_->activeTab();
    QVERIFY(design->isDesign());
    core::Document* doc = &w_->document();
    const int tabs = w_->tabCount();
    w_->setWorkspace(core::Workspace::Drawing);
    QCOMPARE(w_->workspace(), core::Workspace::Drawing);
    QCOMPARE(w_->ribbon()->workspace(), core::Workspace::Drawing);
    QCOMPARE(w_->tabCount(), tabs + 1);
    app::DocumentTab* drawing = w_->activeTab();
    QVERIFY(!drawing->isDesign());
    QCOMPARE(&w_->document(), doc);           // same document
    QVERIFY(&w_->toolManager() != &design->manager());  // own tool manager
    QCOMPARE(&w_->commandLine()->manager(), &w_->toolManager());
    QCOMPARE(&w_->ribbon()->manager(), &w_->toolManager());
    QVERIFY(w_->timeline() == nullptr);      // drawings have no timeline / 3D view
    QVERIFY(w_->viewer() == nullptr);
    QVERIFY(w_->tabBar()->tabText(w_->indexOfTab(drawing)).contains("Drawing"));
    // a second request reuses the tab
    w_->setWorkspace(core::Workspace::Design);
    QCOMPARE(w_->activeTab(), design);
    QCOMPARE(w_->ribbon()->workspace(), core::Workspace::Design);
    settle();
    QVERIFY(w_->timeline()->isVisible());
    QCOMPARE(w_->viewer(), design->viewer());
    w_->setWorkspace(core::Workspace::Drawing);
    QCOMPARE(w_->activeTab(), drawing);
    QCOMPARE(w_->tabCount(), tabs + 1);
    // tab bar clicks switch too; closing the drawing tab keeps the document alive
    w_->activateTab(design);
    QCOMPARE(w_->workspace(), core::Workspace::Design);
    QVERIFY(w_->closeTab(drawing));
    QCOMPARE(w_->tabCount(), tabs);
    QCOMPARE(&w_->document(), doc);
    QCOMPARE(w_->activeTab(), design);
  }

  void tabsOwnTheirState() {
    // every tab has its own document, undo stack, selection, tool manager and camera
    app::DocumentTab* a = w_->activeTab();
    core::Document& docA = w_->document();
    addBox(docA, 10);
    const auto bodiesA = docA.liveBodies();
    QVERIFY(!bodiesA.empty());
    docA.selection().set(ShapeRef::body(bodiesA.front()));
    app::DocumentTab* b = w_->addDesignTab();
    QVERIFY(b != a);
    QCOMPARE(w_->activeTab(), b);
    core::Document& docB = w_->document();
    QVERIFY(&docB != &docA);
    QCOMPARE(docB.featureCount(), 0);
    QVERIFY(docB.selection().isEmpty());
    QVERIFY(!docB.undoStack().canUndo());
    QVERIFY(docA.undoStack().canUndo());
    QCOMPARE(w_->browser(), b->browser());
    QCOMPARE(w_->timeline(), b->timeline());
    // a command typed in tab B runs in tab B's manager only
    QVERIFY(w_->commandLine()->submit("test.dialog"));
    QVERIFY(b->manager().isActive());
    QVERIFY(!a->manager().isActive());
    // switching tabs cancels the command of the tab being left (deferred, like Fusion)
    w_->activateTab(a);
    settle();
    QVERIFY(!b->manager().isActive());
    QCOMPARE(&w_->document(), &docA);
    QCOMPARE(docA.selection().count(), size_t(1));
    // TAB / CLOSE shell verbs
    QVERIFY(w_->commandLine()->submit("TAB " + QString::number(w_->indexOfTab(b) + 1)));
    settle();
    QCOMPARE(w_->activeTab(), b);
    QVERIFY(w_->commandLine()->submit("CLOSE"));
    settle();
    QVERIFY(w_->indexOfTab(b) < 0);
    QCOMPARE(w_->activeTab(), a);
    // closing the last tab leaves a fresh empty design
    for (int i = w_->tabCount() - 1; i >= 0; --i) QVERIFY(w_->closeTab(i));
    QCOMPARE(w_->tabCount(), 1);
    QCOMPARE(w_->document().featureCount(), 0);
  }

  void searchAndMarking() {
    QVERIFY(key(Qt::Key_S));
    QVERIFY(w_->commandSearch()->isVisible());
    const QString grabDir = qEnvironmentVariable("FABRICAD_TEST_GRAB_DIR");  // visual check aid
    if (!grabDir.isEmpty()) {
      w_->commandSearch()->setQuery("fil");
      w_->commandSearch()->grab().save(grabDir + "/search.png");
      w_->showMarkingMenu(QPoint(400, 400));
      settle();
      if (auto* mm = w_->findChild<app::MarkingMenu*>()) mm->grab().save(grabDir + "/marking.png");
      w_->findChild<app::MarkingMenu*>()->hide();
      w_->grab().save(grabDir + "/window.png");
    }
    w_->commandSearch()->setQuery("dialog test");
    const auto r = w_->commandSearch()->results();
    QVERIFY(!r.empty());
    QCOMPARE(r.front(), std::string("test.dialog"));
    w_->commandSearch()->hide();
    // marking gesture: drag north while a command runs = OK
    QVERIFY(w_->runCommand("test.dialog"));
    tool_->accepted = 0;
    w_->markingGesture(QPoint(500, 500), QPoint(500, 420));
    QCOMPARE(tool_->accepted, 1);
    // gesture inside the dead zone does nothing
    w_->markingGesture(QPoint(500, 500), QPoint(503, 502));
    QVERIFY(!w_->toolManager().isActive());
  }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
