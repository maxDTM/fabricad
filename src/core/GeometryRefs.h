// FabriCAD core: resolve ShapeRefs to planes, axes, points and shapes.
// Works on any body-state map so it can be used during recompute (state before the
// feature) and from the UI (current state).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <map>
#include <optional>

namespace fabricad::core {

class Document;

using BodyStateMap = std::map<ObjectId, TopoDS_Shape>;

/// Plane from: OriginPlane ("XY"/"XZ"/"YZ"), ConstructionPlane/Feature (Feature::asPlane),
/// Sketch (IProfileSource::sketchPlane), planar Face of a body (world coords, origin at centroid).
std::optional<gp_Ax3> resolvePlane(const Document& doc, const ShapeRef& ref, const BodyStateMap* state = nullptr);

/// Axis from: OriginAxis ("X"/"Y"/"Z"), ConstructionAxis/Feature (asAxis), linear Edge,
/// circular Edge (its axis), cylindrical/conical Face (its axis), SketchCurve (line).
std::optional<gp_Ax1> resolveAxis(const Document& doc, const ShapeRef& ref, const BodyStateMap* state = nullptr);

/// Point from: OriginPoint, ConstructionPoint/Feature (asPoint), Vertex, SketchPoint,
/// circular Edge (centre), or the ref's stored pick point as last resort.
std::optional<gp_Pnt> resolvePoint(const Document& doc, const ShapeRef& ref, const BodyStateMap* state = nullptr);

/// B-rep sub-shape / body shape / profile face / sketch curve edge / feature output shape.
/// For B-rep refs uses resolveSubShape (index first, then signature). Null when not found.
TopoDS_Shape resolveShape(const Document& doc, const ShapeRef& ref, const BodyStateMap* state = nullptr,
                          ResolveResult* detail = nullptr);

/// Face for a Profile ref (via the sketch's IProfileSource) or a planar Face ref.
TopoDS_Face resolveProfileFace(const Document& doc, const ShapeRef& ref, const BodyStateMap* state = nullptr);

/// Build a ShapeRef for a picked B-rep sub-shape of a body in the given state.
ShapeRef makeBrepRef(const Document& doc, ObjectId bodyId, const TopoDS_Shape& sub, const BodyStateMap* state = nullptr);

}  // namespace fabricad::core
