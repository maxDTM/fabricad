// FabriCAD app: toolbar structure from the registry.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ToolbarModel.h"

#include <algorithm>
#include <map>
#include <numeric>

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

/// Tools shown in a second ribbon place besides their own tab/group (ux.md: TOOLS has
/// Parameters, Preferences, Export). The tool is not duplicated, only listed twice. APP-03.
struct ExtraPlacement {
  const char* id;
  const char* tab;
  const char* group;
};
constexpr ExtraPlacement kExtraPlacements[] = {{"app.parameters", "TOOLS", "UTILITIES"}};
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
      "SYMBOLS",    "TABLES",  "INSPECT",    "INSERT",    "UTILITIES", "EXPORT",      "SELECT",   "FINISH SKETCH", "FINISH"};
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
      for (auto it = std::rbegin(kExtraPlacements); it != std::rend(kExtraPlacements); ++it) {
        if (tabName != it->tab || groupName != it->group) continue;
        core::ITool* t = registry.tool(it->id);
        if (!t || t->info().hidden || t->info().workspace != ws) continue;
        if (std::find(g.all.begin(), g.all.end(), t) == g.all.end()) g.all.insert(g.all.begin(), t);
      }
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

// ---- MainToolbar sections ----------------------------------------------------------------

namespace {

/// Registry tabs whose groups become sections of the default Design page; every other tab
/// (SURFACE, TOOLS, plug-in tabs) folds into MORE (decision 5).
const std::vector<std::string> kMainTabs = {"SOLID", "ASSEMBLE", "CONSTRUCT", "INSPECT"};
/// Groups of a main tab that fold into MORE rather than getting their own section (the
/// selection filter bar of phase 4 takes over SELECT).
const std::vector<std::string> kFoldedGroups = {"INSERT", "SELECT", "OPTIONS"};
/// Section order of the default page; unknown sections follow in registry order.
const std::vector<std::string> kSectionOrder = {"SKETCH", "CREATE", "MODIFY", "ASSEMBLE", "CONSTRUCT", "INSPECT"};
/// Tools of the DOCUMENT section (right-aligned), in order.
const std::vector<std::string> kDocumentTools = {"app.parameters", "app.preferences"};

/// Higher collapses first (MORE and the inspection tools before the modelling sections).
int collapsePriorityFor(const std::string& section) {
  static const std::map<std::string, int> kPriority = {
      {"MORE", 60},      {"DOCUMENT", 55}, {"INSPECT", 50}, {"ASSEMBLE", 45}, {"CONSTRUCT", 40},
      {"OPTIONS", 38},   {"MODIFY", 30},   {"SKETCH", 25},  {"CONSTRAINTS", 20}, {"CREATE", 10}};
  auto it = kPriority.find(section);
  return it == kPriority.end() ? 35 : it->second;
}

/// Assembly filter (decision 2, tentative): ASSEMBLE first with all its main tools, the
/// modelling sections stay reachable through their caption dropdowns. Changing the filter is
/// this table only.
struct FilterRule {
  const char* section;
  int order;            ///< sort key (lower first); sections without a rule keep their place
  bool collapsed;       ///< caption only
  int extraPromoted;    ///< show this many tools as buttons (0 = the registry's promoted set)
};
constexpr FilterRule kAssemblyRules[] = {
    {"ASSEMBLE", 0, false, 6}, {"CONSTRUCT", 1, false, 0}, {"INSPECT", 2, false, 0},
    {"SKETCH", 3, true, 0},    {"CREATE", 4, true, 0},     {"MODIFY", 5, true, 0},
};

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

void promote(ToolbarSection& s) {
  for (core::ITool* t : s.all)
    if (t->info().promoted) s.promoted.push_back(t);
  if (s.promoted.empty())
    for (size_t i = 0; i < s.all.size() && i < 2; ++i) s.promoted.push_back(s.all[i]);
}

ToolbarSection& sectionNamed(std::vector<ToolbarSection>& out, const std::string& name, const std::string& tab) {
  for (ToolbarSection& s : out)
    if (s.name == name) return s;
  ToolbarSection s;
  s.name = name;
  s.toolbarTab = tab;
  s.toolbarGroup = name;
  s.collapsePriority = collapsePriorityFor(name);
  out.push_back(std::move(s));
  return out.back();
}

void addMore(std::vector<MoreEntry>& more, const std::string& label, const std::string& tab, const std::string& group,
             std::vector<core::ITool*> tools) {
  if (tools.empty()) return;
  for (MoreEntry& e : more)
    if (e.label == label) {
      e.groups.emplace_back(group, std::move(tools));
      return;
    }
  more.push_back(MoreEntry{label, tab, {{group, std::move(tools)}}});
}

}  // namespace

const char* designFilterName(DesignFilter f) { return f == DesignFilter::Assembly ? "assembly" : "part"; }

bool parseDesignFilter(const std::string& s, DesignFilter* out) {
  std::string v = s;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  DesignFilter f;
  if (v == "part" || v == "design") f = DesignFilter::Part;
  else if (v == "assembly") f = DesignFilter::Assembly;
  else return false;
  if (out) *out = f;
  return true;
}

std::vector<ToolbarSection> buildToolbarSections(const core::ToolRegistry& registry, core::Workspace ws,
                                                 DesignFilter filter, bool sketchPage) {
  std::vector<ToolbarSection> out;
  std::vector<MoreEntry> more;
  const std::vector<ToolbarTab> model = buildToolbarModel(registry, ws);
  const bool design = ws == core::Workspace::Design;
  const std::string pageTab = sketchPage ? "SKETCH" : (design ? "SOLID" : (model.empty() ? "" : model.front().name));

  for (const ToolbarTab& tab : model) {
    const bool contextual = tab.contextual;
    if (sketchPage && contextual && tab.name == "SKETCH") {
      for (const ToolbarGroup& g : tab.groups) {
        if (g.name == "FINISH SKETCH" || g.name == "FINISH") continue;  // the Finish section below
        if (contains(kFoldedGroups, g.name)) {
          addMore(more, g.name, tab.name, g.name, g.all);
          continue;
        }
        ToolbarSection& s = sectionNamed(out, g.name, tab.name);
        s.all = g.all;
        s.flat = g.name == "CONSTRAINTS";
        if (s.flat) s.promoted = g.all;
        else promote(s);
      }
      continue;
    }
    if (contextual) continue;  // other contextual tabs only on their own page
    const bool mainTab = design ? contains(kMainTabs, tab.name) : tab.name == pageTab;
    if (!mainTab) {
      // whole tab into MORE (SURFACE, TOOLS, plug-ins)
      for (const ToolbarGroup& g : tab.groups) addMore(more, tab.name, tab.name, g.name, g.all);
      continue;
    }
    for (const ToolbarGroup& g : tab.groups) {
      if (contains(kFoldedGroups, g.name)) {
        if (!sketchPage) addMore(more, g.name, tab.name, g.name, g.all);
        continue;
      }
      if (sketchPage) {
        // the contextual page keeps the inspection tools of the main page
        if (g.name != "INSPECT") continue;
      }
      // sketch tools of SOLID > CREATE get their own SKETCH section (mockup v6)
      std::vector<core::ITool*> sketchTools, rest;
      for (core::ITool* t : g.all) (design && t->info().id.starts_with("sketch.") ? sketchTools : rest).push_back(t);
      if (!sketchTools.empty() && !sketchPage) {
        ToolbarSection& s = sectionNamed(out, "SKETCH", tab.name);
        s.all.insert(s.all.end(), sketchTools.begin(), sketchTools.end());
      }
      if (rest.empty()) continue;
      ToolbarSection& s = sectionNamed(out, g.name, tab.name);
      s.all.insert(s.all.end(), rest.begin(), rest.end());
    }
  }
  for (ToolbarSection& s : out)
    if (s.promoted.empty()) promote(s);

  // the default page follows the preferred sequence; the sketch page keeps registry order with
  // its own sections first (INSPECT from the main page last)
  if (sketchPage)
    std::stable_partition(out.begin(), out.end(), [](const ToolbarSection& s) { return s.toolbarTab == "SKETCH"; });
  else
    std::stable_sort(out.begin(), out.end(), [](const ToolbarSection& a, const ToolbarSection& b) {
      auto rank = [](const std::string& n) {
        auto it = std::find(kSectionOrder.begin(), kSectionOrder.end(), n);
        return it == kSectionOrder.end() ? static_cast<long>(kSectionOrder.size()) : it - kSectionOrder.begin();
      };
      return rank(a.name) < rank(b.name);
    });

  if (design && !sketchPage && filter == DesignFilter::Assembly) {
    auto ruleOf = [](const std::string& n) -> const FilterRule* {
      for (const FilterRule& r : kAssemblyRules)
        if (n == r.section) return &r;
      return nullptr;
    };
    for (ToolbarSection& s : out)
      if (const FilterRule* r = ruleOf(s.name)) {
        s.startCollapsed = r->collapsed;
        if (r->extraPromoted > 0) {
          std::vector<core::ITool*> p = s.promoted;
          for (core::ITool* t : s.all)
            if (static_cast<int>(p.size()) < r->extraPromoted && std::find(p.begin(), p.end(), t) == p.end()) p.push_back(t);
          s.promoted = std::move(p);
        }
      }
    std::stable_sort(out.begin(), out.end(), [&](const ToolbarSection& a, const ToolbarSection& b) {
      const FilterRule* ra = ruleOf(a.name);
      const FilterRule* rb = ruleOf(b.name);
      return (ra ? ra->order : 100) < (rb ? rb->order : 100);
    });
  }

  if (!more.empty()) {
    ToolbarSection m;
    m.name = "MORE";
    m.toolbarTab = "MORE";
    m.kind = SectionKind::More;
    m.collapsePriority = collapsePriorityFor("MORE");
    m.more = std::move(more);
    for (const MoreEntry& e : m.more)
      for (const auto& [group, tools] : e.groups) m.all.insert(m.all.end(), tools.begin(), tools.end());
    out.push_back(std::move(m));
  }
  if (sketchPage)
    if (core::ITool* fin = registry.tool("sketch.finish")) {
      ToolbarSection f;
      f.name = "FINISH SKETCH";
      f.toolbarTab = "SKETCH";
      f.toolbarGroup = "FINISH SKETCH";
      f.kind = SectionKind::Finish;
      f.all = f.promoted = {fin};
      out.push_back(std::move(f));
    }
  ToolbarSection doc;
  doc.name = "DOCUMENT";
  doc.toolbarTab = "DOCUMENT";
  doc.kind = SectionKind::Document;
  doc.collapsePriority = collapsePriorityFor("DOCUMENT");
  for (const std::string& id : kDocumentTools)
    if (core::ITool* t = registry.tool(id); t && !t->info().hidden && t->info().workspace == ws) doc.all.push_back(t);
  doc.promoted = doc.all;
  if (!doc.all.empty()) out.push_back(std::move(doc));
  return out;
}

std::string shortToolLabel(const core::ToolInfo& info) {
  static const std::map<std::string, std::string> kShort = {
      {"sketch.create", "Sketch"},          {"sketch.edit", "Edit sketch"},
      {"assembly.new-component", "New comp"}, {"assembly.as-built-joint", "As-built"},
      {"assembly.rigid-group", "Rigid grp"}, {"assembly.drive-joint", "Drive"},
      {"assembly.interference", "Interfere"}, {"assembly.move", "Move comp"}, {"assembly.insert-design", "Insert"},
      {"app.parameters", "Params"},          {"app.preferences", "Settings"},
      {"construct.offset-plane", "Plane"},   {"construct.plane-at-angle", "Angle"},
      {"construct.plane-three-points", "3-pt plane"}, {"construct.tangent-plane", "Tangent"},
      {"part.rectangular-pattern", "Pattern"}, {"part.circular-pattern", "Circ pat"},
      {"part.press-pull", "Press pull"},     {"part.import", "Import"},
      {"part.import-file", "Import"},        {"part.export-step", "STEP"},
      {"part.export-stl", "STL"},            {"sketch.rectangle", "Rectangle"},
      {"sketch.rectangle-center", "Rect ctr"}, {"sketch.circle", "Circle"},
      {"sketch.arc-3point", "Arc"},          {"sketch.dimension", "Dimension"},
      {"sketch.polygon", "Polygon"},         {"sketch.slot", "Slot"},
      {"sketch.dimension-edit", "Edit dim"}, {"sketch.move", "Move"},
      {"part.move", "Move"},                 {"drawing.base-view", "Base view"},
      {"drawing.projected-view", "Projected"}, {"drawing.section-view", "Section"},
      {"drawing.detail-view", "Detail"},     {"drawing.new-sheet", "New sheet"},
      {"drawing.export-pdf", "PDF"},         {"drawing.parts-list", "Parts list"},
  };
  if (auto it = kShort.find(info.id); it != kShort.end()) return it->second;
  std::string n = info.name;
  if (n.size() > 3 && n.ends_with("...")) n.resize(n.size() - 3);
  return n;
}

int fitSectionWidth(const FitSection& s, int visibleButtons, bool collapsed, int spacing, int padding) {
  if (collapsed) return s.collapsedWidth + padding;
  int w = 0;
  const int n = std::min<int>(visibleButtons, static_cast<int>(s.buttonWidths.size()));
  for (int i = 0; i < n; ++i) w += s.buttonWidths[static_cast<size_t>(i)] + (i ? spacing : 0);
  return std::max(w, s.captionWidth) + padding;
}

std::vector<FitResult> fitToolbar(const std::vector<FitSection>& sections, int available, int spacing, int padding,
                                  int* usedWidth) {
  const size_t n = sections.size();
  std::vector<FitResult> r(n);
  for (size_t i = 0; i < n; ++i) {
    r[i].collapsed = sections[i].startCollapsed && !sections[i].fixed;
    r[i].visibleButtons = r[i].collapsed ? 0 : static_cast<int>(sections[i].buttonWidths.size());
  }
  auto total = [&] {
    int t = 0;
    for (size_t i = 0; i < n; ++i) t += fitSectionWidth(sections[i], r[i].visibleButtons, r[i].collapsed, spacing, padding);
    return t;
  };
  int used = total();
  // step 1: fold the right-most buttons of the fullest sections into their caption menus
  while (used > available) {
    int pick = -1;
    for (size_t i = 0; i < n; ++i) {
      const FitSection& s = sections[i];
      if (s.fixed || r[i].collapsed || r[i].visibleButtons <= std::max(1, s.minButtons)) continue;
      if (pick < 0 || r[i].visibleButtons >= r[static_cast<size_t>(pick)].visibleButtons) pick = static_cast<int>(i);
    }
    if (pick < 0) break;
    --r[static_cast<size_t>(pick)].visibleButtons;
    used = total();
  }
  // step 2: collapse whole sections to their caption
  while (used > available) {
    int pick = -1;
    for (size_t i = 0; i < n; ++i) {
      if (sections[i].fixed || r[i].collapsed) continue;
      if (pick < 0 || sections[i].collapsePriority >= sections[static_cast<size_t>(pick)].collapsePriority) pick = static_cast<int>(i);
    }
    if (pick < 0) break;
    r[static_cast<size_t>(pick)].collapsed = true;
    r[static_cast<size_t>(pick)].visibleButtons = 0;
    used = total();
  }
  if (usedWidth) *usedWidth = used;
  return r;
}

}  // namespace fabricad::app
