// Tests for Fillet, Chamfer, Shell, Draft, Offset Face (Press Pull), Hole.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Json.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "part/features/Fillet.h"
#include "part/features/Hole.h"
#include "part/features/Shell.h"
#include "support/TestSketch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {
bool near(double a, double b, double tol = 1e-4) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

/// 20 x 20 x 10 box body from a sketch + extrude. Returns the body id and the extrude feature.
core::ObjectId makeBox(core::Document& doc, core::Feature** extrude = nullptr, double w = 20, double h = 10) {
  TestSketch* sk = addRectSketch(doc, "XY", 0, 0, w, w);
  auto ex = std::make_unique<ExtrudeFeature>();
  ex->prop("profiles").addRef(sk->rectProfile(0));
  ex->prop("distance").setDouble(h);
  ex->prop("operation").setEnum("New Body");
  core::Feature* f = doc.addFeature(std::move(ex));
  doc.recompute();
  if (extrude) *extrude = f;
  return f->createdBodies()[0];
}

/// Ref to a vertical (Z-parallel) edge of the box that still sits on an untouched right-angle
/// corner (x and y both 0 or w), so dress-up volume expectations hold after earlier chamfers.
core::ShapeRef verticalEdge(const core::Document& doc, core::ObjectId body, double w = 20) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  auto onCorner = [w](double v) { return std::abs(v) < 1e-6 || std::abs(v - w) < 1e-6; };
  core::ShapeRef fallback;
  for (const auto& e : core::subShapes(s, TopAbs_EDGE)) {
    BRepAdaptor_Curve c(TopoDS::Edge(e));
    if (c.GetType() != GeomAbs_Line) continue;
    if (std::abs(c.Line().Direction().Z()) < 0.99) continue;
    core::ShapeRef r = core::ShapeRef::subShape(body, s, e);
    annotateRef(doc, r);
    core::Vec3 m = core::centroid(e);
    if (onCorner(m.x) && onCorner(m.y)) return r;
    if (!fallback.isValid()) fallback = r;
  }
  return fallback;
}

core::ShapeRef faceWithNormal(const core::Document& doc, core::ObjectId body, double nz, double z) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  for (const auto& f : core::subShapes(s, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && near(pl->Direction().Z(), nz, 1e-3) && near(pl->Location().Z(), z, 1e-3)) {
      core::ShapeRef r = core::ShapeRef::subShape(body, s, f);
      annotateRef(doc, r);
      return r;
    }
  }
  return {};
}
}  // namespace

class DressUpTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void filletEdgeAndNames() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    core::ShapeRef edge = verticalEdge(doc, body);
    QVERIFY(edge.isValid());
    QVERIFY(!edge.name.empty());  // "{Extrude1/Side/a|Extrude1/Side/b}"
    auto fil = std::make_unique<FilletFeature>();
    fil->prop("edges").addRef(edge);
    fil->prop("radius").setDouble(3);
    core::Feature* ff = doc.addFeature(std::move(fil));
    doc.recompute();
    QCOMPARE(ff->status(), core::FeatureStatus::Ok);
    double removed = (9 - M_PI * 9 / 4) * 10;  // (r^2 - pi r^2/4) * height
    QVERIFY(near(core::volume(doc.bodyShape(body)), 4000 - removed, 1e-3));
    // one cylindrical face named after the edge
    bool found = false;
    for (const auto& [n, s] : ff->namedShapes()) {
      if (n.rfind("Fillet1/", 0) == 0 && s.ShapeType() == TopAbs_FACE) {
        BRepAdaptor_Surface surf(TopoDS::Face(s), false);
        if (surf.GetType() == GeomAbs_Cylinder) found = true;
      }
    }
    QVERIFY(found);
    // names of the box carried through
    QVERIFY(ff->namedShapes().count("Extrude1/End"));

    // Change the box height: the edge index may change; the fillet must still resolve.
    core::Feature* ex = doc.featureAt(1);
    ex->prop("distance").setDouble(25);
    doc.notifyFeatureChanged(ex->id());
    doc.recompute();
    QCOMPARE(ff->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 20 * 20 * 25 - (9 - M_PI * 9 / 4) * 25, 1e-3));
  }

  void filletFaceAddsAllEdges() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    core::ShapeRef top = faceWithNormal(doc, body, 1, 10);
    QVERIFY(top.isValid());
    auto fil = std::make_unique<FilletFeature>();
    fil->prop("edges").addRef(top);
    fil->prop("radius").setDouble(2);
    core::Feature* ff = doc.addFeature(std::move(fil));
    doc.recompute();
    QCOMPARE(ff->status(), core::FeatureStatus::Ok);
    // four fillets of 2 mm along 20 mm edges minus corner overlap: volume decreases
    double v = core::volume(doc.bodyShape(body));
    QVERIFY(v < 4000 && v > 3900);
  }

  void filletTooLargeIsError() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    auto fil = std::make_unique<FilletFeature>();
    fil->prop("edges").addRef(verticalEdge(doc, body));
    fil->prop("radius").setDouble(50);
    core::Feature* ff = doc.addFeature(std::move(fil));
    doc.recompute();
    QCOMPARE(ff->status(), core::FeatureStatus::Error);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 4000));  // previous state kept
  }

  void chamferTypes() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    auto ch = std::make_unique<ChamferFeature>();
    ch->prop("edges").addRef(verticalEdge(doc, body));
    ch->prop("distance").setDouble(2);
    core::Feature* cf = doc.addFeature(std::move(ch));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 4000 - 0.5 * 2 * 2 * 10, 1e-3));

    auto ch2 = std::make_unique<ChamferFeature>();
    ch2->prop("edges").addRef(verticalEdge(doc, body));
    ch2->prop("chamferType").setEnum("Two Distances");
    ch2->prop("distance").setDouble(2);
    ch2->prop("distance2").setDouble(4);
    core::Feature* cf2 = doc.addFeature(std::move(ch2));
    doc.recompute();
    QCOMPARE(cf2->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 4000 - 0.5 * 2 * 2 * 10 - 0.5 * 2 * 4 * 10, 1e-3));

    auto ch3 = std::make_unique<ChamferFeature>();
    ch3->prop("edges").addRef(verticalEdge(doc, body));
    ch3->prop("chamferType").setEnum("Distance and Angle");
    ch3->prop("distance").setDouble(2);
    ch3->prop("angle").setDouble(45);
    core::Feature* cf3 = doc.addFeature(std::move(ch3));
    doc.recompute();
    QCOMPARE(cf3->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 4000 - 0.5 * 2 * 2 * 10 - 0.5 * 2 * 4 * 10 - 0.5 * 2 * 2 * 10, 1e-3));
  }

  void shellRemoveFaceAndHollow() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    auto sh = std::make_unique<ShellFeature>();
    sh->prop("faces").addRef(faceWithNormal(doc, body, 1, 10));
    sh->prop("thickness").setDouble(2);
    core::Feature* sf = doc.addFeature(std::move(sh));
    doc.recompute();
    QCOMPARE(sf->status(), core::FeatureStatus::Ok);
    // open box: outer 20x20x10 minus inner 16x16x8
    QVERIFY(near(core::volume(doc.bodyShape(body)), 4000 - 16 * 16 * 8, 1e-3));

    // hollow a fresh box with no openings
    core::Document doc2;
    core::ObjectId b2 = makeBox(doc2);
    auto hol = std::make_unique<ShellFeature>();
    hol->prop("bodies").addRef(core::ShapeRef::body(b2));
    hol->prop("thickness").setDouble(1);
    core::Feature* hf = doc2.addFeature(std::move(hol));
    doc2.recompute();
    QCOMPARE(hf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc2.bodyShape(b2)), 4000 - 18 * 18 * 8, 1e-3));

    // outside shell
    core::Document doc3;
    core::ObjectId b3 = makeBox(doc3);
    auto out = std::make_unique<ShellFeature>();
    out->prop("faces").addRef(faceWithNormal(doc3, b3, 1, 10));
    out->prop("thickness").setDouble(2);
    out->prop("direction").setEnum("Outside");
    core::Feature* of = doc3.addFeature(std::move(out));
    doc3.recompute();
    QCOMPARE(of->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc3.bodyShape(b3)), 24 * 24 * 12 - 4000, 1e-3));
  }

  void draftFaces() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    auto dr = std::make_unique<DraftFeature>();
    dr->prop("faces").addRef(faceWithNormal(doc, body, 1, 10).owner == body ? core::ShapeRef() : core::ShapeRef());
    dr->prop("faces").setRefs({});
    // side face with normal +X at x = 20
    const TopoDS_Shape& s = doc.bodyShape(body);
    for (const auto& f : core::subShapes(s, TopAbs_FACE)) {
      auto pl = core::facePlane(f);
      if (pl && near(pl->Direction().X(), 1, 1e-3)) dr->prop("faces").addRef(core::ShapeRef::subShape(body, s, f));
    }
    dr->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    dr->prop("angle").setDouble(10);
    core::Feature* df = doc.addFeature(std::move(dr));
    doc.recompute();
    QCOMPARE(df->status(), core::FeatureStatus::Ok);
    double v = core::volume(doc.bodyShape(body));
    QVERIFY(!near(v, 4000, 1e-3));  // face tilted: volume changed
    QVERIFY(v > 3000 && v < 5000);
  }

  void offsetFacePressPull() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    auto off = std::make_unique<OffsetFaceFeature>();
    off->prop("faces").addRef(faceWithNormal(doc, body, 1, 10));
    off->prop("distance").setDouble(5);
    core::Feature* of = doc.addFeature(std::move(off));
    doc.recompute();
    QCOMPARE(of->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 20 * 20 * 15, 1e-3));
    QVERIFY(!of->outputShape().IsNull());

    auto in = std::make_unique<OffsetFaceFeature>();
    in->prop("faces").addRef(faceWithNormal(doc, body, 1, 15));
    in->prop("distance").setDouble(-7);
    core::Feature* inf = doc.addFeature(std::move(in));
    doc.recompute();
    QCOMPARE(inf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 20 * 20 * 8, 1e-3));
  }

  void protoHoleShapes() {
    std::string err;
    HoleFeature::Params p;
    TopoDS_Shape simple = HoleFeature::makeProtoHole(p, &err);
    QVERIFY2(!simple.IsNull(), err.c_str());
    QVERIFY(near(core::volume(simple), M_PI * 3.3 * 3.3 * 10, 1e-3));
    p.type = HoleFeature::Counterbore;
    TopoDS_Shape cb = HoleFeature::makeProtoHole(p, &err);
    QVERIFY2(!cb.IsNull(), err.c_str());
    QVERIFY(near(core::volume(cb), M_PI * 3.3 * 3.3 * 10 + M_PI * (5.5 * 5.5 - 3.3 * 3.3) * 6, 1e-3));
    p.type = HoleFeature::Countersink;
    TopoDS_Shape cs = HoleFeature::makeProtoHole(p, &err);
    QVERIFY2(!cs.IsNull(), err.c_str());
    QVERIFY(core::volume(cs) > core::volume(simple));
    p.type = HoleFeature::Simple;
    p.drillPoint = HoleFeature::Angled;
    TopoDS_Shape drill = HoleFeature::makeProtoHole(p, &err);
    QVERIFY2(!drill.IsNull(), err.c_str());
    QVERIFY(core::volume(drill) > core::volume(simple));
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(drill, mn, mx));
    QVERIFY(mn.z < -10);  // tip below the depth
    // top extension adds exactly a cylinder of the top diameter above z = 0
    p.drillPoint = HoleFeature::Flat;
    p.topExtension = 1.5;
    TopoDS_Shape ext = HoleFeature::makeProtoHole(p, &err);
    QVERIFY2(!ext.IsNull(), err.c_str());
    QVERIFY(near(core::volume(ext), M_PI * 3.3 * 3.3 * 11.5, 1e-3));
    p.type = HoleFeature::Counterbore;
    TopoDS_Shape cbExt = HoleFeature::makeProtoHole(p, &err);
    QVERIFY2(!cbExt.IsNull(), err.c_str());
    QVERIFY(near(core::volume(cbExt), core::volume(cb) + M_PI * 5.5 * 5.5 * 1.5, 1e-3));
    QVERIFY(core::bbox(cbExt, mn, mx) && near(mx.z, 1.5, 1e-6) && near(mn.z, -10, 1e-6));
    p.topExtension = 0;
    p.cboreDiameter = 1;  // invalid
    p.type = HoleFeature::Counterbore;
    QVERIFY(HoleFeature::makeProtoHole(p, &err).IsNull());
    QVERIFY(!err.empty());
  }

  void holeEditKeepsDownstreamRefsQuiet() {
    // DOC-04: editing a hole must not make a later fillet on the box (an edge of
    // Extrude1/End) report "re-matched by geometry", neither after the edit nor after undoing it.
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(faceWithNormal(doc, body, 1, 10));
    {
      auto pl = core::resolvePlane(doc, sk->prop("plane").ref());
      QVERIFY(pl);
      gp_Vec d(pl->Location(), gp_Pnt(10, 10, 10));
      sk->points.push_back(TestSketch::Point{d.Dot(gp_Vec(pl->XDirection())), d.Dot(gp_Vec(pl->YDirection()))});
    }
    TestSketch* s = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
    auto hole = std::make_unique<HoleFeature>();
    hole->prop("points").addRef(s->pointRef(0));
    hole->prop("diameter").setDouble(4);
    hole->prop("depth").setDouble(5);
    core::Feature* hf = doc.addFeature(std::move(hole));
    doc.recompute();
    QVERIFY2(hf->status() == core::FeatureStatus::Ok, hf->statusMessage().c_str());
    QVERIFY(hf->namedShapes().count("Extrude1/End"));
    // a top outer edge, picked after the hole (as a user would)
    core::ShapeRef top;
    {
      const TopoDS_Shape& shape = doc.bodyShape(body);
      for (const auto& e : core::subShapes(shape, TopAbs_EDGE)) {
        BRepAdaptor_Curve c(TopoDS::Edge(e));
        core::Vec3 m = core::centroid(e);
        if (c.GetType() == GeomAbs_Line && near(m.z, 10) && near(m.y, 0)) {
          top = core::ShapeRef::subShape(body, shape, e);
          annotateRef(doc, top);
        }
      }
    }
    QVERIFY(top.isValid());
    QVERIFY2(!top.name.empty(), "the edge has a history name");
    auto fil = std::make_unique<FilletFeature>();
    fil->prop("edges").addRef(top);
    fil->prop("radius").setDouble(1);
    core::Feature* ff = doc.addFeature(std::move(fil));
    doc.recompute();
    QVERIFY2(ff->status() == core::FeatureStatus::Ok, ff->statusMessage().c_str());
    for (double dia : {6.0, 4.0, 8.0, 4.0}) {
      hf->prop("diameter").setDouble(dia);
      doc.notifyFeatureChanged(hf->id());
      doc.recompute();
      QVERIFY2(hf->status() == core::FeatureStatus::Ok, hf->statusMessage().c_str());
      QVERIFY2(hf->namedShapes().count("Extrude1/End"), "the hole carries the box's names");
      QVERIFY2(ff->status() == core::FeatureStatus::Ok,
               qPrintable(QString("diameter %1: %2").arg(dia).arg(core::qs(ff->statusMessage()))));
    }
    // the box itself changes size (the referenced edge moves): still no warning
    core::Feature* ex = doc.featureAt(1);
    ex->prop("distance").setDouble(14);
    doc.notifyFeatureChanged(ex->id());
    doc.recompute();
    QVERIFY2(ff->status() == core::FeatureStatus::Ok, ff->statusMessage().c_str());
    ex->prop("distance").setDouble(10);
    doc.notifyFeatureChanged(ex->id());
    doc.recompute();
    QVERIFY2(ff->status() == core::FeatureStatus::Ok, ff->statusMessage().c_str());
  }

  void holesAtSketchPointsAndThroughAll() {
    core::Document doc;
    core::ObjectId body = makeBox(doc);
    // sketch on the top face: the plane origin is the world origin projected onto the face
    // plane ((0,0,10) here); express the points relative to it so both lie on the face.
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(faceWithNormal(doc, body, 1, 10));  // planar face as sketch plane
    {
      auto pl = core::resolvePlane(doc, sk->prop("plane").ref());
      QVERIFY(pl);
      auto local = [&](double x, double y) {
        gp_Vec d(pl->Location(), gp_Pnt(x, y, 10));
        return TestSketch::Point{d.Dot(gp_Vec(pl->XDirection())), d.Dot(gp_Vec(pl->YDirection()))};
      };
      sk->points.push_back(local(5, 5));
      sk->points.push_back(local(15, 15));
    }
    TestSketch* s = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
    doc.recompute();
    QCOMPARE(s->status(), core::FeatureStatus::Ok);

    auto hole = std::make_unique<HoleFeature>();
    hole->prop("points").addRef(s->pointRef(0));
    hole->prop("points").addRef(s->pointRef(1));
    hole->prop("diameter").setDouble(4);
    hole->prop("depth").setDouble(5);
    core::Feature* hf = doc.addFeature(std::move(hole));
    doc.recompute();
    QVERIFY2(hf->status() == core::FeatureStatus::Ok, hf->statusMessage().c_str());
    QVERIFY2(near(core::volume(doc.bodyShape(body)), 4000 - 2 * M_PI * 4 * 5, 1e-3),
             qPrintable(QString::number(core::volume(doc.bodyShape(body)))));
    QVERIFY(!hf->outputShape().IsNull());

    // through all, counterbore
    auto h2 = std::make_unique<HoleFeature>();
    h2->prop("points").addRef(s->pointRef(0));
    h2->prop("holeType").setEnum("Counterbore");
    h2->prop("diameter").setDouble(4);
    h2->prop("cboreDiameter").setDouble(8);
    h2->prop("cboreDepth").setDouble(2);
    h2->prop("extentType").setEnum("All");
    core::Feature* hf2 = doc.addFeature(std::move(h2));
    doc.recompute();
    QVERIFY2(hf2->status() == core::FeatureStatus::Ok, hf2->statusMessage().c_str());
    double expected = 4000 - M_PI * 4 * 5 /*second simple hole*/ - M_PI * 4 * 10 /*through*/ - M_PI * (16 - 4) * 2;
    QVERIFY2(near(core::volume(doc.bodyShape(body)), expected, 1e-3), qPrintable(QString::number(core::volume(doc.bodyShape(body)))));
  }
};

QTEST_GUILESS_MAIN(DressUpTest)
#include "test_dressup.moc"
