// Sketch transform tests: move / copy / rotate / scale, rectangular and circular sketch
// patterns and sketch chamfer on literal, fully specified shapes (exact expected positions).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"
#include "sketch/SketchProfiles.h"
#include "sketch/SketchTransform.h"
#include "support/SketchTestHelpers.h"

#include <QtTest/QtTest>

#include <algorithm>

using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

namespace {
const gp_Ax3 kXY(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));

int count(const Sketch& sk, ConstraintType t) {
  return static_cast<int>(std::count_if(sk.constraints().begin(), sk.constraints().end(), [&](const Constraint& c) { return c.type == t; }));
}

bool hasCircleAt(const Sketch& sk, Pt2 c, double r) {
  for (const Geometry& g : sk.geometries())
    if (g.type == GeomType::Circle && near(g.center(), c, 1e-9) && near(g.radius(), r, 1e-9)) return true;
  return false;
}

bool hasPointAt(const Sketch& sk, Pt2 c) {
  for (const Geometry& g : sk.geometries())
    if (g.type == GeomType::Point && near(g.center(), c, 1e-9)) return true;
  return false;
}
}  // namespace

class TransformTest : public QObject {
  Q_OBJECT
 private slots:
  void moveKeepsInternalDropsExternal() {
    Sketch sk;
    auto rect = addRectangle(sk, {0, 0}, {10, 5});
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {rect[0]}, .value = 10});
    // an outside line whose end is coincident with the rectangle corner
    int out = sk.addGeometry(Geometry::makeLine({-10, 0}, {0, 0}));
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {out, PointPos::End}, .b = {rect[0], PointPos::Start}});
    QVERIFY(sk.solve().ok());
    int before = static_cast<int>(sk.constraints().size());
    auto moved = transformEntities(sk, rect, SketchTransform::translate({20, 3}), false);
    QCOMPARE(moved.size(), size_t(4));
    QVERIFY(near(sk.geometry(rect[0])->start(), {20, 3}));
    QVERIFY(near(sk.geometry(rect[2])->start(), {30, 8}));
    QVERIFY(near(sk.geometry(out)->end(), {0, 0}));  // not moved
    QCOMPARE(static_cast<int>(sk.constraints().size()), before - 1);  // the tie to `out` is gone
    QVERIFY(sk.solve().ok());
    QVERIFY(near(sk.geometry(rect[0])->start(), {20, 3}, 1e-9));  // nothing pulls it back
    // fixed geometry does not move
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {out}});
    QVERIFY(transformEntities(sk, {out}, SketchTransform::translate({1, 1}), false).empty());
    QVERIFY(near(sk.geometry(out)->start(), {-10, 0}));
  }

  void rotateCopyAndScale() {
    Sketch sk;
    auto rect = addRectangle(sk, {0, 0}, {10, 5});
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {rect[0]}, .value = 10});
    sk.addConstraint(Constraint{.type = ConstraintType::Parallel, .a = {rect[0]}, .b = {rect[2]}});
    QVERIFY(sk.solve().ok());
    int nCo = count(sk, ConstraintType::Coincident), nPar = count(sk, ConstraintType::Parallel);
    // copy rotated by 90 degrees about the origin
    auto copies = transformEntities(sk, rect, SketchTransform::rotate({0, 0}, kPi / 2), true);
    QCOMPARE(copies.size(), size_t(4));
    QCOMPARE(sk.geometryCount(), 8);
    QVERIFY(near(sk.geometry(copies[0])->end(), {0, 10}, 1e-12));
    QVERIFY(near(sk.geometry(copies[1])->end(), {-5, 10}, 1e-12));
    QCOMPARE(count(sk, ConstraintType::Coincident), 2 * nCo);
    QCOMPARE(count(sk, ConstraintType::Parallel), 2 * nPar);
    QCOMPARE(count(sk, ConstraintType::Horizontal), 2);  // orientation constraints not copied
    QCOMPARE(count(sk, ConstraintType::Distance), 2);    // the length dimension is
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(near(sk.geometry(copies[0])->length(), 10.0, 1e-9));
    // rotate the original in place by 30 degrees: H/V constraints of the moved set are dropped
    transformEntities(sk, rect, SketchTransform::rotate({0, 0}, kPi / 6), false);
    QCOMPARE(count(sk, ConstraintType::Horizontal), 0);
    QVERIFY(near(sk.geometry(rect[0])->end(), Pt2::polar(10, kPi / 6), 1e-12));
    QVERIFY(sk.solve().ok());
    // scale a dimensioned circle by 2 about (10, 0)
    Sketch s2;
    int c = s2.addGeometry(Geometry::makeCircle({20, 0}, 3));
    s2.addConstraint(Constraint{.type = ConstraintType::Diameter, .a = {c}, .value = 6, .expression = "d0"});
    transformEntities(s2, {c}, SketchTransform::scaling({10, 0}, 2.0), false);
    QVERIFY(near(s2.geometry(c)->center(), {30, 0}));
    QVERIFY(near(s2.geometry(c)->radius(), 6.0));
    QVERIFY(near(s2.constraints().front().value, 12.0));
    QVERIFY(s2.constraints().front().expression.empty());
    QVERIFY(s2.solve().ok());
    QVERIFY(near(s2.geometry(c)->radius(), 6.0, 1e-9));
    // move + rotate + scale of the new curve types
    Sketch s3;
    int e = s3.addGeometry(Geometry::makeEllipseArc({0, 0}, 20, 10, 0, 0, kPi / 2));
    int sp = s3.addGeometry(Geometry::makeFitSpline({{0, 0}, {10, 5}, {20, 0}}));
    int t = s3.addGeometry(Geometry::makeText({0, 0}, "A", 5));
    transformEntities(s3, {e, sp, t}, SketchTransform{{0, 0}, kPi / 2, 2.0, {1, 1}}, false);
    QVERIFY(near(s3.geometry(e)->start(), {1, 41}, 1e-9));
    QVERIFY(near(s3.geometry(sp)->end(), {1, 41}, 1e-9));
    QVERIFY(near(s3.geometry(t)->p[2], 10.0) && near(s3.geometry(t)->p[3], kPi / 2));
  }

  void rectangularPatternGrid() {
    Sketch sk;
    int c = sk.addGeometry(Geometry::makeCircle({0, 0}, 2));
    sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {c}, .value = 2});
    RectangularPatternSpec spec;
    spec.direction1 = {1, 0};
    spec.count1 = 3;
    spec.spacing1 = 10;
    spec.direction2 = {0, 1};
    spec.count2 = 2;
    spec.spacing2 = 8;
    auto made = rectangularPattern(sk, {c}, spec);
    QCOMPARE(made.size(), size_t(5));
    for (Pt2 p : {Pt2{10, 0}, Pt2{20, 0}, Pt2{0, 8}, Pt2{10, 8}, Pt2{20, 8}}) QVERIFY(hasCircleAt(sk, p, 2));
    QCOMPARE(count(sk, ConstraintType::Radius), 6);  // each instance keeps its dimension
    QCOMPARE(computeProfiles(sk, kXY).size(), size_t(6));
    // symmetric: two per side along X only
    Sketch s2;
    int p = s2.addGeometry(Geometry::makePoint({0, 0}));
    RectangularPatternSpec sym;
    sym.count1 = 2;
    sym.spacing1 = 5;
    sym.count2 = 1;
    sym.symmetric = true;
    QCOMPARE(rectangularPattern(s2, {p}, sym).size(), size_t(2));
    QVERIFY(hasPointAt(s2, {-5, 0}) && hasPointAt(s2, {5, 0}));
  }

  void circularPatternFullAndPartial() {
    Sketch sk;
    int p = sk.addGeometry(Geometry::makePoint({10, 0}));
    auto made = circularPattern(sk, {p}, CircularPatternSpec{{0, 0}, 4, 360});
    QCOMPARE(made.size(), size_t(3));
    for (Pt2 q : {Pt2{0, 10}, Pt2{-10, 0}, Pt2{0, -10}}) QVERIFY(hasPointAt(sk, q));
    // 3 instances over 90 degrees: 0, 45, 90
    Sketch s2;
    int q = s2.addGeometry(Geometry::makePoint({10, 0}));
    circularPattern(s2, {q}, CircularPatternSpec{{0, 0}, 3, 90});
    QVERIFY(hasPointAt(s2, Pt2::polar(10, kPi / 4)) && hasPointAt(s2, {0, 10}));
    // a slot-like closed shape patterned 6x around: six separate profiles
    Sketch s3;
    auto rect = addRectangle(s3, {20, -2}, {30, 2});
    QVERIFY(s3.solve().ok());
    circularPattern(s3, rect, CircularPatternSpec{{0, 0}, 6, 360});
    QCOMPARE(s3.geometryCount(), 24);
    QVERIFY(s3.solve().ok());
    auto regions = computeProfiles(s3, kXY);
    QCOMPARE(regions.size(), size_t(6));
    for (const auto& r : regions) QVERIFY(near(r.area, 40.0, 1e-6));
  }

  void chamferCorner() {
    // closed 10 x 10 square; chamfer the corner at (10, 0) by 2 (along the bottom) x 3 (up)
    Sketch sk;
    auto sq = addRectangle(sk, {0, 0}, {10, 10});
    QVERIFY(sk.solve().ok());
    auto ch = chamfer(sk, sq[0], {5, 0}, sq[1], {10, 5}, 2, 3);
    QVERIFY(ch.has_value());
    const Geometry* g = sk.geometry(*ch);
    QVERIFY(near(g->start(), {8, 0}));
    QVERIFY(near(g->end(), {10, 3}));
    QVERIFY(near(sk.geometry(sq[0])->end(), {8, 0}));
    QVERIFY(near(sk.geometry(sq[1])->start(), {10, 3}));
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(near(sk.geometry(*ch)->start(), {8, 0}, 1e-9));
    auto regions = computeProfiles(sk, kXY);
    QCOMPARE(regions.size(), size_t(1));
    QVERIFY(near(regions[0].area, 100.0 - 3.0, 1e-6));
    // distance too large for the line: refused, nothing changed
    int n = sk.geometryCount();
    QVERIFY(!chamfer(sk, sq[2], {5, 10}, sq[3], {0, 5}, 20, 1).has_value());
    QCOMPARE(sk.geometryCount(), n);
    // lines that do not touch are extended to their corner
    Sketch s2;
    int a = s2.addGeometry(Geometry::makeLine({0, 0}, {8, 0}));
    int b = s2.addGeometry(Geometry::makeLine({10, 2}, {10, 10}));
    auto c2 = chamfer(s2, a, {2, 0}, b, {10, 8}, 4, 4);
    QVERIFY(c2.has_value());
    QVERIFY(near(s2.geometry(a)->end(), {6, 0}) && near(s2.geometry(b)->start(), {10, 4}));
  }
};

QTEST_GUILESS_MAIN(TransformTest)
#include "test_sketch_transform.moc"
