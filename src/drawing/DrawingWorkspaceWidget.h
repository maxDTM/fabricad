// FabriCAD drawing: the widget the app hosts as central widget when the Drawing workspace is
// active. Layout per docs/research/ux.md section 11: sheet browser (left), sheet canvas
// (centre), sheet tabs + status (bottom). The app owns toolbars; drawing tools reach the
// canvas through DrawingWorkspaceWidget::forDocument()/activeCanvas().
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QTabBar;
class QToolButton;

namespace fabricad::core {
class Document;
class ToolManager;
}

namespace fabricad::drawing {

class DrawingCanvas;
class DrawingModel;

class DrawingWorkspaceWidget : public QWidget {
  Q_OBJECT
 public:
  explicit DrawingWorkspaceWidget(QWidget* parent = nullptr);
  ~DrawingWorkspaceWidget() override;

  /// Bind to a document (creates the DrawingModel on demand). nullptr unbinds.
  void setDocument(core::Document* doc);
  core::Document* document() const { return doc_; }
  DrawingModel* model() const { return model_; }
  DrawingCanvas* canvas() const { return canvas_; }
  /// Tool manager used for Esc/Enter/Delete/double-click edits in the canvas.
  void setToolManager(core::ToolManager* manager);
  core::ToolManager* toolManager() const { return manager_; }

  core::ObjectId activeSheetId() const;
  void setActiveSheet(core::ObjectId id);
  /// Show/hide the sheet browser panel.
  void setBrowserVisible(bool visible);

  /// Widget currently bound to `doc` (nullptr if none). Tools use this to find the canvas.
  static DrawingWorkspaceWidget* forDocument(const core::Document* doc);
  /// Canvas for the document, or nullptr headless / when no widget is bound.
  static DrawingCanvas* activeCanvas(const core::Document* doc);
  /// All live instances (most recently created last).
  static const std::vector<DrawingWorkspaceWidget*>& instances();

 signals:
  void sheetActivated(fabricad::core::ObjectId sheetId);

 private:
  void refreshSheets();
  void rebuildSheetList();
  void onSheetTabChanged(int index);
  void onAddSheet();
  void onRemoveSheet();
  void updateStatus();

  core::Document* doc_ = nullptr;
  DrawingModel* model_ = nullptr;
  core::ToolManager* manager_ = nullptr;
  DrawingCanvas* canvas_ = nullptr;
  QListWidget* sheetList_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QToolButton* addSheet_ = nullptr;
  QToolButton* removeSheet_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* coords_ = nullptr;
  QWidget* browser_ = nullptr;
  bool refreshing_ = false;
  bool refreshAgain_ = false;
  QWidget* emptyState_ = nullptr;  ///< shown instead of the canvas while there is no sheet
  QString busyText_;
  std::vector<QMetaObject::Connection> modelConnections_;
};

}  // namespace fabricad::drawing
