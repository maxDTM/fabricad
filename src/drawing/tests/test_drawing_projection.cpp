// Tests for the GL-free geometry layer: view frames, HLR projection, edge conversion,
// hatching, clipping and section cuts.
#include "drawing/Geometry2D.h"
#include "drawing/Projection.h"
#include "drawing/SectionCut.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Circ.hxx>

#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
bool dirNear(const gp_Dir& d, double x, double y, double z) {
  return near(d.X(), x, 1e-9) && near(d.Y(), y, 1e-9) && near(d.Z(), z, 1e-9);
}
int countClass(const std::vector<Curve2D>& cs, LineClass cls) {
  return static_cast<int>(std::count_if(cs.begin(), cs.end(), [&](const Curve2D& c) { return c.cls == cls; }));
}
TopoDS_Shape boxWithHole() {
  TopoDS_Shape box = BRepPrimAPI_MakeBox(40, 30, 20).Shape();
  gp_Ax2 ax(gp_Pnt(20, 15, -1), gp_Dir(0, 0, 1));
  TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(ax, 5, 30).Shape();
  return BRepAlgoAPI_Cut(box, cyl).Shape();
}
}  // namespace

class ProjectionTest : public QObject {
  Q_OBJECT
 private slots:
  void standardFrames() {
    gp_Ax2 front = frameForOrientation(ViewOrientation::Front);
    QVERIFY(dirNear(front.Direction(), 0, -1, 0));
    QVERIFY(dirNear(front.XDirection(), 1, 0, 0));
    QVERIFY(dirNear(front.YDirection(), 0, 0, 1));
    gp_Ax2 right = frameForOrientation(ViewOrientation::Right);
    QVERIFY(dirNear(right.Direction(), 1, 0, 0));
    QVERIFY(dirNear(right.XDirection(), 0, 1, 0));
    QVERIFY(dirNear(right.YDirection(), 0, 0, 1));
    gp_Ax2 top = frameForOrientation(ViewOrientation::Top);
    QVERIFY(dirNear(top.Direction(), 0, 0, 1));
    QVERIFY(dirNear(top.XDirection(), 1, 0, 0));
    QVERIFY(dirNear(top.YDirection(), 0, 1, 0));
    gp_Ax2 iso = frameForOrientation(ViewOrientation::IsoTopRight);
    QVERIFY(iso.Direction().X() > 0 && iso.Direction().Y() < 0 && iso.Direction().Z() > 0);
    QVERIFY(iso.YDirection().Z() > 0.5);  // up stays up
  }

  void projectedFrames() {
    gp_Ax2 front = frameForOrientation(ViewOrientation::Front);
    // Third angle: view placed on the right shows the right side.
    gp_Ax2 r3 = projectedFrame(front, ProjectionDirection::Right, ProjectionStandard::ThirdAngle);
    QVERIFY(dirNear(r3.Direction(), 1, 0, 0));
    QVERIFY(dirNear(r3.YDirection(), 0, 0, 1));
    QVERIFY(dirNear(r3.XDirection(), 0, 1, 0));
    // First angle: the view on the right is the left side view.
    gp_Ax2 r1 = projectedFrame(front, ProjectionDirection::Right, ProjectionStandard::FirstAngle);
    QVERIFY(dirNear(r1.Direction(), -1, 0, 0));
    // Above in third angle = top view
    gp_Ax2 a3 = projectedFrame(front, ProjectionDirection::Above, ProjectionStandard::ThirdAngle);
    QVERIFY(dirNear(a3.Direction(), 0, 0, 1));
    QVERIFY(dirNear(a3.XDirection(), 1, 0, 0));
    // Below in first angle = top view as well (ISO puts top below front)
    gp_Ax2 b1 = projectedFrame(front, ProjectionDirection::Below, ProjectionStandard::FirstAngle);
    QVERIFY(dirNear(b1.Direction(), 0, 0, 1));
    // Diagonal -> isometric-ish direction with all components non-zero
    gp_Ax2 d = projectedFrame(front, ProjectionDirection::AboveRight, ProjectionStandard::ThirdAngle);
    QVERIFY(std::abs(d.Direction().X()) > 0.3 && std::abs(d.Direction().Y()) > 0.3 && std::abs(d.Direction().Z()) > 0.3);
    // frames stay right-handed
    gp_Vec x(d.XDirection()), y(d.YDirection()), z(d.Direction());
    QVERIFY(near(x.Crossed(y).Dot(z), 1.0, 1e-9));
  }

  void classifyDirections() {
    QCOMPARE(classifyDirection({50, 0}), ProjectionDirection::Right);
    QCOMPARE(classifyDirection({-50, 3}), ProjectionDirection::Left);
    QCOMPARE(classifyDirection({2, -50}), ProjectionDirection::Above);
    QCOMPARE(classifyDirection({2, 50}), ProjectionDirection::Below);
    QCOMPARE(classifyDirection({40, -45}), ProjectionDirection::AboveRight);
    QCOMPARE(classifyDirection({-40, 45}), ProjectionDirection::BelowLeft);
  }

  void boxFront() {
    ProjectionInput in;
    in.sources.push_back({7, BRepPrimAPI_MakeBox(10, 20, 30).Shape()});
    in.frame = frameForOrientation(ViewOrientation::Front);
    in.hiddenLines = true;
    ProjectionOutput out = projectShapes(in);
    QVERIFY2(out.ok, out.error.c_str());
    QCOMPARE(countClass(out.curves, LineClass::Visible), 4);
    QVERIFY(near(out.bbox.width(), 10.0, 1e-6));
    QVERIFY(near(out.bbox.height(), 30.0, 1e-6));
    // centred on the shape
    QVERIFY(near(out.bbox.center().x, 0.0, 1e-6));
    QVERIFY(near(out.bbox.center().y, 0.0, 1e-6));
    // every visible curve is associated with a 3D edge of body 7
    for (const auto& c : out.curves)
      if (c.cls == LineClass::Visible) {
        QCOMPARE(c.sourceBody, fabricad::core::ObjectId(7));
        QVERIFY(c.sourceEdge > 0);
      }
    QVERIFY(out.millis >= 0);
  }

  void boxIso() {
    ProjectionInput in;
    in.sources.push_back({1, BRepPrimAPI_MakeBox(10, 20, 30).Shape()});
    in.frame = frameForOrientation(ViewOrientation::IsoTopRight);
    in.hiddenLines = true;
    ProjectionOutput out = projectShapes(in);
    QVERIFY(out.ok);
    QCOMPARE(countClass(out.curves, LineClass::Visible), 9);
    QCOMPARE(countClass(out.curves, LineClass::Hidden), 3);
    std::vector<Vertex2D> verts = collectVertices(out.curves);
    QCOMPARE(static_cast<int>(verts.size()), 8);  // 7 visible corners + 1 hidden
  }

  void cylinderViews() {
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5.0, 12.0).Shape();
    ProjectionInput top;
    top.sources.push_back({1, cyl});
    top.frame = frameForOrientation(ViewOrientation::Top);
    ProjectionOutput t = projectShapes(top);
    QVERIFY(t.ok);
    int circles = 0;
    for (const auto& c : t.curves)
      if (c.isCircle()) {
        ++circles;
        QVERIFY(near(c.radius, 5.0, 1e-6));
      }
    QVERIFY(circles >= 1);
    ProjectionInput front;
    front.sources.push_back({1, cyl});
    front.frame = frameForOrientation(ViewOrientation::Front);
    ProjectionOutput f = projectShapes(front);
    QVERIFY(f.ok);
    QVERIFY(near(f.bbox.width(), 10.0, 1e-5));
    QVERIFY(near(f.bbox.height(), 12.0, 1e-5));
    // silhouettes are outlines (visible) and have no 3D edge behind them
    bool haveOutline = false;
    for (const auto& c : f.curves)
      if (c.cls == LineClass::Visible && c.type == CurveType::Segment && std::abs(c.a.x - c.b.x) < 1e-6)
        haveOutline = true;
    QVERIFY(haveOutline);
  }

  void tangentEdges() {
    // a box with a fillet-like smooth edge is hard to build quickly; use a cylinder's seam: with
    // tangentEdges on, the Rg1 compound is consulted without errors.
    ProjectionInput in;
    in.sources.push_back({1, BRepPrimAPI_MakeCylinder(5.0, 12.0).Shape()});
    in.frame = frameForOrientation(ViewOrientation::Front);
    in.tangentEdges = true;
    ProjectionOutput out = projectShapes(in);
    QVERIFY(out.ok);
    QVERIFY(countClass(out.curves, LineClass::Visible) >= 4);
  }

  void polyAlgorithm() {
    ProjectionInput in;
    in.sources.push_back({1, BRepPrimAPI_MakeBox(10, 20, 30).Shape()});
    in.frame = frameForOrientation(ViewOrientation::IsoTopRight);
    in.exact = false;
    in.hiddenLines = true;
    ProjectionOutput out = projectShapes(in);
    QVERIFY2(out.ok, out.error.c_str());
    QVERIFY(countClass(out.curves, LineClass::Visible) >= 9);
    QVERIFY(out.bbox.width() > 20.0);
  }

  void emptyInput() {
    ProjectionInput in;
    ProjectionOutput out = projectShapes(in);
    QVERIFY(!out.ok);
    QVERIFY(!out.error.empty());
  }

  void arcEdgeConversion() {
    gp_Circ circ(gp_Ax2(gp_Pnt(1, 2, 0), gp_Dir(0, 0, 1)), 4.0);
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(circ, 0.0, M_PI / 2).Edge();
    Curve2D c;
    QVERIFY(edgeToCurve(e, LineClass::Visible, 0.01, c));
    QCOMPARE(c.type, CurveType::Arc);
    QVERIFY(near(c.radius, 4.0));
    QVERIFY(near(c.center.x, 1.0) && near(c.center.y, 2.0));
    QVERIFY(near(c.startAngle, 0.0, 1e-6));
    QVERIFY(near(c.sweep, 90.0, 1e-6));
    QVERIFY(near(c.end().x, 1.0, 1e-6) && near(c.end().y, 6.0, 1e-6));
    TopoDS_Edge full = BRepBuilderAPI_MakeEdge(circ).Edge();
    QVERIFY(edgeToCurve(full, LineClass::Visible, 0.01, c));
    QVERIFY(c.isCircle());
    // a reversed-axis circle yields a clockwise arc
    gp_Circ circCw(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, -1)), 2.0);
    TopoDS_Edge ecw = BRepBuilderAPI_MakeEdge(circCw, 0.0, M_PI / 2).Edge();
    QVERIFY(edgeToCurve(ecw, LineClass::Visible, 0.01, c));
    QVERIFY(c.sweep < 0);
  }

  void curveMath() {
    Curve2D s = Curve2D::segment({0, 0}, {10, 0});
    QVERIFY(near(s.distanceTo({5, 3}), 3.0));
    QVERIFY(near(s.distanceTo({15, 0}), 5.0));
    Curve2D a = Curve2D::arc({0, 0}, 5, 0, 90);
    QVERIFY(near(a.distanceTo({10, 0}), 5.0));          // beyond the radius, on the arc's angular range
    QVERIFY(near(a.distanceTo({0, -5}), 5.0 * std::sqrt(2.0), 1e-6));  // outside range: distance to start
    BBox2 b = a.bbox();
    QVERIFY(near(b.min.x, 0) && near(b.max.x, 5) && near(b.min.y, 0) && near(b.max.y, 5));
    Curve2D t = a.transformed({100, 100}, 2.0, true);
    QVERIFY(near(t.radius, 10.0));
    QVERIFY(near(t.start().x, 110) && near(t.start().y, 100));
    QVERIFY(near(t.end().x, 100) && near(t.end().y, 90));  // Y flipped: end goes up (smaller y)
    QVERIFY(angleInArc(45, 0, 90));
    QVERIFY(!angleInArc(135, 0, 90));
    QVERIFY(angleInArc(315, 0, -90));
    // JSON round trip
    Curve2D p = Curve2D::polyline({{0, 0}, {1, 1}, {2, 0}}, LineClass::Hidden);
    p.sourceBody = 42;
    p.sourceEdge = 3;
    Curve2D p2 = Curve2D::fromJson(p.toJson());
    QCOMPARE(p2.type, CurveType::Polyline);
    QCOMPARE(p2.pts.size(), size_t(3));
    QCOMPARE(p2.cls, LineClass::Hidden);
    QCOMPARE(p2.sourceEdge, 3);
    QCOMPARE(p2.sourceBody, fabricad::core::ObjectId(42));
  }

  void hatching() {
    std::vector<std::vector<Point2>> loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    auto h = hatchLoops(loops, 0.0, 2.0);
    QCOMPARE(static_cast<int>(h.size()), 5);
    for (const auto& c : h) {
      QCOMPARE(c.cls, LineClass::Hatch);
      QVERIFY(near(c.length(), 10.0, 1e-9));
    }
    // with a hole in the middle: lines crossing the hole are split in two
    loops.push_back({{4, 4}, {6, 4}, {6, 6}, {4, 6}});
    auto h2 = hatchLoops(loops, 0.0, 2.0);
    QVERIFY(h2.size() == 6);
    // 45 degrees covers the square too
    auto h3 = hatchLoops({loops[0]}, 45.0, 3.0);
    QVERIFY(h3.size() >= 3);
    for (const auto& c : h3) {
      Point2 d = (c.b - c.a).normalized();
      QVERIFY(near(std::abs(d.x), std::abs(d.y), 1e-9));
    }
  }

  void circleClipping() {
    Curve2D seg = Curve2D::segment({-20, 0}, {20, 0});
    auto parts = clipToCircle(seg, {0, 0}, 5);
    QCOMPARE(parts.size(), size_t(1));
    QVERIFY(near(parts[0].a.x, -5) && near(parts[0].b.x, 5));
    QVERIFY(clipToCircle(Curve2D::segment({10, 10}, {20, 10}), {0, 0}, 5).empty());
    // arc entirely inside stays an arc; arc crossing becomes polylines
    Curve2D inner = Curve2D::circle({0, 0}, 2);
    auto p1 = clipToCircle(inner, {0, 0}, 5);
    QCOMPARE(p1.size(), size_t(1));
    QVERIFY(p1[0].isCircle());
    Curve2D big = Curve2D::circle({4, 0}, 4);
    auto p2 = clipToCircle(big, {0, 0}, 5);
    QVERIFY(!p2.empty());
    for (const auto& c : p2)
      for (const auto& q : c.sample())
        QVERIFY(q.length() <= 5.0 + 1e-6);
  }

  void sectionOfBoxWithHole() {
    TopoDS_Shape shape = boxWithHole();
    gp_Ax2 front = frameForOrientation(ViewOrientation::Front);
    // Front view of the shape: cut vertically through the hole centre (x = 20 -> view x = 0),
    // looking to the left (arrow -X): a side section.
    Point2 p1{0, -20}, p2{0, 20};
    gp_Ax2 fr = sectionFrame(front, p1, p2, {-1, 0});
    // viewer direction is -arrow = +X; up stays world Z
    QVERIFY(dirNear(fr.Direction(), 1, 0, 0));
    QVERIFY(dirNear(fr.YDirection(), 0, 0, 1));
    // move the frame origin to the shape centre (the model does this with the parent's origin)
    gp_Ax2 at(gp_Pnt(20, 15, 10), fr.Direction(), fr.XDirection());
    SectionInput in;
    in.sources.push_back({1, shape});
    in.frame = at;
    in.hatchSpacing = 3.0;
    SectionOutput out = computeSection(in);
    QVERIFY2(out.ok, out.error.c_str());
    QCOMPARE(out.cutSources.size(), size_t(1));
    // the cut plane x=20 passes through the hole: two separate rectangular regions (30-10)/2 = 10 wide
    QCOMPARE(out.loops.size(), size_t(2));
    QVERIFY(!out.hatch.empty());
    QVERIFY(out.outlines.size() == 8);
    // Project the remaining half along the section frame; hatch is inside the bbox
    ProjectionInput proj;
    proj.sources = out.cutSources;
    proj.frame = at;
    proj.useFrameOrigin = true;
    proj.extraCurves = out.hatch;
    ProjectionOutput po = projectShapes(proj);
    QVERIFY(po.ok);
    QVERIFY(near(po.bbox.width(), 30.0, 1e-4));
    QVERIFY(near(po.bbox.height(), 20.0, 1e-4));
    for (const auto& h : out.hatch) QVERIFY(po.bbox.contains(h.a, 1e-6) && po.bbox.contains(h.b, 1e-6));
  }

  void sectionFrameHorizontalCut() {
    gp_Ax2 front = frameForOrientation(ViewOrientation::Front);
    // horizontal cut line, arrows pointing up (+Y view) -> viewer looks up: bottom-like view
    gp_Ax2 fr = sectionFrame(front, {-10, 0}, {10, 0}, {0, 1});
    QVERIFY(dirNear(fr.Direction(), 0, 0, -1));
    QVERIFY(dirNear(fr.XDirection(), 1, 0, 0));
    gp_Vec x(fr.XDirection()), y(fr.YDirection()), z(fr.Direction());
    QVERIFY(near(x.Crossed(y).Dot(z), 1.0, 1e-9));
  }

  void scaleParsing() {
    double v = 0;
    QVERIFY(parseScale("1:2", v) && near(v, 0.5));
    QVERIFY(parseScale("2 : 1", v) && near(v, 2.0));
    QVERIFY(parseScale("0.25", v) && near(v, 0.25));
    QVERIFY(!parseScale("abc", v));
    QVERIFY(!parseScale("1:0", v));
    QCOMPARE(scaleLabel(0.5), std::string("1:2"));
    QCOMPARE(scaleLabel(1.0), std::string("1:1"));
    QCOMPARE(scaleLabel(2.0), std::string("2:1"));
  }
};

QTEST_GUILESS_MAIN(ProjectionTest)
#include "test_drawing_projection.moc"
