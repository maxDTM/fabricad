// FabriCAD part module: OCCT helpers shared by the part features.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "part/Naming.h"
#include "part/PartFeature.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <string>
#include <vector>

class BRepTools_History;

namespace fabricad::core {
class RecomputeContext;
}

namespace fabricad::part {

constexpr double kFuzzy = 1e-7;

/// All solids of a shape (a solid itself, or the solids of a compound/compsolid).
std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& s);
/// Faces/edges/vertices in TopTools_IndexedMapOfShape order.
std::vector<TopoDS_Shape> facesOf(const TopoDS_Shape& s);
std::vector<TopoDS_Shape> edgesOf(const TopoDS_Shape& s);
bool hasSolid(const TopoDS_Shape& s);
/// A single solid stays a solid; several become a compound; none -> null.
TopoDS_Shape solidsToShape(const std::vector<TopoDS_Shape>& solids);
/// Compound of shapes (null skipped), null when empty.
TopoDS_Shape compoundOf(const std::vector<TopoDS_Shape>& shapes);

/// Oriented normal of a planar face (nullopt for non-planar).
std::optional<gp_Dir> faceNormal(const TopoDS_Shape& face);
/// Plane of a planar face with origin at its centroid (core::facePlane) or nullopt.
std::optional<gp_Ax3> planeOf(const TopoDS_Shape& face);
/// Outer wire of a face.
TopoDS_Wire outerWire(const TopoDS_Face& f);
/// Wire from a list of edges (BRepBuilderAPI_MakeWire); null + error text when not connected.
TopoDS_Wire wireFromEdges(const std::vector<TopoDS_Shape>& edges, std::string* error = nullptr);
/// Edges contained in `shape` (edge, wire, compound of edges...).
std::vector<TopoDS_Shape> collectEdges(const TopoDS_Shape& shape);

/// Bounding box diagonal of everything in the state plus `extra` shapes (>= 1).
double sceneSize(const core::RecomputeContext& ctx, const std::vector<TopoDS_Shape>& extra = {});
/// Length that passes through everything: 2 * sceneSize + margin.
double throughAllLength(const core::RecomputeContext& ctx, const std::vector<TopoDS_Shape>& extra = {});

/// Square face on a plane, centred at plane origin (construction plane display).
TopoDS_Face planeDisplayFace(const gp_Ax3& plane, double halfSize);
/// Edge along an axis, centred at its location.
TopoDS_Shape axisDisplayEdge(const gp_Ax1& axis, double halfLength);
TopoDS_Shape pointDisplayVertex(const gp_Pnt& p);

/// Result of a boolean with history.
struct BoolResult {
  TopoDS_Shape shape;
  Handle(BRepTools_History) history;  ///< boolean history followed by unify history
  std::string error;                   ///< non-empty on failure
  explicit operator bool() const { return error.empty() && !shape.IsNull(); }
};

/// Fuse/cut/common with fuzzy value, parallel mode, then ShapeUpgrade_UnifySameDomain and a
/// BRepCheck validity check. `unify` can be disabled for shapes where merging faces hurts.
BoolResult booleanOp(Operation op, const TopoDS_Shape& target, const std::vector<TopoDS_Shape>& tools, bool unify = true);
/// Fuse several shapes together (first = argument, rest = tools).
BoolResult fuseAll(const std::vector<TopoDS_Shape>& shapes, bool unify = true);

/// ShapeUpgrade_UnifySameDomain with history (core::unifySameDomain lacks the history).
TopoDS_Shape unifyWithHistory(const TopoDS_Shape& s, Handle(BRepTools_History)& history);

/// Do two shapes touch/overlap? (bounding boxes first, then BRepExtrema distance <= tol)
bool shapesTouch(const TopoDS_Shape& a, const TopoDS_Shape& b, double tol = 1e-6);

/// The bodies of `candidates` that belong to component `comp` or one of its descendants. The root
/// component (or an unknown one) scopes nothing: every candidate is returned.
std::vector<core::ObjectId> bodiesInComponentTree(const core::Document& doc, core::ObjectId comp,
                                                  const std::vector<core::ObjectId>& candidates);
/// Bodies a feature's automatic boolean may take part in (Fusion: the component the feature is
/// made in and its children; a feature in the root sees every body).
std::vector<core::ObjectId> bodiesInScope(core::RecomputeContext& ctx);

/// Apply an additive/subtractive tool to the document bodies.
///  * NewBody: a new body.
///  * Join: fused with all participating bodies (explicit `targets`, else every body of the
///    feature's component tree that the tool touches, see bodiesInScope()); with no participant
///    a new body is created (Fusion behaviour).
///  * Cut/Intersect: applied to each participant; none -> error.
/// Fills out.bodies and out.names (carrying upstream names through the booleans), sets
/// out.tool/out.operation and adds the tool as preview. Returns false after ctx.setError.
bool applyTool(core::RecomputeContext& ctx, const TopoDS_Shape& tool, Operation op, std::vector<core::ShapeRef>& targets,
               const NameMap& toolNames, Outcome& out, const std::string& newBodyName = {});

/// Replace the shape of one body (Fillet/Chamfer/Shell...): validity check, names mapped
/// through `history`, out.bodies/out.names filled, preview added. False after ctx.setError.
bool replaceBody(core::RecomputeContext& ctx, core::ObjectId bodyId, const TopoDS_Shape& newShape, const History& history,
                 const NameMap& extraNames, Outcome& out, bool unify = false);

/// Group B-rep refs by owner body (resolving each through resolveRef). Refs that fail to
/// resolve are skipped (ctx already carries the warning).
struct ResolvedGroup {
  core::ObjectId body = core::kInvalidId;
  TopoDS_Shape bodyShape;
  std::vector<TopoDS_Shape> shapes;
  std::vector<std::string> names;  ///< history names of the shapes ("" when none)
};
std::vector<ResolvedGroup> resolveByBody(core::RecomputeContext& ctx, std::vector<core::ShapeRef>& refs,
                                         core::ShapeKind expectedKind, bool expandFaceToEdges = false);

/// Profile faces from a RefList of Profile/Face refs (planar faces only). Writes indices back.
std::vector<TopoDS_Face> profileFaces(core::RecomputeContext& ctx, std::vector<core::ShapeRef>& refs, std::string* error);

/// Make a planar face from a closed wire (nullopt if not planar/closed).
TopoDS_Face faceFromWire(const TopoDS_Wire& w);

/// Prism of a planar face along `dir` by `length` (> 0) with an optional taper angle
/// (degrees, positive = outward). Names: prefix+"Start", prefix+"End", prefix+"Side/k" (k =
/// deterministic edge order of the face). Null + error text on failure.
TopoDS_Shape prismFace(const TopoDS_Face& face, const gp_Dir& dir, double length, double taperDeg,
                       const std::string& prefix, NameMap& names, std::string* error);

/// Solid of revolution of a face about `axis` by `angleDeg` (0 < angle <= 360).
/// Rename the prefix+"Side/<k>" faces of a sweep made from a sketch Profile ref to
/// prefix+"Side/c<curveId>" (several pieces of one curve: "#1", "#2"...). The sketch curve id is
/// stable when the sketch is edited, the edge order of the profile face is not. `toSketch` maps
/// the swept face back onto the sketch (inverse of any rotation applied before sweeping; offsets
/// along the sketch normal need no transform). No-op for Face refs or when nothing matches.
void stabilizeSideNames(const core::RecomputeContext& ctx, const core::ShapeRef& profileRef, const std::string& prefix,
                        NameMap& names, const gp_Trsf& toSketch = gp_Trsf());

/// Names like prismFace (no Start/End for a full revolution).
TopoDS_Shape revolveFace(const TopoDS_Face& face, const gp_Ax1& axis, double angleDeg, const std::string& prefix,
                         NameMap& names, std::string* error);

/// Signed distance along `dir` from `from` to the plane `target` (nullopt when parallel).
std::optional<double> distanceToPlane(const gp_Pnt& from, const gp_Dir& dir, const gp_Pln& target);

/// Half-space bounded by the (extended) surface of `face`, on the side of `inside`.
/// `size` bounds infinite surfaces. Null on failure.
TopoDS_Shape halfSpaceOf(const TopoDS_Face& face, const gp_Pnt& inside, double size);

/// Transformed copy of a shape (copy=true so mirrors work).
TopoDS_Shape copyTransformed(const TopoDS_Shape& s, const gp_Trsf& t);

/// Human-friendly direction choice for constructing an orthonormal frame around `axis`.
gp_Dir anyPerpendicular(const gp_Dir& axis);

}  // namespace fabricad::part
