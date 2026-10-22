// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Commands.h"

#include <format>

namespace fabricad::core {

// --- AddFeatureCommand -------------------------------------------------------

AddFeatureCommand::AddFeatureCommand(Document& doc, std::unique_ptr<Feature> feature, int index)
    : UndoCommand(feature ? "Add " + feature->type() : "Add feature"), doc_(doc), feature_(std::move(feature)), index_(index) {}

void AddFeatureCommand::redo() {
  if (!feature_) return;
  rollbackBefore_ = doc_.rollbackIndex();
  Feature* f = doc_.addFeature(std::move(feature_), index_);
  id_ = f->id();
  index_ = doc_.indexOf(id_);
  setText("Add " + f->name());
  doc_.recompute();
}

void AddFeatureCommand::undo() {
  feature_ = doc_.takeFeature(id_);
  doc_.setRollbackIndex(rollbackBefore_);
  doc_.recompute();
}

// --- RemoveFeatureCommand ----------------------------------------------------

RemoveFeatureCommand::RemoveFeatureCommand(Document& doc, ObjectId id) : UndoCommand("Delete feature"), doc_(doc), id_(id) {
  if (const Feature* f = doc.feature(id)) setText("Delete " + f->name());
}

void RemoveFeatureCommand::redo() {
  index_ = doc_.indexOf(id_);
  rollbackBefore_ = doc_.rollbackIndex();
  feature_ = doc_.takeFeature(id_);
  doc_.recompute();
}

void RemoveFeatureCommand::undo() {
  if (!feature_) return;
  doc_.addFeature(std::move(feature_), index_);
  doc_.setRollbackIndex(rollbackBefore_);
  doc_.recompute();
}

// --- ModifyFeatureCommand ----------------------------------------------------

ModifyFeatureCommand::ModifyFeatureCommand(Document& doc, ObjectId id, std::function<void(Feature&)> edit, std::string text)
    : UndoCommand(std::move(text)), doc_(doc), id_(id), edit_(std::move(edit)) {
  if (this->text().empty())
    if (const Feature* f = doc.feature(id)) setText("Edit " + f->name());
}

ModifyFeatureCommand::ModifyFeatureCommand(Document& doc, ObjectId id, QJsonObject before, QJsonObject after, std::string text)
    : UndoCommand(std::move(text)), doc_(doc), id_(id), before_(std::move(before)), after_(std::move(after)), first_(false) {
  if (this->text().empty())
    if (const Feature* f = doc.feature(id)) setText("Edit " + f->name());
}

void ModifyFeatureCommand::apply(const QJsonObject& json) {
  Feature* f = doc_.feature(id_);
  if (!f) return;
  f->fromJson(json);
  doc_.notifyFeatureChanged(id_);
  doc_.recompute();
}

void ModifyFeatureCommand::redo() {
  if (first_) {
    first_ = false;
    Feature* f = doc_.feature(id_);
    if (!f) return;
    before_ = f->toJson();
    if (edit_) edit_(*f);
    after_ = f->toJson();
    doc_.notifyFeatureChanged(id_);
    doc_.recompute();
    return;
  }
  apply(after_);
}

void ModifyFeatureCommand::undo() { apply(before_); }

// --- MoveFeatureCommand ------------------------------------------------------

MoveFeatureCommand::MoveFeatureCommand(Document& doc, ObjectId id, int newIndex)
    : UndoCommand("Reorder feature"), doc_(doc), id_(id), newIndex_(newIndex) {}

void MoveFeatureCommand::redo() {
  oldIndex_ = doc_.indexOf(id_);
  if (doc_.moveFeature(id_, newIndex_)) doc_.recompute();
}

void MoveFeatureCommand::undo() {
  if (oldIndex_ >= 0 && doc_.moveFeature(id_, oldIndex_)) doc_.recompute();
}

// --- SetRollbackCommand ------------------------------------------------------

SetRollbackCommand::SetRollbackCommand(Document& doc, int index)
    : UndoCommand("Move rollback marker"), doc_(doc), oldIndex_(doc.rollbackIndex()), newIndex_(index) {}

void SetRollbackCommand::redo() { doc_.setRollbackIndex(newIndex_); }
void SetRollbackCommand::undo() { doc_.setRollbackIndex(oldIndex_); }

// --- SetParameterCommand -----------------------------------------------------

SetParameterCommand::SetParameterCommand(Document& doc, std::string name, std::string expression, std::string comment)
    : UndoCommand("Change parameter " + name), doc_(doc), name_(std::move(name)), expr_(std::move(expression)), comment_(std::move(comment)) {
  if (const auto* p = doc.parameter(name_)) {
    existed_ = true;
    oldExpr_ = p->expression;
    oldComment_ = p->comment;
  }
}

void SetParameterCommand::redo() {
  if (expr_.empty()) doc_.removeParameter(name_);
  else doc_.setParameter(name_, expr_, comment_);
  doc_.recompute();
}

void SetParameterCommand::undo() {
  if (existed_) doc_.setParameter(name_, oldExpr_, oldComment_);
  else doc_.removeParameter(name_);
  doc_.recompute();
}

// --- DocumentSnapshotCommand -------------------------------------------------

DocumentSnapshotCommand::DocumentSnapshotCommand(Document& doc, std::string text, std::function<void()> edit)
    : UndoCommand(std::move(text)), doc_(doc), edit_(std::move(edit)) {}

void DocumentSnapshotCommand::redo() {
  if (first_) {
    first_ = false;
    before_ = doc_.toJson();
    if (edit_) edit_();
    after_ = doc_.toJson();
    return;
  }
  doc_.fromJson(after_);
  doc_.setModified(true);  // fromJson keeps the flag; a redo is an edit (DOC-05)
}

void DocumentSnapshotCommand::undo() {
  doc_.fromJson(before_);
  doc_.setModified(true);  // an undo is an edit too: the '*' must not disappear (DOC-05)
}

// --- helpers -----------------------------------------------------------------

std::unique_ptr<UndoCommand> makeBodyEditCommand(Document& doc, ObjectId bodyId, std::function<void(Body&)> edit, std::string text) {
  Body* b = doc.body(bodyId);
  if (!b) return nullptr;
  Body before = *b;
  Body after = *b;
  edit(after);
  return std::make_unique<LambdaCommand>(
      std::move(text),
      [&doc, bodyId, after] {
        if (Body* x = doc.body(bodyId)) { *x = after; doc.notifyBodyChanged(bodyId); }
      },
      [&doc, bodyId, before] {
        if (Body* x = doc.body(bodyId)) { *x = before; doc.notifyBodyChanged(bodyId); }
      });
}

std::unique_ptr<UndoCommand> makeComponentEditCommand(Document& doc, ObjectId componentId,
                                                      std::function<void(Component&)> edit, std::string text) {
  Component* c = doc.component(componentId);
  if (!c) return nullptr;
  Component before = *c;
  Component after = *c;
  edit(after);
  return std::make_unique<LambdaCommand>(
      std::move(text),
      [&doc, componentId, after] {
        if (Component* x = doc.component(componentId)) { *x = after; doc.notifyComponentChanged(componentId); }
      },
      [&doc, componentId, before] {
        if (Component* x = doc.component(componentId)) { *x = before; doc.notifyComponentChanged(componentId); }
      });
}

}  // namespace fabricad::core
