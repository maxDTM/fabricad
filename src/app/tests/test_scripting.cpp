// Tests for the scripting layer: ScriptRunner (lines go through the command line exactly like
// typed input, log, timing, exit codes), the textual geometry selectors, CHECK, the shell verbs
// (SELECT / PICK / INPUT / RENAME / TAB / CLOSE) and the command-line routing rules (a command
// name typed while a sketch tool is active starts that command; shell verbs never disturb the
// selection). Everything runs headless (QT_QPA_PLATFORM=offscreen) through a real MainWindow.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandLine.h"
#include "app/DocumentTab.h"
#include "app/MainWindow.h"
#include "app/ScriptRunner.h"
#include "app/ScriptTools.h"
#include "app/Selectors.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "part/tools/FeatureTool.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace fabricad;
using core::ShapeKind;
using core::ShapeRef;

namespace {

void settle() {
  for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/// A 40 x 30 x 10 box from a sketch rectangle, typed as a user would.
const QStringList kBoxScript = {
    "# box",
    "SKETCH XY",
    "RECT",
    "0,0",
    "40,30",
    "FINISH",
    "SELECT profile:@20,15,0",
    "EXTRUDE 10",
    "OK",
    "CHECK bodies == 1",
};

}  // namespace

class TestScripting : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;

  int run(const QStringList& lines, app::ScriptRunner::Options opts = {}, app::ScriptRunner** out = nullptr) {
    auto* r = new app::ScriptRunner(*w_, w_.get());
    r->setOptions(opts);
    r->setLines(lines, "test");
    const int code = r->run();
    if (out) *out = r;
    else r->deleteLater();
    return code;
  }
  core::Document& doc() { return w_->document(); }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_scripting");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->show();
    settle();
  }
  void cleanupTestCase() { w_.reset(); }

  void init() {
    // every test starts in one fresh, empty tab
    for (int i = w_->tabCount() - 1; i >= 0; --i) QVERIFY(w_->closeTab(i));
    QCOMPARE(w_->tabCount(), 1);
    QCOMPARE(doc().featureCount(), 0);
    settle();
  }

  void stripComments() {
    QCOMPARE(app::ScriptRunner::stripComment("  EXTRUDE 10  # join"), QString("EXTRUDE 10"));
    QCOMPARE(app::ScriptRunner::stripComment("SELECT body:#18/face:top // top"), QString("SELECT body:#18/face:top"));
    QCOMPARE(app::ScriptRunner::stripComment("; comment"), QString());
    QCOMPARE(app::ScriptRunner::stripComment("# comment"), QString());
    QCOMPARE(app::ScriptRunner::stripComment("   "), QString());
    QCOMPARE(app::ScriptRunner::stripComment("ECHO a;b"), QString("ECHO a;b"));
  }

  void runnerBuildsThroughTheCommandLine() {
    app::ScriptRunner* r = nullptr;
    QCOMPARE(run(kBoxScript, {}, &r), 0);
    QCOMPARE(r->errorCount(), 0);
    QCOMPARE(r->commandCount(), 9);  // the comment line is not a command
    QCOMPARE(doc().liveBodies().size(), size_t(1));
    QVERIFY(qAbs(core::volume(doc().bodyShape(doc().liveBodies().front())) - 12000.0) < 1e-6);
    // per-command timing and messages in the log
    QVERIFY(r->totalMillis() > 0.0);
    bool sawOk = false, sawCheck = false, sawSummary = false;
    for (const QString& l : r->logLines()) {
      sawOk = sawOk || l.contains("  ok    EXTRUDE 10");
      sawCheck = sawCheck || l.contains("CHECK bodies == 1: ok");
      sawSummary = sawSummary || l.startsWith("# 9 commands, 9 ok, 0 failed");
    }
    QVERIFY(sawOk);
    QVERIFY(sawCheck);
    QVERIFY(sawSummary);
    // the results carry line numbers of the script file
    QCOMPARE(r->results().front().line, 2);
    QCOMPARE(r->results().front().text, QString("SKETCH XY"));
    // the undo stack has the same steps a user would have (sketch, extrude)
    QVERIFY(doc().undoStack().canUndo());
    QVERIFY(!w_->toolManager().isActive());
    r->deleteLater();
  }

  void stepDelayPausesAfterEachCommand() {
    // default: no pause at all
    QCOMPARE(app::ScriptRunner::Options{}.stepDelayMs, 0);
    app::ScriptRunner::Options o;
    o.stepDelayMs = 40;
    app::ScriptRunner* r = nullptr;
    QElapsedTimer t;
    t.start();
    QCOMPARE(run(kBoxScript, o, &r), 0);
    const qint64 elapsed = t.elapsed();
    // 9 commands, of which one CHECK (no pause): at least 8 pauses
    QCOMPARE(r->commandCount(), 9);
    QVERIFY2(elapsed >= 8 * 40, qPrintable(QString("elapsed %1 ms").arg(elapsed)));
    // the pause is not counted in the per-command timings
    QVERIFY(r->totalMillis() < elapsed);
    QCOMPARE(doc().liveBodies().size(), size_t(1));
    r->deleteLater();
  }

  void errorsStopOrContinue() {
    app::ScriptRunner* r = nullptr;
    QCOMPARE(run({"SKETCH XY", "BOGUS", "FINISH", "CHECK sketches == 1"}, {}, &r), 1);
    QCOMPARE(r->errorCount(), 1);
    QCOMPARE(r->commandCount(), 2);  // stopped at BOGUS
    QVERIFY(r->results().back().error.contains("Unknown command"));
    QVERIFY(r->logLines().last().contains("stopped at the first error"));
    r->deleteLater();
    QVERIFY(w_->commandLine()->submit("FINISH"));
    settle();

    app::ScriptRunner::Options keep;
    keep.keepGoing = true;
    QCOMPARE(run({"CHECK bodies == 5", "ECHO still running", "CHECK bodies == 0"}, keep, &r), 1);
    QCOMPARE(r->commandCount(), 3);
    QCOMPARE(r->errorCount(), 1);
    QVERIFY(r->results()[0].error.contains("FAILED"));
    QVERIFY(r->results()[1].ok);
    QVERIFY(r->results()[2].ok);
    r->deleteLater();
    // nothing to run
    QCOMPARE(run({}), 2);
  }

  void logFile() {
    QTemporaryDir dir;
    app::ScriptRunner::Options o;
    o.logPath = dir.filePath("run.log");
    o.quiet = true;
    QCOMPARE(run({"ECHO hello", "CHECK tabs == 1"}, o), 0);
    QFile f(o.logPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    QVERIFY(text.contains("ECHO hello"));
    QVERIFY(text.contains("info: hello"));
    QVERIFY(text.contains("# 2 commands, 2 ok, 0 failed"));
  }

  void selectorsOnABox() {
    QCOMPARE(run(kBoxScript), 0);
    core::Document& d = doc();
    const core::ObjectId bodyId = d.liveBodies().front();
    // bodies by name, id and point
    QCOMPARE(app::resolveSelector(d, "body:Body1").refs, std::vector<ShapeRef>{ShapeRef::body(bodyId)});
    QCOMPARE(app::resolveSelector(d, QString("body:#%1").arg(bodyId).toStdString()).refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "Body1").refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "body:@20,15,5").refs.size(), size_t(1));
    QVERIFY(!app::resolveSelector(d, "body:Nope").ok());
    // faces by side, point, type, index, history name
    app::SelectorResult top = app::resolveSelector(d, "body:Body1/face:top");
    QVERIFY2(top.ok(), top.error.c_str());
    QCOMPARE(top.refs.size(), size_t(1));
    QCOMPARE(top.refs[0].kind, ShapeKind::Face);
    QVERIFY(qAbs(*app::refMetric(d, top.refs[0], "area") - 1200.0) < 1e-6);
    QVERIFY(qAbs(*app::refMetric(d, top.refs[0], "cz") - 10.0) < 1e-6);
    QCOMPARE(app::resolveSelector(d, "body:Body1/face:@40,15,5").refs.size(), size_t(1));  // right face
    QVERIFY(qAbs(*app::refMetric(d, app::resolveSelector(d, "body:Body1/face:@40,15,5").refs[0], "cx") - 40.0) < 1e-6);
    QCOMPARE(app::resolveSelector(d, "body:Body1/faces:plane").refs.size(), size_t(6));
    QVERIFY(!app::resolveSelector(d, "body:Body1/face:plane").ok());  // ambiguous singular
    QCOMPARE(app::resolveSelector(d, "body:Body1/face:Extrude1/End").refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "body:Body1/face:#3").refs.size(), size_t(1));
    QVERIFY(!app::resolveSelector(d, "body:Body1/face:#99").ok());
    // edges: of a face, by axis, at a coordinate, nearest a point, history name
    QCOMPARE(app::resolveSelector(d, "body:Body1/edges:top").refs.size(), size_t(4));
    QCOMPARE(app::resolveSelector(d, "body:Body1/face:top/edges").refs.size(), size_t(4));
    QCOMPARE(app::resolveSelector(d, "body:Body1/edges:z").refs.size(), size_t(4));
    QCOMPARE(app::resolveSelector(d, "body:Body1/edges:z=0").refs.size(), size_t(4));
    QCOMPARE(app::resolveSelector(d, "body:Body1/edges:all").refs.size(), size_t(12));
    app::SelectorResult e = app::resolveSelector(d, "body:Body1/edge:@20,0,10");
    QVERIFY2(e.ok(), e.error.c_str());
    QCOMPARE(e.refs[0].kind, ShapeKind::Edge);
    QVERIFY(qAbs(*app::refMetric(d, e.refs[0], "length") - 40.0) < 1e-6);
    QVERIFY(!e.refs[0].name.empty());  // history name attached
    QCOMPARE(app::resolveSelector(d, "body:Body1/edge:" + e.refs[0].name).refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "body:Body1/vertex:@0,0,0").refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "body:Body1/vertices:all").refs.size(), size_t(8));
    // sketch profiles and curves, origin geometry, features
    QCOMPARE(app::resolveSelector(d, "profile:@20,15,0").refs[0].kind, ShapeKind::Profile);
    QCOMPARE(app::resolveSelector(d, "sketch:Sketch1/profile:@20,15").refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "sketch:Sketch1/profiles:all").refs.size(), size_t(1));
    QCOMPARE(app::resolveSelector(d, "sketch:Sketch1/curves:all").refs.size(), size_t(4));
    QCOMPARE(app::resolveSelector(d, "curve:@20,0,0").refs[0].kind, ShapeKind::SketchCurve);
    QVERIFY(!app::resolveSelector(d, "profile:@20,15,99").ok());  // not on the sketch plane
    QCOMPARE(app::resolveSelector(d, "plane:XZ").refs[0], ShapeRef::originPlane("XZ"));
    QCOMPARE(app::resolveSelector(d, "axis:z").refs[0].kind, ShapeKind::OriginAxis);
    QCOMPARE(app::resolveSelector(d, "feature:Extrude1").refs[0].kind, ShapeKind::Feature);
    QCOMPARE(app::resolveSelector(d, "sketch:Sketch1").refs[0].kind, ShapeKind::Sketch);
    QCOMPARE(app::resolveSelector(d, "component:root").refs[0], ShapeRef::component(core::kRootComponentId));
    QCOMPARE(app::resolveSelector(d, "component:root/bodies").refs.size(), size_t(1));
    QVERIFY(!app::resolveSelector(d, "junk:1").ok());
    QVERIFY(!app::resolveSelector(d, "").ok());
    // several selectors, "none"
    QCOMPARE(app::resolveSelectors(d, {"body:Body1/face:top", "body:Body1/face:bottom"}).refs.size(), size_t(2));
    QVERIFY(app::resolveSelectors(d, {"none"}).ok());
    QVERIFY(app::resolveSelectors(d, {"none"}).refs.empty());
    // metrics and descriptions
    const ShapeRef body = ShapeRef::body(bodyId);
    QVERIFY(qAbs(*app::refMetric(d, body, "volume") - 12000.0) < 1e-6);
    QVERIFY(qAbs(*app::refMetric(d, body, "zmax") - 10.0) < 1e-9);  // tight bbox
    QVERIFY(qAbs(*app::refMetric(d, body, "dx") - 40.0) < 1e-9);
    QCOMPARE(*app::refMetric(d, body, "faces"), 6.0);
    QVERIFY(!app::refMetric(d, body, "nonsense"));
    const std::string info = app::describeRef(d, body);
    QVERIFY(info.find("volume 12000") != std::string::npos);
    QVERIFY(info.find("bbox X[0, 40]") != std::string::npos);
    QVERIFY(app::describeRef(d, ShapeRef::sketch(d.featureAt(0)->id())).find("sketch x (1, 0, 0)") != std::string::npos);
    QVERIFY(app::listText(d, "bodies").find("body:Body1") != std::string::npos);
    QVERIFY(app::listText(d, "features").find("Extrude1") != std::string::npos);
    QVERIFY(app::listText(d, "bogus").empty());
    // ray/point picking honours the filter and finds the face at a point
    auto hit = app::pickAt(d, {20, 15, 10}, core::kAllKinds, 0.5);
    QVERIFY(hit);
    QCOMPARE(hit->ref.kind, ShapeKind::Face);
    auto bodyOnly = app::pickAt(d, {20, 15, 10}, core::mask(ShapeKind::Body), 0.5);
    QVERIFY(bodyOnly && bodyOnly->ref.kind == ShapeKind::Body);
    QVERIFY(!app::pickAt(d, {200, 200, 200}, core::kAllKinds, 0.5));
  }

  void checkExpressions() {
    QCOMPARE(run(kBoxScript), 0);
    app::CheckContext ctx;
    ctx.tabs = 1;
    auto ok = [&](const char* expr) {
      std::vector<std::string> args;
      for (const QString& a : QString(expr).split(' ', Qt::SkipEmptyParts)) args.push_back(a.toStdString());
      return app::evaluateCheck(doc(), ctx, args);
    };
    QVERIFY(ok("bodies == 1").ok);
    QVERIFY(ok("bodies 1").ok);
    QVERIFY(!ok("bodies == 2").ok);
    QVERIFY(ok("features == 2").ok);
    QVERIFY(ok("status ok").ok);
    QVERIFY(ok("command none").ok);
    QVERIFY(ok("body:Body1 volume ~ 12001 0.1%").ok);
    QVERIFY(!ok("body:Body1 volume ~ 12100 0.1%").ok);
    QVERIFY(ok("body:Body1 volume ~ 12000.5 1").ok);
    QVERIFY(ok("body:Body1 zmax == 10").ok);
    QVERIFY(ok("body:Body1 faces >= 6").ok);
    QVERIFY(ok("body:Body1 exists").ok);
    QVERIFY(ok("body:Nope missing").ok);
    QVERIFY(!ok("body:Nope exists").ok);
    QVERIFY(ok("body:Body1/edges:top count == 4").ok);
    QVERIFY(ok("feature:Extrude1 status ok").ok);
    QVERIFY(ok("tabs == 1").ok);
    QVERIFY(!ok("dof == 0").ok);  // no sketch open
    QVERIFY(!ok("").ok);
    QVERIFY(!ok("bodies").ok);
    QVERIFY(!ok("bodies ?? 1").ok);
    QVERIFY(ok("file /definitely/not/here.step missing").ok);
    QVERIFY(ok("undo == 1").ok);
  }

  void routingWhileASketchToolIsActive() {
    core::ToolManager& m = w_->toolManager();
    QVERIFY(w_->commandLine()->submit("SKETCH XY"));
    QVERIFY(w_->commandLine()->submit("RECT"));
    QCOMPARE(m.activeToolId(), std::string("sketch.rectangle"));
    QVERIFY(w_->commandLine()->submit("0,0"));
    QVERIFY(w_->commandLine()->submit("20,20"));
    // a command name typed while the (repeating) rectangle tool is active starts that command
    QVERIFY(w_->commandLine()->submit("C"));
    QCOMPARE(m.activeToolId(), std::string("sketch.circle"));
    // a document parameter with an alias' name stays typed input
    QVERIFY(w_->commandLine()->submit("SET C=5 mm"));
    QVERIFY(doc().parameter("C"));
    QCOMPARE(m.activeToolId(), std::string("sketch.circle"));
    QVERIFY(w_->commandLine()->submit("10,10"));
    QVERIFY(w_->commandLine()->submit("C"));  // diameter from the parameter
    QCOMPARE(m.activeToolId(), std::string("sketch.circle"));
    // FINISH is the alias of the finish tool, so it ends the sketch instead of being swallowed
    QVERIFY(w_->commandLine()->submit("FINISH"));
    settle();
    QVERIFY(!doc().undoStack().isClean());
    QCOMPARE(doc().featureCount(), 1);
    // the disabled sketch fillet does not shadow the solid fillet outside a sketch
    QVERIFY(w_->commandLine()->submit("SELECT profile:@5,5,0"));
    QVERIFY(w_->commandLine()->submit("EXTRUDE 10"));
    QVERIFY(w_->commandLine()->submit("OK"));
    settle();
    QVERIFY(w_->commandLine()->submit("SELECT body:Body1/edges:top"));
    QVERIFY(w_->commandLine()->submit("FILLET 1"));
    QCOMPARE(m.activeToolId(), std::string("part.fillet"));
    QVERIFY(w_->commandLine()->submit("OK"));
    settle();
    QVERIFY(!m.isActive());
    QCOMPARE(doc().featureCount(), 3);
    // unknown words with no command active are rejected with a message
    QVERIFY(!w_->commandLine()->submit("WIBBLE"));
    QVERIFY(w_->messageText().contains("Unknown command"));
  }

  void shellVerbsKeepTheSelection() {
    QCOMPARE(run(kBoxScript), 0);
    QCOMPARE(run({"SELECT body:Body1/face:top", "INFO", "LIST bodies", "CHECK selection == 1", "FIT",
                  "CHECK selection == 1", "SELECT +body:Body1/face:bottom", "CHECK selection == 2", "SELECT none",
                  "CHECK selection == 0"}),
             0);
    // SELECT during a command feeds its active input; items its filter rejects fail the line
    QCOMPARE(run({"EXTRUDE", "SELECT body:Body1/face:top", "OPTIONS", "10", "operation=Join", "OK", "CHECK features == 3",
                  "CHECK body:Body1 zmax == 20"}),
             0);
    QCOMPARE(run({"FILLET", "SELECT plane:XY"}), 1);  // a plane is no fillet input
    QVERIFY(w_->commandLine()->submit("CANCEL"));
    QVERIFY(!w_->toolManager().isActive());
  }

  // P5-LIGHT-2: a curved face picked for Extrude's profiles is refused (not counted); a planar one joins
  void profileInputRefusesCurvedFaces() {
    QCOMPARE(run({"SKETCH XY", "CIRCLE", "0,0", "20", "FINISH", "SELECT profile:@0,0,0", "EXTRUDE 10", "OK"}), 0);
    QVERIFY(w_->runCommand("part.extrude"));
    settle();
    auto* tool = dynamic_cast<part::FeatureTool*>(w_->toolManager().activeTool());
    QVERIFY(tool && tool->feature());
    const app::SelectorResult side = app::resolveSelector(doc(), "body:Body1/face:@10,0,5");
    QCOMPARE(side.refs.size(), size_t(1));
    doc().selection().set(side.refs);
    settle();
    QCOMPARE(tool->feature()->prop("profiles").refs().size(), size_t(0));
    const app::SelectorResult top = app::resolveSelector(doc(), "body:Body1/face:top");
    QCOMPARE(top.refs.size(), size_t(1));
    doc().selection().set(top.refs);
    settle();
    QCOMPARE(tool->feature()->prop("profiles").refs().size(), size_t(1));
    w_->toolManager().cancel();
    settle();
  }

  void pickInputAndRename() {
    QCOMPARE(run(kBoxScript), 0);
    // PICK outside a sketch is a ray pick: it selects the face under the point
    QCOMPARE(run({"PICK 20,15,10", "CHECK selection == 1"}), 0);
    QCOMPARE(doc().selection().first()->kind, ShapeKind::Face);
    QCOMPARE(run({"PICK 20,15,10 ctrl", "CHECK selection == 0"}), 0);  // toggle
    QCOMPARE(run({"PICK 500,500,500"}), 1);                           // nothing there
    // INPUT chooses the selection input of a feature command (split body by a plane); the YZ
    // plane touches the box at x = 0, so that split keeps one body
    QCOMPARE(run({"SELECT body:Body1", "SPLIT", "INPUT splitTool plane:YZ", "OPTIONS", "OK", "CHECK bodies == 1"}), 0);
    QVERIFY(w_->messageText().contains("splitTool") || !w_->toolManager().isActive());
    QCOMPARE(run({"SELECT body:Body1", "SPLIT", "INPUT nothere plane:YZ"}), 1);
    QVERIFY(w_->commandLine()->submit("CANCEL"));
    settle();
    QVERIFY(!w_->toolManager().isActive());
    QCOMPARE(run({"SELECT plane:YZ", "OFFSETPLANE 20", "OK", "SELECT body:Body1", "SPLIT", "INPUT splitTool plane:OffsetPlane1",
                  "OK", "CHECK bodies == 2", "CHECK body:@10,15,5 volume == 6000", "CHECK body:@30,15,5 volume == 6000"}),
             0);
    // RENAME bodies and components, undoable
    QCOMPARE(run({"RENAME body:@10,15,5 Left", "RENAME body:@30,15,5 Right", "CHECK body:Left exists", "CHECK body:Right exists",
                  "UNDO", "CHECK body:Right missing", "REDO", "CHECK body:Right exists", "RENAME feature:Extrude1 Base",
                  "CHECK feature:Base exists"}),
             0);
    QCOMPARE(run({"RENAME body:Nope X"}), 1);
    QCOMPARE(run({"RENAME body:Left"}), 1);
    // parameters set from the command line are undoable and recompute the model
    QCOMPARE(run({"SET h=25 mm", "CHECK params == 1", "CHECK param h == 25", "UNDO", "CHECK params == 0", "REDO",
                  "CHECK param h == 25"}),
             0);
  }

  void groundAndDeleteComponentFromScripts() {
    // GROUND is idempotent (on/off/toggle words, UNGROUND), DELETECOMPONENT keeps the children
    QCOMPARE(run({"NEWCOMPONENT A", "OK", "SKETCH XY", "RECT", "0,0", "10,10", "FINISH", "SELECT profile:@5,5,0", "EXTRUDE 10",
                  "OK", "NEWCOMPONENT B", "OK", "SELECT none", "ACTIVATE", "CHECK components == 2"}),
             0);
    const core::Component* a = nullptr;
    const core::Component* b = nullptr;
    for (const core::Component* c : doc().components()) {
      if (c->name == "A") a = c;
      if (c->name == "B") b = c;
    }
    QVERIFY(a && b);
    QCOMPARE(b->parent, a->id);  // B was made while A was active
    QCOMPARE(run({"SELECT component:A", "GROUND"}), 0);
    QVERIFY(a->grounded);
    QCOMPARE(run({"GROUND"}), 0);  // selection kept by the shell verb; a second GROUND does not unground
    QVERIFY(a->grounded);
    QCOMPARE(run({"UNGROUND"}), 0);
    QVERIFY(!a->grounded);
    QCOMPARE(run({"GROUND on", "GROUND toggle"}), 0);
    QVERIFY(!a->grounded);
    QCOMPARE(run({"GROUND off", "GROUND"}), 0);
    QVERIFY(a->grounded);
    // LIST components shows the hierarchy and INFO the parent
    app::ScriptRunner* r = nullptr;
    QCOMPARE(run({"LIST components", "INFO component:B"}, {}, &r), 0);
    bool sawParent = false, sawTree = false;
    for (const QString& l : r->logLines()) {
      sawParent = sawParent || l.contains("parent: A");
      sawTree = sawTree || (l.contains("component:B") && l.contains("in A"));
    }
    r->deleteLater();
    QVERIFY(sawParent);
    QVERIFY(sawTree);
    const core::ObjectId bId = b->id;
    QCOMPARE(run({"SELECT component:A", "DELETECOMPONENT", "CHECK components == 1", "CHECK component:B exists",
                  "CHECK bodies == 1", "UNDO", "CHECK components == 2", "REDO", "CHECK components == 1"}),
             0);
    QVERIFY(doc().component(bId));
    QCOMPARE(doc().component(bId)->parent, core::kRootComponentId);
    // names with spaces are typed in quotes
    QCOMPARE(run({"RENAME body:Body1 \"My Body\"", "CHECK body:\"My Body\" exists", "SELECT body:'My Body'", "CHECK selection == 1",
                  "RENAME body:\"My Body\" Body1"}),
             0);
  }

  void tabsFromTheCommandLine() {
    QCOMPARE(run(kBoxScript), 0);
    core::Document* first = &doc();
    QCOMPARE(run({"NEW", "CHECK tabs == 2", "CHECK bodies == 0"}), 0);
    QVERIFY(&doc() != first);
    QCOMPARE(run({"TAB 1", "CHECK bodies == 1", "TAB next", "CHECK bodies == 0", "TAB prev", "CHECK bodies == 1"}), 0);
    QCOMPARE(&doc(), first);
    QCOMPARE(run({"TAB 7"}), 1);
    QCOMPARE(run({"CLOSE 2", "CHECK tabs == 1", "CHECK bodies == 1"}), 0);
    // a drawing opens as a tab sharing the design's document; TAB 1 goes back
    QCOMPARE(run({"DRAWING", "OK", "CHECK tabs == 2"}), 0);
    QVERIFY(!w_->activeTab()->isDesign());
    QCOMPARE(&doc(), first);
    QCOMPARE(w_->workspace(), core::Workspace::Drawing);
    QCOMPARE(run({"TAB 1", "CHECK bodies == 1"}), 0);
    QVERIFY(w_->activeTab()->isDesign());
    QCOMPARE(w_->workspace(), core::Workspace::Design);
    QCOMPARE(run({"LIST tabs"}), 0);
    QVERIFY(w_->messageText().contains("Tabs (2)"));
    // a second DRAWING reuses the tab
    QCOMPARE(run({"DRAWING", "OK", "CHECK tabs == 2", "TAB 1"}), 0);
    // saving from a script and reopening in a new tab
    QTemporaryDir dir;
    const QString path = dir.filePath("scripted.fcad");
    QCOMPARE(run({"SAVE " + path, "CHECK file " + path + " exists", "NEW", "OPEN " + path, "CHECK bodies == 1"}), 0);
    QCOMPARE(w_->tabCount(), 3);  // design, drawing, and the reopened file (the empty NEW tab was reused)
  }
};

QTEST_MAIN(TestScripting)
#include "test_scripting.moc"
