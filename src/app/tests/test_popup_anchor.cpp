// Dropdown popups under their labels and right click opening a button's dropdown
// (user decisions 2026-10-23, app/PopupAnchor.h). Headless (offscreen).
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include "app/PopupAnchor.h"

using fabricad::app::PopupAnchor;
using fabricad::app::popupPosUnder;

class TestPopupAnchor : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { PopupAnchor::install(); }

  void geometryUnderLabel() {
    const QRect screen(0, 0, 1920, 1080);
    // plain: left-aligned under the label
    QCOMPARE(popupPosUnder(QRect(400, 40, 60, 70), QSize(200, 300), screen), QPoint(400, 110));
    // right-to-left: right edges line up
    QCOMPARE(popupPosUnder(QRect(400, 40, 60, 70), QSize(200, 300), screen, true), QPoint(260, 110));
    // no room below: above the label
    QCOMPARE(popupPosUnder(QRect(400, 900, 60, 70), QSize(200, 300), screen), QPoint(400, 600));
    // at the right screen edge: pushed left, still under the label
    QCOMPARE(popupPosUnder(QRect(1880, 40, 40, 30), QSize(200, 300), screen), QPoint(1720, 70));
    // a second screen to the right
    QCOMPARE(popupPosUnder(QRect(2000, 40, 60, 30), QSize(200, 300), QRect(1920, 0, 1920, 1080)), QPoint(2000, 70));
    // taller than the room above and below: kept on the screen
    QCOMPARE(popupPosUnder(QRect(400, 500, 60, 30), QSize(200, 1000), screen), QPoint(400, 80));
  }

  void menusOpenUnderTheirButton_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("button");
    QTest::newRow("instant, left") << int(QToolButton::InstantPopup) << int(Qt::LeftButton);
    QTest::newRow("instant, right") << int(QToolButton::InstantPopup) << int(Qt::RightButton);
    QTest::newRow("split, right") << int(QToolButton::MenuButtonPopup) << int(Qt::RightButton);
  }
  void menusOpenUnderTheirButton() {
    QFETCH(int, mode);
    QFETCH(int, button);
    QWidget win;
    win.resize(600, 400);
    auto* lay = new QHBoxLayout(&win);
    lay->addStretch(1);
    auto* b = new QToolButton(&win);
    b->setObjectName("Dropdown");
    b->setText("TOOL");
    b->setFixedSize(80, 60);
    auto* menu = new QMenu(b);
    menu->addAction("One");
    menu->addAction("Two");
    b->setMenu(menu);
    b->setPopupMode(QToolButton::ToolButtonPopupMode(mode));
    int clicked = 0;
    connect(b, &QToolButton::clicked, this, [&clicked] { ++clicked; });
    lay->addWidget(b, 0, Qt::AlignTop);
    lay->addStretch(1);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    bool opened = false;
    QRect placed;
    QTimer::singleShot(50, this, [&] {
      if (QApplication::activePopupWidget() == menu) {
        opened = true;
        placed = menu->geometry();
        menu->close();
      }
    });
    // InstantPopup: anywhere; split: the right click lands on the main (tool) part
    QTest::mousePress(b, Qt::MouseButton(button), Qt::NoModifier, QPoint(20, 30));
    QTest::qWait(120);
    QTest::mouseRelease(b, Qt::MouseButton(button), Qt::NoModifier, QPoint(20, 30));
    QVERIFY(opened);
    QCOMPARE(clicked, 0);  // the dropdown opened, the tool did not run
    const QRect br(b->mapToGlobal(QPoint(0, 0)), b->size());
    QCOMPARE(placed.topLeft(), QPoint(br.left(), br.bottom() + 1));
    QCOMPARE(PopupAnchor::instance()->lastPlacement().topLeft(), placed.topLeft());
  }

  void leftClickOnSplitStillRunsTheTool() {
    QWidget win;
    auto* b = new QToolButton(&win);
    b->setFixedSize(80, 60);
    auto* menu = new QMenu(b);
    menu->addAction("One");
    b->setMenu(menu);
    b->setPopupMode(QToolButton::MenuButtonPopup);
    int clicked = 0;
    connect(b, &QToolButton::clicked, this, [&clicked] { ++clicked; });
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::mouseClick(b, Qt::LeftButton, Qt::NoModifier, QPoint(20, 30));
    QCOMPARE(clicked, 1);
    QVERIFY(!menu->isVisible());
  }

  void comboListUnderTheCombo() {
    QWidget win;
    win.resize(500, 400);
    auto* c = new QComboBox(&win);
    c->addItems({"One side", "Symmetric", "Two sides"});
    c->setCurrentIndex(2);  // a style that centres the list on the current item would move it up
    c->setGeometry(100, 60, 160, 28);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    c->showPopup();
    QWidget* popup = c->view()->window();
    QVERIFY(popup && popup->isVisible());
    const QRect cr(c->mapToGlobal(QPoint(0, 0)), c->size());
    QCOMPARE(popup->geometry().top(), cr.bottom() + 1);
    QCOMPARE(popup->geometry().left(), cr.left());
    c->hidePopup();
  }
};

QTEST_MAIN(TestPopupAnchor)
#include "test_popup_anchor.moc"
