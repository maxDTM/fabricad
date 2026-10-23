// FabriCAD part I/O: mesh export formats beyond STL (production-features.md P3).
//
// OBJ (+ .mtl, RWObj_CafWriter), glTF 2.0 / GLB (RWGltf_CafWriter, PBR base colour from the body
// colour, one node per body grouped under one node per component) and 3MF (core spec 1.x, the
// XML written here and packed into a ZIP with "stored" entries: no compression library needed,
// every 3MF consumer reads stored members). All formats mesh fresh copies of the bodies with the
// STL quality settings (linear / angular deflection).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::part::io {

enum class MeshFormat { Stl, Obj, Gltf, Glb, ThreeMf };

/// Format from the file extension (.stl .obj .gltf .glb .3mf), case-insensitive.
std::optional<MeshFormat> meshFormatFromPath(const std::string& path);
const char* meshFormatName(MeshFormat f);  ///< "STL", "OBJ", "glTF", "GLB", "3MF"

struct MeshItem {
  TopoDS_Shape shape;                                   ///< world placement
  std::string name;                                     ///< body name
  std::array<float, 4> color{0.73f, 0.74f, 0.77f, 1.0f};
  std::string component;                                ///< owning component name; empty = top level
};

struct MeshExportOptions {
  double linearDeflection = 0.05;      ///< mm
  double angularDeflectionDeg = 11.459155902616464;
  bool binaryStl = true;
  bool yUp = false;                     ///< OBJ: rotate Z-up to Y-up (glTF is always Y-up, metres)
  std::string title;                    ///< 3MF / glTF metadata
  std::string designer;
};

struct MeshExportStats {
  int objects = 0;
  long long triangles = 0;
  long long vertices = 0;
};

/// Write the items in the given format. False with `error` set on failure.
bool exportMesh(const std::vector<MeshItem>& items, const std::string& path, MeshFormat format, const MeshExportOptions& options,
                std::string* error = nullptr, MeshExportStats* stats = nullptr);

/// Triangulate one shape (fresh copy, the given deflections) into an indexed mesh with shared
/// vertices (coincident nodes of neighbouring faces merged), outward-facing triangles.
struct IndexedMesh {
  std::vector<std::array<double, 3>> vertices;
  std::vector<std::array<int, 3>> triangles;
};
IndexedMesh triangulate(const TopoDS_Shape& shape, double linearDeflection, double angularDeflectionDeg);

/// 3MF model XML (3D/3dmodel.model) for the items; exposed for tests.
std::string threeMfModelXml(const std::vector<MeshItem>& items, const std::vector<IndexedMesh>& meshes, const MeshExportOptions& options);

/// Minimal ZIP (PKZIP 2.0, stored entries, CRC-32). writeZip returns false on an I/O error.
bool writeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& members);
/// Read the members of a ZIP written with stored entries (and check their CRCs). Empty on failure.
std::map<std::string, std::string> readZipStored(const std::string& path);
unsigned int crc32(const std::string& data);

}  // namespace fabricad::part::io
