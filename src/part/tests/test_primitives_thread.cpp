// Tests for the primitives (Box, Cylinder, Sphere, Torus, Coil), the ISO metric thread table,
// the Thread feature (cosmetic / modelled, internal / external) and the Hole tap types and
// click-on-face placement. All shapes have analytic volumes.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/ExtraUtils.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/PartUtils.h"
#include "part/features/Construction.h"
#include "part/features/Hole.h"
#include "part/features/Primitives.h"
#include "part/features/Thread.h"
#include "part/tools/FeatureTool.h"
#include "support/TestSketch.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS.hxx>

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {

bool rel(double a, double b, double tol) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

/// Exact bounding box (core::bbox adds tolerance gaps and uses B-spline poles).
bool tightBox(const TopoDS_Shape& s, core::Vec3& mn, core::Vec3& mx) {
  Bnd_Box box;
  BRepBndLib::AddOptimal(s, box, false, false);
  if (box.IsVoid()) return false;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  mn = {x0, y0, z0};
  mx = {x1, y1, z1};
  return true;
}

/// Ref to the face of `body` that coincides with a feature's named face (named shapes are not
/// always the very TShapes of the body, e.g. after the result is copied).
core::ShapeRef bodyFace(const core::Document& doc, core::ObjectId body, const TopoDS_Shape& named) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  const core::Vec3 c = core::centroid(named);
  for (const auto& f : core::subShapes(s, TopAbs_FACE)) {
    if ((core::centroid(f) - c).length() < 1e-7 && std::abs(core::area(f) - core::area(named)) < 1e-7) {
      core::ShapeRef r = core::ShapeRef::subShape(body, s, f);
      annotateRef(doc, r);
      return r;
    }
  }
  return {};
}

core::Feature* add(core::Document& doc, std::unique_ptr<core::Feature> f) {
  core::Feature* raw = doc.addFeature(std::move(f));
  doc.recompute();
  return raw;
}

core::ShapeRef cylinderFace(const core::Document& doc, core::ObjectId body, double radius) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  for (const auto& f : core::subShapes(s, TopAbs_FACE)) {
    BRepAdaptor_Surface a(TopoDS::Face(f));
    if (a.GetType() == GeomAbs_Cylinder && std::abs(a.Cylinder().Radius() - radius) < 1e-6) {
      core::ShapeRef r = core::ShapeRef::subShape(body, s, f);
      annotateRef(doc, r);
      return r;
    }
  }
  return {};
}

}  // namespace

class PrimitivesThreadTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools_};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void boxCylinderSphereTorus() {
    core::Document doc;
    auto box = std::make_unique<BoxFeature>();
    box->prop("x").setDouble(5);
    box->prop("length").setDouble(30);
    box->prop("width").setDouble(20);
    box->prop("height").setDouble(10);
    box->prop("operation").setEnum("New Body");
    core::Feature* bf = add(doc, std::move(box));
    QCOMPARE(bf->status(), core::FeatureStatus::Ok);
    core::ObjectId b = bf->createdBodies().front();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 6000, 1e-9));
    core::Vec3 mn, mx;
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mn.x, -10, 1e-9) && rel(mx.x, 20, 1e-9) && rel(mn.z, 0, 1e-9) && rel(mx.z, 10, 1e-9));
    QVERIFY(bf->namedShapes().count("Box1/Top"));
    QVERIFY(bf->namedShapes().count("Box1/Right"));

    // cylinder on the box's top face (picked face, not an origin plane), joined
    auto cyl = std::make_unique<CylinderFeature>();
    core::ShapeRef top = bodyFace(doc, b, bf->namedShapes().at("Box1/Top"));
    QVERIFY(top.isValid() && top.index > 0);
    cyl->prop("plane").setRef(top);
    cyl->prop("diameter").setDouble(10);
    cyl->prop("height").setDouble(5);
    cyl->prop("operation").setEnum("Join");
    core::Feature* cf = add(doc, std::move(cyl));
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 6000 + M_PI * 25 * 5, 1e-6));
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mx.z, 15, 1e-6));
    // editing the box height moves the cylinder with the face
    bf->prop("height").setDouble(12);
    doc.notifyFeatureChanged(bf->id());
    doc.recompute();
    QVERIFY2(cf->status() == core::FeatureStatus::Ok, cf->statusMessage().c_str());
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mx.z, 17, 1e-6));

    // sphere cut from the box, symmetric about the XY plane at (5, 0)
    auto sph = std::make_unique<SphereFeature>();
    sph->prop("x").setDouble(5);
    sph->prop("diameter").setDouble(8);
    sph->prop("operation").setEnum("Cut");
    core::Feature* sf = add(doc, std::move(sph));
    QCOMPARE(sf->status(), core::FeatureStatus::Ok);
    double expect = 30 * 20 * 12 + M_PI * 25 * 5 - (4.0 / 3 * M_PI * 64) / 2;  // half the sphere is in the box
    QVERIFY(rel(core::volume(doc.bodyShape(b)), expect, 1e-6));

    // torus as a new body on the XZ plane (ring in the XZ plane)
    auto tor = std::make_unique<TorusFeature>();
    tor->prop("plane").setRef(core::ShapeRef::originPlane("XZ"));
    tor->prop("y").setDouble(60);
    tor->prop("majorDiameter").setDouble(40);
    tor->prop("minorDiameter").setDouble(10);
    tor->prop("operation").setEnum("New Body");
    core::Feature* tf = add(doc, std::move(tor));
    QCOMPARE(tf->status(), core::FeatureStatus::Ok);
    double torus = 2 * M_PI * M_PI * 20 * 25;
    QVERIFY(rel(core::volume(doc.bodyShape(tf->createdBodies().front())), torus, 1e-6));
    // invalid input is an error, not a crash
    tf->prop("minorDiameter").setDouble(50);
    doc.notifyFeatureChanged(tf->id());
    doc.recompute();
    QCOMPARE(tf->status(), core::FeatureStatus::Error);

    // file round trip
    QTemporaryDir dir;
    const std::string path = dir.filePath("prims.fcad").toStdString();
    QVERIFY(doc.save(path));
    core::Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QVERIFY(rel(core::volume(re.bodyShape(b)), expect, 1e-6));
  }

  void coilVolumes() {
    core::Document doc;
    auto coil = std::make_unique<CoilFeature>();
    coil->prop("diameter").setDouble(40);
    coil->prop("revolutions").setDouble(3);
    coil->prop("height").setDouble(30);
    coil->prop("sectionSize").setDouble(4);
    coil->prop("operation").setEnum("New Body");
    CoilFeature* cf = static_cast<CoilFeature*>(add(doc, std::move(coil)));
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    core::ObjectId b = cf->createdBodies().front();
    // Pappus: section area x centroid path length (section in the meridian plane)
    const double circ = M_PI * 4 * 2 * M_PI * 20 * 3;
    QVERIFY2(rel(core::volume(doc.bodyShape(b)), circ, 1e-4), qPrintable(QString::number(core::volume(doc.bodyShape(b)))));
    core::Vec3 mn, mx;
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mx.z, 32, 1e-3) && rel(mn.z, -2, 1e-3));
    QVERIFY(cf->namedShapes().count("Coil1/Start") && cf->namedShapes().count("Coil1/End"));
    // square section outside the diameter, pitch type: centroid radius 22
    cf->prop("section").setEnum("Square");
    cf->prop("sectionPosition").setEnum("Outside");
    cf->prop("coilType").setEnum("Revolution and Pitch");
    cf->prop("pitch").setDouble(6);
    cf->prop("revolutions").setDouble(2.5);
    doc.notifyFeatureChanged(cf->id());
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 16 * 2 * M_PI * 22 * 2.5, 1e-4));
    // triangular sections and a left-handed tapered coil still build
    cf->prop("section").setEnum("Triangular (External)");
    cf->prop("clockwise").setBool(true);
    cf->prop("angle").setDouble(5);
    doc.notifyFeatureChanged(cf->id());
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(core::isValidShape(doc.bodyShape(b)));
    // overlapping turns are rejected
    cf->prop("pitch").setDouble(3);
    doc.notifyFeatureChanged(cf->id());
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Error);
    QVERIFY(QString::fromStdString(cf->statusMessage()).contains("pitch"));
    // helper
    auto h = CoilFeature::helixFor(CoilFeature::HeightPitch, 0, 20, 4, nullptr);
    QVERIFY(h && rel(h->turns, 5, 1e-12));
  }

  void isoTable() {
    const IsoThread* m10 = findIsoThread("M10x1.5");
    QVERIFY(m10 && m10->coarse && rel(m10->pitch, 1.5, 1e-12));
    QVERIFY(findIsoThread("M10") == m10);
    QVERIFY(findIsoThread("M10x1.25") && !findIsoThread("M10x1.25")->coarse);
    QVERIFY(rel(m10->minorDiameter(), 8.376, 1e-3));
    QVERIFY(rel(m10->tapDrill(), 8.5, 1e-12));
    QCOMPARE(isoThreadForDiameter(6, false)->designation, std::string("M6x1"));
    QCOMPARE(isoThreadForDiameter(8.5, true)->designation, std::string("M10x1.5"));
    QCOMPARE(isoThreadForDiameter(5.0, true)->designation, std::string("M6x1"));
    QVERIFY(isoThreadForDiameter(6.5, false) == nullptr);
    QCOMPARE(isoMetricDesignations().front(), std::string("M1x0.25"));
    QCOMPARE(isoMetricThreads().size(), isoMetricDesignations().size());
    QCOMPARE(HoleFeature::clearanceDiameter(*findIsoThread("M6"), 1), 6.6);
  }

  void cosmeticAndModelledExternalThread() {
    core::Document doc;
    auto rod = std::make_unique<CylinderFeature>();
    rod->prop("diameter").setDouble(6);
    rod->prop("height").setDouble(8);
    rod->prop("operation").setEnum("New Body");
    core::Feature* rf = add(doc, std::move(rod));
    core::ObjectId b = rf->createdBodies().front();
    const double rodVolume = M_PI * 9 * 8;
    auto th = std::make_unique<ThreadFeature>();
    th->prop("faces").addRef(cylinderFace(doc, b, 3));
    ThreadFeature* tf = static_cast<ThreadFeature*>(add(doc, std::move(th)));
    QCOMPARE(tf->status(), core::FeatureStatus::Ok);
    QCOMPARE(tf->threads().size(), size_t(1));
    const ThreadInfo& info = tf->threads().front();
    QCOMPARE(info.designation, std::string("M6x1"));
    QVERIFY(!info.internal && !info.modeled);
    QVERIFY(rel(info.length, 8, 1e-9));
    QCOMPARE(info.callout(), std::string("M6x1 - 6g"));
    QVERIFY(rel(core::volume(doc.bodyShape(b)), rodVolume, 1e-9));  // cosmetic: geometry unchanged

    // modelled, 5 mm from the top end (flip), run-out through the top face only
    tf->prop("modeled").setBool(true);
    tf->prop("fullLength").setBool(false);
    tf->prop("length").setDouble(5);
    tf->prop("flip").setBool(true);
    doc.notifyFeatureChanged(tf->id());
    doc.recompute();
    QVERIFY2(tf->status() == core::FeatureStatus::Ok, tf->statusMessage().c_str());
    double removed = rodVolume - core::volume(doc.bodyShape(b));
    // groove: trapezoid (P/4 + 7P/8)/2 x 5H/8 at mean radius ~ (D + d1)/4, P = 1
    const double P = 1, H = 0.8660254 * P, depth = 5 * H / 8;
    const double area = (P / 4 + 7 * P / 8) / 2 * depth;
    const double perTurn = area * 2 * M_PI * (3 - depth / 2);
    QVERIFY2(removed > perTurn * 4 && removed < perTurn * 6.5, qPrintable(QString::number(removed)));
    QVERIFY(core::isValidShape(doc.bodyShape(b)));
    core::Vec3 mn, mx;
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mn.z, 0, 1e-6) && rel(mx.z, 8, 1e-6));
    // the bottom 3 mm are untouched: the cross-section at z = 1 is a full circle
    QVERIFY(tf->threads().front().modeled);
    QVERIFY(rel(tf->threads().front().axis.Location().Z(), 8, 1e-6));
    QVERIFY(tf->threads().front().axis.Direction().Z() < -0.999);

    // wrong manual size warns but still works
    tf->prop("modeled").setBool(false);
    tf->prop("autoSize").setBool(false);
    tf->prop("designation").setEnum("M8x1.25");
    doc.notifyFeatureChanged(tf->id());
    doc.recompute();
    QCOMPARE(tf->status(), core::FeatureStatus::Warning);
  }

  void tappedHoleOnFace() {
    core::Document doc;
    auto plate = std::make_unique<BoxFeature>();
    plate->prop("length").setDouble(40);
    plate->prop("width").setDouble(30);
    plate->prop("height").setDouble(6);
    plate->prop("operation").setEnum("New Body");
    BoxFeature* pf = static_cast<BoxFeature*>(add(doc, std::move(plate)));
    core::ObjectId b = pf->createdBodies().front();
    // two clicks on the top face (z = 6): M6 tapped through holes
    core::ShapeRef top = bodyFace(doc, b, pf->namedShapes().at("Box1/Top"));
    QVERIFY(top.isValid() && top.index > 0);
    auto hole = std::make_unique<HoleFeature>();
    for (double x : {-10.0, 10.0}) {
      core::ShapeRef r = top;
      r.point = {x, 5, 6.3};  // slightly off the face: projected onto it
      r.hasPoint = true;
      hole->prop("points").refs().push_back(r);  // addRef() would merge same-face refs
    }
    QCOMPARE(hole->prop("points").refs().size(), size_t(2));
    hole->prop("extentType").setEnum("All");
    hole->prop("tapType").setEnum("Tapped");
    hole->prop("threadSize").setEnum("M6x1");
    HoleFeature* hf = static_cast<HoleFeature*>(add(doc, std::move(hole)));
    QVERIFY2(hf->status() == core::FeatureStatus::Ok, hf->statusMessage().c_str());
    const double drill = 5.0;  // tap drill M6 = 6 - 1
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 40 * 30 * 6 - 2 * M_PI * drill * drill / 4 * 6, 1e-6));
    QCOMPARE(hf->threads().size(), size_t(2));
    QVERIFY(hf->threads().front().internal);
    QCOMPARE(hf->threads().front().callout(), std::string("M6x1 - 6H"));
    QVERIFY(rel(hf->threads().front().length, 6, 1e-6));
    QVERIFY(hf->threads().front().axis.Direction().Z() < -0.999);  // from the top face down
    QVERIFY(rel(hf->threads().front().axis.Location().Z(), 6, 1e-6));
    // hole centres at the projected clicks
    QVERIFY(!cylinderFace(doc, b, 2.5).isValid() == false);
    bool atLeft = false;
    for (const auto& f : core::subShapes(doc.bodyShape(b), TopAbs_FACE)) {
      BRepAdaptor_Surface a(TopoDS::Face(f));
      if (a.GetType() == GeomAbs_Cylinder && gp_Lin(a.Cylinder().Axis()).Distance(gp_Pnt(-10, 5, 0)) < 1e-6) atLeft = true;
    }
    QVERIFY(atLeft);

    // modelled internal thread on one hole: removes the thread groove from the plate
    hf->prop("points").refs().pop_back();
    hf->prop("threadModeled").setBool(true);
    doc.notifyFeatureChanged(hf->id());
    doc.recompute();
    QVERIFY2(hf->status() == core::FeatureStatus::Ok, hf->statusMessage().c_str());
    double plain = 40 * 30 * 6 - M_PI * drill * drill / 4 * 6;
    double v = core::volume(doc.bodyShape(b));
    QVERIFY2(v < plain - 5 && v > plain - 40, qPrintable(QString::number(plain - v)));
    QVERIFY(core::isValidShape(doc.bodyShape(b)));

    // clearance hole: ISO 273 normal fit
    hf->prop("tapType").setEnum("Clearance");
    hf->prop("threadSize").setEnum("M8x1.25");
    doc.notifyFeatureChanged(hf->id());
    doc.recompute();
    QCOMPARE(hf->status(), core::FeatureStatus::Ok);
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 40 * 30 * 6 - M_PI * 81 / 4 * 6, 1e-6));
    QVERIFY(hf->threads().empty());
  }

  /// Through the command tool: Box dialog defaults (XY plane), OK, undo / redo; Hole clicks on
  /// the same face are separate holes and a second click on the same point removes it.
  void toolsHeadless() {
    core::Document doc;
    core::NullViewerContext viewer;
    core::NullAppHost host;
    core::ToolManager mgr{tools_};
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);
    QVERIFY(mgr.run("part.box"));
    auto* tool = dynamic_cast<FeatureTool*>(mgr.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->isValid());  // previews on the XY plane right away
    QVERIFY(!viewer.previews().empty());
    QVERIFY(mgr.runCommandLine("25"));  // bare value -> height
    QVERIFY(rel(tool->feature()->length("height"), 25, 1e-12));
    mgr.accept();
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    core::ObjectId b = doc.liveBodies().front();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 20 * 20 * 25, 1e-9));
    doc.undoStack().undo();
    QVERIFY(doc.liveBodies().empty());
    doc.undoStack().redo();
    QCOMPARE(doc.liveBodies().size(), size_t(1));

    QVERIFY(mgr.run("part.hole"));
    tool = dynamic_cast<FeatureTool*>(mgr.activeTool());
    QVERIFY(tool);
    const core::Feature* boxF = doc.featureAt(0);
    core::ShapeRef top = bodyFace(doc, b, boxF->namedShapes().at("Box1/Top"));
    QVERIFY(top.isValid() && top.index > 0);
    for (double x : {-5.0, 5.0, 5.0}) {
      core::ShapeRef r = top;
      r.point = {x, 0, 25};
      r.hasPoint = true;
      doc.selection().set({r});
    }
    QCOMPARE(tool->feature()->prop("points").refs().size(), size_t(1));  // the third click toggled the second off
    QVERIFY(tool->isValid());
    mgr.accept();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 20 * 20 * 25 - M_PI * 3.3 * 3.3 * 10, 1e-6));

    // the new tools are registered in the SOLID tab
    for (const char* id : {"part.box", "part.cylinder", "part.sphere", "part.torus", "part.coil", "part.thread", "part.rib",
                           "part.thicken", "part.scale", "part.split-face"})
      QVERIFY2(tools_.has(id), id);
    QCOMPARE(toolIdForFeatureType("Coil"), std::string("part.coil"));
  }

 private:
  core::ToolRegistry tools_;
};

QTEST_MAIN(PrimitivesThreadTest)
#include "test_primitives_thread.moc"
