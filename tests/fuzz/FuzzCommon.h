// FabriCAD fuzz drivers: shared set-up (docs/TESTING.md "Fuzzing").
//
// Every driver defines LLVMFuzzerTestOneInput / LLVMFuzzerInitialize. With clang they link
// libFuzzer (-fsanitize=fuzzer); otherwise DriverMain.cpp supplies main(): it replays the
// given corpus files and runs a bounded random mutation loop over them.
//
// fuzzEnvironment() makes every run private: offscreen Qt, a fresh temporary working directory,
// HOME / XDG dirs inside it and QStandardPaths test mode (no user settings are read or written),
// FabriCAD log output muted, all modules registered (feature types needed by the loader).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>

namespace fabricad::fuzz {

/// Once per process: QApplication + private temp dir + modules. Returns the temp dir path.
const QString& fuzzEnvironment(bool registerModules = true);
/// Write `data` to <tempdir>/<name> and return the absolute path.
QString writeInput(const QString& name, const uint8_t* data, size_t size);

}  // namespace fabricad::fuzz

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
