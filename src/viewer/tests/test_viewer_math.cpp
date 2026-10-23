// Unit tests for the GL-free viewer math (camera mapping, grid, views, selection tables).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerMath.h"

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::viewer;

namespace {
Handle(Graphic3d_Camera) makeCamera(bool ortho, int w, int h) {
  Handle(Graphic3d_Camera) cam = new Graphic3d_Camera();
  cam->SetProjectionType(ortho ? Graphic3d_Camera::Projection_Orthographic : Graphic3d_Camera::Projection_Perspective);
  cam->SetEyeAndCenter(gp_Pnt(0, 0, 100), gp_Pnt(0, 0, 0));
  cam->SetUp(gp_Dir(0, 1, 0));
  cam->SetAspect(double(w) / h);
  cam->SetScale(200.0);
  cam->SetZRange(1.0, 1000.0);
  if (!ortho) cam->SetFOVy(45.0);
  return cam;
}
}  // namespace

class ViewerMathTest : public QObject {
  Q_OBJECT
 private slots:
  void centreRayHitsOrigin() {
    for (bool ortho : {true, false}) {
      auto cam = makeCamera(ortho, 800, 600);
      gp_Pnt o;
      gp_Dir d;
      QVERIFY(cameraScreenRay(*cam, 400, 300, 800, 600, o, d));
      QVERIFY(d.IsParallel(gp_Dir(0, 0, -1), 1e-6));
      QVERIFY(d.Z() < 0);  // into the scene
      gp_Pnt hit;
      QVERIFY(intersectLinePlane(o, d, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), hit));
      QVERIFY(hit.Distance(gp_Pnt(0, 0, 0)) < 1e-6);
    }
  }

  void projectionRoundTrip() {
    for (bool ortho : {true, false}) {
      auto cam = makeCamera(ortho, 800, 600);
      const gp_Pnt p(12.5, -7.25, 0);
      double x = 0, y = 0;
      QVERIFY(cameraWorldToScreen(*cam, p, 800, 600, x, y));
      QVERIFY(x > 400 && y > 300);  // +x right, -y down on screen
      gp_Pnt o;
      gp_Dir d;
      QVERIFY(cameraScreenRay(*cam, x, y, 800, 600, o, d));
      gp_Pnt hit;
      QVERIFY(intersectLinePlane(o, d, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), hit));
      QVERIFY2(hit.Distance(p) < 1e-6, qPrintable(QString("dist %1").arg(hit.Distance(p))));
    }
  }

  void pixelSizeMatchesProjection() {
    auto cam = makeCamera(true, 800, 600);
    const double ps = cameraPixelSize(*cam, gp_Pnt(0, 0, 0), 800, 600);
    QVERIFY(ps > 0);
    double x0, y0, x1, y1;
    QVERIFY(cameraWorldToScreen(*cam, gp_Pnt(0, 0, 0), 800, 600, x0, y0));
    QVERIFY(cameraWorldToScreen(*cam, gp_Pnt(ps * 10, 0, 0), 800, 600, x1, y1));
    QVERIFY(std::abs((x1 - x0) - 10.0) < 1e-6);
    QVERIFY(std::abs(ps * 600 - 200.0) < 1e-6);  // ortho scale = view height
  }

  void viewportInsetKeepsContentInFreeBand() {
    // content fitted to the whole 800x600 view: a point at the top edge and one at the bottom
    auto cam = makeCamera(true, 800, 600);
    const gp_Pnt top(0, 100, 0), bottom(0, -100, 0);
    applyViewportInset(*cam, 800, 600, 0, 60);
    double x, yt, yb;
    QVERIFY(cameraWorldToScreen(*cam, top, 800, 600, x, yt));
    QVERIFY(cameraWorldToScreen(*cam, bottom, 800, 600, x, yb));
    // now spans exactly the band above the 60 px inset
    QVERIFY2(std::abs(yt - 0.0) < 1e-6, qPrintable(QString::number(yt)));
    QVERIFY2(std::abs(yb - 540.0) < 1e-6, qPrintable(QString::number(yb)));
    // no inset: unchanged; absurd inset: ignored
    auto cam2 = makeCamera(true, 800, 600);
    applyViewportInset(*cam2, 800, 600, 0, 0);
    QCOMPARE(cam2->Scale(), 200.0);
    applyViewportInset(*cam2, 800, 600, 0, 590);
    QCOMPARE(cam2->Scale(), 200.0);
  }

  void faceOnViews() {
    QVERIFY(isFaceOnView(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1)));
    QVERIFY(isFaceOnView(gp_Dir(0, 0, -1), gp_Dir(-1, 0, 0)));  // rolled top view
    QVERIFY(!isFaceOnView(gp_Dir(1, -1, 1), gp_Dir(0, 0, 1)));  // iso
    QVERIFY(!isFaceOnView(gp_Dir(0, -1, 0), gp_Dir(0.5, 0, 0.866)));  // rolled 30 degrees
    QVERIFY(isFaceOnView(gp_Dir(0.001, -1, 0), gp_Dir(0, 0, 1)));  // within tolerance
  }

  void originPlaneSizeFollowsModel() {
    QCOMPARE(originPlaneHalfSize(0.0), 40.0);
    QCOMPARE(originPlaneHalfSize(10.0), 20.0);    // floor of 20
    QCOMPARE(originPlaneHalfSize(1000.0), 500.0);  // 350 -> 500 (1-2-5 steps)
    QCOMPARE(originPlaneHalfSize(1010.0), 500.0);  // small edits do not change it
    QCOMPARE(originPlaneHalfSize(200.0), 100.0);   // 70 -> 100
  }

  void behindPerspectiveCamera() {
    auto cam = makeCamera(false, 800, 600);
    double x, y;
    QVERIFY(!cameraWorldToScreen(*cam, gp_Pnt(0, 0, cam->Eye().Z() + 50), 800, 600, x, y));
  }

  void parallelLineMissesPlane() {
    gp_Pnt out;
    QVERIFY(!intersectLinePlane(gp_Pnt(0, 0, 5), gp_Dir(1, 0, 0), gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), out));
  }

  void gridSnap() {
    gp_Ax3 xy;
    gp_Pnt s = snapToGrid(xy, gp_Pnt(12.4, -7.6, 0), 5.0);
    QCOMPARE(s.X(), 10.0);
    QCOMPARE(s.Y(), -10.0);
    // tilted plane: snapping happens in plane coordinates
    gp_Ax3 front(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    gp_Pnt f = snapToGrid(front, gp_Pnt(3.2, 0, 8.9), 1.0);
    QVERIFY(f.Distance(gp_Pnt(3, 0, 9)) < 1e-9);
    QVERIFY(snapToGrid(xy, gp_Pnt(1.3, 2, 0), 0).IsEqual(gp_Pnt(1.3, 2, 0), 1e-12));
  }

  void niceSteps() {
    QCOMPARE(niceStep(1.0), 1.0);
    QCOMPARE(niceStep(1.2), 2.0);
    QCOMPARE(niceStep(3.0), 5.0);
    QCOMPARE(niceStep(7.0), 10.0);
    QVERIFY(std::abs(niceStep(0.012) - 0.02) < 1e-12);
    QCOMPARE(niceStep(420.0), 500.0);
    QCOMPARE(niceStep(-1.0), 1.0);
    // 0.5 mm/px with >= 12 px spacing -> 6 mm -> 10 mm
    QCOMPARE(adaptiveGridStep(0.5, 12.0), 10.0);
  }

  void standardViews() {
    auto front = standardViewOrientation(core::StandardView::Front);
    QVERIFY(front.toEye.IsEqual(gp_Dir(0, -1, 0), 1e-12));
    QVERIFY(front.up.IsEqual(gp_Dir(0, 0, 1), 1e-12));
    auto top = standardViewOrientation(core::StandardView::Top);
    QVERIFY(top.toEye.IsEqual(gp_Dir(0, 0, 1), 1e-12));
    QVERIFY(top.up.IsEqual(gp_Dir(0, 1, 0), 1e-12));
    auto bottom = standardViewOrientation(core::StandardView::Bottom);
    QVERIFY(bottom.up.IsEqual(gp_Dir(0, -1, 0), 1e-12));
    auto iso = standardViewOrientation(core::StandardView::Iso);
    QVERIFY(std::abs(gp_Vec(iso.toEye).Dot(gp_Vec(iso.up))) < 1e-12);
    QVERIFY(iso.up.Z() > 0);
    // screen right of the front view is +X: forward x up
    gp_Vec right = gp_Vec(front.toEye.Reversed()).Crossed(gp_Vec(front.up));
    QVERIFY(gp_Dir(right).IsEqual(gp_Dir(1, 0, 0), 1e-12));
    gp_Ax3 plane(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    auto pv = planeViewOrientation(plane);
    QVERIFY(pv.toEye.IsEqual(gp_Dir(1, 0, 0), 1e-12));
    QVERIFY(pv.up.IsEqual(plane.YDirection(), 1e-12));
  }

  void selectionModes() {
    BrepModes all = brepModesFor(core::kAllKinds);
    QVERIFY(all.face && all.edge && all.vertex && !all.body);
    BrepModes bodies = brepModesFor(core::mask(core::ShapeKind::Body));
    QVERIFY(bodies.body && !bodies.face);
    BrepModes faces = brepModesFor(core::kPlaneKinds);
    QVERIFY(faces.face && !faces.edge && !faces.body);
    QVERIFY(brepModesFor(core::mask(core::ShapeKind::Profile)) == BrepModes{});
    const core::ShapeKindMask edgePrio = priorityMask(SelectionPriority::Edge);
    QVERIFY(core::hasKind(edgePrio, core::ShapeKind::Edge));
    QVERIFY(!core::hasKind(edgePrio, core::ShapeKind::Face));
    QVERIFY(core::hasKind(edgePrio, core::ShapeKind::SketchCurve));
    QCOMPARE(priorityMask(SelectionPriority::Default), core::kAllKinds);
    QVERIFY(brepModesFor(priorityMask(SelectionPriority::Body)).body);
  }

  void selectOps() {
    QCOMPARE(selectOpForModifiers(core::ModNone), SelectOp::Replace);
    QCOMPARE(selectOpForModifiers(core::ModShift), SelectOp::Add);
    QCOMPARE(selectOpForModifiers(core::ModCtrl), SelectOp::Toggle);
    QVERIFY(isCrossingSelection(100, 20));
    QVERIFY(!isCrossingSelection(20, 100));
    QVERIFY(!exceedsDragThreshold(2, 3));
    QVERIFY(exceedsDragThreshold(5, 0));
  }

  void navigationPresets() {
    QCOMPARE(navActionFor(NavPreset::Fusion, 2, core::ModNone), NavAction::Pan);
    QCOMPARE(navActionFor(NavPreset::Fusion, 2, core::ModShift), NavAction::Orbit);
    QCOMPARE(navActionFor(NavPreset::Fusion, 1, core::ModNone), NavAction::None);  // LMB stays for selection
    QCOMPARE(navActionFor(NavPreset::SolidWorks, 2, core::ModNone), NavAction::Orbit);
    QCOMPARE(navActionFor(NavPreset::SolidWorks, 2, core::ModCtrl), NavAction::Pan);
    QCOMPARE(navActionFor(NavPreset::Fusion, 2, core::ModShift | core::ModAlt), NavAction::Orbit);
    QCOMPARE(navPresetFromName(navPresetName(NavPreset::SolidWorks)), NavPreset::SolidWorks);
    QCOMPARE(navPresetFromName("garbage"), NavPreset::Fusion);
  }

  void azimuthElevationAndDial() {
    // 7.2: Z up, azimuth from +X toward +Y, elevation from the XY plane
    AzEl ae = azimuthElevation(gp_Dir(1, -1, 1));  // the iso (front-right-top)
    QVERIFY(std::abs(ae.az - 315.0) < 1e-9);
    QVERIFY(std::abs(ae.el - 35.2643896828) < 1e-6);
    ae = azimuthElevation(gp_Dir(0, -1, 0));  // front
    QVERIFY(std::abs(ae.az - 270.0) < 1e-9);
    QVERIFY(std::abs(ae.el) < 1e-9);
    ae = azimuthElevation(gp_Dir(0, 0, 1));  // top: azimuth undefined -> 0
    QVERIFY(std::abs(ae.el - 90.0) < 1e-9);
    // round trip, and up stays Z-ish
    for (double az : {0.0, 45.0, 135.0, 270.0, 315.0})
      for (double el : {-60.0, 0.0, 35.0, 75.0}) {
        const ViewOrientation o = azElOrientation(az, el);
        const AzEl back = azimuthElevation(o.toEye);
        QVERIFY(std::abs(std::remainder(back.az - az, 360.0)) < 1e-6);
        QVERIFY(std::abs(back.el - el) < 1e-6);
        QVERIFY(o.up.Z() > 0);
        QVERIFY(std::abs(o.up.Dot(o.toEye)) < 1e-9);
      }
    // straight down from the front: TOP with +Y up
    ViewOrientation top = azElOrientation(270.0, 90.0);
    QVERIFY(top.toEye.IsEqual(gp_Dir(0, 0, 1), 1e-9));
    QVERIFY(top.up.IsEqual(gp_Dir(0, 1, 0), 1e-9));
    // P5R-KBD-6: at the poles the azimuth comes back from the up direction (TOP + Right = +15)
    for (double az : {0.0, 15.0, 30.0, 270.0, 345.0})
      for (double el : {-90.0, 90.0}) {
        const ViewOrientation o = azElOrientation(az, el);
        const AzEl back = azimuthElevation(o.toEye, o.up);
        QVERIFY2(std::abs(std::remainder(back.az - az, 360.0)) < 1e-6, qPrintable(QString::number(back.az)));
        QVERIFY(std::abs(back.el - el) < 1e-6);
      }
    QVERIFY(std::abs(azimuthElevation(top.toEye, top.up).az - 270.0) < 1e-9);
    // dial projection: in the front view X points right, Z up, Y into the screen
    double x = 0, y = 0, depth = 0;
    projectAxis(gp_Dir(1, 0, 0), gp_Dir(0, -1, 0), gp_Dir(0, 0, 1), x, y, &depth);
    QVERIFY(std::abs(x - 1) < 1e-9 && std::abs(y) < 1e-9);
    projectAxis(gp_Dir(0, 0, 1), gp_Dir(0, -1, 0), gp_Dir(0, 0, 1), x, y, &depth);
    QVERIFY(std::abs(y - 1) < 1e-9);
    projectAxis(gp_Dir(0, 1, 0), gp_Dir(0, -1, 0), gp_Dir(0, 0, 1), x, y, &depth);
    QVERIFY(depth < -0.99);
    // tips -> standard views
    QCOMPARE(axisView(0, true), core::StandardView::Right);
    QCOMPARE(axisView(1, false), core::StandardView::Front);
    QCOMPARE(axisView(2, true), core::StandardView::Top);
    QCOMPARE(currentStandardView(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1)), std::optional(core::StandardView::Front));
    QVERIFY(!currentStandardView(gp_Dir(0.1, -1, 0), gp_Dir(0, 0, 1)).has_value());
    const ViewOrientation iso = standardViewOrientation(core::StandardView::Iso);
    QCOMPARE(currentStandardView(iso.toEye, iso.up), std::optional(core::StandardView::Iso));
    // a hex vertex at the upper right of the front view: the front-right-top corner
    QVERIFY(isoCornerNear(30.0, gp_Dir(0, -1, 0), gp_Dir(0, 0, 1)).IsEqual(gp_Dir(1, -1, 1), 1e-9));
    QVERIFY(isoCornerNear(210.0, gp_Dir(0, -1, 0), gp_Dir(0, 0, 1)).IsEqual(gp_Dir(-1, -1, -1), 1e-9));
  }

  void cubeCornerZones() {
    // front view: the four front corners sit at the corners of the face square
    const gp_Dir front(0, -1, 0), up(0, 0, 1);
    auto hit = cubeCornerHit(51, -51, 51, front, up, 13);  // upper right of the FRONT face
    QVERIFY(hit && hit->IsEqual(gp_Dir(1, -1, 1), 1e-9));
    hit = cubeCornerHit(-45, 47, 51, front, up, 13);  // near the lower left
    QVERIFY(hit && hit->IsEqual(gp_Dir(-1, -1, -1), 1e-9));
    QVERIFY(!cubeCornerHit(0, 0, 51, front, up, 13));    // the face centre is the face
    QVERIFY(!cubeCornerHit(51, 0, 51, front, up, 13));   // the right edge is an edge
    // iso view: the centre is the near corner, not the hidden far one
    const ViewOrientation iso = standardViewOrientation(core::StandardView::Iso);
    hit = cubeCornerHit(0, 0, 51, iso.toEye, iso.up, 13);
    QVERIFY(hit && hit->IsEqual(gp_Dir(1, -1, 1), 1e-9));
  }

  void scaleBarSteps() {
    QCOMPARE(scaleBarLength(1.0, 140), 100.0);
    QCOMPARE(scaleBarLength(0.5, 140), 50.0);
    QCOMPARE(scaleBarLength(0.25, 140), 20.0);
    QCOMPARE(scaleBarLength(0.003, 140), 0.2);
    QVERIFY(std::abs(scaleBarLength(0.0007, 140) - 0.05) < 1e-12);
    QCOMPARE(scaleBarLength(0.0, 140), 0.0);
    QCOMPARE(scaleBarLength(1.0, 0), 0.0);
  }

  void colours() {
    Rgb c = hexColor("#0B6CFF");
    QVERIFY(std::abs(c.r - 11 / 255.0) < 1e-12);
    QVERIFY(std::abs(c.b - 1.0) < 1e-12);
  }
};

QTEST_GUILESS_MAIN(ViewerMathTest)
#include "test_viewer_math.moc"
