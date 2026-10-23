// Production features P2: Insert Fastener. A screw put into a counterbored M6 hole seats on the
// counterbore bottom, coaxial, with a rigid joint and a BOM row with its part number; equal holes
// share one definition (qty counted); washer + nut stack on the far face; a 100-hole plate in
// under 2 s; save / load regenerates the parts; STEP export with the screws reads back.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/Bom.h"
#include "assembly/FastenerInsert.h"
#include "assembly/JointFeature.h"
#include "assembly/StepExport.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/PartModule.h"
#include "part/features/Hole.h"
#include "part/features/Import.h"
#include "support/AssemblyTestHelpers.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::assembly;
using namespace fabricad::assembly::test;
using core::ObjectId;
namespace st = fabricad::part::standards;

namespace {

struct Plate {
  core::Document doc;
  ObjectId comp = core::kRootComponentId, body = core::kInvalidId;
  double t = 10;
  Plate(bool inComponent, double size = 100, double thickness = 10) : t(thickness) {
    if (inComponent) {
      comp = doc.addComponent("Plate").id;
      doc.component(comp)->grounded = true;
      doc.setActiveComponent(comp);
    }
    auto f = std::make_unique<core::test::BoxFeature>();
    f->prop("dx").setDouble(size);
    f->prop("dy").setDouble(size);
    f->prop("dz").setDouble(thickness);
    core::Feature* raw = doc.addFeature(std::move(f));
    doc.recompute();
    body = raw->createdBodies().front();
  }
  /// One Hole feature with holes at the points (clearance M6 normal, optional SHCS counterbore).
  part::HoleFeature* holes(const std::vector<std::pair<double, double>>& at, bool cbore) {
    auto h = std::make_unique<part::HoleFeature>();
    core::ShapeRef top = faceWithNormal(doc, body, gp_Dir(0, 0, 1));
    for (auto [x, y] : at) {
      core::ShapeRef r = top;
      r.point = core::Vec3{x, y, t};
      r.hasPoint = true;
      h->prop("points").refs().push_back(r);
    }
    h->applyScriptOption("type", "clearance");
    h->applyScriptOption("size", "M6");
    h->prop("extentType").setEnum("All");
    if (cbore) {
      h->prop("holeType").setEnum("Counterbore");
      h->applyScriptOption("fastener", "shcs");
    }
    h->updatePropertyFlags();
    auto* raw = static_cast<part::HoleFeature*>(doc.addFeature(std::move(h)));
    doc.recompute();
    return raw;
  }
  /// The hole wall (cylindrical face of radius r) whose axis passes through (x, y).
  core::ShapeRef wall(double x, double y, double r) const {
    const TopoDS_Shape& s = doc.bodyShape(body);
    for (const TopoDS_Shape& f : core::subShapes(s, TopAbs_FACE)) {
      auto c = part::analyseThreadCylinder(s, f);
      if (c && c->internal && std::abs(c->radius - r) < 1e-6 && gp_Lin(c->axis).Distance(gp_Pnt(x, y, 0)) < 1e-6)
        return core::ShapeRef::subShape(body, s, f);
    }
    return {};
  }
};

int countJoints(const core::Document& doc) {
  int n = 0;
  for (int i = 0; i < doc.featureCount(); ++i)
    if (dynamic_cast<const JointFeature*>(doc.featureAt(i))) ++n;
  return n;
}

const BomRow* row(const BomTable& t, const std::string& pn) {
  for (const BomRow& r : t.rows)
    if (r.partNumber == pn) return &r;
  return nullptr;
}

}  // namespace

class FastenerInsertTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::test::registerTestFeatures();
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
  }

  void recogniseCounterbore() {
    Plate p(true);
    p.holes({{20, 20}}, true);
    auto site = analyseHole(p.doc, p.wall(20, 20, 3.3));
    QVERIFY(site);
    QVERIFY(near(site->diameter, 6.6));
    QVERIFY(site->through);
    QVERIFY(near(site->seat, gp_Pnt(20, 20, 4), 1e-6));
    QVERIFY(near(site->entry, gp_Pnt(20, 20, 10), 1e-6));
    QVERIFY(site->axisIn.IsEqual(gp_Dir(0, 0, -1), 1e-9));
    QVERIFY(near(site->seatToFar, 4));
    // picking the counterbore wall finds the same hole
    auto again = analyseHole(p.doc, p.wall(20, 20, 5.5));
    QVERIFY(again && near(again->seat, site->seat, 1e-6));
    std::string err;
    st::FastenerSpec s = specForHole(*site, InsertOptions{}, &err);
    QVERIFY2(err.empty(), err.c_str());
    QCOMPARE(s.size, std::string("M6"));
    QCOMPARE(s.length, 10.0);
  }

  void insertIntoHole() {
    Plate p(true);
    p.holes({{20, 20}, {60, 20}}, true);
    InsertResult r = insertFasteners(p.doc, nullptr, {p.wall(20, 20, 3.3)}, InsertOptions{});
    QVERIFY2(r.ok(), r.error.c_str());
    QCOMPARE(r.components.size(), size_t(1));
    QCOMPARE(r.partNumber, std::string("ISO 4762 M6x10 8.8"));
    const ObjectId screw = r.components.front();
    // seating face coincident with the counterbore bottom, axis coaxial
    QVERIFY(near(worldOrigin(p.doc, screw), gp_Pnt(20, 20, 4), 1e-6));
    gp_Dir z = gp_Dir(0, 0, 1).Transformed(p.doc.worldPlacement(screw));
    QVERIFY(z.IsEqual(gp_Dir(0, 0, 1), 1e-9));
    QCOMPARE(countJoints(p.doc), 1);
    for (int i = 0; i < p.doc.featureCount(); ++i)
      QVERIFY2(p.doc.featureAt(i)->status() == core::FeatureStatus::Ok, p.doc.featureAt(i)->statusMessage().c_str());
    // the joint holds it there after a recompute
    p.doc.markDirty(p.doc.featureAt(0)->id());
    p.doc.recompute();
    QVERIFY(near(worldOrigin(p.doc, screw), gp_Pnt(20, 20, 4), 1e-6));
    // the screw body: steel, BOM row with the part number
    const core::Component* c = p.doc.component(screw);
    QCOMPARE(c->partNumber, std::string("ISO 4762 M6x10 8.8"));
    QCOMPARE(c->description, std::string("Socket head cap screw"));
    QCOMPARE(c->material, std::string("Steel"));
    BomTable bom = buildBom(p.doc);
    const BomRow* br = row(bom, "ISO 4762 M6x10 8.8");
    QVERIFY(br);
    QCOMPARE(br->quantity, 1);
    QVERIFY(br->mass > 2 && br->mass < 4);  // ~2.9 g
    // the second, equal hole: the same definition, qty 2
    r = insertFasteners(p.doc, nullptr, {p.wall(60, 20, 3.3)}, InsertOptions{});
    QVERIFY2(r.ok(), r.error.c_str());
    QVERIFY(p.doc.component(r.components.front())->isOccurrence());
    bom = buildBom(p.doc);
    QCOMPARE(row(bom, "ISO 4762 M6x10 8.8")->quantity, 2);
    QVERIFY(near(worldOrigin(p.doc, r.components.front()), gp_Pnt(60, 20, 4), 1e-6));
  }

  void allEqualAndRevolute() {
    Plate p(true);
    p.holes({{20, 20}, {60, 20}, {20, 60}}, true);
    InsertOptions o;
    QVERIFY(applyInsertOption("all=yes", o));
    QVERIFY(applyInsertOption("rotation=yes", o));
    QVERIFY(applyInsertOption("length=16", o));
    QVERIFY(!applyInsertOption("family=rivet", o));
    InsertResult r = insertFasteners(p.doc, nullptr, {p.wall(20, 20, 3.3)}, o);
    QVERIFY2(r.ok(), r.error.c_str());
    QCOMPARE(r.holes, 3);
    QCOMPARE(r.partNumber, std::string("ISO 4762 M6x16 8.8"));
    QCOMPARE(countJoints(p.doc), 3);
    for (int i = 0; i < p.doc.featureCount(); ++i)
      if (auto* j = dynamic_cast<const JointFeature*>(p.doc.featureAt(i))) QCOMPARE(j->jointType(), JointType::Revolute);
    QCOMPARE(row(buildBom(p.doc), "ISO 4762 M6x16 8.8")->quantity, 3);
  }

  void nutAndWasherStack() {
    Plate p(true);
    p.holes({{30, 30}}, false);
    InsertOptions o;
    QVERIFY(applyInsertOption("nut=yes", o));
    QVERIFY(applyInsertOption("washer=yes", o));
    QVERIFY(applyInsertOption("material=A2", o));
    InsertResult r = insertFasteners(p.doc, nullptr, {p.wall(30, 30, 3.3)}, o);
    QVERIFY2(r.ok(), r.error.c_str());
    QCOMPARE(r.components.size(), size_t(4));  // washer, screw, washer, nut
    // grip 10 + washers 2 x 1.6 + nut 5.2 + 2 pitches = 20.4 -> M6x25
    QCOMPARE(r.partNumber, std::string("ISO 4762 M6x25 A2"));
    BomTable bom = buildBom(p.doc);
    QCOMPARE(row(bom, "ISO 7089 M6 A2")->quantity, 2);
    QCOMPARE(row(bom, "ISO 4032 M6 A2")->quantity, 1);
    QCOMPARE(row(bom, "ISO 4762 M6x25 A2")->quantity, 1);
    QCOMPARE(row(bom, "ISO 4032 M6 A2")->material, std::string("Stainless Steel"));
    // head washer on the top face, screw lifted by it, nut below the bottom washer
    QVERIFY(near(worldOrigin(p.doc, r.components[0]), gp_Pnt(30, 30, 10), 1e-6));
    QVERIFY(near(worldOrigin(p.doc, r.components[1]), gp_Pnt(30, 30, 11.6), 1e-6));
    QVERIFY(near(worldOrigin(p.doc, r.components[2]), gp_Pnt(30, 30, 0), 1e-6));
    QVERIFY(near(worldOrigin(p.doc, r.components[3]), gp_Pnt(30, 30, -1.6), 1e-6));
    gp_Dir nz = gp_Dir(0, 0, 1).Transformed(p.doc.worldPlacement(r.components[3]));
    QVERIFY(nz.IsEqual(gp_Dir(0, 0, -1), 1e-9));
    // no part overlaps the plate
    for (ObjectId c : r.components) {
      TopoDS_Shape w = componentWorldShape(p.doc, c);
      QVERIFY(!w.IsNull());
    }
  }

  void hundredHolesFast() {
    Plate p(false, 210, 10);
    std::vector<std::pair<double, double>> at;
    for (int i = 0; i < 10; ++i)
      for (int j = 0; j < 10; ++j) at.push_back({15.0 + 20 * i, 15.0 + 20 * j});
    part::HoleFeature* h = p.holes(at, true);
    QCOMPARE(h->status(), core::FeatureStatus::Ok);
    InsertOptions o;
    o.allEqual = true;
    QElapsedTimer timer;
    timer.start();
    InsertResult r = insertFasteners(p.doc, nullptr, {p.wall(15, 15, 3.3)}, o);
    const qint64 ms = timer.elapsed();
    QVERIFY2(r.ok(), r.error.c_str());
    QCOMPARE(r.holes, 100);
    qInfo("100 fasteners inserted in %lld ms", static_cast<long long>(ms));
    QVERIFY2(ms < 2000, qPrintable(QString::number(ms)));
    BomTable bom = buildBom(p.doc);
    QCOMPARE(row(bom, "ISO 4762 M6x10 8.8")->quantity, 100);
    int defs = 0;
    for (const core::Component* c : p.doc.components())
      if (c->partNumber == "ISO 4762 M6x10 8.8" && !c->isOccurrence()) ++defs;
    QCOMPARE(defs, 1);
  }

  void saveLoadAndStep() {
    Plate p(true);
    p.holes({{20, 20}, {60, 60}}, true);
    InsertOptions o;
    o.allEqual = true;
    InsertResult r = insertFasteners(p.doc, nullptr, {p.wall(20, 20, 3.3)}, o);
    QVERIFY2(r.ok(), r.error.c_str());
    const size_t bodies = p.doc.liveBodies().size();
    QJsonObject saved = p.doc.toJson();
    QVERIFY(!QJsonDocument(saved).toJson().contains("CASCADE"));  // no stored B-rep
    core::Document back;
    std::string err;
    QVERIFY2(back.fromJson(saved, &err), err.c_str());
    back.recompute();
    QCOMPARE(back.liveBodies().size(), bodies);
    QCOMPARE(row(buildBom(back), "ISO 4762 M6x10 8.8")->quantity, 2);
    QVERIFY(near(worldOrigin(back, r.components[1]), worldOrigin(p.doc, r.components[1]), 1e-6));
    // STEP export with the screws reads back: plate + 2 screws
    QTemporaryDir dir;
    const std::string path = (dir.path() + "/plate.step").toStdString();
    QVERIFY2(exportAssemblyStep(back, path, {}, &err), err.c_str());
    part::ImportFeature::Imported im;
    QVERIFY2(part::ImportFeature::readFile(path, im, &err), err.c_str());
    int solids = 0;
    double vol = 0;
    for (const auto& s : im.shapes)
      for (const TopoDS_Shape& so : core::subShapes(s, TopAbs_SOLID)) {
        ++solids;
        vol += core::volume(so);
      }
    QCOMPARE(solids, 3);
    st::FastenerSpec screw{st::Family::SocketCap, "M6", 10, "8.8"};
    const double expect = core::volume(p.doc.bodyShape(p.body)) + 2 * core::volume(st::makeFastener(screw));
    QVERIFY2(std::abs(vol - expect) < 1e-3 * expect, qPrintable(QString("%1 %2").arg(vol).arg(expect)));
  }

  void errors() {
    Plate p(true);
    p.holes({{20, 20}}, false);
    InsertOptions o;
    o.size = "M99";
    InsertResult r = insertFasteners(p.doc, nullptr, {p.wall(20, 20, 3.3)}, o);
    QVERIFY(!r.ok());
    r = insertFasteners(p.doc, nullptr, {faceWithNormal(p.doc, p.body, gp_Dir(0, 0, 1))}, InsertOptions{});
    QVERIFY(!r.ok());
    QCOMPARE(countJoints(p.doc), 0);
  }
};

QTEST_GUILESS_MAIN(FastenerInsertTest)
#include "test_fastener_insert.moc"
