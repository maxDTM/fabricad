// FabriCAD app: document-level actions behind shell commands (Delete, Show/Hide, Edit,
// Rename, Suppress). Widget-free and unit-tested; MainWindow, the browser, the timeline and
// the app tools all go through these so behaviour (and undo) is identical everywhere.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "core/UndoStack.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::core {
class Document;
class Feature;
class ToolRegistry;
}  // namespace fabricad::core

namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

/// The ShapeRef that stands for a whole feature in selections (Sketch / ConstructionPlane /
/// ConstructionAxis / ConstructionPoint / Joint / Feature).
core::ShapeRef featureRef(const core::Feature& f);
/// Feature id a selection item refers to as a whole (sketch, construction, joint, feature
/// refs); kInvalidId for B-rep / component refs.
core::ObjectId featureOfRef(const core::ShapeRef& r);

/// What Delete would remove for a selection.
struct DeletePlan {
  std::vector<core::ObjectId> features;    ///< timeline features, document order
  std::vector<core::ObjectId> components;  ///< child components
  std::vector<std::string> bodyNotes;      ///< "Body1 (created by Extrude1)" for the confirmation text
  bool isEmpty() const { return features.empty() && components.empty(); }
  std::string summary(const core::Document& doc) const;
};
/// Bodies map to the feature that created them (Fusion deletes the creating feature when
/// the body has no other history); sub-shapes are ignored.
DeletePlan planDelete(const core::Document& doc, const std::vector<core::ShapeRef>& items);
/// Undoable command executing the plan (nullptr if empty).
std::unique_ptr<core::UndoCommand> makeDeleteCommand(core::Document& doc, const DeletePlan& plan);

/// Toggle visibility of the selected bodies/components (undoable command, may be null) and
/// sketch/construction/joint features (viewer display state, applied immediately).
std::unique_ptr<core::UndoCommand> toggleVisibility(core::Document& doc, viewer::ViewerWidget* viewer,
                                                    const std::vector<core::ShapeRef>& items);
/// Isolate (Fusion): hide every body and component except the given bodies/components (and the
/// components that contain them). Undoable; Show All undoes the effect. Null if nothing to do.
std::unique_ptr<core::UndoCommand> isolate(core::Document& doc, const std::vector<core::ShapeRef>& items);
/// Show every body, component and feature output. Returns the command for bodies/components.
std::unique_ptr<core::UndoCommand> showAll(core::Document& doc, viewer::ViewerWidget* viewer);

/// Rename helpers (undoable). Return null when the name is empty or unchanged.
std::unique_ptr<core::UndoCommand> makeRenameFeature(core::Document& doc, core::ObjectId id, const std::string& name);
std::unique_ptr<core::UndoCommand> makeRenameBody(core::Document& doc, core::ObjectId id, const std::string& name);
std::unique_ptr<core::UndoCommand> makeRenameComponent(core::Document& doc, core::ObjectId id, const std::string& name);
std::unique_ptr<core::UndoCommand> makeSuppress(core::Document& doc, core::ObjectId id, bool suppressed);

// --- physical material / appearance / component properties ------------------------------------
/// Assign a library material (name, density, and its colour when `applyColor`) to a body. An
/// unknown name is stored as free text with the given density (0 = unknown).
std::unique_ptr<core::UndoCommand> makeSetBodyMaterial(core::Document& doc, core::ObjectId bodyId, const std::string& material,
                                                       double density, bool applyColor);
std::unique_ptr<core::UndoCommand> makeSetBodyColor(core::Document& doc, core::ObjectId bodyId, std::array<float, 4> rgba);
/// Part number / description / material of a component (shown in parts lists and the BOM).
std::unique_ptr<core::UndoCommand> makeSetComponentProperties(core::Document& doc, core::ObjectId componentId,
                                                              const std::string& partNumber, const std::string& description,
                                                              const std::string& material);

/// How to edit a feature: a tool id plus arguments.
struct EditCommand {
  std::string toolId;
  std::vector<std::string> args;
  bool valid() const { return !toolId.empty(); }
};
/// Sketch -> "sketch.edit {id}"; part features -> part::toolIdForFeatureType {"edit", id};
/// otherwise the generic property editor "app.edit-properties {id}" (if registered).
EditCommand editCommandFor(const core::ToolRegistry& reg, const core::Feature& f);

}  // namespace fabricad::app
