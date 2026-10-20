// FabriCAD app: toolbar (ribbon) structure built from the ToolRegistry. Widget-free so the
// ordering/promotion rules are unit-tested (tests/test_app_logic.cpp).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <string>
#include <vector>

namespace fabricad::app {

struct ToolbarGroup {
  std::string name;                      ///< "CREATE"
  std::vector<core::ITool*> promoted;    ///< big icons (ToolInfo::promoted; first two if none)
  std::vector<core::ITool*> all;         ///< dropdown content, registry order by ToolInfo::order
};

struct ToolbarTab {
  std::string name;   ///< "SOLID"
  bool contextual = false;  ///< only shown while active (SKETCH)
  std::vector<ToolbarGroup> groups;
};

/// Tabs known to be contextual (shown on demand via IAppHost::setContextualTab).
bool isContextualTab(const std::string& tab);
/// Preferred tab/group order (Fusion-like); unknown names follow in first-seen order.
std::vector<std::string> orderedTabs(std::vector<std::string> tabs);
std::vector<std::string> orderedGroups(std::vector<std::string> groups);
/// Build the toolbar for a workspace.
std::vector<ToolbarTab> buildToolbarModel(const core::ToolRegistry& registry, core::Workspace ws);
/// "Fillet (F)" style tooltip text; plain text, multi-line.
std::string toolTooltip(const core::ToolInfo& info);

}  // namespace fabricad::app
