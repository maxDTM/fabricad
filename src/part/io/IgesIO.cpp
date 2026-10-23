// FabriCAD part I/O: IGES import / export.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/io/IgesIO.h"

#include "part/io/InteropLoader.h"

#include <format>

namespace fabricad::part::io {

bool applyIgesOption(const std::string& word, bool& brep) {
  std::string w = word;
  for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (w.starts_with("mode=")) w = w.substr(5);
  if (w == "faces" || w == "surfaces" || w == "trimmed") {
    brep = false;
    return true;
  }
  if (w == "brep" || w == "solids" || w == "msbo") {
    brep = true;
    return true;
  }
  return false;
}

bool exportIges(const std::vector<TopoDS_Shape>& shapes, const std::string& path, bool brep, std::string* error) {
  if (shapes.empty()) {
    if (error) *error = "nothing to export";
    return false;
  }
  const InteropApi* api = interopApi(error);
  if (!api) return false;
  char err[512] = {0};
  if (!api->writeIges(shapes.data(), static_cast<int>(shapes.size()), path.c_str(), brep, err, sizeof err)) {
    if (error) *error = err;
    return false;
  }
  return true;
}

bool readIges(const std::string& path, const ImportOptions& options, std::vector<TopoDS_Shape>& shapes, ImportReport* report,
              std::string* error) {
  shapes.clear();
  ImportReport r;
  r.file = path;
  r.format = "IGES";
  const InteropApi* api = interopApi(error);
  if (!api) return false;
  char err[512] = {0};
  TopoDS_Shape all;
  if (!api->readIges(path.c_str(), &all, &r.entities, &r.roots, err, sizeof err)) {
    if (error) *error = err;
    return false;
  }
  shapes.push_back(all);
  // IGES surfaces are always sewn: a trimmed-surface file is the normal case, not damage
  ImportOptions o = options;
  o.sew = true;
  healShapes(shapes, o, r);
  if (r.openShells > 0) r.notes.push_back(std::format("{} open shell(s) stay surface bodies (not closed after sewing)", r.openShells));
  if (report) *report = r;
  return true;
}

}  // namespace fabricad::part::io
