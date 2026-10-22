// FabriCAD core: undoable commands for timeline groups (TimelineGroup.h) and group suppress.
// Factories return nullptr when the edit is refused or would change nothing (`why` says why),
// so callers can push the result unchecked: `if (auto c = make...) doc.undoStack().push(std::move(c));`
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/UndoStack.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::core {

/// Generic group edit: snapshots the group state around `edit` (run once, on the first redo);
/// undo/redo restore the snapshots. Group edits never touch geometry, so nothing recomputes.
class GroupEditCommand : public UndoCommand {
 public:
  GroupEditCommand(Document& doc, std::string text, std::function<void(Document&)> edit);
  void redo() override;
  void undo() override;

 protected:
  Document& doc_;

 private:
  std::function<void(Document&)> edit_;
  TimelineGroupState before_, after_;
  bool done_ = false;
};

/// Group consecutive features and/or groups (the selection). groupId() is valid after the push.
class CreateGroupCommand : public GroupEditCommand {
 public:
  CreateGroupCommand(Document& doc, std::vector<ObjectId> items, std::string name = {});
  ObjectId groupId() const { return id_; }

 private:
  ObjectId id_ = kInvalidId;
};

std::unique_ptr<CreateGroupCommand> makeCreateGroupCommand(Document& doc, std::vector<ObjectId> items,
                                                           std::string name = {}, std::string* why = nullptr);
std::unique_ptr<UndoCommand> makeUngroupCommand(Document& doc, ObjectId groupId);
std::unique_ptr<UndoCommand> makeRenameGroupCommand(Document& doc, ObjectId groupId, std::string name);
std::unique_ptr<UndoCommand> makeSetGroupCollapsedCommand(Document& doc, ObjectId groupId, bool collapsed);

/// Move a whole group (Document::moveGroup); undo restores the order and the group state.
class MoveGroupCommand : public UndoCommand {
 public:
  MoveGroupCommand(Document& doc, ObjectId groupId, int newIndex);
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
  ObjectId id_;
  int oldIndex_ = -1, newIndex_;
  TimelineGroupState before_, after_;
  bool done_ = false;
};

/// Suppress or unsuppress several features as one undo step with one recompute: a MacroCommand
/// with one child per feature whose state changes.
class SuppressFeaturesCommand : public MacroCommand {
 public:
  SuppressFeaturesCommand(Document& doc, const std::vector<ObjectId>& features, bool suppressed, std::string text = {});
  void redo() override;
  void undo() override;

 private:
  Document& doc_;
};

/// Suppress / unsuppress every feature of a group (nested groups included). nullptr for an
/// unknown group or when every member is already in that state.
std::unique_ptr<UndoCommand> makeGroupSuppressCommand(Document& doc, ObjectId groupId, bool suppressed);
/// The same for any mix of features and groups (groups expand to their features).
std::unique_ptr<UndoCommand> makeSuppressCommand(Document& doc, const std::vector<ObjectId>& items, bool suppressed);

}  // namespace fabricad::core
