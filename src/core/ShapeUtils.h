// FabriCAD core: small OCCT helpers shared by all modules.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::core {

/// BRepCheck_Analyzer validity. Null shapes are invalid.
bool isValidShape(const TopoDS_Shape& s);
/// Bounding box diagonal (0 for null/void). Uses the triangulation-free bound.
double bboxDiagonal(const TopoDS_Shape& s);
bool bbox(const TopoDS_Shape& s, Vec3& min, Vec3& max);
/// ShapeUpgrade_UnifySameDomain (merge coplanar faces / colinear edges). Returns input on failure.
TopoDS_Shape unifySameDomain(const TopoDS_Shape& s, bool unifyEdges = true, bool unifyFaces = true);
/// BRepBuilderAPI_Transform with copy=false.
TopoDS_Shape transformed(const TopoDS_Shape& s, const gp_Trsf& t);
/// Volume of all solids (0 for non-solids).
double volume(const TopoDS_Shape& s);
double area(const TopoDS_Shape& s);
/// Centre of mass (volume for solids, area for faces, length for edges).
Vec3 centroid(const TopoDS_Shape& s);
/// Collect sub-shapes of a type in TopTools_IndexedMapOfShape order.
std::vector<TopoDS_Shape> subShapes(const TopoDS_Shape& s, TopAbs_ShapeEnum type);
/// Rectangular face on a plane (for displaying construction/origin planes).
TopoDS_Face planeFace(const gp_Ax3& plane, double halfSize);
/// Plane of a planar face (normal follows face orientation), nullopt if not planar.
std::optional<gp_Ax3> facePlane(const TopoDS_Shape& face);
/// Make a compound from shapes (skips null shapes). Returns null if nothing added.
TopoDS_Shape makeCompound(const std::vector<TopoDS_Shape>& shapes);
/// Stable-ish content hash of a shape (TShape pointers + location); for cache keys only.
std::size_t shapeHash(const TopoDS_Shape& s);
/// Human readable type ("Solid", "Face"...).
std::string shapeTypeName(const TopoDS_Shape& s);

/// The three origin planes. XY: N=+Z,X=+X. XZ: N=-Y,X=+X (so plane Y = +Z, i.e. the
/// "Front" view). YZ: N=+X,X=+Y (plane Y = +Z, the "Right" view).
gp_Ax3 originPlane(const std::string& name);  ///< "XY" | "XZ" | "YZ"; unknown -> XY
gp_Ax1 originAxis(const std::string& name);   ///< "X" | "Y" | "Z"; unknown -> Z

}  // namespace fabricad::core
