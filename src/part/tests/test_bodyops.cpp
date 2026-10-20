// Tests for the round-2 body operations: variable-radius Fillet, Scale (uniform / non-uniform
// about a point), Thicken (one side / symmetric / join), Rib (To Next / Depth) and Split Face.
// Expected volumes are analytic. Undo/redo and save/load go through the normal commands.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/PartUtils.h"
#include "part/features/BodyOps.h"
#include "part/features/Fillet.h"
#include "part/features/Primitives.h"
#include "support/TestSketch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {

bool rel(double a, double b, double tol) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

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

/// Adds a feature through the undoable command (as the tools do) and recomputes.
template <class F>
F* push(core::Document& doc, std::unique_ptr<F> f) {
  F* raw = f.get();
  doc.undoStack().push(std::make_unique<core::AddFeatureCommand>(doc, std::move(f)));
  doc.recompute();
  return raw;
}

/// Box primitive centred at (x, y) on XY, z from z0 (via a Z offset of the plane is not
/// needed here: boxes start on z = 0).
std::unique_ptr<BoxFeature> box(double l, double w, double h, const char* op, double x = 0, double y = 0) {
  auto b = std::make_unique<BoxFeature>();
  b->prop("x").setDouble(x);
  b->prop("y").setDouble(y);
  b->prop("length").setDouble(l);
  b->prop("width").setDouble(w);
  b->prop("height").setDouble(h);
  b->prop("operation").setEnum(op);
  return b;
}

core::ShapeRef ref(const core::Document& doc, core::ObjectId body, const TopoDS_Shape& sub) {
  core::ShapeRef r = core::ShapeRef::subShape(body, doc.bodyShape(body), sub);
  annotateRef(doc, r);
  return r;
}

/// Linear edge of `body` whose two vertices both satisfy `pred`.
core::ShapeRef edgeWhere(const core::Document& doc, core::ObjectId body, const std::function<bool(const gp_Pnt&)>& pred) {
  for (const auto& e : core::subShapes(doc.bodyShape(body), TopAbs_EDGE)) {
    TopoDS_Vertex a, b;
    TopExp::Vertices(TopoDS::Edge(e), a, b);
    if (pred(BRep_Tool::Pnt(a)) && pred(BRep_Tool::Pnt(b))) return ref(doc, body, e);
  }
  return {};
}

/// Planar face of `body` with outward normal `n` (first match).
core::ShapeRef faceWithNormal(const core::Document& doc, core::ObjectId body, const gp_Dir& n) {
  for (const auto& f : core::subShapes(doc.bodyShape(body), TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (!pl) continue;
    if (pl->Direction().IsEqual(n, 1e-6)) return ref(doc, body, f);  // facePlane: outward normal
  }
  return {};
}

core::ShapeRef vertexAt(const core::Document& doc, core::ObjectId body, const gp_Pnt& p) {
  for (const auto& v : core::subShapes(doc.bodyShape(body), TopAbs_VERTEX))
    if (BRep_Tool::Pnt(TopoDS::Vertex(v)).Distance(p) < 1e-7) return ref(doc, body, v);
  return {};
}

void touch(core::Document& doc, core::Feature* f) {
  doc.notifyFeatureChanged(f->id());
  doc.recompute();
}

}  // namespace

class BodyOpsTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools_};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void radiusLaw() {
    std::string err;
    auto law = FilletFeature::radiusLaw(2, 4, QJsonArray{QJsonArray{0.5, 6}, QJsonArray{0.25, 3}}, false, &err);
    QCOMPARE(law.size(), size_t(4));
    QCOMPARE(law[1].first, 0.25);
    QCOMPARE(law[2].second, 6.0);
    auto flipped = FilletFeature::radiusLaw(2, 4, QJsonArray{QJsonArray{0.25, 3}}, true, &err);
    QCOMPARE(flipped.front().second, 4.0);
    QCOMPARE(flipped[1].first, 0.75);
    QCOMPARE(flipped.back().second, 2.0);
    QVERIFY(FilletFeature::radiusLaw(2, -1, QJsonArray(), false, &err).empty());
    QVERIFY(!err.empty());
  }

  void variableFillet() {
    core::Document doc;
    BoxFeature* bf = push(doc, box(20, 20, 20, "New Body"));
    core::ObjectId b = bf->createdBodies().front();
    const double full = 8000;
    // vertical edge at x = y = 10
    core::ShapeRef e = edgeWhere(doc, b, [](const gp_Pnt& p) { return std::abs(p.X() - 10) < 1e-9 && std::abs(p.Y() - 10) < 1e-9; });
    QVERIFY(e.isValid());
    auto fi = std::make_unique<FilletFeature>();
    fi->prop("edges").addRef(e);
    fi->prop("radius").setDouble(2);
    FilletFeature* ff = push(doc, std::move(fi));
    QCOMPARE(ff->status(), core::FeatureStatus::Ok);
    const double k = 1 - M_PI / 4;  // removed area per r^2 of a 90 degree edge
    QVERIFY(rel(full - core::volume(doc.bodyShape(b)), k * 4 * 20, 1e-6));

    // linear 2 -> 4 along the edge: removed volume = k * L * (a^2 + ab + b^2) / 3
    ff->prop("radiusType").setEnum("Variable");
    ff->prop("endRadius").setDouble(4);
    ff->updatePropertyFlags();
    QVERIFY(!ff->prop("endRadius").flags().hidden);
    touch(doc, ff);
    QVERIFY2(ff->status() == core::FeatureStatus::Ok, ff->statusMessage().c_str());
    const double linear = k * 20 * (4 + 8 + 16) / 3.0;
    double removed = full - core::volume(doc.bodyShape(b));
    QVERIFY2(rel(removed, linear, 0.02), qPrintable(QString::number(removed)));
    QVERIFY(core::isValidShape(doc.bodyShape(b)));
    // the radius grows along the edge: the fillet is wider at one end than at the other
    core::Vec3 mn, mx;
    // flipped law: same volume (symmetric formula), still valid
    ff->prop("flipVariable").setBool(true);
    touch(doc, ff);
    QCOMPARE(ff->status(), core::FeatureStatus::Ok);
    QVERIFY(rel(full - core::volume(doc.bodyShape(b)), linear, 0.02));

    // an intermediate radius at mid-length bulges the fillet: more material removed
    ff->prop("flipVariable").setBool(false);
    ff->prop("endRadius").setDouble(2);
    ff->prop("radiusPoints").setJson(QJsonArray{QJsonArray{0.5, 5}});
    touch(doc, ff);
    QVERIFY2(ff->status() == core::FeatureStatus::Ok, ff->statusMessage().c_str());
    removed = full - core::volume(doc.bodyShape(b));
    QVERIFY2(removed > k * 4 * 20 * 2 && removed < k * 25 * 20, qPrintable(QString::number(removed)));
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mx.x, 10, 1e-6));

    // round trip keeps the variable definition
    QTemporaryDir dir;
    const std::string path = dir.filePath("vf.fcad").toStdString();
    QVERIFY(doc.save(path));
    core::Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QVERIFY(rel(core::volume(re.bodyShape(b)), core::volume(doc.bodyShape(b)), 1e-9));
    // undo the fillet entirely
    doc.undoStack().undo();
    doc.recompute();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), full, 1e-9));
  }

  void scale() {
    core::Document doc;
    BoxFeature* bf = push(doc, box(20, 20, 10, "New Body"));
    core::ObjectId b = bf->createdBodies().front();
    auto sc = std::make_unique<ScaleBodyFeature>();
    sc->prop("bodies").addRef(core::ShapeRef::body(b));
    sc->prop("factor").setDouble(2);
    ScaleBodyFeature* sf = push(doc, std::move(sc));
    QCOMPARE(sf->status(), core::FeatureStatus::Ok);
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000 * 8, 1e-9));
    core::Vec3 mn, mx;
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mn.x, -20, 1e-7) && rel(mx.z, 20, 1e-7) && rel(mn.z, 0, 1e-7));  // about the origin

    // non-uniform about the box corner (10, 10, 10) of the unscaled box
    sf->prop("scaleType").setEnum("Non-Uniform");
    sf->prop("factorX").setDouble(1);
    sf->prop("factorY").setDouble(2);
    sf->prop("factorZ").setDouble(3);
    // the vertex lives in the input body (before the scale)
    doc.setRollbackIndex(doc.indexOf(sf->id()));
    core::ShapeRef corner = vertexAt(doc, b, gp_Pnt(10, 10, 10));
    doc.setRollbackIndex(doc.featureCount());
    QVERIFY(corner.isValid());
    sf->prop("point").setRef(corner);
    touch(doc, sf);
    QVERIFY2(sf->status() == core::FeatureStatus::Ok, sf->statusMessage().c_str());
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000 * 6, 1e-9));
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mn.x, -10, 1e-7) && rel(mx.x, 10, 1e-7));
    QVERIFY(rel(mn.y, -30, 1e-7) && rel(mx.y, 10, 1e-7));
    QVERIFY(rel(mn.z, -20, 1e-7) && rel(mx.z, 10, 1e-7));
    // invalid factor
    sf->prop("factorY").setDouble(0);
    touch(doc, sf);
    QCOMPARE(sf->status(), core::FeatureStatus::Error);
    doc.undoStack().undo();
    doc.recompute();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000, 1e-9));
  }

  void thicken() {
    core::Document doc;
    BoxFeature* bf = push(doc, box(20, 20, 10, "New Body"));
    core::ObjectId b = bf->createdBodies().front();
    core::ShapeRef top = faceWithNormal(doc, b, gp::DZ());
    QVERIFY(top.isValid());
    auto th = std::make_unique<ThickenFeature>();
    th->prop("faces").addRef(top);
    th->prop("thickness").setDouble(3);
    ThickenFeature* tf = push(doc, std::move(th));
    QVERIFY2(tf->status() == core::FeatureStatus::Ok, tf->statusMessage().c_str());
    QCOMPARE(tf->createdBodies().size(), size_t(1));  // New Body by default
    core::ObjectId nb = tf->createdBodies().front();
    QVERIFY(rel(core::volume(doc.bodyShape(nb)), 1200, 1e-6));
    core::Vec3 mn, mx;
    tightBox(doc.bodyShape(nb), mn, mx);
    QVERIFY(rel(mn.z, 10, 1e-6) && rel(mx.z, 13, 1e-6));
    // negative: inward
    tf->prop("thickness").setDouble(-3);
    touch(doc, tf);
    QCOMPARE(tf->status(), core::FeatureStatus::Ok);
    tightBox(doc.bodyShape(nb), mn, mx);
    QVERIFY(rel(mn.z, 7, 1e-6) && rel(mx.z, 10, 1e-6));
    QVERIFY(rel(core::volume(doc.bodyShape(nb)), 1200, 1e-6));
    // symmetric 4: 2 on each side
    tf->prop("thickness").setDouble(4);
    tf->prop("direction").setEnum("Symmetric");
    touch(doc, tf);
    QVERIFY2(tf->status() == core::FeatureStatus::Ok, tf->statusMessage().c_str());
    tightBox(doc.bodyShape(nb), mn, mx);
    QVERIFY(rel(mn.z, 8, 1e-6) && rel(mx.z, 12, 1e-6));
    // join outward onto the box
    tf->prop("direction").setEnum("One Side");
    tf->prop("thickness").setDouble(3);
    tf->prop("operation").setEnum("Join");
    touch(doc, tf);
    QVERIFY2(tf->status() == core::FeatureStatus::Ok, tf->statusMessage().c_str());
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000 + 1200, 1e-6));
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    doc.undoStack().undo();
    doc.recompute();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000, 1e-9));
    doc.undoStack().redo();
    doc.recompute();
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 5200, 1e-6));
  }

  /// L-bracket: plate 40 x 20 x 5 and a wall 5 x 20 x 30 at the left end; a rib curve from the
  /// wall (x = -15, z = 25) down to the plate (x = 15, z = 5) in the XZ plane.
  void ribToNextAndDepth() {
    core::Document doc;
    BoxFeature* plate = push(doc, box(40, 20, 5, "New Body"));
    core::ObjectId b = plate->createdBodies().front();
    push(doc, box(5, 20, 30, "Join", -17.5, 0));
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000 + 2500, 1e-9));
    auto sk = std::make_unique<TestSketch>();
    sk->prop("plane").setRef(core::ShapeRef::originPlane("XZ"));
    sk->lines = {{-15, 25}, {15, 5}};
    TestSketch* s = push(doc, std::move(sk));
    auto rib = std::make_unique<RibFeature>();
    rib->prop("curves").addRef(s->lineRef(0));
    rib->prop("thickness").setDouble(2);
    RibFeature* rf = push(doc, std::move(rib));
    QVERIFY2(rf->status() == core::FeatureStatus::Ok, rf->statusMessage().c_str());
    // the triangle between the curve and the inside corner, 2 thick
    const double tri = 0.5 * 30 * 20 * 2;
    QVERIFY2(rel(core::volume(doc.bodyShape(b)), 6500 + tri, 1e-6), qPrintable(QString::number(core::volume(doc.bodyShape(b)))));
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    core::Vec3 mn, mx;
    tightBox(doc.bodyShape(b), mn, mx);
    QVERIFY(rel(mn.y, -10, 1e-6) && rel(mx.y, 10, 1e-6));  // symmetric rib stays inside the bracket width
    // one side: same volume, the rib sits on one side of the sketch plane
    rf->prop("thicknessType").setEnum("One Side");
    touch(doc, rf);
    QCOMPARE(rf->status(), core::FeatureStatus::Ok);
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 6500 + tri, 1e-6));
    // depth 3 toward the corner: a 3 mm strip along the curve (merged with the bracket)
    rf->prop("thicknessType").setEnum("Symmetric");
    rf->prop("depthType").setEnum("Depth");
    rf->prop("depth").setDouble(3);
    rf->prop("flip").setBool(true);
    touch(doc, rf);
    QVERIFY2(rf->status() == core::FeatureStatus::Ok, rf->statusMessage().c_str());
    double v = core::volume(doc.bodyShape(b));
    const double strip = 3 * std::hypot(30, 20) * 2;
    QVERIFY2(v > 6500 + strip * 0.8 && v < 6500 + strip, qPrintable(QString::number(v - 6500)));
    // a closed / missing curve is an error
    rf->prop("curves").setRefs({});
    touch(doc, rf);
    QCOMPARE(rf->status(), core::FeatureStatus::Error);
  }

  void splitFace() {
    core::Document doc;
    BoxFeature* bf = push(doc, box(20, 20, 10, "New Body"));
    core::ObjectId b = bf->createdBodies().front();
    QCOMPARE(core::subShapes(doc.bodyShape(b), TopAbs_FACE).size(), size_t(6));
    auto sp = std::make_unique<SplitFaceFeature>();
    sp->prop("faces").addRef(faceWithNormal(doc, b, gp::DZ()));
    sp->prop("faces").addRef(faceWithNormal(doc, b, gp::DX()));
    sp->prop("splitTool").setRef(core::ShapeRef::originPlane("XZ"));
    SplitFaceFeature* sf = push(doc, std::move(sp));
    QVERIFY2(sf->status() == core::FeatureStatus::Ok, sf->statusMessage().c_str());
    // top and +X faces each split in two; still one solid with the same volume
    QCOMPARE(core::subShapes(doc.bodyShape(b), TopAbs_FACE).size(), size_t(8));
    QCOMPARE(core::subShapes(doc.bodyShape(b), TopAbs_SOLID).size(), size_t(1));
    QVERIFY(rel(core::volume(doc.bodyShape(b)), 4000, 1e-9));
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    // only the top face this time
    doc.setRollbackIndex(doc.indexOf(sf->id()));  // refs point into the unsplit input body
    core::ShapeRef top = faceWithNormal(doc, b, gp::DZ());
    doc.setRollbackIndex(doc.featureCount());
    sf->prop("faces").setRefs({top});
    touch(doc, sf);
    QVERIFY2(sf->status() == core::FeatureStatus::Ok, sf->statusMessage().c_str());
    QCOMPARE(core::subShapes(doc.bodyShape(b), TopAbs_FACE).size(), size_t(7));
    // save / load / undo
    QTemporaryDir dir;
    const std::string path = dir.filePath("sf.fcad").toStdString();
    QVERIFY(doc.save(path));
    core::Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QCOMPARE(core::subShapes(re.bodyShape(b), TopAbs_FACE).size(), size_t(7));
    doc.undoStack().undo();
    doc.recompute();
    QCOMPARE(core::subShapes(doc.bodyShape(b), TopAbs_FACE).size(), size_t(6));
  }

 private:
  core::ToolRegistry tools_;
};

QTEST_GUILESS_MAIN(BodyOpsTest)
#include "test_bodyops.moc"
