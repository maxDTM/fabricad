// Tool tests for the round-2 sketch commands: ellipse, elliptical arc, fit / control point
// spline, text, move/copy, rotate, scale, rectangular / circular pattern and chamfer, driven
// headlessly through the ToolManager (NullViewerContext / NullAppHost; mouse events carry
// sketch-plane points, typed values go through the command line). All picks are at literal
// coordinates away from the 2 mm snap radius of other geometry unless a snap is intended.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "sketch/SketchCurves.h"
#include "sketch/SketchFeature.h"
#include "sketch/SketchModule.h"
#include "sketch/SketchSession.h"
#include "support/SketchTestHelpers.h"

#include <QApplication>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

namespace {

struct Rig {
  core::ToolRegistry registry;
  core::Document doc;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager mgr{registry};

  Rig() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    sketch::registerModule(ctx);
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);
    viewer.setPixelSize(0.25);  // 8 px snap tolerance = 2 mm
    mgr.run("sketch.create", {"XY"});
    pump();
  }
  ~Rig() { SketchSession::end(); }

  static void pump() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  SketchSession* session() { return SketchSession::current(); }
  Sketch& sk() { return *session()->sketch(); }

  core::ViewerMouseEvent at(double x, double y, core::MouseButton b = core::MouseButton::None) {
    core::ViewerMouseEvent e;
    e.button = b;
    e.hasPlanePoint = true;
    e.planePoint = session()->toWorld({x, y});
    e.x = static_cast<int>(x);
    e.y = static_cast<int>(-y);
    return e;
  }
  void move(double x, double y) { mgr.mouseMove(at(x, y)); }
  void click(double x, double y) {
    mgr.mouseMove(at(x, y));
    mgr.mousePress(at(x, y, core::MouseButton::Left));
    mgr.mouseRelease(at(x, y, core::MouseButton::Left));
  }
  void dblclick(double x, double y) {
    auto e = at(x, y, core::MouseButton::Left);
    e.clickCount = 2;
    mgr.mouseDoubleClick(e);
  }
  void key(int k) {
    core::ViewerKeyEvent e;
    e.key = k;
    mgr.keyPress(e);
  }
  void type(const std::string& s) { QVERIFY2(mgr.runCommandLine(s), s.c_str()); }
  /// Ids of geometries of a type.
  std::vector<int> ofType(GeomType t) {
    std::vector<int> out;
    for (const Geometry& g : sk().geometries())
      if (g.type == t) out.push_back(g.id);
    return out;
  }
  void select(const std::vector<int>& ids) {
    doc.selection().clear();
    for (int id : ids) doc.selection().add(session()->toShapeRef(GeoRef{id}));
  }
};

}  // namespace

class CurveToolsTest : public QObject {
  Q_OBJECT
 private slots:
  void registration() {
    Rig r;
    for (const char* id : {"sketch.ellipse", "sketch.ellipse-arc", "sketch.spline-fit", "sketch.spline-control", "sketch.text",
                           "sketch.move", "sketch.rotate", "sketch.scale", "sketch.pattern-rectangular",
                           "sketch.pattern-circular", "sketch.chamfer"}) {
      QVERIFY2(r.registry.has(id), id);
      QCOMPARE(r.registry.tool(id)->info().tab, std::string("SKETCH"));
      QVERIFY(r.registry.tool(id)->isEnabled(r.mgr.makeContext()));
    }
    QCOMPARE(r.registry.byAlias("ELLIPSE")->info().id, std::string("sketch.ellipse"));
    QCOMPARE(r.registry.byAlias("SPLINE")->info().id, std::string("sketch.spline-fit"));
  }

  void ellipseTools() {
    Rig r;
    r.mgr.run("sketch.ellipse");
    r.click(0, 0);
    r.click(20, 0);
    r.move(5, 9);
    QVERIFY(!r.viewer.previews().empty());
    r.click(0, 10);
    auto el = r.ofType(GeomType::Ellipse);
    QCOMPARE(el.size(), size_t(1));
    const Geometry* g = r.sk().geometry(el[0]);
    QVERIFY(near(g->center(), {0, 0}) && near(g->majorRadius(), 20) && near(g->minorRadius(), 10));
    QVERIFY(near(g->ellipse().major, {1, 0}));
    // typed radii: centre, "30", "5"
    r.click(60, 40);
    r.type("30");
    r.type("5");
    el = r.ofType(GeomType::Ellipse);
    QCOMPARE(el.size(), size_t(2));
    g = r.sk().geometry(el[1]);
    QVERIFY(near(g->center(), {60, 40}) && near(g->majorRadius(), 30) && near(g->minorRadius(), 5));
    // undo removes only the last one
    r.doc.undoStack().undo();
    QCOMPARE(r.ofType(GeomType::Ellipse).size(), size_t(1));
    r.key(Qt::Key_Escape);

    // elliptical arc: centre, major end, start (0, 10) -> b = 10, end (-20, 0)
    r.mgr.run("sketch.ellipse-arc");
    r.click(100, 0);
    r.click(120, 0);
    r.click(100, 10);
    r.click(80, 0);
    auto arcs = r.ofType(GeomType::EllipseArc);
    QCOMPARE(arcs.size(), size_t(1));
    const Geometry* a = r.sk().geometry(arcs[0]);
    QVERIFY(near(a->start(), {100, 10}, 1e-6));
    QVERIFY(near(a->end(), {80, 0}, 1e-6));
    QVERIFY(near(a->sweep(), kPi / 2, 1e-9));
  }

  void splineTools() {
    Rig r;
    int line = r.sk().addGeometry(Geometry::makeLine({-10, 0}, {0, 0}));
    r.session()->refresh();
    r.mgr.run("sketch.spline-fit");
    r.click(0.3, 0.2);  // snaps onto the line end
    r.click(10, 5);
    r.click(20, -3);
    r.move(30, 4);
    QVERIFY(!r.viewer.previews().empty());
    r.click(30, 4);
    r.key(Qt::Key_Return);
    auto fits = r.ofType(GeomType::FitSpline);
    QCOMPARE(fits.size(), size_t(1));
    const Geometry* s = r.sk().geometry(fits[0]);
    QCOMPARE(s->vertexCount(), 4);
    QVERIFY(near(s->start(), {0, 0}, 1e-9));
    for (Pt2 p : {Pt2{10, 5}, Pt2{20, -3}, Pt2{30, 4}}) QVERIFY(s->distanceTo(p) < 1e-7);
    QVERIFY(r.sk().findConstraint(ConstraintType::Coincident, {fits[0], PointPos::Start}, {line, PointPos::End}));
    QVERIFY(r.sk().solve().ok());
    r.key(Qt::Key_Escape);

    // control-point spline of degree 2, finished by double-click
    r.mgr.run("sketch.spline-control", {"2"});
    r.click(0, 30);
    r.click(10, 40);
    r.click(20, 30);
    r.click(30, 40);
    r.dblclick(30, 40);
    auto cs = r.ofType(GeomType::Spline);
    QCOMPARE(cs.size(), size_t(1));
    const Geometry* c = r.sk().geometry(cs[0]);
    QCOMPARE(c->degree, 2);
    QCOMPARE(c->vertexCount(), 4);
    QVERIFY(near(c->end(), {30, 40}));
    // a closed fit spline: clicking the first point again ends it and closes it
    r.key(Qt::Key_Escape);
    r.mgr.run("sketch.spline-fit");
    r.click(60, 0);
    r.click(70, 10);
    r.click(80, 0);
    r.click(60, 0);
    fits = r.ofType(GeomType::FitSpline);
    QCOMPARE(fits.size(), size_t(2));
    QVERIFY(r.sk().findConstraint(ConstraintType::Coincident, {fits[1], PointPos::End}, {fits[1], PointPos::Start}));
    QVERIFY(r.session()->feature()->profiles().size() >= 1);
  }

  void textTool() {
    Rig r;
    r.mgr.run("sketch.text", {"AB", "8"});
    r.move(5, 5);
    QVERIFY(!r.viewer.previews().empty());
    r.click(5, 5);
    auto texts = r.ofType(GeomType::Text);
    QCOMPARE(texts.size(), size_t(1));
    const Geometry* t = r.sk().geometry(texts[0]);
    QCOMPARE(t->text, std::string("AB"));
    QVERIFY(near(t->p[2], 8.0) && near(t->start(), {5, 5}));
    // A: outline + counter, B: outline + two counters -> five closed regions
    QCOMPARE(r.session()->feature()->profiles().size(), size_t(5));
    // position first, then type the text
    r.click(0, -30);
    r.type("Hi");
    texts = r.ofType(GeomType::Text);
    QCOMPARE(texts.size(), size_t(2));
    QCOMPARE(r.sk().geometry(texts[1])->text, std::string("Hi"));
    QVERIFY(near(r.sk().geometry(texts[1])->p[2], 8.0));  // height remembered
    // text persists through the feature JSON
    QJsonObject j = r.session()->feature()->toJson();
    SketchFeature f;
    f.fromJson(j);
    int n = 0;
    for (const Geometry& g : f.sketch().geometries()) n += g.type == GeomType::Text;
    QCOMPARE(n, 2);
  }

  void moveCopyRotateScale() {
    Rig r;
    auto rect = addRectangle(r.sk(), {0, 0}, {10, 5});
    r.session()->refresh();
    // move: select by clicking the four sides, Enter, base point, relative target
    r.mgr.run("sketch.move");
    r.click(5, 0);
    r.click(10, 2.5);
    r.click(5, 5);
    r.click(0, 2.5);
    r.key(Qt::Key_Return);
    r.type("0,0");
    r.type("@20,10");
    QVERIFY(near(r.sk().geometry(rect[0])->start(), {20, 10}));
    QVERIFY(near(r.sk().geometry(rect[2])->start(), {30, 15}));
    QCOMPARE(r.sk().geometryCount(), 4);
    QVERIFY(r.sk().solve().ok());
    // copy (C toggles) of the preselected bottom side
    r.select({rect[0]});
    r.mgr.run("sketch.move");
    r.key(Qt::Key_C);
    r.type("0,0");
    r.type("0,-20");
    QCOMPARE(r.sk().geometryCount(), 5);
    QVERIFY(near(r.sk().geometries().back().start(), {20, -10}));
    // rotate the copied line 90 degrees about (20, -10)
    int copied = r.sk().geometries().back().id;
    r.select({copied});
    r.mgr.run("sketch.rotate");
    r.type("20,-10");
    r.type("90");
    QVERIFY(near(r.sk().geometry(copied)->end(), {20, 0}, 1e-9));
    // scale a circle by 3 about the origin
    int c = r.sk().addGeometry(Geometry::makeCircle({-20, 0}, 2));
    r.session()->refresh();
    r.select({c});
    r.mgr.run("sketch.scale");
    r.type("0,0");
    r.type("3");
    QVERIFY(near(r.sk().geometry(c)->center(), {-60, 0}) && near(r.sk().geometry(c)->radius(), 6));
    // each command is one undo step
    r.doc.undoStack().undo();
    QVERIFY(near(r.sk().geometry(c)->radius(), 2));
  }

  void patternsAndChamfer() {
    Rig r;
    int c = r.sk().addGeometry(Geometry::makeCircle({0, 0}, 2));
    r.session()->refresh();
    r.select({c});
    r.mgr.run("sketch.pattern-rectangular");
    r.type("3,10");
    r.type("2,8");
    r.key(Qt::Key_Return);
    QCOMPARE(r.ofType(GeomType::Circle).size(), size_t(6));
    QCOMPARE(r.session()->feature()->profiles().size(), size_t(6));
    // circular pattern of a point, 4 around the origin
    int p = r.sk().addGeometry(Geometry::makePoint({50, 0}));
    r.session()->refresh();
    r.select({p});
    r.mgr.run("sketch.pattern-circular");
    r.type("40,0");
    r.type("4");
    QCOMPARE(r.ofType(GeomType::Point).size(), size_t(4));
    bool found = false;
    for (const Geometry& g : r.sk().geometries())
      if (g.type == GeomType::Point && near(g.center(), {40, 10}, 1e-9)) found = true;
    QVERIFY(found);
    // arguments: preselection + count,spacing applies at once
    int c2 = r.sk().addGeometry(Geometry::makeCircle({0, 100}, 1));
    r.session()->refresh();
    r.select({c2});
    r.mgr.run("sketch.pattern-rectangular", {"4", "5"});
    QCOMPARE(r.ofType(GeomType::Circle).size(), size_t(10));

    // chamfer 2 x 2 on a square corner
    Rig r2;
    auto sq = addRectangle(r2.sk(), {0, 0}, {10, 10});
    r2.session()->refresh();
    r2.mgr.run("sketch.chamfer", {"2"});
    r2.click(5, 0);
    r2.move(10, 5);
    QVERIFY(!r2.viewer.previews().empty());
    r2.click(10, 5);
    QCOMPARE(r2.sk().geometryCount(), 5);
    QVERIFY(near(r2.sk().geometry(sq[0])->end(), {8, 0}));
    QVERIFY(near(r2.sk().geometry(sq[1])->start(), {10, 2}));
    QVERIFY(r2.sk().solve().ok());
    auto prof = r2.session()->feature()->profiles();
    QCOMPARE(prof.size(), size_t(1));
    // typed unequal distances on the opposite corner
    r2.type("3,1");
    r2.click(5, 10);
    r2.click(0, 5);
    QCOMPARE(r2.sk().geometryCount(), 6);
    QVERIFY(near(r2.sk().geometry(sq[2])->end(), {3, 10}));
    QVERIFY(near(r2.sk().geometry(sq[3])->start(), {0, 9}));
    r2.doc.undoStack().undo();
    QCOMPARE(r2.sk().geometryCount(), 5);
  }
};

QTEST_MAIN(CurveToolsTest)
#include "test_curve_tools.moc"
