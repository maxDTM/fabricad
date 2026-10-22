// Headless tests of the redesign phase 3 command card (docs/plans/ui-redesign.md 5.4, 6.3, 6.4,
// 7.1): placements from UiSettings (floating right / left, docked in the sidebar slot, free),
// the sidebar fallback, drag to move with memory and drop targets, double-click back, the
// header (keycap, context, pin), the step indicator, segmented enum rows, the OverlayHost
// regions, and an Extrude end to end through the card in every placement and both themes.
// FABRICAD_CARD_SHOTS=<dir> also saves a grab of the window per placement and theme.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CardSlot.h"
#include "app/CommandCard.h"
#include "app/MainWindow.h"
#include "app/ScriptRunner.h"
#include "app/SegmentedControl.h"
#include "app/StepIndicator.h"
#include "app/ThemeManager.h"
#include "app/UiSettings.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "viewer/OverlayHost.h"
#include "viewer/ViewerWidget.h"

#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

using namespace fabricad;
using viewer::CardPlacement;

namespace {

void settle() {
  for (int i = 0; i < 4; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/// A tool with a small body and Apply/Cancel; counts accepts and activations.
class CardTool : public core::ToolBase {
 public:
  CardTool()
      : ToolBase([] {
          core::ToolInfo i;
          i.id = "test.card";
          i.name = "Card Test";
          i.shortcut = "Shift+K";
          i.tab = "SOLID";
          i.group = "CREATE";
          return i;
        }()) {}
  void activate(core::ToolContext&) override { ++activated; }
  void accept(core::ToolContext& ctx) override {
    ++accepted;
    ctx.finish();
  }
  void cancel(core::ToolContext& ctx) override { ctx.cancel(); }
  bool wantsDialogButtons() const override { return true; }
  QWidget* createDialog(core::ToolContext&, QWidget* parent) override {
    auto* w = new QWidget(parent);
    auto* l = new QVBoxLayout(w);
    auto* e = new QLineEdit("10 mm", w);
    e->setObjectName("dialogField");
    l->addWidget(e);
    return w;
  }
  int activated = 0, accepted = 0;
};

}  // namespace

class TestCommandCard : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  CardTool* tool_ = nullptr;

  app::CommandCard* card() { return w_->commandCard(); }
  QWidget* vp() { return w_->viewer(); }
  app::UiSettings& ui() { return app::UiSettings::instance(); }
  int run(const QStringList& lines) {
    app::ScriptRunner r(*w_);
    r.setLines(lines, "test");
    return r.run();
  }
  void setPlacement(const QString& p) {
    ui().set("ui/cardPlacement", p);
    settle();
  }
  void present() {
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    QVERIFY(w_->runCommand("test.card"));
    settle();
    QVERIFY(card()->isVisible());
  }
  QWidget* title() { return card()->findChild<QWidget*>("CommandDialogTitle"); }
  void send(QWidget* target, QEvent::Type type, const QPoint& global, Qt::MouseButton b, Qt::MouseButtons bs) {
    QMouseEvent ev(type, QPointF(target->mapFromGlobal(global)), QPointF(global), b, bs, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &ev);
  }
  /// Drag the card by its header so that the cursor ends at `toGlobal`.
  void drag(const QPoint& toGlobal) {
    QWidget* t = title();
    const QPoint start = t->mapToGlobal(QPoint(120, 20));
    send(t, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    const QPoint mid = (start + toGlobal) / 2;
    send(t, QEvent::MouseMove, mid, Qt::NoButton, Qt::LeftButton);
    QVERIFY(card()->isDragging());
    QVERIFY(w_->findChild<QWidget*>("CardDropTargets"));
    send(t, QEvent::MouseMove, toGlobal, Qt::NoButton, Qt::LeftButton);
    send(t, QEvent::MouseButtonRelease, toGlobal, Qt::LeftButton, Qt::NoButton);
    settle();
    QVERIFY(!card()->isDragging());
    auto* overlay = w_->findChild<QWidget*>("CardDropTargets");
    QVERIFY(!overlay || !overlay->isVisible());
  }
  QRect cardInViewport() {
    QRect r = card()->geometry();
    if (card()->parentWidget() != vp()) r.moveTopLeft(vp()->mapFromGlobal(card()->mapToGlobal(QPoint(0, 0))));
    return r;
  }
  void shot(const QString& name) {
    const QString dir = qEnvironmentVariable("FABRICAD_CARD_SHOTS");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    w_->grab().save(dir + "/" + name + ".png");
  }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_command_card");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    app::ThemeManager::instance().install(*qApp, /*readSettings=*/false);
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->resize(1600, 1000);
    w_->show();
    settle();
    auto t = std::make_unique<CardTool>();
    tool_ = t.get();
    w_->tools().registerTool(std::move(t));
  }
  void cleanupTestCase() { w_.reset(); }

  void init() {
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    for (const char* k : {"ui/cardPlacement", "ui/cardFreePos", "ui/cardRememberDrag", "ui/sidebarSide"}) ui().reset(k);
    w_->setBrowserVisible(true);
    settle();
  }

  // ---- placements ----------------------------------------------------------------------------

  void defaultsAndHooks() {
    QCOMPARE(ui().string("ui/cardPlacement"), QString("floatRight"));
    present();
    app::CommandCard* c = card();
    // kept objectNames (8.2) and the new ones
    QCOMPARE(c->objectName(), QString("CommandDialog"));
    QVERIFY(c->findChild<QWidget*>("CommandDialogTitle"));
    QVERIFY(c->findChild<QWidget*>("CommandDialogScroll"));
    QVERIFY(c->findChild<QWidget*>("CommandDialogMessage"));
    QCOMPARE(c->findChild<QPushButton*>("OkButton"), c->okButton());
    QCOMPARE(c->findChild<QPushButton*>("CancelButton"), c->cancelButton());
    QVERIFY(c->findChild<QToolButton*>("CardPin"));
    QVERIFY(c->findChild<QToolButton*>("CardClose"));
    QVERIFY(c->findChild<app::StepIndicator*>("StepIndicator"));
    QVERIFY(c->body()->findChild<QLineEdit*>("dialogField"));
    // header: keycap with the tool shortcut, workspace and document context
    auto* cap = c->findChild<QLabel*>("Keycap");
    QVERIFY(cap && cap->isVisible());
    QCOMPARE(cap->text(), QString("Shift K"));
    QVERIFY(c->contextText().startsWith("Part"));
    QVERIFY(c->contextText().contains(QString::fromStdString(w_->document().name())));
    QVERIFY(c->isLive());  // Apply/Cancel: the gold hairline
    // footer keycaps
    QCOMPARE(c->okButton()->findChild<QLabel*>("Keycap")->text(), QString("Enter"));
    QCOMPARE(c->cancelButton()->findChild<QLabel*>("Keycap")->text(), QString("Esc"));
    // floating right: 360 px visible card plus the 8 px bracket margin, at the right edge
    QCOMPARE(c->width(), app::CommandCard::kCardWidth + 16);
    QCOMPARE(c->parentWidget(), vp());
    QVERIFY(c->geometry().right() <= vp()->width() - 1);
    QVERIFY(vp()->width() - 1 - c->geometry().right() <= 16);
    QCOMPARE(c->effectivePlacement(), CardPlacement::FloatRight);
    // the close button cancels
    QTest::mouseClick(c->findChild<QToolButton*>("CardClose"), Qt::LeftButton);
    settle();
    QVERIFY(!c->isVisible());
  }

  void floatLeftAndLive() {
    present();
    setPlacement("floatLeft");
    QCOMPARE(card()->effectivePlacement(), CardPlacement::FloatLeft);
    QVERIFY(card()->x() <= 16);  // live, without restarting the command
    setPlacement("floatRight");
    QVERIFY(card()->x() > vp()->width() / 2);
  }

  void sidebarDocksAndFallsBack() {
    setPlacement("sidebar");
    present();
    auto* slot = w_->findChild<QWidget*>("CardSlot");
    QVERIFY(slot);
    QCOMPARE(card()->effectivePlacement(), CardPlacement::Sidebar);
    QVERIFY(card()->isDocked());
    QCOMPARE(card()->parentWidget(), slot);
    QVERIFY(slot->isVisible());
    QVERIFY(card()->width() >= 200);
    // sidebar hidden: floats (left, the sidebar side) without changing the setting (6.4)
    w_->setBrowserVisible(false);
    settle();
    QVERIFY(!card()->isDocked());
    QCOMPARE(card()->parentWidget(), vp());
    QCOMPARE(card()->effectivePlacement(), CardPlacement::FloatLeft);
    QCOMPARE(ui().string("ui/cardPlacement"), QString("sidebar"));
    ui().set("ui/sidebarSide", "right");
    settle();
    QCOMPARE(card()->effectivePlacement(), CardPlacement::FloatRight);
    w_->setBrowserVisible(true);
    settle();
    QVERIFY(card()->isDocked());
    // finishing the command empties the slot
    w_->toolManager().cancel();
    settle();
    QVERIFY(!card()->isDocked());
    QVERIFY(!slot->isVisible());
  }

  // ---- drag to move (6.4) -------------------------------------------------------------------

  void dragToFreeIsRemembered() {
    present();
    const QPoint target = vp()->mapToGlobal(QPoint(vp()->width() / 2, vp()->height() / 2));
    drag(target);
    QCOMPARE(ui().string("ui/cardPlacement"), QString("free"));
    const viewer::FreePos f = viewer::FreePos::fromString(ui().string("ui/cardFreePos"));
    QVERIFY(f.valid);
    QCOMPARE(f.base, CardPlacement::FloatRight);
    QCOMPARE(card()->effectivePlacement(), CardPlacement::Free);
    const QRect dropped = card()->geometry();
    QVERIFY(qAbs(dropped.left() + title()->x() + 120 - vp()->width() / 2) <= 2);  // the grab point stays under the cursor
    // the next command opens where it was left
    w_->toolManager().cancel();
    settle();
    present();
    QVERIFY((card()->geometry().topLeft() - dropped.topLeft()).manhattanLength() <= 2);
    // a resize keeps the position relative to the anchor corner
    const QSize before = w_->size();
    w_->resize(before.width() - 200, before.height());
    settle();
    const QRect vr = vp()->rect();
    QVERIFY(vr.contains(card()->geometry().topLeft()));
    const viewer::FreePos g = viewer::FreePos::fromRect(card()->geometry(), vr, CardPlacement::FloatRight);
    QVERIFY(qAbs(g.fx - f.fx) < 0.02 && qAbs(g.fy - f.fy) < 0.02);
    w_->resize(before);
    settle();
    // double-click on the header: back to the placement it came from
    QTest::mouseDClick(title(), Qt::LeftButton, {}, QPoint(100, 20));
    settle();
    QCOMPARE(ui().string("ui/cardPlacement"), QString("floatRight"));
    QVERIFY(card()->x() > vp()->width() / 2);
  }

  void dragWithoutMemory() {
    ui().set("ui/cardRememberDrag", false);
    present();
    drag(vp()->mapToGlobal(QPoint(vp()->width() / 2, vp()->height() / 3)));
    // the card stays where it was dropped, the stored placement does not change
    QCOMPARE(card()->effectivePlacement(), CardPlacement::Free);
    QCOMPARE(ui().string("ui/cardPlacement"), QString("floatRight"));
    QVERIFY(ui().string("ui/cardFreePos").isEmpty());
  }

  void dropTargets() {
    present();
    // onto the sidebar slot
    const QRect s = card()->dropTargetGlobal(CardPlacement::Sidebar);
    QVERIFY(!s.isEmpty());
    drag(s.center());
    QCOMPARE(ui().string("ui/cardPlacement"), QString("sidebar"));
    QVERIFY(card()->isDocked());
    // out of the sidebar onto the left float slot: the card's top-left lands on the slot's
    const QRect left = card()->dropTargetGlobal(CardPlacement::FloatLeft);
    QVERIFY(!left.isEmpty());
    QWidget* t = title();
    const QPoint grab = t->mapToGlobal(QPoint(120, 20));
    const QPoint grabInCard = grab - card()->mapToGlobal(card()->cardRect().topLeft());
    send(t, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
    send(t, QEvent::MouseMove, grab + QPoint(40, 40), Qt::NoButton, Qt::LeftButton);
    QVERIFY(card()->isDragging());
    QVERIFY(!card()->isDocked());  // undocked under the cursor
    const QPoint to = left.topLeft() + QPoint(std::min(grabInCard.x(), card()->width() - 76), grabInCard.y()) + QPoint(6, 6);
    send(t, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send(t, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    settle();
    QCOMPARE(ui().string("ui/cardPlacement"), QString("floatLeft"));
    QCOMPARE(card()->effectivePlacement(), CardPlacement::FloatLeft);
    QVERIFY(card()->x() <= 16);
  }

  // ---- overlay layout, pin -------------------------------------------------------------------

  void overlayHostPublishesRegions() {
    present();
    auto* host = viewer::OverlayHost::of(vp());
    QVERIFY(host);
    QRect cardRegion;
    for (const auto& r : host->occupied())
      if (r.slot == viewer::OverlayLayout::Slot::Card) cardRegion = r.rect;
    QCOMPARE(cardRegion, card()->geometry());
    // a free spot for a bubble never overlaps the card
    const QRect bubble = host->findFree(QSize(280, 120), card()->geometry().center());
    QVERIFY(bubble.isValid());
    QVERIFY(!bubble.intersects(card()->geometry()));
    w_->toolManager().cancel();
    settle();
    for (const auto& r : host->occupied()) QVERIFY(r.slot != viewer::OverlayLayout::Slot::Card);
  }

  void pinRepeatsTheCommand() {
    present();
    const int before = tool_->activated;
    card()->setPinned(true);
    QTest::mouseClick(card()->okButton(), Qt::LeftButton);
    settle();
    QCOMPARE(tool_->activated, before + 1);  // re-run after Apply
    QVERIFY(card()->isVisible());
    QVERIFY(card()->isPinned());
    // Cancel ends the pinned run
    QTest::mouseClick(card()->cancelButton(), Qt::LeftButton);
    settle();
    QVERIFY(!w_->toolManager().isActive() || w_->toolManager().activeToolId() != "test.card");
    QVERIFY(!card()->isVisible());
    present();
    QVERIFY(!card()->isPinned());
  }

  // ---- extrude end to end in every placement and both themes ---------------------------------

  void extrudeEndToEnd_data() {
    QTest::addColumn<QString>("placement");
    QTest::addColumn<bool>("light");
    for (bool light : {false, true})
      for (const char* p : {"floatRight", "floatLeft", "sidebar", "free"})
        QTest::newRow(qPrintable(QString("%1-%2").arg(p, light ? "light" : "dark"))) << QString(p) << light;
  }

  void extrudeEndToEnd() {
    QFETCH(QString, placement);
    QFETCH(bool, light);
    app::ThemeManager::instance().setMode(light ? app::ThemeManager::Mode::Light : app::ThemeManager::Mode::Dark, false);
    for (int i = w_->tabCount() - 1; i >= 0; --i) QVERIFY(w_->closeTab(i));
    if (placement == "free") {
      ui().set("ui/cardFreePos", "floatRight;tl;0.3000;0.1000");
    }
    setPlacement(placement);
    QCOMPARE(run({"SKETCH XY", "RECT", "0,0", "40,30", "FINISH", "SELECT profile:@20,15,0"}), 0);
    QVERIFY(w_->runCommand("part.extrude"));
    settle();
    app::CommandCard* c = card();
    QVERIFY(c->isVisible());
    QVERIFY(c->body());
    // placement
    const QRect r = cardInViewport();
    if (placement == "floatRight") QVERIFY(r.center().x() > vp()->width() / 2);
    if (placement == "floatLeft") QVERIFY(r.left() <= 16);
    if (placement == "sidebar") {
      QVERIFY(c->isDocked());
      QCOMPARE(c->parentWidget()->objectName(), QString("CardSlot"));
    }
    if (placement == "free") QVERIFY(qAbs(r.left() - qRound(0.3 * vp()->width())) <= 1);
    // steps: PROFILE done, DISTANCE current, CONFIRM to do
    const auto& steps = c->stepIndicator()->steps();
    QVERIFY(steps.size() >= 3);
    QCOMPARE(steps.front().state, app::StepIndicator::State::Done);
    QCOMPARE(steps.back().label, QString("Confirm"));
    QCOMPARE(c->stepIndicator()->currentStep(), static_cast<int>(steps.size()) - 2);
    QVERIFY(c->stepIndicator()->isVisible());
    // p0-check 5: the selection input is styled by the theme, not an inline blue
    auto* profiles = c->body()->findChild<QPushButton*>("prop_profiles");
    QVERIFY(profiles);
    QVERIFY(profiles->property("selectionInput").toBool());
    QVERIFY(profiles->styleSheet().isEmpty());
    // segmented rows: direction and operation; the combo stays as the model
    auto* direction = c->body()->findChild<app::SegmentedControl*>("prop_direction");
    QVERIFY(direction && direction->isVisible());
    QCOMPARE(direction->count(), 3);
    QVERIFY(!c->body()->findChild<QComboBox*>("prop_direction")->isVisible());
    QVERIFY(c->body()->findChild<QComboBox*>("prop_extentType")->isVisible());  // EXTENT stays a combo (mockup)
    shot(QString("card-%1-%2").arg(placement, light ? "light" : "dark"));
    // distance through the field, then Apply
    auto* distance = c->body()->findChild<QLineEdit*>("prop_distance");
    QVERIFY(distance);
    distance->setFocus();
    distance->selectAll();
    QTest::keyClicks(distance, "15");
    QTest::keyClick(distance, Qt::Key_Tab);
    settle();
    QTest::mouseClick(c->okButton(), Qt::LeftButton);
    settle();
    QVERIFY(!c->isVisible());
    QCOMPARE(run({"CHECK bodies == 1"}), 0);
    if (placement == "sidebar") QVERIFY(!c->isDocked());
  }

  void segmentedControlDrivesCombo() {
    for (int i = w_->tabCount() - 1; i >= 0; --i) QVERIFY(w_->closeTab(i));
    QCOMPARE(run({"SKETCH XY", "RECT", "0,0", "40,30", "FINISH", "SELECT profile:@20,15,0", "EXTRUDE 10", "OK",
                  "SKETCH XY", "RECT", "60,0", "80,30", "FINISH", "SELECT profile:@70,15,0"}),
             0);
    QVERIFY(w_->runCommand("part.extrude"));
    settle();
    auto* seg = card()->body()->findChild<app::SegmentedControl*>("prop_operation");
    QVERIFY(seg);
    auto* combo = card()->body()->findChild<QComboBox*>("prop_operation");
    QVERIFY(combo && !combo->isVisible());
    QCOMPARE(seg->count(), combo->count());
    const int nb = combo->findText("New Body");
    QVERIFY(nb >= 0);
    QTest::mouseClick(seg, Qt::LeftButton, {}, seg->cellRect(nb).center());
    QCOMPARE(combo->currentIndex(), nb);
    // and back: the combo (tools, scripts) drives the control
    combo->setCurrentIndex(0);
    QCOMPARE(seg->currentIndex(), 0);
    // keyboard: arrows move inside it
    seg->setFocus();
    QTest::keyClick(seg, Qt::Key_Right);
    QCOMPARE(combo->currentIndex(), 1);
    QTest::mouseClick(seg, Qt::LeftButton, {}, seg->cellRect(nb).center());
    // a choice that rebuilds the panel (Two Sides adds rows) keeps the keyboard in the control
    settle();  // an operation change may rebuild the panel; the card upgrades the new rows
    auto* dir = card()->body()->findChild<app::SegmentedControl*>("prop_direction");
    QVERIFY(dir);
    w_->activateWindow();
    dir->setFocus();
    settle();
    if (QApplication::focusWidget() == dir) {
      QTest::keyClick(dir, Qt::Key_Right);
      settle();
      auto* dir2 = card()->body()->findChild<app::SegmentedControl*>("prop_direction");
      QVERIFY(dir2);
      QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(dir2));
      QTest::keyClick(dir2, Qt::Key_Left);
      settle();
    }
    QCOMPARE(card()->body()->findChild<QComboBox*>("prop_direction")->currentIndex(), 0);
    QCOMPARE(card()->body()->findChild<QComboBox*>("prop_operation")->currentIndex(), nb);
    QTest::mouseClick(card()->okButton(), Qt::LeftButton);
    settle();
    QCOMPARE(run({"CHECK bodies == 2"}), 0);
  }
};

QTEST_MAIN(TestCommandCard)
#include "test_command_card.moc"
