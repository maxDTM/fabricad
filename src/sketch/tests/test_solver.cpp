// Sketch solver tests (PlaneGCS wrapper): constraints, DOF, conflicts, dragging.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"
#include "sketch/SketchCurves.h"
#include "support/SketchTestHelpers.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

class SolverTest : public QObject {
  Q_OBJECT
 private slots:
  void freeLineHasFourDof() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 3}));
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QCOMPARE(r.dofs, 4);
    QVERIFY(!r.isFullyConstrained(l));
    QCOMPARE(sk.statusOf(l), GeometryStatus::Free);
  }

  void horizontalAndDistance() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {7, 3}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {l}, .value = 10});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QCOMPARE(r.dofs, 0);
    QVERIFY(r.isFullyConstrained(l));
    const Geometry* g = sk.geometry(l);
    QVERIFY(near(g->end(), {10, 0}));
    QCOMPARE(sk.statusOf(l), GeometryStatus::FullyConstrained);
  }

  void rectangleFullyConstrained() {
    Sketch sk;
    auto ids = addRectangle(sk, {0, 0}, {30, 20});
    QCOMPARE(sk.solve().dofs, 4);  // position (2) + width + height
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {ids[0], PointPos::Start}});
    QCOMPARE(sk.solve().dofs, 2);
    int w = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {ids[0]}, .value = 40});
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {ids[1]}, .value = 25});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QCOMPARE(r.dofs, 0);
    QVERIFY(near(sk.geometry(ids[1])->start(), {40, 0}));
    QVERIFY(near(sk.geometry(ids[2])->start(), {40, 25}));
    QVERIFY(near(sk.geometry(ids[3])->start(), {0, 25}));
    QCOMPARE(sk.constraint(w)->name, std::string("d1"));
    for (int id : ids) QVERIFY(r.isFullyConstrained(id));
    // change the dimension and re-solve
    sk.constraint(w)->value = 50;
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(ids[1])->start(), {50, 0}));
  }

  void conflictingConstraintsDetected() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 1}));
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {l}, .value = 10});
    int d2 = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {l}, .value = 20});
    SolveResult r = sk.solve();
    QCOMPARE(r.status, SolveResult::Status::Conflicting);
    QVERIFY(!r.conflicting.empty());
    QVERIFY(std::count(r.conflicting.begin(), r.conflicting.end(), d2) > 0);
    QCOMPARE(sk.statusOf(l), GeometryStatus::Conflicting);
  }

  void collapsingSolutionIsRejected() {
    // horizontal + vertical on one line: only a zero-length line satisfies both
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 1}));
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l}});
    SolveResult r = sk.solve();
    QCOMPARE(r.status, SolveResult::Status::Conflicting);
    QVERIFY(sk.geometry(l)->length() > 1.0);  // geometry restored
    QCOMPARE(sk.statusOf(l), GeometryStatus::Conflicting);
  }

  // fuzzing (docs/TESTING.md): a line coincident end-to-start with itself plus a length left NaN
  // in the geometry, which then made OCCT's profile splitter crawl for 20+ seconds
  void contradictoryConstraintsNeverLeaveNaN() {
    const char* json = R"({"geometry":[{"id":1,"p":[0,0,20,0],"type":"Line"},{"id":2,"p":[20,0,20,10],"type":"Line"},
      {"id":3,"p":[20,10,0,10],"type":"Line"},{"id":4,"p":[0,10,0,0],"type":"Line"}],"constraints":[
      {"a":{"g":1,"pos":"end"},"b":{"g":2,"pos":"start"},"id":1,"type":"Coincident"},
      {"a":{"g":2,"pos":"end"},"b":{"g":2,"pos":"start"},"id":2,"type":"Coincident"},
      {"a":{"g":3,"pos":"end"},"b":{"g":4,"pos":"start"},"id":3,"type":"Coincident"},
      {"a":{"g":4,"pos":"end"},"b":{"g":1,"pos":"start"},"id":4,"type":"Coincident"},
      {"a":{"g":1},"id":5,"type":"Horizontal"},{"a":{"g":3},"id":6,"type":"Horizontal"},
      {"a":{"g":2},"id":7,"type":"Vertical"},{"a":{"g":4},"id":8,"type":"Vertical"},
      {"a":{"g":1,"pos":"start"},"id":9,"name":"origin","type":"Fix"},
      {"a":{"g":1},"id":10,"name":"d1","type":"Distance","value":20},
      {"a":{"g":2},"id":11,"name":"d2","type":"Distance","value":10}]})";
    Sketch sk;
    sk.fromJson(QJsonDocument::fromJson(json).object());
    QCOMPARE(sk.geometries().size(), size_t(4));
    SolveResult r = sk.solve();
    QVERIFY(!r.ok() || r.status == SolveResult::Status::Conflicting || r.status == SolveResult::Status::Success);
    for (const Geometry& g : sk.geometries())
      for (double v : g.p) QVERIFY2(std::isfinite(v), qPrintable(QString("geometry %1").arg(g.id)));
  }

  void redundantConstraintsDetected() {
    Sketch sk;
    int l1 = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int l2 = sk.addGeometry(Geometry::makeLine({0, 5}, {10, 5}));
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l1}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l2}});
    int par = sk.addConstraint(Constraint{.type = ConstraintType::Parallel, .a = {l1}, .b = {l2}});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(!r.redundant.empty());
    QVERIFY(std::count(r.redundant.begin(), r.redundant.end(), par) > 0 || r.redundant.size() == 1);
  }

  void tangentLineCircle() {
    Sketch sk;
    int c = sk.addGeometry(Geometry::makeCircle({0, 0}, 5));
    int l = sk.addGeometry(Geometry::makeLine({-10, 6}, {10, 6}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {c}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l}, .b = {c}});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    const Geometry* g = sk.geometry(l);
    QVERIFY(near(g->start().y, 5.0, 1e-6));
    QVERIFY(near(g->end().y, 5.0, 1e-6));
  }

  void arcTangentToLineAtEndpoint() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    // quarter arc centred at (10, 5), starting at (10, 0) going ccw to (15, 5)
    int a = sk.addGeometry(Geometry::makeArc({10, 5.5}, 5, -kPi / 2, 0));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l, PointPos::End}, .b = {a, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l, PointPos::End}, .b = {a, PointPos::Start}});
    // the coincident constraint must have been replaced by the tangent one
    QVERIFY(!sk.findConstraint(ConstraintType::Coincident, {l, PointPos::End}, {a, PointPos::Start}));
    sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {a}, .value = 5});
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    const Geometry* g = sk.geometry(a);
    QVERIFY(near(g->start(), {10, 0}, 1e-6));
    QVERIFY(near(g->center(), {10, 5}, 1e-6));
    QCOMPARE(r.dofs, 1);  // sweep of the arc
  }

  void equalConcentricSymmetricMidpoint() {
    Sketch sk;
    int c1 = sk.addGeometry(Geometry::makeCircle({0, 0}, 5));
    int c2 = sk.addGeometry(Geometry::makeCircle({1, 1}, 3));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {c1}});
    sk.addConstraint(Constraint{.type = ConstraintType::Concentric, .a = {c1}, .b = {c2}});
    sk.addConstraint(Constraint{.type = ConstraintType::Equal, .a = {c1}, .b = {c2}});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QCOMPARE(r.dofs, 0);
    QVERIFY(near(sk.geometry(c2)->center(), {0, 0}));
    QVERIFY(near(sk.geometry(c2)->radius(), 5.0));

    int l = sk.addGeometry(Geometry::makeLine({-10, 2}, {10, 2}));
    int p = sk.addGeometry(Geometry::makePoint({3, 3}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Midpoint, .a = {p, PointPos::Center}, .b = {l}});
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(p)->center(), {0, 2}));

    int q = sk.addGeometry(Geometry::makePoint({4, 7}));
    int q2 = sk.addGeometry(Geometry::makePoint({4, -1}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {q}});
    sk.addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {q, PointPos::Center}, .b = {q2, PointPos::Center}, .c = {l}});
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(q2)->center(), {4, -3}));
  }

  void anglePerpendicularParallelCollinear() {
    Sketch sk;
    int l1 = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int l2 = sk.addGeometry(Geometry::makeLine({10, 0}, {12, 9}));
    int l3 = sk.addGeometry(Geometry::makeLine({0, 5}, {10, 6}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l1}});
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l1, PointPos::End}, .b = {l2, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Angle, .a = {l1}, .b = {l2}, .value = 90});
    sk.addConstraint(Constraint{.type = ConstraintType::Parallel, .a = {l1}, .b = {l3}});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(l2)->end().x, 10.0, 1e-6));
    QVERIFY(near(sk.geometry(l3)->direction().y, 0.0, 1e-6));
    int l4 = sk.addGeometry(Geometry::makeLine({20, 1}, {30, 2}));
    sk.addConstraint(Constraint{.type = ConstraintType::Collinear, .a = {l1}, .b = {l4}});
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(l4)->start().y, 0.0, 1e-6));
    QVERIFY(near(sk.geometry(l4)->end().y, 0.0, 1e-6));
    int l5 = sk.addGeometry(Geometry::makeLine({0, 10}, {10, 11}));
    sk.addConstraint(Constraint{.type = ConstraintType::Perpendicular, .a = {l1}, .b = {l5}});
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(std::abs(sk.geometry(l5)->direction().y), 1.0, 1e-6));
  }

  void distanceXYAndPointOnCurve() {
    Sketch sk;
    int p0 = sk.addGeometry(Geometry::makePoint({0, 0}));
    int p1 = sk.addGeometry(Geometry::makePoint({3, 4}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {p0}});
    sk.addConstraint(Constraint{.type = ConstraintType::DistanceX, .a = {p0, PointPos::Center}, .b = {p1, PointPos::Center}, .value = 12});
    sk.addConstraint(Constraint{.type = ConstraintType::DistanceY, .a = {p0, PointPos::Center}, .b = {p1, PointPos::Center}, .value = -7});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(p1)->center(), {12, -7}));
    int c = sk.addGeometry(Geometry::makeCircle({12, -7}, 4));
    int p2 = sk.addGeometry(Geometry::makePoint({20, -7}));
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {c, PointPos::Center}, .b = {p1, PointPos::Center}});
    sk.addConstraint(Constraint{.type = ConstraintType::Diameter, .a = {c}, .value = 10});
    sk.addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {p2, PointPos::Center}, .b = {c}});
    r = sk.solve();
    QVERIFY(r.ok());
    QVERIFY(near(sk.geometry(p2)->center().distance({12, -7}), 5.0, 1e-6));
    auto m = sk.measure(*sk.constraint(sk.constraints().back().id - 1));
    QVERIFY(m.has_value());
  }

  void dragMovesFreePointAndKeepsConstraints() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l}});
    sk.solve();
    QVERIFY(sk.beginDrag({l, PointPos::End}, {10, 0}));
    QVERIFY(sk.drag({17, 5}));
    sk.endDrag();
    const Geometry* g = sk.geometry(l);
    QVERIFY(near(g->start(), {0, 0}));
    QVERIFY(near(g->end().y, 0.0, 1e-6));   // horizontal kept
    QVERIFY(near(g->end().x, 17.0, 1e-3));  // follows the cursor along x
    // dragging the line body translates it (start is fixed -> cannot move)
    QVERIFY(sk.beginDrag({l, PointPos::None}, {5, 0}));
    QVERIFY(sk.drag({5, 3}));
    sk.endDrag();
    QVERIFY(near(sk.geometry(l)->start(), {0, 0}));
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
  }

  void drivenDimensionIsMeasuredNotEnforced() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int d = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {l}, .value = 99, .driven = true});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QCOMPARE(r.dofs, 4);
    QVERIFY(near(*sk.measure(*sk.constraint(d)), 10.0));
  }

  void expressionsEvaluated() {
    Sketch sk;
    int l = sk.addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {l}, .value = 1, .expression = "w*2"});
    auto errs = sk.evaluateExpressions([](const std::string& e, bool, std::string*) -> std::optional<double> {
      if (e == "w*2") return 16.0;
      return std::nullopt;
    });
    QVERIFY(errs.empty());
    sk.solve();
    QVERIFY(near(sk.geometry(l)->length(), 16.0));
    errs = sk.evaluateExpressions([](const std::string&, bool, std::string* err) -> std::optional<double> {
      *err = "unknown";
      return std::nullopt;
    });
    QCOMPARE(errs.size(), size_t(1));
  }

  // SK-01: moving an ellipse's centre (drag or dimension) must translate it, not reshape it.
  void ellipseCentreDragKeepsShape() {
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipse({0, 0}, 20, 10, 0.3));
    sk.solve();
    int dofBefore = sk.solve().dofs;
    QVERIFY(sk.beginDrag({e, PointPos::Center}, {0, 0}));
    QVERIFY(sk.drag({15, -5}));
    sk.endDrag();
    const Geometry* g = sk.geometry(e);
    QVERIFY(near(g->center(), {15, -5}));
    QVERIFY(near(g->majorRadius(), 20.0));
    QVERIFY(near(g->minorRadius(), 10.0));
    Pt2 ax = g->ellipse().major;
    QVERIFY(std::abs(std::atan2(ax.y, ax.x) - 0.3) < 1e-6);
    QCOMPARE(sk.solve().dofs, dofBefore);
  }

  // general retest NEW-ADVANCED-SKETCH-3: the R labels of a dragged ellipse move with it
  void dragMovesPlacedDimensionLabels() {
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipse({0, 0}, 20, 10, 0.0));
    int maj = sk.addConstraint(Constraint{.type = ConstraintType::MajorRadius, .a = {e}, .value = 20});
    int mn = sk.addConstraint(Constraint{.type = ConstraintType::MinorRadius, .a = {e}, .value = 10});
    QVERIFY(sk.solve().ok());
    // a label the user placed somewhere else keeps its offset from the geometry
    for (Constraint& c : sk.constraints())
      if (c.id == mn) c.labelPos = {3, 25};
    auto labelOf = [&](int id) {
      for (const Constraint& c : sk.constraints())
        if (c.id == id) return c.labelPos;
      return Pt2{};
    };
    const Pt2 majBefore = labelOf(maj);
    QVERIFY(sk.beginDrag({e, PointPos::Center}, {0, 0}));
    QVERIFY(sk.drag({30, -8}));
    sk.endDrag();
    QVERIFY(near(sk.geometry(e)->center(), {30, -8}));
    QVERIFY(near(labelOf(maj), majBefore + Pt2{30, -8}));
    QVERIFY(near(labelOf(mn), Pt2{33, 17}));
  }

  void ellipseArcCentreDragKeepsShape() {
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipseArc({0, 0}, 20, 10, 0.0, 0.2, 2.0));
    sk.solve();
    Pt2 s0 = sk.geometry(e)->start();
    QVERIFY(sk.beginDrag({e, PointPos::Center}, {0, 0}));
    QVERIFY(sk.drag({5, 7}));
    sk.endDrag();
    const Geometry* g = sk.geometry(e);
    QVERIFY(near(g->center(), {5, 7}));
    QVERIFY(near(g->majorRadius(), 20.0));
    QVERIFY(near(g->minorRadius(), 10.0));
    QVERIFY(near(g->start(), s0 + Pt2{5, 7}));
  }

  void ellipseCentreDimensionKeepsShape() {
    Sketch sk;
    int o = sk.addGeometry(Geometry::makePoint({0, 0}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {o}});
    int e = sk.addGeometry(Geometry::makeEllipse({10, 10}, 20, 8, 0.0));
    int dofBefore = sk.solve().dofs;
    sk.addConstraint(Constraint{.type = ConstraintType::DistanceX, .a = {o, PointPos::Center}, .b = {e, PointPos::Center}, .value = 40});
    sk.addConstraint(Constraint{.type = ConstraintType::DistanceY, .a = {o, PointPos::Center}, .b = {e, PointPos::Center}, .value = -12});
    SolveResult r = sk.solve();
    QVERIFY(r.ok());
    QCOMPARE(r.dofs, dofBefore - 2);
    const Geometry* g = sk.geometry(e);
    QVERIFY(near(g->center(), {40, -12}));
    QVERIFY(near(g->majorRadius(), 20.0));
    QVERIFY(near(g->minorRadius(), 8.0));
    QVERIFY(std::abs(g->ellipse().major.y) < 1e-6);
  }
};

QTEST_GUILESS_MAIN(SolverTest)
#include "test_solver.moc"
