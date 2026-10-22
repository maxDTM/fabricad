// Tests for the on-canvas manipulators of the feature dialogs (part/tools/FeatureManipulators.h)
// driven through FeatureTool with the headless NullViewerContext: every binding is anchored on
// the geometry it drives, a drag of the gizmo writes the property and refreshes the preview,
// typed values move the gizmo, and Fusion's Join -> Cut switch follows the arrow into material.
//
// Camera: NullViewerContext maps pixel (x, y) onto its view plane at pixelSize mm per pixel
// (x right, y down). The Front view looks along +Y with Z up; the Top view looks down -Z.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Manipulator.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/PartModule.h"
#include "part/tools/FeatureManipulators.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/FeatureValueBox.h"
#include "support/TestSketch.h"

#include <QtTest/QtTest>
#include <QLineEdit>

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;
using core::ManipulatorHandle;
using core::ManipulatorKind;
using core::ManipulatorModel;

namespace {

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
bool near(const gp_Pnt& a, const gp_Pnt& b, double tol = 1e-6) { return a.Distance(b) <= tol; }
bool sameDir(const gp_Dir& a, const gp_Dir& b) { return a.IsEqual(b, 1e-6); }

struct Rig {
  core::ToolRegistry registry;
  core::Document doc;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager manager{registry};
  Rig() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    part::registerModule(ctx);
    registerTestSketch();
    manager.setDocument(&doc);
    manager.setViewer(&viewer);
    manager.setHost(&host);
    viewer.setPixelSize(0.1);
    viewer.setStandardView(core::StandardView::Front, false);
  }
  core::ToolContext& ctx() { return *manager.activeContext(); }
  FeatureTool* tool() { return dynamic_cast<FeatureTool*>(manager.activeTool()); }
  /// The gizmo bound to a property of the active feature tool (nullptr when not shown).
  const ManipulatorModel* gizmo(const char* property) {
    FeatureTool* t = tool();
    const FeatureTool::ManipulatorLink* link = t ? t->manipulatorFor(property) : nullptr;
    return link ? viewer.manipulators().model(link->id) : nullptr;
  }
  QPoint pixel(const gp_Pnt& p) const {
    double x, y;
    viewer.worldToScreen(p, x, y);
    return QPoint(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
  }
  /// Press on a gizmo at `from`, move to `to` and (optionally) release there.
  bool drag(const gp_Pnt& from, const gp_Pnt& to, int modifiers = core::ModCtrl, bool release = true) {
    const QPoint a = pixel(from), b = pixel(to);
    if (!viewer.manipulatorPress(a.x(), a.y(), modifiers)) return false;
    viewer.manipulatorMove(b.x(), b.y(), modifiers);
    if (release) viewer.manipulatorRelease(b.x(), b.y(), modifiers);
    return true;
  }
  /// A 10x10x10 box from a rectangle sketch on XY, extruded `distance` along +Z (negative: down).
  core::ObjectId makeBox(double distance = 10.0) {
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 10, 10);
    doc.selection().set(sk->rectProfile(0));
    if (!manager.run("part.extrude")) return core::kInvalidId;
    manager.runCommandLine(std::to_string(distance));
    manager.accept();
    const core::Feature* f = doc.featureAt(doc.featureCount() - 1);
    return f->createdBodies().empty() ? core::kInvalidId : f->createdBodies().front();
  }
  const core::Feature* lastFeature() const { return doc.featureAt(doc.featureCount() - 1); }
  core::ShapeRef topFace(core::ObjectId body) const {
    const core::Feature* f = lastFeature();
    return core::ShapeRef::subShape(body, doc.bodyShape(body), f->namedShapes().at(f->name() + "/End"));
  }
  /// The edge of `body` whose midpoint is `mid`.
  core::ShapeRef edgeAt(core::ObjectId body, const gp_Pnt& mid) const {
    const TopoDS_Shape& s = doc.bodyShape(body);
    for (const TopoDS_Shape& e : core::subShapes(s, TopAbs_EDGE))
      if (near(core::centroid(e).toPnt(), mid, 1e-6)) return core::ShapeRef::subShape(body, s, e);
    return {};
  }
};

}  // namespace

class ManipulatorsTest : public QObject {
  Q_OBJECT
 private slots:
  void providersAreRegistered() {
    for (const char* t : {"Extrude", "Revolve", "Fillet", "Chamfer", "Shell", "OffsetFace", "OffsetPlane", "Hole"})
      QVERIFY2(hasManipulatorProvider(t), t);
    QVERIFY(!hasManipulatorProvider("Sweep"));
  }

  void extrudeArrowDragsTheDistanceBothWays() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    FeatureTool* tool = rig.tool();
    QVERIFY(tool && tool->feature());
    const double d0 = tool->feature()->length("distance");
    QVERIFY(d0 > 0);
    // arrow at the profile centroid along the profile normal, placed at the current distance
    const ManipulatorModel* arrow = rig.gizmo("distance");
    QVERIFY(arrow);
    QCOMPARE(arrow->spec().kind, ManipulatorKind::Linear);
    QVERIFY(near(arrow->spec().frame.Location(), gp_Pnt(5, 5, 0)));
    QVERIFY(sameDir(arrow->spec().frame.Direction(), gp_Dir(0, 0, 1)));
    QVERIFY(near(arrow->value(), d0));
    QVERIFY(near(arrow->handlePoint(ManipulatorHandle::MoveZ, 0.1), gp_Pnt(5, 5, d0)));
    // taper ring on the end face
    const ManipulatorModel* taper = rig.gizmo("taperAngle");
    QVERIFY(taper);
    QCOMPARE(taper->spec().kind, ManipulatorKind::Angular);
    QVERIFY(near(taper->spec().frame.Location(), gp_Pnt(5, 5, d0)));
    QVERIFY(near(taper->value(), 0.0));
    QCOMPARE(rig.viewer.manipulators().count(), size_t(2));

    // gizmo -> dialog: grab the shaft 3 mm above its base and pull it up 5 mm (Ctrl = no snapping)
    const size_t previewsBefore = rig.viewer.previews().size();
    QVERIFY(previewsBefore > 0);
    QVERIFY(rig.drag(gp_Pnt(5, 5, d0 + 3), gp_Pnt(5, 5, d0 + 8), core::ModCtrl, false));
    QVERIFY(tool->isManipulatorDragging());
    QVERIFY(near(tool->feature()->length("distance"), d0 + 5));
    QVERIFY(!rig.viewer.previews().empty());  // live preview refreshed during the drag
    QVERIFY(tool->isValid());
    const QPoint end = rig.pixel(gp_Pnt(5, 5, d0 + 8));
    QVERIFY(rig.viewer.manipulatorRelease(end.x(), end.y(), core::ModCtrl));
    QVERIFY(!tool->isManipulatorDragging());
    QVERIFY(near(tool->feature()->length("distance"), d0 + 5));
    QVERIFY(near(rig.gizmo("distance")->value(), d0 + 5));
    QVERIFY(near(rig.gizmo("taperAngle")->spec().frame.Location(), gp_Pnt(5, 5, d0 + 5)));  // the ring followed
    QVERIFY(near(core::volume(tool->lastOutcome().tool), 100.0 * (d0 + 5), 1e-6));

    // dialog -> gizmo: a typed value moves the arrow; an expression is replaced by the next drag
    QVERIFY(tool->setNumeric(rig.ctx(), "distance", "20"));
    QVERIFY(near(rig.gizmo("distance")->value(), 20.0));
    QVERIFY(near(rig.gizmo("taperAngle")->spec().frame.Location(), gp_Pnt(5, 5, 20)));
    QVERIFY(tool->setNumeric(rig.ctx(), "distance", "2*7 mm"));
    QCOMPARE(tool->feature()->prop("distance").expression(), std::string("2*7 mm"));
    QVERIFY(near(rig.gizmo("distance")->value(), 14.0));
    QVERIFY(rig.drag(gp_Pnt(5, 5, 17), gp_Pnt(5, 5, 20)));
    QVERIFY(near(tool->feature()->length("distance"), 17.0));
    QVERIFY(tool->feature()->prop("distance").expression().empty());

    // automatic snapping without modifiers: 0.5 mm steps at 0.1 mm/px
    QVERIFY(rig.drag(gp_Pnt(5, 5, 20), gp_Pnt(5, 5, 22.3), core::ModNone));
    QVERIFY(near(tool->feature()->length("distance"), 19.5));

    // the arrow may be dragged through the profile: the distance flips sign (other side)
    QVERIFY(rig.drag(gp_Pnt(5, 5, 22), gp_Pnt(5, 5, -3)));
    QVERIFY(near(tool->feature()->length("distance"), -5.5));
    QVERIFY(near(rig.gizmo("distance")->handlePoint(ManipulatorHandle::MoveZ, 0.1), gp_Pnt(5, 5, -5.5)));

    // two sides: a second arrow pointing the other way, with its own taper ring
    QVERIFY(tool->setNumeric(rig.ctx(), "distance", "10"));
    QVERIFY(rig.manager.runCommandLine("direction=Two Sides"));
    const ManipulatorModel* arrow2 = rig.gizmo("distance2");
    QVERIFY(arrow2);
    QVERIFY(sameDir(arrow2->spec().frame.Direction(), gp_Dir(0, 0, -1)));
    QVERIFY(near(arrow2->spec().frame.Location(), gp_Pnt(5, 5, 0)));
    QVERIFY(rig.gizmo("taperAngle2"));
    QCOMPARE(rig.viewer.manipulators().count(), size_t(4));
    const double d2 = tool->feature()->length("distance2");
    QVERIFY(rig.drag(gp_Pnt(5, 5, -d2 - 3), gp_Pnt(5, 5, -d2 - 7)));
    QVERIFY(near(tool->feature()->length("distance2"), d2 + 4));
    QVERIFY(near(tool->feature()->length("distance"), 10.0));  // side 1 untouched

    // symmetric, whole length: one arrow at half the distance (scale 0.5)
    QVERIFY(rig.manager.runCommandLine("direction=Symmetric"));
    QVERIFY(rig.manager.runCommandLine("symmetricMeasure=Whole Length"));
    QVERIFY(!rig.gizmo("distance2"));
    QVERIFY(!rig.gizmo("taperAngle"));
    QCOMPARE(rig.viewer.manipulators().count(), size_t(1));
    QVERIFY(near(rig.gizmo("distance")->value(), 5.0));
    QVERIFY(near(tool->manipulatorFor("distance")->scale, 0.5));
    QVERIFY(rig.drag(gp_Pnt(5, 5, 8), gp_Pnt(5, 5, 11)));  // handle 5 -> 8 => whole length 16
    QVERIFY(near(tool->feature()->length("distance"), 16.0));

    rig.manager.accept();
    QCOMPARE(rig.viewer.manipulators().count(), size_t(0));  // cleared with the command
    QCOMPARE(rig.doc.featureCount(), 2);
    QVERIFY(near(core::volume(rig.doc.bodyShape(rig.lastFeature()->createdBodies().front())), 100.0 * 16.0, 1e-6));
  }

  void extrudeDraggedIntoMaterialBecomesCut() {
    Rig rig;
    const core::ObjectId body = rig.makeBox();
    QVERIFY(body != core::kInvalidId);
    // a smaller profile on the same plane: the default extrusion goes up into the box
    TestSketch* sk = addRectSketch(rig.doc, "XY", 2, 2, 8, 8);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    FeatureTool* tool = rig.tool();
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Join"));  // touches a body
    const double d0 = tool->feature()->length("distance");
    // drag end inside the box: Join -> Cut
    QVERIFY(rig.drag(gp_Pnt(5, 5, d0 + 3), gp_Pnt(5, 5, 8)));
    QVERIFY(near(tool->feature()->length("distance"), 5.0));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Cut"));
    QVERIFY(tool->isValid());
    QVERIFY(near(core::volume(tool->lastOutcome().tool), 36.0 * 5.0, 1e-6));  // the (red) tool solid
    // pulled out below the box: back to Join
    QVERIFY(rig.drag(gp_Pnt(5, 5, 8), gp_Pnt(5, 5, -5)));
    QVERIFY(near(tool->feature()->length("distance"), -8.0));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Join"));
    // an explicit choice stops the automatic switching
    QVERIFY(rig.manager.runCommandLine("operation=Cut"));
    QVERIFY(rig.drag(gp_Pnt(5, 5, -5), gp_Pnt(5, 5, 7)));
    QVERIFY(near(tool->feature()->length("distance"), 4.0));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Cut"));
    rig.manager.accept();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 1000.0 - 36.0 * 4.0, 1e-6));
  }

  void revolveRingDragsTheAngle() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 2, 0, 8, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.revolve"));
    FeatureTool* tool = rig.tool();
    tool->feature()->prop("axis").setRef(core::ShapeRef::originAxis("Y"));
    tool->updatePreview(rig.ctx());
    QVERIFY(!rig.gizmo("angle"));  // Full revolution: nothing to drag
    QVERIFY(rig.manager.runCommandLine("extentType=Angle"));
    const ManipulatorModel* ring = rig.gizmo("angle");
    QVERIFY(ring);
    QCOMPARE(ring->spec().kind, ManipulatorKind::Angular);
    // centred on the axis at the profile's height, radius = distance of the profile centroid, zero towards the profile
    QVERIFY(near(ring->spec().frame.Location(), gp_Pnt(0, 5, 0)));
    QVERIFY(sameDir(ring->spec().frame.Direction(), gp_Dir(0, 1, 0)));
    QVERIFY(sameDir(ring->spec().frame.XDirection(), gp_Dir(1, 0, 0)));
    QVERIFY(near(ring->spec().size, 5.0));
    QVERIFY(near(ring->value(), 90.0));
    const gp_Pnt knob = ring->handlePoint(ManipulatorHandle::RotZ, 0.1);
    QVERIFY(near(knob, gp_Pnt(0, 5, -5)));  // 90 degrees right-handed about +Y
    // turn the knob another quarter (Front view looks along +Y, square onto the ring)
    QVERIFY(rig.drag(knob, gp_Pnt(-5, 5, 0)));
    QVERIFY(near(tool->feature()->angle("angle"), 180.0));
    QVERIFY(near(rig.gizmo("angle")->value(), 180.0));
    QVERIFY(tool->isValid());
    QVERIFY(near(core::volume(tool->lastOutcome().tool), M_PI * (64.0 - 4.0) * 10.0 * 0.5, 1e-4));
    // dialog -> gizmo
    QVERIFY(tool->setNumeric(rig.ctx(), "angle", "45"));
    QVERIFY(near(rig.gizmo("angle")->value(), 45.0));
    // two sides: a second ring about the reversed axis
    QVERIFY(rig.manager.runCommandLine("direction=Two Sides"));
    const ManipulatorModel* ring2 = rig.gizmo("angle2");
    QVERIFY(ring2);
    QVERIFY(sameDir(ring2->spec().frame.Direction(), gp_Dir(0, -1, 0)));
    QVERIFY(near(ring2->value(), tool->feature()->angle("angle2")));
    // symmetric: one ring at half the angle
    QVERIFY(rig.manager.runCommandLine("direction=Symmetric"));
    QVERIFY(!rig.gizmo("angle2"));
    QVERIFY(near(rig.gizmo("angle")->value(), 22.5));
    QVERIFY(near(tool->manipulatorFor("angle")->scale, 0.5));
    rig.manager.cancel();
    QCOMPARE(rig.viewer.manipulators().count(), size_t(0));
  }

  void filletAndChamferArrowsPointIntoTheMaterial() {
    Rig rig;
    rig.viewer.setStandardView(core::StandardView::Top, false);
    rig.viewer.setPixelSize(0.01);
    const core::ObjectId body = rig.makeBox();
    // the top front edge (y = 0, z = 10): inward = between the +Y and -Z face normals
    rig.doc.selection().set(rig.edgeAt(body, gp_Pnt(5, 0, 10)));
    QVERIFY(rig.manager.run("part.fillet"));
    FeatureTool* tool = rig.tool();
    const ManipulatorModel* arrow = rig.gizmo("radius");
    QVERIFY(arrow);
    QCOMPARE(arrow->spec().kind, ManipulatorKind::Linear);
    QVERIFY(near(arrow->spec().frame.Location(), gp_Pnt(5, 0, 10)));
    const gp_Dir inward = arrow->spec().frame.Direction();
    QVERIFY(sameDir(inward, gp_Dir(0, 1, -1)));
    QVERIFY(arrow->spec().minValue && near(*arrow->spec().minValue, 0.0));
    const double r0 = tool->feature()->length("radius");
    QVERIFY(near(arrow->value(), r0));
    // pull the arrow 3 mm further into the material (auto snap: 0.05 mm steps at this zoom).
    // The auto-sized arrow is 70 px = 0.7 mm long here, so grab it 0.3 mm past its base.
    const gp_Pnt mid(5, 0, 10);
    QVERIFY(near(arrow->effectiveSize(0.01), 0.7));
    QVERIFY(rig.drag(mid.Translated(gp_Vec(inward) * (r0 + 0.3)), mid.Translated(gp_Vec(inward) * (r0 + 3.3)), core::ModNone));
    QVERIFY(near(tool->feature()->length("radius"), r0 + 3));
    QVERIFY(tool->isValid());
    // it cannot go negative
    QVERIFY(rig.drag(mid.Translated(gp_Vec(inward) * (r0 + 3.3)), mid.Translated(gp_Vec(inward) * (-20.0)), core::ModNone));
    QVERIFY(near(tool->feature()->length("radius"), 0.0));
    QVERIFY(near(rig.gizmo("radius")->value(), 0.0));
    rig.manager.cancel();

    rig.doc.selection().set(rig.edgeAt(body, gp_Pnt(5, 0, 10)));
    QVERIFY(rig.manager.run("part.chamfer"));
    const ManipulatorModel* ch = rig.gizmo("distance");
    QVERIFY(ch);
    QVERIFY(near(ch->spec().frame.Location(), gp_Pnt(5, 0, 10)));
    QVERIFY(sameDir(ch->spec().frame.Direction(), gp_Dir(0, 1, -1)));
    QVERIFY(near(ch->value(), rig.tool()->feature()->length("distance")));
    rig.manager.cancel();
  }

  void shellOffsetFaceOffsetPlaneAndHoleArrows() {
    Rig rig;
    const core::ObjectId body = rig.makeBox();
    const core::ShapeRef top = rig.topFace(body);

    // Shell, inside: the arrow on the removed face points into the body
    rig.doc.selection().set(top);
    QVERIFY(rig.manager.run("part.shell"));
    FeatureTool* tool = rig.tool();
    QCOMPARE(tool->feature()->type(), std::string("Shell"));
    const ManipulatorModel* th = rig.gizmo("thickness");
    QVERIFY(th);
    QVERIFY(near(th->spec().frame.Location(), gp_Pnt(5, 5, 10)));
    QVERIFY(sameDir(th->spec().frame.Direction(), gp_Dir(0, 0, -1)));
    QVERIFY(th->spec().minValue && near(*th->spec().minValue, 0.0));
    const double t0 = tool->feature()->length("thickness");
    QVERIFY(rig.drag(gp_Pnt(5, 5, 10 - t0 - 3), gp_Pnt(5, 5, 10 - t0 - 5)));
    QVERIFY(near(tool->feature()->length("thickness"), t0 + 2));
    QVERIFY(tool->isValid());
    QVERIFY(rig.manager.runCommandLine("direction=Outside"));
    QVERIFY(sameDir(rig.gizmo("thickness")->spec().frame.Direction(), gp_Dir(0, 0, 1)));
    rig.manager.cancel();

    // Press Pull on a face = Offset Face: arrow along the face normal, negative pushes in
    rig.doc.selection().set(top);
    QVERIFY(rig.manager.run("part.press-pull"));
    tool = rig.tool();
    QCOMPARE(tool->feature()->type(), std::string("OffsetFace"));
    const ManipulatorModel* of = rig.gizmo("distance");
    QVERIFY(of);
    QVERIFY(near(of->spec().frame.Location(), gp_Pnt(5, 5, 10)));
    QVERIFY(sameDir(of->spec().frame.Direction(), gp_Dir(0, 0, 1)));
    const double o0 = tool->feature()->length("distance");
    QVERIFY(rig.drag(gp_Pnt(5, 5, o0 + 13), gp_Pnt(5, 5, 7)));  // handle at 10 + o0 -> 10 - 6
    QVERIFY(near(tool->feature()->length("distance"), -6.0));
    QVERIFY(tool->isValid());
    rig.manager.accept();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 100.0 * 4.0, 1e-6));

    // Offset Plane from the XY origin plane
    rig.doc.selection().set(core::ShapeRef::originPlane("XY"));
    QVERIFY(rig.manager.run("construct.offset-plane"));
    tool = rig.tool();
    QCOMPARE(tool->feature()->type(), std::string("OffsetPlane"));
    const ManipulatorModel* op = rig.gizmo("distance");
    QVERIFY(op);
    QVERIFY(near(op->spec().frame.Location(), gp_Pnt(0, 0, 0)));
    QVERIFY(sameDir(op->spec().frame.Direction(), gp_Dir(0, 0, 1)));
    const double p0 = tool->feature()->length("distance");
    QVERIFY(rig.drag(gp_Pnt(0, 0, p0 + 3), gp_Pnt(0, 0, p0 + 8)));
    QVERIFY(near(tool->feature()->length("distance"), p0 + 5));
    QVERIFY(rig.manager.runCommandLine("flip=1"));
    QVERIFY(sameDir(rig.gizmo("distance")->spec().frame.Direction(), gp_Dir(0, 0, -1)));
    rig.manager.cancel();
  }

  void holeDepthArrow() {
    Rig rig;
    const core::ObjectId body = rig.makeBox(-10.0);  // box below the XY plane
    QVERIFY(body != core::kInvalidId);
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    sk->points.push_back({5, 5});
    auto* pts = static_cast<TestSketch*>(rig.doc.addFeature(std::move(sk)));
    rig.doc.recompute();
    rig.doc.selection().set(pts->pointRef(0));
    QVERIFY(rig.manager.run("part.hole"));
    FeatureTool* tool = rig.tool();
    QCOMPARE(tool->feature()->refs("points").size(), size_t(1));
    const ManipulatorModel* depth = rig.gizmo("depth");
    QVERIFY(depth);
    QVERIFY(near(depth->spec().frame.Location(), gp_Pnt(5, 5, 0)));
    QVERIFY(sameDir(depth->spec().frame.Direction(), gp_Dir(0, 0, -1)));  // into the material
    QVERIFY(depth->spec().minValue && near(*depth->spec().minValue, 0.0));
    const double h0 = tool->feature()->length("depth");
    QVERIFY(rig.drag(gp_Pnt(5, 5, -h0 - 3), gp_Pnt(5, 5, -h0 - 1)));
    QVERIFY(near(tool->feature()->length("depth"), h0 - 2));
    QVERIFY(tool->isValid());
    QVERIFY(rig.manager.runCommandLine("extentType=All"));
    QVERIFY(!rig.gizmo("depth"));  // through all: nothing to drag
    rig.manager.cancel();
  }

  void editingAFeatureShowsAndDragsItsGizmos() {
    Rig rig;
    const core::ObjectId body = rig.makeBox();
    const core::Feature* extrude = rig.lastFeature();
    QVERIFY(rig.manager.run("part.edit-feature", {std::to_string(extrude->id())}));
    FeatureTool* tool = rig.tool();
    QVERIFY(tool && tool->isEditing());
    const ManipulatorModel* arrow = rig.gizmo("distance");
    QVERIFY(arrow);
    QVERIFY(near(arrow->value(), 10.0));
    QVERIFY(near(arrow->spec().frame.Location(), gp_Pnt(5, 5, 0)));
    const int undoBefore = rig.doc.undoStack().index();
    QVERIFY(rig.drag(gp_Pnt(5, 5, 13), gp_Pnt(5, 5, 18)));
    QVERIFY(near(tool->feature()->length("distance"), 15.0));
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 1500.0, 1e-6));  // the document recomputed live
    QCOMPARE(rig.doc.undoStack().index(), undoBefore);                  // ... without an undo step yet
    rig.manager.accept();
    QCOMPARE(rig.doc.undoStack().index(), undoBefore + 1);
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 1500.0, 1e-6));
    rig.doc.undoStack().undo();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 1000.0, 1e-6));
  }

  void escapeAbortsADragAndKeepsTheCommand() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    FeatureTool* tool = rig.tool();
    const double d0 = tool->feature()->length("distance");
    QVERIFY(rig.drag(gp_Pnt(5, 5, d0 + 3), gp_Pnt(5, 5, d0 + 9), core::ModCtrl, false));
    QVERIFY(near(tool->feature()->length("distance"), d0 + 6));
    core::ViewerKeyEvent esc;
    esc.key = Qt::Key_Escape;
    QVERIFY(rig.manager.keyPress(esc));
    QVERIFY(rig.manager.isActive());
    QVERIFY(!tool->isManipulatorDragging());
    QVERIFY(near(tool->feature()->length("distance"), d0));  // restored
    QVERIFY(near(rig.gizmo("distance")->value(), d0));
    rig.manager.cancel();
  }

  /// Round-2 features: Box height arrow, Cylinder diameter, Thicken thickness; the feature value
  /// box (dynamic input) sits at the tip of the gizmo that drives the same property.
  void primitivesAndThickenArrows() {
    Rig rig;
    rig.doc.selection().set(core::ShapeRef::originPlane("XY"));
    QVERIFY(rig.manager.run("part.box"));
    FeatureTool* tool = rig.tool();
    QCOMPARE(tool->feature()->type(), std::string("Box"));
    const ManipulatorModel* h = rig.gizmo("height");
    QVERIFY(h);
    QVERIFY(near(h->spec().frame.Location(), gp_Pnt(0, 0, 0)));
    QVERIFY(sameDir(h->spec().frame.Direction(), gp_Dir(0, 0, 1)));
    const double h0 = tool->feature()->length("height");
    QVERIFY(near(h->value(), h0));
    QVERIFY(tool->valueBox() && tool->valueBox()->isActive());
    QCOMPARE(tool->valueBox()->propertyName(), std::string("height"));
    const core::Vec3 anchor = tool->valueBox()->model().field(0).anchor;
    QVERIFY(near(gp_Pnt(anchor.x, anchor.y, anchor.z), gp_Pnt(0, 0, h0)));  // at the arrow tip
    QVERIFY(rig.drag(gp_Pnt(0, 0, h0), gp_Pnt(0, 0, h0 + 5)));
    QVERIFY(near(tool->feature()->length("height"), h0 + 5));
    QVERIFY(tool->isValid());
    const core::Vec3 moved = tool->valueBox()->model().field(0).anchor;
    QVERIFY(near(gp_Pnt(moved.x, moved.y, moved.z), gp_Pnt(0, 0, h0 + 5)));
    QVERIFY(rig.manager.runCommandLine("x=30"));
    QVERIFY(near(rig.gizmo("height")->spec().frame.Location(), gp_Pnt(30, 0, 0)));
    QVERIFY(rig.manager.runCommandLine("direction=Symmetric"));
    QVERIFY(near(rig.gizmo("height")->value(), (h0 + 5) / 2));  // half the height, like Extrude
    rig.manager.accept();
    QVERIFY(near(core::volume(rig.doc.bodyShape(rig.lastFeature()->createdBodies().front())), 20.0 * 20.0 * (h0 + 5), 1e-6));

    // Cylinder: diameter arrow is radial, its handle on the surface
    rig.doc.selection().set(core::ShapeRef::originPlane("XY"));
    QVERIFY(rig.manager.run("part.cylinder"));
    tool = rig.tool();
    const ManipulatorModel* d = rig.gizmo("diameter");
    QVERIFY(d);
    QVERIFY(sameDir(d->spec().frame.Direction(), gp_Dir(1, 0, 0)));
    const double d0 = tool->feature()->length("diameter");
    QVERIFY(near(d->value(), d0 / 2));
    QVERIFY(rig.drag(gp_Pnt(d0 / 2, 0, 0), gp_Pnt(d0 / 2 + 4, 0, 0)));
    QVERIFY(near(tool->feature()->length("diameter"), d0 + 8));
    rig.manager.cancel();

    // Thicken the top face of a box: arrow along the face normal at the face centre
    const core::ObjectId body = rig.makeBox();
    rig.doc.selection().set(rig.topFace(body));
    QVERIFY(rig.manager.run("part.thicken"));
    tool = rig.tool();
    QCOMPARE(tool->feature()->type(), std::string("Thicken"));
    const ManipulatorModel* th = rig.gizmo("thickness");
    QVERIFY(th);
    QVERIFY(near(th->spec().frame.Location(), gp_Pnt(5, 5, 10)));
    QVERIFY(sameDir(th->spec().frame.Direction(), gp_Dir(0, 0, 1)));
    const double t0 = tool->feature()->length("thickness");
    QVERIFY(rig.drag(gp_Pnt(5, 5, 10 + t0), gp_Pnt(5, 5, 10 + t0 + 3)));
    QVERIFY(near(tool->feature()->length("thickness"), t0 + 3));
    QVERIFY(tool->isValid());
    rig.manager.cancel();
  }

  // UX-33: values typed into the Box dialog fields reach the feature and the preview; keys typed
  // into a field never go to the on-canvas Height box.
  void boxDialogTypedValues() {
    Rig rig;
    rig.doc.selection().set(core::ShapeRef::originPlane("XY"));
    QVERIFY(rig.manager.run("part.box"));
    FeatureTool* tool = rig.tool();
    std::unique_ptr<QWidget> w(tool->createDialog(rig.ctx(), nullptr));
    QVERIFY(w);
    w->show();
    QApplication::setActiveWindow(w.get());
    auto* length = w->findChild<QLineEdit*>("prop_length");
    auto* width = w->findChild<QLineEdit*>("prop_width");
    QVERIFY(length && width);
    const double h0 = tool->feature()->length("height");
    length->setFocus();
    length->selectAll();
    QTest::keyClicks(length, "40");
    QTest::keyClick(length, Qt::Key_Tab);
    QVERIFY(near(tool->feature()->length("length"), 40));
    width->setFocus();
    width->selectAll();
    QTest::keyClicks(width, "25");
    // the same digit as a viewer key while the field has focus: not consumed by the value box
    core::ViewerKeyEvent ev;
    ev.key = Qt::Key_7;
    ev.text = "7";
    QVERIFY(!tool->keyPress(rig.ctx(), ev));
    QTest::keyClick(width, Qt::Key_Tab);
    QVERIFY(near(tool->feature()->length("width"), 25));
    QVERIFY(near(tool->feature()->length("height"), h0));
    QVERIFY(tool->isValid());
    QVERIFY(!tool->lastOutcome().previews.empty());
    QVERIFY(near(core::volume(tool->lastOutcome().previews.front().shape), 40.0 * 25.0 * h0, 1e-6));
    rig.manager.accept();
    QVERIFY(near(core::volume(rig.doc.bodyShape(rig.lastFeature()->createdBodies().front())), 40.0 * 25.0 * h0, 1e-6));
  }

  // UX-32: Move/Copy shows a triad at the selection centre (Free Move by default); its handles
  // drive dx/dy/dz and rx/ry/rz and it follows the moved bodies.
  void moveBodyTriad() {
    Rig rig;
    QVERIFY(hasManipulatorProvider("MoveBody"));
    const core::ObjectId body = rig.makeBox();
    rig.doc.selection().set(core::ShapeRef::body(body));
    QVERIFY(rig.manager.run("part.move"));
    FeatureTool* tool = rig.tool();
    QVERIFY(tool);
    QCOMPARE(tool->feature()->prop("moveType").toInt(), 3);  // Free Move
    const ManipulatorModel* t = rig.gizmo("dx");
    QVERIFY(t);
    QCOMPARE(t->spec().kind, ManipulatorKind::Triad);
    QVERIFY(near(t->spec().frame.Location(), gp_Pnt(5, 5, 5)));
    QVERIFY(t->spec().handles & core::kHandleRotZ);
    // a real drag of the X arrow (Ctrl: no snapping)
    const double px = rig.viewer.pixelSize(gp_Pnt(5, 5, 5));
    const gp_Pnt tip = t->handlePoint(ManipulatorHandle::MoveX, px);
    QVERIFY(rig.drag(tip, tip.Translated(gp_Vec(6, 0, 0))));
    QVERIFY(near(tool->feature()->length("dx"), 6, 0.2));
    QVERIFY(near(tool->feature()->length("dy"), 0));
    // the triad moved with the body after the drag
    QVERIFY(near(rig.gizmo("dx")->spec().frame.Location().X(), 5 + tool->feature()->length("dx"), 1e-6));
    // events: Z ring, then a cancelled Y drag that restores the start values
    const FeatureTool::ManipulatorLink* link = tool->manipulatorFor("dx");
    QVERIFY(link && link->triad);
    core::ManipulatorEvent e;
    e.id = link->id;
    e.phase = core::ManipulatorPhase::DragBegin;
    e.handle = ManipulatorHandle::RotZ;
    tool->onManipulatorEvent(rig.ctx(), e);
    e.phase = core::ManipulatorPhase::Drag;
    e.value = 90;
    tool->onManipulatorEvent(rig.ctx(), e);
    e.phase = core::ManipulatorPhase::DragEnd;
    tool->onManipulatorEvent(rig.ctx(), e);
    QVERIFY(near(tool->feature()->angle("rz"), 90));
    e.phase = core::ManipulatorPhase::DragBegin;
    e.handle = ManipulatorHandle::MoveY;
    tool->onManipulatorEvent(rig.ctx(), e);
    e.phase = core::ManipulatorPhase::Drag;
    e.value = 4;
    tool->onManipulatorEvent(rig.ctx(), e);
    QVERIFY(near(tool->feature()->length("dy"), 4));
    e.phase = core::ManipulatorPhase::DragCancel;
    tool->onManipulatorEvent(rig.ctx(), e);
    QVERIFY(near(tool->feature()->length("dy"), 0));
    QVERIFY(tool->isValid());
    const double dx = tool->feature()->length("dx");
    rig.manager.accept();
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(rig.doc.bodyShape(body), mn, mx));
    QVERIFY(near(mn.x, dx, 1e-6) && near(mx.x, 10 + dx, 1e-6));  // a square turned 90 deg about its centre
  }
};

QTEST_MAIN(ManipulatorsTest)
#include "test_manipulators.moc"
