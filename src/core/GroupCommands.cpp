// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/GroupCommands.h"

#include "core/Feature.h"

#include <algorithm>
#include <set>

namespace fabricad::core {

// --- GroupEditCommand ----------------------------------------------------------------------------

GroupEditCommand::GroupEditCommand(Document& doc, std::string text, std::function<void(Document&)> edit)
    : UndoCommand(std::move(text)), doc_(doc), edit_(std::move(edit)) {}

void GroupEditCommand::redo() {
  if (done_) {
    doc_.setGroupState(after_);
    return;
  }
  before_ = doc_.groupState();
  if (edit_) edit_(doc_);
  after_ = doc_.groupState();
  done_ = true;
}

void GroupEditCommand::undo() { doc_.setGroupState(before_); }

// --- create / ungroup / rename / collapse ----------------------------------------------------------

CreateGroupCommand::CreateGroupCommand(Document& doc, std::vector<ObjectId> items, std::string name)
    : GroupEditCommand(doc, name.empty() ? "Create group" : "Create group " + name,
                       [this, items = std::move(items), name](Document& d) { id_ = d.createGroup(items, name); }) {}

std::unique_ptr<CreateGroupCommand> makeCreateGroupCommand(Document& doc, std::vector<ObjectId> items, std::string name,
                                                           std::string* why) {
  if (!doc.canCreateGroup(items, why)) return nullptr;
  return std::make_unique<CreateGroupCommand>(doc, std::move(items), std::move(name));
}

std::unique_ptr<UndoCommand> makeUngroupCommand(Document& doc, ObjectId groupId) {
  const TimelineGroup* g = doc.group(groupId);
  if (!g) return nullptr;
  return std::make_unique<GroupEditCommand>(doc, "Ungroup " + g->name, [groupId](Document& d) { d.ungroup(groupId); });
}

std::unique_ptr<UndoCommand> makeRenameGroupCommand(Document& doc, ObjectId groupId, std::string name) {
  const TimelineGroup* g = doc.group(groupId);
  if (!g || name.empty() || g->name == name) return nullptr;
  return std::make_unique<GroupEditCommand>(doc, "Rename group " + g->name,
                                            [groupId, name](Document& d) { d.renameGroup(groupId, name); });
}

std::unique_ptr<UndoCommand> makeSetGroupCollapsedCommand(Document& doc, ObjectId groupId, bool collapsed) {
  const TimelineGroup* g = doc.group(groupId);
  if (!g || g->collapsed == collapsed) return nullptr;
  return std::make_unique<GroupEditCommand>(doc, (collapsed ? "Collapse group " : "Expand group ") + g->name,
                                            [groupId, collapsed](Document& d) { d.setGroupCollapsed(groupId, collapsed); });
}

// --- MoveGroupCommand ----------------------------------------------------------------------------

MoveGroupCommand::MoveGroupCommand(Document& doc, ObjectId groupId, int newIndex)
    : UndoCommand("Reorder group"), doc_(doc), id_(groupId), newIndex_(newIndex) {
  if (const TimelineGroup* g = doc.group(groupId)) setText("Reorder group " + g->name);
}

void MoveGroupCommand::redo() {
  oldIndex_ = doc_.groupRange(id_).first;
  if (!done_) before_ = doc_.groupState();
  const bool moved = doc_.moveGroup(id_, newIndex_);
  if (done_) doc_.setGroupState(after_);
  else after_ = doc_.groupState();
  done_ = true;
  if (moved) doc_.recompute();
}

void MoveGroupCommand::undo() {
  const bool moved = oldIndex_ >= 0 && doc_.moveGroup(id_, oldIndex_);
  doc_.setGroupState(before_);
  if (moved) doc_.recompute();
}

// --- suppress ------------------------------------------------------------------------------------

SuppressFeaturesCommand::SuppressFeaturesCommand(Document& doc, const std::vector<ObjectId>& features, bool suppressed,
                                                 std::string text)
    : MacroCommand(text.empty() ? (suppressed ? "Suppress features" : "Unsuppress features") : std::move(text)), doc_(doc) {
  std::set<ObjectId> seen;
  for (ObjectId id : features) {
    const Feature* f = doc.feature(id);
    if (!f || f->isSuppressed() == suppressed || !seen.insert(id).second) continue;
    auto set = [&doc, id](bool s) {
      if (Feature* x = doc.feature(id)) {
        x->setSuppressed(s);
        doc.notifyFeatureChanged(id);
      }
    };
    add(std::make_unique<LambdaCommand>((suppressed ? "Suppress " : "Unsuppress ") + f->name(), [set, suppressed] { set(suppressed); },
                                        [set, suppressed] { set(!suppressed); }));
  }
}

void SuppressFeaturesCommand::redo() {
  MacroCommand::redo();
  doc_.recompute();
}

void SuppressFeaturesCommand::undo() {
  MacroCommand::undo();
  doc_.recompute();
}

std::unique_ptr<UndoCommand> makeGroupSuppressCommand(Document& doc, ObjectId groupId, bool suppressed) {
  const TimelineGroup* g = doc.group(groupId);
  if (!g) return nullptr;
  auto cmd = std::make_unique<SuppressFeaturesCommand>(doc, doc.groupFeatures(groupId), suppressed,
                                                       (suppressed ? "Suppress group " : "Unsuppress group ") + g->name);
  if (cmd->empty()) return nullptr;
  return cmd;
}

std::unique_ptr<UndoCommand> makeSuppressCommand(Document& doc, const std::vector<ObjectId>& items, bool suppressed) {
  std::vector<ObjectId> features;
  std::string label;
  for (ObjectId x : items) {
    if (const TimelineGroup* g = doc.group(x)) {
      for (ObjectId f : doc.groupFeatures(x)) features.push_back(f);
      label = g->name;
    } else if (const Feature* f = doc.feature(x)) {
      features.push_back(x);
      label = f->name();
    }
  }
  if (items.size() != 1) label.clear();
  std::string text = suppressed ? "Suppress" : "Unsuppress";
  text += label.empty() ? " features" : " " + label;
  auto cmd = std::make_unique<SuppressFeaturesCommand>(doc, features, suppressed, text);
  if (cmd->empty()) return nullptr;
  return cmd;
}

}  // namespace fabricad::core
