// FabriCAD assembly: STEP export of the component tree through XCAF (names, colours,
// shared definitions for occurrences). See docs/research/algorithms.md section 5.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Types.h"

#include <string>

namespace fabricad::assembly {

struct StepExportOptions {
  core::ObjectId root = core::kRootComponentId;  ///< sub-tree to export
  std::string schema = "AP214";                  ///< "AP203", "AP214", "AP242"
  bool colors = true;
  bool names = true;
  bool visibleOnly = true;
};

struct StepExportStats {
  int parts = 0;        ///< distinct part definitions written
  int instances = 0;    ///< component instances
  int bodies = 0;
};

/// Write the assembly as a STEP file. Returns false with `error` set on failure.
bool exportAssemblyStep(const core::Document& doc, const std::string& path, const StepExportOptions& options = {},
                        std::string* error = nullptr, StepExportStats* stats = nullptr);

}  // namespace fabricad::assembly
