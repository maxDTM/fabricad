// FabriCAD viewer: overlay layout inside the viewport (docs/plans/ui-redesign.md 6.3, 6.4).
//
// Pure geometry, no widgets: it takes the viewport rect and the sizes of the visible overlays and
// returns their rects in priority order:
//
//   1 orientation widget (top-right; ViewCube rect or the hex dial)   fixed or sized
//   2 view-tool column (right edge, below the orientation widget)
//   3 breadcrumb + selection-filter bar (top-left row)
//   4 command card (by placement: floatRight | floatLeft | free; sidebar = not in the viewport)
//   5 sketch palette (below the card on a shared side, else the free side)
//   6 quick toolbar (between the card and the viewport edge on its side)
//   7 coordinate readout (bottom-left), scale bar (bottom-right), nav bar (fixed)
//
// After compute(), occupied() lists every visible region, so other overlays, toasts and the
// tutorial bubble can keep clear of them (findFree()), and insets() is the union of the occupied
// edges for ViewerWidget::setViewportInsets (Fit and Home keep the model clear).
//
// The card is passed as its widget size, which includes the transparent frame margin around the
// visible card (CardFrame brackets, `cardBleed`): margins below are measured to the visible card.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace fabricad::viewer {

/// ui/cardPlacement (6.1 #1).
enum class CardPlacement : std::uint8_t { Sidebar, FloatRight, FloatLeft, Free };
const char* placementName(CardPlacement p);  ///< "sidebar", "floatRight", "floatLeft", "free"
std::optional<CardPlacement> parsePlacement(const QString& s);

/// ui/cardFreePos (6.1 #1b): where a dragged card was dropped, kept relative to the viewport so it
/// survives resizes. `base` is the placement a double-click on the header returns to.
/// Serialised as "floatRight;tr;0.1200;0.0800" (base, anchor corner, offset x and y as fractions
/// of the viewport width and height, measured from that corner to the same card corner).
struct FreePos {
  CardPlacement base = CardPlacement::FloatRight;
  enum Corner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight } anchor = TopRight;
  double fx = 0.0, fy = 0.0;
  bool valid = false;

  QString toString() const;
  static FreePos fromString(const QString& s);  ///< invalid on garbage
  /// Anchor = the viewport corner nearest the card centre.
  static FreePos fromRect(const QRect& card, const QRect& viewport, CardPlacement base);
  /// The card rect for `size` in `viewport`, clamped inside it.
  QRect toRect(const QSize& size, const QRect& viewport) const;
};

class OverlayLayout {
 public:
  enum class Slot : std::uint8_t {
    Orientation, ViewTools, TopBar, Card, SketchPalette, QuickToolbar, Readout, ScaleBar, NavBar, Other
  };
  static const char* slotName(Slot s);

  struct Item {
    Slot slot = Slot::Other;
    QSize size;        ///< wanted size (the layout places it)
    QRect fixed;       ///< non-empty: already placed by its owner (ViewCube, nav bar); only reported
    QString id;        ///< free text for Slot::Other (the tutorial bubble, toasts)
    bool right = true; ///< quick toolbar / palette side preference
    bool userMoved = false;  ///< palette dragged by the user: `fixed` is its rect, clamped
  };

  struct Options {
    int margin = 16;        ///< viewport edge to the visible overlay
    int gap = 8;            ///< between overlays
    int cardBleed = 8;      ///< transparent frame margin around the visible card (brackets)
    int cardTopOffset = 60; ///< floatRight: below the orientation widget's top edge (mockup v6)
    int cardOrientGap = 56; ///< floatRight: visible card edge to the orientation widget (mockup v6: 1702 - 1646)
    int minModelWidth = 320;///< keep this much viewport clear beside a card left of the dial
    int minCardHeight = 220;///< below this the card would rather move than scroll
    CardPlacement card = CardPlacement::FloatRight;
    FreePos free;           ///< used when card == Free
  };

  struct Region {
    Slot slot;
    QString id;
    QRect rect;
  };

  void setViewport(const QRect& r) { viewport_ = r; }
  const QRect& viewport() const { return viewport_; }
  void setOptions(const Options& o) { opt_ = o; }
  const Options& options() const { return opt_; }
  void add(const Item& item) { items_.push_back(item); }
  void clear() { items_.clear(); regions_.clear(); }

  /// Place every item; afterwards rect(), occupied(), insets() and cardMaxHeight() are valid.
  void compute();

  QRect rect(Slot s, const QString& id = {}) const;
  const std::vector<Region>& occupied() const { return regions_; }
  /// Union of the occupied edges (6.3): what Fit and Home should keep clear.
  QMargins insets() const;
  /// The tallest the card may be at its rect's top before it must scroll (whole widget height).
  int cardMaxHeight() const { return cardMaxH_; }
  /// The card's widget rect for a placement (the drop targets while dragging; Free uses options().free).
  QRect cardRectFor(CardPlacement p, const QSize& size) const;
  /// True when `r` overlaps no occupied region except those of slot `ignore`.
  bool isFree(const QRect& r, std::optional<Slot> ignore = std::nullopt) const;
  /// A rect of `size` inside the viewport that overlaps nothing, as close to `near` as possible
  /// (tutorial bubble, toasts). Empty when there is no such place.
  QRect findFree(const QSize& size, const QPoint& near) const;

 private:
  QRect inner() const;  ///< viewport minus margins
  QRect place(const Item& it) const;
  QRect placeCard(CardPlacement p, const QSize& size, int* maxHeight) const;
  QRect placePalette(const Item& it) const;
  int bottomReserved(int left, int right) const;  ///< y of the highest bottom overlay in [left,right]

  QRect viewport_;
  Options opt_;
  std::vector<Item> items_;
  std::vector<Region> regions_;
  int cardMaxH_ = 0;
};

}  // namespace fabricad::viewer
