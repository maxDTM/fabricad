// Sketch curve tests: ellipses, elliptical arcs, control- and fit-point splines and text —
// geometry queries, JSON, solver constraints (coincident / tangent / point-on-curve), profile
// detection and extrusion of text profiles. Every shape is fully specified by literal
// coordinates so the expected values are exact and the run is repeatable.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/Sketch.h"
#include "sketch/SketchCurves.h"
#include "sketch/SketchProfiles.h"
#include "sketch/SketchText.h"
#include "support/SketchTestHelpers.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <QtTest/QtTest>

using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

namespace {

const gp_Ax3 kXY(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));

double ellipseResidual(const EllipseFrame& f, Pt2 q) {
  Pt2 v = q - f.center;
  double x = v.dot(f.major), y = v.dot(f.minor);
  return (x * x) / (f.a * f.a) + (y * y) / (f.b * f.b) - 1.0;
}

double polygonArea(const std::vector<Pt2>& pts) {
  double a = 0;
  for (size_t i = 0; i < pts.size(); ++i) a += pts[i].cross(pts[(i + 1) % pts.size()]);
  return 0.5 * a;
}

double totalArea(const std::vector<ProfileRegion>& rs) {
  double a = 0;
  for (const auto& r : rs) a += r.area;
  return a;
}

}  // namespace

class CurvesTest : public QObject {
  Q_OBJECT
 private slots:
  // --- ellipse ------------------------------------------------------------------------------
  void ellipseGeometry() {
    // centre (10, 5), a = 20 along 30 degrees, b = 10
    Geometry e = Geometry::makeEllipse({10, 5}, 20, 10, kPi / 6);
    QCOMPARE(e.type, GeomType::Ellipse);
    QVERIFY(near(e.majorRadius(), 20.0, 1e-9));
    QVERIFY(near(e.minorRadius(), 10.0, 1e-9));
    EllipseFrame f = e.ellipse();
    QVERIFY(near(f.major, Pt2::polar(1, kPi / 6), 1e-12));
    QVERIFY(near(e.pointAt(0), Pt2{10, 5} + Pt2::polar(20, kPi / 6), 1e-9));
    // analytic bbox half extents sqrt(a^2 cos^2 + b^2 sin^2)
    auto [mn, mx] = e.bbox();
    double hx = std::sqrt(400 * 0.75 + 100 * 0.25), hy = std::sqrt(400 * 0.25 + 100 * 0.75);
    QVERIFY(near(mn, {10 - hx, 5 - hy}, 1e-9) && near(mx, {10 + hx, 5 + hy}, 1e-9));
    // closest point lies on the ellipse and is the true foot
    Pt2 q{40, 30};
    Pt2 c = e.closestPoint(q);
    QVERIFY(std::abs(ellipseResidual(f, c)) < 1e-9);
    QVERIFY(std::abs((q - c).normalized().dot(e.tangentAt(c))) < 1e-7);
    // a/b swap: a < b rotates the frame by 90 degrees
    Geometry s = Geometry::makeEllipse({0, 0}, 5, 8, 0);
    QVERIFY(near(s.majorRadius(), 8.0, 1e-9));
    QVERIFY(near(s.ellipse().major, {0, 1}, 1e-9));
    // JSON round trip
    Geometry r = Geometry::fromJson(e.toJson());
    QCOMPARE(r.type, GeomType::Ellipse);
    QVERIFY(near(r.pointAt(1.0), e.pointAt(1.0), 1e-12));
    // perimeter (Ramanujan II) of a=20, b=10
    double h = std::pow(10.0 / 30.0, 2);
    double ram = kPi * 30 * (1 + 3 * h / (10 + std::sqrt(4 - 3 * h)));
    QVERIFY(std::abs(e.length() - ram) < 0.01);
  }

  void ellipseArcGeometry() {
    // quarter of the axis-aligned ellipse a = 20, b = 10 centred at the origin
    Geometry a = Geometry::makeEllipseArc({0, 0}, 20, 10, 0, 0, kPi / 2);
    QCOMPARE(a.type, GeomType::EllipseArc);
    QVERIFY(near(a.start(), {20, 0}, 1e-9));
    QVERIFY(near(a.end(), {0, 10}, 1e-9));
    QVERIFY(near(a.sweep(), kPi / 2, 1e-12));
    auto [mn, mx] = a.bbox();
    QVERIFY(near(mn, {0, 0}, 1e-9) && near(mx, {20, 10}, 1e-9));
    // closest point outside the range snaps to an end
    QVERIFY(near(a.closestPoint({-30, -5}), {0, 10}, 1e-9) || near(a.closestPoint({-30, -5}), {20, 0}, 1e-9));
    // mirror about the Y axis: start/end swap and stay on the mirrored ellipse
    Geometry m = a;
    m.mirror({0, 0}, {0, 1});
    QVERIFY(near(m.start(), {0, 10}, 1e-9));
    QVERIFY(near(m.end(), {-20, 0}, 1e-9));
    // rotation by 90 degrees about the origin
    Geometry r = a;
    r.transform({0, 0}, kPi / 2, 1.0);
    QVERIFY(near(r.start(), {0, 20}, 1e-9));
    QVERIFY(near(r.end(), {-10, 0}, 1e-9));
    // scaling by 2 doubles both radii
    Geometry s = a;
    s.transform({0, 0}, 0, 2.0);
    QVERIFY(near(s.majorRadius(), 40.0, 1e-9) && near(s.minorRadius(), 20.0, 1e-9));
    Geometry j = Geometry::fromJson(a.toJson());
    QVERIFY(near(j.start(), a.start(), 1e-12) && near(j.end(), a.end(), 1e-12));
    // line / ellipse intersections are exact: x = 10 crosses at y = 10*sqrt(1 - 1/4)
    Geometry line = Geometry::makeLine({10, -20}, {10, 20});
    auto xs = intersectCurves(Geometry::makeEllipse({0, 0}, 20, 10, 0), line);
    QCOMPARE(xs.size(), size_t(2));
    for (const Pt2& p : xs) QVERIFY(near(std::abs(p.y), 10 * std::sqrt(0.75), 1e-8));
    QCOMPARE(intersectCurves(a, line).size(), size_t(1));  // the quarter arc only above y = 0
  }

  // --- splines -------------------------------------------------------------------------------
  void controlSpline() {
    std::vector<Pt2> poles{{0, 0}, {10, 20}, {30, 20}, {40, 0}};
    Geometry s = Geometry::makeControlSpline(poles, 3);
    BSpline2 b = s.bspline();
    QVERIFY(b.isValid());
    QCOMPARE(b.degree, 3);
    // single-span cubic = Bezier: P(0.5) = (P0 + 3P1 + 3P2 + P3) / 8
    QVERIFY(near(b.value(0.5), Pt2{(0 + 30 + 90 + 40) / 8.0, (0 + 60 + 60 + 0) / 8.0}, 1e-12));
    QVERIFY(near(s.start(), {0, 0}) && near(s.end(), {40, 0}));
    // end tangent along the control polygon
    QVERIFY(near(b.derivative(0).normalized(), Pt2{10, 20}.normalized(), 1e-12));
    // two poles -> degree 1 (a line)
    Geometry l = Geometry::makeControlSpline({{0, 0}, {10, 0}}, 3);
    QCOMPARE(l.bspline().degree, 1);
    // vertices are referencable points
    auto refs = s.pointRefs();
    int vertexRefs = 0;
    for (const GeoRef& r : refs) vertexRefs += r.pos == PointPos::Vertex;
    QCOMPARE(vertexRefs, 2);  // interior poles; ends are Start / End
    Geometry j = Geometry::fromJson(s.toJson());
    QCOMPARE(j.vertexCount(), 4);
    QCOMPARE(j.degree, 3);
  }

  void fitSpline() {
    std::vector<Pt2> fit{{0, 0}, {10, 5}, {20, 0}, {30, 5}, {40, 0}};
    Geometry s = Geometry::makeFitSpline(fit);
    BSpline2 b = s.bspline();
    QVERIFY(b.isValid());
    QCOMPARE(b.degree, 3);
    QCOMPARE(b.poles.size(), fit.size());
    // the curve passes exactly through every fit point at its parameter
    for (size_t i = 0; i < fit.size(); ++i) QVERIFY(near(b.value(s.knots[i]), fit[i], 1e-9));
    for (const Pt2& p : fit) QVERIFY(s.distanceTo(p) < 1e-7);
    // parameters are chord-length, fixed at creation, persisted
    QVERIFY(near(s.knots.front(), 0.0) && near(s.knots.back(), 1.0));
    Geometry j = Geometry::fromJson(s.toJson());
    QCOMPARE(j.knots.size(), fit.size());
    QVERIFY(near(j.pointAt(0.37), s.pointAt(0.37), 1e-12));
    // three points -> quadratic, two -> line
    QCOMPARE(Geometry::makeFitSpline({{0, 0}, {5, 5}, {10, 0}}).bspline().degree, 2);
    QCOMPARE(Geometry::makeFitSpline({{0, 0}, {5, 5}}).bspline().degree, 1);
    // a horizontal line crosses the wave at its four zero-level crossings of y = 2.5
    Geometry h = Geometry::makeLine({-5, 2.5}, {45, 2.5});
    auto xs = intersectCurves(s, h);
    QCOMPARE(xs.size(), size_t(4));
    for (const Pt2& p : xs) QVERIFY(near(p.y, 2.5, 1e-7) && s.distanceTo(p) < 1e-7);
  }

  // --- solver ---------------------------------------------------------------------------------
  void solverEllipseConstraints() {
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipse({0, 0}, 20, 10, 0));
    QCOMPARE(sk.solve().dofs, 5);  // centre (2) + focus (2) + minor radius
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {e}});
    // a horizontal line placed at y = 12 becomes tangent at y = 10
    int l = sk.addGeometry(Geometry::makeLine({-30, 12}, {30, 12}));
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l}});
    sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l}, .b = {e}});
    // a point pulled onto the ellipse
    int p = sk.addGeometry(Geometry::makePoint({25, 3}));
    sk.addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {p, PointPos::Center}, .b = {e}});
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(near(sk.geometry(l)->start().y, 10.0, 1e-7));
    QVERIFY(std::abs(ellipseResidual(sk.geometry(e)->ellipse(), sk.geometry(p)->center())) < 1e-8);
    // an elliptical arc whose start is coincident with a fixed point, centre concentric with the ellipse
    int arc = sk.addGeometry(Geometry::makeEllipseArc({1, 1}, 8, 4, 0.2, 0, kPi));
    int anchor = sk.addGeometry(Geometry::makePoint({6, 0}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {anchor, PointPos::Center}});
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {arc, PointPos::Start}, .b = {anchor, PointPos::Center}});
    sk.addConstraint(Constraint{.type = ConstraintType::Concentric, .a = {arc}, .b = {e}});
    r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    const Geometry* ga = sk.geometry(arc);
    QVERIFY(near(ga->start(), {6, 0}, 1e-7));
    QVERIFY(near(ga->center(), {0, 0}, 1e-7));
    // start/end points stay on the arc's own ellipse
    QVERIFY(std::abs(ellipseResidual(ga->ellipse(), ga->end())) < 1e-8);
    // equal ellipses
    int e2 = sk.addGeometry(Geometry::makeEllipse({60, 0}, 15, 6, 0.4));
    sk.addConstraint(Constraint{.type = ConstraintType::Equal, .a = {e2}, .b = {e}});
    r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(near(sk.geometry(e2)->majorRadius(), 20.0, 1e-7));
    QVERIFY(near(sk.geometry(e2)->minorRadius(), 10.0, 1e-7));
  }

  void solverSplineConstraints() {
    Sketch sk;
    // fixed horizontal line ending at the origin
    int l = sk.addGeometry(Geometry::makeLine({-20, 0}, {0, 0}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {l}});
    int s = sk.addGeometry(Geometry::makeFitSpline({{0.5, 0.5}, {10, 8}, {20, 2}, {30, 6}}));
    QCOMPARE(sk.solve().dofs, 8);  // the fit points; the poles are determined by them
    // the spline starts at the line end and is tangent to it there
    sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l, PointPos::End}, .b = {s, PointPos::Start}});
    // the last fit point fixed, the second one on a vertical construction line x = 10
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {s, PointPos::End}});
    int v = sk.addGeometry(Geometry::makeLine({12, -50}, {12, 50}));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {v}});
    sk.addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {s, PointPos::Vertex, 1}, .b = {v}});
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    const Geometry* g = sk.geometry(s);
    QVERIFY(near(g->start(), {0, 0}, 1e-7));
    QVERIFY(near(g->vertex(1).x, 12.0, 1e-7));
    QVERIFY(near(g->end(), {30, 6}, 1e-9));
    BSpline2 b = g->bspline();
    QVERIFY(std::abs(b.derivative(b.first()).normalized().cross({1, 0})) < 1e-6);
    QVERIFY(b.derivative(b.first()).x > 0);  // continues in the line's direction
    // the solved curve still interpolates its fit points
    for (int i = 0; i < g->vertexCount(); ++i) QVERIFY(g->distanceTo(g->vertex(i)) < 1e-7);
    // a point constrained onto the spline
    int p = sk.addGeometry(Geometry::makePoint({15, 12}));
    sk.addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {p, PointPos::Center}, .b = {s}});
    r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(sk.geometry(s)->distanceTo(sk.geometry(p)->center()) < 1e-6);

    // control-point spline: start coincident with an arc end, tangent there
    Sketch sk2;
    int arc = sk2.addGeometry(Geometry::makeArc({0, 0}, 10, 0, kPi / 2));  // ends at (0, 10)
    sk2.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {arc}});
    int cs = sk2.addGeometry(Geometry::makeControlSpline({{1, 11}, {-8, 14}, {-15, 6}, {-20, 0}}, 3));
    sk2.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {arc, PointPos::End}, .b = {cs, PointPos::Start}});
    r = sk2.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    const Geometry* gc = sk2.geometry(cs);
    QVERIFY(near(gc->start(), {0, 10}, 1e-7));
    // the arc tangent at (0, 10) is horizontal: first control leg is horizontal
    QVERIFY(near(gc->vertex(1).y, 10.0, 1e-6));
    // interior pole coincident with a point
    int pt = sk2.addGeometry(Geometry::makePoint({-14, 4}));
    sk2.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {pt, PointPos::Center}});
    sk2.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {cs, PointPos::Vertex, 2}, .b = {pt, PointPos::Center}});
    r = sk2.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    QVERIFY(near(sk2.geometry(cs)->vertex(2), {-14, 4}, 1e-7));
    // JSON keeps the indexed vertex reference
    Sketch copy;
    copy.fromJson(sk2.toJson());
    bool found = false;
    for (const Constraint& c : copy.constraints())
      if (c.type == ConstraintType::Coincident && c.a.pos == PointPos::Vertex && c.a.index == 2) found = true;
    QVERIFY(found);
  }

  void solverTangentEllipseCircle() {
    // circle tangent to a fixed ellipse anywhere (auxiliary contact point)
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipse({0, 0}, 20, 10, 0));
    sk.addConstraint(Constraint{.type = ConstraintType::Fix, .a = {e}});
    int c = sk.addGeometry(Geometry::makeCircle({0, 17}, 5));
    sk.addConstraint(Constraint{.type = ConstraintType::Radius, .a = {c}, .value = 5});
    sk.addConstraint(Constraint{.type = ConstraintType::DistanceX, .a = {c, PointPos::Center}, .value = 0});
    sk.addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {c}, .b = {e}});
    SolveResult r = sk.solve();
    QVERIFY2(r.ok(), r.message.c_str());
    // touching the top vertex (0, 10) from outside: centre at (0, 15)
    QVERIFY(near(sk.geometry(c)->center(), {0, 15}, 1e-6));
  }

  // --- trimming ellipses ---------------------------------------------------------------------
  void trimEllipse() {
    Sketch sk;
    int e = sk.addGeometry(Geometry::makeEllipse({0, 0}, 20, 10, 0));
    sk.addGeometry(Geometry::makeLine({0, -20}, {0, 20}));
    auto pieces = sk.trim(e, {20, 0});  // removes the right half
    QCOMPARE(pieces.size(), size_t(1));
    const Geometry* g = sk.geometry(e);
    QCOMPARE(g->type, GeomType::EllipseArc);
    QVERIFY(near(g->sweep(), kPi, 1e-9));
    QVERIFY(g->containsAngle(kPi));
    QVERIFY(near(g->pointAt(kPi), {-20, 0}, 1e-9));
  }

  // --- profiles --------------------------------------------------------------------------------
  void profilesOfNewCurves() {
    // full ellipse: area pi a b
    Sketch a;
    a.addGeometry(Geometry::makeEllipse({0, 0}, 20, 10, 0.3));
    auto ra = computeProfiles(a, kXY);
    QCOMPARE(ra.size(), size_t(1));
    QVERIFY(std::abs(ra[0].area - kPi * 200) < 1e-4);
    // half ellipse closed by its major axis: pi a b / 2
    Sketch b;
    int arc = b.addGeometry(Geometry::makeEllipseArc({0, 0}, 20, 10, 0, 0, kPi));
    b.addGeometry(Geometry::makeLine({-20, 0}, {20, 0}));
    auto rb = computeProfiles(b, kXY);
    QCOMPARE(rb.size(), size_t(1));
    QVERIFY(std::abs(rb[0].area - kPi * 100) < 1e-4);
    QVERIFY(std::count(rb[0].curveIds.begin(), rb[0].curveIds.end(), arc));
    // control-point spline closed by a line (a single cubic Bezier span)
    Sketch c;
    Geometry sp = Geometry::makeControlSpline({{0, 0}, {10, 20}, {30, 20}, {40, 0}}, 3);
    c.addGeometry(sp);
    c.addGeometry(Geometry::makeLine({40, 0}, {0, 0}));
    auto rc = computeProfiles(c, kXY);
    QCOMPARE(rc.size(), size_t(1));
    std::vector<Pt2> poly = sp.bspline().sample(2000);
    double expect = std::abs(polygonArea(poly));
    QVERIFY(std::abs(rc[0].area - expect) < 1e-3);
    // exact: y = 60 t (1 - t), dx/dt = 30 + 60 t - 60 t^2 -> integral of y dx = 300 + 120
    QVERIFY(std::abs(rc[0].area - 420.0) < 1e-4);
    // fit-point spline closed by a line (region count and area within the polyline estimate)
    Sketch d;
    Geometry fs = Geometry::makeFitSpline({{0, 0}, {10, 8}, {20, 10}, {30, 8}, {40, 0}});
    d.addGeometry(fs);
    d.addGeometry(Geometry::makeLine({40, 0}, {0, 0}));
    auto rd = computeProfiles(d, kXY);
    QCOMPARE(rd.size(), size_t(1));
    QVERIFY(std::abs(rd[0].area - std::abs(polygonArea(fs.bspline().sample(2000)))) < 1e-3);
  }

  /// UX-26: construction curves never bound or split profiles.
  void profilesIgnoreConstruction() {
    Sketch sk;
    auto addC = [&](Geometry g) {
      g.construction = true;
      return sk.addGeometry(g);
    };
    // rectangle 20 x 10 with a construction diagonal and a construction circle crossing it
    sk.addGeometry(Geometry::makeLine({0, 0}, {20, 0}));
    sk.addGeometry(Geometry::makeLine({20, 0}, {20, 10}));
    sk.addGeometry(Geometry::makeLine({20, 10}, {0, 10}));
    sk.addGeometry(Geometry::makeLine({0, 10}, {0, 0}));
    addC(Geometry::makeLine({0, 0}, {20, 10}));
    addC(Geometry::makeCircle({10, 5}, 8));
    auto r1 = computeProfiles(sk, kXY);
    QCOMPARE(r1.size(), size_t(1));
    QVERIFY(std::abs(r1[0].area - 200.0) < 1e-6);
    // a pentagon with its construction circumcircle, apart from the rectangle
    const int n = 5;
    const double R = 10;
    std::vector<Pt2> vs;
    for (int i = 0; i < n; ++i) vs.push_back(Pt2{60, 5} + Pt2::polar(R, kPi / 2 + 2 * kPi * i / n));
    for (int i = 0; i < n; ++i) sk.addGeometry(Geometry::makeLine(vs[i], vs[(i + 1) % n]));
    addC(Geometry::makeCircle({60, 5}, R));
    auto r2 = computeProfiles(sk, kXY);
    QCOMPARE(r2.size(), size_t(2));
    const double pentagon = 0.5 * n * R * R * std::sin(2 * kPi / n);
    bool found = false;
    for (const auto& r : r2) found = found || std::abs(r.area - pentagon) < 1e-6;
    QVERIFY(found);
  }

  // --- text ---------------------------------------------------------------------------------------
  void textOutline() {
    Geometry h = Geometry::makeText({5, 7}, "H", 10.0);
    auto contours = textContours(h);
    QCOMPARE(contours.size(), size_t(1));  // "H" is one closed contour in every outline font
    auto [mn, mx] = h.bbox();
    QVERIFY2(near(mx.y - mn.y, 10.0, 1e-6), "text height is the cap height of H");
    QVERIFY(near(mn.y, 7.0, 1e-6));        // sits on the baseline
    QVERIFY(mn.x >= 5.0 - 1e-9);           // starts at the anchor (left side bearing >= 0)
    QVERIFY(textWidth(h) > 0);
    // contours are closed
    for (const auto& c : contours) QVERIFY(near(c.front().p0, c.back().p3, 1e-9));
    // "O" has an outer and an inner contour
    QCOMPARE(textContours(Geometry::makeText({0, 0}, "O", 10.0)).size(), size_t(2));
    // rotation by 90 degrees about the anchor: the H now extends 10 mm to the left in x
    Geometry r = h;
    r.transform({5, 7}, kPi / 2, 1.0);
    auto [rmn, rmx] = r.bbox();
    QVERIFY(near(rmx.x - rmn.x, 10.0, 1e-6));
    QVERIFY(near(rmx.x, 5.0, 1e-6));
    // scaling doubles the height
    Geometry s = h;
    s.transform({5, 7}, 0, 2.0);
    auto [smn, smx] = s.bbox();
    QVERIFY(near(smx.y - smn.y, 20.0, 1e-6));
    // mirroring about a vertical line keeps the height and flips the side
    Geometry m = h;
    m.mirror({0, 0}, {0, 1});
    auto [mmn, mmx] = m.bbox();
    QVERIFY(near(mmx.y - mmn.y, 10.0, 1e-6));
    QVERIFY(mmx.x <= -5.0 + 1e-6);
    // JSON
    Geometry j = Geometry::fromJson(Geometry::makeText({1, 2}, "Fab", 6.0, 0.25, "DejaVu Sans").toJson());
    QCOMPARE(j.type, GeomType::Text);
    QCOMPARE(j.text, std::string("Fab"));
    QCOMPARE(j.font, std::string("DejaVu Sans"));
    QVERIFY(near(j.p[2], 6.0) && near(j.p[3], 0.25));
  }

  void textProfilesExtrude() {
    // "O" at 10 mm: the ring and the counter are two regions; the ring extrudes to a valid solid
    Sketch sk;
    int t = sk.addGeometry(Geometry::makeText({0, 0}, "O", 10.0));
    auto regions = computeProfiles(sk, kXY);
    QCOMPARE(regions.size(), size_t(2));
    for (const auto& r : regions) QCOMPARE(r.curveIds, std::vector<int>{t});
    // the ring is the region whose face has an inner wire
    const ProfileRegion* ring = nullptr;
    for (const auto& r : regions) {
      int wires = 0;
      for (TopExp_Explorer ex(r.face, TopAbs_WIRE); ex.More(); ex.Next()) ++wires;
      if (wires == 2) ring = &r;
    }
    QVERIFY(ring);
    TopoDS_Shape prism = BRepPrimAPI_MakePrism(ring->face, gp_Vec(0, 0, 5)).Shape();
    QVERIFY(BRepCheck_Analyzer(prism).IsValid());
    GProp_GProps props;
    BRepGProp::VolumeProperties(prism, props);
    QVERIFY(std::abs(props.Mass() - ring->area * 5.0) < 1e-6 * ring->area);
    // "Hi": two letters -> three regions? H is one, i is two (stem and dot)
    Sketch two;
    two.addGeometry(Geometry::makeText({0, 0}, "Hi", 10.0));
    QCOMPARE(computeProfiles(two, kXY).size(), size_t(3));
    // text drawn as construction is not a profile
    sk.geometry(t)->construction = true;
    QVERIFY(computeProfiles(sk, kXY).empty());
    (void)totalArea;
  }
};

QTEST_MAIN(CurvesTest)
#include "test_curves.moc"
