// FabriCAD part I/O: IGES import / export.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/io/IgesIO.h"

#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <Standard_Failure.hxx>
#include <XSControl_WorkSession.hxx>

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
  try {
    Interface_Static::SetCVal("write.iges.unit", "MM");
    Interface_Static::SetIVal("write.iges.brep.mode", brep ? 1 : 0);
    IGESControl_Writer writer("MM", brep ? 1 : 0);
    for (const auto& s : shapes) {
      if (s.IsNull()) continue;
      if (!writer.AddShape(s)) {
        if (error) *error = "IGES transfer failed";
        return false;
      }
    }
    writer.ComputeModel();
    if (!writer.Write(path.c_str())) {
      if (error) *error = "could not write " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("IGES export failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
}

bool readIges(const std::string& path, const ImportOptions& options, std::vector<TopoDS_Shape>& shapes, ImportReport* report,
              std::string* error) {
  shapes.clear();
  ImportReport r;
  r.file = path;
  r.format = "IGES";
  IGESControl_Reader reader;
  if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
    if (error) *error = "could not read IGES file";
    return false;
  }
  reportModel(reader.WS(), r);
  r.roots = reader.NbRootsForTransfer();
  reader.TransferRoots();
  // one shape for the whole file: trimmed-surface IGES has a root per face, sewn together below
  TopoDS_Shape all = reader.NbShapes() > 0 ? reader.OneShape() : TopoDS_Shape();
  if (!all.IsNull()) shapes.push_back(all);
  if (shapes.empty()) {
    if (error) *error = "IGES file contains no shapes";
    return false;
  }
  // IGES surfaces are always sewn: a trimmed-surface file is the normal case, not damage
  ImportOptions o = options;
  o.sew = true;
  healShapes(shapes, o, r);
  if (r.openShells > 0) r.notes.push_back(std::format("{} open shell(s) stay surface bodies (not closed after sewing)", r.openShells));
  if (report) *report = r;
  return true;
}

}  // namespace fabricad::part::io
