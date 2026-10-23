// Tutorial engine tests (docs/plans/tutorial.md): the lint part loads every tour file (qrc and
// FABRICAD_TUTORIAL_PATH), validates it (anchors, tool ids, conditions, placeholders) and checks that
// every anchor resolves on the current UI in the three layout presets (default, alt, light); the
// engine part runs the sample tour headless: manual and condition steps, skip_if, Back, Alt+Left /
// Alt+Right / F7, Esc never exits, Restore chapter in a new tab, checkpoints, welcome card, Help menu.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainWindow.h"
#include "app/ThemeManager.h"
#include "app/UiSettings.h"
#include "app/tutorial/AnchorResolver.h"
#include "app/tutorial/TestProbe.h"
#include "app/tutorial/TutorialBubble.h"
#include "app/tutorial/TutorialController.h"
#include "app/tutorial/TutorialDefinition.h"
#include "app/tutorial/TutorialEngine.h"
#include "app/tutorial/TutorialOverlay.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "theme/Tokens.h"

#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace fabricad;
using namespace fabricad::app::tutorial;

namespace {
void settle(int rounds = 4) {
  for (int i = 0; i < rounds; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/// Poll the engine until `pred` holds (stable_ms timers need real time).
bool waitFor(const std::function<bool()>& pred, int ms = 3000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) {
    if (pred()) return true;
    QTest::qWait(20);
  }
  return pred();
}

struct Preset {
  const char* name;
  std::vector<std::pair<const char*, const char*>> keys;
};
const std::vector<Preset> kPresets = {
    {"default", {}},
    {"alt", {{"ui/cardPlacement", "sidebar"}, {"ui/sidebarSide", "right"}, {"ui/toolbarArea", "left"}}},
    {"light", {{"ui/theme", "light"}, {"ui/orientation", "hexDial"}, {"ui/timelineStyle", "strip"}}},
};
}  // namespace

class TestTutorial : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  TutorialController* c_ = nullptr;
  app::UiSettings& ui() { return app::UiSettings::instance(); }
  void resetUi() {
    for (const auto& p : kPresets)
      for (const auto& [k, v] : p.keys) ui().reset(k);
  }
  void cancelTool() {
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    if (w_->viewerInSketchMode() || c_->engine().query().inSketch()) w_->runCommand("sketch.finish");
    if (w_->toolManager().isActive()) w_->toolManager().cancel();
    settle();
  }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_tutorial");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    app::ThemeManager::instance().install(*qApp, /*readSettings=*/false);
    w_ = std::make_unique<app::MainWindow>();
    w_->setInteractive(false);
    w_->resize(1600, 1000);
    w_->show();
    settle();
    c_ = TutorialController::of(*w_);
    QVERIFY(c_);
  }
  void cleanupTestCase() {
    c_ = nullptr;
    w_.reset();
  }
  void init() {
    if (c_->engine().running()) c_->engine().stop();
    resetUi();
    cancelTool();
  }

  // ---- definition: load + validate every tour file -----------------------------------------------

  void everyTourFileLoadsAndValidates() {
    const QStringList files = tourFiles();
    QVERIFY2(!files.isEmpty(), "no tour files in :/tutorials");
    for (const QString& f : files) {
      QStringList errors;
      auto t = loadTour(f, &errors);
      QVERIFY2(t, qPrintable(errors.join('\n')));
      QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
      QVERIFY(t->stepCount() > 0);
      const QStringList problems = validateTour(*t, [&](const QString& id) { return w_->tools().has(id.toStdString()); });
      QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }
    QVERIFY2(c_->loadErrors().isEmpty(), qPrintable(c_->loadErrors().join('\n')));
    QVERIFY(c_->findTour("sample"));
    QCOMPARE(c_->findTour("sample")->stepCount(), 3);
  }

  void parserReportsProblems() {
    auto bad = [](const char* json) {
      QStringList e;
      auto t = parseTour(QByteArray(json), ":/x", &e);
      return t ? QString() : e.join("; ");
    };
    const char* head = R"({"id":"t","title":"T","chapters":[{"id":"c","title":"C","steps":[)";
    auto wrap = [&](const char* step) { return QByteArray(head) + step + "]}]}"; };
    QVERIFY(bad(wrap(R"({"id":"1","text":"ok"})")).isEmpty());
    QVERIFY(bad(wrap(R"({"id":"1","text":"x","anchor":{"bogus":"y"}})")).contains("unknown anchor kind"));
    QVERIFY(bad(wrap(R"({"id":"1","text":"x","until":{"weather":"sunny"}})")).contains("unknown condition"));
    QVERIFY(bad(wrap(R"({"id":"1","text":"x","until":{"sketch":"maybe"}})")).contains("sketch"));
    QVERIFY(bad(wrap(R"({"id":"1","text":"x"},{"id":"1","text":"y"})")).contains("duplicate step id"));
    QVERIFY(bad(wrap(R"({"id":"1","text":"in the {sidebar}"})")).contains("unknown placeholder"));
    QVERIFY(bad(wrap(R"({"id":"1","text":"x","do":[{"teleport":1}]})")).contains("unknown action"));
    QVERIFY(bad(wrap(R"({"id":"1","text":"x","colour":"red"})")).contains("unknown key"));
    QVERIFY(bad("{not json").contains("JSON error"));
    // validateTour: unknown tool ids
    QStringList e;
    auto t = parseTour(wrap(R"({"id":"1","text":"x","anchor":"tool:no.such","until":{"tool":"no.such.either"}})"), ":/x", &e);
    QVERIFY(t);
    const QStringList p = validateTour(*t, [](const QString&) { return false; });
    QCOMPARE(p.size(), 2);
  }

  void anchorForms() {
    QString e;
    QCOMPARE(Anchor::parse(QJsonValue("tool:part.extrude"), &e)->kind, QString("tool"));
    QCOMPARE(Anchor::parse(QJsonValue("FinishSketchButton"), &e)->kind, QString("widget"));
    auto a = Anchor::parse(QJsonObject{{"widget", "prop:distance"}}, &e);
    QCOMPARE(a->kind, QString("prop"));
    QCOMPARE(a->target, QString("distance"));
    a = Anchor::parse(QJsonObject{{"model", "body:Body1/face:top"}}, &e);
    QCOMPARE(a->kind, QString("model"));
    QCOMPARE(a->target, QString("body:Body1/face:top"));
    QVERIFY(Anchor::parse(QJsonValue(), &e)->isNone());
  }

  // ---- lint: every anchor resolves in every layout preset ----------------------------------------

  void everyAnchorResolvesInEveryPreset() {
    AnchorResolver res(*w_);
    for (const Preset& p : kPresets) {
      resetUi();
      for (const auto& [k, v] : p.keys) QVERIFY2(ui().set(k, v), k);
      settle(8);
      for (const Tour& t : c_->tours())
        for (const Chapter& ch : t.chapters)
          for (const Step& s : ch.steps) {
            cancelTool();
            if (s.context.startsWith("tool:")) {
              QVERIFY(w_->runCommand(s.context.mid(5).toStdString()));
              settle();
            }
            const ResolvedAnchor r = res.resolve(s.anchor);
            const QString where = QString("%1 preset %2 step %3 anchor %4: %5").arg(t.id, p.name, s.id, s.anchor.toString(), r.why);
            QVERIFY2(r.found, qPrintable(where));
            // model anchors need the 3D view (no GL offscreen): the entity must exist; widget
            // anchors must be visible, or reachable through a menu / an expandable row
            if (!s.anchor.isModel()) {
              QVERIFY2(r.visible || r.via == "menu" || r.via == "expand", qPrintable(where));
              QVERIFY2(r.rect.isValid() || s.anchor.isNone(), qPrintable(where));
            } else {
              QVERIFY2(!r.refs.empty() || s.anchor.kind == "sketchpoint", qPrintable(where));
            }
          }
    }
    cancelTool();
    resetUi();
  }

  void resolverKinds() {
    AnchorResolver res(*w_);
    auto r = res.resolve(*Anchor::parse(QJsonValue("tool:sketch.create"), nullptr));
    QVERIFY(r.visible);
    QCOMPARE(r.widget->property("toolId").toString(), QString("sketch.create"));
    r = res.resolve(*Anchor::parse(QJsonValue("menu:Help/Tutorials"), nullptr));
    QVERIFY2(r.found, qPrintable(r.why));
    QCOMPARE(r.via, QString("menu"));  // the Help menu is closed: point at its button
    QCOMPARE(r.widget->objectName(), QString("HelpButton"));
    r = res.resolve(*Anchor::parse(QJsonValue("group:SKETCH/CREATE"), nullptr));
    Q_UNUSED(r);  // the section exists only in sketch mode
    r = res.resolve(*Anchor::parse(QJsonValue("browser:Origin/YZ"), nullptr));
    QVERIFY2(r.found, qPrintable(r.why));
    r = res.resolve(*Anchor::parse(QJsonValue("model:plane:XY"), nullptr));
    QVERIFY(r.found);
    QCOMPARE(r.refs.size(), size_t(1));
    r = res.resolve(*Anchor::parse(QJsonValue("timeline:NoSuchFeature"), nullptr));
    QVERIFY(!r.found);
    r = res.resolve(*Anchor::parse(QJsonValue("tab:TOOLS"), nullptr));
    QVERIFY2(r.found, qPrintable(r.why));  // folds into MORE
    QVERIFY(AnchorResolver::textMatches("&Extrude ▾", "extrude"));
  }

  // ---- engine --------------------------------------------------------------------------------------

  void sampleTourRunsHeadless() {
    QTemporaryDir dir;
    qputenv("FABRICAD_TUTORIAL_CHECKPOINT_DIR", dir.path().toUtf8());
    QSignalSpy finished(&c_->engine(), &TutorialEngine::finished);
    QVERIFY(c_->startTour("sample"));
    TutorialEngine& e = c_->engine();
    QVERIFY(e.running());
    QCOMPARE(e.step()->id, QString("1.1"));
    QVERIFY(c_->overlay()->isVisible());
    QVERIFY(c_->bubble()->isVisible());
    // a manual step does not advance by itself
    QTest::qWait(300);
    QCOMPARE(e.step()->id, QString("1.1"));
    QCOMPARE(c_->bubble()->findChild<QPushButton*>("TutorialNext")->text(), QString("Next"));
    QTest::mouseClick(c_->bubble()->findChild<QPushButton*>("TutorialNext"), Qt::LeftButton);
    QCOMPARE(e.step()->id, QString("2.1"));
    QCOMPARE(QSettings().value("tutorial/sample/step").toString(), QString("2.1"));
    // spotlight on the Create Sketch button
    settle();
    c_->track();
    QVERIFY(!c_->overlay()->spotlight().isNull());
    // the condition step completes when the app reaches the state (here: the command starts)
    QVERIFY(w_->runCommand("sketch.create"));
    QVERIFY(waitFor([&] { return e.step() && e.step()->id == "2.2"; }));
    // 2.2: the sketch is active (headless: run with the plane as argument)
    cancelTool();
    QVERIFY(w_->runCommand("sketch.create", {"XY"}));
    QVERIFY(waitFor([&] { return !e.running(); }));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.last().at(0).toBool(), true);
    QVERIFY(TutorialEngine::finishedBefore("sample"));
    QVERIFY(TutorialEngine::savedStep("sample").isEmpty());
    QVERIFY(QFileInfo::exists(dir.filePath("sample-sketch.fcad")));  // chapter checkpoint
    QVERIFY(w_->findChild<QFrame*>("TutorialFinished"));
    qunsetenv("FABRICAD_TUTORIAL_CHECKPOINT_DIR");
    cancelTool();
    if (auto* fin = w_->findChild<QFrame*>("TutorialFinished")) delete fin;
  }

  void escapeNeverExitsShortcutsNavigate() {
    QVERIFY(c_->startTour("sample"));
    TutorialEngine& e = c_->engine();
    QVERIFY(w_->runCommand("sketch.create"));
    settle();
    QTest::keyClick(w_.get(), Qt::Key_Escape);
    settle();
    QVERIFY(e.running());  // Esc cancels the command, never the tour
    QCOMPARE(e.step()->id, QString("1.1"));
    QWidget* target = w_->focusWidget() ? w_->focusWidget() : w_.get();
    QTest::keyClick(target, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(e.step()->id, QString("2.1"));
    QTest::keyClick(target, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(e.step()->id, QString("2.2"));
    QTest::keyClick(target, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(e.step()->id, QString("2.1"));
    // F7: the ghost cursor plays the step's input; nothing reaches the app
    cancelTool();
    c_->track();
    const ResolvedAnchor cur = c_->currentAnchor();
    QVERIFY2(cur.visible, qPrintable(cur.why));
    QTest::keyClick(target, Qt::Key_F7);
    QVERIFY2(c_->overlay()->ghostRunning(), qPrintable(c_->bubble()->hint()));
    QVERIFY(!w_->toolManager().isActive());
    QVERIFY(waitFor([&] { return !c_->overlay()->ghostRunning(); }, 4000));
    QVERIFY(!w_->toolManager().isActive());  // show me never clicks
    e.stop();
    QVERIFY(!c_->overlay()->isVisible());
    // the position is kept for resume
    QCOMPARE(TutorialEngine::savedStep("sample"), QString("2.1"));
  }

  void skipIfSkipsAndBackUndoes() {
    // a disk tour on FABRICAD_TUTORIAL_PATH, also checks the multi-tour discovery
    QTemporaryDir dir;
    QDir(dir.path()).mkpath("extra");
    QFile f(dir.filePath("extra/tutorial.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"id":"extra","title":"Extra","order":200,"welcome":false,"chapters":[{"id":"a","title":"A","steps":[
      {"id":"a1","text":"Skipped in Part.","skip_if":{"workspace":"part"},"until":{"next":true}},
      {"id":"a2","text":"Add a parameter.","until":{"check":"params == 1"},"stable_ms":0},
      {"id":"a3","text":"Done.","anchor":"none"}]}]})");
    f.close();
    qputenv("FABRICAD_TUTORIAL_PATH", dir.path().toUtf8());
    QStringList errors;
    const auto tours = discoverTours(&errors);
    qunsetenv("FABRICAD_TUTORIAL_PATH");
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    QCOMPARE(tours.size(), size_t(2));
    QCOMPARE(tours.front().id, QString("sample"));  // order 50 before 200
    TutorialEngine& e = c_->engine();
    QVERIFY(e.start(tours.back()));
    QVERIFY(waitFor([&] { return e.step() && e.step()->id == "a2"; }));  // skip_if: Part is active
    const int undo0 = w_->document().undoStack().index();
    QVERIFY(w_->submitCommandLine("SET width=20 mm"));
    settle();
    QVERIFY(waitFor([&] { return e.step() && e.step()->id == "a3"; }));
    QVERIFY(w_->document().undoStack().index() > undo0);
    e.back();  // back to a2: its change is undone
    QCOMPARE(e.step()->id, QString("a2"));
    QCOMPARE(w_->document().undoStack().index(), undo0);
    e.stop();
  }

  void restoreChapterOpensNewTab() {
    QVERIFY(c_->startTour("sample"));
    TutorialEngine& e = c_->engine();
    e.next();
    e.next();
    QCOMPARE(e.step()->id, QString("2.2"));
    const int tabs = w_->tabCount();
    w_->document().setModified(true);  // the user's work: it must stay
    QString err;
    QVERIFY2(e.restoreChapter(&err), qPrintable(err));
    QCOMPARE(w_->tabCount(), tabs + 1);
    QCOMPARE(e.step()->id, QString("2.1"));  // first step of the chapter
    e.stop();
    w_->closeTab(w_->activeTab());
  }

  void bubbleTextAsTypedAndThemed() {
    QVERIFY(c_->startTour("sample"));
    TutorialBubble* b = c_->bubble();
    QVERIFY(b->plainText().contains("right-drag"));  // as typed, markup removed
    auto* text = b->findChild<QLabel*>("TutorialText");
    QCOMPARE(text->font().capitalization(), QFont::MixedCase);
    QVERIFY(TutorialBubble::richText("Type **8**, [[Enter]]").contains("<b>8</b>"));
    QCOMPARE(b->focusPolicy(), Qt::NoFocus);
    for (QPushButton* p : b->findChildren<QPushButton*>()) QCOMPARE(p->focusPolicy(), Qt::NoFocus);
    for (auto mode : {app::ThemeManager::Mode::Dark, app::ThemeManager::Mode::Light}) {
      app::ThemeManager::instance().setMode(mode, false);
      settle();
      const QImage img = b->grab().toImage();
      QCOMPARE(img.pixelColor(img.width() / 2, img.height() - 3).rgb(), theme::color(theme::Tok::SurfaceOverlay).rgb());
    }
    app::ThemeManager::instance().setMode(app::ThemeManager::Mode::Dark, false);
    // placeholders follow the layout
    QCOMPARE(TutorialEngine::expand("{orientation}"), QString(ui().string("ui/orientation") == "viewCube" ? "ViewCube" : "orientation dial"));
    ui().set("ui/timelineStyle", "strip");
    QCOMPARE(TutorialEngine::expand("the {timeline}"), QString("the timeline"));
    ui().reset("ui/timelineStyle");
    c_->engine().stop();
  }

  void welcomeCardAndHelpMenu() {
    QSettings().remove("tutorial/offered");
    QVERIFY(TutorialController::welcomeSuppressed());  // offscreen
    QVERIFY(!c_->maybeShowWelcome());
    qputenv("FABRICAD_TUTORIAL_WELCOME", "1");
    w_->setInteractive(true);
    QVERIFY(c_->maybeShowWelcome());
    QFrame* card = c_->welcomeCard();
    QVERIFY(card && card->isVisible());
    QVERIFY(QSettings().value("tutorial/offered").toStringList().contains("sample"));
    QTest::mouseClick(card->findChild<QPushButton*>("TutorialStart"), Qt::LeftButton);
    QVERIFY(c_->engine().running());
    c_->engine().stop();
    QVERIFY(!c_->maybeShowWelcome());  // offered once
    w_->setInteractive(false);
    qunsetenv("FABRICAD_TUTORIAL_WELCOME");
    // Help > Tutorials lists the tours and the dim toggle
    auto* menu = w_->findChild<QMenu*>("TutorialsMenu");
    QVERIFY(menu);
    emit menu->aboutToShow();
    QStringList texts;
    for (QAction* a : menu->actions()) texts << a->text();
    QVERIFY2(texts.join('|').contains("Quick tour"), qPrintable(texts.join('|')));
    QAction* dim = menu->findChild<QAction*>("TutorialDimAction");
    QVERIFY(dim && dim->isCheckable());
  }

  void probeAnswersQueries() {
    TestProbe probe(*w_, *c_, QDir::temp().filePath("fabricad-test-probe-%1").arg(QCoreApplication::applicationPid()));
    QVERIFY(probe.answer({{"cmd", "ping"}}).value("ok").toBool());
    const QJsonObject st = probe.answer({{"cmd", "state"}});
    QVERIFY(st.value("ok").toBool());
    QVERIFY(st.value("ui").toObject().contains("ui/theme"));
    const QJsonObject loc = probe.answer({{"cmd", "locate"}, {"anchor", "tool:sketch.create"}});
    QVERIFY(loc.value("visible").toBool());
    QCOMPARE(loc.value("point").toArray().size(), 2);
    const QJsonObject ev = probe.answer({{"cmd", "eval"}, {"cond", QJsonObject{{"tool", "none"}}}});
    QVERIFY(ev.value("value").toBool());
    QVERIFY(probe.answer({{"cmd", "check"}, {"expr", "bodies == 0"}}).value("value").toBool());
    QVERIFY(!probe.answer({{"cmd", "click"}}).value("ok").toBool());  // no write commands
  }
};

QTEST_MAIN(TestTutorial)
#include "test_tutorial.moc"
