// Smoke test for the viewer widget. The real OCCT viewer cannot run offscreen (no GLX), so
// this checks the headless behaviour: fallback IViewerContext, document-driven display
// model, filters, visibility state and signals.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerWidget.h"

#include "core/Document.h"
#include "core/Selection.h"
#include "core/tests/support/TestFeatures.h"
#include "support/ViewerTestFeatures.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace fabricad;
using core::ShapeKind;

class ViewerSmokeTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { core::test::registerTestFeatures(); }

  void constructsHeadless() {
    viewer::ViewerWidget w;
    QVERIFY(w.widget() == &w);
    QVERIFY(!w.inSketchMode());
    QSignalSpy failed(&w, &viewer::ViewerWidget::viewInitFailed);
    QVERIFY(!w.initializeView());  // offscreen platform: no GL view
    QCOMPARE(failed.count(), 1);
    QVERIFY(w.glInfo().contains("xcb"));
    QVERIFY(!w.hasGlView());
    QVERIFY(!w.initializeView());  // stays failed, no second signal
    QCOMPARE(failed.count(), 1);
  }

  void fallbackContext() {
    viewer::ViewerWidget w;
    gp_Ax3 plane(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    w.enterSketchMode(plane, true);
    QVERIFY(w.inSketchMode());
    QVERIFY(w.fallbackContext().inSketchMode());
    gp_Pnt p;
    QVERIFY(w.screenToPlane(10, 20, gp_Pln(plane), p));
    w.exitSketchMode();
    QVERIFY(!w.inSketchMode());
    auto h = w.showPreviewLines({{0, 0, 0}, {1, 0, 0}}, core::PreviewStyle::sketchLine());
    QVERIFY(h != core::kInvalidPreview);
    QCOMPARE(w.fallbackContext().previews().size(), size_t(1));
    w.updatePreviewLines(h, {{0, 0, 0}, {2, 0, 0}});
    w.removePreview(h);
    QVERIFY(w.fallbackContext().previews().empty());
    w.setProjection(core::ProjectionMode::Perspective);
    QCOMPARE(w.projection(), core::ProjectionMode::Perspective);
    w.setDisplayMode(core::DisplayMode::Wireframe);
    QCOMPARE(w.displayMode(), core::DisplayMode::Wireframe);
    w.setGridSnap(true, 5.0);
    QCOMPARE(w.gridSpacing(), 5.0);
    w.fitAll(false);
    QCOMPARE(w.fallbackContext().fitCount(), 1);
  }

  void displayFollowsDocument() {
    core::Document doc;
    viewer::ViewerWidget w;
    w.setDocument(&doc);
    w.syncNow();
    QVERIFY(w.sceneItems().empty());
    auto* box = doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    QTRY_COMPARE(w.sceneItems().size(), size_t(1));  // coalesced sync on the event loop
    const int syncs = w.syncCount();
    // many signals, one sync
    for (int i = 0; i < 5; ++i) doc.notifyBodyChanged(box->createdBodies().front());
    QCoreApplication::processEvents();
    QCOMPARE(w.syncCount(), syncs + 1);
    doc.setRollbackIndex(0);
    QTRY_VERIFY(w.sceneItems().empty());
  }

  void filtersShowOriginPlanesAndVisibility() {
    core::Document doc;
    auto* sk = doc.addFeature(std::make_unique<viewer::test::FakeSketch>());
    doc.recompute();
    viewer::ViewerWidget w;
    w.setDocument(&doc);
    w.syncNow();
    QCOMPARE(w.sceneItems().size(), size_t(1));
    QVERIFY(w.isFeatureDisplayed(sk->id()));

    // a tool restricting the pick filter to planes shows the origin planes
    doc.selection().setFilter(core::kPlaneKinds);
    w.syncNow();
    QCOMPARE(w.sceneItems().size(), size_t(4));
    QVERIFY(!core::hasKind(w.effectiveFilter(), ShapeKind::Edge));
    doc.selection().setFilter(core::kAllKinds);
    w.syncNow();
    QCOMPARE(w.sceneItems().size(), size_t(1));

    // tool filter via IViewerContext + user priority combine
    w.setSelectionFilter(core::ShapeKind::Face | core::ShapeKind::Edge);
    w.setSelectionPriority(viewer::SelectionPriority::Edge);
    QVERIFY(core::hasKind(w.effectiveFilter(), ShapeKind::Edge));
    QVERIFY(!core::hasKind(w.effectiveFilter(), ShapeKind::Face));
    w.setSelectionFilter(core::kAllKinds);
    w.setSelectionPriority(viewer::SelectionPriority::Default);
    QCOMPARE(w.effectiveFilter(), core::kAllKinds);

    QSignalSpy vis(&w, &viewer::ViewerWidget::featureVisibilityChanged);
    w.setFeatureHidden(sk->id(), true);
    QCOMPARE(vis.count(), 1);
    w.setFeatureHidden(sk->id(), true);  // no change, no signal
    QCOMPARE(vis.count(), 1);
    w.syncNow();
    QVERIFY(w.sceneItems().empty());
    QVERIFY(!w.isFeatureDisplayed(sk->id()));
    w.setFolderVisible("Sketches", false);
    w.setFeatureHidden(sk->id(), false);
    w.syncNow();
    QVERIFY(w.sceneItems().empty());
    QVERIFY(!w.isFolderVisible("Sketches"));
    w.setFolderVisible("Sketches", true);
    w.setOriginVisible(true);
    w.syncNow();
    QCOMPARE(w.sceneItems().size(), size_t(8));
    // a tool hiding the sketch it edits
    w.setFeatureVisibilityOverride(sk->id(), false);
    w.syncNow();
    QCOMPARE(w.sceneItems().size(), size_t(7));
    w.setFeatureVisibilityOverride(sk->id(), std::nullopt);
    w.syncNow();
    QCOMPARE(w.sceneItems().size(), size_t(8));
  }

  void originPlanesFollowModelSize() {
    core::Document doc;
    auto* box = doc.addFeature(std::make_unique<core::test::BoxFeature>());
    box->prop("dx").setDouble(400.0);
    box->prop("dy").setDouble(300.0);
    box->prop("dz").setDouble(200.0);
    doc.recompute();
    viewer::ViewerWidget w;
    w.setDocument(&doc);
    w.setOriginVisible(true);
    w.syncNow();
    double halfSize = 0;
    for (const viewer::SceneItem& it : w.sceneItems())
      if (it.key == "origin:XY") {
        Bnd_Box b;
        BRepBndLib::Add(it.shape, b);
        double x0, y0, z0, x1, y1, z1;
        b.Get(x0, y0, z0, x1, y1, z1);
        halfSize = (x1 - x0) / 2 / 0.9;  // planeFace(.., 0.9 h)
      }
    // diagonal ~539 -> 0.35 * 539 = 189 -> 200
    QVERIFY2(std::abs(halfSize - 200.0) < 1.0, qPrintable(QString::number(halfSize)));
  }
};

QTEST_MAIN(ViewerSmokeTest)
#include "test_viewer_smoke.moc"
