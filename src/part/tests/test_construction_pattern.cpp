// Tests for construction geometry, patterns, mirror, combine, move/copy, split, sweep, loft.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/features/Pattern.h"
#include "part/features/Shell.h"
#include "part/features/SweepLoft.h"
#include "support/TestSketch.h"

#include <QElapsedTimer>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {
bool near(double a, double b, double tol = 1e-4) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

core::Feature* addBox(core::Document& doc, double x0, double y0, double x1, double y1, double h, const char* op = "New Body") {
  auto sk = std::make_unique<TestSketch>();
  sk->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
  sk->rects.push_back({x0, y0, x1, y1});
  TestSketch* s = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
  doc.recompute();
  auto ex = std::make_unique<ExtrudeFeature>();
  ex->prop("profiles").addRef(s->rectProfile(0));
  ex->prop("distance").setDouble(h);
  ex->prop("operation").setEnum(op);
  core::Feature* f = doc.addFeature(std::move(ex));
  doc.recompute();
  return f;
}
}  // namespace

class ConstructionPatternTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void offsetPlaneAndSketchOnIt() {
    core::Document doc;
    auto op = std::make_unique<OffsetPlaneFeature>();
    op->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    op->prop("distance").setDouble(15);
    core::Feature* pf = doc.addFeature(std::move(op));
    doc.recompute();
    QCOMPARE(pf->status(), core::FeatureStatus::Ok);
    QCOMPARE(pf->browserFolder(), std::string("Construction"));
    QVERIFY(pf->asPlane());
    QVERIFY(near(pf->asPlane()->Location().Z(), 15));
    QVERIFY(!pf->outputShape().IsNull());
    QCOMPARE(pf->outputShape().ShapeType(), TopAbs_FACE);
    // a sketch on the construction plane extrudes from z = 15
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(core::ShapeRef::feature(pf->id(), core::ShapeKind::ConstructionPlane));
    sk->rects.push_back({0, 0, 10, 10});
    TestSketch* s = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(s->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    core::Feature* ef = doc.addFeature(std::move(ex));
    doc.recompute();
    QCOMPARE(ef->status(), core::FeatureStatus::Ok);
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(doc.bodyShape(ef->createdBodies()[0]), mn, mx));
    QVERIFY(near(mn.z, 15, 1e-3) && near(mx.z, 20, 1e-3));
    // dependency: the sketch depends on the plane
    auto deps = doc.dependenciesOf(s->id());
    QVERIFY(std::find(deps.begin(), deps.end(), pf->id()) != deps.end());
  }

  void planeAtAngleMidplaneThreePoints() {
    core::Document doc;
    auto pa = std::make_unique<PlaneAtAngleFeature>();
    pa->prop("axis").setRef(core::ShapeRef::originAxis("X"));
    pa->prop("reference").setRef(core::ShapeRef::originPlane("XY"));
    pa->prop("angle").setDouble(90);
    core::Feature* f1 = doc.addFeature(std::move(pa));
    doc.recompute();
    QCOMPARE(f1->status(), core::FeatureStatus::Ok);
    // XY normal (0,0,1) rotated 90 deg about X -> (0,-1,0) or (0,1,0)
    QVERIFY(near(std::abs(f1->asPlane()->Direction().Y()), 1, 1e-6));

    auto mp = std::make_unique<MidplaneFeature>();
    mp->prop("planeA").setRef(core::ShapeRef::originPlane("XY"));
    mp->prop("planeB").setRef(core::ShapeRef::feature(f1->id(), core::ShapeKind::ConstructionPlane));
    core::Feature* f2 = doc.addFeature(std::move(mp));
    doc.recompute();
    QCOMPARE(f2->status(), core::FeatureStatus::Ok);
    // bisector of XY and XZ-like plane: normal at 45 deg
    QVERIFY(near(std::abs(f2->asPlane()->Direction().Z()), std::sqrt(0.5), 1e-6));

    // parallel midplane
    auto op = std::make_unique<OffsetPlaneFeature>();
    op->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    op->prop("distance").setDouble(10);
    core::Feature* f3 = doc.addFeature(std::move(op));
    auto mp2 = std::make_unique<MidplaneFeature>();
    mp2->prop("planeA").setRef(core::ShapeRef::originPlane("XY"));
    mp2->prop("planeB").setRef(core::ShapeRef::feature(f3->id(), core::ShapeKind::ConstructionPlane));
    core::Feature* f4 = doc.addFeature(std::move(mp2));
    doc.recompute();
    QCOMPARE(f4->status(), core::FeatureStatus::Ok);
    QVERIFY(near(f4->asPlane()->Location().Z(), 5));

    // three points: use the origin point and two construction points
    auto p1 = std::make_unique<PointFeature>();
    p1->prop("point").setRef(core::ShapeRef::originPoint());
    core::Feature* fp1 = doc.addFeature(std::move(p1));
    doc.recompute();
    QCOMPARE(fp1->status(), core::FeatureStatus::Ok);
    QVERIFY(fp1->asPoint());
    auto ax = std::make_unique<AxisFeature>();
    ax->prop("mode").setEnum("Through Two Planes");
    ax->prop("planeA").setRef(core::ShapeRef::originPlane("XY"));
    ax->prop("planeB").setRef(core::ShapeRef::originPlane("XZ"));
    core::Feature* fa = doc.addFeature(std::move(ax));
    doc.recompute();
    QCOMPARE(fa->status(), core::FeatureStatus::Ok);
    QVERIFY(fa->asAxis());
    QVERIFY(near(std::abs(fa->asAxis()->Direction().X()), 1, 1e-6));
    auto p2 = std::make_unique<PointFeature>();
    p2->prop("mode").setEnum("Three Planes");
    p2->prop("planeA").setRef(core::ShapeRef::originPlane("XY"));
    p2->prop("planeB").setRef(core::ShapeRef::originPlane("XZ"));
    p2->prop("planeC").setRef(core::ShapeRef::feature(f1->id(), core::ShapeKind::ConstructionPlane));
    core::Feature* fp2 = doc.addFeature(std::move(p2));
    doc.recompute();
    // XY, XZ and the rotated plane all contain the X axis -> degenerate: error expected
    QCOMPARE(fp2->status(), core::FeatureStatus::Error);
    auto p3 = std::make_unique<PointFeature>();
    p3->prop("mode").setEnum("Three Planes");
    p3->prop("planeA").setRef(core::ShapeRef::originPlane("XY"));
    p3->prop("planeB").setRef(core::ShapeRef::originPlane("XZ"));
    p3->prop("planeC").setRef(core::ShapeRef::originPlane("YZ"));
    core::Feature* fp3 = doc.addFeature(std::move(p3));
    doc.recompute();
    QCOMPARE(fp3->status(), core::FeatureStatus::Ok);
    QVERIFY(near(fp3->asPoint()->Distance(gp_Pnt(0, 0, 0)), 0, 1e-6));
  }

  void rectangularPatternOfFeatureAndBodies() {
    core::Document doc;
    core::Feature* base = addBox(doc, 0, 0, 100, 20, 10);
    core::ObjectId body = base->createdBodies()[0];
    // a cut feature to pattern: 5x5 pocket at (5,5) through
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    sk->rects.push_back({5, 5, 10, 10});
    TestSketch* s = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
    auto cut = std::make_unique<ExtrudeFeature>();
    cut->prop("profiles").addRef(s->rectProfile(0));
    cut->prop("direction").setEnum("Symmetric");
    cut->prop("distance").setDouble(50);
    cut->prop("operation").setEnum("Cut");
    core::Feature* cf = doc.addFeature(std::move(cut));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 100 * 20 * 10 - 250, 1e-3));

    auto pat = std::make_unique<RectangularPatternFeature>();
    pat->prop("objects").addRef(core::ShapeRef::feature(cf->id()));
    pat->prop("direction1").setRef(core::ShapeRef::originAxis("X"));
    pat->prop("quantity1").setInt(4);
    pat->prop("distance1").setDouble(60);  // extent: 0,20,40,60
    core::Feature* pf = doc.addFeature(std::move(pat));
    doc.recompute();
    QCOMPARE(pf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 100 * 20 * 10 - 4 * 250, 1e-3));

    // spacing distribution, 2 directions, suppressed instance
    pf->prop("distribution").setEnum("Spacing");
    pf->prop("distance1").setDouble(20);
    pf->prop("direction2").setRef(core::ShapeRef::originAxis("Y"));
    pf->prop("quantity2").setInt(2);
    pf->prop("distance2").setDouble(10);
    pf->prop("suppressed").setJson(QJsonArray{3});  // index 3 = (a=3 steps, b=0)
    doc.notifyFeatureChanged(pf->id());
    doc.recompute();
    QCOMPARE(pf->status(), core::FeatureStatus::Ok);
    // 4 x 2 = 8 instances minus the original minus one suppressed = 6 copies + original
    QVERIFY2(near(core::volume(doc.bodyShape(body)), 100 * 20 * 10 - 7 * 250, 1e-3),
             qPrintable(QString::number(core::volume(doc.bodyShape(body)))));
    // dependencies: pattern depends on the cut feature
    auto deps = doc.dependenciesOf(pf->id());
    QVERIFY(std::find(deps.begin(), deps.end(), cf->id()) != deps.end());

    // body pattern creates new bodies
    auto bp = std::make_unique<RectangularPatternFeature>();
    bp->prop("patternType").setEnum("Bodies");
    bp->prop("objects").addRef(core::ShapeRef::body(body));
    bp->prop("direction1").setRef(core::ShapeRef::originAxis("Z"));
    bp->prop("quantity1").setInt(3);
    bp->prop("distance1").setDouble(40);
    core::Feature* bf = doc.addFeature(std::move(bp));
    doc.recompute();
    QCOMPARE(bf->status(), core::FeatureStatus::Ok);
    QCOMPARE(bf->createdBodies().size(), size_t(2));
    QCOMPARE(doc.liveBodies().size(), size_t(3));
  }

  // PART-01: huge quantities are rejected instead of freezing the UI.
  void patternQuantityIsCapped() {
    core::Document doc;
    core::Feature* base = addBox(doc, 0, 0, 10, 10, 10);
    core::ObjectId body = base->createdBodies()[0];
    auto bp = std::make_unique<RectangularPatternFeature>();
    bp->prop("patternType").setEnum("Bodies");
    bp->prop("objects").addRef(core::ShapeRef::body(body));
    bp->prop("direction1").setRef(core::ShapeRef::originAxis("X"));
    QCOMPARE(bp->prop("quantity1").flags().intMax, 1000);
    QCOMPARE(bp->prop("quantity1").flags().intMin, 1);
    bp->prop("quantity1").setInt(100000);
    bp->prop("distance1").setDouble(20);
    bp->prop("distribution").setEnum("Spacing");
    core::Feature* bf = doc.addFeature(std::move(bp));
    QElapsedTimer timer;
    timer.start();
    doc.recompute();
    QVERIFY(timer.elapsed() < 2000);
    QCOMPARE(bf->status(), core::FeatureStatus::Error);
    QVERIFY2(bf->statusMessage().find("too large") != std::string::npos, bf->statusMessage().c_str());
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    // the product of both directions counts too
    bf->prop("quantity1").setInt(40);
    bf->prop("direction2").setRef(core::ShapeRef::originAxis("Y"));
    bf->prop("quantity2").setInt(40);
    doc.notifyFeatureChanged(bf->id());
    doc.recompute();
    QCOMPARE(bf->status(), core::FeatureStatus::Error);
    // a sane quantity works
    bf->prop("quantity1").setInt(3);
    bf->prop("quantity2").setInt(2);
    doc.notifyFeatureChanged(bf->id());
    doc.recompute();
    QVERIFY2(bf->status() == core::FeatureStatus::Ok, bf->statusMessage().c_str());
    QCOMPARE(doc.liveBodies().size(), size_t(6));
    // circular
    auto cp = std::make_unique<CircularPatternFeature>();
    QCOMPARE(cp->prop("quantity").flags().intMin, 2);
    cp->prop("patternType").setEnum("Bodies");
    cp->prop("objects").addRef(core::ShapeRef::body(body));
    cp->prop("axis").setRef(core::ShapeRef::originAxis("Z"));
    cp->prop("quantity").setInt(5000);
    core::Feature* cf = doc.addFeature(std::move(cp));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Error);
    QVERIFY(cf->statusMessage().find("too large") != std::string::npos);
  }

  void circularPatternAndMirror() {
    core::Document doc;
    // ring: big box with a small boss joined at a radius, pattern the boss 6x about Z
    core::Feature* base = addBox(doc, -50, -50, 50, 50, 5);
    core::ObjectId body = base->createdBodies()[0];
    core::Feature* boss = addBox(doc, 30, -2, 34, 2, 10, "Join");
    QCOMPARE(boss->status(), core::FeatureStatus::Ok);
    double v1 = core::volume(doc.bodyShape(body));
    QVERIFY(near(v1, 100 * 100 * 5 + 4 * 4 * 5, 1e-3));
    auto cp = std::make_unique<CircularPatternFeature>();
    cp->prop("objects").addRef(core::ShapeRef::feature(boss->id()));
    cp->prop("axis").setRef(core::ShapeRef::originAxis("Z"));
    cp->prop("quantity").setInt(6);
    core::Feature* cf = doc.addFeature(std::move(cp));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 100 * 100 * 5 + 6 * 4 * 4 * 5, 1e-3));

    // angle type: 3 instances over 90 deg -> 0, 45, 90
    cf->prop("type").setEnum("Angle");
    cf->prop("quantity").setInt(3);
    cf->prop("angle").setDouble(90);
    doc.notifyFeatureChanged(cf->id());
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 100 * 100 * 5 + 3 * 4 * 4 * 5, 1e-3));

    // mirror the boss feature across YZ -> boss at x = -32 too
    auto mi = std::make_unique<MirrorFeature>();
    mi->prop("objects").addRef(core::ShapeRef::feature(boss->id()));
    mi->prop("mirrorPlane").setRef(core::ShapeRef::originPlane("YZ"));
    core::Feature* mf = doc.addFeature(std::move(mi));
    doc.recompute();
    QCOMPARE(mf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(body)), 100 * 100 * 5 + 4 * 4 * 4 * 5, 1e-3));

    // mirror a body (new body)
    core::Document doc2;
    core::Feature* b = addBox(doc2, 10, 0, 20, 10, 5);
    auto mb = std::make_unique<MirrorFeature>();
    mb->prop("patternType").setEnum("Bodies");
    mb->prop("objects").addRef(core::ShapeRef::body(b->createdBodies()[0]));
    mb->prop("mirrorPlane").setRef(core::ShapeRef::originPlane("YZ"));
    mb->prop("operation").setEnum("New Body");
    core::Feature* mbf = doc2.addFeature(std::move(mb));
    doc2.recompute();
    QCOMPARE(mbf->status(), core::FeatureStatus::Ok);
    QCOMPARE(mbf->createdBodies().size(), size_t(1));
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(doc2.bodyShape(mbf->createdBodies()[0]), mn, mx));
    QVERIFY(near(mn.x, -20, 1e-3) && near(mx.x, -10, 1e-3));
    QVERIFY(near(core::volume(doc2.bodyShape(mbf->createdBodies()[0])), 500, 1e-3));

    // mirror body with join across a touching plane -> one body of double volume
    core::Document doc3;
    core::Feature* b3 = addBox(doc3, 0, 0, 10, 10, 5);
    auto mj = std::make_unique<MirrorFeature>();
    mj->prop("patternType").setEnum("Bodies");
    mj->prop("objects").addRef(core::ShapeRef::body(b3->createdBodies()[0]));
    mj->prop("mirrorPlane").setRef(core::ShapeRef::originPlane("YZ"));
    core::Feature* mjf = doc3.addFeature(std::move(mj));
    doc3.recompute();
    QCOMPARE(mjf->status(), core::FeatureStatus::Ok);
    QVERIFY(mjf->createdBodies().empty());
    QVERIFY(near(core::volume(doc3.bodyShape(b3->createdBodies()[0])), 1000, 1e-3));
  }

  void combineMoveSplit() {
    core::Document doc;
    core::Feature* a = addBox(doc, 0, 0, 20, 20, 10);
    core::Feature* b = addBox(doc, 10, 10, 30, 30, 10);
    core::ObjectId ba = a->createdBodies()[0], bb = b->createdBodies()[0];
    QCOMPARE(doc.liveBodies().size(), size_t(2));
    auto comb = std::make_unique<CombineFeature>();
    comb->prop("target").setRef(core::ShapeRef::body(ba));
    comb->prop("tools").addRef(core::ShapeRef::body(bb));
    comb->prop("operation").setEnum("Cut");
    core::Feature* cf = doc.addFeature(std::move(comb));
    doc.recompute();
    QCOMPARE(cf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(ba)), 4000 - 1000, 1e-3));
    QVERIFY(doc.bodyShape(bb).IsNull());  // tool consumed
    cf->prop("keepTools").setBool(true);
    cf->prop("operation").setEnum("Join");
    doc.notifyFeatureChanged(cf->id());
    doc.recompute();
    QVERIFY(near(core::volume(doc.bodyShape(ba)), 4000 + 4000 - 1000, 1e-3));
    QVERIFY(!doc.bodyShape(bb).IsNull());

    // move with copy
    auto mv = std::make_unique<MoveBodyFeature>();
    mv->prop("bodies").addRef(core::ShapeRef::body(bb));
    mv->prop("dz").setDouble(50);
    mv->prop("createCopy").setBool(true);
    core::Feature* mf = doc.addFeature(std::move(mv));
    doc.recompute();
    QCOMPARE(mf->status(), core::FeatureStatus::Ok);
    QCOMPARE(mf->createdBodies().size(), size_t(1));
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(doc.bodyShape(mf->createdBodies()[0]), mn, mx));
    QVERIFY(near(mn.z, 50, 1e-3));
    // rotate in place
    auto rot = std::make_unique<MoveBodyFeature>();
    rot->prop("bodies").addRef(core::ShapeRef::body(bb));
    rot->prop("moveType").setEnum("Rotate");
    rot->prop("axis").setRef(core::ShapeRef::originAxis("Z"));
    rot->prop("angle").setDouble(90);
    core::Feature* rf = doc.addFeature(std::move(rot));
    doc.recompute();
    QCOMPARE(rf->status(), core::FeatureStatus::Ok);
    QVERIFY(core::bbox(doc.bodyShape(bb), mn, mx));
    QVERIFY(near(mn.x, -30, 1e-3) && near(mx.x, -10, 1e-3));

    // split body ba (the L shape) with the XZ plane offset... use YZ plane: x = 0 touches; use a construction plane at x=10
    auto op = std::make_unique<OffsetPlaneFeature>();
    op->prop("plane").setRef(core::ShapeRef::originPlane("YZ"));
    op->prop("distance").setDouble(10);
    core::Feature* pf = doc.addFeature(std::move(op));
    auto sp = std::make_unique<SplitBodyFeature>();
    sp->prop("bodies").addRef(core::ShapeRef::body(ba));
    sp->prop("splitTool").setRef(core::ShapeRef::feature(pf->id(), core::ShapeKind::ConstructionPlane));
    core::Feature* sf = doc.addFeature(std::move(sp));
    doc.recompute();
    QCOMPARE(sf->status(), core::FeatureStatus::Ok);
    QCOMPARE(sf->createdBodies().size(), size_t(1));
    double total = core::volume(doc.bodyShape(ba)) + core::volume(doc.bodyShape(sf->createdBodies()[0]));
    QVERIFY(near(total, 7000, 1e-3));
  }

  void sweepAndLoft() {
    core::Document doc;
    // sweep a circle (XZ plane at origin, r=2) along a straight line in XY from (0,0) to (0,30)
    auto prof = std::make_unique<TestSketch>();
    prof->prop("plane").setRef(core::ShapeRef::originPlane("XZ"));
    prof->circles.push_back({0, 0, 2});
    TestSketch* p = static_cast<TestSketch*>(doc.addFeature(std::move(prof)));
    auto path = std::make_unique<TestSketch>();
    path->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    path->lines.push_back({0, 0});
    path->lines.push_back({0, 30});
    TestSketch* pa = static_cast<TestSketch*>(doc.addFeature(std::move(path)));
    doc.recompute();
    auto sw = std::make_unique<SweepFeature>();
    sw->prop("profiles").addRef(p->circleProfile(0));
    sw->prop("path").addRef(pa->lineRef(0));
    sw->prop("operation").setEnum("New Body");
    core::Feature* sf = doc.addFeature(std::move(sw));
    doc.recompute();
    QCOMPARE(sf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc.bodyShape(sf->createdBodies()[0])), M_PI * 4 * 30, 1e-3));

    // loft between a 20x20 square at z=0 and a 10x10 square at z=10 (construction plane)
    core::Document doc2;
    auto op = std::make_unique<OffsetPlaneFeature>();
    op->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    op->prop("distance").setDouble(10);
    core::Feature* pf = doc2.addFeature(std::move(op));
    auto s1 = std::make_unique<TestSketch>();
    s1->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    s1->rects.push_back({-10, -10, 10, 10});
    TestSketch* a = static_cast<TestSketch*>(doc2.addFeature(std::move(s1)));
    auto s2 = std::make_unique<TestSketch>();
    s2->prop("plane").setRef(core::ShapeRef::feature(pf->id(), core::ShapeKind::ConstructionPlane));
    s2->rects.push_back({-5, -5, 5, 5});
    TestSketch* b = static_cast<TestSketch*>(doc2.addFeature(std::move(s2)));
    doc2.recompute();
    auto lo = std::make_unique<LoftFeature>();
    lo->prop("profiles").addRef(a->rectProfile(0));
    lo->prop("profiles").addRef(b->rectProfile(0));
    lo->prop("ruled").setBool(true);
    lo->prop("operation").setEnum("New Body");
    core::Feature* lf = doc2.addFeature(std::move(lo));
    doc2.recompute();
    QCOMPARE(lf->status(), core::FeatureStatus::Ok);
    // frustum of a square pyramid: h/3 (A1 + A2 + sqrt(A1 A2)) = 10/3 (400 + 100 + 200)
    QVERIFY(near(core::volume(doc2.bodyShape(lf->createdBodies()[0])), 10.0 / 3.0 * 700, 1e-3));
    QVERIFY(lf->namedShapes().count("Loft1/Start"));
    QVERIFY(lf->namedShapes().count("Loft1/End"));
  }
};

QTEST_GUILESS_MAIN(ConstructionPatternTest)
#include "test_construction_pattern.moc"
