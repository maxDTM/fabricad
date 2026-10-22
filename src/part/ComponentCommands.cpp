// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/ComponentCommands.h"

namespace fabricad::part {

void AddComponentCommand::redo() {
  if (!doc_.component(c_.parent)) c_.parent = core::kRootComponentId;
  c_.children.clear();
  c_.id = doc_.addComponent(c_).id;
  doc_.notifyComponentChanged(c_.id);
}

void AddComponentCommand::undo() { doc_.removeComponent(c_.id); }

void RemoveComponentCommand::redo() {
  if (const core::Component* c = doc_.component(id_)) {
    saved_ = *c;
    saved_.children.clear();
    doc_.removeComponent(id_);
  }
}

void RemoveComponentCommand::undo() {
  if (saved_.id == core::kInvalidId || doc_.component(saved_.id)) return;
  if (!doc_.component(saved_.parent)) saved_.parent = core::kRootComponentId;
  doc_.addComponent(saved_);
  doc_.notifyComponentChanged(saved_.id);
}

void runOrPush(core::UndoStack* undo, std::unique_ptr<core::UndoCommand> cmd) {
  if (undo) undo->push(std::move(cmd));
  else cmd->redo();
}

}  // namespace fabricad::part
