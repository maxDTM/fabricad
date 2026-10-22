// FabriCAD app: Browser tree dock (ux.md section 3).
//
//   v (o) Part1                 root component (radio = active component)
//     > Origin            (eye) O, X, Y, Z, XY, XZ, YZ
//     > Bodies            (eye) Body1 ...
//     > Sketches          (eye) Sketch1 ...   (icon/text colour from feature status)
//     > Construction      (eye) Plane1 ...
//     > Joints            (eye)
//     > Bracket:1         (eye) child components, same sub-folders for bodies
//
// Column 1 is the visibility eye. Bodies/components visibility is document data (undoable
// makeBodyEditCommand / makeComponentEditCommand); sketch/construction/joint and folder eyes
// are viewer display state (ViewerWidget). Rebuilt from Document signals (coalesced) while
// keeping expansion; selection is mirrored both ways with SelectionModel; hover sets the
// preselection.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <QTreeWidget>

#include <set>
#include <string>

namespace fabricad::core {
class Document;
}
namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

class BrowserTree : public QTreeWidget {
  Q_OBJECT
 public:
  enum ItemKind { KindRoot = 1, KindFolder, KindBody, KindFeature, KindComponent, KindOrigin };
  enum Roles { RoleKind = Qt::UserRole + 1, RoleId, RoleKey, RoleFolder, RoleOrigin };

  BrowserTree(core::Document& doc, viewer::ViewerWidget* viewer, QWidget* parent = nullptr);

  /// Rebuild now (normally coalesced to the next event-loop turn).
  void rebuildNow();
  /// Item for a stable key ("body:18", "feat:16", "comp:23", "folder:1:Bodies", "origin:XY").
  QTreeWidgetItem* itemForKey(const QString& key) const;
  /// ShapeRef an item stands for (invalid for folders).
  core::ShapeRef refForItem(const QTreeWidgetItem* item) const;
  /// Toggle the eye of an item (same as clicking column 1).
  void toggleVisibility(QTreeWidgetItem* item);
  void scrollToFeature(core::ObjectId id);

 signals:
  void editFeatureRequested(fabricad::core::ObjectId id);
  void deleteRequested(const std::vector<fabricad::core::ShapeRef>& refs);
  void findInTimelineRequested(fabricad::core::ObjectId id);
  void suppressRequested(fabricad::core::ObjectId id, bool suppress);
  /// Run a registered tool (Copy / Paste component...).
  void runToolRequested(const std::string& toolId);

 protected:
  void contextMenuEvent(QContextMenuEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) override;

 public:
  /// Inline rename of a row (F2, context menu, double-click on bodies/components). Components
  /// show "Name:1" but the editor starts with the bare name.
  void startRename(QTreeWidgetItem* item);

 protected:

 private:
  void scheduleRebuild();
  void addMaterialMenu(QMenu& menu, core::ObjectId bodyId);
  void editComponentProperties(core::ObjectId componentId);
  void pickBodyAppearance(core::ObjectId bodyId);
  void fillComponent(QTreeWidgetItem* parent, core::ObjectId compId, bool root);
  QTreeWidgetItem* addFolder(QTreeWidgetItem* parent, core::ObjectId compId, const QString& name, bool eye, bool visible);
  void updateEye(QTreeWidgetItem* item, bool hasEye, bool visible);
  void onItemClicked(QTreeWidgetItem* item, int column);
  void onSelectionFromTree();
  void syncSelectionFromModel();
  void onItemRenamed(QTreeWidgetItem* item, int column);
  void collectExpanded(QTreeWidgetItem* item, std::set<QString>& out) const;
  bool folderVisible(core::ObjectId compId, const QString& folder) const;
  void setFolderVisible(core::ObjectId compId, const QString& folder, bool visible);

  core::Document& doc_;
  viewer::ViewerWidget* viewer_;
  bool pending_ = false;
  bool syncing_ = false;
  bool rebuilding_ = false;
  std::set<QString> collapsed_;
  bool firstBuild_ = true;
  QString hoverKey_;
};

}  // namespace fabricad::app
