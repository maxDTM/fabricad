// FabriCAD interop plugin: OCCT IGES / OBJ / glTF translators.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "interop/InteropPlugin.h"

#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <Message_ProgressRange.hxx>
#include <RWGltf_CafWriter.hxx>
#include <RWObj_CafWriter.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <XSControl_WorkSession.hxx>

#include <cstdio>
#include <string>

namespace {
void setErr(char* err, int len, const std::string& m) {
  if (err && len > 0) std::snprintf(err, static_cast<size_t>(len), "%s", m.c_str());
}
std::string what(const Standard_Failure& e) { return e.GetMessageString() ? e.GetMessageString() : "OCCT error"; }
}  // namespace

extern "C" {

int fabricad_interop_version() { return kFabricadInteropVersion; }

bool fabricad_interop_write_caf(TDocStd_Document* doc, const char* path, int format, bool yUp, const char* title, const char* author,
                                char* err, int errLen) {
  try {
    Handle(TDocStd_Document) d(doc);
    TColStd_IndexedDataMapOfStringString info;
    if (title && *title) info.Add("Title", title);
    if (author && *author) info.Add("Author", author);
    info.Add("Generator", "FabriCAD");
    bool ok = false;
    if (format == 0) {
      RWObj_CafWriter w(path);
      w.ChangeCoordinateSystemConverter().SetInputLengthUnit(0.001);
      w.ChangeCoordinateSystemConverter().SetOutputLengthUnit(0.001);
      if (yUp) {
        w.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
        w.ChangeCoordinateSystemConverter().SetOutputCoordinateSystem(RWMesh_CoordinateSystem_Yup);
      }
      ok = w.Perform(d, info, Message_ProgressRange());
    } else {
      RWGltf_CafWriter w(path, format == 2);
      // glTF: metres, Y up; FabriCAD: millimetres, Z up
      w.ChangeCoordinateSystemConverter().SetInputLengthUnit(0.001);
      w.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
      w.SetNodeNameFormat(RWMesh_NameFormat_InstanceOrProduct);
      w.SetMeshNameFormat(RWMesh_NameFormat_Product);
      ok = w.Perform(d, info, Message_ProgressRange());
    }
    if (!ok) setErr(err, errLen, std::string("could not write ") + path);
    return ok;
  } catch (const Standard_Failure& e) {
    setErr(err, errLen, what(e));
    return false;
  }
}

bool fabricad_interop_write_iges(const TopoDS_Shape* shapes, int count, const char* path, bool brep, char* err, int errLen) {
  try {
    IGESControl_Writer writer("MM", brep ? 1 : 0);
    for (int i = 0; i < count; ++i) {
      if (shapes[i].IsNull()) continue;
      if (!writer.AddShape(shapes[i])) {
        setErr(err, errLen, "IGES transfer failed");
        return false;
      }
    }
    writer.ComputeModel();
    if (!writer.Write(path)) {
      setErr(err, errLen, std::string("could not write ") + path);
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    setErr(err, errLen, "IGES export failed: " + what(e));
    return false;
  }
}

bool fabricad_interop_read_iges(const char* path, TopoDS_Shape* out, int* entities, int* roots, char* err, int errLen) {
  try {
    IGESControl_Reader reader;
    if (reader.ReadFile(path) != IFSelect_RetDone) {
      setErr(err, errLen, "could not read IGES file");
      return false;
    }
    if (entities) *entities = reader.WS()->Model().IsNull() ? 0 : reader.WS()->Model()->NbEntities();
    if (roots) *roots = reader.NbRootsForTransfer();
    reader.TransferRoots();
    // one shape for the whole file: trimmed-surface IGES has a root per face, sewn by the caller
    *out = reader.NbShapes() > 0 ? reader.OneShape() : TopoDS_Shape();
    if (out->IsNull()) {
      setErr(err, errLen, "IGES file contains no shapes");
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    setErr(err, errLen, "IGES import failed: " + what(e));
    return false;
  }
}
}
