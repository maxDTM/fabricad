// FabriCAD app: Fusion-style tabbed toolbar ("ribbon") built from the ToolRegistry.
//
//   [DESIGN v] | SOLID  SKETCH*  TOOLS ...                       (* contextual, accent colour)
//              | [big][big][big]   [big][big]   ...  [FINISH SKETCH]
//              |    CREATE v         MODIFY v
//
// Each group shows its promoted tools as 32 px icons and a label button whose drop-down menu
// lists every tool of the group (icon, name, shortcut). Structure comes from
// app::buildToolbarModel (unit-tested). Enabled states are refreshed cheaply on demand.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/ToolbarModel.h"
#include "core/Tool.h"

#include <QWidget>

#include <map>
#include <set>
#include <string>
#include <vector>

class QAction;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace fabricad::app {

class Ribbon : public QWidget {
  Q_OBJECT
 public:
  explicit Ribbon(core::ToolManager& manager, QWidget* parent = nullptr);
  /// Rebind to another tab's tool manager (multi-document tabs).
  void setToolManager(core::ToolManager* m) { manager_ = m; }
  core::ToolManager& manager() const { return *manager_; }

  /// Rebuild tabs for a workspace (after registration or a workspace switch).
  void rebuild(core::Workspace ws);
  core::Workspace workspace() const { return ws_; }
  /// Show/hide a contextual tab; showing also activates it, hiding returns to the previous tab.
  void setContextualTab(const std::string& name, bool visible);
  bool isTabVisible(const std::string& name) const;
  std::string currentTab() const;
  void setCurrentTab(const std::string& name);
  std::vector<std::string> tabNames() const;
  /// Update enabled/checked states (selection changed, tool started/finished, ...).
  void refreshStates();
  /// Number of tool buttons + menu actions (tests).
  int toolActionCount() const { return static_cast<int>(actions_.size()); }

 signals:
  void toolTriggered(const std::string& id);
  void workspaceRequested(fabricad::core::Workspace ws);

 private:
  QWidget* buildPage(const ToolbarTab& tab);
  QToolButton* bigButton(core::ITool* tool, QWidget* parent);
  void updateTabStyle();

  core::ToolManager* manager_;
  core::Workspace ws_ = core::Workspace::Design;
  QToolButton* switcher_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  std::vector<std::string> tabNames_;
  std::set<std::string> contextual_;
  std::set<std::string> contextualShown_;
  std::string tabBeforeContextual_;
  /// (tool id, action or button) pairs for state refresh
  std::vector<std::pair<std::string, QObject*>> actions_;
};

}  // namespace fabricad::app
