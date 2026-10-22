// FabriCAD app: the view-tool column at the right edge of the viewport, below the orientation
// widget (docs/plans/ui-redesign.md 5.3; it replaced the bottom nav bar of ux.md 5.2 and keeps
// its objectName #NavBar):
//   [Fit] [Orbit] [Pan] [Zoom] | [Look At] [Display v] [Grid v] [Views v] [Nav v]
// Orbit/Pan/Zoom are sticky modes (LMB drags navigate until Esc or toggled off); the menus
// cover visual style, projection, object visibility, grid/snap, named views, navigation
// preset and reverse zoom. Preferences (preset, reverse zoom) persist in QSettings.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFrame>

class QActionGroup;
class QMenu;
class QToolButton;

namespace fabricad::core {
class ToolManager;
}
namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

class NavBar : public QFrame {
  Q_OBJECT
 public:
  NavBar(viewer::ViewerWidget& viewer, core::ToolManager& manager);
  /// Refresh check marks from the viewer state.
  void refresh();
  void reposition();
  /// Load preferences from QSettings and apply them to the viewer.
  void applySettings();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  QToolButton* button(const char* icon, const QString& tip);
  QMenu* buildDisplayMenu();
  QMenu* buildGridMenu();
  QMenu* buildViewsMenu();
  QMenu* buildNavMenu();
  void runTool(const char* id);

  viewer::ViewerWidget& viewer_;
  core::ToolManager& manager_;
  QToolButton* orbit_ = nullptr;
  QToolButton* pan_ = nullptr;
  QToolButton* zoom_ = nullptr;
  QMenu* display_ = nullptr;
  QMenu* grid_ = nullptr;
  QMenu* nav_ = nullptr;
};

}  // namespace fabricad::app
