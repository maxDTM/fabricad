// Sketch model tests: geometry maths, trim/extend/offset/mirror/fillet, queries, JSON.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"
#include "support/SketchTestHelpers.h"

#include <QtTest/QtTest>

using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

class SketchOpsTest : public QObject {
  Q_OBJECT
 private slots:
  void geometryBasics() {
    Geometry arc = Geometry::makeArc({0, 0}, 10, 0, kPi / 2);
    QVERIFY(near(arc.start(), {10, 0}));
    QVERIFY(near(arc.end(), {0, 10}));
    QVERIFY(near(arc.midpoint(), Pt2::polar(10, kPi / 4)));
    QVERIFY(arc.containsAngle(kPi / 4));
    QVERIFY(!arc.containsAngle(kPi));
    QVERIFY(near(arc.length(), 10 * kPi / 2));
    auto a3 = Geometry::makeArc3Points({10, 0}, {0, 10}, {-10, 0});
    QVERIFY(a3.has_value());
    QVERIFY(near(a3->center(), {0, 0}));
    QVERIFY(near(a3->sweep(), kPi));
    auto a3b = Geometry::makeArc3Points({-10, 0}, {0, 10}, {10, 0});  // reversed order -> same arc
    QVERIFY(near(a3b->midpoint(), {0, 10}));
    Geometry line = Geometry::makeLine({0, 0}, {10, 0});
    QVERIFY(near(line.closestPoint({5, 3}), {5, 0}));
    QVERIFY(near(line.closestPoint({15, 3}), {10, 0}));
    QVERIFY(near(line.distanceTo({5, 3}), 3.0));
    Geometry circle = Geometry::makeCircle({0, 0}, 5);
    QVERIFY(near(circle.closestPoint({10, 0}), {5, 0}));
    // mirror an arc about the y axis
    Geometry m = arc;
    m.mirror({0, 0}, {0, 1});
    QVERIFY(near(m.start(), {0, 10}));
    QVERIFY(near(m.end(), {-10, 0}));
  }

  void intersections() {
    Geometry l1 = Geometry::makeLine({0, 0}, {10, 10});
    Geometry l2 = Geometry::makeLine({0, 10}, {10, 0});
    auto x = intersectCurves(l1, l2);
    QCOMPARE(x.size(), size_t(1));
    QVERIFY(near(x[0], {5, 5}));
    Geometry l3 = Geometry::makeLine({20, 0}, {30, 0});
    QVERIFY(intersectCurves(l1, l3).empty());
    Geometry c = Geometry::makeCircle({5, 5}, 5);
    x = intersectCurves(l1, c);
    QCOMPARE(x.size(), size_t(2));
    Geometry arc = Geometry::makeArc({5, 5}, 5, 0, kPi / 2);
    x = intersectCurves(l1, arc);
    QCOMPARE(x.size(), size_t(1));
    Geometry c2 = Geometry::makeCircle({12, 5}, 5);
    x = intersectCurves(c, c2);
    QCOMPARE(x.size(), size_t(2));
  }

  void hitTestingAndChain() {
    Sketch sk;
    auto ids = addRectangle(sk, {0, 0}, {10, 10});
    auto hp = sk.hitPoint({0.2, 0.1}, 0.5);
    QVERIFY(hp.has_value());
    QVERIFY(near(hp->point, {0, 0}));
    auto hm = sk.hitPoint({5.1, 0}, 0.5);
    QVERIFY(hm.has_value());
    QCOMPARE(hm->ref.pos, PointPos::Mid);
    auto hc = sk.hitCurve({5, 0.3}, 0.5);
    QVERIFY(hc.has_value());
    QCOMPARE(hc->ref.geo, ids[0]);
    QVERIFY(!sk.hit({5, 5}, 0.5).has_value());
    QCOMPARE(sk.chain(ids[0]).size(), size_t(4));
    QCOMPARE(sk.curvesInRect({-1, -1}, {11, 11}, false).size(), size_t(4));
    QCOMPARE(sk.curvesInRect({-1, -1}, {5, 11}, false).size(), size_t(1));
    QCOMPARE(sk.curvesInRect({-1, -1}, {5, 11}, true).size(), size_t(3));
  }

  void trimLineMiddle() {
    Sketch sk;
    int h = sk.addGeometry(Geometry::makeLine({0, 0}, {30, 0}));
    int v1 = sk.addGeometry(Geometry::makeLine({10, -5}, {10, 5}));
    int v2 = sk.addGeometry(Geometry::makeLine({20, -5}, {20, 5}));
    auto pieces = sk.trim(h, {15, 0});
    QCOMPARE(pieces.size(), size_t(2));
    QVERIFY(near(sk.geometry(pieces[0])->end(), {10, 0}));
    QVERIFY(near(sk.geometry(pieces[1])->start(), {20, 0}));
    QVERIFY(near(sk.geometry(pieces[1])->end(), {30, 0}));
    QVERIFY(sk.solve().ok());
    // a piece ending exactly at the trimming curve with nothing else crossing is removed entirely
    auto gone = sk.trim(pieces[1], {25, 0});
    QVERIFY(gone.empty());
    QVERIFY(!sk.geometry(pieces[1]));
    // trim an end piece: start part removed up to the new vertical at x=5
    sk.addGeometry(Geometry::makeLine({5, -5}, {5, 5}));
    auto p2 = sk.trim(pieces[0], {2, 0});
    QCOMPARE(p2.size(), size_t(1));
    QVERIFY(near(sk.geometry(p2[0])->start(), {5, 0}));
    QVERIFY(near(sk.geometry(p2[0])->end(), {10, 0}));
    (void)v1;
    (void)v2;
    // trim with no intersections deletes
    int lone = sk.addGeometry(Geometry::makeLine({100, 100}, {110, 100}));
    QVERIFY(sk.trim(lone, {105, 100}).empty());
    QVERIFY(!sk.geometry(lone));
  }

  void trimCircleToArc() {
    Sketch sk;
    int c = sk.addGeometry(Geometry::makeCircle({0, 0}, 10));
    sk.addGeometry(Geometry::makeLine({-20, 0}, {20, 0}));
    auto pieces = sk.trim(c, {0, -10});
    QCOMPARE(pieces.size(), size_t(1));
    const Geometry* g = sk.geometry(c);
    QCOMPARE(g->type, GeomType::Arc);
    QVERIFY(near(g->start(), {10, 0}));
    QVERIFY(near(g->end(), {-10, 0}));
    QVERIFY(near(g->midpoint(), {0, 10}));
    QVERIFY(sk.solve().ok());
  }

  void trimArc() {
    Sketch sk;
    int a = sk.addGeometry(Geometry::makeArc({0, 0}, 10, 0, kPi));
    sk.addGeometry(Geometry::makeLine({0, -20}, {0, 20}));
    auto pieces = sk.trim(a, {-7, 7});
    QCOMPARE(pieces.size(), size_t(1));
    QVERIFY(near(sk.geometry(a)->end(), {0, 10}));
    QVERIFY(near(sk.geometry(a)->start(), {10, 0}));
  }

  void extendLineAndArc() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {5, 0}));
    sk.addGeometry(Geometry::makeLine({10, -5}, {10, 5}));
    QVERIFY(sk.extend(l, {5, 0}));
    QVERIFY(near(sk.geometry(l)->end(), {10, 0}));
    QVERIFY(!sk.extend(l, {0, 0}));  // nothing behind the start
    int a = sk.addGeometry(Geometry::makeArc({0, 20}, 5, 0, kPi / 4));
    sk.addGeometry(Geometry::makeLine({0, 20}, {0, 40}));
    QVERIFY(sk.extend(a, sk.geometry(a)->end()));
    QVERIFY(near(sk.geometry(a)->end(), {0, 25}));
  }

  void offsetRectangleAndCircle() {
    Sketch sk;
    auto ids = addRectangle(sk, {0, 0}, {10, 10}, false);
    auto out = sk.offset(ids, 2.0, Pt2{5, 5});  // inward
    QCOMPARE(out.size(), size_t(4));
    Pt2 mn, mx;
    Sketch tmp;
    for (int id : out) tmp.addGeometry(*sk.geometry(id));
    QVERIFY(tmp.bbox(mn, mx));
    QVERIFY(near(mn, {2, 2}));
    QVERIFY(near(mx, {8, 8}));
    for (int id : out) QVERIFY(near(sk.geometry(id)->length(), 6.0));
    auto out2 = sk.offset(ids, 3.0, Pt2{-5, -5});  // outward
    Sketch tmp2;
    for (int id : out2) tmp2.addGeometry(*sk.geometry(id));
    QVERIFY(tmp2.bbox(mn, mx));
    QVERIFY(near(mn, {-3, -3}));
    QVERIFY(near(mx, {13, 13}));
    int c = sk.addGeometry(Geometry::makeCircle({50, 50}, 5));
    auto oc = sk.offset({c}, 2.0, Pt2{50, 50});
    QCOMPARE(oc.size(), size_t(1));
    QVERIFY(near(sk.geometry(oc[0])->radius(), 3.0));
    QVERIFY(sk.solve().ok());
  }

  void offsetLineArcChain() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int a = sk.addGeometry(Geometry::makeArc({10, 5}, 5, -kPi / 2, 0));  // from (10,0) to (15,5)
    auto out = sk.offset({l, a}, 1.0, Pt2{10, 10});  // towards the arc centre side
    QCOMPARE(out.size(), size_t(2));
    const Geometry* ol = sk.geometry(out[0]);
    const Geometry* oa = sk.geometry(out[1]);
    QVERIFY(near(ol->start().y, 1.0));
    QVERIFY(near(oa->radius(), 4.0));
    QVERIFY(near(ol->end(), oa->start(), 1e-6));
  }

  /// UX-24: offset of a slot (lines + arcs) goes to the clicked side, inward and outward,
  /// whatever curve the chain ordering puts first.
  static std::vector<int> addSlot(Sketch& sk) {
    // centres (0,0) and (20,0), radius 5; same geometry as the Slot tool builds
    std::vector<int> ids;
    ids.push_back(sk.addGeometry(Geometry::makeLine({0, 5}, {20, 5})));
    ids.push_back(sk.addGeometry(Geometry::makeLine({20, -5}, {0, -5})));
    ids.push_back(sk.addGeometry(Geometry::makeArc({20, 0}, 5, -kPi / 2, kPi / 2)));
    ids.push_back(sk.addGeometry(Geometry::makeArc({0, 0}, 5, kPi / 2, 3 * kPi / 2)));
    return ids;
  }
  static void bboxOf(const Sketch& sk, const std::vector<int>& ids, Pt2& mn, Pt2& mx) {
    Sketch tmp;
    for (int id : ids) tmp.addGeometry(*sk.geometry(id));
    QVERIFY(tmp.bbox(mn, mx));
  }

  void offsetSlotOutwardAndInward() {
    for (Pt2 side : {Pt2{10, 12}, Pt2{10, -12}, Pt2{-9, 0}, Pt2{29, 0}}) {
      Sketch sk;
      auto ids = addSlot(sk);
      auto out = sk.offset(ids, 3.0, side);  // outward
      QCOMPARE(out.size(), size_t(4));
      Pt2 mn, mx;
      bboxOf(sk, out, mn, mx);
      QVERIFY2(near(mn, {-8, -8}) && near(mx, {28, 8}), "outward slot offset");
      for (int id : out)
        if (sk.geometry(id)->isCircular()) QVERIFY(near(sk.geometry(id)->radius(), 8.0));
      QVERIFY(sk.solve().ok());
    }
    for (Pt2 side : {Pt2{10, 3}, Pt2{10, -3}, Pt2{-3, 0}}) {
      Sketch sk;
      auto ids = addSlot(sk);
      auto out = sk.offset(ids, 2.0, side);  // inward
      QCOMPARE(out.size(), size_t(4));
      Pt2 mn, mx;
      bboxOf(sk, out, mn, mx);
      QVERIFY2(near(mn, {-3, -3}) && near(mx, {23, 3}), "inward slot offset");
    }
    // typed distance without a cursor side: positive = outward for a closed loop
    {
      Sketch sk;
      auto out = sk.offset(addSlot(sk), 1.0);
      Pt2 mn, mx;
      bboxOf(sk, out, mn, mx);
      QVERIFY(near(mn, {-6, -6}) && near(mx, {26, 6}));
    }
    // an inward offset larger than the radius degenerates: no geometry instead of 1e-6 arcs
    {
      Sketch sk;
      auto out = sk.offset(addSlot(sk), 6.0, Pt2{10, 1});
      QVERIFY(out.empty());
    }
  }

  void offsetRectangleSideByCursor() {
    // the cursor just outside each edge offsets outward, also near the "first" curve's extension
    for (Pt2 side : {Pt2{5, 11}, Pt2{11, 5}, Pt2{-1, 5}, Pt2{5, -1}, Pt2{20, 5}}) {
      Sketch sk;
      auto ids = addRectangle(sk, {0, 0}, {10, 10}, false);
      auto out = sk.offset(ids, 1.0, side);
      Pt2 mn, mx;
      bboxOf(sk, out, mn, mx);
      QVERIFY(near(mn, {-1, -1}) && near(mx, {11, 11}));
    }
    Sketch sk;
    auto ids = addRectangle(sk, {0, 0}, {10, 10}, false);
    QVERIFY(sk.offset(ids, 6.0, Pt2{5, 5}).empty());  // collapses: lines would flip
  }

  void offsetOpenLChainByNearestCurve() {
    // L: (0,10) -> (0,0) -> (10,0); the cursor side is taken from the curve nearest to it
    Sketch sk;
    int a = sk.addGeometry(Geometry::makeLine({0, 10}, {0, 0}));
    int b = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    auto out = sk.offset({a, b}, 1.0, Pt2{8, -2});  // below the horizontal leg -> outside of the L
    QCOMPARE(out.size(), size_t(2));
    Pt2 mn, mx;
    bboxOf(sk, out, mn, mx);
    QVERIFY(near(mn, {-1, -1}));
    Sketch sk2;
    a = sk2.addGeometry(Geometry::makeLine({0, 10}, {0, 0}));
    b = sk2.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    out = sk2.offset({a, b}, 1.0, Pt2{-2, 8});  // left of the vertical leg -> also outside
    bboxOf(sk2, out, mn, mx);
    QVERIFY(near(mn, {-1, -1}));
    Sketch sk3;
    a = sk3.addGeometry(Geometry::makeLine({0, 10}, {0, 0}));
    b = sk3.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    out = sk3.offset({a, b}, 1.0, Pt2{8, 2});  // inside the corner
    bboxOf(sk3, out, mn, mx);
    QVERIFY(near(mn, {1, 1}));
  }

  void mirrorWithConstraints() {
    Sketch sk;
    int axis = sk.addGeometry(Geometry::makeLine({0, -10}, {0, 10}));
    sk.geometry(axis)->construction = true;
    int l = sk.addGeometry(Geometry::makeLine({2, 0}, {6, 3}));
    int c = sk.addGeometry(Geometry::makeCircle({5, 5}, 1));
    auto out = sk.mirror({l, c}, axis, true);
    QCOMPARE(out.size(), size_t(2));
    QVERIFY(near(sk.geometry(out[0])->start(), {-2, 0}));
    QVERIFY(near(sk.geometry(out[0])->end(), {-6, 3}));
    QVERIFY(near(sk.geometry(out[1])->center(), {-5, 5}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {axis}});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l}});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(r.isFullyConstrained(out[0]));
    // move the original: the mirror follows through the symmetric constraints
    sk.geometry(l)->setEnd({6, 8});
    sk.removeConstraint(sk.constraints().back().id);  // drop fix on l
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l}});
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(out[0])->end(), {-6, 8}, 1e-6));
  }

  void filletTwoLines() {
    Sketch sk;
    int l1 = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int l2 = sk.addGeometry(Geometry::makeLine({10, 0}, {10, 10}));
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l1, PointPos::End}, .b = {l2, PointPos::Start}});
    auto arcId = sk.fillet(l1, {5, 0}, l2, {10, 5}, 2.0);
    QVERIFY(arcId.has_value());
    const Geometry* a = sk.geometry(*arcId);
    QVERIFY(near(a->center(), {8, 2}));
    QVERIFY(near(a->radius(), 2.0));
    QVERIFY(near(sk.geometry(l1)->end(), {8, 0}));
    QVERIFY(near(sk.geometry(l2)->start(), {10, 2}));
    QVERIFY(a->sweep() < kPi + 1e-9);
    // tangency constraints exist and the sketch solves
    int tangents = 0;
    for (const auto& c : sk.constraints())
      if (c.type == ConstraintType::Tangent) ++tangents;
    QCOMPARE(tangents, 2);
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l1, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l1}});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l2, PointPos::End}});
    sk.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l2}});
    sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {*arcId}, .value = 3});
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(near(sk.geometry(*arcId)->radius(), 3.0));
    QVERIFY(near(sk.geometry(l1)->end(), {7, 0}, 1e-5));
    QVERIFY(near(sk.geometry(l2)->start(), {10, 3}, 1e-5));
    QVERIFY(near(sk.geometry(*arcId)->center(), {7, 3}, 1e-5));
    QCOMPARE(r.dofs, 0);
  }

  void filletLineArc() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {20, 0}));
    int c = sk.addGeometry(Geometry::makeCircle({20, 10}, 10));  // touches the line at (20,0)? no: passes through (20,0)
    auto arcId = sk.fillet(l, {5, 0}, c, {10, 10}, 2.0);
    QVERIFY(arcId.has_value());
    QVERIFY(sk.solve().ok());
  }

  void splitLine() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    auto n = sk.split(l, {4, 1});
    QVERIFY(n.has_value());
    QVERIFY(near(sk.geometry(l)->end(), {4, 0}));
    QVERIFY(near(sk.geometry(*n)->start(), {4, 0}));
    QVERIFY(sk.findConstraint(ConstraintType::Coincident, {l, PointPos::End}, {*n, PointPos::Start}));
  }

  void removeGeometryDropsConstraints() {
    Sketch sk;
    auto ids = addRectangle(sk, {0, 0}, {10, 10});
    QCOMPARE(sk.constraints().size(), size_t(8));
    sk.removeGeometry(ids[0]);
    QCOMPARE(sk.constraints().size(), size_t(5));
    QCOMPARE(sk.geometryCount(), 3);
  }

  void jsonRoundTrip() {
    Sketch sk;
    auto ids = addRectangle(sk, {0, 0}, {10, 10});
    int a = sk.addGeometry(Geometry::makeArc({20, 0}, 5, 0.3, 2.0));
    sk.geometry(a)->construction = true;
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {ids[0]}, .value = 10, .expression = "w"});
    sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {a}, .value = 5, .driven = true});
    QJsonObject o = sk.toJson();
    Sketch back;
    back.fromJson(o);
    QCOMPARE(back.geometryCount(), sk.geometryCount());
    QCOMPARE(back.constraints().size(), sk.constraints().size());
    const Geometry* ga = back.geometry(a);
    QVERIFY(ga && ga->type == GeomType::Arc && ga->construction);
    QVERIFY(near(ga->start(), sk.geometry(a)->start()));
    QVERIFY(near(ga->end(), sk.geometry(a)->end()));
    const Constraint* d = back.dimensionByName("d1");
    QVERIFY(d);
    QCOMPARE(d->expression, std::string("w"));
    QVERIFY(back.dimensionByName("d2")->driven);
    int newId = back.addGeometry(Geometry::makePoint({0, 0}));
    QVERIFY(newId > a);
  }

  // ---- SK-03: trim / break on splines ----
  static double maxDeviation(const Geometry& piece, const Geometry& orig) {
    double d = 0;
    for (const Pt2& q : piece.tessellate(64)) d = std::max(d, orig.distanceTo(q));
    return d;
  }

  void trimControlSplineMiddleExact() {
    Sketch sk;
    int s = sk.addGeometry(Geometry::makeControlSpline({{0, 0}, {10, 20}, {30, -10}, {40, 10}, {50, 0}}));
    sk.addGeometry(Geometry::makeLine({15, -20}, {15, 30}));
    sk.addGeometry(Geometry::makeLine({35, -20}, {35, 30}));
    const Geometry orig = *sk.geometry(s);
    Pt2 mid = orig.pointAt(orig.paramOf({25, 3}));
    QVERIFY(sk.trimPiece(s, mid).has_value());
    auto out = sk.trim(s, mid);
    QCOMPARE(out.size(), size_t(2));
    const Geometry* a = sk.geometry(out[0]);
    const Geometry* b = sk.geometry(out[1]);
    QCOMPARE(a->type, GeomType::Spline);
    QVERIFY(near(a->start(), orig.start()));
    QVERIFY(near(b->end(), orig.end()));
    QVERIFY(std::abs(a->end().x - 15) < 1e-4);
    QVERIFY(std::abs(b->start().x - 35) < 1e-4);
    QVERIFY(maxDeviation(*a, orig) < 1e-6);  // exact pieces
    QVERIFY(maxDeviation(*b, orig) < 1e-6);
    // pieces survive JSON with their knots, and solve unchanged
    Sketch copy;
    copy.fromJson(sk.toJson());
    QVERIFY(maxDeviation(*copy.geometry(out[0]), orig) < 1e-6);
    QVERIFY(copy.solve().ok());
    QVERIFY(maxDeviation(*copy.geometry(out[0]), orig) < 1e-6);
  }

  void trimFitSplineEndAndBreak() {
    Sketch sk;
    int s = sk.addGeometry(Geometry::makeFitSpline({{0, 0}, {10, 10}, {20, 0}, {30, 10}, {40, 0}}));
    sk.addGeometry(Geometry::makeLine({25, -20}, {25, 30}));
    const Geometry orig = *sk.geometry(s);
    auto out = sk.trim(s, orig.pointAt(orig.paramOf({35, 5})));  // the end piece beyond x = 25
    QCOMPARE(out.size(), size_t(1));
    const Geometry* g = sk.geometry(s);
    QCOMPARE(g->type, GeomType::FitSpline);
    QVERIFY(near(g->start(), {0, 0}));
    QVERIFY(std::abs(g->end().x - 25) < 1e-3);
    QVERIFY(maxDeviation(*g, orig) < 0.05);  // refitted, close to the original
    QVERIFY2(g->vertexCount() <= 8, qPrintable(QString::number(g->vertexCount())));  // few added fit points
    QVERIFY(sk.solve().ok());
    QVERIFY(maxDeviation(*sk.geometry(s), orig) < 0.2);
    // break: two connected pieces
    int n = sk.geometryCount();
    auto second = sk.split(s, g->pointAt(g->paramOf({10, 10})));
    QVERIFY(second.has_value());
    QCOMPARE(sk.geometryCount(), n + 1);
    QVERIFY(near(sk.geometry(s)->end(), sk.geometry(*second)->start(), 1e-9));
    QVERIFY(sk.findConstraint(ConstraintType::Coincident, {s, PointPos::End}, {*second, PointPos::Start}));
    QVERIFY(sk.solve().ok());
    // control-point spline break
    int c = sk.addGeometry(Geometry::makeControlSpline({{0, 50}, {10, 70}, {30, 40}, {40, 60}}));
    const Geometry corig = *sk.geometry(c);
    auto c2 = sk.split(c, corig.pointAt(0.4));
    QVERIFY(c2.has_value());
    QVERIFY(maxDeviation(*sk.geometry(c), corig) < 1e-6);
    QVERIFY(maxDeviation(*sk.geometry(*c2), corig) < 1e-6);
  }

  void trimClosedSplineNeedsTwoCuts() {
    Sketch sk;
    int s = sk.addGeometry(Geometry::makeFitSpline({{0, 0}, {20, -5}, {25, 20}, {5, 25}, {0, 0}}));
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {s, PointPos::End}, .b = {s, PointPos::Start}});
    int l = sk.addGeometry(Geometry::makeLine({12, -20}, {12, 40}));
    const Geometry orig = *sk.geometry(s);
    // one crossing line cuts the loop twice: the piece right of x = 12 goes
    auto piece = sk.trimPiece(s, orig.pointAt(orig.paramOf({25, 20})));
    QVERIFY(piece.has_value());
    for (const Pt2& q : piece->tessellate(32)) QVERIFY(q.x > 12 - 1e-3);
    auto out = sk.trim(s, orig.pointAt(orig.paramOf({25, 20})));
    QCOMPARE(out.size(), size_t(1));
    const Geometry* g = sk.geometry(s);
    QVERIFY(!near(g->start(), g->end(), 1e-3));  // open now
    for (const Pt2& q : g->tessellate(32)) QVERIFY(q.x < 12 + 1e-3);
    QVERIFY(!sk.findConstraint(ConstraintType::Coincident, {s, PointPos::End}, {s, PointPos::Start}));
    QVERIFY(sk.solve().ok());
    (void)l;
  }

  // ---- SK-05: offset of ellipses (approximated by a closed fit-point spline) ----
  void offsetEllipse() {
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipse({10, 5}, 20, 10, 0.4));
    auto out = sk.offset({e}, 3.0, Pt2{10 + 40, 5});  // cursor outside
    QCOMPARE(out.size(), size_t(1));
    const Geometry* o = sk.geometry(out[0]);
    QVERIFY(o->isSpline());
    QVERIFY(near(o->start(), o->end(), 1e-9));
    const Geometry* src = sk.geometry(e);
    double dmin = 1e9, dmax = 0;
    for (const Pt2& q : o->tessellate(128)) {
      double d = src->distanceTo(q);
      dmin = std::min(dmin, d);
      dmax = std::max(dmax, d);
    }
    QVERIFY2(dmin > 2.9 && dmax < 3.1, qPrintable(QString("%1 %2").arg(dmin).arg(dmax)));
    QVERIFY(sk.findConstraint(ConstraintType::Coincident, {out[0], PointPos::End}, {out[0], PointPos::Start}));
    // inward: smaller, inside
    auto in = sk.offset({e}, 2.0, Pt2{10, 5});
    QCOMPARE(in.size(), size_t(1));
    src = sk.geometry(e);  // the geometry vector grew
    for (const Pt2& q : sk.geometry(in[0])->tessellate(64)) QVERIFY2(std::abs(src->distanceTo(q) - 2.0) < 0.1, qPrintable(QString("%1 at %2,%3").arg(src->distanceTo(q)).arg(q.x).arg(q.y)));
    // inward beyond b^2/a (= 5) would fold: nothing
    QVERIFY(sk.offset({e}, 6.0, Pt2{10, 5}).empty());
    // elliptical arc and spline offsets too
    int ea = sk.addGeometry(Geometry::makeEllipseArc({0, 50}, 20, 10, 0.0, 0.0, kPi));
    QCOMPARE(sk.offset({ea}, 2.0, Pt2{0, 80}).size(), size_t(1));
    int sp = sk.addGeometry(Geometry::makeFitSpline({{0, 100}, {10, 110}, {20, 100}}));
    auto so = sk.offset({sp}, 1.0, Pt2{10, 120});
    QCOMPARE(so.size(), size_t(1));
    QVERIFY(sk.geometry(so[0])->center().y > 100 || sk.geometry(so[0])->start().y > 100);
  }
};

QTEST_GUILESS_MAIN(SketchOpsTest)
#include "test_sketch_ops.moc"
