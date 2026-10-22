// FabriCAD core: timeline groups of the Document (TimelineGroup.h explains the invariants).
//
// Representation: every group knows its parent group, every grouped feature its innermost
// group. Group order is never stored: it follows from the feature order, which stays the one
// source of truth. Placement rule for a feature (or a moved group) landing between the features
// L and R: it must go into the deepest group holding both L and R (CA) or into a group inside CA
// that L or R belongs to (the slot is at that group's edge). Everything else would split a group.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"

#include "core/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

namespace fabricad::core {

namespace {

std::string lowerCopy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool contains(const std::vector<ObjectId>& v, ObjectId x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

const TimelineGroup* Document::group(ObjectId groupId) const {
  auto it = groups_.find(groupId);
  return it == groups_.end() ? nullptr : &it->second;
}

ObjectId Document::groupOf(ObjectId x) const {
  if (auto it = featureGroup_.find(x); it != featureGroup_.end()) return it->second;
  if (auto it = groups_.find(x); it != groups_.end()) return it->second.parent;
  return kInvalidId;
}

std::vector<ObjectId> Document::groupChain(ObjectId x) const {
  std::vector<ObjectId> out;
  ObjectId g = groupOf(x);
  while (g != kInvalidId && out.size() <= groups_.size()) {
    auto it = groups_.find(g);
    if (it == groups_.end()) break;
    out.push_back(g);
    g = it->second.parent;
  }
  return out;
}

bool Document::isInGroup(ObjectId x, ObjectId groupId) const {
  return groupId != kInvalidId && contains(groupChain(x), groupId);
}

std::vector<ObjectId> Document::groupFeatures(ObjectId groupId) const {
  std::vector<ObjectId> out;
  if (!groups_.count(groupId)) return out;
  for (const auto& f : features_)
    if (isInGroup(f->id(), groupId)) out.push_back(f->id());
  return out;
}

std::pair<int, int> Document::groupRange(ObjectId groupId) const {
  int first = -1, last = -1;
  for (int i = 0; i < featureCount(); ++i)
    if (isInGroup(features_[static_cast<size_t>(i)]->id(), groupId)) {
      if (first < 0) first = i;
      last = i;
    }
  return {first, last};
}

std::vector<TimelineItem> Document::timelineItems() const {
  std::vector<TimelineItem> out;
  std::vector<ObjectId> open;  // groups of the previous row, outer -> inner
  for (const auto& f : features_) {
    std::vector<ObjectId> chain = groupChain(f->id());
    std::reverse(chain.begin(), chain.end());
    size_t k = 0;
    while (k < open.size() && k < chain.size() && open[k] == chain[k]) ++k;
    open.resize(k);
    for (; k < chain.size(); ++k) {
      out.push_back({TimelineItem::Kind::Group, chain[k], static_cast<int>(k), k ? chain[k - 1] : kInvalidId});
      open.push_back(chain[k]);
    }
    out.push_back({TimelineItem::Kind::Feature, f->id(), static_cast<int>(chain.size()), chain.empty() ? kInvalidId : chain.back()});
  }
  return out;
}

std::vector<ObjectId> Document::groupIds() const {
  std::vector<ObjectId> out;
  for (const TimelineItem& it : timelineItems())
    if (it.kind == TimelineItem::Kind::Group) out.push_back(it.id);
  return out;
}

std::vector<ObjectId> Document::groupChildren(ObjectId groupId) const {
  std::vector<ObjectId> out;
  if (groupId != kInvalidId && !groups_.count(groupId)) return out;
  for (const TimelineItem& it : timelineItems())
    if (it.parent == groupId) out.push_back(it.id);
  return out;
}

ObjectId Document::findGroup(const std::string& name) const {
  for (ObjectId g : groupIds())
    if (groups_.at(g).name == name) return g;
  const std::string low = lowerCopy(name);
  for (ObjectId g : groupIds())
    if (lowerCopy(groups_.at(g).name) == low) return g;
  return kInvalidId;
}

GroupSuppression Document::groupSuppression(ObjectId groupId) const {
  int on = 0, total = 0;
  for (ObjectId f : groupFeatures(groupId)) {
    ++total;
    if (feature(f)->isSuppressed()) ++on;
  }
  if (total == 0 || on == 0) return GroupSuppression::None;
  return on == total ? GroupSuppression::All : GroupSuppression::Some;
}

std::string Document::uniqueGroupName(const std::string& base) const {
  std::set<std::string> used;
  for (const auto& [id, g] : groups_) used.insert(g.name);
  for (int n = 1;; ++n) {
    std::string candidate = base + std::to_string(n);
    if (!used.count(candidate)) return candidate;
  }
}

// ---------------------------------------------------------------------------
// internal helpers
// ---------------------------------------------------------------------------

void Document::clearGroups() {
  groups_.clear();
  featureGroup_.clear();
  removedChains_.clear();
  removedGroups_.clear();
}

bool Document::groupHasChildren(ObjectId groupId, const std::vector<ObjectId>& ignoring) const {
  for (const auto& [f, g] : featureGroup_)
    if (!contains(ignoring, f) && isInGroup(f, groupId)) return true;
  return false;
}

bool Document::pruneEmptyGroups(ObjectId from, bool remember) {
  bool changed = false;
  ObjectId g = from;
  while (g != kInvalidId) {
    auto it = groups_.find(g);
    if (it == groups_.end() || groupHasChildren(g)) break;
    const ObjectId parent = it->second.parent;
    if (remember) removedGroups_[g] = it->second;
    groups_.erase(it);
    changed = true;
    g = parent;
  }
  return changed;
}

ObjectId Document::commonGroup(ObjectId a, ObjectId b) const {
  if (a == kInvalidId || b == kInvalidId) return kInvalidId;
  const std::vector<ObjectId> ca = groupChain(a), cb = groupChain(b);
  for (ObjectId g : ca)
    if (contains(cb, g)) return g;
  return kInvalidId;
}

bool Document::placementLegal(ObjectId g, ObjectId left, ObjectId right, const std::vector<ObjectId>& moving) const {
  const ObjectId ca = commonGroup(left, right);
  if (g == kInvalidId) return ca == kInvalidId;
  auto it = groups_.find(g);
  if (it == groups_.end()) return false;
  if (ca != kInvalidId && g != ca && !isInGroup(g, ca)) return false;  // would leave the enclosing group
  if ((left != kInvalidId && isInGroup(left, g)) || (right != kInvalidId && isInGroup(right, g))) return true;
  // a group whose only features are the moving ones goes wherever its parent may go
  if (!groupHasChildren(g, moving)) return placementLegal(it->second.parent, left, right, moving);
  return false;
}

ObjectId Document::choosePlacement(const std::vector<ObjectId>& hints, ObjectId left, ObjectId right,
                                   const std::vector<ObjectId>& moving) const {
  for (ObjectId h : hints)
    if (groupHasChildren(h, moving) && placementLegal(h, left, right, moving)) return h;
  return commonGroup(left, right);
}

void Document::placeInsertedFeature(ObjectId id, int index) {
  const int count = featureCount();
  const ObjectId left = index > 0 ? features_[static_cast<size_t>(index - 1)]->id() : kInvalidId;
  const ObjectId right = index + 1 < count ? features_[static_cast<size_t>(index + 1)]->id() : kInvalidId;
  ObjectId target = commonGroup(left, right);
  // A feature that comes back (undo of a delete, a module re-inserting what it took) returns to
  // the groups it was in where that still fits, re-creating groups that vanished with it.
  if (auto rc = removedChains_.find(id); rc != removedChains_.end()) {
    const std::vector<ObjectId> hints = rc->second;
    removedChains_.erase(rc);
    auto hintAt = [&](size_t k) { return k < hints.size() ? hints[k] : kInvalidId; };
    for (size_t k = 0; k <= hints.size(); ++k) {
      const ObjectId g = hintAt(k);
      if (g != kInvalidId && !groups_.count(g)) continue;  // removed (maybe revived below) or unknown
      if (!placementLegal(g, left, right, {})) continue;
      size_t j = k;
      while (j > 0) {
        auto dead = removedGroups_.find(hints[j - 1]);
        if (dead == removedGroups_.end() || dead->second.parent != hintAt(j)) break;
        --j;
      }
      for (size_t i = k; i > j; --i) {
        auto dead = removedGroups_.find(hints[i - 1]);
        groups_[dead->first] = dead->second;
        removedGroups_.erase(dead);
      }
      target = j < k ? hints[j] : g;
      break;
    }
  }
  if (target != kInvalidId) featureGroup_[id] = target;
}

void Document::detachFeatureFromGroups(ObjectId id) {
  const std::vector<ObjectId> chain = groupChain(id);
  if (chain.empty()) return;
  removedChains_[id] = chain;
  featureGroup_.erase(id);
  pruneEmptyGroups(chain.front(), true);
}

void Document::normalizeGroups() {
  for (bool again = true; again;) {
    again = false;
    for (auto it = featureGroup_.begin(); it != featureGroup_.end();)
      it = (indexOf(it->first) < 0 || !groups_.count(it->second)) ? featureGroup_.erase(it) : std::next(it);
    for (auto& [id, g] : groups_)
      if (g.parent != kInvalidId && !groups_.count(g.parent)) g.parent = kInvalidId;
    for (auto& [id, g] : groups_) {
      // break parent cycles
      std::set<ObjectId> seen{id};
      for (ObjectId p = g.parent; p != kInvalidId; p = groups_.at(p).parent)
        if (!seen.insert(p).second) {
          g.parent = kInvalidId;
          break;
        }
    }
    for (auto it = groups_.begin(); it != groups_.end();) {
      if (groupHasChildren(it->first)) {
        ++it;
        continue;
      }
      for (auto& [id, g] : groups_)
        if (g.parent == it->first) g.parent = it->second.parent;
      it = groups_.erase(it);
    }
    for (const auto& [id, g] : groups_) {
      const auto [first, last] = groupRange(id);
      if (static_cast<int>(groupFeatures(id).size()) == last - first + 1) continue;
      FC_WARN("timeline group '{}' is not consecutive, ungrouping it", g.name);
      const ObjectId split = id;
      const ObjectId parent = g.parent;
      for (auto& [f, fg] : featureGroup_)
        if (fg == split) fg = parent;
      for (auto& [cid, c] : groups_)
        if (c.parent == split) c.parent = parent;
      for (auto fit = featureGroup_.begin(); fit != featureGroup_.end();)
        fit = fit->second == kInvalidId ? featureGroup_.erase(fit) : std::next(fit);
      groups_.erase(split);
      again = true;
      break;
    }
  }
  for (const auto& [id, g] : groups_) nextId_ = std::max(nextId_, id + 1);
}

// ---------------------------------------------------------------------------
// moves
// ---------------------------------------------------------------------------

namespace {
/// Neighbours of the slot a feature lands in when moved to `newIndex` (final index).
std::pair<ObjectId, ObjectId> neighbours(std::vector<ObjectId> order, const std::vector<ObjectId>& moving, int newIndex) {
  order.erase(std::remove_if(order.begin(), order.end(), [&](ObjectId x) { return contains(moving, x); }), order.end());
  const int n = static_cast<int>(order.size());
  const ObjectId left = newIndex > 0 && newIndex - 1 < n ? order[static_cast<size_t>(newIndex - 1)] : kInvalidId;
  const ObjectId right = newIndex >= 0 && newIndex < n ? order[static_cast<size_t>(newIndex)] : kInvalidId;
  return {left, right};
}
}  // namespace

bool Document::canMoveFeatureTo(ObjectId id, int newIndex, std::optional<ObjectId> targetGroup, std::string* why) const {
  if (!canMoveFeature(id, newIndex, why)) return false;
  if (!targetGroup) return true;
  const ObjectId g = *targetGroup;
  if (g != kInvalidId && !groups_.count(g)) {
    if (why) *why = "unknown group";
    return false;
  }
  std::vector<ObjectId> order;
  for (const auto& f : features_) order.push_back(f->id());
  const auto [left, right] = neighbours(order, {id}, newIndex);
  if (placementLegal(g, left, right, {id})) return true;
  if (why) {
    const ObjectId ca = commonGroup(left, right);
    if (ca != kInvalidId && (g == kInvalidId || (g != ca && !isInGroup(g, ca))))
      *why = std::format("would split group '{}'", groups_.at(ca).name);
    else
      *why = std::format("group '{}' does not reach this position", groups_.at(g).name);
  }
  return false;
}

bool Document::moveFeatureTo(ObjectId id, int newIndex, std::optional<ObjectId> targetGroup) {
  if (!canMoveFeatureTo(id, newIndex, targetGroup)) return false;
  const int oldIndex = indexOf(id);
  std::vector<ObjectId> order;
  for (const auto& f : features_) order.push_back(f->id());
  const auto [left, right] = neighbours(order, {id}, newIndex);
  const ObjectId before = groupOf(id);
  const ObjectId target = targetGroup ? *targetGroup : choosePlacement(groupChain(id), left, right, {id});
  if (oldIndex != newIndex) rawMoveFeature(oldIndex, newIndex);
  if (target == before) return true;
  if (target == kInvalidId) featureGroup_.erase(id);
  else featureGroup_[id] = target;
  pruneEmptyGroups(before, false);
  touchModified();
  emit groupsChanged();
  return true;
}

bool Document::canMoveGroup(ObjectId groupId, int newIndex, std::string* why) const {
  if (!groups_.count(groupId)) {
    if (why) *why = "unknown group";
    return false;
  }
  const std::vector<ObjectId> block = groupFeatures(groupId);
  const int n = static_cast<int>(block.size());
  const int start = groupRange(groupId).first;
  if (newIndex < 0 || newIndex > featureCount() - n) {
    if (why) *why = "index out of range";
    return false;
  }
  if (newIndex == start) return true;
  std::vector<ObjectId> order;
  for (const auto& f : features_)
    if (!contains(block, f->id())) order.push_back(f->id());
  order.insert(order.begin() + newIndex, block.begin(), block.end());
  auto posOf = [&](ObjectId x) { return static_cast<int>(std::find(order.begin(), order.end(), x) - order.begin()); };
  for (ObjectId m : block) {
    for (ObjectId dep : dependenciesOf(m))
      if (!contains(block, dep) && posOf(dep) > posOf(m)) {
        if (why) *why = std::format("would move '{}' before its input '{}'", feature(m)->name(), feature(dep)->name());
        return false;
      }
    for (ObjectId dependent : dependentsOf(m))
      if (!contains(block, dependent) && posOf(dependent) < posOf(m)) {
        if (why) *why = std::format("would move '{}' after dependent '{}'", feature(m)->name(), feature(dependent)->name());
        return false;
      }
  }
  return true;
}

bool Document::moveGroup(ObjectId groupId, int newIndex) {
  if (!canMoveGroup(groupId, newIndex)) return false;
  const std::vector<ObjectId> block = groupFeatures(groupId);
  const int n = static_cast<int>(block.size());
  const int start = groupRange(groupId).first;
  if (newIndex == start) return true;
  std::vector<ObjectId> order;
  for (const auto& f : features_) order.push_back(f->id());
  const auto [left, right] = neighbours(order, block, newIndex);
  const ObjectId before = groups_.at(groupId).parent;
  const ObjectId target = choosePlacement(groupChain(groupId), left, right, block);
  if (newIndex < start)
    for (int k = 0; k < n; ++k) rawMoveFeature(start + k, newIndex + k);
  else
    for (int k = n - 1; k >= 0; --k) rawMoveFeature(start + k, newIndex + k);
  if (target != before) {
    groups_.at(groupId).parent = target;
    pruneEmptyGroups(before, false);
    touchModified();
    emit groupsChanged();
  }
  return true;
}

// ---------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------

bool Document::canCreateGroup(const std::vector<ObjectId>& items, std::string* why) const {
  auto fail = [&](std::string w) {
    if (why) *why = std::move(w);
    return false;
  };
  if (items.empty()) return fail("nothing to group");
  std::set<int> idx;
  std::vector<ObjectId> itemGroups;
  for (ObjectId x : items) {
    if (feature(x)) idx.insert(indexOf(x));
    else if (groups_.count(x)) {
      itemGroups.push_back(x);
      const auto [first, last] = groupRange(x);
      for (int i = first; i <= last && first >= 0; ++i) idx.insert(i);
    } else {
      return fail(std::format("#{} is not a feature or group of this document", x));
    }
  }
  const int a = *idx.begin(), b = *idx.rbegin();
  if (b - a + 1 != static_cast<int>(idx.size())) return fail("the features to group are not consecutive");
  for (const auto& [gid, g] : groups_) {
    const auto [g1, g2] = groupRange(gid);
    if (g2 < a || g1 > b) continue;               // disjoint
    if (g1 >= a && g2 <= b) continue;             // goes inside the new group
    if (g1 <= a && g2 >= b) continue;             // encloses the new group
    return fail(std::format("would split group '{}'", g.name));
  }
  return true;
}

ObjectId Document::createGroup(const std::vector<ObjectId>& items, std::string name, ObjectId id, std::string* why) {
  if (!canCreateGroup(items, why)) return kInvalidId;
  if (id != kInvalidId && (groups_.count(id) || feature(id))) {
    if (why) *why = std::format("id #{} is in use", id);
    return kInvalidId;
  }
  int a = featureCount(), b = -1;
  std::vector<ObjectId> inside;  // selected groups and the groups nested in them
  for (ObjectId x : items) {
    if (feature(x)) {
      a = std::min(a, indexOf(x));
      b = std::max(b, indexOf(x));
    } else {
      const auto [first, last] = groupRange(x);
      a = std::min(a, first);
      b = std::max(b, last);
      inside.push_back(x);
    }
  }
  const std::vector<ObjectId> selectedGroups = inside;
  for (const auto& [gid, g] : groups_)
    for (ObjectId x : selectedGroups)
      if (isInGroup(gid, x) && !contains(inside, gid)) inside.push_back(gid);
  // the new group's parent: the deepest group around the range that is not being grouped itself
  ObjectId parent = kInvalidId;
  size_t depth = 0;
  for (const auto& [gid, g] : groups_) {
    if (contains(inside, gid)) continue;
    const auto [g1, g2] = groupRange(gid);
    if (g1 <= a && g2 >= b) {
      const size_t d = groupChain(gid).size() + 1;
      if (d > depth) {
        depth = d;
        parent = gid;
      }
    }
  }
  if (id == kInvalidId) id = allocateId();
  else nextId_ = std::max(nextId_, id + 1);
  TimelineGroup ng;
  ng.id = id;
  ng.name = name.empty() ? uniqueGroupName() : std::move(name);
  ng.parent = parent;
  for (int i = a; i <= b; ++i) {
    const ObjectId f = features_[static_cast<size_t>(i)]->id();
    if (groupOf(f) == parent) featureGroup_[f] = id;
  }
  for (auto& [gid, g] : groups_) {
    if (g.parent != parent) continue;
    const auto [g1, g2] = groupRange(gid);
    if (g1 >= a && g2 <= b) g.parent = id;
  }
  groups_[id] = ng;
  touchModified();
  emit groupsChanged();
  return id;
}

bool Document::ungroup(ObjectId groupId) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return false;
  const ObjectId parent = it->second.parent;
  for (auto f = featureGroup_.begin(); f != featureGroup_.end();) {
    if (f->second == groupId) {
      if (parent == kInvalidId) {
        f = featureGroup_.erase(f);
        continue;
      }
      f->second = parent;
    }
    ++f;
  }
  for (auto& [gid, g] : groups_)
    if (g.parent == groupId) g.parent = parent;
  groups_.erase(groupId);
  touchModified();
  emit groupsChanged();
  return true;
}

bool Document::renameGroup(ObjectId groupId, std::string name) {
  auto it = groups_.find(groupId);
  if (it == groups_.end() || name.empty()) return false;
  if (it->second.name == name) return true;
  it->second.name = std::move(name);
  touchModified();
  emit groupsChanged();
  return true;
}

bool Document::setGroupCollapsed(ObjectId groupId, bool collapsed) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return false;
  if (it->second.collapsed == collapsed) return true;
  it->second.collapsed = collapsed;
  touchModified();
  emit groupsChanged();
  return true;
}

void Document::setGroupState(TimelineGroupState state) {
  if (state.groups == groups_ && state.featureGroup == featureGroup_) return;
  groups_ = std::move(state.groups);
  featureGroup_ = std::move(state.featureGroup);
  normalizeGroups();
  touchModified();
  emit groupsChanged();
}

// ---------------------------------------------------------------------------
// serialisation: "groups": [{id, name, parent?, collapsed?, features: [direct members]}]
// ---------------------------------------------------------------------------

QJsonArray Document::groupsToJson() const {
  QJsonArray out;
  for (ObjectId gid : groupIds()) {
    const TimelineGroup& g = groups_.at(gid);
    QJsonObject o;
    o["id"] = idToJson(gid);
    o["name"] = qs(g.name);
    if (g.parent != kInvalidId) o["parent"] = idToJson(g.parent);
    if (g.collapsed) o["collapsed"] = true;
    std::vector<ObjectId> members;
    for (const auto& f : features_)
      if (groupOf(f->id()) == gid) members.push_back(f->id());
    o["features"] = core::toJson(members);
    out.append(o);
  }
  return out;
}

void Document::groupsFromJson(const QJsonArray& arr) {
  groups_.clear();
  featureGroup_.clear();
  for (const auto& v : arr) {
    const QJsonObject o = v.toObject();
    TimelineGroup g;
    g.id = idFromJson(o["id"]);
    if (g.id == kInvalidId || groups_.count(g.id)) continue;
    g.name = ss(o["name"].toString());
    if (g.name.empty()) g.name = uniqueGroupName();
    g.parent = idFromJson(o["parent"]);
    g.collapsed = o["collapsed"].toBool(false);
    groups_[g.id] = g;
    for (ObjectId f : idsFromJson(o["features"])) featureGroup_[f] = g.id;
  }
  normalizeGroups();
}

}  // namespace fabricad::core
