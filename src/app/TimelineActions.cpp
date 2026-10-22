// FabriCAD app: shared timeline commands and context menu.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/TimelineActions.h"

#include "app/AppActions.h"
#include "app/Icons.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GroupCommands.h"
#include "core/Json.h"
#include "core/UndoStack.h"

#include <QMenu>

#include <algorithm>
#include <set>

namespace fabricad::app {

using core::ObjectId;

TimelineActions::TimelineActions(core::Document& doc, View view, QObject* parent)
    : QObject(parent), doc_(doc), view_(view) {}

void TimelineActions::setRollback(int index) {
  index = std::clamp(index, 0, doc_.featureCount());
  if (index != doc_.rollbackIndex()) doc_.undoStack().push(std::make_unique<core::SetRollbackCommand>(doc_, index));
}

std::vector<ObjectId> TimelineActions::featuresOf(const std::vector<ObjectId>& items) const {
  std::set<ObjectId> ids;
  for (ObjectId id : items) {
    if (doc_.group(id))
      for (ObjectId f : doc_.groupFeatures(id)) ids.insert(f);
    else if (doc_.feature(id))
      ids.insert(id);
  }
  std::vector<ObjectId> out(ids.begin(), ids.end());
  std::sort(out.begin(), out.end(), [this](ObjectId a, ObjectId b) { return doc_.indexOf(a) < doc_.indexOf(b); });
  return out;
}

void TimelineActions::setSuppressed(const std::vector<ObjectId>& items, bool suppressed) {
  if (auto c = core::makeSuppressCommand(doc_, items, suppressed)) doc_.undoStack().push(std::move(c));
}

void TimelineActions::toggleSuppress(const std::vector<ObjectId>& items) {
  const std::vector<ObjectId> fs = featuresOf(items);
  if (fs.empty()) return;
  const bool allSuppressed = std::all_of(fs.begin(), fs.end(), [this](ObjectId f) {
    const core::Feature* feat = doc_.feature(f);
    return feat && feat->isSuppressed();
  });
  setSuppressed(items, !allSuppressed);
}

bool TimelineActions::group(const std::vector<ObjectId>& items) {
  std::string why;
  if (auto c = core::makeCreateGroupCommand(doc_, items, {}, &why)) {
    doc_.undoStack().push(std::move(c));
    return true;
  }
  emit messageRequested("Cannot group: " + core::qs(why), 1);
  return false;
}

void TimelineActions::ungroup(ObjectId groupId) {
  if (auto c = core::makeUngroupCommand(doc_, groupId)) doc_.undoStack().push(std::move(c));
}

void TimelineActions::setCollapsed(ObjectId groupId, bool collapsed) {
  if (auto c = core::makeSetGroupCollapsedCommand(doc_, groupId, collapsed)) doc_.undoStack().push(std::move(c));
}

bool TimelineActions::rename(ObjectId id, const QString& name) {
  const std::string n = name.trimmed().toStdString();
  if (n.empty()) return false;
  std::unique_ptr<core::UndoCommand> c;
  if (doc_.group(id)) c = core::makeRenameGroupCommand(doc_, id, n);
  else c = makeRenameFeature(doc_, id, n);
  if (!c) return false;
  doc_.undoStack().push(std::move(c));
  return true;
}

void TimelineActions::populate(QMenu& menu, const std::vector<ObjectId>& items) {
  if (items.empty()) {
    menu.addAction(resourceIcon("timeline-last"), "Move Rollback to End", this, [this] { setRollback(doc_.featureCount()); });
    if (view_ == View::FeatureList && doc_.groupCount() > 0) {
      menu.addAction(resourceIcon("app-expand-group"), "Expand All Groups", this, [this] {
        for (ObjectId g : doc_.groupIds())
          if (doc_.group(g)->collapsed) setCollapsed(g, false);
      });
      menu.addAction(resourceIcon("app-collapse-group"), "Collapse All Groups", this, [this] {
        for (ObjectId g : doc_.groupIds())
          if (!doc_.group(g)->collapsed) setCollapsed(g, true);
      });
    }
    return;
  }
  const ObjectId clicked = items.front();
  const std::vector<ObjectId> fs = featuresOf(items);
  const bool allSuppressed = !fs.empty() && std::all_of(fs.begin(), fs.end(), [this](ObjectId f) {
    return doc_.feature(f) && doc_.feature(f)->isSuppressed();
  });

  if (const core::TimelineGroup* g = doc_.group(clicked)) {
    const auto range = doc_.groupRange(clicked);
    menu.addAction(resourceIcon(g->collapsed ? "app-expand-group" : "app-collapse-group"),
                   g->collapsed ? "Expand Group" : "Collapse Group", this,
                   [this, clicked, c = !g->collapsed] { setCollapsed(clicked, c); });
    const core::GroupSuppression st = doc_.groupSuppression(clicked);
    menu.addAction(st == core::GroupSuppression::None ? "Suppress Group" : "Unsuppress Group", this,
                   [this, clicked, s = st == core::GroupSuppression::None] { setSuppressed({clicked}, s); });
    menu.addAction(resourceIcon("app-rollback"), "Roll History Marker to Group End", this,
                   [this, end = range.second + 1] { setRollback(end); });
    menu.addAction("Rename Group\tF2", this, [this, clicked] { emit renameRequested(clicked); });
    if (items.size() > 1)
      menu.addAction(resourceIcon("app-group"), "Group Selection", this, [this, items] { group(items); });
    menu.addAction(resourceIcon("app-ungroup"), "Ungroup", this, [this, clicked] { ungroup(clicked); });
    menu.addSeparator();
    menu.addAction(resourceIcon("delete"), "Delete Group Features", this, [this, fs] { emit deleteRequested(fs); });
    return;
  }

  const core::Feature* f = doc_.feature(clicked);
  if (!f) return;
  menu.addAction(resourceIcon("feature"), f->type() == "Sketch" ? "Edit Sketch" : "Edit Feature", this,
                 [this, clicked] { emit editRequested(clicked); });
  menu.addAction(resourceIcon("app-rollback"), "Roll History Marker Here", this,
                 [this, idx = doc_.indexOf(clicked) + 1] { setRollback(idx); });
  if (items.size() > 1)
    menu.addAction(allSuppressed ? "Unsuppress Features\tSpace" : "Suppress Features\tSpace", this,
                   [this, items, s = !allSuppressed] { setSuppressed(items, s); });
  else
    menu.addAction(f->isSuppressed() ? "Unsuppress Feature" : "Suppress Feature", this,
                   [this, clicked, s = !f->isSuppressed()] { setSuppressed({clicked}, s); });
  menu.addAction("Rename\tF2", this, [this, clicked] { emit renameRequested(clicked); });
  menu.addAction(resourceIcon("app-group"), items.size() > 1 ? "Group Selection" : "Create Group", this,
                 [this, items] { group(items); });
  if (!f->browserFolder().empty())
    menu.addAction("Find in Browser", this, [this, clicked] { emit findInBrowserRequested(clicked); });
  menu.addSeparator();
  menu.addAction(resourceIcon("delete"), "Delete\tDel", this, [this, fs] { emit deleteRequested(fs); });
}

}  // namespace fabricad::app
