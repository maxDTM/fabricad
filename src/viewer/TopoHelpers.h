// FabriCAD viewer: small topology helpers for selection (GL-free, unit-tested).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace fabricad::viewer {

/// Edges of `shape` connected to `seed` through vertices where the edges are tangent (G1
/// within `angleTolDeg`). Used for double-click "select tangent chain". Includes `seed`.
std::vector<TopoDS_Edge> tangentChain(const TopoDS_Shape& shape, const TopoDS_Edge& seed, double angleTolDeg = 2.0);

}  // namespace fabricad::viewer
