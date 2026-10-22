// Tests for the layout toggles of the redesign that phase 4 applies on the app side
// (docs/plans/ui-redesign.md 6.1 toggles 2 and 3, 6.2, 6.3, 6.4): the main toolbar at the top,
// left or right (the vertical MainToolbar with section captions, live switch), and the floating
// quick toolbar in the model view (off / left / right) placed through the OverlayLayout.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandCard.h"
#include "app/MainToolbar.h"
#include "app/MainWindow.h"
#include "app/QuickToolbar.h"
#include "app/ScriptRunner.h"
#include "app/ThemeManager.h"
#include "app/UiSettings.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "viewer/OverlayHost.h"
#include "viewer/ViewerWidget.h"

#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

using namespace fabricad;
using viewer::OverlayLayout;

namespace {
void settle() {
  for (int i = 0; i < 4; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

int visibleToolButtons(const QWidget* root) {
  int n = 0;
  for (const QToolButton* b : root->findChildren<QToolButton*>("RibbonTool"))
    if (b->isVisibleTo(root)) ++n;
  return n;
}
}  // namespace

class TestLayoutToggles : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  app::UiSettings& ui() { return app::UiSettings::instance(); }
  int run(const QStringList& lines) {
    app::ScriptRunner r(*w_);
    r.setLines(lines, "test");
    return r.run();
  }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_layout_toggles");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    app::ThemeManager::instance().install(*qApp, /*readSettings=*/false);
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->resize(1600, 1000);
    w_->show();
    settle();
  }
  void cleanupTestCase() { w_.reset(); }
  void init() {
    for (const char* k : {"ui/toolbarArea", "ui/quickToolbar", "ui/cardPlacement", "ui/sidebarSide"}) ui().reset(k);
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    w_->resize(1600, 1000);
    settle();
  }

  // ---- ui/toolbarArea -------------------------------------------------------------------------

  void toolbarTopByDefault() {
    QToolBar* host = w_->mainToolbarHost();
    QCOMPARE(w_->toolBarArea(host), Qt::TopToolBarArea);
    QCOMPARE(w_->mainToolbar()->orientation(), Qt::Horizontal);
    QCOMPARE(w_->mainToolbar()->height(), 70);
  }

  void toolbarLeftAndRightLive() {
    app::MainToolbar* tb = w_->mainToolbar();
    const int horizontalButtons = static_cast<int>(tb->findChildren<QToolButton*>("RibbonTool").size());
    for (const char* side : {"left", "right"}) {
      QVERIFY(ui().set("ui/toolbarArea", side));
      settle();
      QCOMPARE(w_->toolBarArea(w_->mainToolbarHost()), QString(side) == "left" ? Qt::LeftToolBarArea : Qt::RightToolBarArea);
      QCOMPARE(tb->orientation(), Qt::Vertical);
      QCOMPARE(tb->width(), app::MainToolbar::kVerticalWidth);
      QVERIFY2(tb->height() > 500, qPrintable(QString::number(tb->height())));
      QCOMPARE(w_->mainToolbarHost()->property("area").toString(), QString(side));
      // the same sections and hooks: captions with toolbarTab / toolbarGroup, buttons with toolId
      const auto sections = tb->sectionWidgets();
      QVERIFY(sections.size() >= 6);
      for (QWidget* s : sections) {
        QVERIFY(s->property("vertical").toBool());
        if (s->property("finish").toBool()) continue;
        auto* caption = s->findChild<QToolButton*>("RibbonGroupLabel");
        QVERIFY(caption);
        QVERIFY(!caption->property("toolbarTab").toString().isEmpty());
        // caption bar above the icon column (5.1)
        if (auto* tool = s->findChild<QToolButton*>("RibbonTool"); tool && tool->isVisibleTo(s))
          QVERIFY(caption->geometry().bottom() < tool->mapTo(s, QPoint(0, 0)).y());
      }
      QVERIFY(static_cast<int>(tb->findChildren<QToolButton*>("RibbonTool").size()) >= horizontalButtons - 2);
      bool extrude = false;
      for (QToolButton* b : tb->findChildren<QToolButton*>("RibbonTool"))
        if (b->property("toolId").toString() == "part.extrude" && b->isVisibleTo(tb)) extrude = true;
      QVERIFY(extrude);
      // a click on a vertical button runs the tool
      for (QToolButton* b : tb->findChildren<QToolButton*>("RibbonTool"))
        if (b->property("toolId").toString() == "sketch.create" && b->isVisibleTo(tb)) {
          b->click();
          settle();
          QCOMPARE(QString::fromStdString(w_->toolManager().activeToolId()), QString("sketch.create"));
          w_->toolManager().cancel();
          settle();
        }
    }
    // back to the top, live
    QVERIFY(ui().set("ui/toolbarArea", "top"));
    settle();
    QCOMPARE(w_->toolBarArea(w_->mainToolbarHost()), Qt::TopToolBarArea);
    QCOMPARE(tb->orientation(), Qt::Horizontal);
    QCOMPARE(tb->height(), 70);
  }

  void verticalToolbarFoldsWhenShort() {
    QVERIFY(ui().set("ui/toolbarArea", "left"));
    settle();
    app::MainToolbar* tb = w_->mainToolbar();
    const int tall = visibleToolButtons(tb);
    w_->resize(1600, 520);
    settle();
    const int shortCount = visibleToolButtons(tb);
    QVERIFY2(shortCount < tall, qPrintable(QString("%1 vs %2").arg(shortCount).arg(tall)));
    // nothing is cut: every visible section fits the column
    for (QWidget* s : tb->sectionWidgets())
      if (s->isVisibleTo(tb)) QVERIFY(s->mapTo(tb, QPoint(0, s->height())).y() <= tb->height() + 1);
    // folded tools are still in their caption dropdowns
    bool inMenu = false;
    for (QToolButton* c : tb->findChildren<QToolButton*>("RibbonGroupLabel"))
      if (c->menu())
        for (QAction* a : c->menu()->actions())
          if (a->property("toolId").toString() == "part.fillet") inMenu = true;
    QVERIFY(inMenu);
    // the window did not grow to fit the toolbar
    QVERIFY(w_->height() <= 560);
  }

  void verticalSketchPage() {
    QVERIFY(ui().set("ui/toolbarArea", "right"));
    settle();
    QVERIFY(w_->newDocument());
    QCOMPARE(run({"SKETCH XY"}), 0);
    settle();
    app::MainToolbar* tb = w_->mainToolbar();
    QCOMPARE(QString::fromStdString(tb->currentTab()), QString("SKETCH"));
    auto* finish = tb->findChild<QToolButton*>("FinishSketchButton");
    QVERIFY(finish && finish->isVisibleTo(tb));
    QVERIFY(finish->width() <= app::MainToolbar::kVerticalWidth);
    QCOMPARE(run({"FINISH"}), 0);
    settle();
  }

  // ---- ui/quickToolbar ------------------------------------------------------------------------

  void quickToolbarOffByDefault() {
    QCOMPARE(ui().string("ui/quickToolbar"), QString("off"));
    app::QuickToolbar* q = w_->quickToolbar();
    QVERIFY(!q || !q->isVisible());
  }

  void quickToolbarSidesAndContents() {
    viewer::ViewerWidget* vp = w_->viewer();
    QVERIFY(ui().set("ui/quickToolbar", "left"));
    settle();
    app::QuickToolbar* q = w_->quickToolbar();
    QVERIFY(q && q->isVisible());
    QCOMPARE(q->parentWidget(), static_cast<QWidget*>(vp));
    QCOMPARE(q->width(), app::QuickToolbar::kWidth);
    QVERIFY(q->x() <= 24);
    // repeat + the six most-used (defaults until commands are used)
    QCOMPARE(static_cast<int>(q->toolIds().size()), 6);
    QVERIFY(std::find(q->toolIds().begin(), q->toolIds().end(), "part.extrude") != q->toolIds().end());
    QVERIFY(q->findChild<QToolButton*>("QuickRepeat"));
    // registered with the viewport's OverlayLayout (6.3 priority 6)
    const QRect region = viewer::OverlayHost::of(vp)->layout().rect(OverlayLayout::Slot::QuickToolbar);
    QCOMPARE(region, q->geometry());
    QVERIFY(ui().set("ui/quickToolbar", "right"));
    settle();
    QVERIFY(q->x() > vp->width() / 2);
    // a button runs its command
    for (QToolButton* b : q->findChildren<QToolButton*>("QuickTool"))
      if (b->property("toolId").toString() == "sketch.create") {
        b->click();
        settle();
        QCOMPARE(QString::fromStdString(w_->toolManager().activeToolId()), QString("sketch.create"));
        w_->toolManager().cancel();
      }
    QVERIFY(ui().set("ui/quickToolbar", "off"));
    settle();
    QVERIFY(!q->isVisible());
  }

  void quickToolbarBesideTheCard() {
    // 6.4: quick toolbar right + card floatRight: from the edge, quick toolbar then card
    QVERIFY(ui().set("ui/quickToolbar", "right"));
    QVERIFY(ui().set("ui/cardPlacement", "floatRight"));
    settle();
    QVERIFY(w_->newDocument());
    QCOMPARE(run({"SKETCH XY", "RECT", "0,0", "40,30", "FINISH", "SELECT profile:@20,15,0", "EXTRUDE 10"}), 0);
    settle();
    app::CommandCard* card = w_->commandCard();
    app::QuickToolbar* q = w_->quickToolbar();
    QVERIFY(card->isVisible() && q->isVisible());
    QVERIFY(!card->geometry().adjusted(8, 8, -8, -8).intersects(q->geometry()));
    QVERIFY2(card->geometry().right() - 8 < q->geometry().left(),
             qPrintable(QString("card %1, quick %2").arg(card->geometry().right()).arg(q->geometry().left())));
    // left side: the card on the left sits right of the strip
    QVERIFY(ui().set("ui/quickToolbar", "left"));
    QVERIFY(ui().set("ui/cardPlacement", "floatLeft"));
    settle();
    QVERIFY(card->geometry().left() + 8 > q->geometry().right());
    w_->toolManager().cancel();
    settle();
    w_->document().setModified(false);
  }

  void quickToolbarInSketch() {
    QVERIFY(ui().set("ui/quickToolbar", "right"));
    settle();
    QVERIFY(w_->newDocument());
    QCOMPARE(run({"SKETCH XY"}), 0);
    settle();
    app::QuickToolbar* q = w_->quickToolbar();
    QVERIFY(q->isSketching());
    QVERIFY(std::find(q->toolIds().begin(), q->toolIds().end(), "sketch.line") != q->toolIds().end());
    auto* finish = q->findChild<QToolButton*>("FinishSketchButton");
    QVERIFY(finish && finish->isVisible());
    QCOMPARE(run({"FINISH"}), 0);
    settle();
    QVERIFY(!q->isSketching());
    QVERIFY(!finish->isVisible());
  }

  void quickToolbarSurvivesClosingItsTab() {
    QVERIFY(ui().set("ui/quickToolbar", "right"));
    settle();
    QVERIFY(w_->newDocument());
    settle();
    QVERIFY(w_->quickToolbar() && w_->quickToolbar()->parentWidget() == w_->viewer());
    QVERIFY(w_->closeTab(w_->activeTab()));  // its viewport goes, and the strip with it
    settle();
    QVERIFY(w_->quickToolbar() && w_->quickToolbar()->isVisible());
    QCOMPARE(w_->quickToolbar()->parentWidget(), static_cast<QWidget*>(w_->viewer()));
  }

  void pickQuickToolsByUsage() {
    core::ToolRegistry& reg = w_->tools();
    app::ToolUsage usage;
    usage["part.chamfer"] = 5;
    usage["part.fillet"] = 9;
    usage["sketch.line"] = 50;  // a sketch tool does not count for the model strip
    usage["view.fit"] = 99;     // view commands are not commands
    const auto ids = app::pickQuickTools(reg, core::Workspace::Design, false, usage);
    QCOMPARE(static_cast<int>(ids.size()), 6);
    QCOMPARE(ids[0], std::string("part.fillet"));
    QCOMPARE(ids[1], std::string("part.chamfer"));
    QVERIFY(std::find(ids.begin(), ids.end(), "sketch.line") == ids.end());
    QVERIFY(std::find(ids.begin(), ids.end(), "view.fit") == ids.end());
    const auto sk = app::pickQuickTools(reg, core::Workspace::Design, true, usage);
    QCOMPARE(sk.front(), std::string("sketch.line"));
  }
};

QTEST_MAIN(TestLayoutToggles)
#include "test_layout_toggles.moc"
