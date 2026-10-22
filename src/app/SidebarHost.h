// FabriCAD app: the redesigned sidebar (docs/plans/ui-redesign.md 5.2, 6.2, 6.4, 6.5, phase 2).
//
//   #BrowserDock (QDockWidget, no title bar, left or right: ui/sidebarSide, live)
//    └ SidebarHost (#SidebarHost)
//       └ QSplitter #SidebarSplitter (vertical)
//          [0] #CardSlot          the command card when ui/cardPlacement = sidebar (hidden otherwise)
//          [1] FEATURES panel     PanelHeader + QStackedWidget (one FeatureListWidget per design tab)
//          [2] MODEL panel        PanelHeader + QStackedWidget (one BrowserTree #Browser per tab)
//          [3] SELECTION panel    PanelHeader + SelectionPanel (follows the active document)
//
// The panel splitter sizes persist in ui/sidebarSplit ("features,model,selection" in px) and the
// collapse state in ui/panels ("features:1,model:1,selection:0", 1 = open). In the `strip`
// timeline style (ui/timelineStyle) FEATURES is hidden and the bottom strip shows instead.
//
// ---- Command card API (phase 3, agreed with the card agent; docs/uiux/redesign/p2.md) ---------
// The card docks as an *overlay of FEATURES* (decision 6): dockCard() puts it in #CardSlot at the
// top of the splitter and folds FEATURES to its header plus the live row (the feature being
// created or edited, see FeatureListWidget::setPendingLive / setLiveFeature). releaseCard()
// restores FEATURES and the splitter sizes the user had. While a feature-selection input is
// active (Mirror, patterns: a selection filter that contains ShapeKind::Feature, detected from
// the active document's SelectionModel, or forced with setFeatureSelectionActive) FEATURES
// expands again, also when the user collapsed it, and folds back afterwards.
// canDockCard() is false when the sidebar is hidden (ShowBrowserAction off) or a drawing tab is
// active; the card then falls back to floating (6.4) without touching the setting.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QPointer>
#include <QWidget>

#include <array>
#include <vector>

class QSplitter;
class QStackedWidget;
class QToolButton;
class QLineEdit;

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

class BrowserTree;
class FeatureListWidget;
class SelectionPanel;
class SidebarPanel;

class SidebarHost : public QWidget {
  Q_OBJECT
 public:
  enum class Panel { Features = 0, Model = 1, Selection = 2 };
  enum class TimelineStyle { FeatureList, Strip };
  static constexpr int kDefaultWidth = 320;  ///< size token width.sidebar
  static constexpr int kMinWidth = 260;      ///< width.sidebarMin

  explicit SidebarHost(QWidget* parent = nullptr);
  ~SidebarHost() override;

  // --- structure ------------------------------------------------------------------------------
  SidebarPanel* panel(Panel p) const { return panels_[static_cast<size_t>(p)]; }
  QStackedWidget* featureStack() const { return featureStack_; }
  QStackedWidget* modelStack() const { return modelStack_; }
  SelectionPanel* selectionPanel() const { return selection_; }
  QSplitter* splitter() const { return splitter_; }
  QLineEdit* featureSearch() const { return search_; }

  /// The active tab's widgets (null for a drawing tab): the stacks switch, the SELECTION panel
  /// and the feature-selection watch follow the document.
  void setActive(core::Document* doc, FeatureListWidget* features, BrowserTree* model);
  FeatureListWidget* featureList() const { return features_; }
  BrowserTree* modelTree() const { return model_; }

  // --- timeline style (ui/timelineStyle) ------------------------------------------------------
  void setTimelineStyle(TimelineStyle s);
  TimelineStyle timelineStyle() const { return style_; }

  // --- collapse and sizes (ui/panels, ui/sidebarSplit) ----------------------------------------
  void setPanelCollapsed(Panel p, bool collapsed);
  bool isPanelCollapsed(Panel p) const;
  /// Heights of the three panels (FEATURES, MODEL, SELECTION) as laid out now.
  std::array<int, 3> panelSizes() const;
  void setPanelSizes(const std::array<int, 3>& sizes);
  void saveState() const;
  void restoreState();

  // --- command card API (phase 3) ---------------------------------------------------------------
  /// The slot widget a docked card is parented to (objectName CardSlot).
  QWidget* cardSlot() const { return cardSlot_; }
  /// False while the sidebar is hidden or a drawing tab is active (the card floats then).
  bool canDockCard() const;
  /// MainWindow: the sidebar is shown and the tab can host the card.
  void setCardDockAllowed(bool allowed);
  /// Dock `card` (reparented into the slot, shown) and fold FEATURES to header + live row.
  void dockCard(QWidget* card);
  /// Undock: the card is hidden and handed back to `newParent` (may be null); FEATURES and the
  /// splitter sizes are restored. Safe to call when nothing is docked.
  void releaseCard(QWidget* newParent = nullptr);
  QWidget* dockedCard() const { return card_; }
  /// Force the feature-selection state (the card knows when its input takes features). The
  /// sidebar also detects it from the SelectionModel filter; either one expands FEATURES.
  void setFeatureSelectionActive(bool on);
  /// FEATURES is expanded now because a feature-selection input is active.
  bool featureSelectionActive() const;
  /// Highlight the slot as a drop target while the card is dragged (dashed accent outline).
  void setDropHighlight(bool on);
  bool dropHighlight() const { return dropHighlight_; }
  /// Rect of the slot drop target in global coordinates (the FEATURES area when empty).
  QRect cardDropRect() const;
  /// Height a docked card may take: the sidebar minus what stays visible beside it (the
  /// FEATURES header and live row, the MODEL and SELECTION headers). The card floats when its
  /// minimum does not fit (p2p3-check 2).
  int cardSlotMaxHeight() const;

 signals:
  void cardDockAvailabilityChanged(bool available);
  /// The sidebar's height changed (a docked card re-checks whether it still fits).
  void heightChanged(int height);
  void featureSelectionChanged(bool active);
  void panelCollapsedChanged(fabricad::app::SidebarHost::Panel panel, bool collapsed);
  /// The MODEL header's master eye was clicked (MainWindow shows or hides every body).
  void masterEyeRequested();

 protected:
  void paintEvent(QPaintEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;

 private:
  void applyLayout();
  void onSplitterMoved();
  void updateCounts();
  void updateFeatureSelection();
  bool temporarilyExpanded() const;

  std::array<SidebarPanel*, 3> panels_{};
  QSplitter* splitter_ = nullptr;
  QWidget* cardSlot_ = nullptr;
  QStackedWidget* featureStack_ = nullptr;
  QStackedWidget* modelStack_ = nullptr;
  SelectionPanel* selection_ = nullptr;
  QLineEdit* search_ = nullptr;
  QToolButton* searchButton_ = nullptr;
  QToolButton* filterButton_ = nullptr;
  QToolButton* eyeButton_ = nullptr;
  QWidget* dropOverlay_ = nullptr;
  bool hideSuppressed_ = false;
  bool problemsOnly_ = false;

  QPointer<core::Document> doc_;
  QPointer<FeatureListWidget> features_;
  QPointer<BrowserTree> model_;
  std::vector<QMetaObject::Connection> docConns_;
  QPointer<QWidget> card_;
  TimelineStyle style_ = TimelineStyle::FeatureList;
  std::array<bool, 3> collapsed_{false, false, false};  ///< the user's choice (persisted)
  std::array<int, 3> sizes_{460, 300, 170};             ///< last user sizes of the panels
  bool cardAllowed_ = true;
  bool forcedFeatureSelection_ = false;
  bool filterFeatureSelection_ = false;
  bool dropHighlight_ = false;
  bool applying_ = false;
};

}  // namespace fabricad::app
