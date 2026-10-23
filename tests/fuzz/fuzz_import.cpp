// Fuzz the STEP / STL import entry points: ImportFeature::readFile (bodies) and, for STEP,
// ImportFeature::readStepAssembly (XCAF product structure). FUZZ_IMPORT_EXT selects the file
// extension the input is written with (".step" or ".stl"), which picks the reader.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FuzzCommon.h"

#include "part/features/Import.h"

#include <string>

#ifndef FUZZ_IMPORT_EXT
#define FUZZ_IMPORT_EXT ".step"
#endif

using namespace fabricad;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  fuzz::fuzzEnvironment(false);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > (1u << 20)) return -1;
  fuzz::fuzzEnvironment(false);
  // a new name per run: readStepAssembly caches by path + modification time
  static unsigned counter = 0;
  const QString name = QString("input%1" FUZZ_IMPORT_EXT).arg(counter++ % 2);
  const std::string path = fuzz::writeInput(name, data, size).toStdString();
  part::ImportFeature::Imported out;
  std::string error;
  (void)part::ImportFeature::readFile(path, out, &error);
  if (std::string(FUZZ_IMPORT_EXT) == ".step") {
    std::string error2;
    (void)part::ImportFeature::readStepAssembly(path, &error2);
  }
  return 0;
}
