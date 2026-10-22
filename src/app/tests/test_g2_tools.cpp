// Headless regression tests for the G2 app-tool fixes (QT_QPA_PLATFORM=offscreen, no GL):
// parameter rename/remove validation (APP-01/02/05), units command with undo (APP-03/09),
// Appearance as a tool (APP-04), command-line Enter / ':' / Ctrl+Z (APP-06/07/08), command
// search ranking (APP-10) and the TOOLS > UTILITIES group (APP-03).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppActions.h"
#include "app/CommandLine.h"
#include "app/CommandSearch.h"
#include "app/MainWindow.h"
#include "app/ParametersDialog.h"
#include "app/PreferencesDialog.h"
#include "app/ToolbarModel.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QtTest>

#include <algorithm>

using namespace fabricad;

namespace {

class StepTool : public core::ToolBase {
 public:
  StepTool()
      : ToolBase([] {
          core::ToolInfo i;
          i.id = "test.g2-step";
          i.name = "G2 Step Test";
          i.aliases = {"G2STEP"};
          return i;
        }()) {}
  void activate(core::ToolContext&) override { ++activated; }
  void accept(core::ToolContext& ctx) override {
    ++accepted;
    ctx.finish();
  }
  void cancel(core::ToolContext& ctx) override {
    ++cancelled;
    ctx.cancel();
  }
  int activated = 0, accepted = 0, cancelled = 0;
};

core::ObjectId addBox(core::Document& doc, const std::string& dxExpr = {}) {
  auto f = std::make_unique<core::test::BoxFeature>();
  if (!dxExpr.empty()) f->prop("dx").setExpression(dxExpr);
  auto cmd = std::make_unique<core::AddFeatureCommand>(doc, std::move(f));
  core::AddFeatureCommand* raw = cmd.get();
  doc.undoStack().push(std::move(cmd));
  return raw->featureId();
}

void settle() {
  for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

std::string expr(const core::Document& d, const std::string& n) {
  const auto* p = d.parameter(n);
  return p ? p->expression : "<none>";
}

}  // namespace

class TestG2Tools : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  StepTool* tool_ = nullptr;
  core::Document& doc() { return w_->document(); }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_g2_tools");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    core::test::registerTestFeatures();
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->show();
    settle();
    auto t = std::make_unique<StepTool>();
    tool_ = t.get();
    w_->tools().registerTool(std::move(t));
  }
  void cleanupTestCase() { w_.reset(); }
  void init() {
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    settle();
  }

  // --- APP-01 / APP-02 / APP-05 -------------------------------------------------------------
  void renameRejectsDuplicateAndInvalid() {
    doc().setParameter("a", "10");
    doc().setParameter("b", "20");
    doc().recompute();
    app::ParametersDialog dlg(doc());
    QTableWidget* t = dlg.table();
    auto rowOf = [&](const QString& n) {
      for (int r = 0; r < t->rowCount(); ++r)
        if (t->item(r, 0)->text() == n) return r;
      return -1;
    };
    // duplicate
    t->item(rowOf("a"), 0)->setText("b");
    settle();
    QCOMPARE(expr(doc(), "a"), std::string("10"));
    QCOMPARE(expr(doc(), "b"), std::string("20"));
    QVERIFY2(dlg.errorText().contains("already exists"), qPrintable(dlg.errorText()));
    QVERIFY(rowOf("a") >= 0);  // the old name is back in the table
    // invalid identifiers / unit names
    for (const char* bad : {"my var", "mm", "1x"}) {
      t->item(rowOf("a"), 0)->setText(bad);
      settle();
      QCOMPARE(expr(doc(), "a"), std::string("10"));
      QVERIFY2(dlg.errorText().contains("Invalid name"), qPrintable(dlg.errorText()));
      QVERIFY(rowOf("a") >= 0);
    }
    // a valid rename still works and clears the message
    t->item(rowOf("a"), 0)->setText("width");
    settle();
    QCOMPARE(expr(doc(), "width"), std::string("10"));
    QVERIFY(!doc().parameter("a"));
    QVERIFY(dlg.errorText().isEmpty());
    doc().removeParameter("width");
    doc().removeParameter("b");
  }

  void setParameterCommandIgnoresInvalidName() {
    const size_t before = doc().parameters().size();
    doc().undoStack().push(std::make_unique<core::SetParameterCommand>(doc(), "my var", "3"));
    QCOMPARE(doc().parameters().size(), before);
    doc().undoStack().undo();
    QCOMPARE(doc().parameters().size(), before);
  }

  void inUseParameterCannotBeRemovedOrRenamed() {
    doc().setParameter("len", "10");
    doc().setParameter("half", "len/2");
    const core::ObjectId box = addBox(doc(), "half");
    const std::string boxName = doc().feature(box)->name();
    const auto users = app::parameterUsers(doc(), "len");
    QCOMPARE(users, std::vector<std::string>{"half"});
    QCOMPARE(app::parameterUsers(doc(), "half"), std::vector<std::string>{boxName});

    app::ParametersDialog dlg(doc());
    QTableWidget* t = dlg.table();
    int lenRow = -1, halfRow = -1;
    for (int r = 0; r < t->rowCount(); ++r) {
      if (t->item(r, 0)->text() == "len") lenRow = r;
      if (t->item(r, 0)->text() == "half") halfRow = r;
    }
    dlg.removeRow(lenRow);
    QVERIFY(doc().parameter("len"));
    QVERIFY2(dlg.errorText().contains("half"), qPrintable(dlg.errorText()));
    dlg.removeRow(halfRow);
    QVERIFY(doc().parameter("half"));
    QVERIFY2(dlg.errorText().contains(QString::fromStdString(boxName)), qPrintable(dlg.errorText()));
    // rename of a used parameter is refused too
    t->item(lenRow, 0)->setText("length");
    settle();
    QVERIFY(doc().parameter("len"));
    QVERIFY(!doc().parameter("length"));
    // SET len= (remove) from the command line is refused as well
    QVERIFY(w_->commandLine()->submit("SET len="));
    QVERIFY(doc().parameter("len"));
    QVERIFY(w_->commandLine()->submit("SET my var=3"));
    QVERIFY(!doc().parameter("my var"));
    doc().undoStack().undo();  // the box
    QVERIFY(app::parameterUsers(doc(), "half").empty());
    doc().removeParameter("half");
    doc().removeParameter("len");
  }

  // --- APP-03 / APP-09: units ------------------------------------------------------------------
  void unitsCommandKeepsGeometryAndUndoes() {
    doc().setParameter("w", "10");
    doc().setParameter("count", "4");  // scalar, not used as a length: left alone
    const core::ObjectId box = addBox(doc(), "w");
    QCOMPARE(doc().feature(box)->prop("dx").toDouble(), 10.0);
    QVERIFY(w_->commandLine()->submit("UNITS in"));
    QCOMPARE(doc().units().length, core::LengthUnit::Inch);
    QCOMPARE(expr(doc(), "w"), std::string("10 mm"));
    QCOMPARE(expr(doc(), "count"), std::string("4"));
    QCOMPARE(doc().feature(box)->prop("dx").toDouble(), 10.0);
    doc().undoStack().undo();
    QCOMPARE(doc().units().length, core::LengthUnit::Millimeter);
    QCOMPARE(expr(doc(), "w"), std::string("10"));
    doc().undoStack().redo();
    QCOMPARE(doc().units().length, core::LengthUnit::Inch);
    doc().undoStack().undo();
    QVERIFY(w_->commandLine()->submit("UNITS mm rad"));
    QCOMPARE(doc().units().angle, core::AngleUnit::Radian);
    doc().undoStack().undo();
    QCOMPARE(doc().units().angle, core::AngleUnit::Degree);
    doc().undoStack().undo();  // box
    doc().removeParameter("w");
    doc().removeParameter("count");
  }

  void unitsWithoutArgumentOpensPreferences() {
    QVERIFY(w_->runCommand("app.units"));
    settle();
    auto* dlg = w_->findChild<app::PreferencesDialog*>();
    QVERIFY(dlg && dlg->isVisible());
    QCOMPARE(dlg->currentPage(), app::PreferencesDialog::Page::Document);
    // the Document page changes the active design (one undo step)
    dlg->documentLengthBox()->setCurrentIndex(dlg->documentLengthBox()->findText("cm"));
    QCOMPARE(doc().units().length, core::LengthUnit::Centimeter);
    doc().undoStack().undo();
    QCOMPARE(doc().units().length, core::LengthUnit::Millimeter);
    QCOMPARE(dlg->documentLengthBox()->currentText(), QString("mm"));  // follows undo
    dlg->close();
    settle();
  }

  void toolsTabHasUtilities() {
    const auto model = app::buildToolbarModel(w_->tools(), core::Workspace::Design);
    auto tab = std::find_if(model.begin(), model.end(), [](const app::ToolbarTab& t) { return t.name == "TOOLS"; });
    QVERIFY(tab != model.end());
    QVERIFY(tab->groups.size() >= 2);
    QCOMPARE(tab->groups.front().name, std::string("UTILITIES"));
    std::vector<std::string> ids;
    for (core::ITool* t : tab->groups.front().all) ids.push_back(t->info().id);
    QVERIFY(std::find(ids.begin(), ids.end(), "app.parameters") != ids.end());
    QVERIFY(std::find(ids.begin(), ids.end(), "app.preferences") != ids.end());
    // Parameters stays in SOLID > MODIFY as well, next to Appearance
    auto solid = std::find_if(model.begin(), model.end(), [](const app::ToolbarTab& t) { return t.name == "SOLID"; });
    QVERIFY(solid != model.end());
    bool params = false, appearance = false;
    for (const auto& g : solid->groups)
      if (g.name == "MODIFY")
        for (core::ITool* t : g.all) {
          params |= t->info().id == "app.parameters";
          appearance |= t->info().id == "app.appearance";
        }
    QVERIFY(params && appearance);
  }

  // --- APP-04: appearance --------------------------------------------------------------------------
  void appearanceTool() {
    core::ITool* t = w_->tools().byShortcut("A");
    QVERIFY(t);
    QCOMPARE(t->info().id, std::string("app.appearance"));
    const core::ObjectId box = addBox(doc());
    QVERIFY(!doc().bodies().empty());
    const core::ObjectId body = doc().bodies().back()->id;
    const auto before = doc().body(body)->color;
    // nothing selected: message, no change
    doc().selection().clear();
    QVERIFY(w_->runCommand("app.appearance", {"#ff0000"}));
    QCOMPARE(doc().body(body)->color, before);
    doc().selection().set({core::ShapeRef::body(body)});
    QVERIFY(w_->runCommand("app.appearance", {"#ff0000"}));
    QCOMPARE(doc().body(body)->color[0], 1.0f);
    QCOMPARE(doc().body(body)->color[1], 0.0f);
    // a face maps to its body
    QCOMPARE(app::appearanceTargets(doc(), {core::ShapeRef::face(body, 1)}), std::vector<core::ObjectId>{body});
    doc().undoStack().undo();
    QCOMPARE(doc().body(body)->color, before);
    doc().selection().clear();
    doc().undoStack().undo();
    QVERIFY(!doc().feature(box));
  }

  // --- APP-06 / APP-07 / APP-08: command line ------------------------------------------------------
  void emptyEnterGoesToActiveTool() {
    app::CommandLine* cl = w_->commandLine();
    QVERIFY(cl->submit("G2STEP"));
    QVERIFY(w_->toolManager().isActive());
    const int act = tool_->activated, acc = tool_->accepted, can = tool_->cancelled;
    cl->submit("");
    settle();
    QCOMPARE(tool_->activated, act);  // not restarted by repeatLast
    QCOMPARE(tool_->cancelled, can);
    QCOMPARE(tool_->accepted, acc + 1);
    QVERIFY(!w_->toolManager().isActive());
    // idle: empty Enter still repeats the last command
    cl->submit("");
    settle();
    QCOMPARE(tool_->activated, act + 1);
    w_->toolManager().cancel();
  }

  void colonDoesNotLandInBox() {
    app::CommandLine* cl = w_->commandLine();
    cl->focusForTyping();
    QTest::keyClick(cl->edit(), ':');
    QCOMPARE(cl->edit()->text(), QString());
    QTest::keyClicks(cl->edit(), "a:b");
    QCOMPARE(cl->edit()->text(), QString("a:b"));
    cl->edit()->setText(QString());
    // a leading ':' never reaches the command parser
    QVERIFY(cl->submit(":G2STEP"));
    QVERIFY(w_->toolManager().isActive());
    w_->toolManager().cancel();
  }

  void ctrlZInEmptyBoxUndoesModel() {
    app::CommandLine* cl = w_->commandLine();
    QVERIFY(cl->submit("SET zz=5"));
    QVERIFY(doc().parameter("zz"));
    QCOMPARE(cl->edit()->text(), QString());
    QTest::keyClick(cl->edit(), Qt::Key_Z, Qt::ControlModifier);
    QVERIFY2(!doc().parameter("zz"), "Ctrl+Z in an empty box must undo the model, not recall text");
    QCOMPARE(cl->edit()->text(), QString());
    QTest::keyClick(cl->edit(), Qt::Key_Y, Qt::ControlModifier);
    QVERIFY(doc().parameter("zz"));
    // with typed text, Ctrl+Z undoes the typing only
    QTest::keyClicks(cl->edit(), "abc");
    QTest::keyClick(cl->edit(), Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(doc().parameter("zz"));
    QVERIFY(cl->edit()->text() != QString("abc"));
    cl->edit()->setText(QString());
    doc().removeParameter("zz");
  }

  void zoomCommand() {
    QVERIFY(w_->tools().byAlias("ZOOM"));
    QVERIFY(!w_->tools().byAlias("Z") || w_->tools().byAlias("Z")->info().id != "view.zoom");
    QVERIFY(w_->commandLine()->submit("ZOOM 2"));
    QVERIFY(w_->commandLine()->submit("ZOOM E"));
  }

  // --- APP-10: search ----------------------------------------------------------------------------------
  void searchRanking() {
    const core::ToolRegistry& reg = w_->tools();
    auto r = app::searchCommandIds(reg, "appear", {}, {});
    QVERIFY(!r.empty());
    QCOMPARE(r.front(), std::string("app.appearance"));
    QVERIFY(std::find(r.begin(), r.end(), "app.search") == r.end());  // no tooltip subsequence noise
    r = app::searchCommandIds(reg, "meas", {}, {});
    QVERIFY(!r.empty());
    QCOMPARE(r.front(), std::string("inspect.measure"));
    QVERIFY(app::searchCommandIds(reg, "zzz", {}, {}).empty());
    // ranking: disabled tools go last
    auto rank = [](const core::ITool* t) { return t->info().id == "app.appearance" ? 1 : 0; };
    r = app::searchCommandIds(reg, "appear", {}, {}, 20, rank);
    if (r.size() > 1) QVERIFY(r.front() != "app.appearance");
  }
};

QTEST_MAIN(TestG2Tools)
#include "test_g2_tools.moc"
