// FabriCAD part: small undo commands for features that own components (component patterns,
// STEP import with assembly structure). They live here because the part module cannot use the
// assembly module's ComponentOps.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Component.h"
#include "core/Document.h"
#include "core/UndoStack.h"

#include <memory>

namespace fabricad::part {

/// Insert a component with a fixed id (allocated on the first redo); undo removes it.
class AddComponentCommand : public core::UndoCommand {
 public:
  AddComponentCommand(core::Document& doc, core::Component c, std::string text = "Add component")
      : UndoCommand(std::move(text)), doc_(doc), c_(std::move(c)) {}
  core::ObjectId id() const { return c_.id; }
  void redo() override;
  void undo() override;

 private:
  core::Document& doc_;
  core::Component c_;
};

/// Remove a (leaf) component; undo puts it back with the same id.
class RemoveComponentCommand : public core::UndoCommand {
 public:
  RemoveComponentCommand(core::Document& doc, core::ObjectId id, std::string text = "Remove component")
      : UndoCommand(std::move(text)), doc_(doc), id_(id) {}
  void redo() override;
  void undo() override;

 private:
  core::Document& doc_;
  core::ObjectId id_;
  core::Component saved_;
};

/// Push onto the stack (or run directly when `undo` is null).
void runOrPush(core::UndoStack* undo, std::unique_ptr<core::UndoCommand> cmd);

}  // namespace fabricad::part
