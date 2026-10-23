// Production features P2: Pocket Hole feature (Kreg 15° exit point at mid-thickness, pilot
// through the edge face, step depth from the collar rule, mating-part pilot, script words,
// save / load).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "part/features/PocketHole.h"
#include "part/standards/PocketJigs.h"
#include "support/TestSketch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopoDS.hxx>

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;
namespace st = fabricad::part::standards;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

core::ObjectId board(core::Document& doc, double x0, double w, double t) {
  TestSketch* sk = addRectSketch(doc, "XY", x0, 0, x0 + w, 80);
  auto ex = std::make_unique<ExtrudeFeature>();
  ex->prop("profiles").addRef(sk->rectProfile(0));
  ex->prop("distance").setDouble(t);
  ex->prop("operation").setEnum("New Body");
  core::Feature* f = doc.addFeature(std::move(ex));
  doc.recompute();
  return f->createdBodies().front();
}

core::ShapeRef planarFace(const core::Document& doc, core::ObjectId body, const gp_Dir& n, double offset) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  for (const TopoDS_Shape& f : core::subShapes(s, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && pl->Direction().IsEqual(n, 1e-6) && std::abs(gp_Vec(pl->Location().XYZ()).Dot(gp_Vec(n)) - offset) < 1e-6)
      return core::ShapeRef::subShape(body, s, f);
  }
  return {};
}

/// The edge of the top face (z = t) lying on x = x.
core::ShapeRef topEdgeAtX(const core::Document& doc, core::ObjectId body, double x, double t) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  for (const TopoDS_Shape& e : core::subShapes(s, TopAbs_EDGE)) {
    BRepAdaptor_Curve c(TopoDS::Edge(e));
    gp_Pnt a = c.Value(c.FirstParameter()), b = c.Value(c.LastParameter());
    if (near(a.X(), x) && near(b.X(), x) && near(a.Z(), t) && near(b.Z(), t)) return core::ShapeRef::subShape(body, s, e);
  }
  return {};
}

bool inVoid(const TopoDS_Shape& body, const gp_Pnt& p) {
  BRepClass3d_SolidClassifier c(body, p, 1e-7);
  return c.State() == TopAbs_OUT;
}
}  // namespace

class PocketHoleTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void jigTables() {
    QVERIFY(st::findPocketJig("kreg15"));
    QVERIFY(near(st::findPocketJig("kreg")->angle, 15));
    QVERIFY(near(st::findPocketJig("castle6")->angle, 6));
    QVERIFY(near(st::pocketScrewLength(19), 31.75));
    QVERIFY(near(st::pocketScrewLength(12), 25.4));
    QVERIFY(near(st::pocketScrewLength(38), 63.5));
    auto g = st::pocketGeometry(19, 15, 31.75);
    QVERIFY(near(g.axisToExit * std::sin(15 * M_PI / 180), 9.5, 1e-9));
    QVERIFY(near(g.stepDepth, g.axisToExit - 31.75 / 2, 1e-9));
  }

  void kreg15ExitAtMidThickness() {
    core::Document doc;
    const double T = 19;
    core::ObjectId b = board(doc, 0, 200, T);
    const double v0 = core::volume(doc.bodyShape(b));
    auto ph = std::make_unique<PocketHoleFeature>();
    ph->prop("face").setRef(planarFace(doc, b, gp::DZ(), T));
    ph->prop("edges").addRef(topEdgeAtX(doc, b, 0, T));
    ph->prop("count").setInt(2);
    ph->prop("edgeDistance").setDouble(20);
    auto* f = static_cast<PocketHoleFeature*>(doc.addFeature(std::move(ph)));
    doc.recompute();
    QVERIFY2(f->status() == core::FeatureStatus::Ok, f->statusMessage().c_str());
    QCOMPARE(f->placed().size(), size_t(2));
    const TopoDS_Shape& s = doc.bodyShape(b);
    QVERIFY(core::volume(s) < v0 - 100);
    for (const auto& p : f->placed()) {
      // exit point on the edge face x = 0 at mid-thickness (within 0.05 mm)
      QVERIFY2(std::abs(p.exit.X()) < 0.05, qPrintable(QString::number(p.exit.X())));
      QVERIFY2(std::abs(p.exit.Z() - T / 2) < 0.05, qPrintable(QString::number(p.exit.Z())));
      QVERIFY(near(p.thickness, T, 1e-6));
      // the axis is 15 deg below the entry face, heading for the edge
      QVERIFY(near(std::asin(-p.axis.Z()) * 180 / M_PI, 15, 1e-6));
      QVERIFY(p.axis.X() < 0);
      // the entry lies on the top face
      QVERIFY(near(p.entry.Z(), T, 1e-6));
      // step depth from the collar rule and screw from the chart
      QVERIFY(near(p.screwLength, 31.75));
      QVERIFY(near(p.stepDepth, p.entry.Distance(p.exit) - 31.75 / 2, 1e-6));
      // the pilot exits: void just inside the edge face on the axis, and in the step
      QVERIFY(inVoid(s, p.exit.Translated(gp_Vec(p.axis) * -0.3)));
      QVERIFY(inVoid(s, p.entry.Translated(gp_Vec(p.axis) * (p.stepDepth - 0.5))));
      // material beside the pilot (3 mm off the axis, mid way) is still there
      QVERIFY(!inVoid(s, p.exit.Translated(gp_Vec(p.axis) * -8).Translated(gp_Vec(0, 3, 0))));
    }
    QVERIFY(near(f->placed()[0].exit.Y(), 20, 1e-6) || near(f->placed()[0].exit.Y(), 60, 1e-6));
    const std::string c = f->callout();
    QVERIFY2(c.find("POCKET HOLE 15") != std::string::npos && c.find("Kreg") != std::string::npos && c.find("1 1/4") != std::string::npos,
             c.c_str());
    // save / load: same geometry
    QJsonObject saved = doc.toJson();
    core::Document back;
    std::string err;
    QVERIFY2(back.fromJson(saved, &err), err.c_str());
    back.recompute();
    double vb = 0;
    for (core::ObjectId id : back.liveBodies()) vb += core::volume(back.bodyShape(id));
    QVERIFY(near(vb, core::volume(s), 1e-6));
  }

  void exitOffsetAndThicknessWords() {
    core::Document doc;
    core::ObjectId b = board(doc, 0, 100, 19);
    auto ph = std::make_unique<PocketHoleFeature>();
    ph->prop("face").setRef(planarFace(doc, b, gp::DZ(), 19));
    ph->prop("edges").addRef(topEdgeAtX(doc, b, 0, 19));
    QVERIFY(ph->applyScriptOption("jig", "kreg15"));
    QVERIFY(ph->applyScriptOption("screw", "32"));
    QVERIFY(!ph->applyScriptOption("jig", "dowelmax"));
    ph->prop("count").setInt(1);
    ph->prop("exitOffset").setDouble(2);
    auto* f = static_cast<PocketHoleFeature*>(doc.addFeature(std::move(ph)));
    doc.recompute();
    QVERIFY2(f->status() == core::FeatureStatus::Ok, f->statusMessage().c_str());
    QCOMPARE(f->placed().size(), size_t(1));
    QVERIFY(std::abs(f->placed()[0].exit.Z() - 11.5) < 0.05);  // 2 mm towards the entry face
    QVERIFY(near(f->placed()[0].exit.Y(), 40, 1e-6));            // single hole: edge midpoint
    QVERIFY(near(f->placed()[0].screwLength, 32));
    // Castle preset switches angle / pilot / step
    QVERIFY(f->applyScriptOption("jig", "castle6"));
    QVERIFY(near(f->prop("angle").toDouble(), 6));
    QVERIFY(near(f->prop("stepDiameter").toDouble(), 7.9));
  }

  void matingPartPilot() {
    core::Document doc;
    core::ObjectId a = board(doc, 0, 150, 19);
    core::ObjectId m = board(doc, -19, 19, 80);  // upright board touching the edge face x = 0... at x in [-19, 0]
    const double vm = core::volume(doc.bodyShape(m));
    auto ph = std::make_unique<PocketHoleFeature>();
    ph->prop("face").setRef(planarFace(doc, a, gp::DZ(), 19));
    ph->prop("edges").addRef(topEdgeAtX(doc, a, 0, 19));
    ph->prop("drillMating").setBool(true);
    ph->prop("matingBody").setRef(core::ShapeRef::body(m));
    auto* f = static_cast<PocketHoleFeature*>(doc.addFeature(std::move(ph)));
    doc.recompute();
    QVERIFY2(f->status() == core::FeatureStatus::Ok, f->statusMessage().c_str());
    const double pilotVol = core::volume(doc.bodyShape(m));
    QVERIFY2(pilotVol < vm - 1.0, qPrintable(QString("%1 %2").arg(pilotVol).arg(vm)));
    QVERIFY(pilotVol > vm - 2 * M_PI * 1.8 * 1.8 * 20);  // only pilots, two holes
  }
};

QTEST_GUILESS_MAIN(PocketHoleTest)
#include "test_pocket_hole.moc"
