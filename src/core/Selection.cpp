// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Selection.h"

#include <algorithm>

namespace fabricad::core {

SelectionModel::SelectionModel(QObject* parent) : QObject(parent) {}

bool SelectionModel::contains(const ShapeRef& r) const {
  return std::find(items_.begin(), items_.end(), r) != items_.end();
}

std::vector<ShapeRef> SelectionModel::itemsOfKind(ShapeKindMask kinds) const {
  std::vector<ShapeRef> out;
  for (const auto& r : items_)
    if (hasKind(kinds, r.kind)) out.push_back(r);
  return out;
}

std::optional<ShapeRef> SelectionModel::first() const {
  if (items_.empty()) return std::nullopt;
  return items_.front();
}

void SelectionModel::set(std::vector<ShapeRef> items) {
  if (!enabled_) return;
  std::vector<ShapeRef> filtered;
  for (auto& r : items)
    if (isAllowed(r) && std::find(filtered.begin(), filtered.end(), r) == filtered.end()) filtered.push_back(std::move(r));
  if (filtered == items_) return;
  items_ = std::move(filtered);
  emit changed();
}

void SelectionModel::add(const ShapeRef& r) {
  if (!enabled_ || !isAllowed(r) || contains(r)) return;
  items_.push_back(r);
  emit changed();
}

void SelectionModel::remove(const ShapeRef& r) {
  auto it = std::find(items_.begin(), items_.end(), r);
  if (it == items_.end()) return;
  items_.erase(it);
  emit changed();
}

void SelectionModel::toggle(const ShapeRef& r) {
  if (contains(r)) remove(r);
  else add(r);
}

void SelectionModel::clear() {
  if (items_.empty()) return;
  items_.clear();
  emit changed();
}

void SelectionModel::removeOwner(ObjectId owner) {
  size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(), [owner](const ShapeRef& r) { return r.owner == owner; }),
               items_.end());
  if (preselection_ && preselection_->owner == owner) {
    preselection_.reset();
    emit preselectionChanged();
  }
  if (items_.size() != before) emit changed();
}

void SelectionModel::setUserFilter(ShapeKindMask m) {
  if (m == userFilter_) return;
  userFilter_ = m;
  emit filterChanged(filter_);
}

void SelectionModel::setFilter(ShapeKindMask m) {
  if (m == filter_) return;
  filter_ = m;
  emit filterChanged(filter_);
  // drop items that are no longer allowed
  size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(), [this](const ShapeRef& r) { return !isAllowed(r); }), items_.end());
  if (items_.size() != before) emit changed();
}

void SelectionModel::setPreselection(std::optional<ShapeRef> r) {
  if (r && !isAllowed(*r)) r.reset();
  if (r == preselection_) return;
  preselection_ = std::move(r);
  emit preselectionChanged();
}

}  // namespace fabricad::core
