// Timeline groups from the command line (docs/SCRIPTING.md): GROUP, UNGROUP, SUPPRESS,
// UNSUPPRESS, COLLAPSE, EXPAND, RENAME group:, ROLLBACK, the group CHECKs and LIST, plus the
// committed bracket-v3 fixture (tests/e2e/bracket-v3.fcad) the redesign's GUI checks open.
// Headless through a real MainWindow (QT_QPA_PLATFORM=offscreen).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandLine.h"
#include "app/MainWindow.h"
#include "app/ScriptRunner.h"
#include "app/ScriptTools.h"
#include "core/Document.h"
#include "core/UndoStack.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace fabricad;

namespace {

void settle() {
  for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/// Three boxes side by side: Sketch1 Extrude1 Sketch2 Extrude2 Sketch3 Extrude3.
const QStringList kThreeBoxes = {
    "SKETCH XY", "RECT", "0,0", "10,10", "FINISH", "SELECT profile:@5,5,0", "EXTRUDE 10", "OK",
    "SKETCH XY", "RECT", "20,0", "30,10", "FINISH", "SELECT profile:@25,5,0", "EXTRUDE 10", "operation=New Body", "OK",
    "SKETCH XY", "RECT", "40,0", "50,10", "FINISH", "SELECT profile:@45,5,0", "EXTRUDE 10", "operation=New Body", "OK",
    "CHECK features == 6", "CHECK bodies == 3",
};

}  // namespace

class TestScriptGroups : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;

  int run(const QStringList& lines) {
    auto* r = new app::ScriptRunner(*w_, w_.get());
    r->setLines(lines, "test");
    const int code = r->run();
    r->deleteLater();
    return code;
  }
  core::Document& doc() { return w_->document(); }
  core::ObjectId featureId(const char* name) {
    for (const auto& f : doc().features())
      if (f->name() == name) return f->id();
    return core::kInvalidId;
  }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_script_groups");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->show();
    settle();
  }
  void cleanupTestCase() { w_.reset(); }

  void init() {
    for (int i = w_->tabCount() - 1; i >= 0; --i) QVERIFY(w_->closeTab(i));
    QCOMPARE(w_->tabCount(), 1);
    settle();
    QCOMPARE(run(kThreeBoxes), 0);
  }

  void groupUngroupRename() {
    QCOMPARE(run({"GROUP \"Box one\" Sketch1 feature:Extrude1", "CHECK groups == 1", "CHECK \"group:Box one\" members == 2",
                  "CHECK \"group:box ONE\" exists", "CHECK group:Nope missing", "CHECK \"group:Box one\" parent none"}),
             0);
    const core::ObjectId g = doc().findGroup("Box one");
    QVERIFY(g);
    QCOMPARE(doc().groupOf(featureId("Extrude1")), g);
    // refused: not consecutive, cutting through a group, unknown item, no name
    QCOMPARE(run({"GROUP Bad Sketch2 Sketch3"}), 1);
    QVERIFY(w_->messageText().contains("consecutive"));
    QCOMPARE(run({"GROUP Bad Extrude1 Sketch2"}), 1);
    QVERIFY(w_->messageText().contains("would split group 'Box one'"));
    QCOMPARE(run({"GROUP Bad Nothing"}), 1);
    QCOMPARE(run({"GROUP"}), 1);
    QCOMPARE(doc().groupCount(), 1);
    // nesting by name and by #id; groups as items
    QCOMPARE(run({std::format("GROUP Inner feature:#{}", featureId("Extrude1")).c_str(), "CHECK group:Inner parent \"Box one\"",
                  "GROUP Outer \"group:Box one\" Sketch2", "CHECK \"group:Box one\" parent Outer", "CHECK group:Outer members == 3"}),
             0);
    // LIST shows the tree
    QCOMPARE(run({"LIST features"}), 0);
    const QString list = QString::fromStdString(app::listText(doc(), "features"));
    QVERIFY2(list.contains("group:Outer") && list.contains("group:Inner") && list.contains("      group:Inner"), qPrintable(list));
    QCOMPARE(run({"LIST groups"}), 0);
    QVERIFY(QString::fromStdString(app::listText(doc(), "groups")).contains("features 1-3"));
    // rename + undo
    QCOMPARE(run({"RENAME group:Outer \"Left side\"", "CHECK \"group:Left side\" exists", "UNDO", "CHECK group:Outer exists",
                  "REDO", "RENAME group:Gone X"}),
             1);
    QVERIFY(doc().findGroup("Left side"));
    // ungroup: the children move up; undo restores the nesting
    QCOMPARE(run({"UNGROUP \"group:Left side\"", "CHECK groups == 2", "CHECK \"group:Box one\" parent none", "UNDO",
                  "CHECK groups == 3", "CHECK \"group:Box one\" parent \"Left side\""}),
             0);
    QCOMPARE(run({"UNGROUP Inner \"Box one\"", "CHECK groups == 1", "UNDO", "CHECK groups == 3"}), 0);  // one step for both
    QCOMPARE(run({"UNGROUP Nope"}), 1);
  }

  void groupFromTheSelection() {
    QCOMPARE(run({"SELECT feature:Sketch2 feature:Extrude2", "GROUP Middle", "CHECK group:Middle members == 2"}), 0);
    QCOMPARE(doc().groupOf(featureId("Sketch2")), doc().findGroup("Middle"));
    QCOMPARE(run({"SELECT none", "GROUP Empty"}), 1);
  }

  void suppressGroupsAndFeatures() {
    QCOMPARE(run({"GROUP Right Sketch3 Extrude3", "SUPPRESS group:Right", "CHECK group:Right suppressed all", "CHECK bodies == 2",
                  "UNDO", "CHECK group:Right suppressed none", "CHECK bodies == 3", "REDO", "CHECK bodies == 2"}),
             0);
    const int steps = doc().undoStack().index();
    QCOMPARE(run({"UNSUPPRESS Extrude3", "CHECK group:Right suppressed some"}), 0);
    QCOMPARE(doc().undoStack().index(), steps + 1);
    QCOMPARE(run({"UNSUPPRESS Right", "CHECK group:Right suppressed none", "CHECK bodies == 3"}), 0);
    // already in that state: a note, not an error
    QCOMPARE(run({"UNSUPPRESS Right"}), 0);
    QVERIFY(w_->messageText().contains("nothing to change"));
    // a mix of a group and a feature is one step
    const int before = doc().undoStack().index();
    QCOMPARE(run({"SUPPRESS Right Extrude1", "CHECK bodies == 1"}), 0);
    QCOMPARE(doc().undoStack().index(), before + 1);
    QCOMPARE(run({"UNDO", "CHECK bodies == 3", "SUPPRESS nothing"}), 1);
  }

  void suppressKeepsItsResultBesideDownstreamFailures() {
    // p1-check 3: suppressing a sketch breaks its extrude; the cell keeps the SUPPRESS result
    QVERIFY(w_->commandLine()->submit("GROUP Base Sketch1"));
    settle();
    QVERIFY(w_->commandLine()->submit("SUPPRESS group:Base"));
    settle();
    settle();
    const QString m = w_->messageText();
    QVERIFY2(m.startsWith("Suppress Base") && m.contains("Extrude1 failed"), qPrintable(m));
    QVERIFY(w_->commandLine()->submit("UNDO"));
    settle();
  }

  void collapseRollbackAndFile() {
    QCOMPARE(run({"GROUP Two Sketch2 Extrude2", "COLLAPSE Two", "CHECK group:Two collapsed", "UNDO", "CHECK group:Two expanded",
                  "REDO", "ROLLBACK Sketch2", "CHECK rollback == 3", "ROLLBACK 1", "CHECK rollback == 1", "UNDO",
                  "CHECK rollback == 3", "ROLLBACK group:Two", "CHECK rollback == 4", "ROLLBACK end", "CHECK rollback == 6"}),
             0);
    QCOMPARE(run({"ROLLBACK 99"}), 1);
    QCOMPARE(run({"EXPAND Nope"}), 1);
    QCOMPARE(run({"ROLLBACK 3", "SUPPRESS group:Two"}), 0);
    QTemporaryDir dir;
    const QString path = dir.filePath("groups.fcad");
    QCOMPARE(run({"SAVE \"" + path + "\"", "SAVE \"" + dir.filePath("other.fcad") + "\""}), 0);  // OPEN then makes a new tab
    const core::TimelineGroupState saved = doc().groupState();
    QCOMPARE(run({"OPEN \"" + path + "\"", "CHECK tabs == 2", "CHECK groups == 1", "CHECK group:Two collapsed",
                  "CHECK group:Two suppressed all", "CHECK rollback == 3"}),
             0);
    QCOMPARE(doc().groupState(), saved);
  }

  /// The fixture of the redesign's T-mockup / T-light checks (feature-list plan, item 7).
  void bracketV3Fixture() {
    const QString path = QFINDTESTDATA("../../../tests/e2e/bracket-v3.fcad");
    QVERIFY2(!path.isEmpty(), "tests/e2e/bracket-v3.fcad missing");
    QCOMPARE(run({"OPEN \"" + path + "\"", "CHECK features == 13", "CHECK groups == 3", "CHECK \"group:Base plate\" members == 3",
                  "CHECK \"group:Back wall\" members == 4", "CHECK \"group:Back wall\" collapsed", "CHECK \"group:Base plate\" expanded",
                  "CHECK group:Lightening members == 2", "CHECK group:Lightening suppressed all",
                  "CHECK \"group:Base plate\" suppressed none", "CHECK rollback == 11", "CHECK status ok", "CHECK bodies == 1",
                  "CHECK body:Bracket exists", "CHECK sketch:Sketch3 profiles == 1", "CHECK param boss_height == 25"}),
             0);
    // the rows the FEATURES panel will show
    std::vector<std::string> rows;
    for (const core::TimelineItem& it : doc().timelineItems())
      rows.push_back(std::string(static_cast<size_t>(it.depth), ' ') +
                     (it.kind == core::TimelineItem::Kind::Group ? doc().group(it.id)->name : doc().feature(it.id)->name()));
    const std::vector<std::string> expected = {"Base plate", " Sketch1", " Extrude1", " Hole1",    "Back wall", " Sketch2",
                                               " Extrude2",  " Fillet1", " Hole2",    "Lightening", " Sketch4",  " Pocket1",
                                               "Sketch3",    "Extrude3", "Fillet2",   "Chamfer1"};
    QCOMPARE(rows, expected);
    QVERIFY(!doc().isFeatureActive(featureId("Fillet2")));
    QVERIFY(doc().isFeatureActive(featureId("Extrude3")));
  }
};

QTEST_MAIN(TestScriptGroups)
#include "test_script_groups.moc"
