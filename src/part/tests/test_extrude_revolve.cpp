// Tests for Extrude, Revolve and the boolean/naming machinery.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "part/features/Revolve.h"
#include "support/TestSketch.h"

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {
bool near(double a, double b, double tol = 1e-4) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }
}

class ExtrudeRevolveTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void extrudeNewBodyAndNames() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 20, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = doc.addFeature(std::move(ex));
    doc.recompute();
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QCOMPARE(f->createdBodies().size(), size_t(1));
    core::ObjectId body = f->createdBodies()[0];
    QVERIFY(near(core::volume(doc.bodyShape(body)), 20 * 10 * 5));
    // history names
    const auto& names = f->namedShapes();
    QVERIFY(names.count("Extrude1/Start"));
    QVERIFY(names.count("Extrude1/End"));
    // side faces are named by the sketch curve id ("Side/c<id>") so they survive sketch edits
    int sides = 0;
    for (const auto& [k, v] : names) sides += k.rfind("Extrude1/Side/", 0) == 0;
    QCOMPARE(sides, 4);
    // end face is at z = 5
    core::Vec3 c = core::centroid(names.at("Extrude1/End"));
    QVERIFY(near(c.z, 5));
    // edge names derived from faces
    bool anyEdge = false;
    for (const auto& [n, s] : names)
      if (n.front() == '{') anyEdge = true;
    QVERIFY(anyEdge);
    QVERIFY(!f->outputShape().IsNull());  // tool solid stored
  }

  void extrudeJoinCutIntersect() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 20, 20);
    auto base = std::make_unique<ExtrudeFeature>();
    base->prop("profiles").addRef(sk->rectProfile(0));
    base->prop("distance").setDouble(10);
    base->prop("operation").setEnum("New Body");
    core::Feature* bf = doc.addFeature(std::move(base));
    doc.recompute();
    core::ObjectId body = bf->createdBodies()[0];

    // second sketch: small rect on XY inside the footprint, extruded symmetric 30 -> cut through
    auto sk2 = std::make_unique<TestSketch>();
    sk2->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    sk2->rects.push_back({5, 5, 10, 10});
    TestSketch* s2 = static_cast<TestSketch*>(doc.addFeature(std::move(sk2)));
    doc.recompute();

    auto cut = std::make_unique<ExtrudeFeature>();
    cut->prop("profiles").addRef(s2->rectProfile(0));
    cut->prop("direction").setEnum("Symmetric");
    cut->prop("distance").setDouble(30);
    cut->prop("operation").setEnum("Cut");
    core::Feature* cf = doc.addFeature(std::move(cut));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 20 * 20 * 10 - 5 * 5 * 10));
    QVERIFY(cf->createdBodies().empty());
    QCOMPARE(cf->touchedBodies().size(), size_t(1));
    // names of the base body carried through the cut
    QVERIFY(cf->namedShapes().count("Extrude1/Start"));
    QVERIFY(cf->namedShapes().count("Extrude1/End"));

    // join: rect next to the body extruded 10 -> single body, volume adds
    auto sk3 = std::make_unique<TestSketch>();
    sk3->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    sk3->rects.push_back({20, 0, 30, 20});
    TestSketch* s3 = static_cast<TestSketch*>(doc.addFeature(std::move(sk3)));
    doc.recompute();
    auto join = std::make_unique<ExtrudeFeature>();
    join->prop("profiles").addRef(s3->rectProfile(0));
    join->prop("distance").setDouble(10);
    join->prop("operation").setEnum("Join");
    core::Feature* jf = doc.addFeature(std::move(join));
    doc.recompute();
    QCOMPARE(jf->status(), core::FeatureStatus::Ok);
    QVERIFY(jf->createdBodies().empty());
    QVERIFY(near(core::volume(doc.bodyShape(body)), 20 * 20 * 10 - 5 * 5 * 10 + 10 * 20 * 10));
    QCOMPARE(doc.liveBodies().size(), size_t(1));

    // intersect with a box covering half the body
    auto sk4 = std::make_unique<TestSketch>();
    sk4->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    sk4->rects.push_back({0, 0, 15, 20});
    TestSketch* s4 = static_cast<TestSketch*>(doc.addFeature(std::move(sk4)));
    doc.recompute();
    auto inter = std::make_unique<ExtrudeFeature>();
    inter->prop("profiles").addRef(s4->rectProfile(0));
    inter->prop("distance").setDouble(100);
    inter->prop("operation").setEnum("Intersect");
    core::Feature* inf = doc.addFeature(std::move(inter));
    doc.recompute();
    QCOMPARE(inf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 15 * 20 * 10 - 5 * 5 * 10));
  }

  void extrudeJoinWithoutTouchMakesNewBody() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 10, 10);
    auto a = std::make_unique<ExtrudeFeature>();
    a->prop("profiles").addRef(sk->rectProfile(0));
    a->prop("operation").setEnum("New Body");
    doc.addFeature(std::move(a));
    auto sk2 = std::make_unique<TestSketch>();
    sk2->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    sk2->rects.push_back({50, 50, 60, 60});
    TestSketch* s2 = static_cast<TestSketch*>(doc.addFeature(std::move(sk2)));
    auto b = std::make_unique<ExtrudeFeature>();
    b->prop("profiles").addRef(s2->rectProfile(0));
    b->prop("operation").setEnum("Join");
    core::Feature* bf = doc.addFeature(std::move(b));
    doc.recompute();
    QCOMPARE(bf->status(), core::FeatureStatus::Ok);
    QCOMPARE(bf->createdBodies().size(), size_t(1));
    QCOMPARE(doc.liveBodies().size(), size_t(2));
  }

  void extrudeCutNothingIsError() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 10, 10);
    auto a = std::make_unique<ExtrudeFeature>();
    a->prop("profiles").addRef(sk->rectProfile(0));
    a->prop("operation").setEnum("Cut");
    core::Feature* f = doc.addFeature(std::move(a));
    doc.recompute();
    QCOMPARE(f->status(), core::FeatureStatus::Error);
    QVERIFY(doc.liveBodies().empty());
  }

  // PART-04: a cut that only touches the body (profile on the top face, extruded outward) removes
  // nothing and must be an error, not a silent no-op.
  void extrudeCutTouchingOnlyIsError() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 10, 10);
    auto a = std::make_unique<ExtrudeFeature>();
    a->prop("profiles").addRef(sk->rectProfile(0));
    a->prop("distance").setDouble(-10);
    a->prop("operation").setEnum("New Body");
    core::Feature* af = doc.addFeature(std::move(a));
    doc.recompute();
    core::ObjectId body = af->createdBodies().front();
    TestSketch* s2 = addRectSketch(doc, "XY", 2, 2, 4, 4);
    auto c = std::make_unique<ExtrudeFeature>();
    c->prop("profiles").addRef(s2->rectProfile(0));
    c->prop("distance").setDouble(5);
    c->prop("operation").setEnum("Cut");
    core::Feature* cf = doc.addFeature(std::move(c));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Error);
    QVERIFY2(cf->statusMessage().find("does not remove material") != std::string::npos, cf->statusMessage().c_str());
    QVERIFY(std::abs(core::volume(doc.bodyShape(body)) - 1000) < 1e-6);
    cf->prop("flip").setBool(true);
    doc.notifyFeatureChanged(cf->id());
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(std::abs(core::volume(doc.bodyShape(body)) - (1000 - 20)) < 1e-6);
  }

  void extrudeTwoSidesTaperAllAndToObject() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 10, 10);
    // two sides: 5 up, 3 down
    auto a = std::make_unique<ExtrudeFeature>();
    a->prop("profiles").addRef(sk->rectProfile(0));
    a->prop("direction").setEnum("Two Sides");
    a->prop("distance").setDouble(5);
    a->prop("distance2").setDouble(3);
    a->prop("operation").setEnum("New Body");
    core::Feature* af = doc.addFeature(std::move(a));
    doc.recompute();
    QCOMPARE(af->status(), core::FeatureStatus::Ok);
    core::ObjectId body = af->createdBodies()[0];
    QVERIFY(near(core::volume(doc.bodyShape(body)), 10 * 10 * 8));
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(doc.bodyShape(body), mn, mx));
    QVERIFY(near(mn.z, -3, 1e-3) && near(mx.z, 5, 1e-3));

    // taper: positive = outward -> volume grows
    auto t = std::make_unique<ExtrudeFeature>();
    t->prop("profiles").addRef(sk->rectProfile(0));
    t->prop("distance").setDouble(10);
    t->prop("taperAngle").setDouble(10);
    t->prop("operation").setEnum("New Body");
    core::Feature* tf = doc.addFeature(std::move(t));
    doc.recompute();
    QCOMPARE(tf->status(), core::FeatureStatus::Ok);
    double vt = core::volume(doc.bodyShape(tf->createdBodies()[0]));
    QVERIFY2(vt > 10 * 10 * 10 + 1, qPrintable(QString::number(vt)));

    // through all upward from z=0 must pass beyond every body
    auto all = std::make_unique<ExtrudeFeature>();
    all->prop("profiles").addRef(sk->rectProfile(0));
    all->prop("extentType").setEnum("All");
    all->prop("operation").setEnum("New Body");
    core::Feature* allf = doc.addFeature(std::move(all));
    doc.recompute();
    QCOMPARE(allf->status(), core::FeatureStatus::Ok);
    QVERIFY(core::bbox(doc.bodyShape(allf->createdBodies()[0]), mn, mx));
    QVERIFY(mx.z > 10);

    // to object: the XZ origin plane offset... use a construction-free target: a Face of the
    // first body (its top at z=5) from a profile at z=-3? Simpler: extrude the sketch (z=0) to the
    // top face of the first body (z=5).
    core::ShapeRef topFace;
    const TopoDS_Shape& bs = doc.bodyShape(body);
    for (const auto& f : core::subShapes(bs, TopAbs_FACE)) {
      auto pl = core::facePlane(f);
      if (pl && near(pl->Location().Z(), 5, 1e-3) && pl->Direction().Z() > 0.9) {
        topFace = core::ShapeRef::subShape(body, bs, f);
        break;
      }
    }
    QVERIFY(topFace.isValid());
    auto to = std::make_unique<ExtrudeFeature>();
    to->prop("profiles").addRef(sk->rectProfile(0));
    to->prop("extentType").setEnum("To Object");
    to->prop("toObject").setRef(topFace);
    to->prop("operation").setEnum("New Body");
    core::Feature* tof = doc.addFeature(std::move(to));
    doc.recompute();
    QCOMPARE(tof->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(tof->createdBodies()[0])), 10 * 10 * 5));
  }

  void extrudeFromBodyFaceWithNamedRef() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 10, 10);
    auto a = std::make_unique<ExtrudeFeature>();
    a->prop("profiles").addRef(sk->rectProfile(0));
    a->prop("distance").setDouble(10);
    a->prop("operation").setEnum("New Body");
    core::Feature* af = doc.addFeature(std::move(a));
    doc.recompute();
    core::ObjectId body = af->createdBodies()[0];
    // pick the end face via its name
    TopoDS_Shape end = af->namedShapes().at("Extrude1/End");
    core::ShapeRef faceRef = core::ShapeRef::subShape(body, doc.bodyShape(body), end);
    annotateRef(doc, faceRef);
    QCOMPARE(faceRef.name, std::string("Extrude1/End"));
    auto b = std::make_unique<ExtrudeFeature>();
    b->prop("profiles").addRef(faceRef);
    b->prop("distance").setDouble(5);
    b->prop("operation").setEnum("Join");
    core::Feature* bf = doc.addFeature(std::move(b));
    doc.recompute();
    QCOMPARE(bf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 10 * 10 * 15));

    // Change the first extrusion's height: the face index may change, the name re-finds it.
    af->prop("distance").setDouble(20);
    doc.notifyFeatureChanged(af->id());
    doc.recompute();
    QCOMPARE(bf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 10 * 10 * 25));
  }

  void revolveFullAndAngle() {
    core::Document doc;
    // rectangle in XZ plane at x in [5,10], z in [0,4] revolved about Z -> ring
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(core::ShapeRef::originPlane("XZ"));
    sk->rects.push_back({5, 0, 10, 4});
    TestSketch* s = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
    doc.recompute();
    auto r = std::make_unique<RevolveFeature>();
    r->prop("profiles").addRef(s->rectProfile(0));
    r->prop("axis").setRef(core::ShapeRef::originAxis("Z"));
    r->prop("operation").setEnum("New Body");
    core::Feature* rf = doc.addFeature(std::move(r));
    doc.recompute();
    QCOMPARE(rf->status(), core::FeatureStatus::Ok);
    double expected = M_PI * (100 - 25) * 4;
    QVERIFY(near(core::volume(doc.bodyShape(rf->createdBodies()[0])), expected, 1e-3));
    {
      int sides = 0;
      for (const auto& [k, v] : rf->namedShapes()) sides += k.rfind("Revolve1/Side/", 0) == 0;
      QVERIFY(sides >= 1);
    }

    auto r2 = std::make_unique<RevolveFeature>();
    r2->prop("profiles").addRef(s->rectProfile(0));
    r2->prop("axis").setRef(core::ShapeRef::originAxis("Z"));
    r2->prop("extentType").setEnum("Angle");
    r2->prop("angle").setDouble(90);
    r2->prop("operation").setEnum("New Body");
    core::Feature* rf2 = doc.addFeature(std::move(r2));
    doc.recompute();
    QCOMPARE(rf2->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(rf2->createdBodies()[0])), expected / 4, 1e-3));
    QVERIFY(rf2->namedShapes().count("Revolve2/Start"));
    QVERIFY(rf2->namedShapes().count("Revolve2/End"));

    // symmetric 90 -> same volume as 90 one side
    auto r3 = std::make_unique<RevolveFeature>();
    r3->prop("profiles").addRef(s->rectProfile(0));
    r3->prop("axis").setRef(core::ShapeRef::originAxis("Z"));
    r3->prop("extentType").setEnum("Angle");
    r3->prop("direction").setEnum("Symmetric");
    r3->prop("angle").setDouble(90);
    r3->prop("operation").setEnum("New Body");
    core::Feature* rf3 = doc.addFeature(std::move(r3));
    doc.recompute();
    QCOMPARE(rf3->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(rf3->createdBodies()[0])), expected / 4, 1e-3));
  }

  void saveLoadRoundTrip() {
    core::Document doc;
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 20, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setExpression("2 * 5 mm");
    ex->prop("operation").setEnum("New Body");
    doc.addFeature(std::move(ex));
    doc.recompute();
    QJsonObject json = doc.toJson();

    core::Document doc2;
    std::string err;
    QVERIFY2(doc2.fromJson(json, &err), err.c_str());
    QCOMPARE(doc2.featureCount(), 2);
    const core::Feature* f = doc2.featureAt(1);
    QCOMPARE(f->type(), std::string("Extrude"));
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QCOMPARE(f->prop("distance").expression(), std::string("2 * 5 mm"));
    QVERIFY(near(core::volume(doc2.bodyShape(f->createdBodies()[0])), 20 * 10 * 10));
  }
};

QTEST_GUILESS_MAIN(ExtrudeRevolveTest)
#include "test_extrude_revolve.moc"
