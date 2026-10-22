// FabriCAD app: command card frame painting.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CardFrame.h"

#include "theme/Tokens.h"

#include <QPainter>
#include <QPainterPath>

namespace fabricad::app::CardFrame {

using theme::Tok;

int liveHairline() { return theme::current() == theme::Theme::Light ? 2 : 1; }

void paint(QPainter& p, const QRect& card, const Style& style) {
  p.save();
  if (style.shadow) {
    // soft drop shadow inside the bleed (shadow.card, offset down): stacked translucent outlines
    const QColor base = theme::color(Tok::ShadowCard);
    for (int i = kBleed - 1; i >= 1; --i) {
      QColor c = base;
      c.setAlphaF(base.alphaF() * 0.16 * (kBleed - i) / kBleed);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRect(card.adjusted(-i + 1, -i + 3, i - 1, i - 1).intersected(card.adjusted(-kBleed, -kBleed, kBleed, kBleed)));
    }
  }
  p.setBrush(theme::color(Tok::SurfaceRaised));
  p.setPen(Qt::NoPen);
  p.drawRect(card);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(theme::color(Tok::BorderDefault), 1));
  p.drawRect(card.adjusted(0, 0, -1, -1));
  if (style.live) {
    const int w = liveHairline();
    p.fillRect(QRect(card.left(), card.top(), card.width(), w), theme::color(Tok::LiveBase));
  }
  if (style.brackets) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(theme::color(Tok::AccentIndicator), 1.5);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    const QRectF o = QRectF(card).adjusted(-kOffset + 0.75, -kOffset + 0.75, kOffset - 0.75, kOffset - 0.75);
    auto corner = [&](QPointF c, double dx, double dy) {
      QPainterPath path;
      path.moveTo(c.x() + dx * kArm, c.y());
      path.lineTo(c);
      path.lineTo(c.x(), c.y() + dy * kArm);
      p.drawPath(path);
    };
    corner(o.topLeft(), 1, 1);
    corner(o.topRight(), -1, 1);
    corner(o.bottomLeft(), 1, -1);
    corner(o.bottomRight(), -1, -1);
  }
  p.restore();
}

}  // namespace fabricad::app::CardFrame
