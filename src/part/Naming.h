// FabriCAD part module: topological naming, layer 1 (history names).
//
// Each part feature produces a NameMap (name -> sub-shape) built from the OCCT maker it
// used ("Extrude1/End", "Extrude1/Side/3", "Fillet1/Extrude1/Side/3") and carries the names
// of the body it modified through the boolean's Modified() lists. Edges are named after
// their two adjacent named faces ("{A|B}"). The cumulative map of a body lives in the last
// feature that touched it (Feature::namedShapes()), so later features can re-find a
// referenced sub-shape by name before falling back to the geometric signature (layer 2,
// core::resolveSubShape).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Feature.h"
#include "core/ShapeRef.h"
#include "core/Types.h"

#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Shape.hxx>

#include <functional>
#include <map>
#include <string>
#include <vector>

class BRepBuilderAPI_MakeShape;
class BRepTools_History;

namespace fabricad::core {
class Document;
class RecomputeContext;
}  // namespace fabricad::core

namespace fabricad::part {

using NameMap = std::map<std::string, TopoDS_Shape>;

/// Deterministic sort key for sub-shapes (centroid, then size). Never TopExp order alone.
bool shapeLess(const TopoDS_Shape& a, const TopoDS_Shape& b);
std::vector<TopoDS_Shape> sortedShapes(const TopTools_ListOfShape& list);

/// Query interface over a maker history (BRepBuilderAPI_MakeShape or BRepTools_History).
struct History {
  std::function<TopTools_ListOfShape(const TopoDS_Shape&)> modified;
  std::function<TopTools_ListOfShape(const TopoDS_Shape&)> generated;
  std::function<bool(const TopoDS_Shape&)> isDeleted;

  static History of(BRepBuilderAPI_MakeShape& maker);
  static History of(const Handle(BRepTools_History)& history);
  /// Identity (nothing changed).
  static History none();
};

/// Carry names through an operation: deleted shapes drop out, modified 1->1 keep the name,
/// 1->N become "name#k" (k by deterministic order), unmodified shapes still present in
/// `result` are kept. Only faces/edges/vertices actually present in `result` survive.
NameMap mapNames(const NameMap& names, const History& history, const TopoDS_Shape& result);

/// Add "{FaceA|FaceB}" edge names for every edge of `result` whose two adjacent faces are
/// named (existing edge names win). Duplicates get "#k".
void addEdgeNames(NameMap& names, const TopoDS_Shape& result);

/// Remove names whose shape is not a sub-shape of `shape` (after merges/splits).
void pruneNames(NameMap& names, const TopoDS_Shape& shape);

/// Name of `sub` in `names` (IsSame), empty if unnamed.
std::string nameOf(const NameMap& names, const TopoDS_Shape& sub);

/// Cumulative names for a body as of the state before `ctx`'s feature: the namedShapes()
/// of the latest upstream feature that touched the body, pruned to the body's shape.
NameMap upstreamNames(const core::RecomputeContext& ctx, core::ObjectId bodyId);
/// Same against the document's current state (for dialogs naming picked sub-shapes).
NameMap currentNames(const core::Document& doc, core::ObjectId bodyId);

/// Resolve a reference: by history name first (updates index + signature, no warning), then
/// core's index/signature matching (ctx.resolve). Fills ref.name when a name is found.
TopoDS_Shape resolveRef(core::RecomputeContext& ctx, core::ShapeRef& ref);

/// Same for a single-Ref property (copies the ref, resolves, stores it back).
TopoDS_Shape resolveRefProperty(core::RecomputeContext& ctx, core::Property& p);

/// Annotate a picked B-rep ref with its history name (dialog side).
void annotateRef(const core::Document& doc, core::ShapeRef& ref);

}  // namespace fabricad::part
