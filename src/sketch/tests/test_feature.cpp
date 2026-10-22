// SketchFeature tests: recompute in a Document, profiles, IProfileSource, JSON persistence.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "sketch/SketchFeature.h"
#include "sketch/SketchModule.h"
#include "support/SketchTestHelpers.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

namespace {
double faceArea(const TopoDS_Shape& f) {
  if (f.IsNull()) return 0;
  GProp_GProps p;
  BRepGProp::SurfaceProperties(f, p);
  return p.Mass();
}
int count(const TopoDS_Shape& s, TopAbs_ShapeEnum t) {
  int n = 0;
  for (TopExp_Explorer ex(s, t); ex.More(); ex.Next()) ++n;
  return n;
}
}  // namespace

class FeatureTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    static core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    sketch::registerModule(ctx);
    QVERIFY(core::FeatureFactory::instance().has("Sketch"));
    QVERIFY(tools.has("sketch.line"));
  }

  void recomputeRectangleProfile() {
    core::Document doc;
    auto f = std::make_unique<SketchFeature>();
    f->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    auto ids = addRectangle(f->sketch(), {0, 0}, {30, 20});
    SketchFeature* sk = f.get();
    doc.undoStack().push(std::make_unique<core::AddFeatureCommand>(doc, std::move(f)));
    QCOMPARE(sk->status(), core::FeatureStatus::Ok);
    QCOMPARE(sk->browserFolder(), std::string("Sketches"));
    QVERIFY(sk->profileSource() == sk);
    QVERIFY(!sk->outputShape().IsNull());
    QCOMPARE(count(sk->outputShape(), TopAbs_EDGE), 4);
    auto profiles = sk->profiles();
    QCOMPARE(profiles.size(), size_t(1));
    QVERIFY(near(profiles[0].area, 600.0, 1e-6));
    QCOMPARE(profiles[0].curveIds.size(), size_t(4));
    QCOMPARE(profiles[0].ref.kind, core::ShapeKind::Profile);
    QCOMPARE(profiles[0].ref.owner, sk->id());
    TopoDS_Face face = sk->profileFace(profiles[0].ref);
    QVERIFY(!face.IsNull());
    QVERIFY(near(faceArea(face), 600.0, 1e-6));
    // via core resolver
    TopoDS_Face viaCore = core::resolveProfileFace(doc, profiles[0].ref);
    QVERIFY(!viaCore.IsNull());
    // sketch point and curve refs
    auto p = sk->sketchPoint(core::ShapeRef::sketchPoint(sk->id(), ids[1], core::SketchPointRole::Start));
    QVERIFY(p.has_value());
    QVERIFY(near(core::Vec3::from(*p).x, 30.0) && near(core::Vec3::from(*p).y, 0.0));
    QVERIFY(!sk->curveEdge(ids[0]).IsNull());
    QVERIFY(sk->curveEdge(999).IsNull());
    auto axis = core::resolveAxis(doc, core::ShapeRef::sketchCurve(sk->id(), ids[0]));
    QVERIFY(axis.has_value());
    // the sketch itself acts as a plane
    auto pl = core::resolvePlane(doc, core::ShapeRef::sketch(sk->id()));
    QVERIFY(pl.has_value());
    QVERIFY(sk->statusText() == "4 DOF");
  }

  void overlappingCirclesGiveThreeRegions() {
    core::Document doc;
    auto f = std::make_unique<SketchFeature>();
    f->prop("plane").setRef(core::ShapeRef::originPlane("XZ"));
    f->sketch().addGeometry(Geometry::makeCircle({0, 0}, 10));
    f->sketch().addGeometry(Geometry::makeCircle({12, 0}, 10));
    // a construction line must not split regions
    int cl = f->sketch().addGeometry(Geometry::makeLine({-20, 0}, {40, 0}));
    f->sketch().geometry(cl)->construction = true;
    SketchFeature* sk = f.get();
    doc.addFeature(std::move(f));
    doc.recompute();
    QCOMPARE(sk->status(), core::FeatureStatus::Ok);
    auto profiles = sk->profiles();
    QCOMPARE(profiles.size(), size_t(3));
    double total = 0;
    for (const auto& p : profiles) total += p.area;
    // union area of two circles = sum - lens; the three regions tile the union exactly
    // every region is bounded by both circles; the lens is the smallest one
    double lens = profiles[0].area;
    for (const auto& p : profiles) {
      QCOMPARE(p.curveIds.size(), size_t(2));
      lens = std::min(lens, p.area);
    }
    QVERIFY(lens > 0);
    QVERIFY(near(total + lens, 2 * kPi * 100, 1e-3));
    // the region containing a point on the lens
    const ProfileRegion* r = sk->regionAt({6, 0});
    QVERIFY(r);
    QCOMPARE(r->curveIds.size(), size_t(2));
    // seed matching: a ref with a stale curve list but a good seed still resolves
    core::ShapeRef ref = core::ShapeRef::profile(sk->id(), sk->toWorldVec({6, 0}), {1, 2});
    QVERIFY(!sk->profileFace(ref).IsNull());
    core::ShapeRef stale = core::ShapeRef::profile(sk->id(), sk->toWorldVec({6, 0}), {7, 8});
    QVERIFY(!sk->profileFace(stale).IsNull());
    // seed outside everything but curve ids match -> overlap fallback
    core::ShapeRef outside = core::ShapeRef::profile(sk->id(), sk->toWorldVec({100, 100}), {1, 2});
    QVERIFY(!sk->profileFace(outside).IsNull());
    core::ShapeRef nothing = core::ShapeRef::profile(sk->id(), sk->toWorldVec({100, 100}), {7, 8});
    QVERIFY(sk->profileFace(nothing).IsNull());
    // the face lies on the XZ plane
    QVERIFY(near(sk->sketchPlane().Direction().Y(), -1.0));
    QVERIFY(count(sk->allEdges(false), TopAbs_EDGE) == 2);
    QVERIFY(count(sk->allEdges(true), TopAbs_EDGE) == 3);
  }

  void danglingEdgesAndNestedRegions() {
    core::Document doc;
    auto f = std::make_unique<SketchFeature>();
    f->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    addRectangle(f->sketch(), {0, 0}, {40, 40});
    f->sketch().addGeometry(Geometry::makeCircle({20, 20}, 5));
    f->sketch().addGeometry(Geometry::makeLine({50, 0}, {60, 10}));  // dangling
    SketchFeature* sk = f.get();
    doc.addFeature(std::move(f));
    doc.recompute();
    auto profiles = sk->profiles();
    QCOMPARE(profiles.size(), size_t(2));
    double big = std::max(profiles[0].area, profiles[1].area);
    double small = std::min(profiles[0].area, profiles[1].area);
    QVERIFY(near(small, kPi * 25, 1e-3));
    QVERIFY(near(big, 1600 - kPi * 25, 1e-3));
  }

  void documentJsonRoundTrip() {
    core::Document doc;
    auto f = std::make_unique<SketchFeature>();
    f->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    auto ids = addRectangle(f->sketch(), {0, 0}, {30, 20});
    f->sketch().addConstraint(Constraint{.type = ConstraintType::Distance, .a = {ids[0]}, .value = 30, .expression = "width"});
    SketchFeature* sk = f.get();
    core::ObjectId id = doc.addFeature(std::move(f))->id();
    doc.setParameter("width", "50 mm");
    doc.recompute();
    QCOMPARE(sk->status(), core::FeatureStatus::Ok);
    QVERIFY(near(sk->sketch().geometry(ids[0])->length(), 50.0));
    QJsonObject json = doc.toJson();
    core::Document doc2;
    std::string err;
    QVERIFY2(doc2.fromJson(json, &err), err.c_str());
    auto* sk2 = doc2.featureAs<SketchFeature>(id);
    QVERIFY(sk2);
    QCOMPARE(sk2->sketch().geometryCount(), 4);
    QCOMPARE(sk2->sketch().constraints().size(), size_t(9));
    QCOMPARE(sk2->status(), core::FeatureStatus::Ok);
    QVERIFY(near(sk2->sketch().geometry(ids[0])->length(), 50.0));
    QCOMPARE(sk2->profiles().size(), size_t(1));
    // modify through undo command
    QJsonObject before = sk2->toJson();
    sk2->sketch().constraint(sk2->sketch().dimensionByName("d1")->id)->expression = "width/2";
    QJsonObject after = sk2->toJson();
    doc2.undoStack().push(std::make_unique<core::ModifyFeatureCommand>(doc2, id, before, after));
    QVERIFY(near(sk2->sketch().geometry(ids[0])->length(), 25.0));
    doc2.undoStack().undo();
    QVERIFY(near(sk2->sketch().geometry(ids[0])->length(), 50.0));
  }

  // SK-04: a face sketch keeps the normal side and X axis it was created with, whatever
  // orientation / parametrisation later recomputes resolve the face with
  void storedPlaneAxesStayStable() {
    SketchFeature f;
    QVERIFY(!f.hasStoredPlaneAxes());
    const gp_Ax3 first(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    gp_Ax3 a = f.alignToStoredAxes(first);  // first recompute stores
    QVERIFY(f.hasStoredPlaneAxes());
    QVERIFY(a.Direction().IsEqual(first.Direction(), 1e-12));
    // same face, reversed and with another X: aligned back
    const gp_Ax3 flipped(gp_Pnt(0, 0, 5), gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
    a = f.alignToStoredAxes(flipped);
    QVERIFY(a.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-12));
    QVERIFY(a.XDirection().IsEqual(gp_Dir(1, 0, 0), 1e-12));
    QVERIFY(a.YDirection().IsEqual(gp_Dir(0, 1, 0), 1e-12));
    QVERIFY(a.Location().IsEqual(gp_Pnt(0, 0, 5), 1e-12));
    // slightly tilted (upstream edit): stored X projected onto the new plane
    const gp_Dir tn(0, 0.01, 1);
    a = f.alignToStoredAxes(gp_Ax3(gp_Pnt(0, 0, 5), tn, gp_Dir(0, 1, -0.01)));
    QVERIFY(a.Direction().IsEqual(tn, 1e-12));
    QVERIFY(a.XDirection().IsEqual(gp_Dir(1, 0, 0), 1e-9));
    // JSON round trip keeps the axes
    SketchFeature g;
    g.fromJson(f.toJson());
    QVERIFY(g.hasStoredPlaneAxes());
    a = g.alignToStoredAxes(flipped);
    QVERIFY(a.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-12));
    // a mirrored (left-handed) frame from an older file keeps its handedness
    SketchFeature m;
    gp_Ax3 left(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    left.YReverse();
    QVERIFY(!left.Direct());
    m.alignToStoredAxes(left);
    SketchFeature m2;
    m2.fromJson(m.toJson());
    a = m2.alignToStoredAxes(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0)));
    QVERIFY(!a.Direct());
    QVERIFY(a.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-12));
    QVERIFY(a.YDirection().IsEqual(gp_Dir(0, -1, 0), 1e-12));
    // another face (not parallel): the axes start over
    const gp_Ax3 side(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    a = g.alignToStoredAxes(side);
    QVERIFY(a.Direction().IsEqual(side.Direction(), 1e-12));
    QVERIFY(a.XDirection().IsEqual(side.XDirection(), 1e-12));
  }

  void missingPlaneIsAnError() {
    core::Document doc;
    auto f = std::make_unique<SketchFeature>();
    f->prop("plane").setRef(core::ShapeRef::make(999, core::ShapeKind::ConstructionPlane, -1));
    SketchFeature* sk = f.get();
    doc.addFeature(std::move(f));
    doc.recompute();
    QCOMPARE(sk->status(), core::FeatureStatus::Error);
  }

  void displayItemsCarryStatus() {
    core::Document doc;
    auto f = std::make_unique<SketchFeature>();
    f->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    int l = f->sketch().addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    f->sketch().addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l}});
    int c = f->sketch().addGeometry(Geometry::makeCircle({0, 0}, 3));
    f->sketch().geometry(c)->construction = true;
    SketchFeature* sk = f.get();
    doc.addFeature(std::move(f));
    doc.recompute();
    auto items = sk->displayItems();
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].status, GeometryStatus::Fixed);
    QVERIFY(items[1].construction);
    QCOMPARE(items[1].status, GeometryStatus::Free);
    QVERIFY(items[1].points.size() > 8);
  }
};

QTEST_GUILESS_MAIN(FeatureTest)
#include "test_feature.moc"
