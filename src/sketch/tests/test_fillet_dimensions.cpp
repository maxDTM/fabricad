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
};

QTEST_GUILESS_MAIN(FilletDimensionsTest)
#include "test_fillet_dimensions.moc"
