// FabriCAD app: the command card's frame (docs/plans/ui-redesign.md 7.1).
//
//   ⌐                    ¬      4 L-shaped brackets: 10 px arms, 1.5 px accent.indicator,
//    +------------------+       6 px outside the card rect (the card widget keeps an 8 px
//    |==== gold hairline|       transparent margin, kBleed, for them and the shadow).
//    |                  |       A 1 px live.base hairline (2 px in light, see live.base) runs
//    +------------------+       along the top edge while a live operation is in progress.
//   L                    ┘      Only the active card is framed; plain dialogs get no gold.
//
// Painted from the tokens at paint time (no cached colours), so a theme switch only needs update().
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QRect>

class QPainter;

namespace fabricad::app::CardFrame {

constexpr int kBleed = 8;      ///< transparent margin around the visible card
constexpr int kArm = 10;       ///< bracket arm length
constexpr int kOffset = 6;     ///< brackets sit this far outside the card rect

struct Style {
  bool brackets = true;  ///< active card
  bool live = false;     ///< gold hairline along the top
  bool shadow = true;    ///< floating (not docked in the sidebar)
};

/// Paint the shadow, the panel (surface.raised + 1 px border.default), the brackets and the
/// hairline for the visible card rect `card` (widget coordinates).
void paint(QPainter& p, const QRect& card, const Style& style);
/// Hairline width of live.base borders: 1 px dark, 2 px light (gold is 1.8:1 on white).
int liveHairline();

}  // namespace fabricad::app::CardFrame
