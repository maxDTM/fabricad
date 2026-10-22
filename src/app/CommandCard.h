// FabriCAD app: the command card that hosts ITool::createDialog() bodies (docs/plans/ui-redesign.md
// 5.3, 5.4, 6.3, 6.4, 7.1; it was CommandDialog, ux.md 6.2).
//
//    ⌐                                      ¬
//     +------------------------------------+   CardFrame: brackets, gold hairline while live
//     | [ico] EXTRUDE [E] PART · Bracket ⊥ x|   header 40 (drag to move; double-click: back)
//     | 01 PROFILE   02 DISTANCE  03 CONFIRM|   StepIndicator (derived from the body)
//     | <tool body: label-left rows,        |   enums <= 4 values -> SegmentedControl rows
//     |  segmented rows label-above>        |   with the label above (5.4)
//     | message (prompt / error)            |
//     | [CANCEL ESC]   [ ✓ APPLY     ENTER ]|   footer 52 (OkButton kept)
//     +------------------------------------+
//    L                                      ┘
//
// Placements (ui/cardPlacement, 6.1): floating right (default) or left in the viewport, docked
// into the sidebar's card slot (CardSlotHost), or `free` where the user dropped it (ui/cardFreePos,
// relative to the viewport, so it survives resizes). Dragging the header shows the drop targets
// (sidebar slot, left and right float slots, dashed accent outlines); dropping elsewhere gives
// `free`. A sidebar placement falls back to floating while the slot is unavailable (6.4). The
// floating rect comes from OverlayLayout (viewer/OverlayLayout.h), which keeps clear of the
// ViewCube / orientation widget and the bottom overlays; the result is published on the
// viewport's OverlayHost.
//
// Its size is stable while a command runs: it may grow (a longer message, more rows) but never
// shrinks until the next present(). Tall bodies scroll. Enter -> accepted(), Esc -> cancelled()
// (Enter is ignored inside multi-line editors and while Apply is disabled).
// objectNames kept for tests and the tutorial (8.2): CommandDialog, CommandDialogTitle,
// CommandDialogMessage, CommandDialogScroll, OkButton; new: StepIndicator, CardPin, CardClose,
// CancelButton, CardDropTargets.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "viewer/OverlayLayout.h"

#include <QFrame>
#include <QIcon>
#include <QPointer>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace fabricad::app {

class CardSlotHost;
class StepIndicator;

class CommandCard : public QFrame {
  Q_OBJECT
 public:
  using Placement = viewer::CardPlacement;
  static constexpr int kCardWidth = 360;  ///< visible card width when floating (5.3)

  explicit CommandCard(QWidget* viewport);
  ~CommandCard() override;

  /// Show `body` (ownership taken; may be null for "title only") for a tool.
  void present(const QString& title, const QIcon& icon, QWidget* body, bool buttons);
  /// Remove and delete the current body; hides the card (and empties the sidebar slot).
  void dismiss();
  bool isPresenting() const { return isVisible() || body_; }
  QWidget* body() const { return body_; }
  void setAcceptEnabled(bool enabled);
  bool isAcceptEnabled() const { return acceptEnabled_; }
  void setMessage(const QString& text, bool error = false);
  /// Keep the card inside its area after a resize (refit + place).
  void reposition();
  QPushButton* okButton() const { return ok_; }
  QPushButton* cancelButton() const { return cancel_; }

  // ---- header (5.4)
  /// Tool shortcut for the keycap ("E"); empty hides it.
  void setShortcut(const QString& keys);
  /// Context after the title: workspace (caps) and document name (as typed).
  void setContext(const QString& workspace, const QString& documentName);
  QString contextText() const;
  /// Gold hairline: a live operation is in progress (7.1).
  void setLive(bool live);
  bool isLive() const { return live_; }
  /// Pin: keep the card open for repeats (the host re-runs the command after Apply).
  bool isPinned() const;
  void setPinned(bool pinned);
  StepIndicator* stepIndicator() const { return steps_; }
  /// Re-derive the steps and re-read segmented controls after the tool changed its body's state.
  void syncControls();

  // ---- placement (6.1, 6.4)
  void setPlacement(Placement requested, const viewer::FreePos& free = {});
  Placement requestedPlacement() const { return requested_; }
  /// What the card does now: the requested placement, or a float when the slot is unavailable.
  Placement effectivePlacement() const;
  const viewer::FreePos& freePos() const { return free_; }
  void setSlotHost(CardSlotHost* host);
  CardSlotHost* slotHost() const { return slotHost_; }
  /// The card may use the sidebar slot (active tab of the design workspace).
  void setSlotAllowed(bool allowed);
  bool isDocked() const;
  /// Placement for a sidebar fallback (6.4): floatLeft when the sidebar is on the left.
  void setSidebarOnLeft(bool left);
  /// Extra fixed regions for the overlay layout (nav bar, quick toolbar...), added before placing.
  void setOverlayRegions(std::function<void(viewer::OverlayLayout&)> regions) { regions_ = std::move(regions); }
  /// The layout the last floating placement used (empty viewport when docked).
  const viewer::OverlayLayout& overlayLayout() const { return layout_; }
  /// Drop target of a drag in global coordinates (tests, the drop overlay); empty when unavailable.
  QRect dropTargetGlobal(Placement p) const;
  QWidget* viewportWidget() const { return viewport_; }
  /// Visible card rect in the card's own coordinates (without the bracket margin).
  QRect cardRect() const;
  bool isDragging() const { return dragging_; }

 signals:
  void accepted();
  void cancelled();
  void pinnedChanged(bool pinned);
  /// The user dropped the card (drag) or double-clicked its header: the host stores the
  /// placement (ui/cardPlacement) and, for `free`, the position (ui/cardFreePos).
  void placementChosen(viewer::CardPlacement placement, const viewer::FreePos& free);
  /// Placed or resized (the host lays out the overlays that depend on the card).
  void placed();

 protected:
  void keyPressEvent(QKeyEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;
  void paintEvent(QPaintEvent* e) override;

 private:
  /// Fit the scroll area to the body (capped), apply the stable height and place the card.
  void relayout();
  void applyDocking();
  void clearBody();
  void adoptBody(QWidget* body);
  void upgradeForms();
  void updateSteps();
  void focusStep(int index);
  int chromeHeight() const;
  QRect viewportRect() const;
  void buildLayout(viewer::OverlayLayout& l, const QSize& size, Placement p) const;
  void publishLayout();
  // drag (6.4)
  void beginDrag();
  void dragTo(const QPoint& global);
  void endDrag(const QPoint& global);
  Placement dropTargetAt(const QPoint& global, bool* hit) const;

  QWidget* viewport_;
  QWidget* title_ = nullptr;
  QLabel* titleIcon_ = nullptr;
  QLabel* titleText_ = nullptr;
  QLabel* keycap_ = nullptr;
  QLabel* context_ = nullptr;
  QLabel* docName_ = nullptr;
  QToolButton* pin_ = nullptr;
  QToolButton* close_ = nullptr;
  StepIndicator* steps_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  QWidget* inner_ = nullptr;  ///< scroll area content; holds bodyLayout_
  QVBoxLayout* bodyLayout_ = nullptr;
  QPointer<QWidget> body_;
  QLabel* message_ = nullptr;
  QWidget* buttonRow_ = nullptr;
  QPushButton* ok_ = nullptr;
  QPushButton* cancel_ = nullptr;
  bool acceptEnabled_ = true;
  bool live_ = false;
  bool relayoutPending_ = false;
  int stableHeight_ = 0;  ///< height since present(); only grows
  std::vector<QPointer<QWidget>> stepTargets_;  ///< input focused by clicking a done step

  Placement requested_ = Placement::FloatRight;
  viewer::FreePos free_;
  CardSlotHost* slotHost_ = nullptr;
  bool slotAllowed_ = true;
  bool sidebarLeft_ = true;
  std::function<void(viewer::OverlayLayout&)> regions_;
  viewer::OverlayLayout layout_;

  // drag state
  bool pressed_ = false;
  bool dragging_ = false;
  QPoint pressGlobal_;
  QPoint grabOffset_;  ///< cursor - card top-left (global)
  QPointer<QWidget> dropOverlay_;
};

}  // namespace fabricad::app
