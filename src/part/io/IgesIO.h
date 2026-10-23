// FabriCAD part I/O: IGES 5.3 import / export (TKDEIGES).
//
// Export: BRep solids (entities 186 / 514, the default, read by every current CAD) or trimmed
// surfaces (144 / 143, "faces", for older CAM). Import: every transferable root; loose faces are
// sewn and closed shells become solids (the import report says which bodies stayed surfaces).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/io/StepIO.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace fabricad::part::io {

bool exportIges(const std::vector<TopoDS_Shape>& shapes, const std::string& path, bool brep, std::string* error = nullptr);
/// Read an IGES file. One shape per transferred root (after healing). False with `error` set.
bool readIges(const std::string& path, const ImportOptions& options, std::vector<TopoDS_Shape>& shapes, ImportReport* report,
              std::string* error = nullptr);
/// "faces" / "surfaces" -> false (trimmed surfaces), "brep" / "solids" -> true. False if not an option.
bool applyIgesOption(const std::string& word, bool& brep);

}  // namespace fabricad::part::io
