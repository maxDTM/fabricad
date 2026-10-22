// FabriCAD app: the sidebar's command-card slot (docs/plans/ui-redesign.md 6.2, 6.4, decision 6).
//
// CardSlotHost is the minimal interface the command card needs from the sidebar when
// ui/cardPlacement = sidebar. Phase 2's SidebarHost owns the real slot (the top pane of its
// splitter, objectName "CardSlot"); SidebarCardSlotAdapter<SidebarHost> maps this interface onto
// it (MainWindow::buildUi). The calls below are all the card uses.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QRect>
#include <QWidget>

#include <algorithm>

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
  /// The slot is the drop target under the cursor while the card is dragged (optional outline;
  /// the card paints its own drop-target overlay as well).
  virtual void setDropHighlight(bool /*on*/) {}
};

/// Adapter for SidebarHost (docs/uiux/redesign/p2.md "Command card interface"). A template so
/// the card code does not depend on the sidebar's header.
template <class Host>
class SidebarCardSlotAdapter : public CardSlotHost {
 public:
  explicit SidebarCardSlotAdapter(Host* host) : host_(host) {}
  bool cardSlotAvailable() const override { return host_ && host_->canDockCard(); }
  void dockCard(QWidget* card) override {
    if (card == host_->dockedCard()) {
      if (card) card->show();
    } else if (card) {
      host_->dockCard(card);
    } else {
      host_->releaseCard(nullptr);  // the card reparents itself to its viewport
    }
  }
  QWidget* dockedCard() const override { return host_ ? host_->dockedCard() : nullptr; }
  int cardSlotWidth() const override { return host_->cardSlot()->width() > 0 ? host_->cardSlot()->width() : host_->width(); }
  int cardSlotMaxHeight() const override { return std::max(200, host_->height() - 120); }  // FEATURES header + live row stay
  QRect cardSlotGlobalRect() const override { return cardSlotAvailable() ? host_->cardDropRect() : QRect(); }
  void setFeatureSelectionActive(bool active) override { host_->setFeatureSelectionActive(active); }
  void setDropHighlight(bool on) override { host_->setDropHighlight(on); }

 private:
  Host* host_;
};

}  // namespace fabricad::app
