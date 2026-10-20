// FabriCAD app: toolbar structure from the registry.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ToolbarModel.h"

#include <algorithm>

namespace fabricad::app {

namespace {
std::vector<std::string> ordered(std::vector<std::string> names, const std::vector<std::string>& preferred) {
  std::vector<std::string> out;
  for (const std::string& p : preferred)
    if (std::find(names.begin(), names.end(), p) != names.end()) out.push_back(p);
  for (const std::string& n : names)
    if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
  return out;
}
}  // namespace

bool isContextualTab(const std::string& tab) { return tab == "SKETCH"; }

std::vector<std::string> orderedTabs(std::vector<std::string> tabs) {
  static const std::vector<std::string> kPreferred = {"SOLID",   "SURFACE", "SKETCH", "ASSEMBLE", "CONSTRUCT",
                                                      "INSPECT", "INSERT",  "TOOLS",  "DRAWING"};
  return ordered(std::move(tabs), kPreferred);
}

std::vector<std::string> orderedGroups(std::vector<std::string> groups) {
  static const std::vector<std::string> kPreferred = {
      "CREATE",     "MODIFY",  "ASSEMBLE",   "CONSTRUCT", "CONSTRAINTS", "GEOMETRY", "DIMENSIONS", "TEXT",
      "SYMBOLS",    "TABLES",  "INSPECT",    "INSERT",    "EXPORT",      "SELECT",   "FINISH SKETCH", "FINISH"};
  return ordered(std::move(groups), kPreferred);
}

std::vector<ToolbarTab> buildToolbarModel(const core::ToolRegistry& registry, core::Workspace ws) {
  std::vector<ToolbarTab> out;
  for (const std::string& tabName : orderedTabs(registry.tabs(ws))) {
    ToolbarTab tab;
    tab.name = tabName;
    tab.contextual = isContextualTab(tabName);
    for (const std::string& groupName : orderedGroups(registry.groups(ws, tabName))) {
      ToolbarGroup g;
      g.name = groupName;
      g.all = registry.toolsFor(ws, tabName, groupName);
      for (core::ITool* t : g.all)
        if (t->info().promoted) g.promoted.push_back(t);
      if (g.promoted.empty())
        for (size_t i = 0; i < g.all.size() && i < 2; ++i) g.promoted.push_back(g.all[i]);
      tab.groups.push_back(std::move(g));
    }
    out.push_back(std::move(tab));
  }
  return out;
}

std::string toolTooltip(const core::ToolInfo& info) {
  std::string s = info.name;
  if (!info.shortcut.empty()) s += "  (" + info.shortcut + ")";
  if (!info.tooltip.empty()) s += "\n" + info.tooltip;
  return s;
}

}  // namespace fabricad::app
