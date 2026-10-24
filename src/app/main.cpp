// FabriCAD entry point. OWNER: ui agent.
//
// Startup budget: everything before the first frame is cheap (module registration, widget
// construction). The OCCT GL view is created from the event loop right after show(), and a
// file given on the command line is opened after that. FABRICAD_STARTUP_TIMING=1 prints the
// phases to stderr.
//
// Usage: fabricad [options] [file.fcad|part.step ...]
//   --script <file.fcs>   run the script (docs/SCRIPTING.md) after start-up and exit with its
//                         status (0 ok, 1 a line failed, 2 unreadable); --keep-going continues
//                         after failures, --stay leaves the window open afterwards
//   --offscreen           headless (QT_QPA_PLATFORM=offscreen, no 3D view). With --script (and
//                         without --stay / --step-delay) the script runs in the lightweight
//                         headless runner: no MainWindow, no widget at all (HeadlessHost);
//                         FABRICAD_SCRIPT_WINDOW=1 runs it in the (offscreen) window instead
//   --batch <file.fcs>    the same as --script <file.fcs> --offscreen
//   --log <file>          write the script log there (default: stdout); --quiet: no stdout copy
//   --step-delay <ms>     pause (repaint + fit the view) after every command, to watch a run
//
// Developer aids (environment): FABRICAD_WINDOW_SIZE=WxH, FABRICAD_SCREENSHOT=out.png (+ _DELAY ms),
// FABRICAD_GEOMETRY_DUMP=geo.json (widget rectangles for tools/uitest/compare.py).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/HeadlessHost.h"
#include "app/MainWindow.h"
#include "app/tutorial/TutorialController.h"
#include "app/ScriptRunner.h"
#include "app/ThemeManager.h"
#include "core/Tool.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "viewer/ViewerWidget.h"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct CommandLineOptions {
  QString script;
  QString log;
  QStringList files;
  bool offscreen = false;
  bool stay = false;
  int stepDelay = 0;
  bool keepGoing = false;
  bool quiet = false;
  bool help = false;
  QString error;
};

CommandLineOptions parseOptions(int argc, char** argv) {
  CommandLineOptions o;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        o.error = QString("%1 needs a value").arg(name);
        return nullptr;
      }
      return argv[++i];
    };
    if (std::strcmp(a, "--script") == 0 || std::strcmp(a, "-s") == 0) {
      if (const char* v = value(a)) o.script = QString::fromLocal8Bit(v);
    } else if (std::strcmp(a, "--batch") == 0) {
      if (const char* v = value(a)) o.script = QString::fromLocal8Bit(v);
      o.offscreen = true;
    } else if (std::strcmp(a, "--log") == 0) {
      if (const char* v = value(a)) o.log = QString::fromLocal8Bit(v);
    } else if (std::strcmp(a, "--offscreen") == 0 || std::strcmp(a, "--headless") == 0) {
      o.offscreen = true;
    } else if (std::strcmp(a, "--step-delay") == 0) {
      if (const char* v = value(a)) o.stepDelay = std::max(0, std::atoi(v));
    } else if (std::strcmp(a, "--stay") == 0) {
      o.stay = true;
    } else if (std::strcmp(a, "--keep-going") == 0 || std::strcmp(a, "-k") == 0) {
      o.keepGoing = true;
    } else if (std::strcmp(a, "--quiet") == 0 || std::strcmp(a, "-q") == 0) {
      o.quiet = true;
    } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      o.help = true;
    } else if (a[0] == '-' && a[1] == '-') {
      // Qt's own options (--platform ...) pass through
      if (std::strncmp(a, "--platform", 10) == 0 || std::strncmp(a, "--style", 7) == 0) ++i;
    } else if (a[0] != '-') {
      o.files << QString::fromLocal8Bit(a);
    }
  }
  return o;
}

void printUsage() {
  std::fputs("usage: fabricad [--script file.fcs [--offscreen] [--log out.txt] [--keep-going] [--stay] [--step-delay ms] [--quiet]] [files...]\n"
             "       fabricad --batch file.fcs [--log out.txt] [--keep-going] [--quiet] [files...]\n"
             "  --script    run a FabriCAD command script (docs/SCRIPTING.md) and exit with its status\n"
             "  --offscreen headless run (no window, no 3D view; scripts use the lightweight runner)\n"
             "  --batch     the same as --script <file> --offscreen\n"
             "  --log       write the per-command log to a file instead of stdout\n"
             "  --keep-going continue after a failed line (default: stop at the first error)\n"
             "  --stay      keep the window open after the script (interactive inspection)\n"
             "  --step-delay ms  pause (with repaint and view fit) after each command, to watch a run\n",
             stdout);
}

/// Developer aid for tools/uitest/compare.py: every visible widget with an objectName (or one of the
/// tutorial/test properties) and its rectangle in window coordinates (logical px), as JSON.
void dumpGeometry(QWidget& window, const QString& path) {
  QJsonArray items;
  const auto add = [&](QWidget* w) {
    if (!w->isVisible()) return;
    QJsonObject o;
    o["name"] = w->objectName();
    o["class"] = QString::fromLatin1(w->metaObject()->className());
    for (const char* prop : {"toolbarTab", "toolbarGroup", "section", "overlay", "toolId"}) {
      const QVariant v = w->property(prop);
      if (v.isValid() && !v.toString().isEmpty()) o[QString::fromLatin1(prop)] = v.toString();
    }
    const QRect r(w->mapTo(&window, QPoint(0, 0)), w->size());
    o["x"] = r.x();
    o["y"] = r.y();
    o["w"] = r.width();
    o["h"] = r.height();
    items.append(o);
  };
  for (QWidget* w : window.findChildren<QWidget*>()) {
    if (!w->objectName().isEmpty() || w->property("overlay").isValid() || w->property("toolbarGroup").isValid() ||
        w->inherits("QStatusBar") || w->inherits("fabricad::viewer::ViewerWidget"))
      add(w);
  }
  QJsonObject root;
  root["window"] = QJsonObject{{"w", window.width()}, {"h", window.height()}};
  root["dpr"] = window.devicePixelRatioF();
  root["widgets"] = items;
  QFile f(path);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace

int main(int argc, char** argv) {
  QElapsedTimer clock;
  clock.start();
  const bool timing = qEnvironmentVariableIntValue("FABRICAD_STARTUP_TIMING") != 0;
  auto mark = [&](const char* what) {
    if (timing) std::fprintf(stderr, "[startup] %-28s %6lld ms\n", what, static_cast<long long>(clock.elapsed()));
  };
  const CommandLineOptions opts = parseOptions(argc, argv);
  if (opts.help) {
    printUsage();
    return 0;
  }
  if (!opts.error.isEmpty()) {
    std::fprintf(stderr, "fabricad: %s\n", opts.error.toUtf8().constData());
    printUsage();
    return 2;
  }
  if (opts.offscreen) qputenv("QT_QPA_PLATFORM", "offscreen");
  const bool scripted = !opts.script.isEmpty();
  // Script mode computes each new feature once, at OK (core::isBatchMode): only when nobody
  // watches the run (no --stay hand-over, no --step-delay pauses). Set here, not detected.
  fabricad::core::setBatchMode(scripted && !opts.stay && opts.stepDelay == 0);
  // The lightweight headless runner (backend-deferred "Headless runner"): no MainWindow
  const bool headless = scripted && opts.offscreen && !opts.stay && opts.stepDelay == 0 &&
                        qEnvironmentVariableIntValue("FABRICAD_SCRIPT_WINDOW") == 0 &&
                        qEnvironmentVariableIsEmpty("FABRICAD_SCREENSHOT");

  // The system OCCT is an X11/GLX build: force Qt onto xcb (XWayland) unless the user
  // chose a platform explicitly (offscreen for tests, etc.). Also shares GL contexts and
  // sets the default surface format (depth/stencil, compatibility profile).
  // Desktops often export QT_QPA_PLATFORM="wayland;xcb", which would pick Wayland and leave
  // the 3D view dead: prefer xcb whenever an X server (XWayland) is reachable, unless
  // FABRICAD_ALLOW_WAYLAND=1. Explicit non-Wayland choices (offscreen, xcb) are kept.
  {
    const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
    const bool haveX = !qEnvironmentVariableIsEmpty("DISPLAY");
    if (qpa.isEmpty() || (qpa.startsWith("wayland") && haveX && qEnvironmentVariableIntValue("FABRICAD_ALLOW_WAYLAND") == 0))
      qputenv("QT_QPA_PLATFORM", "xcb");
  }
  if (!headless) fabricad::viewer::setupGlPlatform();  // the headless runner has no 3D view
  // OCCT prints its STEP "Statistics on Transfer" banner and similar info through its default
  // messenger on stdout (even with --quiet); keep only warnings and errors there.
  if (Handle(Message_Messenger) messenger = Message::DefaultMessenger(); !messenger.IsNull())
    for (Message_SequenceOfPrinters::Iterator it(messenger->Printers()); it.More(); it.Next())
      if (!it.Value().IsNull() && it.Value()->GetTraceLevel() < Message_Warning) it.Value()->SetTraceLevel(Message_Warning);
  QApplication::setApplicationName("FabriCAD");
  QApplication::setOrganizationName("FabriCAD");
  QApplication::setApplicationVersion("0.2.1");
  // Qt's own file/colour dialogs follow the application palette; the GTK portal dialog would
  // follow the (possibly dark) desktop theme instead.
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  mark("QApplication");
  if (headless) {
    // QApplication (offscreen) rather than QGuiApplication: a tool that would open a message
    // box must not crash the run. Nothing here creates a widget, the theme is not installed.
    fabricad::app::HeadlessHost host;
    mark("HeadlessHost constructed");
    for (const QString& file : opts.files) {
      QString err;
      if (!host.openFile(file, &err))
        host.showMessage("Cannot open " + file.toStdString() + ": " + err.toStdString(), fabricad::core::MessageLevel::Error);
    }
    fabricad::app::ScriptRunner runner(host);
    fabricad::app::ScriptRunner::Options ro;
    ro.logPath = opts.log;
    ro.keepGoing = opts.keepGoing;
    ro.quiet = opts.quiet;
    runner.setOptions(ro);
    QString err;
    int code = 2;
    if (!runner.loadFile(opts.script, &err)) std::fprintf(stderr, "fabricad: %s\n", err.toUtf8().constData());
    else code = runner.run();
    mark("script finished");
    return code;
  }
  // tokens: palette, fonts (JetBrains Mono, caps) and QSS for ui/theme, before any window exists so
  // the first frame is already in the right theme (the desktop's platform theme must not leak in)
  fabricad::app::ThemeManager::instance().install(app);
  mark("theme applied");
  app.setWindowIcon(QIcon(":/icons/app/fabricad-app.svg"));  // app icon (resources/icons/app)

  // Drawing workspace central widget (created lazily on the first switch to DRAWING).
  fabricad::app::MainWindow::setDrawingWorkspaceFactory(
      [](fabricad::core::Document& doc, fabricad::core::ToolManager& tm, QWidget* parent) -> QWidget* {
        auto* w = new fabricad::drawing::DrawingWorkspaceWidget(parent);
        w->setDocument(&doc);
        w->setToolManager(&tm);
        return w;
      });
  fabricad::app::MainWindow window;
  mark("MainWindow constructed");
  // Developer aid: FABRICAD_WINDOW_SIZE=1280x800 fixes the window size (screenshots, layout checks)
  if (const QString size = qEnvironmentVariable("FABRICAD_WINDOW_SIZE"); !size.isEmpty()) {
    const QStringList wh = size.split('x');
    if (wh.size() == 2 && wh[0].toInt() > 0 && wh[1].toInt() > 0) window.resize(wh[0].toInt(), wh[1].toInt());
  }
  if (scripted) window.setInteractive(false);  // no modal prompts while a script drives the app
  window.show();
  mark("shown");

  // a few ms so the window chrome is painted before the (blocking) GL/driver initialisation
  QTimer::singleShot(30, &window, [&] {
    window.initializeViewer();
    mark("3D view created");
    bool openedFile = false;
    for (const QString& file : opts.files) {
      QString err;
      openedFile = true;
      if (!window.openFile(file, &err))
        window.showMessage("Cannot open " + file.toStdString() + ": " + err.toStdString(), fabricad::core::MessageLevel::Error);
    }
    // nothing opened: a previous session may have left an autosave behind
    if (!openedFile && !scripted) window.offerRecovery();
    // first launch: offer the guided tour (never for scripts, offscreen runs, FABRICAD_NO_TUTORIAL=1)
    if (!openedFile && !scripted)
      if (auto* tour = fabricad::app::tutorial::TutorialController::of(window)) tour->maybeShowWelcome();
    if (scripted) {
      auto* runner = new fabricad::app::ScriptRunner(window, &window);
      fabricad::app::ScriptRunner::Options ro;
      ro.logPath = opts.log;
      ro.keepGoing = opts.keepGoing;
      ro.quiet = opts.quiet;
      ro.stepDelayMs = opts.stepDelay;
      runner->setOptions(ro);
      QString err;
      int code = 2;
      if (!runner->loadFile(opts.script, &err)) std::fprintf(stderr, "fabricad: %s\n", err.toUtf8().constData());
      else code = runner->run();
      mark("script finished");
      if (opts.stay) {
        fabricad::core::setBatchMode(false);  // the user takes over: live previews again
        window.setInteractive(true);
        window.showMessage(code == 0 ? "Script finished" : "Script failed: see the command history (F2 in the command line)",
                           code == 0 ? fabricad::core::MessageLevel::Info : fabricad::core::MessageLevel::Error);
      } else {
        app.exit(code);
      }
    }
  });
  // Developer aid: FABRICAD_SCREENSHOT=out.png grabs the window after
  // FABRICAD_SCREENSHOT_DELAY ms (default 3000) and quits.
  if (const QString shot = qEnvironmentVariable("FABRICAD_SCREENSHOT"); !shot.isEmpty()) {
    const int delay = qEnvironmentVariableIsSet("FABRICAD_SCREENSHOT_DELAY")
                          ? qEnvironmentVariableIntValue("FABRICAD_SCREENSHOT_DELAY")
                          : 3000;
    QTimer::singleShot(delay, &app, [&, shot] {
      window.grab().save(shot);
      if (const QString geo = qEnvironmentVariable("FABRICAD_GEOMETRY_DUMP"); !geo.isEmpty()) dumpGeometry(window, geo);
      mark("screenshot saved");
      window.setInteractive(false);
      app.quit();
    });
  }
  // Developer aid: FABRICAD_GEOMETRY_DUMP=geo.json without FABRICAD_SCREENSHOT rewrites the widget
  // geometry every second (tools/uitest/ui.sh runs: `ui.sh geom <N>` copies it for compare.py)
  else if (const QString geo = qEnvironmentVariable("FABRICAD_GEOMETRY_DUMP"); !geo.isEmpty()) {
    auto* t = new QTimer(&app);
    QObject::connect(t, &QTimer::timeout, &window, [&window, geo] { dumpGeometry(window, geo); });
    t->start(1000);
  }
  if (qEnvironmentVariableIntValue("FABRICAD_QUIT_AFTER_STARTUP") != 0)
    QTimer::singleShot(500, &app, [&] {
      mark("quit (FABRICAD_QUIT_AFTER_STARTUP)");
      window.setInteractive(false);
      app.quit();
    });
  return app.exec();
}
