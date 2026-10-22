// Sketch fillet / chamfer with typed (driving) dimensions on the trimmed lines: the dimensions
// are re-targeted to the "virtual sharp" like Fusion 360 (far end to the adjacent line, then
// line to line) so the shape keeps its typed size, and dimensions that cannot be re-expressed
// become driven. Fixed shape: the chair seat slab of docs/comparison/CHAIR_SPEC.md (420 x 400,
// R20 at both front corners).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"
#include "sketch/SketchTransform.h"
#include "support/SketchTestHelpers.h"

#include <QtTest/QtTest>

using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

namespace {

/// 420 x 400 rectangle with the corners typed: Distance 420 on the bottom line, 400 on the left.
struct Slab {
  Sketch sk;
  std::vector<int> ids;  // bottom, right, top, left
  int width = -1, height = -1;
  Slab() {
    ids = addRectangle(sk, {-210, -200}, {210, 200});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {ids[2], PointPos::End}});  // top-left corner fixed
    width = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {ids[0]}, .value = 420});
    height = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {ids[3]}, .value = 400});
    QVERIFY(sk.solve().ok());
  }
  bool bboxIs(Pt2 mn, Pt2 mx) {
    Pt2 a, b;
    return sk.bbox(a, b) && near(a, mn, 1e-6) && near(b, mx, 1e-6);
  }
};

}  // namespace

class FilletDimensionsTest : public QObject {
  Q_OBJECT
 private slots:
  void filletKeepsTypedSizeAndDimensionsDrive() {
    Slab s;
    // front-left corner: bottom x left
    auto arc1 = s.sk.fillet(s.ids[0], {0, -200}, s.ids[3], {-210, 0}, 20);
    QVERIFY(arc1);
    QVERIFY(s.sk.solve().ok());
    QVERIFY(s.bboxIs({-210, -200}, {210, 200}));
    // the width now measures from the bottom line's far end to the left line (virtual sharp)
    const Constraint* w = s.sk.constraint(s.width);
    QVERIFY(w && !w->driven);
    QCOMPARE(w->a.geo, s.ids[0]);
    QCOMPARE(w->a.pos, PointPos::End);
    QCOMPARE(w->b.geo, s.ids[3]);
    QVERIFY(near(*s.sk.measure(*w), 420));
    // front-right corner: bottom x right -> the width becomes the distance between the vertical lines
    auto arc2 = s.sk.fillet(s.ids[0], {0, -200}, s.ids[1], {210, 0}, 20);
    QVERIFY(arc2);
    QVERIFY(s.sk.solve().ok());
    QVERIFY(s.bboxIs({-210, -200}, {210, 200}));
    w = s.sk.constraint(s.width);
    QVERIFY(w && !w->driven);
    QCOMPARE(w->a.pos, PointPos::None);
    QCOMPARE(w->b.pos, PointPos::None);
    QVERIFY(near(*s.sk.measure(*w), 420));
    QVERIFY(s.sk.takeDemotedDimensions().empty());
    // pin the fillet radii (the fillet leaves them free), then the dimensions still drive:
    // 500 wide moves the right side
    s.sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {*arc1}, .value = 20});
    s.sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {*arc2}, .value = 20});
    QVERIFY(s.sk.solve().ok());
    QCOMPARE(s.sk.dofs(), 0);
    s.sk.constraint(s.width)->value = 500;
    QVERIFY(s.sk.solve().ok());
    QVERIFY(s.bboxIs({-210, -200}, {290, 200}));
    QVERIFY(near(s.sk.geometry(*arc2)->radius(), 20));
    s.sk.constraint(s.height)->value = 300;
    QVERIFY(s.sk.solve().ok());
    QVERIFY(s.bboxIs({-210, -100}, {290, 200}));
  }

  void chamferKeepsTypedSize() {
    Slab s;
    QVERIFY(chamfer(s.sk, s.ids[0], {0, -200}, s.ids[1], {210, 0}, 30, 30));
    QVERIFY(s.sk.solve().ok());
    QVERIFY(s.bboxIs({-210, -200}, {210, 200}));
    QVERIFY(!s.sk.constraint(s.width)->driven);
    s.sk.constraint(s.width)->value = 440;
    QVERIFY(s.sk.solve().ok());
    QVERIFY(s.bboxIs({-210, -200}, {230, 200}));
  }

  void obliqueCornerDemotesTheDimension() {
    Sketch sk;
    int a = sk.addGeometry(Geometry::makeLine({0, 0}, {40, 0}));
    int b = sk.addGeometry(Geometry::makeLine({40, 0}, {60, 30}));  // not perpendicular
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {a, PointPos::End}, .b = {b, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {a, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {a}});
    int d = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {a}, .value = 40});
    QVERIFY(sk.solve().ok());
    QVERIFY(sk.fillet(a, {20, 0}, b, {50, 15}, 5));
    QVERIFY(sk.solve().ok());
    QVERIFY(sk.constraint(d)->driven);
    const std::vector<std::string> demoted = sk.takeDemotedDimensions();
    QCOMPARE(demoted.size(), size_t(1));
    QCOMPARE(demoted.front(), sk.constraint(d)->name);
    QVERIFY(sk.takeDemotedDimensions().empty());
  }

  /// UX-22: a corner that other constraints still pin (a third curve's coincidence, an old hidden
  /// alignment, a Dimension-tool point dimension) keeps them on a virtual-sharp construction point:
  /// the solve succeeds, nothing moves, and the dimensions keep driving.
  void filletKeepsCornerConstraintsOnVirtualSharp() {
    Sketch sk;
    int a = sk.addGeometry(Geometry::makeLine({0, 0}, {40, 0}));
    int b = sk.addGeometry(Geometry::makeLine({40, 0}, {40, 30}));
    int c = sk.addGeometry(Geometry::makeLine({40, 0}, {60, -10}));  // third curve at the corner
    int d = sk.addGeometry(Geometry::makeLine({70, 0}, {80, 5}));    // a separate shape
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {a, PointPos::End}, .b = {b, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {c, PointPos::Start}, .b = {a, PointPos::End}});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {a, PointPos::Start}});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {c, PointPos::End}});
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {d}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {a}});
    sk.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {b}});
    // hidden alignment from an older document (UX-23) between the corner and the other shape
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {b, PointPos::Start}, .b = {d, PointPos::Start}, .internal = true});
    int dx = sk.addConstraint(Constraint{.type = ConstraintType::DistanceX, .a = {a, PointPos::Start}, .b = {b, PointPos::Start}, .value = 40});
    int len = sk.addConstraint(Constraint{.type = ConstraintType::Distance, .a = {b}, .value = 30});
    QVERIFY(sk.solve().ok());
    const int before = sk.geometryCount();
    auto arc = sk.fillet(a, {20, 0}, b, {40, 15}, 5);
    QVERIFY(arc);
    QVERIFY(sk.solve().ok());
    QCOMPARE(sk.geometryCount(), before + 2);  // the arc and the sharp point
    int sharp = -1;
    for (const Geometry& g : sk.geometries())
      if (g.type == GeomType::Point) sharp = g.id;
    QVERIFY(sharp > 0);
    QVERIFY(sk.geometry(sharp)->construction);
    QVERIFY(near(sk.geometry(sharp)->center(), {40, 0}));
    QVERIFY(near(sk.geometry(c)->start(), {40, 0}));  // the third curve still meets the corner
    QVERIFY(near(sk.geometry(a)->end(), {35, 0}));
    QVERIFY(near(sk.geometry(b)->start(), {40, 5}));
    QVERIFY(!sk.constraint(dx)->driven);
    QCOMPARE(sk.constraint(dx)->b.geo, sharp);
    QVERIFY(!sk.constraint(len)->driven);
    QVERIFY(sk.takeDemotedDimensions().empty());
    QVERIFY(!sk.takeModifyNotes().empty());
    // the point dimension still drives the corner
    sk.constraint(dx)->value = 50;
    QVERIFY(sk.solve().ok());
    QVERIFY(near(sk.geometry(sharp)->center(), {50, 0}));
    QVERIFY(near(sk.geometry(b)->start().x, 50));
    QVERIFY(near(sk.geometry(c)->start(), {50, 0}));
    QVERIFY(near(sk.geometry(*arc)->radius(), 5, 1e-6) || sk.geometry(*arc)->radius() > 0);
  }

  /// UX-22 / UX-27b: filleting one corner of a regular polygon keeps it regular (Equal moves onto
  /// construction lines that stand in for the untrimmed sides).
  void filletKeepsPolygonRegular() {
    Sketch sk;
    const int n = 5;  // odd: an equilateral tangential polygon is regular
    const double R = 10;
    std::vector<Pt2> vs;
    for (int i = 0; i < n; ++i) vs.push_back(Pt2::polar(R / std::cos(kPi / n), 2 * kPi * i / n + kPi / n));
    std::vector<int> l(n);
    for (int i = 0; i < n; ++i) l[i] = sk.addGeometry(Geometry::makeLine(vs[i], vs[(i + 1) % n]));
    Geometry circle = Geometry::makeCircle({0, 0}, R);
    circle.construction = true;
    int c = sk.addGeometry(circle);
    for (int i = 0; i < n; ++i) {
      sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l[i], PointPos::End}, .b = {l[(i + 1) % n], PointPos::Start}});
      if (i > 0) sk.addConstraint(Constraint{.type = ConstraintType::Equal, .a = {l[0]}, .b = {l[i]}});
      sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l[i]}, .b = {c}, .internal = true});
    }
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {c, PointPos::Center}});
    sk.addConstraint(Constraint{.type = ConstraintType::Angle, .a = {l[0]}, .value = 90 + 360.0 / n});
    int rad = sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {c}, .value = R});
    QVERIFY(sk.solve().ok());
    const double side = 2 * R * std::tan(kPi / n);
    QVERIFY2(near(sk.geometry(l[3])->length(), side, 1e-6), std::to_string(sk.geometry(l[3])->length()).c_str());
    auto arc = sk.fillet(l[0], sk.geometry(l[0])->midpoint(), l[1], sk.geometry(l[1])->midpoint(), 2);
    QVERIFY(arc);
    QVERIFY(sk.solve().ok());
    // the fillet stays a small arc inside the corner (exterior angle 2 pi / n), tangent to both sides
    auto checkArc = [&] {
      const Geometry* a = sk.geometry(*arc);
      QVERIFY2(near(a->sweep(), 2 * kPi / n, 1e-6), std::to_string(a->sweep()).c_str());
      QVERIFY(a->center().length() < sk.geometry(c)->radius());
      const Geometry* s0 = sk.geometry(l[0]);
      const Geometry* s1 = sk.geometry(l[1]);
      QVERIFY(near(a->start(), s0->end(), 1e-6) || near(a->end(), s0->end(), 1e-6));
      QVERIFY(near(a->start(), s1->start(), 1e-6) || near(a->end(), s1->start(), 1e-6));
    };
    checkArc();
    for (int i = 2; i < n; ++i) QVERIFY(near(sk.geometry(l[i])->length(), side, 1e-6));
    // change the size: every untrimmed side follows, the polygon stays regular
    sk.constraint(rad)->value = 12;
    QVERIFY(sk.solve().ok());
    const double side12 = 2 * 12 * std::tan(kPi / n);
    for (int i = 2; i < n; ++i) QVERIFY2(near(sk.geometry(l[i])->length(), side12, 1e-5), "regular after resize");
    // the far ends of the filleted sides are still polygon vertices on the circumcircle
    const double Rv = 12 / std::cos(kPi / n);
    QVERIFY(near(sk.geometry(l[0])->start().length(), Rv, 1e-5));
    QVERIFY(near(sk.geometry(l[1])->end().length(), Rv, 1e-5));
    checkArc();
  }

  /// The GUI case: an unconstrained circumscribed hexagon as the Polygon tool makes it (4 DOF).
  void filletFreeHexagon() {
    Sketch sk;
    const int n = 6;
    const double R = 12;
    std::vector<Pt2> vs;
    for (int i = 0; i < n; ++i) vs.push_back(Pt2{20, 10} + Pt2::polar(R / std::cos(kPi / n), 2 * kPi * i / n + kPi / n - kPi / 2 + kPi / n));
    std::vector<int> l(n);
    for (int i = 0; i < n; ++i) l[i] = sk.addGeometry(Geometry::makeLine(vs[i], vs[(i + 1) % n]));
    Geometry circle = Geometry::makeCircle({20, 10}, R);
    circle.construction = true;
    int c = sk.addGeometry(circle);
    for (int i = 0; i < n; ++i) {
      sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l[i], PointPos::End}, .b = {l[(i + 1) % n], PointPos::Start}});
      if (i > 0) sk.addConstraint(Constraint{.type = ConstraintType::Equal, .a = {l[0]}, .b = {l[i]}});
      sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l[i]}, .b = {c}, .internal = true});
    }
    QVERIFY(sk.solve().ok());
    std::vector<Geometry> before;
    for (int id : l) before.push_back(*sk.geometry(id));
    for (int k = 0; k < n; ++k) {
      Sketch s2 = sk;
      int a = l[k], b = l[(k + 1) % n];
      auto arc = s2.fillet(a, s2.geometry(a)->midpoint(), b, s2.geometry(b)->midpoint(), 2);
      QVERIFY(arc);
      QVERIFY(s2.solve().ok());
      const Geometry* g = s2.geometry(*arc);
      QVERIFY2(near(g->sweep(), 2 * kPi / n, 1e-6), (std::to_string(k) + " sweep " + std::to_string(g->sweep())).c_str());
      QVERIFY2(near(g->radius(), 2, 1e-6), (std::to_string(k) + " r " + std::to_string(g->radius())).c_str());
      for (int i = 0; i < n; ++i) {
        if (i == k || i == (k + 1) % n) continue;
        QVERIFY(near(s2.geometry(l[i])->start(), before[i].start(), 1e-6));
      }
    }
  }
};

QTEST_GUILESS_MAIN(FilletDimensionsTest)
#include "test_fillet_dimensions.moc"
