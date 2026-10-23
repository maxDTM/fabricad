// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FuzzCommon.h"

#include "assembly/AssemblyModule.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Tool.h"
#include "drawing/DrawingModule.h"
#include "part/PartModule.h"
#include "sketch/SketchModule.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

namespace fabricad::fuzz {

const QString& fuzzEnvironment(bool registerModules) {
  static QString path;
  if (!path.isEmpty()) return path;
  static QTemporaryDir dir(QDir::tempPath() + "/fabricad-fuzz-XXXXXX");
  path = dir.path();
  // private, before QApplication: no user settings, no display
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("HOME", path.toLocal8Bit());
  qputenv("XDG_CONFIG_HOME", (path + "/config").toLocal8Bit());
  qputenv("XDG_DATA_HOME", (path + "/data").toLocal8Bit());
  qputenv("XDG_CACHE_HOME", (path + "/cache").toLocal8Bit());
  qputenv("FABRICAD_BATCH", "1");
  QStandardPaths::setTestModeEnabled(true);
  static int argc = 1;
  static char arg0[] = "fabricad-fuzz";
  static char* argv[] = {arg0, nullptr};
  static auto* app = new QApplication(argc, argv);
  (void)app;
  qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) {});
  core::setLogSink([](core::LogLevel, const std::string&) {});
  core::setBatchMode(true);
  if (registerModules) {
    static core::ToolRegistry registry;
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    sketch::registerModule(ctx);
    part::registerModule(ctx);
    assembly::registerModule(ctx);
    drawing::registerModule(ctx);
  }
  return path;
}

QString writeInput(const QString& name, const uint8_t* data, size_t size) {
  const QString file = fuzzEnvironment(false) + "/" + name;
  QFile f(file);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    f.close();
  }
  return file;
}

}  // namespace fabricad::fuzz
