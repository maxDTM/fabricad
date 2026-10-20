// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Import.h"

#include "core/Feature.h"
#include "core/Log.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <QDateTime>
#include <QFileInfo>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace fabricad::part {

namespace {

std::string lowerExt(const std::string& path) {
  std::string ext = QFileInfo(QString::fromStdString(path)).suffix().toStdString();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

/// Closed shells read from STL become solids so booleans can work on them.
TopoDS_Shape solidify(const TopoDS_Shape& s) {
  if (s.IsNull() || hasSolid(s)) return s;
  try {
    BRepBuilderAPI_Sewing sew(1e-3);
    sew.Add(s);
    sew.Perform();
    TopoDS_Shape sewn = sew.SewedShape();
    if (sewn.IsNull()) return s;
    std::vector<TopoDS_Shape> solids;
    for (TopExp_Explorer ex(sewn, TopAbs_SHELL); ex.More(); ex.Next()) {
      TopoDS_Shell shell = TopoDS::Shell(ex.Current());
      if (!shell.Closed()) continue;
      BRepBuilderAPI_MakeSolid ms(shell);
      if (ms.IsDone()) solids.push_back(ms.Solid());
    }
    if (solids.empty()) return sewn;
    return solidsToShape(solids);
  } catch (const Standard_Failure&) {
    return s;
  }
}

}  // namespace

ImportFeature::ImportFeature() : PartFeature(kType) {
  addString("filePath", "").setLabel("File");
  addEnum("format", {"Auto", "STEP", "STL"}, 0).setLabel("Format");
  addBool("oneBodyPerSolid", true).setLabel("One body per solid");
  addDouble("scale", 1.0).setLabel("Scale");
}

bool ImportFeature::readFile(const std::string& path, Imported& out, std::string* error) {
  out = {};
  std::string ext = lowerExt(path);
  QFileInfo fi(QString::fromStdString(path));
  if (!fi.exists()) {
    if (error) *error = "file not found: " + path;
    return false;
  }
  std::string baseName = fi.completeBaseName().toStdString();
  try {
    if (ext == "stl") {
      StlAPI_Reader reader;
      TopoDS_Shape s;
      if (!reader.Read(s, path.c_str()) || s.IsNull()) {
        if (error) *error = "could not read STL file";
        return false;
      }
      out.shapes.push_back(solidify(s));
      out.names.push_back(baseName);
      return true;
    }
    if (ext == "step" || ext == "stp") {
      STEPControl_Reader reader;
      if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
        if (error) *error = "could not read STEP file";
        return false;
      }
      reader.TransferRoots();
      int n = reader.NbShapes();
      if (n == 0) {
        if (error) *error = "STEP file contains no shapes";
        return false;
      }
      for (int i = 1; i <= n; ++i) {
        TopoDS_Shape s = reader.Shape(i);
        if (s.IsNull()) continue;
        std::vector<TopoDS_Shape> solids = solidsOf(s);
        if (solids.empty()) {
          out.shapes.push_back(s);
          out.names.push_back(n == 1 ? baseName : std::format("{} {}", baseName, i));
          continue;
        }
        for (size_t k = 0; k < solids.size(); ++k) {
          out.shapes.push_back(solids[k]);
          out.names.push_back(solids.size() == 1 && n == 1 ? baseName : std::format("{} {}", baseName, out.shapes.size()));
        }
      }
      return !out.shapes.empty();
    }
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("import failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
  if (error) *error = "unsupported file type: " + ext;
  return false;
}

void ImportFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::string path = prop("filePath").toString();
  if (path.empty()) {
    ctx.setError("choose a file to import");
    return;
  }
  QFileInfo fi(QString::fromStdString(path));
  long long stamp = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -2;
  if (path != cachedPath_ || stamp != cachedStamp_ || cache_.shapes.empty()) {
    std::string err;
    Imported imported;
    if (!readFile(path, imported, &err)) {
      ctx.setError(err);
      return;
    }
    cache_ = std::move(imported);
    cachedPath_ = path;
    cachedStamp_ = stamp;
  }
  double scale = prop("scale").toDouble();
  bool perSolid = flag("oneBodyPerSolid");
  std::vector<TopoDS_Shape> shapes = cache_.shapes;
  if (std::abs(scale - 1.0) > 1e-12 && scale > 0) {
    gp_Trsf t;
    t.SetScale(gp_Pnt(0, 0, 0), scale);
    for (auto& s : shapes) s = copyTransformed(s, t);
  }
  const std::string base = name().empty() ? type() : name();
  if (!perSolid && shapes.size() > 1) {
    TopoDS_Shape all = compoundOf(shapes);
    out.addNew(all, cache_.names.front());
    int fi2 = 0;
    for (const auto& f : facesOf(all)) out.names[std::format("{}/F{}", base, ++fi2)] = f;
    out.addPreview(all, Operation::NewBody);
    return;
  }
  for (size_t i = 0; i < shapes.size(); ++i) {
    out.addNew(shapes[i], cache_.names[i]);
    int fi2 = 0;
    for (const auto& f : facesOf(shapes[i])) out.names[std::format("{}/B{}/F{}", base, i + 1, ++fi2)] = f;
    out.addPreview(shapes[i], Operation::NewBody);
  }
}

// ---------------------------------------------------------------------------
// export
// ---------------------------------------------------------------------------

bool exportStep(const std::vector<TopoDS_Shape>& shapes, const std::vector<std::string>& names, const std::string& path,
                std::string* error) {
  (void)names;  // product names need XCAF; plain writer for now
  if (shapes.empty()) {
    if (error) *error = "nothing to export";
    return false;
  }
  try {
    Interface_Static::SetCVal("write.step.schema", "AP214");
    Interface_Static::SetCVal("write.step.unit", "MM");
    STEPControl_Writer writer;
    for (const auto& s : shapes) {
      if (s.IsNull()) continue;
      if (writer.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) {
        if (error) *error = "STEP transfer failed";
        return false;
      }
    }
    if (writer.Write(path.c_str()) != IFSelect_RetDone) {
      if (error) *error = "could not write " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("STEP export failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
}

bool exportStl(const std::vector<TopoDS_Shape>& shapes, const std::string& path, bool binary, double linearDeflection,
               double angularDeflectionDeg, std::string* error) {
  if (shapes.empty()) {
    if (error) *error = "nothing to export";
    return false;
  }
  try {
    TopoDS_Shape all = shapes.size() == 1 ? shapes.front() : compoundOf(shapes);
    if (all.IsNull()) {
      if (error) *error = "nothing to export";
      return false;
    }
    // Mesh a fresh copy (geometry shared, no triangulation): BRepMesh keeps an existing mesh when the
    // linear deflection is not coarser and ignores the angle then, so a body that was already
    // meshed (viewer display, an earlier export) would be written with the old tessellation.
    BRepBuilderAPI_Copy copier(all, Standard_True, Standard_False);
    TopoDS_Shape fresh = copier.IsDone() ? copier.Shape() : all;
    if (fresh.IsNull()) fresh = all;
    BRepMesh_IncrementalMesh mesh(fresh, linearDeflection, Standard_False, angularDeflectionDeg * M_PI / 180.0, Standard_True);
    (void)mesh;
    StlAPI_Writer writer;
    writer.ASCIIMode() = binary ? Standard_False : Standard_True;
    if (!writer.Write(fresh, path.c_str())) {
      if (error) *error = "could not write " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("STL export failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
}

}  // namespace fabricad::part
