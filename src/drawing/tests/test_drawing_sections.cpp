// Tests for break views (mapping, clipped curves, true dimension values, persistence) and for
// stepped / aligned section views (cut geometry, hatch regions, rendering of the cut line).
#include "core/Document.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/BreakView.h"
#include "drawing/DrawingModel.h"
#include "drawing/Projection.h"
#include "drawing/SectionCut.h"
#include "drawing/SheetRenderer.h"
#include "support/HoleTestFeatures.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

double loopsArea(const std::vector<std::vector<Point2>>& loops) {
  double a = 0;
  for (const auto& l : loops) a += std::abs(polygonArea(l));
  return a;
}

BBox2 loopsBox(const std::vector<std::vector<Point2>>& loops) {
  BBox2 b;
  for (const auto& l : loops)
    for (const auto& p : l) b.add(p);
  return b;
}

struct PlateFixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  Sheet* sheet = nullptr;
  core::ObjectId parentId = core::kInvalidId;

  PlateFixture(double w, double h, std::vector<gp_Pnt> holes) {
    doc.addFeature(std::make_unique<test::HolePlateFeature>(w, h, std::move(holes)));
    doc.recompute();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    sheet = &model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
    parentId = model->addBaseView(*sheet, {}, ViewOrientation::Top, 1.0, {120, 100}).id;
    model->updateGeometry();
  }
  const DrawingView& parent() const { return *model->view(parentId); }
  Point2 local(double x, double y) const { return worldToView(parent().frame, gp_Pnt(x, y, 5)); }
  /// Parent-local viewing direction for world direction (dx, dy).
  Point2 dir(double dx, double dy) const { return (local(dx, dy) - local(0, 0)).normalized(); }
  SectionLine line(const std::vector<Point2>& worldPts, Point2 arrowWorld, bool aligned) const {
    SectionLine L;
    for (const auto& p : worldPts) L.points.push_back(local(p.x, p.y));
    L.p1 = L.points[0];
    L.p2 = L.points[1];
    L.arrow = dir(arrowWorld.x, arrowWorld.y);
    L.aligned = aligned;
    return L;
  }
  SectionOutput cut(const SectionLine& L) {
    DrawingView probe;
    probe.type = ViewType::Section;
    probe.parent = parentId;
    probe.orientation = ViewOrientation::Custom;
    probe.section = L;
    probe.customFrame = sectionFrame(parent().frame, L.p1, L.p2, L.arrow);
    ViewJob job = model->makeJob(probe, true);
    return computeSection(job.section);
  }
};

struct LongBarFixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  Sheet* sheet = nullptr;
  core::ObjectId viewId = core::kInvalidId;
  LongBarFixture() {
    core::test::registerTestFeatures();
    auto box = std::make_unique<core::test::BoxFeature>();
    box->prop("dx").setDouble(200.0);
    doc.addFeature(std::move(box));
    doc.recompute();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    sheet = &model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
    viewId = model->addBaseView(*sheet, {}, ViewOrientation::Front, 1.0, {200, 120}).id;
    model->updateGeometry();
  }
  DrawingView& view() { return *model->view(viewId); }
};
}  // namespace

class SectionsTest : public QObject {
  Q_OBJECT
 private slots:
  void breakMapping() {
    std::vector<ViewBreak> b{{BreakOrientation::Horizontal, 10, 30}};
    QVERIFY(near(breakMap(b, BreakOrientation::Horizontal, 5, 5), 5));
    QVERIFY(near(breakMap(b, BreakOrientation::Horizontal, 40, 5), 25));
    QVERIFY(near(breakMap(b, BreakOrientation::Horizontal, 20, 5), 12.5));
    QVERIFY(near(breakMap(b, BreakOrientation::Vertical, 40, 5), 40));  // other axis untouched
    for (double v : {-7.0, 5.0, 10.0, 17.0, 30.0, 55.0})
      QVERIFY(near(breakUnmap(b, BreakOrientation::Horizontal, breakMap(b, BreakOrientation::Horizontal, v, 5), 5), v, 1e-9));
    QVERIFY(insideBreak(b, BreakOrientation::Horizontal, 20));
    QVERIFY(!insideBreak(b, BreakOrientation::Horizontal, 30));
    // overlapping / unsorted breaks are merged
    auto n = normalizedBreaks({{BreakOrientation::Horizontal, 50, 70}, {BreakOrientation::Horizontal, 30, 10},
                               {BreakOrientation::Horizontal, 25, 40}, {BreakOrientation::Vertical, 0, 1}});
    QCOMPARE(n.size(), size_t(3));
    QVERIFY(near(n[0].from, 10) && near(n[0].to, 40) && near(n[1].from, 50));
    QCOMPARE(n[2].orientation, BreakOrientation::Vertical);
    // two breaks accumulate
    std::vector<ViewBreak> two{{BreakOrientation::Horizontal, 0, 10}, {BreakOrientation::Horizontal, 20, 40}};
    QVERIFY(near(breakMap(two, BreakOrientation::Horizontal, 50, 2), 50 - 8 - 18));
    QVERIFY(near(breakUnmap(two, BreakOrientation::Horizontal, 24, 2), 50));
  }

  void brokenView() {
    LongBarFixture fx;
    DrawingView& v = fx.view();
    QVERIFY(v.geometryValid);
    QVERIFY(near(v.sheetBBox().width(), 200, 1e-3));
    // dimension of the overall length before the break
    const Curve2D* left = nullptr;
    const Curve2D* right = nullptr;
    for (const auto& c : v.curves)
      if (c.isLine() && std::abs(c.direction().x) < 1e-9) {
        if (!left || c.a.x < left->a.x) left = &c;
        if (!right || c.a.x > right->a.x) right = &c;
      }
    QVERIFY(left && right);
    Dimension d;
    d.viewId = v.id;
    d.type = DimensionType::Horizontal;
    d.refs = {DimRef::toCurve(*left), DimRef::toCurve(*right)};
    d.textPos = {200, 90};
    core::ObjectId dimId = fx.model->addDimension(*fx.sheet, d).id;

    fx.model->edit("Break", nullptr, [&]() {
      v.breaks = normalizedBreaks({{BreakOrientation::Horizontal, -60, 60}});
      v.breakGap = 6;
      fx.model->notifyChanged(fx.sheet->id);
    });
    DrawingView& bv = fx.view();
    QVERIFY(near(bv.sheetBBox().width(), 86, 1e-3));
    for (Point2 p : {Point2{-90, 5}, Point2{0, 3}, Point2{75, -12}})
      QVERIFY(near(bv.fromSheet(bv.toSheet(p)).x, p.x, 1e-9) && near(bv.fromSheet(bv.toSheet(p)).y, p.y, 1e-9));
    // the dimension still measures the model: 200, while its line is short on the sheet
    fx.model->refreshAnnotations(*fx.sheet);
    QVERIFY(near(fx.sheet->dimension(dimId)->value, 200, 1e-6));
    DimLayout L = layoutDimension(*fx.sheet->dimension(dimId), bv, fx.model->style(), fx.doc.units());
    QVERIFY(L.ok);
    QCOMPARE(L.text, std::string("200"));

    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList list;
    r.addView(list, *fx.sheet, bv);
    const double x0 = bv.toSheet({-60, 0}).x, x1 = x0 + 6;
    int breakLines = 0;
    for (const auto& c : list.curves) {
      if (c.cls == LineClass::Annotation) {
        ++breakLines;
        continue;
      }
      for (const Point2& p : c.sample(0.05)) QVERIFY2(!(p.x > x0 + 1e-6 && p.x < x1 - 1e-6), "geometry inside the break gap");
    }
    QCOMPARE(breakLines, 2);
    // horizontal edges are split in two pieces
    Curve2D top = Curve2D::segment({-100, 15}, {100, 15});
    auto pieces = brokenCurves(top, bv);
    QCOMPARE(pieces.size(), size_t(2));
    QVERIFY(near(pieces[0].b.x, x0) && near(pieces[1].a.x, x1));
    // curved style draws wavy lines
    bv.breakStyle = BreakStyle::Curved;
    DisplayList wavy;
    addBreakLines(wavy, bv);
    QCOMPARE(wavy.curves.size(), size_t(2));
    QVERIFY(wavy.curves[0].pts.size() > 20);
    // persistence
    DrawingView back = DrawingView::fromJson(bv.toJson());
    QCOMPARE(back.breaks.size(), size_t(1));
    QVERIFY(back.breaks[0] == bv.breaks[0]);
    QVERIFY(near(back.breakGap, 6) && back.breakStyle == BreakStyle::Curved);
    // breaks do not change the projected geometry key (no re-projection needed)
    QVERIFY(back.geometryKey() == DrawingView::fromJson(fx.view().toJson()).geometryKey());
  }

  void verticalBreak() {
    DrawingView v;
    v.position = {100, 100};
    v.scale = 2;
    v.bbox.add({-10, -50});
    v.bbox.add({10, 50});
    v.breaks = {{BreakOrientation::Vertical, -20, 20}};
    v.breakGap = 4;
    // local y 30 -> 30 - 40 + 2 = -8 -> sheet y = 100 + 16
    QVERIFY(near(v.toSheet({0, 30}).y, 116));
    QVERIFY(near(v.fromSheet({100, 116}).y, 30));
    QVERIFY(near(v.toSheet({0, -30}).y, 160));
    DisplayList l;
    addBreakLines(l, v);
    QCOMPARE(l.curves.size(), size_t(2));
    // lines are horizontal at the gap edges (sheet y of -20 and 4 mm above it)
    double y0 = v.toSheet({0, -20}).y;
    QVERIFY(near(l.curves[0].pts.front().y, y0) && near(l.curves[1].pts.front().y, y0 - 4));
  }

  void steppedSection() {
    PlateFixture fx(60, 20, {gp_Pnt(15, 5, 0), gp_Pnt(45, 15, 0)});
    QVERIFY(fx.parent().geometryValid);
    // straight cut through the first hole only
    SectionLine straight;
    straight.p1 = fx.local(-5, 5);
    straight.p2 = fx.local(65, 5);
    straight.arrow = fx.dir(0, 1);
    SectionOutput s = fx.cut(straight);
    QVERIFY(s.ok);
    QCOMPARE(s.loops.size(), size_t(2));
    QVERIFY(near(loopsArea(s.loops), 560, 0.5));
    // stepped cut through both holes
    SectionLine stepped = fx.line({{-5, 5}, {30, 5}, {30, 15}, {65, 15}}, {0, 1}, false);
    SectionOutput c = fx.cut(stepped);
    QVERIFY2(c.ok, c.error.c_str());
    QCOMPARE(c.loops.size(), size_t(4));
    QVERIFY(near(loopsArea(c.loops), 520, 0.5));
    QVERIFY(!c.hatch.empty());
    QVERIFY(near(loopsBox(c.loops).width(), 60, 1e-3));
    // full pipeline through the model
    core::ObjectId secId = core::kInvalidId;
    if (DrawingView* v = fx.model->addSectionView(*fx.sheet, fx.parentId, stepped, {120, 170})) secId = v->id;
    QVERIFY(secId != core::kInvalidId);
    fx.model->updateGeometry();
    const DrawingView* sec = fx.model->view(secId);
    QVERIFY2(sec->geometryValid, sec->error.c_str());
    int hatch = 0;
    for (const auto& curve : sec->curves)
      if (curve.cls == LineClass::Hatch) ++hatch;
    QVERIFY(hatch > 10);
    QVERIFY(near(sec->bbox.width(), 60, 0.01));
    // persistence of the path
    DrawingView back = DrawingView::fromJson(sec->toJson());
    QCOMPARE(back.section.points.size(), size_t(4));
    QVERIFY(back.section.isComplex() && !back.section.aligned);
    // the cut line on the parent: chain segments along the path, thick ends + bend marks
    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList deco;
    r.addViewDecorations(deco, *fx.sheet, fx.parent());
    int chain = 0, thick = 0;
    for (const auto& curve : deco.curves) {
      if (curve.cls == LineClass::Center) ++chain;
      if (curve.cls == LineClass::Visible) ++thick;
    }
    QCOMPARE(chain, 3);
    QCOMPARE(thick, 4);  // two ends + two bends
    QCOMPARE(deco.fills.size(), size_t(2));
  }

  void alignedSection() {
    PlateFixture fx(40, 40, {gp_Pnt(35, 20, 0), gp_Pnt(20, 35, 0)});
    SectionLine aligned = fx.line({{45, 20}, {20, 20}, {20, 45}}, {0, 1}, true);
    SectionOutput c = fx.cut(aligned);
    QVERIFY2(c.ok, c.error.c_str());
    QCOMPARE(c.loops.size(), size_t(4));
    QVERIFY(near(loopsArea(c.loops), 320, 0.5));
    // the revolved arm lies on the other side of the bend: together they span the plate width
    BBox2 b = loopsBox(c.loops);
    QVERIFY(near(b.width(), 40, 1e-3));
    QVERIFY(near(b.height(), 10, 1e-3));
    QCOMPARE(c.cutSources.size(), size_t(1));
    // through the model
    DrawingView* v = fx.model->addSectionView(*fx.sheet, fx.parentId, aligned, {120, 170});
    QVERIFY(v);
    core::ObjectId id = v->id;
    fx.model->updateGeometry();
    const DrawingView* sec = fx.model->view(id);
    QVERIFY2(sec->geometryValid, sec->error.c_str());
    QVERIFY(near(sec->bbox.width(), 40, 0.01));
    // the second arm's arrow is turned with the arm
    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList deco;
    r.addViewDecorations(deco, *fx.sheet, fx.parent());
    QCOMPARE(deco.fills.size(), size_t(2));
    Point2 d0 = (deco.fills[0].polygon[0] - (deco.fills[0].polygon[1] + deco.fills[0].polygon[2]) * 0.5).normalized();
    Point2 d1 = (deco.fills[1].polygon[0] - (deco.fills[1].polygon[1] + deco.fills[1].polygon[2]) * 0.5).normalized();
    QVERIFY(std::abs(d0.dot(d1)) < 1e-6);  // perpendicular arms -> perpendicular arrows
  }
};

QTEST_MAIN(SectionsTest)
#include "test_drawing_sections.moc"
