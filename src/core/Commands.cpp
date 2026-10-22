// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Commands.h"

#include "core/Expression.h"
#include "core/Feature.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

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
  // takeFeature drops the feature's body records; keep them so an undo brings the bodies back
  // with their names, colours and materials instead of fresh ones (DOC-07)
  bodies_.clear();
  if (const Feature* f = doc_.feature(id_))
    for (ObjectId b : f->createdBodies())
      if (const Body* body = doc_.body(b)) bodies_.push_back(*body);
  feature_ = doc_.takeFeature(id_);
  doc_.recompute();
}

void RemoveFeatureCommand::undo() {
  if (!feature_) return;
  for (const Body& b : bodies_)
    if (!doc_.body(b.id)) doc_.addBody(b);
  bodies_.clear();
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
  else if (!doc_.setParameter(name_, expr_, comment_)) {
    failed_ = true;  // invalid name: nothing changed, so undo must not remove anything either
    return;
  }
  failed_ = false;
  doc_.recompute();
}

void SetParameterCommand::undo() {
  if (failed_) return;
  if (existed_) doc_.setParameter(name_, oldExpr_, oldComment_);
  else doc_.removeParameter(name_);
  doc_.recompute();
}

// --- SetUnitsCommand ---------------------------------------------------------

SetUnitsCommand::SetUnitsCommand(Document& doc, UnitSystem units, std::vector<std::string> lengthParams,
                                 std::vector<std::string> angleParams)
    : UndoCommand("Change units"), doc_(doc), old_(doc.units()), new_(units) {
  std::set<std::string> lengths(lengthParams.begin(), lengthParams.end());
  std::set<std::string> angles(angleParams.begin(), angleParams.end());
  for (const auto& f : doc.features())
    for (const Property& p : f->properties()) {
      if (!p.hasExpression()) continue;
      if (p.type() != PropertyType::Length && p.type() != PropertyType::Angle) continue;
      for (const std::string& n : referencedParameters(p.expression()))
        (p.type() == PropertyType::Length ? lengths : angles).insert(n);
    }
  // A parameter whose expression is a plain scalar (no references, e.g. "10" or "2*5") and that
  // is used as a length/angle changes meaning with the display unit: pin it to the old unit.
  for (const Document::Parameter& p : doc.parameters()) {
    if (!p.ok || !p.value.dim.isScalar() || !referencedParameters(p.expression).empty()) continue;
    const bool len = lengths.count(p.name) > 0, ang = angles.count(p.name) > 0;
    if (len == ang) continue;  // unused, or ambiguous: leave it
    if (len && old_.length == new_.length) continue;
    if (ang && old_.angle == new_.angle) continue;
    const char* unit = len ? lengthUnitSymbol(old_.length) : angleUnitSymbol(old_.angle);
    std::string e = p.expression;
    const bool simple = std::all_of(e.begin(), e.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ' '; });
    oldExprs_.emplace_back(p.name, p.expression);
    newExprs_.emplace_back(p.name, simple ? e + " " + unit : "(" + e + ") " + unit);
  }
}

void SetUnitsCommand::redo() {
  for (const auto& [n, e] : newExprs_)
    if (const auto* p = doc_.parameter(n)) doc_.setParameter(n, e, p->comment);
  doc_.setUnits(new_);
  doc_.recompute();
}

void SetUnitsCommand::undo() {
  for (const auto& [n, e] : oldExprs_)
    if (const auto* p = doc_.parameter(n)) doc_.setParameter(n, e, p->comment);
  doc_.setUnits(old_);
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
