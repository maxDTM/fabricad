// FabriCAD entry point. OWNER: ui agent.
//
// Startup budget: everything before the first frame is cheap (module registration, widget
// construction). The OCCT GL view is created from the event loop right after show(), and a
// file given on the command line is opened after that. FABRICAD_STARTUP_TIMING=1 prints the
// phases to stderr.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainWindow.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "viewer/ViewerWidget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv) {
  QElapsedTimer clock;
  clock.start();
  const bool timing = qEnvironmentVariableIntValue("FABRICAD_STARTUP_TIMING") != 0;
  auto mark = [&](const char* what) {
    if (timing) std::fprintf(stderr, "[startup] %-28s %6lld ms\n", what, static_cast<long long>(clock.elapsed()));
  };

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
  fabricad::viewer::setupGlPlatform();
  QApplication::setApplicationName("FabriCAD");
  QApplication::setOrganizationName("FabriCAD");
  QApplication::setApplicationVersion("0.1.0");
  QApplication app(argc, argv);
  mark("QApplication");
  app.setStyle(QStyleFactory::create("Fusion"));
  if (QFile qss(":/theme-light.qss"); qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));
  app.setWindowIcon(QIcon(":/icons/fabricad.svg"));

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
  window.show();
  mark("shown");

  const QStringList args = app.arguments();
  // a few ms so the window chrome is painted before the (blocking) GL/driver initialisation
  QTimer::singleShot(30, &window, [&] {
    window.initializeViewer();
    mark("3D view created");
    bool openedFile = false;
    for (int i = 1; i < args.size(); ++i) {
      if (args[i].startsWith('-')) continue;
      QString err;
      openedFile = true;
      if (!window.openFile(args[i], &err))
        window.showMessage("Cannot open " + args[i].toStdString() + ": " + err.toStdString(),
                           fabricad::core::MessageLevel::Error);
      break;
    }
    // nothing opened: a previous session may have left an autosave behind
    if (!openedFile) window.offerRecovery();
  });
  // Developer aid: FABRICAD_SCREENSHOT=out.png grabs the window after
  // FABRICAD_SCREENSHOT_DELAY ms (default 3000) and quits.
  if (const QString shot = qEnvironmentVariable("FABRICAD_SCREENSHOT"); !shot.isEmpty()) {
    const int delay = qEnvironmentVariableIsSet("FABRICAD_SCREENSHOT_DELAY")
                          ? qEnvironmentVariableIntValue("FABRICAD_SCREENSHOT_DELAY")
                          : 3000;
    QTimer::singleShot(delay, &app, [&, shot] {
      window.grab().save(shot);
      mark("screenshot saved");
      window.setInteractive(false);
      app.quit();
    });
  }
  if (qEnvironmentVariableIntValue("FABRICAD_QUIT_AFTER_STARTUP") != 0)
    QTimer::singleShot(500, &app, [&] {
      mark("quit (FABRICAD_QUIT_AFTER_STARTUP)");
      window.setInteractive(false);
      app.quit();
    });
  return app.exec();
}
