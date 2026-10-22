// FabriCAD app: the sidebar's command-card slot (docs/plans/ui-redesign.md 6.2, 6.4, decision 6).
//
// CardSlotHost is the minimal interface the command card needs from the sidebar when
// ui/cardPlacement = sidebar. Phase 2's SidebarHost (branch ui/sidebar) owns the real slot (the
// top pane of its splitter, objectName "CardSlot") and should implement this interface; until it
// lands, SimpleCardSlot is a stand-in placed above the browser stack in #BrowserDock.
//
// MERGE NOTE (ui/card x ui/sidebar): make SidebarHost implement CardSlotHost (or wrap its card
// slot in an adapter) and hand it to MainWindow::setCardSlotHost(); then delete SimpleCardSlot
// and the wrapper in MainWindow::buildUi(). The calls below are all the card uses.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QPointer>
#include <QRect>
#include <QWidget>

namespace fabricad::app {

class CardSlotHost {
 public:
  virtual ~CardSlotHost() = default;
  /// The slot can take a card now (the sidebar is shown). The card falls back to floating
  /// (6.4) when it cannot, without changing the setting.
  virtual bool cardSlotAvailable() const = 0;
  /// Reparent `card` into the slot and show the slot (nullptr: empty the slot and hide it;
  /// the card is not deleted). FEATURES collapses to its header plus the live row while a card
  /// is docked and restores when it is emptied (decision 6).
  virtual void dockCard(QWidget* card) = 0;
  virtual QWidget* dockedCard() const = 0;
  /// Width the docked card gets (the slot's contents width).
  virtual int cardSlotWidth() const = 0;
  /// Height the docked card may take before its body scrolls.
  virtual int cardSlotMaxHeight() const = 0;
  /// Global screen rect of the slot (the drop target while a card is dragged); empty when unavailable.
  virtual QRect cardSlotGlobalRect() const = 0;
  /// Decision 6: FEATURES expands while the docked command's feature-selection input is active
  /// (Mirror, patterns) and collapses again afterwards.
  virtual void setFeatureSelectionActive(bool active) = 0;
};

/// Stand-in slot until SidebarHost exists: a plain container at the top of the sidebar.
class SimpleCardSlot : public QWidget, public CardSlotHost {
  Q_OBJECT
 public:
  explicit SimpleCardSlot(QWidget* parent = nullptr);

  bool cardSlotAvailable() const override;
  void dockCard(QWidget* card) override;
  QWidget* dockedCard() const override { return card_; }
  int cardSlotWidth() const override;
  int cardSlotMaxHeight() const override;
  QRect cardSlotGlobalRect() const override;
  void setFeatureSelectionActive(bool active) override { featureSelection_ = active; }
  bool featureSelectionActive() const { return featureSelection_; }

  /// The panel below the slot (the browser stack) whose height the card shares.
  void setBelow(QWidget* w) { below_ = w; }

 private:
  QPointer<QWidget> card_;
  QPointer<QWidget> below_;
  bool featureSelection_ = false;
};

}  // namespace fabricad::app
