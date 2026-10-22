// FabriCAD core: selection model (what the user has picked) + hover preselection.
// Owned by the Document; the viewer, browser, timeline and command dialogs all talk to it.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <QObject>

#include <optional>
#include <vector>

namespace fabricad::core {

class SelectionModel : public QObject {
  Q_OBJECT
 public:
  explicit SelectionModel(QObject* parent = nullptr);

  const std::vector<ShapeRef>& items() const { return items_; }
  int count() const { return static_cast<int>(items_.size()); }
  bool isEmpty() const { return items_.empty(); }
  bool contains(const ShapeRef& r) const;
  /// Items of one kind (mask).
  std::vector<ShapeRef> itemsOfKind(ShapeKindMask kinds) const;
  /// First item or nullopt.
  std::optional<ShapeRef> first() const;

  void set(std::vector<ShapeRef> items);
  void set(const ShapeRef& item) { set(std::vector<ShapeRef>{item}); }
  void add(const ShapeRef& r);
  void remove(const ShapeRef& r);
  void toggle(const ShapeRef& r);
  void clear();
  /// Drop items whose owner is `owner` (feature/body deleted).
  void removeOwner(ObjectId owner);

  /// Kinds the user may currently pick (tools set this while active; viewer honours it).
  ShapeKindMask filter() const { return filter_; }
  void setFilter(ShapeKindMask m);
  /// User selection filters (SELECT > Selection Filters): kinds switched off here cannot be picked
  /// while no tool restricts the filter itself; a tool's own filter always wins, so a filter never
  /// blocks the input a command asks for.
  ShapeKindMask userFilter() const { return userFilter_; }
  void setUserFilter(ShapeKindMask m);
  ShapeKindMask effectiveFilter() const { return filter_ == kAllKinds ? userFilter_ : filter_; }
  bool isAllowed(const ShapeRef& r) const { return hasKind(effectiveFilter(), r.kind); }

  /// Hover highlight (not part of the selection).
  const std::optional<ShapeRef>& preselection() const { return preselection_; }
  void setPreselection(std::optional<ShapeRef> r);

  /// While false, add/toggle/set are ignored (e.g. during a drag).
  bool isEnabled() const { return enabled_; }
  void setEnabled(bool e) { enabled_ = e; }

 signals:
  void changed();
  void filterChanged(fabricad::core::ShapeKindMask filter);
  void preselectionChanged();

 private:
  std::vector<ShapeRef> items_;
  std::optional<ShapeRef> preselection_;
  ShapeKindMask filter_ = kAllKinds;
  ShapeKindMask userFilter_ = kAllKinds;
  bool enabled_ = true;
};

}  // namespace fabricad::core
