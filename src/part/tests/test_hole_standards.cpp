// Production features P2: hole standards (ISO metric / ANSI inch threads, clearance fits,
// tap drills, fastener head tables), Hole card behaviour (fastener-sized counterbores, Custom on
// edit, old files unchanged, script words), thread depth readout.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/Json.h"
#include "core/Registry.h"
#include "core/Tool.h"
#include "core/ShapeUtils.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "part/features/Hole.h"
#include "part/standards/Clearance.h"
#include "part/standards/Heads.h"
#include "part/standards/Threads.h"
#include "support/TestSketch.h"

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;
namespace st = fabricad::part::standards;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

core::ShapeRef topFace(const core::Document& doc, core::ObjectId body, double x, double y, double z) {
  const TopoDS_Shape& s = doc.bodyShape(body);
  for (const TopoDS_Shape& f : core::subShapes(s, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && pl->Direction().IsEqual(gp::DZ(), 1e-6) && std::abs(pl->Location().Z() - z) < 1e-6) {
      core::ShapeRef r = core::ShapeRef::subShape(body, s, f);
      r.point = core::Vec3{x, y, z};
      r.hasPoint = true;
      return r;
    }
  }
  return {};
}

struct Plate {
  core::Document doc;
  core::ObjectId body = core::kInvalidId;
  explicit Plate(double t = 12) {
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 40, 40);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(t);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = doc.addFeature(std::move(ex));
    doc.recompute();
    body = f->createdBodies().front();
  }
  HoleFeature* addHole(double t = 12) {
    auto h = std::make_unique<HoleFeature>();
    h->prop("points").addRef(topFace(doc, body, 20, 20, t));
    h->prop("depth").setDouble(t);
    return static_cast<HoleFeature*>(doc.addFeature(std::move(h)));
  }
};
}  // namespace

class HoleStandardsTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
    registerTestSketch();
  }

  void tableSpotChecks() {
    // ISO 273 / ASME B18.2.8 clearance
    QVERIFY(near(st::clearanceDiameter(*findIsoThread("M6"), st::Fit::Normal), 6.6));
    QVERIFY(near(st::clearanceDiameter(*findIsoThread("M6"), st::Fit::Close), 6.4));
    QVERIFY(near(st::clearanceDiameter(*findIsoThread("M6"), st::Fit::Loose), 7.0));
    const auto* q20 = st::findInchThread("1/4-20");
    QVERIFY(q20);
    QVERIFY(near(st::clearanceDiameter(*q20, st::Fit::Normal) / st::kInch, 0.281, 1e-9));
    QVERIFY(q20->isInch() && q20->coarse);
    QVERIFY(near(q20->nominal, 6.35));
    QVERIFY(near(q20->pitch, 25.4 / 20));
    // tap drills: M6x1 = 5.0, 1/4-20 = #7 = 0.201", #10-32 = #21 = 0.159"
    QVERIFY(near(findIsoThread("M6x1")->tapDrill(), 5.0));
    QVERIFY(near(findIsoThread("M8")->tapDrill(), 6.75));
    QVERIFY(near(q20->tapDrill() / st::kInch, 0.201, 1e-9));
    QVERIFY(near(st::findInchThread("#10-32")->tapDrill() / st::kInch, 0.159, 1e-9));
    QVERIFY(st::findInchThread("1/4")->designation == "1/4-20");
    QVERIFY(st::findInchThread("1/4-28 UNF")->designation == "1/4-28");
    QVERIFY(!st::findInchThread("1/4-19"));
    QVERIFY(findIsoThread("#10-24"));  // the shared lookup reaches the inch table
    QCOMPARE(st::sizeOf("M6x1"), std::string("M6"));
    QCOMPARE(st::sizeOf("1 1/4-7"), std::string("1 1/4"));
    QCOMPARE(st::inchText(0.25), std::string("1/4"));
    QCOMPARE(st::inchText(1.25), std::string("1 1/4"));
    QCOMPARE(st::defaultThreadClass(*q20, true), std::string("2B"));
    // heads: M6 SHCS counterbore 11 x 6 (+ clearance), M6 CSK 90 deg, 1/4 SHCS 0.438"
    auto rec = st::headRecess(st::HeadType::SocketCap, *findIsoThread("M6"), 0, 0);
    QVERIFY(rec && near(rec->diameter, 11) && near(rec->depth, 6) && !rec->countersink);
    rec = st::headRecess(st::HeadType::SocketCap, *findIsoThread("M6"), 0.5, 1.0);
    QVERIFY(rec && near(rec->depth, 7.5));
    rec = st::headRecess(st::HeadType::FlatCountersunk, *findIsoThread("M6"), 0, 0);
    QVERIFY(rec && rec->countersink && near(rec->angle, 90) && near(rec->diameter, 13.44));
    rec = st::headRecess(st::HeadType::FlatCountersunk, *q20, 0, 0);
    QVERIFY(rec && near(rec->angle, 82));
    rec = st::headRecess(st::HeadType::SocketCap, *q20, 0, 0);
    QVERIFY(rec && near(rec->diameter, 0.438 * 25.4) && near(rec->depth, 0.25 * 25.4));
    QVERIFY(!st::headRecess(st::HeadType::Button, *q20, 0, 0));  // no inch button head table
    QCOMPARE(st::drillPointPresets().size(), size_t(3));
    QCOMPARE(st::headTypeKeys().size(), st::headTypeNames().size());
  }

  // every table row gives a valid clearance, tap-drill and (where tabulated) counterbore hole
  void everyRowMakesAHole() {
    std::vector<const IsoThread*> all;
    for (const auto& t : isoMetricThreads())
      if (t.nominal >= 1.6 && t.nominal <= 36) all.push_back(&t);
    for (const auto& t : st::inchThreads()) all.push_back(&t);
    int cbores = 0;
    for (const IsoThread* t : all) {
      for (int fit = 0; fit < 3; ++fit) {
        HoleFeature::Params p;
        p.diameter = HoleFeature::clearanceDiameter(*t, fit);
        QVERIFY2(p.diameter > t->nominal, t->designation.c_str());
        p.depth = 4 * t->nominal + 5;
        std::string err;
        QVERIFY2(!HoleFeature::makeProtoHole(p, &err).IsNull(), (t->designation + ": " + err).c_str());
      }
      HoleFeature::Params p;
      p.diameter = t->tapDrill();
      QVERIFY2(p.diameter < t->nominal && p.diameter > t->minorDiameter() - 0.05 * t->nominal, t->designation.c_str());
      p.depth = 2 * t->nominal + 3;
      std::string err;
      QVERIFY2(!HoleFeature::makeProtoHole(p, &err).IsNull(), (t->designation + ": " + err).c_str());
      for (int h = 1; h < static_cast<int>(st::headTypeNames().size()); ++h) {
        auto rec = st::headRecess(static_cast<st::HeadType>(h), *t, 0, 0);
        if (!rec || !t->coarse) continue;
        HoleFeature::Params q;
        q.diameter = HoleFeature::clearanceDiameter(*t, 1);
        q.depth = rec->depth + 2 * t->nominal + 2;
        q.type = rec->countersink ? HoleFeature::Countersink : HoleFeature::Counterbore;
        q.cboreDiameter = q.csinkDiameter = rec->diameter;
        q.cboreDepth = rec->depth;
        q.csinkAngle = rec->angle > 0 ? rec->angle : 90;
        QVERIFY2(!HoleFeature::makeProtoHole(q, &err).IsNull(), (t->designation + " head " + std::to_string(h) + ": " + err).c_str());
        ++cbores;
      }
    }
    QVERIFY(cbores > 60);
  }

  // the Hole card fills the counterbore from the fastener table; a hand edit makes it Custom
  void counterboreFromFastener() {
    Plate pl;
    HoleFeature* h = pl.addHole();
    h->prop("holeType").setEnum("Counterbore");
    h->prop("tapType").setEnum("Clearance");
    h->prop("fastener").setEnum(static_cast<int>(st::HeadType::SocketCap));
    h->prop("extentType").setEnum("All");
    h->updatePropertyFlags();
    QVERIFY(near(h->prop("cboreDiameter").toDouble(), 11));
    QVERIFY(near(h->prop("cboreDepth").toDouble(), 6));
    QVERIFY(!h->prop("headClearance").flags().hidden);
    pl.doc.recompute();
    QCOMPARE(h->status(), core::FeatureStatus::Ok);
    const double expect = 40 * 40 * 12 - M_PI * 3.3 * 3.3 * 12 - M_PI * (5.5 * 5.5 - 3.3 * 3.3) * 6;
    QVERIFY2(near(core::volume(pl.doc.bodyShape(pl.body)), expect, 1e-3), qPrintable(QString::number(core::volume(pl.doc.bodyShape(pl.body)))));
    // head clearance deepens the counterbore
    h->prop("headClearance").setDouble(0.5);
    h->updatePropertyFlags();
    QVERIFY(near(h->prop("cboreDepth").toDouble(), 6.5));
    // a hand edit switches to Custom and keeps the typed value
    h->prop("cboreDiameter").setDouble(12);
    h->updatePropertyFlags();
    QCOMPARE(h->enumIndex("fastener"), 0);
    QVERIFY(near(h->prop("cboreDiameter").toDouble(), 12));
    QVERIFY(h->prop("headClearance").flags().hidden);
  }

  // a Hole saved before P2 (no standard/fastener properties) loads as Custom with its typed sizes
  void oldFileKeepsGeometry() {
    Plate pl;
    HoleFeature* h = pl.addHole();
    h->prop("holeType").setEnum("Counterbore");
    h->prop("diameter").setDouble(6.6);
    h->prop("cboreDiameter").setDouble(10.5);
    h->prop("cboreDepth").setDouble(5.2);
    pl.doc.recompute();
    const double v0 = core::volume(pl.doc.bodyShape(pl.body));
    QJsonObject o = h->toJson();
    QJsonObject props = o["properties"].toObject();
    for (const char* k : {"standard", "ansiSize", "fastener", "headClearance", "headOffset", "threadClass", "threadInfo"}) props.remove(k);
    QJsonObject tap = props["tapType"].toObject();
    tap["name"] = "Simple";
    props["tapType"] = tap;
    o["properties"] = props;
    HoleFeature loaded;
    loaded.fromJson(o);
    QCOMPARE(loaded.enumIndex("fastener"), 0);
    QCOMPARE(loaded.enumIndex("standard"), 0);
    QVERIFY(near(loaded.prop("cboreDiameter").toDouble(), 10.5));
    QVERIFY(near(loaded.prop("cboreDepth").toDouble(), 5.2));
    // recompute the loaded definition in place of the original: same volume
    h->fromJson(o);
    pl.doc.notifyFeatureChanged(h->id());
    pl.doc.recompute();
    QVERIFY(near(core::volume(pl.doc.bodyShape(pl.body)), v0, 1e-9));
  }

  void ansiInchHoleRoundTrip() {
    Plate pl(12.7);
    HoleFeature* h = pl.addHole(12.7);
    QVERIFY(h->applyScriptOption("standard", "ansi"));
    QVERIFY(h->applyScriptOption("type", "clearance"));
    QVERIFY(h->applyScriptOption("size", "1/4-20"));
    QVERIFY(h->applyScriptOption("fit", "normal"));
    QVERIFY(!h->applyScriptOption("fit", "snug"));
    QVERIFY(h->applyScriptOption("fastener", "shcs"));
    QVERIFY(h->applyScriptOption("point", "135"));
    QVERIFY(!h->applyScriptOption("size", "M99"));
    QCOMPARE(h->enumIndex("drillPoint"), int(HoleFeature::Angled));
    QVERIFY(near(h->prop("drillAngle").toDouble(), 135));
    h->prop("drillPoint").setEnum(int(HoleFeature::Flat));
    QVERIFY(near(h->effectiveDiameter(), 0.281 * 25.4, 1e-9));
    h->prop("holeType").setEnum("Counterbore");
    h->updatePropertyFlags();
    QVERIFY(!h->prop("ansiSize").flags().hidden);
    QVERIFY(h->prop("threadSize").flags().hidden);
    QVERIFY(near(h->prop("cboreDiameter").toDouble(), 0.438 * 25.4));
    pl.doc.recompute();
    QCOMPARE(h->status(), core::FeatureStatus::Ok);
    const double v = core::volume(pl.doc.bodyShape(pl.body));
    // save / load the document: inch hole comes back identical
    QJsonObject saved = pl.doc.toJson();
    core::Document back;
    std::string err;
    QVERIFY2(back.fromJson(saved, &err), err.c_str());
    back.recompute();
    const auto bodies = back.liveBodies();
    QCOMPARE(bodies.size(), size_t(1));
    QVERIFY(near(core::volume(back.bodyShape(bodies.front())), v, 1e-6));
    const HoleFeature* bh = nullptr;
    for (int i = 0; i < back.featureCount(); ++i)
      if (auto* x = dynamic_cast<const HoleFeature*>(back.featureAt(i))) bh = x;
    QVERIFY(bh);
    QCOMPARE(bh->enumName("ansiSize"), std::string("1/4-20"));
    QCOMPARE(bh->enumIndex("standard"), int(HoleFeature::Ansi));
    QCOMPARE(bh->enumIndex("fastener"), int(st::HeadType::SocketCap));
  }

  void tappedHoleThreadAndReadout() {
    Plate pl(20);
    HoleFeature* h = pl.addHole(20);
    h->prop("depth").setDouble(15);
    QVERIFY(h->applyScriptOption("type", "tapped"));
    QVERIFY(h->applyScriptOption("size", "M6x1"));
    QVERIFY(h->applyScriptOption("point", "118"));
    h->prop("threadFullDepth").setBool(false);
    h->prop("threadDepth").setDouble(12);
    h->updatePropertyFlags();
    const std::string info = h->prop("threadInfo").toString();
    QVERIFY2(info.find("thread 12 mm, drill 15 mm + 1.502 mm point") != std::string::npos, info.c_str());
    QVERIFY(!h->prop("threadInfo").flags().hidden);
    pl.doc.recompute();
    QCOMPARE(h->status(), core::FeatureStatus::Ok);
    QCOMPARE(h->threads().size(), size_t(1));
    QCOMPARE(h->threads().front().callout(), std::string("M6x1 - 6H"));
    QVERIFY(near(h->threads().front().length, 12, 1e-6));
    // tap drill only: the 5 mm drill, no thread
    QVERIFY(h->applyScriptOption("type", "tapdrill"));
    pl.doc.notifyFeatureChanged(h->id());
    pl.doc.recompute();
    QCOMPARE(h->status(), core::FeatureStatus::Ok);
    QVERIFY(h->threads().empty());
    QVERIFY(near(h->effectiveDiameter(), 5.0));
    // inch tapped: 1/4-20 class 2B
    QVERIFY(h->applyScriptOption("type", "tapped"));
    QVERIFY(h->applyScriptOption("size", "1/4-20"));
    pl.doc.notifyFeatureChanged(h->id());
    pl.doc.recompute();
    QCOMPARE(h->status(), core::FeatureStatus::Ok);
    QCOMPARE(h->threads().size(), size_t(1));
    QCOMPARE(h->threads().front().callout(), std::string("1/4-20 - 2B"));
    h->updatePropertyFlags();
    QVERIFY2(h->prop("threadInfo").toString().find("0.472\"") != std::string::npos, h->prop("threadInfo").toString().c_str());
  }

  void newHoleDefaults() {
    core::Document doc;
    HoleFeature h;
    h.applyDefaultsForNewFeature(doc);
    QCOMPARE(h.enumIndex("fastener"), core::isBatchMode() ? 0 : int(st::HeadType::SocketCap));
    QCOMPARE(h.enumIndex("standard"), int(HoleFeature::Iso));
    core::UnitSystem u = doc.units();
    u.length = core::LengthUnit::Inch;
    doc.setUnits(u);
    HoleFeature hi;
    hi.applyDefaultsForNewFeature(doc);
    QCOMPARE(hi.enumIndex("standard"), int(HoleFeature::Ansi));
  }
};

QTEST_GUILESS_MAIN(HoleStandardsTest)
#include "test_hole_standards.moc"
