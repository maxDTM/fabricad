// FabriCAD part I/O: interop plugin loader.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/io/InteropLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <mutex>

#ifndef FABRICAD_INTEROP_BUILD_DIR
#define FABRICAD_INTEROP_BUILD_DIR ""
#endif

namespace fabricad::part::io {

const InteropApi* interopApi(std::string* error) {
  static std::once_flag once;
  static InteropApi api;
  static bool ok = false;
  static std::string loadError;
  std::call_once(once, [] {
    QStringList candidates;
    if (const QByteArray env = qgetenv("FABRICAD_INTEROP_PLUGIN"); !env.isEmpty()) candidates << QString::fromLocal8Bit(env);
    const QString app = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
    if (!app.isEmpty())
      candidates << app + "/../lib/fabricad/libfabricad_interop" << app + "/../lib/libfabricad_interop" << app + "/libfabricad_interop";
    if (*FABRICAD_INTEROP_BUILD_DIR) candidates << QString(FABRICAD_INTEROP_BUILD_DIR) + "/libfabricad_interop";
    QString tried;
    for (const QString& c : candidates) {
      auto* lib = new QLibrary(c);  // never unloaded: OCCT keeps static state
      if (!lib->load()) {
        tried += "\n  " + lib->errorString();
        delete lib;
        continue;
      }
      using VersionFn = int (*)();
      auto version = reinterpret_cast<VersionFn>(lib->resolve("fabricad_interop_version"));
      api.writeCaf = reinterpret_cast<decltype(api.writeCaf)>(lib->resolve("fabricad_interop_write_caf"));
      api.writeIges = reinterpret_cast<decltype(api.writeIges)>(lib->resolve("fabricad_interop_write_iges"));
      api.readIges = reinterpret_cast<decltype(api.readIges)>(lib->resolve("fabricad_interop_read_iges"));
      if (version && version() == 1 && api.writeCaf && api.writeIges && api.readIges) {
        ok = true;
        return;
      }
      tried += "\n  " + c + ": wrong plugin version";
    }
    loadError = ("the IGES / OBJ / glTF plugin (libfabricad_interop) could not be loaded:" + tried).toStdString();
  });
  if (!ok && error) *error = loadError;
  return ok ? &api : nullptr;
}

}  // namespace fabricad::part::io
