// Redesign phase 2 (sidebar): SidebarHost, FEATURES (FeatureListWidget over the timeline groups),
// MODEL restyle, SELECTION panel, panel headers, timeline-style and sidebar-side toggles, the
// persisted splitter / collapse state and the command-card dock API. Headless through a real
// MainWindow (QT_QPA_PLATFORM=offscreen) with the bracket-v3 fixture, in both themes, both sides
// and both timeline styles. docs/uiux/redesign/p2.md lists what each test covers.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/BrowserTree.h"
#include "app/FeatureListModel.h"
#include "app/FeatureListWidget.h"
#include "app/InlineTip.h"
#include "app/MainWindow.h"
#include "app/SelectionPanel.h"
#include "app/SidebarHost.h"
#include "app/SidebarPanel.h"
#include "app/ThemeManager.h"
#include "app/TimelineActions.h"
#include "app/TimelineWidget.h"
#include "app/UiSettings.h"
#include "core/Document.h"
#include "core/GroupCommands.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "theme/Tokens.h"

#include <QDockWidget>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace fabricad;
using app::FeatureListWidget;
using app::FeatureRow;
using app::SidebarHost;
using core::ObjectId;
using Kind = FeatureRow::Kind;

namespace {

void settle() {
  for (int i = 0; i < 4; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/// Menu entries of a context menu built by TimelineActions.
QStringList menuTexts(QMenu& m) {
  QStringList out;
  for (QAction* a : m.actions())
    if (!a->isSeparator()) out << a->text().section('\t', 0, 0);
  return out;
}

}  // namespace

class TestSidebar : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  QString fixture_;

  core::Document& doc() { return w_->document(); }
  FeatureListWidget* list() { return w_->featureList(); }
  SidebarHost* host() { return w_->sidebarHost(); }
  ObjectId feature(const char* name) {
    for (const auto& f : doc().features())
      if (f->name() == name) return f->id();
    return core::kInvalidId;
  }
  const FeatureRow& row(int r) { return list()->model().rows()[static_cast<size_t>(r)]; }
  int rowOf(const char* name) {
    const auto& rows = list()->model().rows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
      if (rows[static_cast<size_t>(i)].name == name) return i;
    return -1;
  }
  void openFixture() {
    for (int i = w_->tabCount() - 1; i >= 0; --i) QVERIFY(w_->closeTab(i));
    QString err;
    QVERIFY2(w_->openFile(fixture_, &err), qPrintable(err));
    settle();
    list()->refresh();
    QCOMPARE(doc().featureCount(), 13);
  }
  QPoint center(int r) { return list()->rowRect(r).center(); }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_sidebar");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    fixture_ = QFINDTESTDATA("../../../tests/e2e/bracket-v3.fcad");
    QVERIFY2(!fixture_.isEmpty(), "tests/e2e/bracket-v3.fcad missing");
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->resize(1600, 1000);
    w_->show();
    settle();
  }
  void cleanupTestCase() { w_.reset(); }

  void init() {
    app::UiSettings::instance().reset("ui/sidebarSide");
    app::UiSettings::instance().reset("ui/timelineStyle");
    openFixture();
  }

  // --- structure and headers (5.2) ---------------------------------------------------------
  void structure() {
    auto* dock = w_->findChild<QDockWidget*>("BrowserDock");
    QVERIFY(dock);
    QCOMPARE(dock->widget(), static_cast<QWidget*>(host()));
    QCOMPARE(dock->features(), QDockWidget::NoDockWidgetFeatures);  // not floatable, no title bar
    QVERIFY(w_->findChild<QWidget*>("SidebarHost"));
    QVERIFY(w_->findChild<QWidget*>("CardSlot"));
    QVERIFY(w_->findChild<FeatureListWidget*>("FeatureList"));
    QVERIFY(w_->findChild<app::SelectionPanel*>("SelectionPanel"));
    QVERIFY(w_->findChild<app::BrowserTree*>("Browser"));
    QCOMPARE(w_->browser(), host()->modelTree());
    QCOMPARE(host()->splitter()->count(), 4);  // card slot, FEATURES, MODEL, SELECTION
    QVERIFY(host()->cardSlot()->isHidden());
    QVERIFY(dock->width() >= SidebarHost::kMinWidth);
    // FEATURES count chip = timeline features of the fixture
    QCOMPARE(host()->panel(SidebarHost::Panel::Features)->header()->count(), 13);
  }

  void headersStandOut() {
    for (theme::Theme th : {theme::Theme::Dark, theme::Theme::Light}) {
      app::ThemeManager::instance().setMode(th == theme::Theme::Dark ? app::ThemeManager::Mode::Dark : app::ThemeManager::Mode::Light, false);
      settle();
      app::PanelHeader* h = host()->panel(SidebarHost::Panel::Features)->header();
      QCOMPARE(h->height(), app::PanelHeader::kHeight);
      const QImage img = h->grab().toImage();
      const QImage rows = list()->viewport()->grab().toImage();
      const int dpr = qRound(img.devicePixelRatio());
      // band: surface.header, distinct from the rows (surface.panel); 1 px border.strong rules
      const QRgb band = img.pixelColor(190 * dpr, 4 * dpr).rgb();
      const QRgb rowBg = rows.pixelColor((list()->viewport()->width() - 4) * dpr, 2 * dpr).rgb();
      QCOMPARE(band, theme::color(theme::Tok::SurfaceHeader, th).rgb());
      QCOMPARE(rowBg, theme::color(theme::Tok::SurfacePanel, th).rgb());
      QVERIFY(band != rowBg);
      QCOMPARE(img.pixelColor(190 * dpr, 0).rgb(), theme::color(theme::Tok::BorderStrong, th).rgb());
      QCOMPARE(img.pixelColor(190 * dpr, (h->height() - 1) * dpr).rgb(), theme::color(theme::Tok::BorderStrong, th).rgb());
      QCOMPARE(img.pixelColor(11 * dpr, 18 * dpr).rgb(), theme::color(theme::Tok::AccentIndicator, th).rgb());  // 3 px tick
      // the header text is one size above and heavier than the row text
      const QFont hf = theme::font(theme::TextStyle::Header);
      const QFont rf = theme::font(theme::TextStyle::Value);
      QVERIFY(hf.weight() > rf.weight());
      QVERIFY(hf.pixelSize() >= 12);
      QVERIFY(hf.capitalization() == QFont::AllUppercase);
      // the title is never cut at the minimum sidebar width
      QVERIFY(h->titleRect().right() < SidebarHost::kMinWidth - 80);
    }
    app::ThemeManager::instance().setMode(app::ThemeManager::Mode::Dark, false);
  }

  // --- rows from the timeline (feature-list plan) ---------------------------------------------
  void rowsFromFixture() {
    const auto& rows = list()->model().rows();
    QCOMPARE(rows.front().kind, Kind::Origin);
    QCOMPARE(rows.back().kind, Kind::Insert);
    int groups = 0;
    for (const FeatureRow& r : rows) groups += r.kind == Kind::Group;
    QCOMPARE(groups, 3);
    const int base = rowOf("Base plate"), back = rowOf("Back wall"), light = rowOf("Lightening");
    QVERIFY(base > 0 && back > base && light > back);
    QCOMPARE(row(base).memberCount, 3);
    QCOMPARE(row(base).groupState, core::GroupSuppression::None);
    QVERIFY(row(back).collapsed);                       // members hidden
    QCOMPARE(row(back).memberCount, 4);
    QCOMPARE(rowOf("Extrude2"), -1);
    QCOMPARE(row(light).groupState, core::GroupSuppression::All);
    QVERIFY(row(light).suppressed);
    QVERIFY(row(rowOf("Sketch4")).suppressed);
    QCOMPARE(row(rowOf("Pocket1")).value, QString("off"));
    QCOMPARE(row(rowOf("Sketch1")).depth, 1);
    QCOMPARE(row(rowOf("Extrude1")).value, QString("12 mm"));
    QCOMPARE(row(rowOf("Hole1")).value, QString::fromUtf8("ø14 × 2"));
    QCOMPARE(row(rowOf("Sketch1")).value, QString::fromUtf8("150 × 100"));
    // rollback bar right after Extrude3; Fillet2 and Chamfer1 are rolled back
    const int bar = list()->model().rollbackRow();
    QCOMPARE(bar, rowOf("Extrude3") + 1);
    QVERIFY(row(rowOf("Fillet2")).rolledBack);
    QVERIFY(!row(rowOf("Extrude3")).rolledBack);
    QCOMPARE(list()->model().rollbackIndexForGap(bar), doc().rollbackIndex());
    // every row has a non-colour cue: suppressed = strike + "off"; rolled back = below the bar
    QVERIFY(rowOf("Fillet2") > bar);
  }

  void valueTextUnits() {
    core::UnitSystem u = doc().units();
    u.length = core::LengthUnit::Inch;
    doc().setUnits(u);
    list()->refresh();
    QVERIFY(row(rowOf("Extrude1")).value.endsWith(" in"));
    u.length = core::LengthUnit::Millimeter;
    doc().setUnits(u);
    list()->refresh();
    const QString chamfer = row(rowOf("Chamfer1")).value;
    QVERIFY2(!chamfer.isEmpty() && !chamfer.contains(" deg"), qPrintable(chamfer));  // ° without a space
  }

  // --- group interactions ---------------------------------------------------------------------
  void groupCheckboxTriState() {
    const int light = rowOf("Lightening");
    const ObjectId g = row(light).id;
    const int undoBefore = doc().undoStack().count();
    // unticked (all suppressed) -> tick includes all, one undo step
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, list()->checkBoxRect(light).center());
    settle();
    QCOMPARE(doc().groupSuppression(g), core::GroupSuppression::None);
    QCOMPARE(doc().undoStack().count(), undoBefore + 1);
    // suppress one member: partial
    list()->actions()->setSuppressed({feature("Sketch4")}, true);
    settle();
    QCOMPARE(row(rowOf("Lightening")).groupState, core::GroupSuppression::Some);
    // partial -> a click includes all
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, list()->checkBoxRect(rowOf("Lightening")).center());
    settle();
    QCOMPARE(doc().groupSuppression(g), core::GroupSuppression::None);
    // ticked -> a click suppresses all
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, list()->checkBoxRect(rowOf("Lightening")).center());
    settle();
    QCOMPARE(doc().groupSuppression(g), core::GroupSuppression::All);
    doc().undoStack().undo();
    settle();
    QCOMPARE(doc().groupSuppression(g), core::GroupSuppression::None);
  }

  void suppressedGroupInlineTip() {
    const int light = rowOf("Lightening");
    QTest::mouseMove(list()->viewport(), center(light));
    settle();
    auto* tip = w_->findChild<app::InlineTip*>("InlineTip");
    QVERIFY(tip);
    QVERIFY(tip->isVisible());
    QCOMPARE(tip->title(), QString("Group suppressed"));
    QVERIFY(tip->text().contains("2 features"));
    QTest::mouseMove(list()->viewport(), center(rowOf("Extrude3")));
    settle();
    QVERIFY(!tip->isVisible());
  }

  void groupCollapseChevron() {
    const int back = rowOf("Back wall");
    QVERIFY(row(back).collapsed);
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, list()->chevronRect(back).center());
    settle();
    QVERIFY(rowOf("Extrude2") > back);  // members shown
    QCOMPARE(row(rowOf("Extrude2")).depth, 1);
    doc().undoStack().undo();  // collapse is an undo step (core GroupCommands)
    settle();
    QCOMPARE(rowOf("Extrude2"), -1);
    // keyboard: Right expands, Left folds
    list()->setFocus();
    list()->setCurrentRow(rowOf("Back wall"));
    QTest::keyClick(list(), Qt::Key_Right);
    settle();
    QVERIFY(rowOf("Extrude2") > 0);
    list()->setCurrentRow(rowOf("Back wall"));
    QTest::keyClick(list(), Qt::Key_Left);
    settle();
    QCOMPARE(rowOf("Extrude2"), -1);
  }

  // --- mouse and keyboard on feature rows -------------------------------------------------------
  void clickSelectsAndSyncs() {
    const int r = rowOf("Extrude1");
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, center(r));
    settle();
    QCOMPARE(list()->selectedItems(), std::vector<ObjectId>{feature("Extrude1")});
    QCOMPARE(doc().selection().count(), 1);
    QCOMPARE(host()->selectionPanel()->facts().front().value, QString("Extrude"));
    QCOMPARE(host()->panel(SidebarHost::Panel::Selection)->header()->count(), 1);
    // Ctrl adds, Shift extends
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, Qt::ControlModifier, center(rowOf("Hole1")));
    settle();
    QCOMPARE(list()->selectedItems().size(), size_t(2));
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, center(rowOf("Sketch1")));
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, Qt::ShiftModifier, center(rowOf("Hole1")));
    settle();
    QCOMPARE(list()->selectedItems().size(), size_t(3));
    // a selection made elsewhere shows in the list
    doc().selection().clear();
    settle();
    QVERIFY(list()->selectedItems().empty());
    QVERIFY(host()->selectionPanel()->facts().empty());
  }

  void doubleClickEditsAndF2Renames() {
    QSignalSpy edit(list(), &FeatureListWidget::editRequested);
    QTest::mouseDClick(list()->viewport(), Qt::LeftButton, {}, center(rowOf("Extrude3")));
    QCOMPARE(edit.count(), 1);
    QCOMPARE(edit.takeFirst().at(0).value<ObjectId>(), feature("Extrude3"));
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    settle();
    // F2: inline rename, as typed
    list()->setFocus();
    list()->setCurrentRow(rowOf("Fillet2"));
    QTest::keyClick(list(), Qt::Key_F2);
    QLineEdit* e = list()->renameEditor();
    QVERIFY(e);
    e->setText("Edge round");
    QTest::keyClick(e, Qt::Key_Return);
    settle();
    QVERIFY(feature("Edge round") != core::kInvalidId);
    // groups rename through the same path (double-click on a group row)
    QTest::mouseDClick(list()->viewport(), Qt::LeftButton, {}, center(rowOf("Base plate")) + QPoint(-40, 0));
    e = list()->renameEditor();
    QVERIFY(e);
    e->setText("Plate");
    QTest::keyClick(e, Qt::Key_Return);
    settle();
    QVERIFY(doc().findGroup("Plate") != core::kInvalidId);
    // Escape cancels
    list()->startRename(feature("Hole1"));
    QVERIFY(list()->renameEditor());
    list()->renameEditor()->setText("nope");
    QTest::keyClick(list()->renameEditor(), Qt::Key_Escape);
    settle();
    QVERIFY(feature("Hole1") != core::kInvalidId);
  }

  void spaceSuppressesAndKeysStayInList() {
    list()->setFocus();
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, center(rowOf("Extrude3")));
    settle();
    // MainWindow lets the list keep Space (not "repeat last command")
    QVERIFY(!w_->handleKeyPress(Qt::Key_Space, 0, " ", false, list()));
    QVERIFY(!w_->handleKeyPress(Qt::Key_Down, 0, {}, false, list()));
    QVERIFY(!w_->handleKeyPress(Qt::Key_F2, 0, {}, false, list()));
    QTest::keyClick(list(), Qt::Key_Space);
    settle();
    QVERIFY(doc().feature(feature("Extrude3"))->isSuppressed());
    QTest::keyClick(list(), Qt::Key_Space);
    settle();
    QVERIFY(!doc().feature(feature("Extrude3"))->isSuppressed());
    // Up / Down move the current row and the selection
    QTest::keyClick(list(), Qt::Key_Up);
    settle();
    QCOMPARE(list()->selectedItems(), std::vector<ObjectId>{feature("Sketch3")});
    // Delete asks the shell (with its confirmation)
    QSignalSpy del(list(), &FeatureListWidget::deleteRequested);
    disconnect(list(), &FeatureListWidget::deleteRequested, w_.get(), nullptr);
    QTest::keyClick(list(), Qt::Key_Delete);
    QCOMPARE(del.count(), 1);
  }

  // --- rollback bar -------------------------------------------------------------------------
  void rollbackBarDrag() {
    const int before = doc().rollbackIndex();
    const int bar = list()->model().rollbackRow();
    const QPoint from = center(bar);
    const QPoint to = list()->rowRect(rowOf("Sketch3")).topLeft() + QPoint(60, 1);
    QTest::mousePress(list()->viewport(), Qt::LeftButton, {}, from);
    QMouseEvent move(QEvent::MouseMove, to, list()->viewport()->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, {});
    QCoreApplication::sendEvent(list()->viewport(), &move);
    QTest::mouseRelease(list()->viewport(), Qt::LeftButton, {}, to);
    settle();
    QCOMPARE(doc().rollbackIndex(), doc().indexOf(feature("Sketch3")));
    QVERIFY(doc().rollbackIndex() < before);
    doc().undoStack().undo();  // one SetRollbackCommand
    settle();
    QCOMPARE(doc().rollbackIndex(), before);
  }

  void rollbackBarKeyboard() {
    const int before = doc().rollbackIndex();
    list()->setFocus();
    list()->setCurrentRow(list()->model().rollbackRow());
    QTest::keyClick(list(), Qt::Key_Up);
    settle();
    QCOMPARE(doc().rollbackIndex(), doc().indexOf(feature("Extrude3")));
    QCOMPARE(list()->currentRow(), list()->model().rollbackRow());  // the bar keeps the focus
    QTest::keyClick(list(), Qt::Key_Down);
    QTest::keyClick(list(), Qt::Key_Down);
    settle();
    QCOMPARE(doc().rollbackIndex(), before + 1);
    QTest::keyClick(list(), Qt::Key_Return);  // leave the bar
    QVERIFY(list()->currentRow() != list()->model().rollbackRow());
  }

  // --- drag reorder ---------------------------------------------------------------------------
  void dropRules() {
    const auto& m = list()->model();
    // Extrude1 above its own sketch: refused with the core's reason
    app::FeatureListModel::Drop d = m.dropForGap(doc(), rowOf("Extrude1"), rowOf("Sketch1"));
    QVERIFY(!d.valid);
    QVERIFY(!d.reason.isEmpty());
    // a group moves as a whole, checked by canMoveGroup
    d = m.dropForGap(doc(), rowOf("Lightening"), m.insertRow());
    QVERIFY(d.valid || !d.reason.isEmpty());
    // the end of an expanded group: indentation chooses inside / after
    const int afterHole = rowOf("Hole1") + 1;  // gap below the last member of Base plate
    d = m.dropForGap(doc(), rowOf("Chamfer1"), afterHole, 1);
    QVERIFY(d.targetGroup.has_value());
    QCOMPARE(*d.targetGroup, doc().findGroup("Base plate"));
    d = m.dropForGap(doc(), rowOf("Chamfer1"), afterHole, 0);
    QVERIFY(d.targetGroup.has_value());
    QCOMPARE(*d.targetGroup, core::kInvalidId);
  }

  void dragReorderWithRefusal() {
    QSignalSpy msg(list(), &FeatureListWidget::messageRequested);
    auto drag = [&](int fromRow, QPoint to) {
      QTest::mousePress(list()->viewport(), Qt::LeftButton, {}, center(fromRow));
      for (QPoint p : {center(fromRow) + QPoint(0, 8), to}) {
        QMouseEvent move(QEvent::MouseMove, p, list()->viewport()->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
        QCoreApplication::sendEvent(list()->viewport(), &move);
      }
      QTest::mouseRelease(list()->viewport(), Qt::LeftButton, {}, to);
      settle();
    };
    // refused: Extrude1 above Sketch1
    const int count = doc().undoStack().count();
    drag(rowOf("Extrude1"), list()->rowRect(rowOf("Sketch1")).topLeft() + QPoint(80, 2));
    QCOMPARE(doc().undoStack().count(), count);
    QCOMPARE(msg.count(), 1);
    QVERIFY(msg.takeFirst().at(0).toString().startsWith("Cannot move Extrude1"));
    // allowed: Chamfer1 (rolled back, no dependants) up before Fillet2
    const int chamferBefore = doc().indexOf(feature("Chamfer1"));
    drag(rowOf("Chamfer1"), list()->rowRect(rowOf("Fillet2")).topLeft() + QPoint(80, 2));
    QCOMPARE(doc().indexOf(feature("Chamfer1")), chamferBefore - 1);
    doc().undoStack().undo();
    settle();
    QCOMPARE(doc().indexOf(feature("Chamfer1")), chamferBefore);
  }

  // --- shared context menu (TimelineActions) --------------------------------------------------
  void sharedContextMenus() {
    QMenu fm;
    list()->actions()->populate(fm, {feature("Extrude1")});
    const QStringList f = menuTexts(fm);
    for (const char* t : {"Edit Feature", "Roll History Marker Here", "Suppress Feature", "Rename", "Create Group", "Delete"})
      QVERIFY2(f.contains(t), t);
    QMenu gm;
    list()->actions()->populate(gm, {doc().findGroup("Base plate")});
    const QStringList g = menuTexts(gm);
    for (const char* t : {"Collapse Group", "Suppress Group", "Rename Group", "Ungroup"}) QVERIFY2(g.contains(t), t);
    // the strip builds its menu from the same helper
    QMenu sm;
    w_->timeline()->strip()->actions()->populate(sm, {feature("Extrude1")});
    QCOMPARE(menuTexts(sm), f);
    // group from the menu helper: one undo step, refused ranges say why
    QSignalSpy msg(list()->actions(), &app::TimelineActions::messageRequested);
    QVERIFY(list()->actions()->group({feature("Fillet2"), feature("Chamfer1")}));
    settle();
    QCOMPARE(doc().groupCount(), 4);
    QVERIFY(!list()->actions()->group({feature("Sketch1"), feature("Chamfer1")}));
    QCOMPARE(msg.count(), 1);
    doc().undoStack().undo();
    settle();
    QCOMPARE(doc().groupCount(), 3);
  }

  void insertAtRollback() {
    QSignalSpy ins(list(), &FeatureListWidget::insertRequested);
    disconnect(list(), &FeatureListWidget::insertRequested, w_.get(), nullptr);
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, center(list()->model().insertRow()));
    QCOMPARE(ins.count(), 1);
  }

  // --- toggles: sidebar side, timeline style ---------------------------------------------------
  void sidebarSideToggle() {
    auto* dock = w_->findChild<QDockWidget*>("BrowserDock");
    app::UiSettings& ui = app::UiSettings::instance();
    QCOMPARE(w_->dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QVERIFY(ui.set("ui/sidebarSide", "right"));
    settle();
    QCOMPARE(w_->dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QVERIFY(dock->isVisible());
    QVERIFY(dock->width() >= SidebarHost::kMinWidth);
    QVERIFY(dock->geometry().left() > w_->width() / 2);
    // the list works the same on the right: click, chevron, checkbox, tip on the left side
    QTest::mouseClick(list()->viewport(), Qt::LeftButton, {}, center(rowOf("Extrude1")));
    settle();
    QCOMPARE(list()->selectedItems(), std::vector<ObjectId>{feature("Extrude1")});
    QTest::mouseMove(list()->viewport(), center(rowOf("Lightening")));
    settle();
    auto* tip = w_->findChild<app::InlineTip*>("InlineTip");
    QVERIFY(tip && tip->isVisible());
    QVERIFY(tip->geometry().right() < dock->geometry().left());  // beside the sidebar, inside the window
    QTest::mouseMove(list()->viewport(), center(rowOf("Extrude3")));
    ui.reset("ui/sidebarSide");
    settle();
    QCOMPARE(w_->dockWidgetArea(dock), Qt::LeftDockWidgetArea);
  }

  void timelineStyleToggle() {
    app::UiSettings& ui = app::UiSettings::instance();
    auto* features = host()->panel(SidebarHost::Panel::Features);
    auto* strip = w_->findChild<QDockWidget*>("TimelineDock");
    QVERIFY(features->isVisible());
    QVERIFY(strip->isHidden());  // feature list is the default (decision 1)
    QVERIFY(ui.set("ui/timelineStyle", "strip"));
    settle();
    QVERIFY(!features->isVisible());
    QVERIFY(strip->isVisible());
    QVERIFY(w_->timeline()->isVisible());
    // strip: group brackets, read-only
    const auto& br = w_->timeline()->strip()->brackets();
    QCOMPARE(br.size(), size_t(3));
    // the timeline item of the panels menu follows the style
    ui.reset("ui/timelineStyle");
    settle();
    QVERIFY(features->isVisible());
    QVERIFY(strip->isHidden());
  }

  // --- persistence: ui/sidebarSplit, ui/panels ------------------------------------------------
  void collapseAndSplitPersist() {
    host()->setPanelCollapsed(SidebarHost::Panel::Model, true);
    settle();
    QCOMPARE(host()->panel(SidebarHost::Panel::Model)->height(), app::PanelHeader::kHeight);
    QCOMPARE(QSettings().value("ui/panels").toString(), QString("features:1,model:0,selection:1"));
    host()->setPanelSizes({500, 250, 150});
    QCOMPARE(QSettings().value("ui/sidebarSplit").toString(), QString("500,250,150"));
    {
      SidebarHost other;  // a new host reads the stored state
      QVERIFY(other.isPanelCollapsed(SidebarHost::Panel::Model));
      QVERIFY(!other.isPanelCollapsed(SidebarHost::Panel::Features));
    }
    // header click toggles too (and persists)
    auto* header = host()->panel(SidebarHost::Panel::Model)->header();
    QTest::mouseClick(header, Qt::LeftButton, {}, QPoint(100, 18));
    settle();
    QVERIFY(!host()->isPanelCollapsed(SidebarHost::Panel::Model));
    QCOMPARE(QSettings().value("ui/panels").toString(), QString("features:1,model:1,selection:1"));
    // garbage keeps the defaults
    QSettings().setValue("ui/sidebarSplit", "x,y");
    QSettings().setValue("ui/panels", "features:7");
    SidebarHost fresh;
    QVERIFY(!fresh.isPanelCollapsed(SidebarHost::Panel::Features));
  }

  // --- command card dock API (phase 3 contract, p2.md) ----------------------------------------
  void cardDockApi() {
    QVERIFY(host()->canDockCard());
    QSignalSpy avail(host(), &SidebarHost::cardDockAvailabilityChanged);
    w_->setBrowserVisible(false);
    QVERIFY(!host()->canDockCard());  // falls back to floating (6.4)
    w_->setBrowserVisible(true);
    QVERIFY(host()->canDockCard());
    QCOMPARE(avail.count(), 2);

    // a creating command: live row at the rollback
    list()->setPendingLive(app::FeatureListModel::PendingLive{"Extrude", "part-extrude", "25 mm"});
    QVERIFY(list()->hasLiveRow());
    QCOMPARE(row(list()->model().liveRow()).kind, Kind::Live);
    QCOMPARE(list()->model().liveRow() + 1, list()->model().rollbackRow());

    auto* card = new QWidget;
    card->setObjectName("TestCard");
    card->setMinimumSize(300, 380);
    host()->dockCard(card);
    settle();
    QCOMPARE(host()->dockedCard(), card);
    QCOMPARE(card->parentWidget(), host()->cardSlot());
    QVERIFY(host()->cardSlot()->isVisible());
    QVERIFY(list()->isCompact());
    auto* features = host()->panel(SidebarHost::Panel::Features);
    QCOMPARE(features->height(), app::PanelHeader::kHeight + list()->compactHeight());  // header + live row
    QVERIFY(card->geometry().height() >= 300);
    // a feature-selection input (filter with ShapeKind::Feature) expands FEATURES
    QSignalSpy fsel(host(), &SidebarHost::featureSelectionChanged);
    doc().selection().setFilter(core::mask(core::ShapeKind::Feature) | core::ShapeKind::Body);
    settle();
    QVERIFY(host()->featureSelectionActive());
    QVERIFY(!list()->isCompact());
    QVERIFY(features->height() > app::PanelHeader::kHeight + 4 * FeatureListWidget::kRowH);
    QVERIFY(card->isVisible());
    doc().selection().setFilter(core::kAllKinds);
    settle();
    QVERIFY(!host()->featureSelectionActive());
    QVERIFY(list()->isCompact());
    QCOMPARE(fsel.count(), 2);
    // the forced flag (card side) works the same, also when the user collapsed FEATURES
    host()->releaseCard(nullptr);
    host()->setPanelCollapsed(SidebarHost::Panel::Features, true);
    host()->setFeatureSelectionActive(true);
    settle();
    QVERIFY(!host()->isPanelCollapsed(SidebarHost::Panel::Features));
    host()->setFeatureSelectionActive(false);
    settle();
    QVERIFY(host()->isPanelCollapsed(SidebarHost::Panel::Features));
    host()->setPanelCollapsed(SidebarHost::Panel::Features, false);
    // released: FEATURES back to normal, card hidden and unparented
    QVERIFY(host()->dockedCard() == nullptr);
    QVERIFY(host()->cardSlot()->isHidden());
    QVERIFY(!list()->isCompact());
    QVERIFY(card->parentWidget() == nullptr);
    delete card;
    list()->setPendingLive(std::nullopt);
    QVERIFY(!list()->hasLiveRow());
    // drop target highlight
    host()->setDropHighlight(true);
    QVERIFY(host()->findChild<QWidget*>("CardDropHighlight")->isVisible());
    host()->setDropHighlight(false);
  }

  void liveRowFollowsEdit() {
    w_->editFeature(feature("Extrude1"));
    settle();
    QTimer::singleShot(0, [] {});
    settle();
    if (w_->toolManager().isActive()) {
      QCOMPARE(list()->model().liveRow(), rowOf("Extrude1"));
      w_->toolManager().cancel();
      settle();
      settle();
      QCOMPARE(list()->model().liveRow(), -1);
    }
  }

  // --- MODEL and SELECTION ------------------------------------------------------------------
  void modelRestyle() {
    app::BrowserTree* tree = w_->browser();
    tree->rebuildNow();
    QTreeWidgetItem* root = tree->topLevelItem(0);
    QVERIFY(root);
    QCOMPARE(root->data(0, app::BrowserTree::RoleMeta).toString(), QString("Part"));
    QTreeWidgetItem* body = nullptr;
    for (const core::Body* b : doc().bodies())
      if (!doc().bodyShape(b->id).IsNull()) body = tree->itemForKey(QString("body:%1").arg(b->id));
    QVERIFY(body);
    QCOMPARE(body->data(0, app::BrowserTree::RoleMeta).toString(), QString("Solid"));
    QCOMPARE(tree->visualItemRect(body).height(), app::BrowserTree::kRowH);
    QVERIFY(!body->icon(1).isNull());  // the eye
    QTreeWidgetItem* sketches = tree->itemForKey(QString("folder:%1:Sketches").arg(core::kRootComponentId));
    QVERIFY(sketches);
    QCOMPARE(sketches->data(0, app::BrowserTree::RoleMeta).toString(), QString::number(sketches->childCount()));
    // the master eye hides every body (one undo step) and shows them again
    auto* eye = w_->findChild<QToolButton*>("ModelMasterEye");
    QVERIFY(eye);
    eye->click();
    settle();
    for (const core::Body* b : doc().bodies()) QVERIFY(!b->visible);
    eye->click();
    settle();
    bool any = false;
    for (const core::Body* b : doc().bodies()) any = any || b->visible;
    QVERIFY(any);
  }

  void selectionPanelFacts() {
    QVERIFY(host()->selectionPanel()->facts().empty());
    core::ObjectId bodyId = core::kInvalidId;
    for (const core::Body* b : doc().bodies())
      if (!doc().bodyShape(b->id).IsNull()) bodyId = b->id;
    doc().selection().set(core::ShapeRef::body(bodyId));
    host()->selectionPanel()->refresh();
    const auto& facts = host()->selectionPanel()->facts();
    QVERIFY(!facts.empty());
    QCOMPARE(facts.front().value, QString("Solid body"));
    bool volume = false;
    for (const auto& f : facts) volume = volume || (f.key == "Volume" && f.value.contains(QChar(0x00B3)));
    QVERIFY(volume);
    QCOMPARE(app::formatGrouped(1256.6371, 2), QString("1") + QChar(0x2009) + "256.64");
    doc().selection().clear();
  }

  // --- both themes, both styles: paint without leftovers ----------------------------------------
  void paintsInBothThemesAndStyles() {
    for (const char* style : {"featureList", "strip"})
      for (auto mode : {app::ThemeManager::Mode::Dark, app::ThemeManager::Mode::Light})
        for (const char* side : {"left", "right"}) {
          app::UiSettings::instance().set("ui/timelineStyle", style);
          app::UiSettings::instance().set("ui/sidebarSide", side);
          app::ThemeManager::instance().setMode(mode, false);
          settle();
          const QImage img = host()->grab().toImage();
          QVERIFY(!img.isNull());
          const theme::Theme th = app::ThemeManager::instance().theme();
          // the sidebar background follows the theme
          const int dpr = qRound(img.devicePixelRatio());
          QCOMPARE(img.pixelColor((host()->width() / 2) * dpr, (host()->height() - 3) * dpr).rgb(),
                   theme::color(theme::Tok::SurfacePanel, th).rgb());
          if (QString(style) == "strip") QVERIFY(w_->timeline()->strip()->grab().toImage().width() > 0);
        }
    app::ThemeManager::instance().setMode(app::ThemeManager::Mode::Dark, false);
    app::UiSettings::instance().reset("ui/timelineStyle");
    app::UiSettings::instance().reset("ui/sidebarSide");
  }

  void searchAndFilter() {
    host()->featureSearch()->setVisible(true);
    host()->featureSearch()->setText("extrude");
    settle();
    int features = 0;
    for (const FeatureRow& r : list()->model().rows()) features += r.kind == Kind::Feature;
    QVERIFY(features >= 3);
    for (const FeatureRow& r : list()->model().rows())
      if (r.kind == Kind::Feature) QVERIFY(r.name.contains("Extrude", Qt::CaseInsensitive) || r.name.contains("Pocket"));
    host()->featureSearch()->clear();
    settle();
    QVERIFY(rowOf("Sketch1") > 0);
    list()->setHideSuppressed(true);
    QCOMPARE(rowOf("Pocket1"), -1);
    list()->setHideSuppressed(false);
  }
};

QTEST_MAIN(TestSidebar)
#include "test_sidebar.moc"
