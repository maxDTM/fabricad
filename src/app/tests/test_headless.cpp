// Tests for the lightweight headless script runner (HeadlessHost: `fabricad --script --offscreen`
// without MainWindow): no widget is ever created, scripts see the same tabs, messages, OK state,
// failure reports and document names as in the window, and core batch mode is what main() sets.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/HeadlessHost.h"
#include "app/ScriptRunner.h"
#include "core/Document.h"
#include "core/Tool.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace fabricad;

namespace {

const QStringList kBoxScript = {
    "SKETCH XY", "RECT", "0,0", "40,30", "FINISH", "SELECT profile:@20,15,0", "EXTRUDE 10", "OK", "CHECK bodies == 1",
};

}  // namespace

class TestHeadless : public QObject {
  Q_OBJECT
 private:
  int run(app::HeadlessHost& h, const QStringList& lines, app::ScriptRunner** keep = nullptr) {
    auto* r = new app::ScriptRunner(h, &h);
    app::ScriptRunner::Options o;
    o.quiet = true;
    o.logPath = dir_.filePath("log.txt");
    r->setOptions(o);
    r->setLines(lines, "test");
    const int code = r->run();
    if (keep) *keep = r;
    return code;
  }
  QTemporaryDir dir_;

 private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(dir_.isValid());
    QDir::setCurrent(dir_.path());
    core::setBatchMode(true);  // as main() for --script --offscreen
  }
  void cleanupTestCase() { core::setBatchMode(std::nullopt); }

  void buildsWithoutWidgets() {
    app::HeadlessHost h;
    QCOMPARE(h.tabCount(), 1);
    QCOMPARE(QString::fromStdString(h.document().name()), QString("Untitled"));
    QCOMPARE(run(h, kBoxScript), 0);
    QCOMPARE(h.document().bodies().size(), size_t(1));
    // the point of the runner: not one widget (no MainWindow, no theme, no dialogs)
    QVERIFY2(QApplication::allWidgets().isEmpty(), qPrintable(QString::number(QApplication::allWidgets().size())));
  }

  void tabsNewOpenSaveClose() {
    app::HeadlessHost h;
    QStringList lines = kBoxScript;
    lines << "SAVE box.fcad"
          << "CHECK file box.fcad exists"
          << "NEW"
          << "CHECK tabs == 2"
          << "OPEN box.fcad"  // already open: goes to that tab
          << "CHECK tabs == 2"
          << "CHECK bodies == 1"
          << "TAB 2"
          << "CHECK bodies == 0"
          << "CLOSE"
          << "CHECK tabs == 1";
    app::ScriptRunner* r = nullptr;
    QCOMPARE(run(h, lines, &r), 0);
    QCOMPARE(h.tabCount(), 1);
    // SAVE named the design after its file (as the window does)
    QCOMPARE(QString::fromStdString(h.document().name()), QString("box"));
    QVERIFY(h.tabInfo(0).design);
  }

  void openFcadInNewTabAndStepName() {
    {
      app::HeadlessHost h;
      QCOMPARE(run(h, QStringList(kBoxScript) << "SAVE b2.fcad" << "EXPORTSTEP b2.step"), 0);
    }
    app::HeadlessHost h;
    QString err;
    QVERIFY2(h.openFile(dir_.filePath("b2.fcad"), &err), qPrintable(err));
    QCOMPARE(h.tabCount(), 1);  // the pristine start-up tab is used
    QCOMPARE(QString::fromStdString(h.document().name()), QString("b2"));
    QVERIFY(h.newDocument());
    // DOC-06: a STEP opened in an Untitled tab names the design
    QVERIFY2(h.openFile(dir_.filePath("b2.step"), &err), qPrintable(err));
    QVERIFY(h.acceptActiveTool() || !h.toolManager().isActive());
    QCOMPARE(QString::fromStdString(h.document().name()), QString("b2"));
  }

  void okDisabledAndUnknownCommandFail() {
    app::HeadlessHost h;
    app::ScriptRunner* r = nullptr;
    QCOMPARE(run(h, {"OK"}, &r), 0);  // a warning, not an error
    QVERIFY(r->results().front().messages.join('\n').contains("no command is active"));
    QCOMPARE(run(h, {"NOSUCHCOMMAND 1"}, &r), 1);
    QCOMPARE(run(h, {"SCREENSHOT x.png"}, &r), 1);  // no window to grab
    QVERIFY(r->results().front().error.contains("headless"));
  }

  void failureIsReported() {
    // a fillet larger than the box fails: the line that caused it fails (MainWindow::reportNewFailures)
    app::HeadlessHost h;
    QStringList lines = kBoxScript;
    lines << "SELECT body:Body1/edge:@40,30,5"
          << "FILLET 50"
          << "OK";
    app::ScriptRunner* r = nullptr;
    const int code = run(h, lines, &r);
    QCOMPARE(code, 1);
    QCOMPARE(r->results().back().text, QString("OK"));  // the selection and FILLET lines were fine
    QVERIFY2(r->results().back().error.contains("fail", Qt::CaseInsensitive) ||
                 r->results().back().error.contains("disabled", Qt::CaseInsensitive),
             qPrintable(r->results().back().error));
  }

  void drawingTab() {
    app::HeadlessHost h;
    QStringList lines = kBoxScript;
    lines << "DRAWING"
          << "OK"
          << "CHECK tabs == 2"
          << "TAB 1"
          << "CHECK bodies == 1";
    QCOMPARE(run(h, lines), 0);
    QCOMPARE(h.tabCount(), 2);
    QVERIFY(!h.tabInfo(1).design);
    QVERIFY(h.tabInfo(1).title.contains("Drawing"));
  }
};

QTEST_MAIN(TestHeadless)
#include "test_headless.moc"
