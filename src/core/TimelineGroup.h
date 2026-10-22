// FabriCAD core: timeline groups (folders of the feature list, docs/plans/feature-list-timeline.md).
//
// A group is a named, collapsible range of consecutive features. Groups nest. The Document owns
// them (Document::createGroup, groupOf, timelineItems ...) and keeps these invariants under every
// insert, delete, move, undo and load:
//   * every group's features (its own and those of nested groups) are consecutive in the timeline;
//   * groups form a tree (a group has at most one parent group), so two groups are either disjoint
//     or one lies inside the other;
//   * no group is empty: deleting a group's last feature removes the group.
// The rollback marker is a plain timeline index and may sit inside a group.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <map>
#include <string>

namespace fabricad::core {

struct TimelineGroup {
  ObjectId id = kInvalidId;
  std::string name;              ///< as typed by the user
  bool collapsed = false;        ///< folded in the feature list (view state saved with the file)
  ObjectId parent = kInvalidId;  ///< enclosing group, kInvalidId = top level
  bool operator==(const TimelineGroup&) const = default;
};

/// Suppression of a group's features (the tri-state checkbox of a group row).
enum class GroupSuppression { None, Some, All };

/// One row of the flattened timeline: a group header precedes its members.
struct TimelineItem {
  enum class Kind { Feature, Group };
  Kind kind = Kind::Feature;
  ObjectId id = kInvalidId;
  int depth = 0;                 ///< 0 = top level; members of a top-level group have depth 1
  ObjectId parent = kInvalidId;  ///< innermost enclosing group
  bool operator==(const TimelineItem&) const = default;
};

/// Complete group data of a document; commands snapshot it for exact undo.
struct TimelineGroupState {
  std::map<ObjectId, TimelineGroup> groups;
  std::map<ObjectId, ObjectId> featureGroup;  ///< feature -> innermost group (absent = top level)
  bool operator==(const TimelineGroupState&) const = default;
};

}  // namespace fabricad::core
