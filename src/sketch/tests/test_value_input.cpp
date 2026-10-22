// Unit tests for the on-screen value input model (ValueInputModel) and its widget overlay
// (ValueInputOverlay): typing, Tab cycling, locking, Enter/Esc semantics, command-line style
// separators, evaluation with units/expressions, and the overlay's screen placement through
// IViewerContext::worldToScreen. Headless (QT_QPA_PLATFORM=offscreen); the overlay is parented
// to a plain QWidget standing in for the viewer.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/ViewerContext.h"
#include "sketch/ValueInput.h"
#include "sketch/ValueInputOverlay.h"
#include "sketch/tools/SketchToolBase.h"

#include <QFrame>
#include <QLineEdit>
#include <QWidget>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::sketch;

namespace {

ValueInputModel lineModel() {
  ValueInputModel m;
  m.setEvaluator(makeSketchEvaluator(nullptr));  // units + expressions, no document parameters
  m.addField(ValueField{.id = "length", .label = "Length", .kind = ValueKind::Length});
  m.addField(ValueField{.id = "angle", .label = "Angle", .kind = ValueKind::Angle});
  return m;
}

/// Type a string character by character through keyPress (as the shell forwards keys).
ValueInputModel::KeyResult typeChars(ValueInputModel& m, const std::string& s) {
  ValueInputModel::KeyResult last = ValueInputModel::KeyResult::Ignored;
  for (char c : s) last = m.keyPress(static_cast<int>(std::toupper(static_cast<unsigned char>(c))), 0, std::string(1, c));
  return last;
}

}  // namespace

class ValueInputTest : public QObject {
  Q_OBJECT
 private slots:
  void fieldsAndActive() {
    ValueInputModel m = lineModel();
    QCOMPARE(m.fieldCount(), 2);
    QCOMPARE(m.activeIndex(), 0);
    QCOMPARE(m.indexOf("angle"), 1);
    QVERIFY(m.field("length"));
    QVERIFY(!m.field("nope"));
    QVERIFY(!m.hasUserInput());
    // measured values flow in while nothing is locked
    m.setMeasured("length", 25.0);
    QCOMPARE(m.value("length"), 25.0);
    QVERIFY(!m.lockedValue("length"));
    QCOMPARE(m.displayText(0), std::string("25.00"));
    QCOMPARE(m.displayText(1), std::string("0.0°"));
  }

  void typingTabAndLock() {
    ValueInputModel m = lineModel();
    int rev = m.revision();
    // letters do not start a value (they stay shortcuts); digits do
    QCOMPARE(m.keyPress(Qt::Key_L, 0, "l"), ValueInputModel::KeyResult::Ignored);
    QCOMPARE(typeChars(m, "30"), ValueInputModel::KeyResult::Consumed);
    QVERIFY(m.isTyping());
    QVERIFY(m.revision() > rev);
    QCOMPARE(m.displayText(0), std::string("30"));
    // once text exists, letters are accepted (units, parameter names)
    QCOMPARE(typeChars(m, " mm"), ValueInputModel::KeyResult::Consumed);
    QCOMPARE(m.field(0).text, std::string("30 mm"));
    // measured values are ignored while typing? no: they still update the value, the text wins for display
    m.setMeasured("length", 12.0);
    QCOMPARE(m.displayText(0), std::string("30 mm"));
    // Tab locks and moves on
    QCOMPARE(m.keyPress(Qt::Key_Tab, 0, "\t"), ValueInputModel::KeyResult::Consumed);
    QVERIFY(m.isLocked("length"));
    QCOMPARE(*m.lockedValue("length"), 30.0);
    QCOMPARE(m.activeIndex(), 1);
    // locked values do not follow the cursor any more
    m.setMeasured("length", 99.0);
    QCOMPARE(m.value("length"), 30.0);
    // Shift+Tab goes back, typing into a locked box replaces it
    QCOMPARE(m.keyPress(Qt::Key_Backtab, core::ModShift, ""), ValueInputModel::KeyResult::Consumed);
    QCOMPARE(m.activeIndex(), 0);
    typeChars(m, "4");
    QVERIFY(!m.isLocked("length"));
    QCOMPARE(m.field(0).text, std::string("4"));
    // Backspace to empty, then Backspace is not consumed (nothing to delete)
    QCOMPARE(m.keyPress(Qt::Key_Backspace, 0, ""), ValueInputModel::KeyResult::Consumed);
    QCOMPARE(m.keyPress(Qt::Key_Backspace, 0, ""), ValueInputModel::KeyResult::Ignored);
    QVERIFY(!m.hasUserInput());
  }

  void enterCommitsEscClears() {
    ValueInputModel m = lineModel();
    // Enter with nothing typed is not ours
    QCOMPARE(m.keyPress(Qt::Key_Return, 0, "\r"), ValueInputModel::KeyResult::Ignored);
    typeChars(m, "50");
    // Esc clears the text first ...
    QCOMPARE(m.keyPress(Qt::Key_Escape, 0, ""), ValueInputModel::KeyResult::Consumed);
    QVERIFY(!m.isTyping());
    // ... and a second Esc is passed on (cancel the tool)
    QCOMPARE(m.keyPress(Qt::Key_Escape, 0, ""), ValueInputModel::KeyResult::Ignored);
    typeChars(m, "50");
    m.keyPress(Qt::Key_Tab, 0, "\t");
    typeChars(m, "30");
    // Enter locks the active box and asks the tool to commit
    QCOMPARE(m.keyPress(Qt::Key_Return, 0, "\r"), ValueInputModel::KeyResult::Commit);
    QVERIFY(m.isLocked("length") && m.isLocked("angle"));
    QCOMPARE(*m.lockedValue("angle"), 30.0);
    auto locked = m.lockedFields();
    QCOMPARE(locked.size(), size_t(2));
    QCOMPARE(locked[0].id, std::string("length"));
    // Esc with locked values unlocks everything
    QCOMPARE(m.keyPress(Qt::Key_Escape, 0, ""), ValueInputModel::KeyResult::Consumed);
    QVERIFY(!m.anyLocked());
    // after a commit the tool resets the input: fields stay, text and locks go, first box active
    typeChars(m, "7");
    m.keyPress(Qt::Key_Tab, 0, "\t");
    m.resetInput();
    QCOMPARE(m.fieldCount(), 2);
    QCOMPARE(m.activeIndex(), 0);
    QVERIFY(!m.hasUserInput());
  }

  void separatorsLikeAutoCad() {
    ValueInputModel m = lineModel();
    // "50<30": '<' jumps to the angle box
    QCOMPARE(typeChars(m, "50<30"), ValueInputModel::KeyResult::Consumed);
    QVERIFY(m.isLocked("length"));
    QCOMPARE(m.activeIndex(), 1);
    QCOMPARE(m.field(1).text, std::string("30"));
    m.resetInput();
    // "30,20" with a comma; a comma inside parentheses is text
    typeChars(m, "(1+2)*max(1,2),20");
    QCOMPARE(m.field(0).text, std::string("(1+2)*max(1,2)"));
    QCOMPARE(*m.lockedValue("length"), 6.0);
    QCOMPARE(m.field(1).text, std::string("20"));
    m.resetInput();
    // a separator on an empty box is not consumed
    QCOMPARE(m.keyPress(Qt::Key_Comma, 0, ","), ValueInputModel::KeyResult::Ignored);
  }

  void invalidTextStaysInTheBox() {
    ValueInputModel m = lineModel();
    typeChars(m, "3+");
    QCOMPARE(m.keyPress(Qt::Key_Tab, 0, "\t"), ValueInputModel::KeyResult::Consumed);
    QCOMPARE(m.activeIndex(), 0);  // stayed: the text does not evaluate
    QVERIFY(!m.field(0).valid);
    QVERIFY(!m.field(0).error.empty());
    QCOMPARE(m.keyPress(Qt::Key_Return, 0, "\r"), ValueInputModel::KeyResult::Consumed);  // no commit either
    typeChars(m, "1");
    QVERIFY(m.field(0).valid);  // typing clears the error state
    QCOMPARE(m.keyPress(Qt::Key_Return, 0, "\r"), ValueInputModel::KeyResult::Commit);
    QCOMPARE(*m.lockedValue("length"), 4.0);
  }

  void visibilityPhases() {
    ValueInputModel m;
    m.addField(ValueField{.id = "length", .kind = ValueKind::Length});
    m.addField(ValueField{.id = "width", .kind = ValueKind::Length});
    m.setVisible("width", false);
    QCOMPARE(m.visibleCount(), 1);
    typeChars(m, "40");
    m.keyPress(Qt::Key_Tab, 0, "\t");
    QCOMPARE(m.activeIndex(), 0);  // only one visible box: Tab stays
    // phase change: hide the first, show the second -> the active box follows
    m.setVisible("length", false);
    m.setVisible("width", true);
    QCOMPARE(m.activeIndex(), 1);
    QVERIFY(m.isLocked("length"));  // hidden boxes keep their lock until the commit
    typeChars(m, "10");
    QCOMPARE(m.keyPress(Qt::Key_Return, 0, "\r"), ValueInputModel::KeyResult::Commit);
    QCOMPARE(m.lockedFields().size(), size_t(2));
    QVERIFY(m.summary().find("10") != std::string::npos);
  }

  void countFieldsAndEvaluator() {
    ValueInputModel m;
    m.addField(ValueField{.id = "sides", .kind = ValueKind::Count});
    std::string err;
    QVERIFY(!m.lock("sides", "6.5", &err));
    QVERIFY(m.lock("sides", "8", &err));
    QCOMPARE(*m.lockedValue("sides"), 8.0);
    // document evaluator: units, expressions and parameters
    core::Document doc;
    doc.setParameter("w", "40 mm");
    ValueInputModel d;
    d.setEvaluator(makeSketchEvaluator(&doc));
    d.setFormatter(makeUnitFormatter(&doc));
    d.addField(ValueField{.id = "length", .kind = ValueKind::Length});
    d.addField(ValueField{.id = "angle", .kind = ValueKind::Angle});
    QVERIFY(d.lock("length", "1 in", &err));
    QCOMPARE(*d.lockedValue("length"), 25.4);
    QVERIFY(d.lock("length", "w/2 + 3", &err));
    QCOMPARE(*d.lockedValue("length"), 23.0);
    QVERIFY(d.lock("angle", "45 + 45", &err));
    QVERIFY(std::abs(*d.lockedValue("angle") - 90.0) < 1e-9);
    QVERIFY(!d.lock("length", "nope", &err));
    QVERIFY(!err.empty());
    d.resetInput();
    d.setMeasured("length", 12.5);
    QCOMPARE(d.displayText(0).rfind("12.5", 0), size_t(0));  // "12.5..." with the unit appended
    QVERIFY(d.displayText(0).find("mm") != std::string::npos);
  }

  void overlayWidgetsFollowTheModel() {
    QWidget viewport;
    viewport.resize(800, 600);
    core::NullViewerContext viewer;  // 1 mm per pixel, origin at the widget centre (see NullViewerContext)
    ValueInputModel m = lineModel();
    ValueInputOverlay overlay(m, &viewer, &viewport);
    m.setChangedCallback([&overlay] { overlay.sync(); });
    QVERIFY(overlay.hasWidgets());
    QCOMPARE(overlay.boxCount(), 2);
    // no anchor yet: hidden
    QVERIFY(!overlay.frameAt(0)->isVisible() || !viewport.isVisible());
    viewport.show();
    m.setAnchor("length", core::Vec3{10, -20, 0});
    m.setAnchor("angle", core::Vec3{10, -20, 0});
    m.setMeasured("length", 42.0);
    QVERIFY(overlay.frameAt(0)->isVisible());
    QVERIFY(overlay.frameAt(1)->isVisible());
    QCOMPARE(overlay.editAt(0)->text(), QString("42.00"));
    // placement: worldToScreen of the anchor + the cursor offset; the second box stacks below the first
    double sx = 0, sy = 0;
    QVERIFY(viewer.worldToScreen(gp_Pnt(10, -20, 0), sx, sy));
    QFrame* f0 = overlay.frameAt(0);
    QFrame* f1 = overlay.frameAt(1);
    QCOMPARE(f0->x(), static_cast<int>(sx) + 18);
    QCOMPARE(f0->y(), static_cast<int>(sy) + 14);
    QCOMPARE(f1->x(), f0->x());
    QVERIFY(f1->y() >= f0->y() + f0->height());
    // typing updates the box text and the lock mark appears on Tab
    m.keyPress(Qt::Key_3, 0, "3");
    m.keyPress(Qt::Key_0, 0, "0");
    QCOMPARE(overlay.editAt(0)->text(), QString("30"));
    m.keyPress(Qt::Key_Tab, 0, "\t");
    QVERIFY(m.isLocked("length"));
    // styled by the theme QSS through properties (no inline colours): idle + locked, active
    QCOMPARE(overlay.frameAt(0)->property("state").toString(), QString("idle"));
    QVERIFY(overlay.frameAt(0)->property("locked").toBool());
    QCOMPARE(overlay.frameAt(1)->property("state").toString(), QString("active"));
    QVERIFY(overlay.frameAt(0)->styleSheet().isEmpty());
    QVERIFY(overlay.frameAt(0)->findChild<QFrame*>("fcValueField"));
    // editing the widget directly pushes text into the model
    overlay.editAt(1)->setText("45");
    emit overlay.editAt(1)->textEdited("45");
    QCOMPARE(m.field(1).text, std::string("45"));
    // hiding a field hides its box; clamping keeps boxes inside the viewport
    m.setVisible("angle", false);
    QVERIFY(!overlay.frameAt(1)->isVisible());
    m.setAnchor("length", core::Vec3{5000, -5000, 0});
    QVERIFY(f0->x() + f0->width() <= viewport.width());
    QVERIFY(f0->y() + f0->height() <= viewport.height());
    overlay.setVisible(false);
    QVERIFY(!f0->isVisible());
  }

  void boxesKeepClearOfOverlays() {
    // p4-check M3: the box must not cover the command card when its handle sits behind it
    const QRect bounds(0, 0, 1000, 700);
    const QSize size(130, 46);
    const QRect card(600, 100, 360, 500);
    // free spot: right / below the anchor, as before
    QCOMPARE(ValueInputOverlay::placeClear({200, 200}, size, {18, 14}, {card}, bounds).topLeft(), QPoint(218, 214));
    // right of the anchor is the card: the box flips to the left of the anchor
    QRect r = ValueInputOverlay::placeClear({560, 300}, size, {18, 14}, {card}, bounds);
    QVERIFY(!r.intersects(card));
    QCOMPARE(r.right(), 560 - 18 - 1);
    // the anchor is behind the card: the box goes beside the card, near its first choice
    r = ValueInputOverlay::placeClear({700, 300}, size, {18, 14}, {card}, bounds);
    QVERIFY(!r.intersects(card));
    QVERIFY(bounds.contains(r));
    QCOMPARE(r.right(), card.left() - 8 - 1);
    QCOMPARE(r.top(), 314);

    // published keep-out rects move a live overlay's box, and the leader ties it to the anchor
    QWidget viewport;
    viewport.resize(800, 600);
    core::NullViewerContext viewer;
    ValueInputModel m = lineModel();
    ValueInputOverlay overlay(m, &viewer, &viewport);
    m.setChangedCallback([&overlay] { overlay.sync(); });
    viewport.show();
    m.setVisible("angle", false);
    m.setAnchor("length", core::Vec3{0, 0, 0});
    double sx = 0, sy = 0;
    QVERIFY(viewer.worldToScreen(gp_Pnt(0, 0, 0), sx, sy));
    const QRect keep(static_cast<int>(sx) + 10, static_cast<int>(sy) - 100, 300, 300);
    viewport.setProperty("fcOverlayKeepOut", QVariantList{keep});
    overlay.reposition();
    QVERIFY(!overlay.frameAt(0)->geometry().intersects(keep));
    QVERIFY(overlay.leader() && overlay.leader()->isVisible());
    QCOMPARE(overlay.leader()->from(), QPoint(static_cast<int>(sx), static_cast<int>(sy)));
    QVERIFY(overlay.frameAt(0)->geometry().adjusted(-1, -1, 1, 1).contains(overlay.leader()->to()));
    overlay.setVisible(false);
    QVERIFY(!overlay.leader()->isVisible());
  }
};

QTEST_MAIN(ValueInputTest)
#include "test_value_input.moc"
