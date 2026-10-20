// Chair benchmark, part-level stages (docs/comparison/CHAIR_SPEC.md): LegFrame (stage A),
// Split Body with the YZ origin plane (stage B), the stage-C dress-ups (chamfer, fillet, hole
// placed by a click on a face), the stage-D moves (Free Move with typed values) and the Split
// Body edge cases (deterministic piece order, face / body tools, undo, save/load).
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
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/features/Fillet.h"
#include "part/features/Hole.h"
#include "part/features/Primitives.h"
#include "part/features/Shell.h"
#include "support/ChairFixture.h"
#include "support/TestSketch.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

struct Box3 {
  core::Vec3 mn, mx;
};
Box3 boxOf(const TopoDS_Shape& s) {
  Box3 b;
  core::bbox(s, b.mn, b.mx);
  return b;
}
bool boxIs(const Box3& b, double x0, double x1, double y0, double y1, double z0, double z1, double tol = 0.01) {
  return near(b.mn.x, x0, tol) && near(b.mx.x, x1, tol) && near(b.mn.y, y0, tol) && near(b.mx.y, y1, tol) &&
         near(b.mn.z, z0, tol) && near(b.mx.z, z1, tol);
}

/// Bottom (z = 0, normal -Z) faces of a body, as refs.
std::vector<core::ShapeRef> bottomFaces(const core::Document& doc, core::ObjectId body) {
  std::vector<core::ShapeRef> out;
  const TopoDS_Shape& s = doc.bodyShape(body);
  for (const auto& f : core::subShapes(s, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && pl->Direction().Z() < -0.999 && std::abs(core::centroid(f).z) < 1e-6) {
      core::ShapeRef r = core::ShapeRef::subShape(body, s, f);
      annotateRef(doc, r);
      out.push_back(r);
    }
  }
  return out;
}

/// The planar face of `body` with outward normal `n` whose centroid is nearest to `p`.
core::ShapeRef faceAt(const core::Document& doc, core::ObjectId body, const gp_Dir& n, const gp_Pnt& p) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  core::ShapeRef best;
  double bestD = 1e100;
  for (const auto& f : core::subShapes(s, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (!pl || !pl->Direction().IsEqual(n, 1e-6)) continue;
    double d = std::abs(gp_Vec(pl->Location(), p).Dot(gp_Vec(n)));
    if (d < bestD) {
      bestD = d;
      best = core::ShapeRef::subShape(body, s, f);
    }
  }
  annotateRef(doc, best);
  return best;
}

SplitBodyFeature* addSplit(core::Document& doc, core::ObjectId body, const core::ShapeRef& tool) {
  auto sp = std::make_unique<SplitBodyFeature>();
  sp->prop("bodies").addRef(core::ShapeRef::body(body));
  sp->prop("splitTool").setRef(tool);
  auto* f = static_cast<SplitBodyFeature*>(doc.addFeature(std::move(sp)));
  doc.recompute();
  return f;
}

}  // namespace

class ChairSplitTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
    registerTestSketch();
  }

  /// Stage A: the LegFrame is one connected solid of exactly 3,440,000 mm^3.
  void legFrameMatchesSpec() {
    core::Document doc;
    chair::LegFrame lf = chair::buildLegFrame(doc);
    QCOMPARE(lf.rails->status(), core::FeatureStatus::Ok);
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    const TopoDS_Shape& s = doc.bodyShape(lf.body);
    QCOMPARE(solidsOf(s).size(), size_t(1));
    QVERIFY(near(core::volume(s), chair::kLegFrameVolume, 1e-3));
    QVERIFY(boxIs(boxOf(s), -190, 190, -180, 180, 0, 430));
  }

  /// Stage B + C + D on the leg parts.
  void stagesBCD() {
    core::Document doc;
    chair::LegFrame lf = chair::buildLegFrame(doc);
    // B: split with the YZ origin plane; the x < 0 piece keeps the original body (LegLeft)
    SplitBodyFeature* sp = addSplit(doc, lf.body, core::ShapeRef::originPlane("YZ"));
    QCOMPARE(sp->status(), core::FeatureStatus::Ok);
    QCOMPARE(sp->createdBodies().size(), size_t(1));
    const core::ObjectId left = lf.body, right = sp->createdBodies().front();
    QVERIFY(near(core::volume(doc.bodyShape(left)), chair::kHalfVolume, 1e-3));
    QVERIFY(near(core::volume(doc.bodyShape(right)), chair::kHalfVolume, 1e-3));
    QVERIFY(boxIs(boxOf(doc.bodyShape(left)), -190, 0, -180, 180, 0, 430));
    QVERIFY(boxIs(boxOf(doc.bodyShape(right)), 0, 190, -180, 180, 0, 430));
    QCOMPARE(solidsOf(doc.bodyShape(left)).size(), size_t(1));
    QCOMPARE(doc.body(right)->name, doc.body(left)->name + " (2)");
    // names survive the split (the leg sides are still named after their sketch curves)
    QVERIFY(!sp->namedShapes().empty());

    // C: LegLeft chamfer 3 on the 8 bottom edges (the two bottom faces select them)
    auto ch = std::make_unique<ChamferFeature>();
    for (const auto& r : bottomFaces(doc, left)) ch->prop("edges").addRef(r);
    QCOMPARE(ch->prop("edges").refs().size(), size_t(2));
    ch->prop("distance").setDouble(chair::kChamfer);
    core::Feature* chf = doc.addFeature(std::move(ch));
    doc.recompute();
    QCOMPARE(chf->status(), core::FeatureStatus::Ok);
    QVERIFY2(near(core::volume(doc.bodyShape(left)), chair::kLeftAfterChamfer, 0.5),
             qPrintable(QString::number(core::volume(doc.bodyShape(left)), 'f', 3)));
    // C: LegRight fillet R5 on the 8 bottom edges
    auto fi = std::make_unique<FilletFeature>();
    for (const auto& r : bottomFaces(doc, right)) fi->prop("edges").addRef(r);
    fi->prop("radius").setDouble(chair::kFillet);
    core::Feature* fif = doc.addFeature(std::move(fi));
    doc.recompute();
    QCOMPARE(fif->status(), core::FeatureStatus::Ok);
    // C: LegRight hole D10 through all along X centred at (170, 0, 150): a click on the
    // rail's outer face (x = 180) at y = 0, z = 150
    auto ho = std::make_unique<HoleFeature>();
    core::ShapeRef face = faceAt(doc, right, gp_Dir(1, 0, 0), gp_Pnt(180, 0, 150));
    face.point = {180, 0, 150};
    face.hasPoint = true;
    ho->prop("points").addRef(face);
    ho->prop("diameter").setDouble(chair::kHoleDiameter);
    ho->prop("extentType").setEnum("All");
    ho->prop("targets").addRef(core::ShapeRef::body(right));  // LegRight only (in the assembly it is its own part)
    core::Feature* hof = doc.addFeature(std::move(ho));
    doc.recompute();
    QCOMPARE(hof->status(), core::FeatureStatus::Ok);
    QVERIFY2(near(core::volume(doc.bodyShape(right)), chair::kRightAfterFilletHole, 1.0),
             qPrintable(QString::number(core::volume(doc.bodyShape(right)), 'f', 3)));
    QVERIFY(near(core::volume(doc.bodyShape(left)), chair::kLeftAfterChamfer, 0.5));  // untouched by the hole

    // D: translate the legs -15 / +15 with Free Move (typed distances)
    for (auto [body, dx] : {std::pair{left, -chair::kStageDShift}, std::pair{right, chair::kStageDShift}}) {
      auto mv = std::make_unique<MoveBodyFeature>();
      mv->prop("bodies").addRef(core::ShapeRef::body(body));
      mv->prop("moveType").setEnum("Free Move");
      mv->prop("dx").setDouble(dx);
      core::Feature* m = doc.addFeature(std::move(mv));
      doc.recompute();
      QCOMPARE(m->status(), core::FeatureStatus::Ok);
    }
    QVERIFY(boxIs(boxOf(doc.bodyShape(left)), -205, -15, -180, 180, 0, 430));
    QVERIFY(boxIs(boxOf(doc.bodyShape(right)), 15, 205, -180, 180, 0, 430));
    QVERIFY(near(core::volume(doc.bodyShape(right)), chair::kRightAfterFilletHole, 1.0));

    // the whole history replays identically from the file
    QTemporaryDir dir;
    const std::string path = dir.filePath("chair-legs.fcad").toStdString();
    QVERIFY(doc.save(path));
    core::Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QVERIFY(near(core::volume(re.bodyShape(left)), chair::kLeftAfterChamfer, 0.5));
    QVERIFY(near(core::volume(re.bodyShape(right)), chair::kRightAfterFilletHole, 1.0));
    QVERIFY(boxIs(boxOf(re.bodyShape(right)), 15, 205, -180, 180, 0, 430));
  }

  /// Stage D for the seat envelope: Free Move rotation -5 deg about the X-parallel axis
  /// through (0, 200, 430) gives the spec's bounding box (the fillets and the slot do not
  /// touch the extreme points, so the slab + backrest boxes are enough).
  void seatRotationMatchesSpec() {
    core::Document doc;
    auto slab = std::make_unique<BoxFeature>();  // X[-210, 210] x Y[-200, 200] x Z[430, 460]
    slab->prop("length").setDouble(420);
    slab->prop("width").setDouble(400);
    slab->prop("height").setDouble(30);
    auto off = std::make_unique<OffsetPlaneFeature>();
    off->prop("plane").setRef(core::ShapeRef::originPlane("XY"));
    off->prop("distance").setDouble(430);
    core::Feature* pl = doc.addFeature(std::move(off));
    slab->prop("plane").setRef(core::ShapeRef::feature(pl->id(), core::ShapeKind::ConstructionPlane));
    slab->prop("operation").setEnum("New Body");
    core::Feature* sf = doc.addFeature(std::move(slab));
    auto back = std::make_unique<BoxFeature>();  // X[-210, 210] x Y[175, 200] x Z[430, 840]
    back->prop("plane").setRef(core::ShapeRef::feature(pl->id(), core::ShapeKind::ConstructionPlane));
    back->prop("y").setDouble(187.5);
    back->prop("length").setDouble(420);
    back->prop("width").setDouble(25);
    back->prop("height").setDouble(410);
    back->prop("operation").setEnum("Join");
    doc.addFeature(std::move(back));
    doc.recompute();
    const core::ObjectId seat = sf->createdBodies().front();
    QVERIFY(boxIs(boxOf(doc.bodyShape(seat)), -210, 210, -200, 200, 430, 840));
    auto mv = std::make_unique<MoveBodyFeature>();
    mv->prop("bodies").addRef(core::ShapeRef::body(seat));
    mv->prop("moveType").setEnum("Free Move");
    mv->prop("pivotType").setEnum("Coordinates");
    mv->prop("py").setDouble(200);
    mv->prop("pz").setDouble(430);
    mv->prop("rx").setDouble(-5);
    core::Feature* m = doc.addFeature(std::move(mv));
    doc.recompute();
    QCOMPARE(m->status(), core::FeatureStatus::Ok);
    QVERIFY2(boxIs(boxOf(doc.bodyShape(seat)), -210, 210, -198.478, 235.734, 430.0, 840.619), "seat bbox after stage D");
  }

  /// Piece order is a property of the tool, not of OCCT's output order: behind the plane first.
  void splitOrderFollowsPlaneNormal() {
    core::Document doc;
    chair::LegFrame lf = chair::buildLegFrame(doc);
    // XZ plane: normal -Y, so the y > 0 (back) side comes first. The back half falls apart
    // into two pieces (no back cross rail); equal distances are ordered by x (shapeLess).
    SplitBodyFeature* sp = addSplit(doc, lf.body, core::ShapeRef::originPlane("XZ"));
    QCOMPARE(sp->status(), core::FeatureStatus::Ok);
    QCOMPARE(sp->createdBodies().size(), size_t(2));
    core::Vec3 c0 = core::centroid(doc.bodyShape(lf.body));
    core::Vec3 c1 = core::centroid(doc.bodyShape(sp->createdBodies()[0]));
    core::Vec3 c2 = core::centroid(doc.bodyShape(sp->createdBodies()[1]));
    QVERIFY(c0.y > 0 && c0.x < 0);  // back left keeps the body
    QVERIFY(c1.y > 0 && c1.x > 0);  // back right
    QVERIFY(c2.y < 0);              // the front half carries the cross rail
    QVERIFY(near(core::volume(doc.bodyShape(sp->createdBodies()[1])), 2 * 40 * 40 * 430 + 2 * 20 * 140 * 40 + 300 * 20 * 40, 1e-3));
    // the same order after an upstream edit (taller legs) and a full replay
    lf.legs->prop("distance").setDouble(500);
    doc.notifyFeatureChanged(lf.legs->id());
    doc.recompute();
    QVERIFY(core::centroid(doc.bodyShape(lf.body)).x < 0 && core::centroid(doc.bodyShape(lf.body)).y > 0);
    QVERIFY(core::centroid(doc.bodyShape(sp->createdBodies()[1])).y < 0);
  }

  /// Split with a face (extended), a non-planar face and a body; undo/redo and file round trip.
  void splitToolsUndoAndReload() {
    core::Document doc;
    auto a = std::make_unique<BoxFeature>();  // 40 x 20 x 10 centred on the origin
    a->prop("length").setDouble(40);
    a->prop("operation").setEnum("New Body");
    core::Feature* af = doc.addFeature(std::move(a));
    auto c = std::make_unique<CylinderFeature>();  // D10 x 10 cylinder on the box's end face x = 20
    c->prop("x").setDouble(20);
    c->prop("diameter").setDouble(10);
    c->prop("height").setDouble(10);
    c->prop("operation").setEnum("New Body");
    core::Feature* cf = doc.addFeature(std::move(c));
    doc.recompute();
    const core::ObjectId box = af->createdBodies().front(), cyl = cf->createdBodies().front();
    // the cylinder's side face, extended to the full surface, cuts a half disc off the box end
    core::ShapeRef side;
    for (const auto& f : core::subShapes(doc.bodyShape(cyl), TopAbs_FACE))
      if (!core::facePlane(f)) side = core::ShapeRef::subShape(cyl, doc.bodyShape(cyl), f);
    QVERIFY(side.isValid());
    SplitBodyFeature* sp = addSplit(doc, box, side);
    QCOMPARE(sp->status(), core::FeatureStatus::Ok);
    QCOMPARE(sp->createdBodies().size(), size_t(1));
    double inside = M_PI * 25 * 10 / 2;  // half of the D10 x 10 cylinder lies inside the box (x <= 20)
    double v1 = core::volume(doc.bodyShape(box)), v2 = core::volume(doc.bodyShape(sp->createdBodies().front()));
    QVERIFY(near(v1 + v2, 8000, 1e-3));
    QVERIFY(near(std::min(v1, v2), inside, 1e-2));

    // undo / redo of the split through the command
    core::UndoStack& undo = doc.undoStack();
    auto sp2 = std::make_unique<SplitBodyFeature>();
    sp2->prop("bodies").addRef(core::ShapeRef::body(box));
    sp2->prop("splitTool").setRef(core::ShapeRef::originPlane("YZ"));
    undo.push(std::make_unique<core::AddFeatureCommand>(doc, std::move(sp2)));
    const int bodiesAfter = static_cast<int>(doc.liveBodies().size());
    undo.undo();
    QCOMPARE(static_cast<int>(doc.liveBodies().size()), bodiesAfter - 1);
    undo.redo();
    QCOMPARE(static_cast<int>(doc.liveBodies().size()), bodiesAfter);

    // body tool: the cylinder body splits the box like its (bounded) solid
    core::Document doc2;
    auto b2 = std::make_unique<BoxFeature>();
    b2->prop("operation").setEnum("New Body");  // 20 x 20 x 10 centred
    core::Feature* b2f = doc2.addFeature(std::move(b2));
    auto t2 = std::make_unique<CylinderFeature>();  // D10 x 30 through the box
    t2->prop("diameter").setDouble(10);
    t2->prop("height").setDouble(30);
    t2->prop("direction").setEnum("Symmetric");
    t2->prop("operation").setEnum("New Body");
    core::Feature* t2f = doc2.addFeature(std::move(t2));
    doc2.recompute();
    SplitBodyFeature* bs = addSplit(doc2, b2f->createdBodies().front(), core::ShapeRef::body(t2f->createdBodies().front()));
    QCOMPARE(bs->status(), core::FeatureStatus::Ok);
    QCOMPARE(bs->createdBodies().size(), size_t(1));
    std::vector<double> vols{core::volume(doc2.bodyShape(b2f->createdBodies().front())),
                             core::volume(doc2.bodyShape(bs->createdBodies().front()))};
    std::sort(vols.begin(), vols.end());
    QVERIFY(near(vols[0], M_PI * 25 * 10, 1e-2));
    QVERIFY(near(vols[1], 4000 - M_PI * 25 * 10, 1e-2));
    // reload: same pieces, same order
    QTemporaryDir dir;
    const std::string path = dir.filePath("split.fcad").toStdString();
    QVERIFY(doc2.save(path));
    core::Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QVERIFY(near(core::volume(re.bodyShape(b2f->createdBodies().front())),
                 core::volume(doc2.bodyShape(b2f->createdBodies().front())), 1e-6));
  }
};

QTEST_GUILESS_MAIN(ChairSplitTest)
#include "test_chair_split.moc"
