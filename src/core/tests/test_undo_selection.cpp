#include "core/Selection.h"
#include "core/UndoStack.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace fabricad::core;

namespace {
class CounterCommand : public UndoCommand {
 public:
  CounterCommand(int& counter, int delta, int mergeId = -1) : UndoCommand("count"), c_(counter), d_(delta), m_(mergeId) {}
  void redo() override { c_ += d_; }
  void undo() override { c_ -= d_; }
  int mergeId() const override { return m_; }
  bool mergeWith(const UndoCommand& other) override {
    d_ += static_cast<const CounterCommand&>(other).d_;
    return true;
  }
 private:
  int& c_;
  int d_;
  int m_;
};
}  // namespace

class UndoSelectionTest : public QObject {
  Q_OBJECT
 private slots:
  void undoRedo() {
    UndoStack s;
    int c = 0;
    QSignalSpy idx(&s, &UndoStack::indexChanged);
    s.push(std::make_unique<CounterCommand>(c, 1));
    s.push(std::make_unique<CounterCommand>(c, 10));
    QCOMPARE(c, 11);
    QVERIFY(s.canUndo());
    QVERIFY(!s.canRedo());
    QCOMPARE(s.undoText(), std::string("count"));
    s.undo();
    QCOMPARE(c, 1);
    QVERIFY(s.canRedo());
    s.redo();
    QCOMPARE(c, 11);
    s.undo();
    s.push(std::make_unique<CounterCommand>(c, 100));  // drops redo branch
    QCOMPARE(c, 101);
    QVERIFY(!s.canRedo());
    QCOMPARE(s.count(), 2);
    QVERIFY(idx.count() >= 5);
  }

  void macroAndMerge() {
    UndoStack s;
    int c = 0;
    s.beginMacro("many");
    s.push(std::make_unique<CounterCommand>(c, 1));
    s.push(std::make_unique<CounterCommand>(c, 2));
    QVERIFY(s.inMacro());
    QVERIFY(!s.canUndo());
    s.endMacro();
    QCOMPARE(c, 3);
    QCOMPARE(s.count(), 1);
    s.undo();
    QCOMPARE(c, 0);
    s.redo();
    // merge
    s.push(std::make_unique<CounterCommand>(c, 5, 7));
    s.push(std::make_unique<CounterCommand>(c, 5, 7));
    QCOMPARE(c, 13);
    QCOMPARE(s.count(), 2);
    s.undo();
    QCOMPARE(c, 3);
    // clean tracking
    QVERIFY(!s.isClean());
    s.setClean();
    QVERIFY(s.isClean());
    s.redo();
    QVERIFY(!s.isClean());
    s.clear();
    QCOMPARE(s.count(), 0);
    QVERIFY(s.isClean());
    // empty macro adds nothing
    s.beginMacro("empty");
    s.endMacro();
    QCOMPARE(s.count(), 0);
  }

  void selectionModel() {
    SelectionModel sel;
    QSignalSpy changed(&sel, &SelectionModel::changed);
    ShapeRef f1 = ShapeRef::face(1, 1), f2 = ShapeRef::face(1, 2), e1 = ShapeRef::edge(2, 1);
    sel.add(f1);
    sel.add(f1);
    QCOMPARE(sel.count(), 1);
    QCOMPARE(changed.count(), 1);
    sel.toggle(f2);
    sel.toggle(e1);
    QCOMPARE(sel.count(), 3);
    sel.toggle(f2);
    QCOMPARE(sel.count(), 2);
    QVERIFY(sel.contains(e1));
    QCOMPARE(sel.itemsOfKind(mask(ShapeKind::Face)).size(), size_t(1));
    sel.setFilter(mask(ShapeKind::Face));  // drops the edge
    QCOMPARE(sel.count(), 1);
    sel.add(e1);  // not allowed
    QCOMPARE(sel.count(), 1);
    sel.setPreselection(e1);  // not allowed either
    QVERIFY(!sel.preselection());
    sel.setPreselection(f2);
    QVERIFY(sel.preselection() && *sel.preselection() == f2);
    sel.set({f1, f2, f2});
    QCOMPARE(sel.count(), 2);
    sel.removeOwner(1);
    QVERIFY(sel.isEmpty());
    QVERIFY(!sel.preselection());
    sel.setEnabled(false);
    sel.add(f1);
    QVERIFY(sel.isEmpty());
    sel.setEnabled(true);
    sel.add(f1);
    QVERIFY(sel.first() && *sel.first() == f1);
    sel.clear();
    QVERIFY(sel.isEmpty());
  }
};

QTEST_GUILESS_MAIN(UndoSelectionTest)
#include "test_undo_selection.moc"
