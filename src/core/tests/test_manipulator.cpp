// Unit tests for the headless manipulator layer (core/Manipulator.h).
//
// Test shape (repeatable, no GL): all rays are explicit world rays or pixels mapped by
// NullViewerContext's camera (XY plane, Z towards the eye, pixel (x, y) -> world
// (x * px, -y * px), px = pixelSize). Every case states the frame, the size, the increment
// policy (Ctrl = free, Shift = coarse, spec increment or automatic) and the expected value.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Manipulator.h"
#include "core/Tool.h"
#include "core/ViewerContext.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::core;

namespace {

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
bool near(const gp_Pnt& a, const gp_Pnt& b, double tol = 1e-6) { return a.Distance(b) <= tol; }

/// Ray straight down the -Z axis through (x, y, 100).
gp_Pnt rayDown(double x, double y) { return gp_Pnt(x, y, 100); }
const gp_Dir kDown(0, 0, -1);
const gp_Ax3 kXYFrame(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
/// Frame whose Z (drag axis) is world +X, in the XY plane.
const gp_Ax3 kAlongX(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));

ManipulatorSpec linearSpec(double value, double size = 20.0) {
  ManipulatorSpec s;
  s.kind = ManipulatorKind::Linear;
  s.frame = kAlongX;
  s.size = size;
  s.value = value;
  return s;
}

struct Recorder {
  std::vector<ManipulatorEvent> events;
  ManipulatorCallback callback() {
    return [this](const ManipulatorEvent& e) { events.push_back(e); };
  }
  int count(ManipulatorPhase p) const {
    int n = 0;
    for (const auto& e : events)
      if (e.phase == p) ++n;
    return n;
  }
  const ManipulatorEvent* last(ManipulatorPhase p) const {
    for (auto it = events.rbegin(); it != events.rend(); ++it)
      if (it->phase == p) return &*it;
    return nullptr;
  }
};

}  // namespace

class ManipulatorTest : public QObject {
  Q_OBJECT
 private slots:
  // --- snapping ------------------------------------------------------------------------------
  void snappingSeries() {
    QVERIFY(near(ManipulatorModel::snapValue(12.3, 1.0), 12.0));
    QVERIFY(near(ManipulatorModel::snapValue(12.6, 0.5), 12.5));
    QVERIFY(near(ManipulatorModel::snapValue(-7.4, 5.0), -5.0));
    QVERIFY(near(ManipulatorModel::snapValue(12.3, 0.0), 12.3));  // off
    QVERIFY(near(ManipulatorModel::niceIncrement(0.3), 0.5));
    QVERIFY(near(ManipulatorModel::niceIncrement(1.0), 1.0));
    QVERIFY(near(ManipulatorModel::niceIncrement(12.0), 20.0));
    QVERIFY(near(ManipulatorModel::niceIncrement(0.0), 1.0));
    QVERIFY(near(ManipulatorModel::niceAngleIncrement(0.86), 1.0));
    QVERIFY(near(ManipulatorModel::niceAngleIncrement(12.0), 15.0));
    QVERIFY(near(ManipulatorModel::niceAngleIncrement(200.0), 90.0));
    // automatic: about 3 px of travel per step
    QVERIFY(near(ManipulatorModel::autoIncrement(0.1, false, 0), 0.5));
    QVERIFY(near(ManipulatorModel::autoIncrement(1.0, false, 0), 5.0));
    QVERIFY(near(ManipulatorModel::autoIncrement(0.1, true, 20.0), 1.0));
    // modifier policy: Ctrl free, Shift coarse
    ManipulatorModel m(linearSpec(0));
    QVERIFY(near(m.incrementFor(ModNone, 0.1), 0.5));
    QVERIFY(near(m.incrementFor(ModShift, 0.1), 5.0));
    QVERIFY(near(m.incrementFor(ModCtrl, 0.1), 0.0));
    ManipulatorSpec fixed = linearSpec(0);
    fixed.increment = 2.5;
    ManipulatorModel f(fixed);
    QVERIFY(near(f.incrementFor(ModNone, 0.1), 2.5));
    QVERIFY(near(f.incrementFor(ModShift, 0.1), 25.0));
  }

  // --- linear arrow ----------------------------------------------------------------------------
  void linearHitAndDrag() {
    // arrow along +X, base at x = 5 (value 5), length 20 -> shaft+cone cover x in [5, 25]
    ManipulatorModel m(linearSpec(5.0));
    const double px = 0.1;  // tolerance 0.8 mm, cone radius 1.7 mm
    QCOMPARE(m.hit(rayDown(15, 0), kDown, px).handle, ManipulatorHandle::MoveZ);
    QCOMPARE(m.hit(rayDown(24, 1.0), kDown, px).handle, ManipulatorHandle::MoveZ);
    QCOMPARE(m.hit(rayDown(3, 0), kDown, px).handle, ManipulatorHandle::None);   // behind the base
    QCOMPARE(m.hit(rayDown(15, 5), kDown, px).handle, ManipulatorHandle::None);  // beside the shaft
    QVERIFY(near(m.handlePoint(ManipulatorHandle::MoveZ, px), gp_Pnt(5, 0, 0)));
    QVERIFY(near(m.activeFrame().Location(), gp_Pnt(5, 0, 0)));

    // free drag (Ctrl): grab at x = 15, move to 22.3 -> value 5 + 7.3
    QVERIFY(m.beginDrag(ManipulatorHandle::MoveZ, rayDown(15, 0), kDown, px));
    QVERIFY(m.isDragging());
    QVERIFY(near(m.startValue(), 5.0));
    QVERIFY(m.dragTo(rayDown(22.3, 0), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), 12.3));
    QVERIFY(near(m.dragAmount(), 7.3));
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(m.dragTransform()), gp_Pnt(7.3, 0, 0)));
    // automatic snapping at 0.1 mm/px: 0.5 mm steps
    QVERIFY(m.dragTo(rayDown(22.3, 0), kDown, ModNone, px));
    QVERIFY(near(m.value(), 12.5));
    // Shift: ten times coarser
    QVERIFY(m.dragTo(rayDown(22.3, 0), kDown, ModShift, px));
    QVERIFY(near(m.value(), 10.0));
    // the value may go negative (extrude flips sides)
    QVERIFY(m.dragTo(rayDown(-10, 0), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), -20.0));
    // the mouse may wander off the axis: only the axial component counts
    QVERIFY(m.dragTo(rayDown(20, 30), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), 10.0));
    m.endDrag();
    QVERIFY(!m.isDragging());
    QVERIFY(near(m.value(), 10.0));

    // a ray parallel to the axis cannot drive it
    ManipulatorModel z(ManipulatorSpec{});  // Z-axis arrow
    QVERIFY(!z.beginDrag(ManipulatorHandle::MoveZ, rayDown(0, 0), kDown, px));
    QVERIFY(!z.isDragging());
  }

  void linearLimitsAndCancel() {
    ManipulatorSpec s = linearSpec(2.0);
    s.minValue = 0.0;
    s.maxValue = 30.0;
    ManipulatorModel m(s);
    QVERIFY(m.beginDrag(ManipulatorHandle::MoveZ, rayDown(10, 0), kDown, 0.1));
    QVERIFY(m.dragTo(rayDown(-50, 0), kDown, ModCtrl, 0.1));
    QVERIFY(near(m.value(), 0.0));
    QVERIFY(m.dragTo(rayDown(500, 0), kDown, ModCtrl, 0.1));
    QVERIFY(near(m.value(), 30.0));
    m.cancelDrag();
    QVERIFY(!m.isDragging());
    QVERIFY(near(m.value(), 2.0));  // restored
    // setValue moves the handle when idle, not during a drag
    m.setValue(7.0);
    QVERIFY(near(m.value(), 7.0));
    QVERIFY(m.beginDrag(ManipulatorHandle::MoveZ, rayDown(15, 0), kDown, 0.1));
    m.setValue(1.0);
    QVERIFY(near(m.value(), 7.0));
    // setSpec during a drag keeps the dragged value but takes the new frame
    ManipulatorSpec moved = s;
    moved.frame = gp_Ax3(gp_Pnt(0, 5, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    moved.value = 99.0;
    m.setSpec(moved);
    QVERIFY(near(m.value(), 7.0));
    QVERIFY(near(m.spec().frame.Location(), gp_Pnt(0, 5, 0)));
    m.endDrag();
    m.setSpec(moved);
    QVERIFY(near(m.value(), 99.0));
  }

  // --- angular ring ----------------------------------------------------------------------------
  void angularHitAndDrag() {
    ManipulatorSpec s;
    s.kind = ManipulatorKind::Angular;
    s.frame = kXYFrame;
    s.size = 20.0;  // ring radius
    s.value = 0.0;  // knob at +X
    ManipulatorModel m(s);
    const double px = 0.1;
    QCOMPARE(m.hit(rayDown(20, 0), kDown, px).handle, ManipulatorHandle::RotZ);  // the knob
    QCOMPARE(m.hit(rayDown(0, 20), kDown, px).handle, ManipulatorHandle::RotZ);  // on the ring
    QCOMPARE(m.hit(rayDown(10, 0), kDown, px).handle, ManipulatorHandle::None);  // inside
    QVERIFY(near(m.handlePoint(ManipulatorHandle::RotZ, px), gp_Pnt(20, 0, 0)));

    QVERIFY(m.beginDrag(ManipulatorHandle::RotZ, rayDown(20, 0), kDown, px));
    QVERIFY(m.dragTo(rayDown(0, 20), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), 90.0));
    QVERIFY(near(gp_Pnt(1, 0, 0).Transformed(m.dragTransform()), gp_Pnt(0, 1, 0)));
    QVERIFY(near(m.handlePoint(ManipulatorHandle::RotZ, px), gp_Pnt(0, 20, 0)));
    // angles accumulate past 180 degrees when the mouse keeps turning
    QVERIFY(m.dragTo(rayDown(-20, 0), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), 180.0));
    QVERIFY(m.dragTo(rayDown(0, -20), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), 270.0));
    // and back
    QVERIFY(m.dragTo(rayDown(20, 0), kDown, ModCtrl, px));
    QVERIFY(near(m.value(), 360.0));
    // automatic angular snapping at this zoom: 1 degree steps
    QVERIFY(m.dragTo(rayDown(20 * std::cos(0.2), 20 * std::sin(0.2)), kDown, ModNone, px));
    QVERIFY(near(m.value(), 360.0 + std::round(0.2 * 180.0 / M_PI)));
    m.endDrag();

    // fixed increment + limits (joint with limits -45..45, 15 degree steps)
    s.increment = 15.0;
    s.minValue = -45.0;
    s.maxValue = 45.0;
    ManipulatorModel j(s);
    QVERIFY(j.beginDrag(ManipulatorHandle::RotZ, rayDown(20, 0), kDown, px));
    QVERIFY(j.dragTo(rayDown(14.14, 14.14), kDown, ModNone, px));
    QVERIFY(near(j.value(), 45.0));
    QVERIFY(j.dragTo(rayDown(20 * std::cos(0.35), 20 * std::sin(0.35)), kDown, ModNone, px));  // 20 deg -> 15
    QVERIFY(near(j.value(), 15.0));
    QVERIFY(j.dragTo(rayDown(-20, 0), kDown, ModNone, px));  // clamped
    QVERIFY(near(j.value(), 45.0));
    // the ray along the plane cannot drive it
    QVERIFY(!j.dragTo(gp_Pnt(100, 0, 0), gp_Dir(-1, 0, 0), ModNone, px));
  }

  // --- point handle --------------------------------------------------------------------------------
  void pointDrag() {
    ManipulatorSpec s;
    s.kind = ManipulatorKind::Point;
    s.frame = gp_Ax3(gp_Pnt(1, 2, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    s.size = 10.0;
    s.planar = true;
    ManipulatorModel m(s);
    const double px = 0.1;
    QCOMPARE(m.hit(rayDown(1, 2), kDown, px).handle, ManipulatorHandle::Point);
    QCOMPARE(m.hit(rayDown(5, 2), kDown, px).handle, ManipulatorHandle::None);
    QVERIFY(m.beginDrag(ManipulatorHandle::Point, rayDown(1, 2), kDown, px));
    QVERIFY(m.dragTo(rayDown(4, 6), kDown, ModCtrl, px));
    QVERIFY(near(m.point(), gp_Pnt(4, 6, 0)));
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(m.dragTransform()), gp_Pnt(3, 4, 0)));
    QVERIFY(near(m.dragAmount(), 5.0));
    // snapping applies per axis
    QVERIFY(m.dragTo(rayDown(4.3, 6.4), kDown, ModNone, px));  // 0.5 mm steps
    QVERIFY(near(m.point(), gp_Pnt(4.5, 6.5, 0)));
    m.endDrag();
    QVERIFY(near(m.point(), gp_Pnt(1, 2, 0)));  // idle: back at the anchor (the tool re-anchors it)
    // view-plane mode: the plane is perpendicular to the ray at the press
    s.planar = false;
    ManipulatorModel v(s);
    QVERIFY(v.beginDrag(ManipulatorHandle::Point, rayDown(1, 2), kDown, px));
    QVERIFY(v.dragTo(rayDown(2, 2), kDown, ModCtrl, px));
    QVERIFY(near(v.point(), gp_Pnt(2, 2, 0)));
  }

  // --- triad ------------------------------------------------------------------------------------------
  void triadHitAndDrag() {
    ManipulatorSpec s;
    s.kind = ManipulatorKind::Triad;
    s.frame = kXYFrame;
    s.size = 20.0;
    ManipulatorModel t(s);
    const double px = 0.05;  // tol 0.4 mm
    QCOMPARE(t.hit(rayDown(10, 0), kDown, px).handle, ManipulatorHandle::MoveX);
    QCOMPARE(t.hit(rayDown(0, 12), kDown, px).handle, ManipulatorHandle::MoveY);
    QCOMPARE(t.hit(rayDown(8, 8), kDown, px).handle, ManipulatorHandle::PlaneXY);  // square spans 6..10
    QCOMPARE(t.hit(rayDown(40, 40), kDown, px).handle, ManipulatorHandle::None);
    const double d = 15.0 / std::sqrt(2.0);  // on the Z ring (r = 0.75 * 20)
    QCOMPARE(t.hit(rayDown(d, d), kDown, px).handle, ManipulatorHandle::RotZ);
    // only enabled handles are drawn and hit
    s.handles = kHandleRotZ;
    t.setSpec(s);
    QCOMPARE(t.hit(rayDown(10, 0), kDown, px).handle, ManipulatorHandle::None);
    QCOMPARE(t.handles().size(), size_t(1));
    s.handles = kHandleTriadAll;
    t.setSpec(s);
    QCOMPARE(t.handles().size(), size_t(9));

    // translate along X by 5 (free), then snapped to 1 mm
    QVERIFY(t.beginDrag(ManipulatorHandle::MoveX, rayDown(10, 0), kDown, px));
    QVERIFY(t.dragTo(rayDown(15, 0), kDown, ModCtrl, px));
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(t.dragTransform()), gp_Pnt(5, 0, 0)));
    QVERIFY(near(t.dragAmount(), 5.0));
    ManipulatorSpec snapped = s;
    snapped.increment = 1.0;
    t.setSpec(snapped);
    QVERIFY(t.dragTo(rayDown(17.4, 0), kDown, ModNone, px));
    QVERIFY(near(t.dragAmount(), 7.0));
    t.endDrag();
    QVERIFY(near(t.value(), 0.0));  // triads do not keep a value between drags
    // rotate 90 degrees about Z
    QVERIFY(t.beginDrag(ManipulatorHandle::RotZ, rayDown(15, 0), kDown, px));
    QVERIFY(t.dragTo(rayDown(0, 15), kDown, ModCtrl, px));
    QVERIFY(near(t.dragAmount(), 90.0));
    QVERIFY(near(gp_Pnt(1, 0, 0).Transformed(t.dragTransform()), gp_Pnt(0, 1, 0)));
    t.endDrag();
    // plane drag moves in XY only
    QVERIFY(t.beginDrag(ManipulatorHandle::PlaneXY, rayDown(8, 8), kDown, px));
    QVERIFY(t.dragTo(rayDown(11, 6), kDown, ModCtrl, px));
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(t.dragTransform()), gp_Pnt(3, -2, 0)));
    t.endDrag();
    // a disabled handle cannot be dragged
    t.setSpec([&] { ManipulatorSpec r = s; r.handles = kHandleRotZ; return r; }());
    QVERIFY(!t.beginDrag(ManipulatorHandle::MoveX, rayDown(10, 0), kDown, px));
  }

  // --- glyphs -----------------------------------------------------------------------------------------
  void glyphsDescribeEveryKind() {
    for (ManipulatorKind k : {ManipulatorKind::Linear, ManipulatorKind::Angular, ManipulatorKind::Triad, ManipulatorKind::Point}) {
      ManipulatorSpec s;
      s.kind = k;
      s.frame = kXYFrame;
      s.size = 20.0;
      s.value = k == ManipulatorKind::Angular ? 30.0 : 4.0;
      ManipulatorModel m(s);
      auto g = m.glyphs(0.1);
      QVERIFY2(!g.empty(), manipulatorKindName(k));
      for (const auto& x : g) QVERIFY(x.handle != ManipulatorHandle::None);
      // highlighted handle takes the highlight colour
      auto hl = m.glyphs(0.1, m.handles().front());
      bool any = false;
      for (const auto& x : hl)
        if (x.color == kManipulatorHighlight) any = true;
      QVERIFY(any);
    }
    // linear: shaft + cone + offset guide (value != 0)
    ManipulatorModel lin(linearSpec(4.0));
    auto g = lin.glyphs(0.1);
    QCOMPARE(g.size(), size_t(3));
    QCOMPARE(g[0].shape, ManipulatorGlyph::Shape::Cylinder);
    QCOMPARE(g[1].shape, ManipulatorGlyph::Shape::Cone);
    QVERIFY(near(g[0].frame.Location(), gp_Pnt(4, 0, 0)));
    QVERIFY(near(g[0].length + g[1].length, 20.0));
    QVERIFY(g[2].dashed);
    QCOMPARE(ManipulatorModel(linearSpec(0.0)).glyphs(0.1).size(), size_t(2));
    // automatic size: kAutoSizePixels on screen
    ManipulatorSpec auto_ = linearSpec(0.0, 0.0);
    QVERIFY(near(ManipulatorModel(auto_).effectiveSize(0.5), kAutoSizePixels * 0.5));
    // triad: arrows carry the axis colours, planes are translucent
    ManipulatorSpec t;
    t.kind = ManipulatorKind::Triad;
    t.size = 10;
    auto tg = ManipulatorModel(t).glyphs(0.1);
    int quads = 0, tori = 0, cones = 0;
    for (const auto& x : tg) {
      if (x.shape == ManipulatorGlyph::Shape::Quad) ++quads;
      if (x.shape == ManipulatorGlyph::Shape::Torus) ++tori;
      if (x.shape == ManipulatorGlyph::Shape::Cone) ++cones;
      if (x.shape == ManipulatorGlyph::Shape::Quad) QVERIFY(x.color[3] < 1.0f);
    }
    QCOMPARE(quads, 3);
    QCOMPARE(tori, 3);
    QCOMPARE(cones, 3);
  }

  // --- controller through NullViewerContext -------------------------------------------------------
  void controllerHoverDragRelease() {
    NullViewerContext viewer;
    viewer.setPixelSize(0.1);  // pixel (x, y) -> world (0.1 x, -0.1 y)
    Recorder rec;
    ManipulatorSpec s = linearSpec(0.0);  // arrow along +X from the origin, 20 mm = 200 px
    s.tag = 7;
    ManipulatorId id = viewer.showManipulator(s, rec.callback());
    QVERIFY(id != kInvalidManipulator);
    QCOMPARE(viewer.manipulators().count(), size_t(1));
    QCOMPARE(viewer.manipulators().findByTag(7), id);

    // hover on / off
    QVERIFY(viewer.manipulatorMove(100, 0));
    QCOMPARE(viewer.manipulators().hoveredId(), id);
    QCOMPARE(viewer.manipulators().hoveredHandle(), ManipulatorHandle::MoveZ);
    QCOMPARE(rec.count(ManipulatorPhase::Hover), 1);
    QVERIFY(!viewer.manipulatorMove(400, 400));
    QCOMPARE(viewer.manipulators().hoveredId(), kInvalidManipulator);
    QCOMPARE(rec.count(ManipulatorPhase::Leave), 1);
    // a press away from the gizmo is not consumed
    QVERIFY(!viewer.manipulatorPress(400, 400));
    QVERIFY(!viewer.isManipulatorDragging());
    // drag: grab at 100 px (10 mm), move to 150 px -> +5 mm (Ctrl = free)
    QVERIFY(viewer.manipulatorPress(100, 0));
    QVERIFY(viewer.isManipulatorDragging());
    QCOMPARE(viewer.manipulators().draggingId(), id);
    QCOMPARE(rec.count(ManipulatorPhase::DragBegin), 1);
    QVERIFY(viewer.manipulatorMove(150, 3, ModCtrl));
    QCOMPARE(rec.count(ManipulatorPhase::Drag), 1);
    QVERIFY(near(rec.last(ManipulatorPhase::Drag)->value, 5.0));
    QVERIFY(near(rec.last(ManipulatorPhase::Drag)->startValue, 0.0));
    QCOMPARE(rec.last(ManipulatorPhase::Drag)->tag, 7);
    QCOMPARE(rec.last(ManipulatorPhase::Drag)->handle, ManipulatorHandle::MoveZ);
    // the dialog cannot move a handle that is being dragged, but may re-anchor it
    viewer.setManipulatorValue(id, 1.0);
    QVERIFY(near(viewer.manipulators().model(id)->value(), 5.0));
    viewer.updateManipulator(id, s);
    QVERIFY(near(viewer.manipulators().model(id)->value(), 5.0));
    QCOMPARE(viewer.manipulators().highlightFor(id), ManipulatorHandle::MoveZ);
    // automatic snapping without modifiers: 0.5 mm steps at this zoom
    QVERIFY(viewer.manipulatorMove(163, 0));
    QVERIFY(near(rec.last(ManipulatorPhase::Drag)->value, 6.5));
    QVERIFY(viewer.manipulatorRelease(163, 0));
    QVERIFY(!viewer.isManipulatorDragging());
    QCOMPARE(rec.count(ManipulatorPhase::DragEnd), 1);
    QVERIFY(near(rec.last(ManipulatorPhase::DragEnd)->value, 6.5));
    QVERIFY(near(viewer.manipulators().model(id)->value(), 6.5));
    // the arrow moved with the value: hovering its new place works, the old base is empty
    QVERIFY(viewer.manipulatorMove(170, 0));
    QCOMPARE(viewer.manipulators().hoveredId(), id);
    QVERIFY(!viewer.manipulatorMove(30, 0));
    // release without a drag is not consumed
    QVERIFY(!viewer.manipulatorRelease(30, 0));
    // dialog -> gizmo
    viewer.setManipulatorValue(id, 2.0);
    QVERIFY(near(viewer.manipulators().model(id)->value(), 2.0));
    viewer.removeManipulator(id);
    QCOMPARE(viewer.manipulators().count(), size_t(0));
    QVERIFY(!viewer.manipulatorMove(100, 0));
  }

  void controllerCancelRestores() {
    NullViewerContext viewer;
    viewer.setPixelSize(0.1);
    Recorder rec;
    ManipulatorId id = viewer.showManipulator(linearSpec(3.0), rec.callback());
    QVERIFY(viewer.manipulatorPress(100, 0));
    QVERIFY(viewer.manipulatorMove(180, 0, ModCtrl));
    QVERIFY(near(viewer.manipulators().model(id)->value(), 11.0));
    viewer.manipulatorCancel();
    QVERIFY(!viewer.isManipulatorDragging());
    QCOMPARE(rec.count(ManipulatorPhase::DragCancel), 1);
    QVERIFY(near(rec.last(ManipulatorPhase::DragCancel)->value, 3.0));
    QVERIFY(near(viewer.manipulators().model(id)->value(), 3.0));
  }

  void controllerPicksClosestAndAllowsReentrantRemoval() {
    NullViewerContext viewer;
    viewer.setPixelSize(0.1);
    Recorder a, b;
    ManipulatorSpec sa = linearSpec(0.0);  // along +X at y = 0
    ManipulatorSpec sb = linearSpec(0.0);
    sb.frame = gp_Ax3(gp_Pnt(0, 1.0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));  // 1 mm above (10 px)
    ManipulatorId ia = viewer.showManipulator(sa, a.callback());
    ManipulatorId ib = viewer.showManipulator(sb, b.callback());
    QVERIFY(viewer.manipulatorMove(100, -8));  // y = +0.8 mm: closer to b
    QCOMPARE(viewer.manipulators().hoveredId(), ib);
    QVERIFY(viewer.manipulatorMove(100, -2));  // y = +0.2 mm: closer to a
    QCOMPARE(viewer.manipulators().hoveredId(), ia);
    // a callback that removes its own manipulator during DragEnd (the tool switched state)
    int changes = 0;
    viewer.manipulators().setChangeListener([&] { ++changes; });
    ManipulatorId ic = viewer.showManipulator(
        [&] {
          ManipulatorSpec s = linearSpec(0.0);
          s.frame = gp_Ax3(gp_Pnt(0, -5, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
          return s;
        }(),
        [&](const ManipulatorEvent& e) {
          if (e.phase == ManipulatorPhase::DragEnd) {
            viewer.removeManipulator(e.id);
            viewer.showManipulator(linearSpec(0.0), nullptr);  // re-entrant add is fine too
          }
        });
    QVERIFY(changes >= 1);
    QVERIFY(viewer.manipulatorPress(100, 50));
    QCOMPARE(viewer.manipulators().draggingId(), ic);
    QVERIFY(viewer.manipulatorMove(120, 50, ModCtrl));
    QVERIFY(viewer.manipulatorRelease(120, 50, ModCtrl));
    QVERIFY(!viewer.manipulators().has(ic));
    QCOMPARE(viewer.manipulators().count(), size_t(3));
    // clear during dispatch is deferred as well
    ManipulatorId ic2 = viewer.showManipulator(
        [&] {
          ManipulatorSpec s = linearSpec(0.0);
          s.frame = gp_Ax3(gp_Pnt(0, -10, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
          return s;
        }(),
        [&](const ManipulatorEvent& e) {
          if (e.phase == ManipulatorPhase::Drag) viewer.clearManipulators();
        });
    QVERIFY(viewer.manipulatorPress(100, 100));
    QCOMPARE(viewer.manipulators().draggingId(), ic2);
    QVERIFY(viewer.manipulatorMove(110, 100, ModCtrl));
    QCOMPARE(viewer.manipulators().count(), size_t(0));
    QVERIFY(!viewer.isManipulatorDragging());
  }

  void toolManagerClearsManipulatorsOnFinish() {
    struct GizmoTool : ToolBase {
      GizmoTool() : ToolBase(ToolInfo{.id = "test.gizmo", .name = "Gizmo", .needsDocument = false}) {}
      void activate(ToolContext& ctx) override {
        ctx.viewer->showManipulator(linearSpec(0.0), nullptr);
        ctx.viewer->showManipulator(linearSpec(5.0), nullptr);
      }
    };
    ToolRegistry registry;
    registry.registerTool(std::make_unique<GizmoTool>());
    NullViewerContext viewer;
    ToolManager manager(registry);
    manager.setViewer(&viewer);
    QVERIFY(manager.run("test.gizmo"));
    QCOMPARE(viewer.manipulators().count(), size_t(2));
    manager.cancel();
    QCOMPARE(viewer.manipulators().count(), size_t(0));
    QVERIFY(manager.run("test.gizmo"));
    QCOMPARE(viewer.manipulators().count(), size_t(2));
    manager.accept();
    QCOMPARE(viewer.manipulators().count(), size_t(0));
  }

  void escapeAbortsTheDragBeforeTheCommand() {
    struct GizmoTool : ToolBase {
      GizmoTool() : ToolBase(ToolInfo{.id = "test.gizmo2", .name = "Gizmo", .needsDocument = false}) {}
      void activate(ToolContext& ctx) override { ctx.viewer->showManipulator(linearSpec(0.0), nullptr); }
    };
    ToolRegistry registry;
    registry.registerTool(std::make_unique<GizmoTool>());
    NullViewerContext viewer;
    viewer.setPixelSize(0.1);
    ToolManager manager(registry);
    manager.setViewer(&viewer);
    QVERIFY(manager.run("test.gizmo2"));
    const ManipulatorId id = viewer.manipulators().ids().front();
    QVERIFY(viewer.manipulatorPress(100, 0));
    QVERIFY(viewer.manipulatorMove(150, 0, ModCtrl));
    QVERIFY(near(viewer.manipulators().model(id)->value(), 5.0));
    ViewerKeyEvent esc;
    esc.key = Qt::Key_Escape;
    QVERIFY(manager.keyPress(esc));  // consumed: the drag is aborted, the command stays
    QVERIFY(!viewer.isManipulatorDragging());
    QVERIFY(manager.isActive());
    QVERIFY(near(viewer.manipulators().model(id)->value(), 0.0));
    QVERIFY(!manager.keyPress(esc));  // idle: not consumed, so the shell cancels the command
    manager.cancel();
    QVERIFY(!manager.isActive());
  }

  void namesAndMasks() {
    for (ManipulatorHandle h : {ManipulatorHandle::MoveX, ManipulatorHandle::RotY, ManipulatorHandle::PlaneZX, ManipulatorHandle::Point}) {
      QVERIFY(manipulatorHandleMask(h) != 0);
      QVERIFY(std::string(manipulatorHandleName(h)) != "None");
    }
    QCOMPARE(manipulatorHandleMask(ManipulatorHandle::None), 0u);
    QVERIFY(isMoveHandle(ManipulatorHandle::MoveZ) && !isMoveHandle(ManipulatorHandle::RotZ));
    QVERIFY(isRotationHandle(ManipulatorHandle::RotX) && isPlaneHandle(ManipulatorHandle::PlaneYZ));
    QCOMPARE(std::string(manipulatorPhaseName(ManipulatorPhase::DragEnd)), std::string("DragEnd"));
    QCOMPARE(std::string(manipulatorKindName(ManipulatorKind::Angular)), std::string("Angular"));
  }
};

QTEST_GUILESS_MAIN(ManipulatorTest)
#include "test_manipulator.moc"
