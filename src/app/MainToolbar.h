// FabriCAD app: the main toolbar of the redesign (docs/plans/ui-redesign.md 5.1, 6.2, 8.2;
// replaces the tabbed Ribbon). One row of labelled sections divided by straight 1 px lines:
//
//   SKETCH | CREATE | MODIFY | ASSEMBLE | CONSTRUCT | INSPECT | MORE v            | DOCUMENT
//   [Sketch][Extrude][Revolve]...                       [Surface][Tools]          [Params][Settings]
//   ------- caption dropdowns (every tool of the section) ---------------------------------------
//
// Vertical layout (ui/toolbarArea left or right, 5.1 "Vertical toolbar", 6.2): a 64 px column;
// each section is a caption bar (16 h, straight rule above) over one column of icon buttons
// (40 h, the label below at 9 px when it fits, else in the tooltip). When the column is shorter
// than its content the bottom buttons fold into the caption dropdowns, then whole sections
// collapse to their caption. DOCUMENT (and FINISH SKETCH) sit at the bottom.
//
// The structure comes from app::buildToolbarSections() (unit-tested): the default page per
// workspace, the contextual SKETCH page while a sketch is edited (decision 5, with FINISH
// SKETCH) and the Design filter Part / Assembly (decision 2). When the row is narrower than
// its content, app::fitToolbar() folds the right-most buttons into the caption dropdowns and
// then collapses whole sections to their caption; there is no scrolling and no cut label.
//
// Test and tutorial hooks (section 8.2): #RibbonRow (this widget), #RibbonGroup (sections),
// #RibbonGroupLabel (captions), #RibbonTool (buttons), #FinishSketchButton; dynamic properties
// `toolId` on every tool button and dropdown action, `toolbarTab` / `toolbarGroup` on every
// section and caption (MORE's buttons carry the registry tab they list).
//
// The old Ribbon API (tabNames, currentTab, isTabVisible, setContextualTab) is kept: "tabs"
// are now pages, and the contextual SKETCH tab is the sketch page.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/ToolbarModel.h"
#include "core/Tool.h"

#include <QWidget>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class QHBoxLayout;
class QMenu;
class QStackedWidget;
class QToolButton;

namespace fabricad::app {

class MainToolbar : public QWidget {
  Q_OBJECT
 public:
  explicit MainToolbar(core::ToolManager& manager, QWidget* parent = nullptr);
  ~MainToolbar() override;
  /// Rebind to another tab's tool manager (multi-document tabs).
  void setToolManager(core::ToolManager* m) { manager_ = m; }
  core::ToolManager& manager() const { return *manager_; }

  /// Rebuild for a workspace (after registration or a workspace switch).
  void rebuild(core::Workspace ws);
  core::Workspace workspace() const { return ws_; }
  /// Toolbar filter of the Design workspace (Part / Assembly).
  void setDesignFilter(DesignFilter f);
  DesignFilter designFilter() const { return filter_; }

  // --- page API (formerly ribbon tabs) ----------------------------------------------------
  /// Show/hide a contextual page (SKETCH while a sketch is edited).
  void setContextualTab(const std::string& name, bool visible);
  bool isTabVisible(const std::string& name) const;
  /// "SKETCH" on the sketch page, else the default page's registry tab ("SOLID", "DRAWING").
  std::string currentTab() const;
  void setCurrentTab(const std::string& name);
  /// Registry tabs of the workspace (pages and the tabs folded into sections and MORE).
  std::vector<std::string> tabNames() const;

  /// Update enabled/checked states (selection changed, tool started/finished, ...).
  void refreshStates();
  /// Number of tool buttons + menu actions of the current page (tests).
  int toolActionCount() const;

  // --- layout (tests, tutorial) -----------------------------------------------------------
  /// Sections of the current page, left to right (#RibbonGroup widgets).
  std::vector<QWidget*> sectionWidgets() const;
  /// Collapsed to caption only (by the filter or because the row is too narrow).
  bool isSectionCollapsed(const std::string& name) const;
  /// Visible tool buttons of a section (the others are in its caption dropdown).
  int visibleButtonCount(const std::string& name) const;
  /// Width the current page needs with every section expanded (vertical: the height).
  int naturalWidth() const;

  /// Horizontal (top) or vertical (left / right tool bar area). Rebuilds the pages; tool ids,
  /// enabled and checked states are re-applied, so nothing else changes.
  void setOrientation(Qt::Orientation o);
  Qt::Orientation orientation() const { return vertical_ ? Qt::Vertical : Qt::Horizontal; }
  static constexpr int kVerticalWidth = 64;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void toolTriggered(const std::string& id);

 protected:
  void resizeEvent(QResizeEvent* e) override;
  bool event(QEvent* e) override;

 private:
  struct Section;
  struct Page;
  Page* currentPage() const;
  Page* buildPage(bool sketchPage);
  Page* buildVerticalPage(bool sketchPage);
  void addCaptionMenu(Page& p, Section& s, QToolButton* caption);
  void fitVertical();
  void clearPages();
  void showPage(bool sketchPage);
  void fit();
  void scheduleFit();
  QToolButton* toolButton(core::ITool* tool, QWidget* parent, bool small);
  void addAction(Page& page, const std::string& id, QObject* obj);

  core::ToolManager* manager_;
  core::Workspace ws_ = core::Workspace::Design;
  DesignFilter filter_ = DesignFilter::Part;
  QStackedWidget* stack_ = nullptr;
  std::unique_ptr<Page> defaultPage_;
  std::unique_ptr<Page> sketchPage_;
  bool sketching_ = false;
  std::set<std::string> contextualShown_;
  bool fitPending_ = false;
  bool vertical_ = false;
};

}  // namespace fabricad::app
