// FabriCAD sketch: OCCT bridge. Sketch curves -> 3D edges on the sketch plane, and the
// planar arrangement -> closed regions (profiles) as faces.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <vector>

namespace fabricad::sketch {

/// A closed region of the planar arrangement of the (non-construction) sketch curves.
struct ProfileRegion {
  TopoDS_Face face;             ///< world coordinates, lies on the sketch plane
  Pt2 seed;                     ///< a point strictly inside the region (sketch coords)
  std::vector<int> curveIds;    ///< sketch curves bounding the region (sorted)
  double area = 0;
};

gp_Pnt toWorld(const gp_Ax3& plane, Pt2 p);
Pt2 toLocal(const gp_Ax3& plane, const gp_Pnt& p);

/// 3D edge for a curve (null shape for points / degenerate curves).
TopoDS_Shape makeEdge(const Geometry& g, const gp_Ax3& plane);
/// Vertex for a point geometry.
TopoDS_Shape makeVertex(const Geometry& g, const gp_Ax3& plane);
/// Compound of all curve edges (+ vertices for points). Null when empty.
TopoDS_Shape makeEdgesCompound(const Sketch& sketch, const gp_Ax3& plane, bool includeConstruction);

/// All bounded regions of the arrangement of the non-construction curves.
std::vector<ProfileRegion> computeProfiles(const Sketch& sketch, const gp_Ax3& plane);

/// True if the (world) point lies inside the face.
bool faceContains(const TopoDS_Face& face, const gp_Pnt& p, double tol = 1e-6);

/// Project a B-rep vertex or edge onto the sketch plane: vertex -> Point, linear edge -> Line,
/// circular edge parallel to the plane -> Circle/Arc. Other curves are approximated by their
/// chord (a note goes to *warning). nullopt for unsupported shapes. The result has no id and
/// its `projected`/`source` fields are left for the caller.
std::optional<Geometry> projectShape(const TopoDS_Shape& shape, const gp_Ax3& plane, std::string* warning = nullptr);

}  // namespace fabricad::sketch
