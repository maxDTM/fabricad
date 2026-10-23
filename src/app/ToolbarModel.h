// FabriCAD app: toolbar structure built from the ToolRegistry. Widget-free so the ordering,
// promotion, section and collapse rules are unit-tested (tests/test_app_logic.cpp).
//
// Two levels:
//   buildToolbarModel()    registry tabs and groups (SOLID > CREATE ...), as registered;
//   buildToolbarSections() the redesign's single-row MainToolbar (ui-redesign.md 5.1, 6.2, 8.2):
//                          SKETCH CREATE MODIFY ASSEMBLE CONSTRUCT INSPECT  MORE v  ...  DOCUMENT
//                          per page (the default page, or the contextual SKETCH page) and per
//                          design filter (Part / Assembly, decision 2). fitToolbar() decides
//                          which buttons fold into the caption dropdowns and which sections
//                          collapse to their caption when the row is too narrow.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <map>
#include <string>
#include <utility>
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

// ---- MainToolbar sections ----------------------------------------------------------------

/// Toolbar filter of the Design workspace (decision 2: Assembly is not a core workspace).
enum class DesignFilter { Part, Assembly };
const char* designFilterName(DesignFilter f);  ///< "part" / "assembly"
bool parseDesignFilter(const std::string& s, DesignFilter* out);

enum class SectionKind {
  Tools,     ///< promoted tools as labelled buttons, caption dropdown lists all
  More,      ///< MORE v: one dropdown button per folded registry tab/group (SURFACE, TOOLS, ...)
  Finish,    ///< FINISH SKETCH (contextual page only)
  Document,  ///< right-aligned DOCUMENT section (parameters, settings)
};

/// One dropdown button of the MORE section ("TOOLS" lists TOOLS > EXPORT, UTILITIES).
struct MoreEntry {
  std::string label;    ///< "TOOLS"
  std::string tab;      ///< registry tab ("TOOLS", or the page tab for a folded group)
  std::vector<std::pair<std::string, std::vector<core::ITool*>>> groups;  ///< (group, tools)
};

struct ToolbarSection {
  std::string name;          ///< caption: "CREATE", "MORE", "DOCUMENT"
  std::string toolbarTab;    ///< dynamic property toolbarTab (tutorial anchors): "SOLID", "SKETCH", "MORE"
  std::string toolbarGroup;  ///< dynamic property toolbarGroup: "CREATE" (empty for MORE/DOCUMENT)
  SectionKind kind = SectionKind::Tools;
  bool flat = false;            ///< small icon grid without labels (CONSTRAINTS)
  bool startCollapsed = false;  ///< filter: caption only from the start (Assembly hides modelling)
  int collapsePriority = 0;     ///< higher collapses first when the row is too narrow
  std::vector<core::ITool*> promoted;  ///< buttons, in order (right-most fold first)
  std::vector<core::ITool*> all;       ///< caption dropdown content
  std::vector<MoreEntry> more;         ///< SectionKind::More only
  /// Split buttons (mockup v6 arrows): a promoted tool with variants gets a menu arrow listing
  /// them (Pattern: rectangular, circular; Plane: offset, angle, midplane ...). The variants
  /// are not promoted buttons of their own; the caption dropdown still lists every tool.
  std::map<core::ITool*, std::vector<core::ITool*>> variants;
};

/// Split-button families: the primary tool id and its variants, in menu order (the primary
/// first). A family applies when its primary tool is promoted in a section.
const std::vector<std::pair<std::string, std::vector<std::string>>>& toolVariantFamilies();

/// The toolbar page: the default page (SOLID in Design, DRAWING in Drawing) or the contextual
/// SKETCH page while a sketch is edited (decision 5).
std::vector<ToolbarSection> buildToolbarSections(const core::ToolRegistry& registry, core::Workspace ws,
                                                 DesignFilter filter = DesignFilter::Part, bool sketchPage = false);

/// Short toolbar label (section 3.4 width budget): "New Component" -> "New comp",
/// "Create Sketch" -> "Sketch". ToolInfo::name is unchanged; tooltips keep the full name.
std::string shortToolLabel(const core::ToolInfo& info);

/// Collapse input per section: widths in px of its buttons (left to right), its caption, and
/// the width of its caption-only form.
struct FitSection {
  std::vector<int> buttonWidths;
  int captionWidth = 0;
  int collapsedWidth = 0;
  int minButtons = 1;        ///< buttons kept before the section collapses (flat grids: all)
  int collapsePriority = 0;  ///< higher collapses first
  bool startCollapsed = false;
  bool fixed = false;        ///< never folds or collapses (FINISH SKETCH)
};
struct FitResult {
  int visibleButtons = 0;  ///< the first N buttons stay; the rest are in the caption dropdown
  bool collapsed = false;  ///< caption only
};
/// Width of a section in a given state (buttons + spacing, at least the caption, plus padding).
int fitSectionWidth(const FitSection& s, int visibleButtons, bool collapsed, int spacing, int padding);
/// Fold buttons, then collapse sections, until the row fits `available` px (never scrolls,
/// never cuts a label). Step 1 removes the right-most button of the section with the most
/// visible buttons (ties: the right-most section), down to minButtons. Step 2 collapses whole
/// sections by collapsePriority (ties: right-most first).
std::vector<FitResult> fitToolbar(const std::vector<FitSection>& sections, int available, int spacing, int padding,
                                  int* usedWidth = nullptr);

}  // namespace fabricad::app
