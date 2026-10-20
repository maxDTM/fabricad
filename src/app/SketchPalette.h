// FabriCAD app: the floating SKETCH PALETTE (ux.md section 7, step 2), shown on the right of
// the viewport while a sketch is being edited (sketch::SketchSession::current()).
//   Options: Construction linetype, Look At, Sketch Grid, Snap, Show Profile / Points /
//   Dimensions / Constraints;  Constraints: one-click buttons for every SKETCH > CONSTRAINTS
//   tool;  [Finish Sketch].
// Options write straight into the session (and the viewer grid); the palette re-reads them
// whenever the session changes.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFrame>
#include <QPointer>

#include <string>

class QCheckBox;
class QGridLayout;

namespace fabricad::core {
class ToolManager;
}
namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

class SketchPalette : public QFrame {
  Q_OBJECT
 public:
  SketchPalette(viewer::ViewerWidget& viewer, core::ToolManager& manager);
  /// Show/hide and refresh from the active sketch session.
  void sync();
  /// Place at the right edge, below `above` (the command dialog) when it is visible.
  void reposition(const QWidget* above);

 signals:
  void commandRequested(const std::string& id);

 private:
  void rebuildConstraintButtons();
  QCheckBox* option(const QString& text, const QString& tip);

  viewer::ViewerWidget& viewer_;
  core::ToolManager& manager_;
  QCheckBox* construction_ = nullptr;
  QCheckBox* grid_ = nullptr;
  QCheckBox* snap_ = nullptr;
  QCheckBox* profiles_ = nullptr;
  QCheckBox* points_ = nullptr;
  QCheckBox* dimensions_ = nullptr;
  QCheckBox* constraints_ = nullptr;
  QGridLayout* constraintGrid_ = nullptr;
  QWidget* constraintBox_ = nullptr;
  size_t toolCount_ = 0;
  bool updating_ = false;
};

}  // namespace fabricad::app
