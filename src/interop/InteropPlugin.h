// FabriCAD interop plugin interface (see CMakeLists.txt): plain C entry points taking OCCT
// objects by pointer. Loaded by part/io/InteropLoader.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

class TDocStd_Document;
class TopoDS_Shape;

extern "C" {
/// Version of this interface (the loader refuses a different one).
int fabricad_interop_version();
/// Write an XCAF document with meshed shapes as OBJ (format 0) / glTF (1) / GLB (2).
/// yUp: OBJ only. Returns true on success; `err` (size errLen) gets a message on failure.
bool fabricad_interop_write_caf(TDocStd_Document* doc, const char* path, int format, bool yUp, const char* title,
                                const char* author, char* err, int errLen);
/// Write shapes to IGES (brep: 186/514, else trimmed surfaces 144).
bool fabricad_interop_write_iges(const TopoDS_Shape* shapes, int count, const char* path, bool brep, char* err, int errLen);
/// Read an IGES file into one shape; entities / roots of the file model.
bool fabricad_interop_read_iges(const char* path, TopoDS_Shape* out, int* entities, int* roots, char* err, int errLen);
}

inline constexpr int kFabricadInteropVersion = 1;
