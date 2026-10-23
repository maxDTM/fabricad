// Production features P2: Hole Table rows from a view (tags per callout, X / Y from the view's
// lower-left corner, sizes, quantities), persistence in the sheet JSON, rendering, the
// HOLETABLE tool, and the hole callout's fastener line / ANSI data from a Hole feature.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "drawing/tools/CalloutTools.h"
#include "drawing/CalloutRenderer.h"
#include "drawing/Callouts.h"
#include "drawing/DrawingModel.h"
#include "drawing/HoleTable.h"
#include "part/PartModule.h"
#include "part/features/Hole.h"
#include "support/HoleTestFeatures.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

struct Fixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  Sheet* sheet = nullptr;
  core::ObjectId viewId = core::kInvalidId;
  explicit Fixture(std::unique_ptr<core::Feature> part) {
    doc.addFeature(std::move(part));
    doc.recompute();
  }
  void makeView() {
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    sheet = &model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
    viewId = model->addBaseView(*sheet, {}, ViewOrientation::Top, 1.0, {150, 140}).id;
    model->updateGeometry();
  }
  const DrawingView& view() const { return *model->view(viewId); }
};
}  // namespace

class HoleTableTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    part::registerModule(ctx);
  }

  void rowsOfABlock() {
    Fixture fx(std::make_unique<test::HoleBlockFeature>());
    fx.makeView();
    auto rows = holeTableRows(fx.doc, fx.view(), fx.doc.units());
    QCOMPARE(rows.size(), size_t(3));  // the counterbore circle does not add a row
    QCOMPARE(rows[0].tag, std::string("A1"));
    QCOMPARE(rows[1].tag, std::string("B1"));
    QCOMPARE(rows[2].tag, std::string("C1"));
    QVERIFY(near(rows[0].x, 15) && near(rows[0].y, 20));
    QVERIFY(near(rows[0].diameter, 6.6));
    QVERIFY2(rows[0].callout.find("CBORE") != std::string::npos, rows[0].callout.c_str());
    QVERIFY(near(rows[1].x, 35) && near(rows[2].x, 50));
    QCOMPARE(rows[2].quantity, 1);
    const auto cells = holeTableCells(rows[0], fx.doc.units());
    QCOMPARE(cells.size(), size_t(6));
    QCOMPARE(cells[1], std::string("15"));
    QCOMPARE(cells[3], std::string("\xC3\x98" "6.6"));
  }

  void equalHolesShareALetter() {
    Fixture fx(std::make_unique<test::HolePlateFeature>(80, 50, std::vector<gp_Pnt>{{10, 10, 0}, {70, 10, 0}, {10, 40, 0}, {70, 40, 0}}));
    fx.makeView();
    auto rows = holeTableRows(fx.doc, fx.view(), fx.doc.units());
    QCOMPARE(rows.size(), size_t(4));
    for (const auto& r : rows) {
      QCOMPARE(r.tag.substr(0, 1), std::string("A"));
      QCOMPARE(r.quantity, 4);
    }
    QCOMPARE(rows[0].tag, std::string("A1"));
    QVERIFY(near(rows[0].y, 40));  // top row first
    // the table on the sheet: persistence, refresh, rendering
    HoleTable t;
    t.id = fx.model->allocateId();
    t.viewId = fx.viewId;
    t.position = {300, 30};
    fx.sheet->callouts.holeTables.push_back(t);
    refreshCallouts(fx.doc, *fx.sheet);
    QCOMPARE(fx.sheet->callouts.holeTables.front().rows.size(), size_t(4));
    QVERIFY(fx.sheet->callouts.contains(t.id));
    QCOMPARE(fx.sheet->callouts.viewOf(t.id), fx.viewId);
    QJsonObject o;
    fx.sheet->callouts.writeJson(o);
    CalloutSet back = CalloutSet::readJson(o);
    QCOMPARE(back.holeTables.size(), size_t(1));
    QCOMPARE(back.holeTables.front().viewId, fx.viewId);
    QVERIFY(near(back.holeTables.front().position.x, 300));
    DisplayList l;
    CalloutRenderer(fx.model->style(), fx.doc.units()).addHoleTable(l, fx.sheet->callouts.holeTables.front(), fx.doc.units());
    bool tag = false, header = false;
    for (const auto& x : l.texts) {
      if (x.text == "A4") tag = true;
      if (x.text == "Description") header = true;
    }
    QVERIFY(tag && header);
    QVERIFY(fx.sheet->callouts.move(t.id, {5, 0}));
    QVERIFY(fx.sheet->callouts.erase(t.id));
    QVERIFY(fx.sheet->callouts.holeTables.empty());
  }

  void holeFeatureCalloutAndTool() {
    core::Document* d = nullptr;
    Fixture fx(std::make_unique<test::HolePlateFeature>(60, 40, std::vector<gp_Pnt>{}));
    d = &fx.doc;
    const core::ObjectId body = d->liveBodies().front();
    // a counterbored M6 clearance hole sized for an SHCS, and an ANSI tapped hole
    auto top = [&](double x, double y) {
      const TopoDS_Shape& s = d->bodyShape(body);
      for (const TopoDS_Shape& f : core::subShapes(s, TopAbs_FACE)) {
        auto pl = core::facePlane(f);
        if (pl && pl->Direction().IsEqual(gp::DZ(), 1e-6) && near(pl->Location().Z(), 10)) {
          core::ShapeRef r = core::ShapeRef::subShape(body, s, f);
          r.point = core::Vec3{x, y, 10};
          r.hasPoint = true;
          return r;
        }
      }
      return core::ShapeRef{};
    };
    auto h = std::make_unique<part::HoleFeature>();
    h->prop("points").refs().push_back(top(15, 20));
    h->setName("Hole1");
    h->applyScriptOption("type", "clearance");
    h->applyScriptOption("size", "M6");
    h->applyScriptOption("fastener", "shcs");
    h->prop("holeType").setEnum("Counterbore");
    h->prop("extentType").setEnum("All");
    h->updatePropertyFlags();
    d->addFeature(std::move(h));
    auto t = std::make_unique<part::HoleFeature>();
    t->prop("points").refs().push_back(top(45, 20));
    t->setName("Hole2");
    t->applyScriptOption("type", "tapped");
    t->applyScriptOption("size", "1/4-20");
    t->prop("depth").setDouble(8);
    t->updatePropertyFlags();
    d->addFeature(std::move(t));
    d->recompute();
    fx.makeView();
    auto rows = holeTableRows(fx.doc, fx.view(), fx.doc.units());
    QCOMPARE(rows.size(), size_t(2));
    std::string all = rows[0].callout + " || " + rows[1].callout;
    QVERIFY2(all.find("FOR M6 SHCS") != std::string::npos, all.c_str());
    QVERIFY2(all.find("CBORE  \xC3\x98" "11 DEEP 6") != std::string::npos, all.c_str());
    QVERIFY2(all.find("1/4-20 - 2B") != std::string::npos, all.c_str());
    // the fastener line can be switched off on a callout
    for (const Curve2D& c : fx.view().curves) {
      if (!c.isCircle() || !near(c.radius, 3.3)) continue;
      HoleInfo info = analyzeHole(fx.doc, c);
      QCOMPARE(info.fastener, std::string("M6 SHCS"));
      QCOMPARE(info.standard, std::string("ISO"));
      QCOMPARE(info.fit, std::string("Normal"));
      HoleCallout off;
      off.fastenerLine = false;
      QCOMPARE(holeCalloutLines(info, off, fx.doc.units()).size(), holeCalloutLines(info, HoleCallout{}, fx.doc.units()).size() - 1);
      QCOMPARE(HoleCallout::fromJson(off.toJson()).fastenerLine, false);
    }
    // the HOLETABLE tool places a table right of the view
    core::ToolRegistry reg;
    registerCalloutTools(reg);
    QVERIFY(reg.has("drawing.hole-table"));
    QVERIFY(reg.byAlias("HOLETABLE"));
  }
};

QTEST_MAIN(HoleTableTest)
#include "test_drawing_hole_table.moc"
