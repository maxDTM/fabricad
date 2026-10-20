// FabriCAD assembly: component helpers (occurrences, world shapes, placements, undoable edits).
// Body shapes live in component-local coordinates; the world shape of a component is its
// bodies (or its definition's bodies for an occurrence) transformed by Document::worldPlacement.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/ShapeRef.h"
#include "core/Types.h"
#include "core/UndoStack.h"

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::assembly {

/// The component whose geometry `comp` shows: itself, or the end of its `definition` chain.
core::ObjectId definitionOf(const core::Document& doc, core::ObjectId comp);
/// Live bodies displayed for a component (own bodies, or the definition's for an occurrence).
std::vector<core::ObjectId> displayedBodies(const core::Document& doc, core::ObjectId comp,
                                            const core::BodyStateMap* state = nullptr);
/// Descendant component ids, depth first (not including `comp`).
std::vector<core::ObjectId> descendants(const core::Document& doc, core::ObjectId comp);
bool isAncestorOf(const core::Document& doc, core::ObjectId ancestor, core::ObjectId comp);
/// Child-most components (no children) under `root`, including `root` if it has none.
std::vector<core::ObjectId> leafComponents(const core::Document& doc, core::ObjectId root = core::kRootComponentId);

/// A body as it appears in the assembly: body id + the component instance showing it.
struct BodyInstance {
  core::ObjectId body = core::kInvalidId;
  core::ObjectId component = core::kInvalidId;  ///< the instance (occurrence or definition)
  TopoDS_Shape worldShape;                      ///< transformed by the instance's world placement
};
/// All body instances of the given components (empty list = whole document), optionally with
/// their sub-trees. Null shapes are skipped.
std::vector<BodyInstance> bodyInstances(const core::Document& doc, const std::vector<core::ObjectId>& components,
                                        bool includeChildren = true, const core::BodyStateMap* state = nullptr);

/// Compound of a component's bodies in local coordinates (own or definition's). Null if none.
TopoDS_Shape componentLocalShape(const core::Document& doc, core::ObjectId comp, const core::BodyStateMap* state = nullptr);
/// Compound of a component (and optionally its sub-tree) in world coordinates. Null if none.
TopoDS_Shape componentWorldShape(const core::Document& doc, core::ObjectId comp, bool includeChildren = true,
                                 const core::BodyStateMap* state = nullptr);
/// World bounding-box centre of a component sub-tree (nullopt if it has no geometry: then use its origin).
std::optional<gp_Pnt> componentCenter(const core::Document& doc, core::ObjectId comp);
/// Component that owns a picked entity: body refs -> the body's component, Component refs -> itself,
/// origin/construction -> root (or the active component for sketch/construction features).
core::ObjectId componentOfRef(const core::Document& doc, const core::ShapeRef& ref);
/// Fusion-style browser label: "Bracket" for a definition, "Bracket:2" for the second occurrence.
std::string componentLabel(const core::Document& doc, core::ObjectId comp);
/// True when some active joint/rigid-group feature references the component.
bool isJointed(const core::Document& doc, core::ObjectId comp);

/// Write the local placement that gives `world` for the component (no undo, emits componentChanged).
/// Returns false if nothing changed (within tolerance) or the component is unknown.
bool setWorldPlacement(core::Document& doc, core::ObjectId comp, const gp_Trsf& world, double tol = 1e-9);
/// World placements of every component (snapshot for undo).
std::map<core::ObjectId, gp_Trsf> snapshotPlacements(const core::Document& doc);

// --- undoable edits ----------------------------------------------------------------
/// Restore/apply local placements (ids missing from `after` keep `before`). Does not recompute.
std::unique_ptr<core::UndoCommand> makePlacementsCommand(core::Document& doc, std::map<core::ObjectId, gp_Trsf> before,
                                                         std::map<core::ObjectId, gp_Trsf> after,
                                                         std::string text = "Move component");
std::unique_ptr<core::UndoCommand> makeGroundCommand(core::Document& doc, core::ObjectId comp, bool grounded);

struct NewComponentSpec {
  std::string name;                           ///< empty = "ComponentN"
  core::ObjectId parent = core::kInvalidId;   ///< kInvalidId = active component
  std::vector<core::ObjectId> bodies;         ///< bodies to move into the new component
  bool activate = true;
  bool ground = false;
  bool addTimelineMarker = true;              ///< add a "NewComponent" feature
  std::string externalPath;
};
/// Create a component (undoable through the stack when given; direct edit otherwise). Returns its id.
/// Note: core has no insert-with-id, so a component re-created by redo (after an undo) gets a fresh id.
core::ObjectId createComponent(core::Document& doc, core::UndoStack* undo, const NewComponentSpec& spec);
/// Create an occurrence of `definition` under `parent` with the given local placement. Returns its id.
core::ObjectId createOccurrence(core::Document& doc, core::UndoStack* undo, core::ObjectId definition,
                                core::ObjectId parent, const gp_Trsf& placement);
/// Remove a component sub-tree and every feature referencing it (joints, rigid groups, markers, inserts).
/// Undo re-creates the tree (fresh ids) and re-inserts the features with remapped component references.
struct DeleteComponentResult {
  int movedChildren = 0;  ///< child components re-parented to the deleted component's parent
  int movedBodies = 0;    ///< bodies moved to the parent
};
/// Delete a component (undoable): its bodies and child components move to its parent (children keep
/// their world placement), joints and other features referencing it are removed.
bool deleteComponent(core::Document& doc, core::UndoStack* undo, core::ObjectId comp,
                     DeleteComponentResult* result = nullptr);
/// Re-solve the joints after a placement or ground change: recompute from the first active joint
/// feature only (body features do not depend on placements). Returns that timeline index, -1 when
/// there is no joint (nothing to recompute).
int recomputeJoints(core::Document& doc);

}  // namespace fabricad::assembly
