// FabriCAD core: ready-made undoable document commands.
// Modules should use these (or compose them in a MacroCommand) instead of mutating the
// Document directly from UI code, so undo/redo stays consistent. Each command calls
// Document::recompute() where geometry changes.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/UndoStack.h"

#include <QJsonObject>

#include <functional>
#include <memory>
#include <string>

namespace fabricad::core {

/// Append (or insert) a feature; undo removes it again.
class AddFeatureCommand : public UndoCommand {
 public:
  AddFeatureCommand(Document& doc, std::unique_ptr<Feature> feature, int index = -1);
  void redo() override;
  void undo() override;
  ObjectId featureId() const { return id_; }

 private:
  Document& doc_;
  std::unique_ptr<Feature> feature_;
  int index_;
  ObjectId id_ = kInvalidId;
  int rollbackBefore_ = 0;
  TimelineGroupState groupsBefore_, groupsAfter_;  ///< exact group membership for undo/redo
  bool done_ = false;
};

/// Remove a feature; undo re-inserts it at the same index.
class RemoveFeatureCommand : public UndoCommand {
 public:
  RemoveFeatureCommand(Document& doc, ObjectId id);
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  ObjectId id_;
  std::unique_ptr<Feature> feature_;
  std::vector<Body> bodies_;  ///< records of the bodies the feature created (restored on undo)
  int index_ = -1;
  int rollbackBefore_ = 0;
  TimelineGroupState groupsBefore_, groupsAfter_;
  bool done_ = false;
};

/// Change a feature's inputs. Captures JSON before/after; `edit` is applied once.
class ModifyFeatureCommand : public UndoCommand {
 public:
  ModifyFeatureCommand(Document& doc, ObjectId id, std::function<void(Feature&)> edit, std::string text = {});
  /// Build from explicit snapshots (e.g. dialog kept the original JSON).
  ModifyFeatureCommand(Document& doc, ObjectId id, QJsonObject before, QJsonObject after, std::string text = {});
  void redo() override;
  void undo() override;

 private:
  void apply(const QJsonObject& json);
  Document& doc_;
  ObjectId id_;
  std::function<void(Feature&)> edit_;
  QJsonObject before_, after_;
  bool first_ = true;
};

/// Reorder a feature (Document::moveFeatureTo rules). `targetGroup` as in moveFeatureTo:
/// std::nullopt = automatic group membership.
class MoveFeatureCommand : public UndoCommand {
 public:
  MoveFeatureCommand(Document& doc, ObjectId id, int newIndex, std::optional<ObjectId> targetGroup = std::nullopt);
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  ObjectId id_;
  int oldIndex_ = -1, newIndex_;
  std::optional<ObjectId> target_;
  TimelineGroupState groupsBefore_, groupsAfter_;
  bool done_ = false;
};

class SetRollbackCommand : public UndoCommand {
 public:
  SetRollbackCommand(Document& doc, int index);
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  int oldIndex_, newIndex_;
};

class SetParameterCommand : public UndoCommand {
 public:
  /// Empty expression removes the parameter.
  SetParameterCommand(Document& doc, std::string name, std::string expression, std::string comment = {});
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  std::string name_, expr_, comment_;
  bool existed_ = false;
  bool failed_ = false;  ///< redo could not set it (invalid name)
  std::string oldExpr_, oldComment_;
};

/// Change the document's units (UNITS command, Preferences > Document). Bare numbers in
/// parameter expressions are read in the display unit, so plain-number parameters that are
/// used as lengths (angles) are rewritten with the old unit appended ("10" -> "10 mm") to
/// keep the geometry unchanged. Parameters used by feature properties are found here;
/// callers add names used elsewhere (sketch dimensions) through `lengthParams`/`angleParams`.
class SetUnitsCommand : public UndoCommand {
 public:
  SetUnitsCommand(Document& doc, UnitSystem units, std::vector<std::string> lengthParams = {},
                  std::vector<std::string> angleParams = {});
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  UnitSystem old_, new_;
  std::vector<std::pair<std::string, std::string>> oldExprs_, newExprs_;  ///< rewritten parameters
};

/// Generic fallback: snapshot the whole document before/after an arbitrary edit.
/// Expensive on big documents; fine for rare structural edits (component tree...).
class DocumentSnapshotCommand : public UndoCommand {
 public:
  DocumentSnapshotCommand(Document& doc, std::string text, std::function<void()> edit);
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  std::function<void()> edit_;
  QJsonObject before_, after_;
  bool first_ = true;
};

/// Convenience: push a command that edits body attributes (name/visible/color) with undo.
std::unique_ptr<UndoCommand> makeBodyEditCommand(Document& doc, ObjectId bodyId, std::function<void(Body&)> edit,
                                                 std::string text = "Edit body");
/// Convenience: edit a component (placement, name, grounded...) with undo.
std::unique_ptr<UndoCommand> makeComponentEditCommand(Document& doc, ObjectId componentId,
                                                      std::function<void(Component&)> edit,
                                                      std::string text = "Edit component");

}  // namespace fabricad::core
