// Production features P2: generated standard parts. Each family x 3 sizes gives one closed,
// valid solid whose volume is within 5 % of the closed form from the catalogue dimensions;
// heads and sockets are present; BOM data; StandardPart regenerates identically after save/load.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/PartModule.h"
#include "part/features/StandardPart.h"
#include "part/standards/Fasteners.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp_Explorer.hxx>

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
namespace st = fabricad::part::standards;

namespace {
int count(const TopoDS_Shape& s, TopAbs_ShapeEnum t) {
  int n = 0;
  for (TopExp_Explorer ex(s, t); ex.More(); ex.Next()) ++n;
  return n;
}
bool inside(const TopoDS_Shape& s, const gp_Pnt& p) {
  BRepClass3d_SolidClassifier c(s, p, 1e-7);
  return c.State() == TopAbs_IN;
}
}  // namespace

class FastenersTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
  }

  void everyFamilyThreeSizes() {
    const std::map<st::Family, std::vector<std::string>> sizes{
        {st::Family::SocketCap, {"M3", "M6", "1/4"}},     {st::Family::HexBolt, {"M5", "M10", "3/8"}},
        {st::Family::HexNut, {"M4", "M8", "1/2"}},        {st::Family::NylocNut, {"M3", "M6", "5/16"}},
        {st::Family::Washer, {"M3", "M8", "1/4"}},        {st::Family::SmallWasher, {"M4", "M6", "M10"}},
        {st::Family::HeatSetInsert, {"M2", "M3", "M5"}}, {st::Family::DowelPin, {"3", "6", "1/4"}},
        {st::Family::SetScrew, {"M3", "M6", "M10"}},
    };
    QCOMPARE(sizes.size(), size_t(st::kFamilyCount));
    for (const auto& [fam, list] : sizes) {
      for (const std::string& size : list) {
        st::FastenerSpec s;
        s.family = fam;
        s.size = size;
        const auto ls = st::familyLengths(fam, size);
        QCOMPARE(ls.empty(), !st::familyHasLength(fam));
        if (!ls.empty()) s.length = ls[ls.size() / 2];
        const std::string what = st::partNumber(s);
        std::string err;
        TopoDS_Shape shape = st::makeFastener(s, &err);
        QVERIFY2(!shape.IsNull(), (what + ": " + err).c_str());
        QVERIFY2(BRepCheck_Analyzer(shape).IsValid(), what.c_str());
        QVERIFY2(count(shape, TopAbs_SOLID) == 1, what.c_str());
        QVERIFY2(count(shape, TopAbs_SHELL) == 1, what.c_str());  // closed, one shell
        const double v = core::volume(shape), ref = st::nominalVolume(s);
        QVERIFY2(ref > 0 && std::abs(v - ref) <= 0.05 * ref, qPrintable(QString("%1: %2 vs %3").arg(what.c_str()).arg(v).arg(ref)));
        auto d = st::fastenerDims(s);
        QVERIFY(d);
        if (fam == st::Family::SocketCap) {
          // head present above the seat, socket recess in it, shank below
          QVERIFY(inside(shape, gp_Pnt(d->headDia / 2 - 0.1, 0, d->headHeight / 2)));
          QVERIFY(!inside(shape, gp_Pnt(0, 0, d->headHeight - 0.1)));
          QVERIFY(inside(shape, gp_Pnt(0, 0, -s.length / 2)));
          QVERIFY(st::threadLength(s) > 0 && st::threadLength(s) <= s.length);
        }
        if (fam == st::Family::HexNut || fam == st::Family::Washer) QVERIFY(!inside(shape, gp_Pnt(0, 0, d->headHeight / 2)));
      }
    }
  }

  void bomDataAndLookups() {
    st::FastenerSpec s{st::Family::SocketCap, "M6", 20, "8.8"};
    QCOMPARE(st::partNumber(s), std::string("ISO 4762 M6x20 8.8"));
    QCOMPARE(st::familyDescription(s.family), std::string("Socket head cap screw"));
    st::FastenerSpec in{st::Family::SocketCap, "1/4", 25.4, "8.8"};
    QCOMPARE(st::partNumber(in), std::string("ASME B18.3 1/4-20 x 1 ASTM A574"));
    QCOMPARE(st::partNumber(st::FastenerSpec{st::Family::HexNut, "M6", 0, "A2"}), std::string("ISO 4032 M6 A2"));
    QCOMPARE(st::sizeForHoleDiameter(6.6, false), std::string("M6"));
    QCOMPARE(st::sizeForHoleDiameter(6.4, false), std::string("M6"));
    QCOMPARE(st::sizeForHoleDiameter(5.0, false), std::string("M6"));  // tap drill
    QCOMPARE(st::sizeForHoleDiameter(0.281 * 25.4, true), std::string("1/4"));
    QVERIFY(st::sizeForHoleDiameter(6.6, true).empty() || st::sizeForHoleDiameter(6.6, true) != "M6");
    QCOMPARE(st::nextStandardLength(st::Family::SocketCap, "M6", 17.2), 20.0);
    QVERIFY(st::findGrade("a2") && std::string(st::findGrade("a2")->materialName) == "Stainless Steel");
    QVERIFY(*st::familyFromKey("shcs") == st::Family::SocketCap);
    QVERIFY(!st::familyFromKey("rivet"));
    std::string err;
    QVERIFY(st::makeFastener(st::FastenerSpec{st::Family::SocketCap, "M7", 20, "8.8"}, &err).IsNull());
    QVERIFY(!err.empty());
  }

  void standardPartRegenerates() {
    core::Document doc;
    auto f = std::make_unique<StandardPartFeature>();
    f->setSpec(st::FastenerSpec{st::Family::HexBolt, "M8", 40, "10.9"});
    doc.addFeature(std::move(f));
    doc.recompute();
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    const double v = core::volume(doc.bodyShape(doc.liveBodies().front()));
    QVERIFY(v > 0);
    QJsonObject saved = doc.toJson();
    // no B-rep stored
    QVERIFY(!QJsonDocument(saved).toJson().contains("CASCADE"));
    core::Document back;
    std::string err;
    QVERIFY2(back.fromJson(saved, &err), err.c_str());
    back.recompute();
    QCOMPARE(back.liveBodies().size(), size_t(1));
    QCOMPARE(core::volume(back.bodyShape(back.liveBodies().front())), v);
  }
};

QTEST_GUILESS_MAIN(FastenersTest)
#include "test_fasteners.moc"
