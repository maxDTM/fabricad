// Unit tests of the viewport overlay layout (docs/plans/ui-redesign.md 6.3, 6.4): pure geometry.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/OverlayLayout.h"

#include <QtTest>

using namespace fabricad::viewer;
using Slot = OverlayLayout::Slot;

namespace {
OverlayLayout::Item item(Slot s, QSize size = {}, QRect fixed = {}) {
  OverlayLayout::Item it;
  it.slot = s;
  it.size = size;
  it.fixed = fixed;
  return it;
}
constexpr int kB = 8;  // card bleed
QRect visible(const QRect& r) { return r.adjusted(kB, kB, -kB, -kB); }
}  // namespace

class TestOverlayLayout : public QObject {
  Q_OBJECT
 private slots:
  void placementNames() {
    for (CardPlacement p : {CardPlacement::Sidebar, CardPlacement::FloatRight, CardPlacement::FloatLeft, CardPlacement::Free})
      QCOMPARE(parsePlacement(placementName(p)), std::optional<CardPlacement>(p));
    QCOMPARE(parsePlacement("FLOATLEFT"), std::optional<CardPlacement>(CardPlacement::FloatLeft));
    QVERIFY(!parsePlacement("middle"));
  }

  // mockup v6 at 1920 x 1080: viewport 1600 x 946, dial 140 x 162 at the top-right
  void floatRightLeftOfTheDial() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 1600, 946));
    l.add(item(Slot::Orientation, QSize(140, 162)));
    l.add(item(Slot::ViewTools, QSize(36, 220)));
    l.add(item(Slot::Card, QSize(376, 548)));
    l.compute();
    const QRect dial = l.rect(Slot::Orientation);
    QCOMPARE(dial, QRect(1600 - 16 - 140, 16, 140, 162));
    const QRect tools = l.rect(Slot::ViewTools);
    QCOMPARE(tools.right(), 1600 - 16 - 1);
    QVERIFY(tools.top() > dial.bottom());
    const QRect card = visible(l.rect(Slot::Card));
    QCOMPARE(card.width(), 360);
    QVERIFY(card.right() < dial.left());          // left of the dial
    QCOMPARE(card.top(), dial.top() + 60);         // below its top edge (mockup: 182 - 122)
    QVERIFY(!card.intersects(tools));
    QCOMPARE(l.cardMaxHeight(), 946 - 16 - card.top() + 2 * kB);
    // insets: the card on the right; the dial is not an edge
    QVERIFY(l.insets().right() >= 1600 - card.left() - kB);
    QCOMPARE(l.insets().left(), 0);
  }

  void floatRightBelowCubeWhenNarrow() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 700, 800));
    l.add(item(Slot::Orientation, {}, QRect(560, 10, 130, 130)));
    l.add(item(Slot::Card, QSize(376, 400)));
    l.compute();
    const QRect card = visible(l.rect(Slot::Card));
    QCOMPARE(card.right(), 700 - 16 - 1);  // at the right edge
    QVERIFY(card.top() > 140);             // below the cube
  }

  void floatRightShortViewportGoesUp() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 900, 330));
    l.add(item(Slot::Orientation, {}, QRect(760, 10, 130, 130)));
    l.add(item(Slot::NavBar, {}, QRect(300, 290, 300, 32)));
    l.add(item(Slot::Card, QSize(376, 400)));
    l.compute();
    const QRect card = visible(l.rect(Slot::Card));
    QCOMPARE(card.top(), 16);              // the height matters more: from the top, left of the cube
    QVERIFY(card.right() < 760);
    QVERIFY(l.cardMaxHeight() < 400);      // the card scrolls
    QCOMPARE(l.rect(Slot::Card).height(), l.cardMaxHeight());
  }

  void floatLeftBelowTheBreadcrumbAndQuickToolbar() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 1600, 900));
    OverlayLayout::Options o;
    o.card = CardPlacement::FloatLeft;
    l.setOptions(o);
    l.add(item(Slot::TopBar, QSize(700, 28)));
    OverlayLayout::Item q = item(Slot::QuickToolbar, QSize(40, 300));
    q.right = false;
    l.add(q);
    l.add(item(Slot::Card, QSize(376, 500)));
    l.compute();
    const QRect bar = l.rect(Slot::TopBar);
    const QRect quick = l.rect(Slot::QuickToolbar);
    const QRect card = visible(l.rect(Slot::Card));
    QCOMPARE(bar.topLeft(), QPoint(16, 16));
    QVERIFY(card.top() > bar.bottom());
    QCOMPARE(quick.left(), 16);            // the quick toolbar sits between the card and the edge
    QVERIFY(card.left() > quick.right());
    QVERIFY(l.insets().left() > card.right() - 1);
    QCOMPARE(l.insets().top(), bar.bottom() + 1);
  }

  void quickToolbarRightOrder() {
    // from the edge: tool column, quick toolbar, card (6.4)
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 1600, 900));
    l.add(item(Slot::Orientation, QSize(140, 162)));
    l.add(item(Slot::ViewTools, QSize(36, 220)));
    l.add(item(Slot::QuickToolbar, QSize(40, 300)));
    l.add(item(Slot::Card, QSize(376, 500)));
    l.compute();
    const QRect tools = l.rect(Slot::ViewTools), quick = l.rect(Slot::QuickToolbar), card = visible(l.rect(Slot::Card));
    QVERIFY(quick.right() < tools.left());
    QVERIFY(card.right() < quick.left());
  }

  void bottomOverlaysLimitTheCard() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 1000, 600));
    l.add(item(Slot::NavBar, {}, QRect(500, 560, 480, 32)));
    l.add(item(Slot::Card, QSize(376, 1000)));
    l.compute();
    const QRect card = visible(l.rect(Slot::Card));
    QVERIFY(card.bottom() < 560);
    QCOMPARE(l.insets().bottom(), 600 - 560);
  }

  void freePositionRoundTrip() {
    const QRect vp(0, 0, 1200, 800);
    const QRect card(300, 100, 376, 400);
    const FreePos f = FreePos::fromRect(card, vp, CardPlacement::FloatLeft);
    QVERIFY(f.valid);
    QCOMPARE(f.anchor, FreePos::TopLeft);
    QCOMPARE(f.base, CardPlacement::FloatLeft);
    const FreePos g = FreePos::fromString(f.toString());
    QVERIFY(g.valid);
    QCOMPARE(g.toString(), f.toString());
    QCOMPARE(g.toRect(card.size(), vp), card);
    // bottom-right anchor keeps the distance to that corner (as a fraction) on resize
    const QRect br(800, 380, 376, 400);
    const FreePos h = FreePos::fromRect(br, vp, CardPlacement::FloatRight);
    QCOMPARE(h.anchor, FreePos::BottomRight);
    const QRect big(0, 0, 2400, 1600);
    const QRect r = h.toRect(br.size(), big);
    QCOMPARE(big.right() - r.right(), 2 * (vp.right() - br.right()));
    // clamped inside a small viewport
    const QRect tiny(0, 0, 300, 200);
    QVERIFY(tiny.contains(h.toRect(QSize(200, 100), tiny)));
    // garbage reads as invalid
    for (const char* s : {"", "free;tl;0;0", "floatLeft;xx;0;0", "floatLeft;tl;a;0", "floatLeft;tl;0"})
      QVERIFY(!FreePos::fromString(s).valid);
  }

  void freeCardPlacement() {
    OverlayLayout l;
    const QRect vp(0, 0, 1000, 700);
    l.setViewport(vp);
    OverlayLayout::Options o;
    o.card = CardPlacement::Free;
    o.free = FreePos::fromString("floatRight;tl;0.2500;0.1000");
    l.setOptions(o);
    l.add(item(Slot::Card, QSize(376, 300)));
    l.compute();
    QCOMPARE(l.rect(Slot::Card).topLeft(), QPoint(250, 70));
    // the drop targets ignore the free position
    QVERIFY(visible(l.cardRectFor(CardPlacement::FloatLeft, QSize(376, 300))).left() == 16);
    QVERIFY(l.cardRectFor(CardPlacement::Sidebar, QSize(376, 300)).isEmpty());
  }

  void paletteStacksOrMoves() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 1200, 900));
    l.add(item(Slot::Orientation, {}, QRect(1050, 10, 140, 140)));
    l.add(item(Slot::Card, QSize(376, 300)));
    l.add(item(Slot::SketchPalette, QSize(260, 200)));
    l.compute();
    const QRect card = visible(l.rect(Slot::Card));
    const QRect pal = l.rect(Slot::SketchPalette);
    QVERIFY(!pal.intersects(card));
    QVERIFY(pal.isValid());
    // a tall card on the right: the palette takes the free left side
    OverlayLayout m;
    m.setViewport(QRect(0, 0, 900, 700));
    m.add(item(Slot::Orientation, {}, QRect(760, 10, 130, 130)));
    m.add(item(Slot::Card, QSize(376, 650)));
    m.add(item(Slot::SketchPalette, QSize(260, 200)));
    m.compute();
    const QRect c2 = visible(m.rect(Slot::Card));
    const QRect p2 = m.rect(Slot::SketchPalette);
    QVERIFY(!p2.intersects(c2));
    QCOMPARE(p2.left(), 16);
    // a palette the user dragged stays (clamped)
    OverlayLayout n;
    n.setViewport(QRect(0, 0, 900, 700));
    OverlayLayout::Item moved = item(Slot::SketchPalette, QSize(260, 200), QRect(800, 600, 260, 200));
    moved.userMoved = true;
    n.add(moved);
    n.compute();
    QCOMPARE(n.rect(Slot::SketchPalette), QRect(640, 500, 260, 200));
  }

  void findFreeAvoidsEverything() {
    OverlayLayout l;
    l.setViewport(QRect(0, 0, 800, 600));
    l.add(item(Slot::Orientation, {}, QRect(650, 10, 140, 140)));
    l.add(item(Slot::Card, QSize(376, 500)));
    OverlayLayout::Item bubble;
    bubble.slot = Slot::Other;
    bubble.id = "tutorialBubble";
    bubble.size = QSize(200, 80);
    l.add(bubble);
    l.compute();
    const QRect b = l.rect(Slot::Other, "tutorialBubble");
    QVERIFY(b.isValid());
    for (const auto& r : l.occupied())
      if (r.slot != Slot::Other) QVERIFY(!r.rect.intersects(b));
    QVERIFY(l.isFree(QRect(20, 20, 50, 50)));
    QVERIFY(!l.isFree(l.rect(Slot::Card)));
    QVERIFY(l.isFree(l.rect(Slot::Card), Slot::Card));
    // nothing fits: empty
    QVERIFY(l.findFree(QSize(900, 50), QPoint(0, 0)).isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestOverlayLayout)
#include "test_overlay_layout.moc"
