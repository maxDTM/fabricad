// Viewport overlays (docs/plans/ui-redesign.md 5.3, 6.3, 7.2): the hex orientation dial and its
// ViewCube switch, breadcrumb, selection-filter bar, coordinate readout and scale bar. Headless:
// the ViewerWidget runs on its fallback context (no GL), which the overlays read like the camera.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerWidget.h"

#include "core/Document.h"
#include "viewer/OverlayLayout.h"
#include "viewer/ViewportOverlays.h"

#include <QImage>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::viewer;

namespace {
std::vector<QWidget*> overlayWidgets(ViewerWidget& w) {
  return {w.orientationDial(), w.breadcrumb(), w.selectionFilterBar(), w.coordReadout(), w.scaleBar()};
}

/// Every pair of the given rects is disjoint.
bool disjoint(const std::vector<QRect>& rs) {
  for (size_t i = 0; i < rs.size(); ++i)
    for (size_t j = i + 1; j < rs.size(); ++j)
      if (!rs[i].isEmpty() && !rs[j].isEmpty() && rs[i].intersects(rs[j])) return false;
  return true;
}
}  // namespace

class OverlaysTest : public QObject {
  Q_OBJECT
 private slots:
  void objectNamesAndPlacement() {
    ViewerWidget w;
    w.resize(1600, 900);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    for (const char* name : {"OrientationDial", "Breadcrumb", "SelectionFilterBar", "CoordReadout", "ScaleBar"}) {
      auto* o = w.findChild<QWidget*>(name);
      QVERIFY2(o && o->isVisible(), name);
      QVERIFY2(w.rect().contains(o->geometry()), name);
    }
    // 16 px margins: dial top-right, breadcrumb top-left, readout bottom-left, scale bar bottom-right
    QCOMPARE(w.orientationDial()->geometry().topRight(), QPoint(1600 - 16 - 34 - 1, 16));  // left of the column lane
    QCOMPARE(w.breadcrumb()->pos(), QPoint(16, 16));
    QCOMPARE(w.coordReadout()->geometry().bottomLeft(), QPoint(16, 900 - 16 - 1));
    QCOMPARE(w.scaleBar()->geometry().bottomRight(), QPoint(1600 - 16 - 1, 900 - 16 - 1));
    // breadcrumb and filter bar share the top row at this width
    QCOMPARE(w.selectionFilterBar()->y(), w.breadcrumb()->y());
    QVERIFY(w.selectionFilterBar()->x() > w.breadcrumb()->geometry().right());
    std::vector<QRect> rs;
    for (QWidget* o : overlayWidgets(w)) rs.push_back(o->geometry());
    QVERIFY(disjoint(rs));
    QCOMPARE(w.orientationRect(), w.orientationDial()->geometry());
  }

  void narrowViewportWrapsTheTopRow() {
    ViewerWidget w;
    w.resize(760, 520);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.breadcrumb()->setCrumbs({"bracket-v3", "Bracket"});
    w.breadcrumb()->setLiveChip("EDITING", "Extrude 3");
    w.layoutOverlayWidgets();
    QVERIFY(w.selectionFilterBar()->y() > w.breadcrumb()->geometry().bottom());
    std::vector<QRect> rs;
    for (QWidget* o : overlayWidgets(w)) rs.push_back(o->geometry());
    QVERIFY(disjoint(rs));
    QVERIFY(!w.breadcrumb()->geometry().intersects(w.orientationRect()));
  }

  void overlayLayoutKeepsTheCardClear() {
    // the viewer reports its overlays as fixed regions; a floating card and the view-tool column
    // never overlap them or each other (6.3)
    for (QSize size : {QSize(1600, 900), QSize(1280, 700), QSize(1000, 640)}) {
      ViewerWidget w;
      w.resize(size);
      w.show();
      QVERIFY(QTest::qWaitForWindowExposed(&w));
      OverlayLayout l;
      l.setViewport(w.rect());
      w.addOverlayRegions(l);
      OverlayLayout::Item tools;
      tools.slot = OverlayLayout::Slot::ViewTools;
      tools.size = QSize(38, 300);
      l.add(tools);
      OverlayLayout::Item card;
      card.slot = OverlayLayout::Slot::Card;
      card.size = QSize(376, 520);
      l.add(card);
      l.compute();
      std::vector<QRect> rs;
      for (const auto& r : l.occupied()) {
        QRect rect = r.rect;
        if (r.slot == OverlayLayout::Slot::Card) rect = rect.adjusted(8, 8, -8, -8);  // the visible card
        rs.push_back(rect);
      }
      QVERIFY(l.rect(OverlayLayout::Slot::Orientation).isValid());
      QVERIFY(l.rect(OverlayLayout::Slot::TopBar).isValid());
      QVERIFY(l.rect(OverlayLayout::Slot::Readout).isValid());
      QVERIFY(l.rect(OverlayLayout::Slot::ScaleBar).isValid());
      QVERIFY2(disjoint(rs), qPrintable(QString("%1x%2").arg(size.width()).arg(size.height())));
      // the column sits below the orientation widget at the right edge
      QVERIFY(l.rect(OverlayLayout::Slot::ViewTools).top() > l.rect(OverlayLayout::Slot::Orientation).bottom());
      // insets for Fit / Home cover the bottom readouts
      QVERIFY(l.insets().bottom() >= 26);
    }
  }

  void orientationSwitch() {
    ViewerWidget w;
    w.resize(1400, 860);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QCOMPARE(w.orientationWidget(), OrientationWidget::HexDial);
    OrientationDial* d = w.orientationDial();
    QVERIFY(d->plateVisible());
    const int dialH = d->height();
    QSignalSpy moved(&w, &ViewerWidget::overlayGeometryChanged);
    w.setOrientationWidget(OrientationWidget::ViewCube);
    QVERIFY(!d->plateVisible());
    QVERIFY(d->height() < dialH);  // readout + view buttons only, under the cube
    QVERIFY(d->isVisible());
    QVERIFY(moved.count() >= 1);
    QVERIFY(d->hitTest(d->buttonRect(2).center()).part == OrientationDial::Part::Button);
    w.setOrientationWidget(OrientationWidget::HexDial);
    QVERIFY(d->plateVisible());
    QCOMPARE(d->height(), dialH);
  }

  void dialReadoutAndButtons() {
    ViewerWidget w;
    w.resize(1400, 860);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    OrientationDial* d = w.orientationDial();
    // headless default: top view
    w.setStandardView(core::StandardView::Top, false);
    QCOMPARE(d->currentButton(), 1);
    QVERIFY(d->readoutText().startsWith("AZ "));
    QVERIFY(d->readoutText().contains("EL 90°"));
    // the buttons drive the standard views (clicked like a user)
    QTest::mouseClick(d, Qt::LeftButton, {}, d->buttonRect(2).center());
    QCOMPARE(d->currentButton(), 2);  // FRONT
    QVERIFY(w.viewDirection().IsEqual(gp_Dir(0, -1, 0), 1e-9));
    QCOMPARE(d->readoutText(), QString("AZ 270°  EL 0°"));
    QTest::mouseClick(d, Qt::LeftButton, {}, d->buttonRect(0).center());
    QCOMPARE(d->currentButton(), 0);  // ISO
    QCOMPARE(d->readoutText(), QString("AZ 315°  EL 35°"));
    QTest::mouseClick(d, Qt::LeftButton, {}, d->buttonRect(3).center());
    QCOMPARE(d->currentButton(), 3);  // RIGHT
  }

  void dialAxisTipsAndVertices() {
    ViewerWidget w;
    w.resize(1400, 860);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    OrientationDial* d = w.orientationDial();
    w.setStandardView(core::StandardView::Iso, false);
    // the +Z tip is above the centre in an iso view, +X lower right, +Y upper right... (Z up)
    const QPointF c = d->plateCenter();
    QVERIFY(d->tipPosition(2, true).y() < c.y());
    QVERIFY(d->tipPosition(0, true).x() > c.x());
    // click the +Z tip: TOP; a second click: BOTTOM
    OrientationDial::Hit h = d->hitTest(d->tipPosition(2, true));
    QCOMPARE(h.part, OrientationDial::Part::AxisTip);
    QCOMPARE(h.axis, 2);
    QVERIFY(h.positive);
    d->activate(h);
    QVERIFY(w.viewDirection().IsEqual(gp_Dir(0, 0, 1), 1e-9));
    h = d->hitTest(d->tipPosition(2, true));
    QCOMPARE(h.part, OrientationDial::Part::AxisTip);
    d->activate(h);
    QVERIFY(w.viewDirection().IsEqual(gp_Dir(0, 0, -1), 1e-9));
    // a hex vertex gives an iso view (|x| = |y| = |z|)
    w.setStandardView(core::StandardView::Front, false);
    OrientationDial::Hit v;
    v.part = OrientationDial::Part::Vertex;
    v.vertex = 5;  // upper right
    d->activate(v);
    const gp_Dir e = w.viewDirection();
    QVERIFY(std::abs(std::abs(e.X()) - std::abs(e.Z())) < 1e-6);
    QVERIFY(std::abs(std::abs(e.Y()) - std::abs(e.Z())) < 1e-6);
    QVERIFY(e.Z() > 0);
    // the wheel steps the elevation by 15 degrees
    w.setViewAzEl(30.0, 0.0, false);
    QWheelEvent wheel(c, d->mapToGlobal(c), {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(d, &wheel);
    const AzEl ae = azimuthElevation(w.viewDirection());
    QVERIFY(std::abs(ae.el - 15.0) < 1e-6);
    QVERIFY(std::abs(ae.az - 30.0) < 1e-6);
  }

  void dialPaintsInBothThemes() {
    // the dial and the bars paint from the palette's theme (no cached colours)
    ViewerWidget w;
    w.resize(1400, 860);
    for (theme::Theme th : {theme::Theme::Dark, theme::Theme::Light}) {
      w.setViewerPalette(ViewerPalette::fromTokens(th));
      OrientationDial* d = w.orientationDial();
      QImage img(d->size(), QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      d->render(&img);
      // the plate centre region is painted with surface.overlay (90 %)
      const QColor px = img.pixelColor(static_cast<int>(d->plateCenter().x()) + 30, static_cast<int>(d->plateCenter().y()) + 40);
      const QColor want = theme::color(theme::Tok::SurfaceOverlay, th);
      QVERIFY2(std::abs(px.red() - want.red()) < 40 && px.alpha() > 200, theme::themeName(th));
    }
  }

  void breadcrumbChip() {
    ViewerWidget w;
    Breadcrumb* b = w.breadcrumb();
    b->setCrumbs({"bracket-v3", "Bracket"});
    const int w0 = b->sizeHint().width();
    QVERIFY(!b->hasChip());
    b->setLiveChip("EDITING", "Extrude 3");
    QCOMPARE(b->chipText(), QString("EDITING Extrude 3"));  // the feature name keeps its case
    QVERIFY(b->sizeHint().width() > w0);
    QVERIFY(b->crumbRect(1).left() > b->crumbRect(0).right());
    QSignalSpy clicked(b, &Breadcrumb::crumbClicked);
    b->resize(b->sizeHint());
    b->show();
    QTest::mouseMove(b, b->crumbRect(0).center());
    QTest::mouseClick(b, Qt::LeftButton, {}, b->crumbRect(0).center());
    QCOMPARE(clicked.count(), 1);
    b->setLiveChip({}, {});
    QVERIFY(!b->hasChip());
  }

  void selectionFilterBar() {
    core::Document doc;
    ViewerWidget w;
    w.setDocument(&doc);
    SelectionFilterBar* f = w.selectionFilterBar();
    for (int i = 0; i < SelectionFilterBar::SegmentCount; ++i) QVERIFY(f->segmentVisible(SelectionFilterBar::Segment(i)));
    QVERIFY(!f->segmentActive(SelectionFilterBar::Faces));
    f->toggle(SelectionFilterBar::Faces);
    QCOMPARE(w.selectionPriority(), SelectionPriority::Face);
    QVERIFY(f->segmentActive(SelectionFilterBar::Faces));
    QVERIFY(!f->segmentActive(SelectionFilterBar::Edges));
    f->toggle(SelectionFilterBar::Faces);  // again: everything
    QCOMPARE(w.selectionPriority(), SelectionPriority::Default);
    f->toggle(SelectionFilterBar::Profiles);
    QCOMPARE(w.selectionPriority(), SelectionPriority::Sketch);
    QVERIFY(!core::hasKind(w.effectiveFilter(), core::ShapeKind::Face));
    QVERIFY(core::hasKind(w.effectiveFilter(), core::ShapeKind::Profile));
    w.setSelectionPriority(SelectionPriority::Default);
    // a tool that takes profiles only: the other segments hide, SKETCH PROFILES lights up
    w.setSelectionFilter(core::ShapeKindMask(core::ShapeKind::Profile) | core::ShapeKind::Sketch);
    QVERIFY(!f->segmentVisible(SelectionFilterBar::Bodies));
    QVERIFY(!f->segmentVisible(SelectionFilterBar::Faces));
    QVERIFY(f->segmentVisible(SelectionFilterBar::Profiles));
    QVERIFY(f->segmentActive(SelectionFilterBar::Profiles));
    QVERIFY(f->segmentRect(SelectionFilterBar::Faces).isEmpty());
    w.setSelectionFilter(core::kAllKinds);
    QVERIFY(f->segmentVisible(SelectionFilterBar::Faces));
  }

  void readoutAndScaleBar() {
    core::Document doc;
    ViewerWidget w;
    w.setDocument(&doc);
    w.resize(1400, 860);
    CoordReadout* r = w.coordReadout();
    QCOMPARE(r->cellText(0), QString::fromUtf8("X —"));
    r->setPoint(gp_Pnt(62, 42, 37));
    QCOMPARE(r->cellText(0), QString("X 62.000"));
    QCOMPARE(r->cellText(1), QString("Y 42.000"));
    QCOMPARE(r->cellText(2), QString("Z 37.000"));
    QCOMPARE(r->unitText(), QString("mm"));
    // headless: 1 mm per pixel -> the largest 1-2-5 step under 140 px is 100 mm
    ScaleBar* s = w.scaleBar();
    s->refresh();
    QCOMPARE(s->length(), 100.0);
    QCOMPARE(s->label(), QString("100 mm"));
    QVERIFY(s->pixels() <= ScaleBar::kRuler);
  }

  void overlaysCanBeHidden() {
    ViewerWidget w;
    w.resize(1400, 860);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.setOverlaysVisible(false, false, false);
    QVERIFY(!w.breadcrumb()->isVisible());
    QVERIFY(!w.coordReadout()->isVisible());
    QVERIFY(!w.scaleBar()->isVisible());
    QVERIFY(w.orientationDial()->isVisible());
    OverlayLayout l;
    l.setViewport(w.rect());
    w.addOverlayRegions(l);
    l.compute();
    QCOMPARE(l.occupied().size(), size_t(1));
  }
};

QTEST_MAIN(OverlaysTest)
#include "test_overlays.moc"
