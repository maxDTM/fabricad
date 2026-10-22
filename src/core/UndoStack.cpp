// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/UndoStack.h"

namespace fabricad::core {

void MacroCommand::redo() {
  for (auto& c : children_) c->redo();
}

void MacroCommand::undo() {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) (*it)->undo();
}

UndoStack::UndoStack(QObject* parent) : QObject(parent) {}
UndoStack::~UndoStack() = default;

void UndoStack::push(std::unique_ptr<UndoCommand> cmd) {
  if (!cmd) return;
  {
    const Busy busy(*this);
    cmd->redo();
  }
  if (inMacro()) {
    macros_.back()->add(std::move(cmd));
    return;
  }
  // drop redo branch
  commands_.erase(commands_.begin() + index_, commands_.end());
  if (cleanIndex_ > index_) cleanIndex_ = -1;
  // try merge
  if (!commands_.empty() && cmd->mergeId() >= 0 && commands_.back()->mergeId() == cmd->mergeId() &&
      commands_.back()->mergeWith(*cmd)) {
    // the merged step is no longer the one that was saved (DOC-09)
    if (cleanIndex_ == index_) {
      cleanIndex_ = -1;
      emit cleanChanged(false);
    }
    emit changed();
    return;
  }
  commands_.push_back(std::move(cmd));
  if (limit_ > 0 && static_cast<int>(commands_.size()) > limit_) {
    commands_.erase(commands_.begin());
    if (cleanIndex_ >= 0) --cleanIndex_;
  }
  setIndex(static_cast<int>(commands_.size()));
}

void UndoStack::undo() {
  if (!canUndo()) return;
  {
    const Busy busy(*this);
    commands_[static_cast<size_t>(index_ - 1)]->undo();
  }
  setIndex(index_ - 1);
}

void UndoStack::redo() {
  if (!canRedo()) return;
  {
    const Busy busy(*this);
    commands_[static_cast<size_t>(index_)]->redo();
  }
  setIndex(index_ + 1);
}

std::string UndoStack::undoText() const {
  return canUndo() ? commands_[static_cast<size_t>(index_ - 1)]->text() : std::string();
}

std::string UndoStack::redoText() const {
  return canRedo() ? commands_[static_cast<size_t>(index_)]->text() : std::string();
}

void UndoStack::beginMacro(const std::string& text) { macros_.push_back(std::make_unique<MacroCommand>(text)); }

void UndoStack::endMacro() {
  if (macros_.empty()) return;
  std::unique_ptr<MacroCommand> m = std::move(macros_.back());
  macros_.pop_back();
  if (m->empty()) return;
  if (inMacro()) {
    macros_.back()->add(std::move(m));
    return;
  }
  commands_.erase(commands_.begin() + index_, commands_.end());
  if (cleanIndex_ > index_) cleanIndex_ = -1;
  commands_.push_back(std::move(m));
  setIndex(static_cast<int>(commands_.size()));
}

void UndoStack::clear() {
  commands_.clear();
  macros_.clear();
  bool wasClean = isClean();
  index_ = 0;
  cleanIndex_ = 0;
  emit indexChanged(0);
  emit changed();
  if (!wasClean) emit cleanChanged(true);
}

void UndoStack::setClean() {
  bool was = isClean();
  cleanIndex_ = index_;
  if (!was) emit cleanChanged(true);
}

void UndoStack::resetClean() {
  const bool was = isClean();
  cleanIndex_ = -1;
  if (was) emit cleanChanged(false);
}

void UndoStack::setIndex(int i) {
  bool wasClean = isClean();
  index_ = i;
  emit indexChanged(index_);
  emit changed();
  if (wasClean != isClean()) emit cleanChanged(isClean());
}

}  // namespace fabricad::core
