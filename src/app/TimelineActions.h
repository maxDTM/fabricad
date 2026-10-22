// FabriCAD app: the timeline commands and context menu shared by the FEATURES list and the
// bottom strip (docs/plans/feature-list-timeline.md, "Shared code"), so both views behave the
// same. Edits that the document can do on its own (rollback, suppress, groups, rename) are
// pushed here as undo commands; edits that need the shell (edit a feature, delete with its
// confirmation, find in the MODEL tree) are signals the owning view forwards to MainWindow.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QObject>
#include <QString>

#include <vector>

class QMenu;

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

class TimelineActions : public QObject {
  Q_OBJECT
 public:
  enum class View { FeatureList, Strip };
  TimelineActions(core::Document& doc, View view, QObject* parent = nullptr);

  core::Document& document() const { return doc_; }

  /// Fill a context menu. `items` are feature and/or group ids (the clicked row first, then the
  /// rest of the selection); empty = the background of the view.
  void populate(QMenu& menu, const std::vector<core::ObjectId>& items);

  // --- direct operations (keys and menus) ----------------------------------------------------
  /// SetRollbackCommand when the index changes (clamped).
  void setRollback(int index);
  /// Space: suppress the items unless all of them already are (then unsuppress). One undo step.
  void toggleSuppress(const std::vector<core::ObjectId>& items);
  void setSuppressed(const std::vector<core::ObjectId>& items, bool suppressed);
  /// Group the items (consecutive features / groups). False + message when refused.
  bool group(const std::vector<core::ObjectId>& items);
  void ungroup(core::ObjectId groupId);
  void setCollapsed(core::ObjectId groupId, bool collapsed);
  /// Rename a feature or a group (as typed). False when empty or unchanged.
  bool rename(core::ObjectId id, const QString& name);
  /// Every feature id covered by the items (groups expanded), in timeline order.
  std::vector<core::ObjectId> featuresOf(const std::vector<core::ObjectId>& items) const;

 signals:
  void editRequested(fabricad::core::ObjectId id);
  /// Inline rename of a feature or group row (the view decides how).
  void renameRequested(fabricad::core::ObjectId id);
  void deleteRequested(const std::vector<fabricad::core::ObjectId>& features);
  void findInBrowserRequested(fabricad::core::ObjectId id);
  /// A refusal or note for the status bar (core::MessageLevel as int).
  void messageRequested(const QString& text, int level);

 private:
  core::Document& doc_;
  View view_;
};

}  // namespace fabricad::app
