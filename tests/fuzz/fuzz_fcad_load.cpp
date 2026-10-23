// Fuzz the .fcad loader: Document::load (JSON parse, every registered feature type's fromJson,
// the recompute that load runs) and a save/load round trip of whatever loaded.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FuzzCommon.h"

#include "core/Document.h"
#include "core/Json.h"

#include <QJsonDocument>

#include <string>

using namespace fabricad;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  fuzz::fuzzEnvironment(true);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > (1u << 20)) return -1;
  fuzz::fuzzEnvironment(true);
  const QString path = fuzz::writeInput("input.fcad", data, size);
  core::Document doc;
  std::string error;
  if (doc.load(path.toStdString(), &error)) {
    // round trip: what we write must load again
    const QJsonObject json = doc.toJson();
    core::Document again;
    std::string error2;
    if (!again.fromJson(json, &error2)) __builtin_trap();
  }
  // the generic string helper used by feature properties
  std::string err;
  (void)core::jsonFromString(std::string(reinterpret_cast<const char*>(data), size), &err);
  return 0;
}
