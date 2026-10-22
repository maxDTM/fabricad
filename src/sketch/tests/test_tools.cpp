// Sketch tool tests: drive the registered sketch tools headlessly through a ToolManager with
// NullViewerContext / NullAppHost (mouse events carry sketch-plane points; typed input goes
// through the command line), and check the resulting geometry, constraints and undo.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "sketch/SketchFeature.h"
#include "sketch/SketchModule.h"
#include "sketch/SketchSession.h"
#include "sketch/tools/SketchToolBase.h"
#include "support/SketchTestHelpers.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <QCoreApplication>
#include <QtTest/QtTest>

#include <cctype>
#include <format>

using namespace fabricad;
using namespace fabricad::sketch;
using namespace fabricad::sketch::test;

namespace {

/// A body-producing feature so Project has something to project.
class BoxFeature : public core::Feature {
 public:
  BoxFeature() : Feature("TestBox") {
    addLength("size", 20.0);
  }
  void recompute(core::RecomputeContext& ctx) override {
    double s = ctx.length("size");
    ctx.createBody("Box", BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 5), s, s, s).Shape());
  }
};

struct Rig {
  core::ToolRegistry registry;
  core::Document doc;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager mgr{registry};

  Rig() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    sketch::registerModule(ctx);
    if (!core::FeatureFactory::instance().has("TestBox"))
      core::FeatureFactory::instance().registerType("TestBox", [] { return std::make_unique<BoxFeature>(); });
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);
    viewer.setPixelSize(0.25);  // 8 px snap tolerance = 2 mm
  }
  ~Rig() { SketchSession::end(); }

  /// Let the session's deferred render/idle-tool timers run.
  static void pump() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  SketchSession* session() { return SketchSession::current(); }
  SketchFeature* sketchFeature() { return session() ? session()->feature() : nullptr; }
  Sketch& sk() { return *session()->sketch(); }

  core::ViewerMouseEvent at(double x, double y, core::MouseButton b = core::MouseButton::None, int mods = 0) {
    core::ViewerMouseEvent e;
    e.button = b;
    e.modifiers = mods;
    e.hasPlanePoint = true;
    e.planePoint = session()->toWorld({x, y});
    e.x = static_cast<int>(x);
    e.y = static_cast<int>(-y);
    return e;
  }
  void move(double x, double y, int buttons = 0, int mods = 0) {
    auto e = at(x, y, core::MouseButton::None, mods);
    e.buttons = buttons;
    mgr.mouseMove(e);
  }
  void click(double x, double y, int mods = 0) {
    mgr.mouseMove(at(x, y, core::MouseButton::None, mods));
    mgr.mousePress(at(x, y, core::MouseButton::Left, mods));
    mgr.mouseRelease(at(x, y, core::MouseButton::Left, mods));
  }
  void dblclick(double x, double y) {
    auto e = at(x, y, core::MouseButton::Left);
    e.clickCount = 2;
    mgr.mouseDoubleClick(e);
  }
  void key(int k, int mods = 0) {
    core::ViewerKeyEvent e;
    e.key = k;
    e.modifiers = mods;
    mgr.keyPress(e);
  }
  /// User-visible constraints of a type (internal alignment/inference helpers excluded).
  int countType(ConstraintType t, bool includeInternal = false) {
    int n = 0;
    for (const auto& c : sk().constraints())
      if (c.type == t && (includeInternal || !c.internal)) ++n;
    return n;
  }
};

}  // namespace

class ToolsTest : public QObject {
  Q_OBJECT
 private slots:
  void registration() {
    Rig r;
    QVERIFY(r.registry.has("sketch.create"));
    QVERIFY(r.registry.has("sketch.finish"));
    QVERIFY(r.registry.has("sketch.select"));
    for (const char* id : {"sketch.line", "sketch.rectangle", "sketch.rectangle-3point", "sketch.rectangle-center", "sketch.circle",
                           "sketch.circle-2point", "sketch.circle-3point", "sketch.arc-3point", "sketch.arc-center", "sketch.arc-tangent",
                           "sketch.polygon", "sketch.slot", "sketch.point", "sketch.mirror", "sketch.project", "sketch.dimension",
                           "sketch.fillet", "sketch.trim", "sketch.extend", "sketch.break", "sketch.offset", "sketch.construction",
                           "sketch.constraint.horizontal-vertical", "sketch.constraint.coincident", "sketch.constraint.tangent",
                           "sketch.constraint.equal", "sketch.constraint.parallel", "sketch.constraint.perpendicular",
                           "sketch.constraint.fix", "sketch.constraint.midpoint", "sketch.constraint.concentric",
                           "sketch.constraint.collinear", "sketch.constraint.symmetric"})
      QVERIFY2(r.registry.has(id), id);
    auto groups = r.registry.groups(core::Workspace::Design, "SKETCH");
    QVERIFY(std::count(groups.begin(), groups.end(), "CREATE"));
    QVERIFY(std::count(groups.begin(), groups.end(), "MODIFY"));
    QVERIFY(std::count(groups.begin(), groups.end(), "CONSTRAINTS"));
    QCOMPARE(r.registry.byShortcut("L")->info().id, std::string("sketch.line"));
    QCOMPARE(r.registry.byShortcut("D")->info().id, std::string("sketch.dimension"));
    QCOMPARE(r.registry.byAlias("REC")->info().id, std::string("sketch.rectangle"));
    // sketch tools are disabled outside sketch mode
    QVERIFY(!r.registry.tool("sketch.line")->isEnabled(r.mgr.makeContext()));
    QVERIFY(r.registry.tool("sketch.create")->isEnabled(r.mgr.makeContext()));
  }

  void typedInputParsing() {
    TypedInput a = parseTypedInput("12.5");
    QCOMPARE(a.kind, TypedInput::Kind::Number);
    QCOMPARE(a.a, 12.5);
    TypedInput b = parseTypedInput("10, 20");
    QCOMPARE(b.kind, TypedInput::Kind::Pair);
    QCOMPARE(b.b, 20.0);
    TypedInput c = parseTypedInput("@5,-3");
    QCOMPARE(c.kind, TypedInput::Kind::Relative);
    QCOMPARE(c.b, -3.0);
    TypedInput d = parseTypedInput("@50<30");
    QCOMPARE(d.kind, TypedInput::Kind::Polar);
    QCOMPARE(d.a, 50.0);
    QCOMPARE(d.b, 30.0);
    QCOMPARE(parseTypedInput("width/2").kind, TypedInput::Kind::Text);
    bool startIsP = false;
    auto arc = tangentArc({0, 0}, {1, 0}, {10, 10}, &startIsP);
    QVERIFY(arc.has_value());
    QVERIFY(startIsP);
    QVERIFY(near(arc->center(), {0, 10}));
    QVERIFY(near(arc->start(), {0, 0}));
    QVERIFY(near(arc->end(), {10, 10}));
  }

  void createSketchAndEnterMode() {
    Rig r;
    QVERIFY(r.mgr.run("sketch.create", {"XY"}));
    QVERIFY(!r.mgr.isActive());  // create finishes itself
    QVERIFY(r.session());
    QCOMPARE(r.doc.featureCount(), 1);
    auto* f = r.sketchFeature();
    QVERIFY(f);
    QCOMPARE(f->type(), std::string("Sketch"));
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QVERIFY(r.viewer.inSketchMode());
    QCOMPARE(r.host.contextualTab(), std::string("SKETCH"));
    Rig::pump();
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.select"));
    // the sketch tools are enabled now
    QVERIFY(r.registry.tool("sketch.line")->isEnabled(r.mgr.makeContext()));
    // finish leaves sketch mode and the idle tool
    QVERIFY(r.mgr.run("sketch.finish"));
    QVERIFY(!r.session());
    QVERIFY(!r.viewer.inSketchMode());
    QVERIFY(r.host.contextualTab().empty());
    QVERIFY(!r.mgr.isActive());
    // undo removes the sketch
    r.doc.undoStack().undo();
    QCOMPARE(r.doc.featureCount(), 0);
    // creating from a preselected plane
    r.doc.selection().set(core::ShapeRef::originPlane("XZ"));
    QVERIFY(r.mgr.run("sketch.create"));
    QVERIFY(r.session());
    QVERIFY(near(r.sketchFeature()->plane().Direction().Y(), -1.0));
    // edit re-enters an existing sketch
    core::ObjectId id = r.session()->sketchId();
    r.mgr.run("sketch.finish");
    QVERIFY(!r.session());
    QVERIFY(r.mgr.run("sketch.edit", {std::to_string(id)}));
    QVERIFY(r.session());
    QCOMPARE(r.session()->sketchId(), id);
  }

  void createSketchWaitsForPlanePick() {
    Rig r;
    QVERIFY(r.mgr.run("sketch.create"));
    QVERIFY(r.mgr.isActive());
    QCOMPARE(r.doc.selection().filter(), core::kPlaneKinds);
    QVERIFY(!r.host.prompt().empty());
    r.doc.selection().set(core::ShapeRef::originPlane("YZ"));
    QVERIFY(!r.mgr.isActive());
    QVERIFY(r.session());
    QVERIFY(near(r.sketchFeature()->plane().Direction().X(), 1.0));
  }

  void lineChainWithInference() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    QVERIFY(r.mgr.run("sketch.line"));
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.line"));
    // a rectangle drawn slightly off-axis: inference snaps to H/V and closes the loop
    r.click(0, 0);
    r.move(20, 0.3);
    QVERIFY(!r.viewer.previews().empty());  // rubber band + glyph
    r.click(20, 0.3);
    QCOMPARE(r.sk().geometryCount(), 1);
    QVERIFY(near(r.sk().geometry(1)->end(), {20, 0}));  // snapped horizontal
    r.click(20.2, 10);
    r.click(0, 10.1);
    r.click(0.2, -0.1);  // closes on the first point
    QCOMPARE(r.sk().geometryCount(), 4);
    QCOMPARE(r.countType(ConstraintType::Coincident), 4);
    QCOMPARE(r.countType(ConstraintType::Horizontal), 2);
    QCOMPARE(r.countType(ConstraintType::Vertical), 2);
    QVERIFY(r.mgr.isActive());  // tool stays active after closing the loop
    QCOMPARE(r.sk().dofs(), 4);
    QCOMPARE(r.sketchFeature()->profiles().size(), size_t(1));
    QCOMPARE(r.doc.undoStack().count(), 5);  // create + 4 lines
    // every segment is one undo step
    r.doc.undoStack().undo();
    QCOMPARE(r.sk().geometryCount(), 3);
    r.doc.undoStack().redo();
    QCOMPARE(r.sk().geometryCount(), 4);
    // Esc with no chain in progress ends the tool; the idle tool returns
    r.key(Qt::Key_Escape);
    QVERIFY(!r.mgr.isActive() || r.mgr.activeToolId() != "sketch.line");
    Rig::pump();
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.select"));
    // display convention for the viewer
    const auto& named = r.sketchFeature()->namedShapes();
    QVERIFY(named.count("display:free"));
    QVERIFY(named.count("display:points"));
  }

  /// UX-23: a point snapped to another shape's tracking line adds no (hidden) constraint, so
  /// changing a dimension on one shape leaves the other where it is.
  void alignmentAddsNoHiddenConstraint() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    QVERIFY(r.mgr.run("sketch.line"));
    r.click(0, 0);
    r.click(0.2, 20);  // vertical line A
    r.key(Qt::Key_Escape);
    r.click(30, 5);
    r.click(50, 20.5);  // B's end snaps to A's end height (tracking line), no H/V direction
    r.key(Qt::Key_Escape);
    QCOMPARE(r.sk().geometryCount(), 2);
    const int a = 1, b = 2;
    QVERIFY(near(r.sk().geometry(b)->end(), {50, 20}));  // the snapped position stays
    for (const auto& c : r.sk().constraints())
      QVERIFY2(!(c.references(a) && c.references(b)), "no constraint ties the two shapes together");
    // dimension A's length and change it: B does not move
    r.sk().addConstraint(Constraint{.type = ConstraintType::Fix, .a = {a, PointPos::Start}});
    int dim = r.sk().addConstraint(Constraint{.type = ConstraintType::Distance, .a = {a}, .value = 20});
    QVERIFY(r.sk().solve().ok());
    r.sk().constraint(dim)->value = 30;
    QVERIFY(r.sk().solve().ok());
    QVERIFY(near(r.sk().geometry(a)->end(), {0, 30}));
    QVERIFY(near(r.sk().geometry(b)->end(), {50, 20}));
    QVERIFY(near(r.sk().geometry(b)->start(), {30, 5}));
  }

  void lineTypedInputAndTangentArc() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    r.mgr.run("sketch.line");
    QVERIFY(r.mgr.runCommandLine("0,0"));
    QVERIFY(r.mgr.runCommandLine("@30<0"));
    QCOMPARE(r.sk().geometryCount(), 1);
    QVERIFY(near(r.sk().geometry(1)->end(), {30, 0}));
    QCOMPARE(r.countType(ConstraintType::Horizontal), 1);
    QVERIFY(r.mgr.runCommandLine("@0,10"));
    QCOMPARE(r.sk().geometryCount(), 2);
    QCOMPARE(r.countType(ConstraintType::Vertical), 1);
    QVERIFY(near(r.sk().geometry(2)->end(), {30, 10}));
    // typed length along the current cursor direction
    r.move(30, 30);
    QVERIFY(r.mgr.runCommandLine("5"));
    QVERIFY(near(r.sk().geometry(3)->end(), {30, 15}, 1e-6));
    // drag from the last point: tangent arc
    r.mgr.mousePress(r.at(30, 15, core::MouseButton::Left));
    r.move(35, 20, 1);
    r.move(40, 25, 1);
    r.mgr.mouseRelease(r.at(40, 25, core::MouseButton::Left));
    QCOMPARE(r.sk().geometryCount(), 4);
    const Geometry* arc = r.sk().geometry(4);
    QCOMPARE(arc->type, GeomType::Arc);
    QVERIFY(near(arc->center(), {40, 15}, 1e-6));
    QCOMPARE(r.countType(ConstraintType::Tangent), 1);
    SolveResult res = r.sk().solve();
    QVERIFY2(res.ok(), res.message.c_str());
    // double-click ends the chain, the tool stays
    r.dblclick(40, 25);
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.line"));
  }

  void rectangleCircleArcPolygonSlot() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    r.mgr.run("sketch.rectangle");
    r.click(0, 0);
    r.move(25, 15);
    QVERIFY(!r.viewer.previews().empty());
    QVERIFY(r.mgr.runCommandLine("30,20"));
    QCOMPARE(r.sk().geometryCount(), 4);
    QCOMPARE(r.countType(ConstraintType::Horizontal), 2);
    QCOMPARE(r.countType(ConstraintType::Vertical), 2);
    // the typed size became driving width / height dimensions: only the position is free
    QCOMPARE(r.countType(ConstraintType::Distance), 2);
    QCOMPARE(r.sk().dofs(), 2);
    QCOMPARE(r.sketchFeature()->profiles().size(), size_t(1));
    QVERIFY(near(r.sketchFeature()->profiles()[0].area, 600.0, 1e-6));

    r.mgr.run("sketch.rectangle-center");
    r.click(60, 10);
    r.click(70, 15);
    QCOMPARE(r.sk().geometryCount(), 9);  // 4 lines + centre point
    QCOMPARE(r.countType(ConstraintType::Symmetric), 2);
    Pt2 mn, mx;
    Sketch tmp;
    for (int id = 5; id <= 8; ++id) tmp.addGeometry(*r.sk().geometry(id));
    QVERIFY(tmp.bbox(mn, mx));
    QVERIFY2(near(mn, {50, 5}) && near(mx, {70, 15}),
             std::format("bbox ({},{})-({},{})", mn.x, mn.y, mx.x, mx.y).c_str());

    r.mgr.run("sketch.rectangle-3point");
    r.click(100, 0);
    r.click(110, 10);
    r.click(100, 20);
    QCOMPARE(r.sk().geometryCount(), 13);
    QCOMPARE(r.countType(ConstraintType::Perpendicular), 3);

    r.mgr.run("sketch.circle");
    r.click(15, 10);  // centre: inside the first rectangle
    QVERIFY(r.mgr.runCommandLine("10"));  // diameter
    const Geometry* c = r.sk().geometry(14);
    QVERIFY(c && c->type == GeomType::Circle);
    QVERIFY(near(c->radius(), 5.0));
    QCOMPARE(r.countType(ConstraintType::Diameter), 1);  // typed diameter -> driving dimension
    QCOMPARE(r.sketchFeature()->profiles().size(), size_t(4));  // rect ring + circle + centre rect + 3pt rect

    r.mgr.run("sketch.circle-2point");
    r.click(150, 0);
    r.click(170, 0);
    QVERIFY(near(r.sk().geometry(15)->center(), {160, 0}) && near(r.sk().geometry(15)->radius(), 10.0));

    r.mgr.run("sketch.circle-3point");
    r.click(200, 0);
    r.click(210, 10);
    r.click(220, 0);
    QVERIFY(near(r.sk().geometry(16)->center(), {210, 0}));

    r.mgr.run("sketch.arc-3point");
    r.click(300, 0);
    r.click(320, 0);
    r.click(310, 10);
    const Geometry* a = r.sk().geometry(17);
    QVERIFY(a && a->type == GeomType::Arc);
    QVERIFY(near(a->center(), {310, 0}) && near(a->radius(), 10.0));

    r.mgr.run("sketch.arc-center");
    r.click(400, 0);
    r.click(410, 0);
    r.click(400, 10);
    const Geometry* a2 = r.sk().geometry(18);
    QVERIFY(a2 && a2->type == GeomType::Arc);
    QVERIFY(near(a2->start(), {410, 0}) && near(a2->end(), {400, 10}));

    // tangent arc continues from the end of the last arc
    r.mgr.run("sketch.arc-tangent");
    r.click(400, 10);  // snaps onto the end point of arc 18
    r.click(390, 20);
    QCOMPARE(r.sk().geometryCount(), 19);
    QCOMPARE(r.countType(ConstraintType::Tangent), 1);

    r.mgr.run("sketch.polygon", {"6"});
    r.click(500, 0);
    r.click(510, 0);
    QCOMPARE(r.sk().geometryCount(), 26);  // 6 lines + construction circle
    QCOMPARE(r.countType(ConstraintType::Equal), 5);
    SolveResult res = r.sk().solve();
    QVERIFY2(res.ok(), res.message.c_str());

    r.mgr.run("sketch.slot");
    r.click(600, 0);
    r.click(640, 0);
    QVERIFY(r.mgr.runCommandLine("10"));  // width
    QCOMPARE(r.sk().geometryCount(), 31);  // 2 lines, 2 arcs, axis
    res = r.sk().solve();
    QVERIFY2(res.ok(), res.message.c_str());
    QCOMPARE(r.countType(ConstraintType::Tangent), 5);
    QCOMPARE(r.countType(ConstraintType::Diameter), 2);  // circle d10 + slot width 10
    size_t before = r.sketchFeature()->profiles().size();
    QVERIFY(before >= 6);

    r.mgr.run("sketch.point");
    r.click(700, 0);
    QCOMPARE(r.sk().geometry(32)->type, GeomType::Point);
  }

  /// Keys as the shell forwards them while drawing: printable text, Tab, Enter.
  static void typeKeys(Rig& r, const std::string& s) {
    for (char c : s) {
      core::ViewerKeyEvent e;
      e.key = std::toupper(static_cast<unsigned char>(c));
      e.text = std::string(1, c);
      r.mgr.keyPress(e);
    }
  }

  // Test shape "L50-30": from (0,0) a line of exactly 50 mm at 30 deg typed into the value boxes,
  // then a 20 mm segment at 90 deg to it (relative angle), then a closing chain is not drawn.
  void lineValueBoxesCreateDimensions() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    r.mgr.run("sketch.line");
    auto* tool = dynamic_cast<SketchTool*>(r.mgr.activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->values().fieldCount(), 2);
    QCOMPARE(tool->values().visibleCount(), 0);  // no boxes before the first point
    r.click(0, 0);
    QCOMPARE(tool->values().visibleCount(), 2);
    r.move(40, 5);
    QVERIFY(near(tool->values().value("length"), std::sqrt(40.0 * 40.0 + 25.0), 1e-9));
    QVERIFY(!r.viewer.previews().empty());
    // type the length, Tab, the angle, Enter
    typeKeys(r, "50");
    QVERIFY(tool->values().isTyping());
    QCOMPARE(tool->values().displayText(0), std::string("50"));
    r.key(Qt::Key_Tab);
    QVERIFY(tool->values().isLocked("length"));
    QCOMPARE(tool->values().activeIndex(), 1);
    // the rubber band is locked to 50 mm along the cursor direction
    r.move(10, 1);
    QVERIFY(near(tool->values().value("length"), 50.0, 1e-9));
    typeKeys(r, "30");
    r.key(Qt::Key_Return);
    QCOMPARE(r.sk().geometryCount(), 1);
    const Geometry* l1 = r.sk().geometry(1);
    QVERIFY(near(l1->end(), {50 * std::cos(kPi / 6), 50 * std::sin(kPi / 6)}, 1e-6));
    const Constraint* d1 = r.sk().dimensionByName("d1");
    const Constraint* d2 = r.sk().dimensionByName("d2");
    QVERIFY(d1 && d2);
    QCOMPARE(d1->type, ConstraintType::Distance);
    QVERIFY(near(d1->value, 50.0));
    QVERIFY(!d1->driven);
    QCOMPARE(d2->type, ConstraintType::Angle);
    QVERIFY(near(d2->value, 30.0));
    QVERIFY(!d2->driven);
    QCOMPARE(r.sk().dofs(), 2);  // only the start point is free
    // boxes are reset for the next segment; the angle is now relative to the previous line
    QVERIFY(!tool->values().hasUserInput());
    r.move(60, 40);
    typeKeys(r, "20");
    r.key(Qt::Key_Tab);
    typeKeys(r, "90");
    r.key(Qt::Key_Return);
    QCOMPARE(r.sk().geometryCount(), 2);
    QCOMPARE(r.countType(ConstraintType::Perpendicular), 1);  // 90 deg -> perpendicular, no angle dimension
    QCOMPARE(r.countType(ConstraintType::Angle), 1);
    QVERIFY(near(r.sk().geometry(2)->length(), 20.0, 1e-6));
    QVERIFY(near(r.sk().dimensionByName("d3")->value, 20.0));
    QCOMPARE(r.sk().dofs(), 2);
    SolveResult res = r.sk().solve();
    QVERIFY2(res.ok(), res.message.c_str());
    // Esc with typed text clears the box instead of ending the chain
    typeKeys(r, "7");
    r.key(Qt::Key_Escape);
    QVERIFY(!tool->values().isTyping());
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.line"));
    r.key(Qt::Key_Escape);  // ends the chain
    r.key(Qt::Key_Escape);  // ends the tool
    Rig::pump();
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.select"));
    // expression with a unit through the command line: driving dimension keeps the expression
    r.doc.setParameter("len", "2 in");
    r.mgr.run("sketch.line");
    r.click(0, 50);
    r.move(10, 50);
    QVERIFY(r.mgr.runCommandLine("len/2"));
    const Geometry* l3 = r.sk().geometry(3);
    QVERIFY(near(l3->length(), 25.4, 1e-6));
    QCOMPARE(r.sk().dimensionByName("d4")->expression, std::string("len/2"));
    QCOMPARE(r.countType(ConstraintType::Horizontal), 1);  // inference still applies with a locked length
    // relative coordinates create DistanceX / DistanceY
    QVERIFY(r.mgr.runCommandLine("@10,5"));
    QCOMPARE(r.countType(ConstraintType::DistanceX), 1);
    QCOMPARE(r.countType(ConstraintType::DistanceY), 1);
    QVERIFY(near(r.sk().geometry(4)->end(), {35.4, 55}, 1e-6));
    // undo removes one segment with its dimensions
    r.doc.undoStack().undo();
    QCOMPARE(r.sk().geometryCount(), 3);
    QCOMPARE(r.countType(ConstraintType::DistanceX), 0);
  }

  // Test shapes: rectangle 30 x 20 at (0,0) via boxes; centre rectangle 40 x 10 at (100,0) typed
  // "40,10"; circle d12 at (0,50); centre arc R10 sweep 90 at (60,60); hexagon R8 at (0,100);
  // slot 40 long 10 wide at (100,100). Every typed value is a driving dimension afterwards.
  void shapeValueBoxesCreateDimensions() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    // 2-point rectangle: width Tab height Enter
    r.mgr.run("sketch.rectangle");
    auto* tool = dynamic_cast<SketchTool*>(r.mgr.activeTool());
    QVERIFY(tool);
    r.click(0, 0);
    r.move(5, 5);
    typeKeys(r, "30");
    r.key(Qt::Key_Tab);
    typeKeys(r, "20");
    r.key(Qt::Key_Return);
    QCOMPARE(r.sk().geometryCount(), 4);
    QVERIFY(near(r.sk().geometry(1)->length(), 30.0, 1e-9));
    QVERIFY(near(r.sk().geometry(2)->length(), 20.0, 1e-9));
    QCOMPARE(r.countType(ConstraintType::Distance), 2);
    QCOMPARE(r.sk().dofs(), 2);
    QVERIFY(near(r.sketchFeature()->profiles()[0].area, 600.0, 1e-6));
    // typed width only: the height follows the cursor
    r.mgr.run("sketch.rectangle-center");
    r.click(100, 0);
    r.move(130, 5);
    QVERIFY(r.mgr.runCommandLine("40,10"));
    QCOMPARE(r.sk().geometryCount(), 9);
    QCOMPARE(r.countType(ConstraintType::Distance), 4);
    Pt2 mn, mx;
    Sketch tmp;
    for (int id = 5; id <= 8; ++id) tmp.addGeometry(*r.sk().geometry(id));
    QVERIFY(tmp.bbox(mn, mx));
    QVERIFY2(near(mn, {80, -5}) && near(mx, {120, 5}), std::format("bbox ({},{})-({},{})", mn.x, mn.y, mx.x, mx.y).c_str());
    // circle: diameter box
    r.mgr.run("sketch.circle");
    r.click(0, 50);
    r.move(3, 50);
    typeKeys(r, "12");
    r.key(Qt::Key_Return);
    const Geometry* c = r.sk().geometry(10);
    QVERIFY(c && c->type == GeomType::Circle && near(c->radius(), 6.0, 1e-9));
    QCOMPARE(r.sk().dimensionByName("d5")->type, ConstraintType::Diameter);
    QVERIFY(near(r.sk().dimensionByName("d5")->value, 12.0));
    // centre arc: radius at the start point, sweep angle at the end point
    r.mgr.run("sketch.arc-center");
    r.click(60, 60);
    r.move(65, 60);
    typeKeys(r, "10");
    r.key(Qt::Key_Return);  // start point at (70,60)
    r.move(60, 70);         // counter-clockwise side
    typeKeys(r, "90");
    r.key(Qt::Key_Return);
    const Geometry* a = r.sk().geometry(11);
    QVERIFY(a && a->type == GeomType::Arc);
    QVERIFY(near(a->radius(), 10.0, 1e-9));
    QVERIFY(near(a->start(), {70, 60}, 1e-6));
    QVERIFY(near(a->end(), {60, 70}, 1e-6));
    QVERIFY(near(a->sweep() * 180.0 / kPi, 90.0, 1e-6));
    QCOMPARE(r.sk().dimensionByName("d6")->type, ConstraintType::Radius);
    QCOMPARE(r.sk().dimensionByName("d7")->type, ConstraintType::Angle);
    QVERIFY(near(*r.sk().measure(*r.sk().dimensionByName("d7")), 90.0, 1e-6));
    // the sweep dimension drives the solver: change it to 180 and the arc becomes a half circle
    // (the centre and start angle are free, so only the sweep itself and the chord are checked)
    r.sk().constraint(r.sk().dimensionByName("d7")->id)->value = 180.0;
    SolveResult res = r.sk().solve();
    QVERIFY2(res.ok(), res.message.c_str());
    {
      const Geometry* a2 = r.sk().geometry(11);
      QVERIFY2(near(a2->sweep() * 180.0 / kPi, 180.0, 1e-6), std::format("sweep {}", a2->sweep() * 180.0 / kPi).c_str());
      QVERIFY2(near(a2->start().distance(a2->end()), 20.0, 1e-6),
               std::format("chord {} start ({},{}) end ({},{})", a2->start().distance(a2->end()), a2->start().x, a2->start().y, a2->end().x, a2->end().y).c_str());
      QVERIFY(near(a2->radius(), 10.0, 1e-6));
    }
    r.sk().constraint(r.sk().dimensionByName("d7")->id)->value = 90.0;
    r.sk().solve();
    // polygon: sides box before the centre, radius box after
    r.mgr.run("sketch.polygon-inscribed");
    typeKeys(r, "8");
    r.key(Qt::Key_Return);
    QVERIFY(r.host.prompt().find("8 sides") != std::string::npos);
    r.click(0, 100);
    r.move(5, 100);
    typeKeys(r, "8");
    r.key(Qt::Key_Return);
    QCOMPARE(r.sk().geometryCount(), 11 + 9);  // 8 lines + construction circle
    const Geometry* circ = r.sk().geometry(20);
    QVERIFY(circ && circ->type == GeomType::Circle && circ->construction);
    QVERIFY2(near(circ->radius(), 8.0, 1e-6), std::format("polygon circle radius {}", circ->radius()).c_str());
    QCOMPARE(r.sk().dimensionByName("d8")->type, ConstraintType::Radius);
    // slot: centre distance, then width
    r.mgr.run("sketch.slot");
    r.click(100, 100);
    r.move(110, 100);
    typeKeys(r, "40");
    r.key(Qt::Key_Return);
    r.move(120, 108);
    typeKeys(r, "10");
    r.key(Qt::Key_Return);
    QCOMPARE(r.sk().geometryCount(), 25);
    QVERIFY(near(r.sk().geometry(25)->length(), 40.0, 1e-6));  // construction axis
    QVERIFY(near(r.sk().geometry(23)->radius(), 5.0, 1e-6));   // arc radius = width / 2
    QCOMPARE(r.sk().dimensionByName("d9")->type, ConstraintType::Distance);
    QCOMPARE(r.sk().dimensionByName("d10")->type, ConstraintType::Diameter);
    res = r.sk().solve();
    QVERIFY2(res.ok(), res.message.c_str());
    QVERIFY(!r.sk().hasConflicts());
    // a typed value that cannot be satisfied is kept as a driven dimension, never a conflict
    r.mgr.run("sketch.circle");
    int c2 = r.sk().addGeometry(Geometry::makeCircle({200, 0}, 5));
    r.sk().addConstraint(Constraint{.type = ConstraintType::Radius, .a = {c2}, .value = 5});
    r.session()->refresh();
    r.mgr.run("sketch.offset");
    r.click(205, 0);  // the circle
    r.move(212, 0);
    QVERIFY(r.mgr.runCommandLine("3"));
    QVERIFY(!r.mgr.isActive() || r.mgr.activeToolId() != "sketch.offset");
    QVERIFY(!r.sk().hasConflicts());
  }

  void offsetValueBoxCreatesDimension() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    auto ids = addRectangle(r.sk(), {0, 0}, {30, 20});
    r.session()->refresh();
    r.mgr.run("sketch.offset");
    auto* tool = dynamic_cast<SketchTool*>(r.mgr.activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->values().visibleCount(), 0);  // nothing selected yet
    r.click(15, 0);  // bottom line -> whole chain
    QCOMPARE(tool->values().visibleCount(), 1);
    r.move(15, -3);  // outside
    QVERIFY(near(tool->values().value("distance"), 3.0, 1e-9));
    typeKeys(r, "5");
    r.key(Qt::Key_Return);
    QCOMPARE(r.sk().geometryCount(), 8);
    const Constraint* d1 = r.sk().dimensionByName("d1");
    QVERIFY(d1);
    QCOMPARE(d1->type, ConstraintType::Distance);
    QVERIFY(near(d1->value, 5.0));
    QVERIFY(!d1->driven);
    // the dimension links a source line to its offset copy at exactly 5 mm
    const Geometry* src = r.sk().geometry(d1->a.geo);
    const Geometry* off = r.sk().geometry(d1->b.geo);
    QVERIFY(src && off && src->type == GeomType::Line && off->type == GeomType::Line);
    QVERIFY(near(std::abs(signedDistanceToLine(src->start(), src->direction(), off->start())), 5.0, 1e-6));
    QVERIFY(!r.mgr.isActive() || r.mgr.activeToolId() != "sketch.offset");
    Rig::pump();
    // undo removes the offset and its dimension together
    r.doc.undoStack().undo();
    QCOMPARE(r.sk().geometryCount(), 4);
    QVERIFY(!r.sk().dimensionByName("d1"));
  }

  void constraintToolsAndStatus() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    int l1 = r.sk().addGeometry(Geometry::makeLine({0, 0}, {10, 1}));
    int l2 = r.sk().addGeometry(Geometry::makeLine({0, 5}, {10, 7}));
    r.session()->refresh();
    // horizontal/vertical on a click
    r.mgr.run("sketch.constraint.horizontal-vertical");
    r.click(5, 0.5);
    QCOMPARE(r.countType(ConstraintType::Horizontal), 1);
    QVERIFY(near(r.sk().geometry(l1)->end().y, 0.0, 1e-9));
    QVERIFY(r.mgr.isActive());  // stays active
    // parallel from preselection
    r.doc.selection().set({r.session()->toShapeRef({l1, PointPos::None}), r.session()->toShapeRef({l2, PointPos::None})});
    r.mgr.run("sketch.constraint.parallel");
    QCOMPARE(r.countType(ConstraintType::Parallel), 1);
    QVERIFY(near(r.sk().geometry(l2)->direction().y, 0.0, 1e-9));
    // a conflicting constraint is rejected with a message
    size_t msgs = r.host.messages().size();
    r.mgr.run("sketch.constraint.perpendicular");
    r.click(5, 0);
    r.click(5, 5);
    QCOMPARE(r.countType(ConstraintType::Perpendicular), 0);
    QVERIFY(r.host.messages().size() > msgs);
    // coincident: two end points
    r.mgr.run("sketch.constraint.coincident");
    r.click(10, 0);
    r.click(0, 5);
    QCOMPARE(r.countType(ConstraintType::Coincident), 1);
    QVERIFY(near(r.sk().geometry(l1)->end(), r.sk().geometry(l2)->start(), 1e-6));
    // fix the first line (the solve moved it: click its current midpoint): status goes green
    r.mgr.run("sketch.constraint.fix");
    Pt2 m1 = r.sk().geometry(l1)->midpoint();
    r.click(m1.x, m1.y);
    QCOMPARE(r.countType(ConstraintType::Fix), 1);
    QCOMPARE(r.sk().statusOf(l1), GeometryStatus::Fixed);
    QVERIFY(r.session()->statusText().find("DOF") != std::string::npos);
    // fix again removes it
    r.click(m1.x, m1.y);
    QCOMPARE(r.countType(ConstraintType::Fix), 0);
    // equal via two clicks
    r.mgr.run("sketch.constraint.equal");
    m1 = r.sk().geometry(l1)->midpoint();
    Pt2 m2 = r.sk().geometry(l2)->midpoint();
    r.click(m1.x, m1.y);
    r.click(m2.x, m2.y);
    QCOMPARE(r.countType(ConstraintType::Equal), 1);
    QVERIFY(near(r.sk().geometry(l1)->length(), r.sk().geometry(l2)->length(), 1e-6));
    // undo steps exist for each constraint commit
    QVERIFY(r.doc.undoStack().count() >= 5);
  }

  void dimensionToolAndEdit() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    auto ids = addRectangle(r.sk(), {0, 0}, {30, 20});
    r.sk().addConstraint(Constraint{.type = ConstraintType::Fix, .a = {ids[0], PointPos::Start}});
    r.session()->refresh();
    r.mgr.run("sketch.dimension");
    r.click(15, 0);   // bottom line
    r.click(15, -8);  // place below
    const Constraint* d1 = r.sk().dimensionByName("d1");
    QVERIFY(d1);
    QCOMPARE(d1->type, ConstraintType::Distance);
    QVERIFY(near(d1->value, 30.0));
    QVERIFY(r.host.prompt().find("d1") != std::string::npos);
    QVERIFY(r.mgr.runCommandLine("40"));  // typed value
    QVERIFY(near(r.sk().geometry(ids[0])->length(), 40.0, 1e-6));
    QVERIFY(near(r.sk().geometry(ids[1])->start(), {40, 0}, 1e-6));
    QVERIFY(r.mgr.isActive());  // the tool stays for the next dimension
    // point-to-point placed to the side -> vertical distance, expression with a parameter
    r.doc.setParameter("h", "25 mm");
    r.click(40, 0);   // corner point
    r.click(40, 20);  // corner point
    r.click(55, 10);  // place to the right -> DistanceY
    const Constraint* d2 = r.sk().dimensionByName("d2");
    QVERIFY(d2);
    QCOMPARE(d2->type, ConstraintType::DistanceY);
    QVERIFY(r.mgr.runCommandLine("h"));
    QCOMPARE(r.sk().dimensionByName("d2")->expression, std::string("h"));
    QVERIFY(near(r.sk().geometry(ids[1])->length(), 25.0, 1e-6));
    QCOMPARE(r.sk().dofs(), 0);
    QCOMPARE(r.sketchFeature()->statusText(), std::string("Fully constrained"));
    // a dimension that over-constrains becomes driven when the host says yes
    r.host.setYesNoAnswer(true);
    r.click(20, 25);  // top line (now at y=25)
    r.click(20, 32);
    const Constraint* d3 = r.sk().dimensionByName("d3");
    QVERIFY(d3);
    QVERIFY(d3->driven);
    r.key(Qt::Key_Return);  // keep measured value
    QCOMPARE(r.sk().dimensionByName("d3")->driven, true);
    QVERIFY(near(*r.sk().measure(*r.sk().dimensionByName("d3")), 40.0, 1e-6));
    // ... and is refused when the host says no
    r.host.setYesNoAnswer(false);
    r.click(0, 12);  // left line
    r.click(-8, 12);
    QVERIFY(!r.sk().dimensionByName("d4"));
    r.key(Qt::Key_Escape);
    Rig::pump();
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.select"));
    // double-click on a dimension label opens the edit tool; a typed value applies it
    Pt2 label = r.sk().dimensionByName("d1")->labelPos;
    r.dblclick(label.x, label.y);
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.dimension-edit"));
    QVERIFY(r.mgr.runCommandLine("50"));
    QVERIFY(near(r.sk().geometry(ids[0])->length(), 50.0, 1e-6));
    QVERIFY(!r.mgr.isActive() || r.mgr.activeToolId() != "sketch.dimension-edit");
    // edit with arguments; expression referencing another dimension
    QVERIFY(r.mgr.run("sketch.dimension-edit", {std::to_string(r.sk().dimensionByName("d2")->id), "d1/2"}));
    QVERIFY(near(r.sk().geometry(ids[1])->length(), 25.0, 1e-6));
    // document persists expressions and re-evaluates them on load
    QJsonObject json = r.doc.toJson();
    core::Document doc2;
    std::string err;
    QVERIFY2(doc2.fromJson(json, &err), err.c_str());
    auto* f2 = doc2.featureAs<SketchFeature>(r.session()->sketchId());
    QVERIFY(f2);
    QCOMPARE(f2->sketch().dimensionByName("d2")->expression, std::string("d1/2"));
    QVERIFY(near(f2->sketch().geometry(ids[1])->length(), 25.0, 1e-6));
    // undo the last edit
    r.doc.undoStack().undo();
    QVERIFY(near(r.sk().geometry(ids[1])->length(), 25.0, 1e-6));  // value was 25 before too (h)
    QCOMPARE(r.sk().dimensionByName("d2")->expression, std::string("h"));
  }

  void trimExtendBreakFilletOffsetMirror() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    int h = r.sk().addGeometry(Geometry::makeLine({0, 0}, {30, 0}));
    int v1 = r.sk().addGeometry(Geometry::makeLine({10, -5}, {10, 5}));
    int v2 = r.sk().addGeometry(Geometry::makeLine({20, -5}, {20, 5}));
    r.session()->refresh();
    r.mgr.run("sketch.trim");
    r.move(15, 0);
    QVERIFY(!r.viewer.previews().empty());  // red piece preview
    r.click(15, 0);
    QCOMPARE(r.sk().geometryCount(), 4);  // h split into two pieces
    QVERIFY(near(r.sk().geometry(h)->end(), {10, 0}));
    // power trim: press and sweep across both verticals' lower halves
    r.mgr.mousePress(r.at(5, -3, core::MouseButton::Left));
    r.move(10, -3, 1);
    r.move(20, -3, 1);
    r.mgr.mouseRelease(r.at(20, -3, core::MouseButton::Left));
    QVERIFY(near(r.sk().geometry(v1)->start(), {10, 0}, 1e-6) || near(r.sk().geometry(v1)->end(), {10, 0}, 1e-6));
    QVERIFY(near(r.sk().geometry(v2)->start(), {20, 0}, 1e-6) || near(r.sk().geometry(v2)->end(), {20, 0}, 1e-6));
    // extend: the first piece (0..10) back? no: extend the short second piece to the right wall
    int wall = r.sk().addGeometry(Geometry::makeLine({40, -10}, {40, 10}));
    r.session()->refresh();
    r.mgr.run("sketch.extend");
    r.click(29, 0);  // near the end of the right piece (20..30)
    bool extended = false;
    for (const Geometry& g : r.sk().geometries())
      if (g.type == GeomType::Line && near(g.end(), {40, 0}, 1e-6)) extended = true;
    QVERIFY(extended);
    (void)wall;
    // break
    int n = r.sk().geometryCount();
    r.mgr.run("sketch.break");
    r.click(5, 0);
    QCOMPARE(r.sk().geometryCount(), n + 1);
    QVERIFY(near(r.sk().geometry(h)->end(), {5, 0}, 1e-6));

    // fillet two lines meeting at a corner, with a typed radius
    Rig r2;
    r2.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    int l1 = r2.sk().addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int l2 = r2.sk().addGeometry(Geometry::makeLine({10, 0}, {10, 10}));
    r2.sk().addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l1, PointPos::End}, .b = {l2, PointPos::Start}});
    r2.session()->refresh();
    r2.mgr.run("sketch.fillet");
    QVERIFY(r2.mgr.runCommandLine("2"));
    r2.click(5, 0);
    r2.move(10, 5);
    r2.click(10, 5);
    QCOMPARE(r2.sk().geometryCount(), 3);
    QVERIFY(near(r2.sk().geometry(3)->radius(), 2.0));
    QVERIFY(r2.sk().solve().ok());

    // UX-22: a fillet that leaves the sketch unsolvable is rolled back instead of committed
    Rig r5;
    r5.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    int m1 = r5.sk().addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    int m2 = r5.sk().addGeometry(Geometry::makeLine({10, 0}, {10, 10}));
    r5.sk().addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {m1, PointPos::End}, .b = {m2, PointPos::Start}});
    int e = r5.sk().addGeometry(Geometry::makeLine({30, 0}, {40, 0}));
    r5.sk().addConstraint(Constraint{.type = ConstraintType::Distance, .a = {e}, .value = 10});
    r5.sk().addConstraint(Constraint{.type = ConstraintType::Distance, .a = {e}, .value = 20});  // conflict
    r5.session()->refresh();
    const int undoCount = r5.doc.undoStack().count();
    r5.mgr.run("sketch.fillet");
    QVERIFY(r5.mgr.runCommandLine("2"));
    r5.click(5, 0);
    r5.click(10, 5);
    QCOMPARE(r5.sk().geometryCount(), 3);  // no arc
    QVERIFY(near(r5.sk().geometry(m1)->end(), {10, 0}));
    QCOMPARE(r5.doc.undoStack().count(), undoCount);

    // offset a rectangle chain inward by a typed distance
    Rig r3;
    r3.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    auto ids = addRectangle(r3.sk(), {0, 0}, {20, 20});
    r3.session()->refresh();
    r3.mgr.run("sketch.offset");
    r3.click(10, 0);  // picks the whole chain
    r3.move(10, 10);  // inside
    QVERIFY(r3.mgr.runCommandLine("3"));
    QCOMPARE(r3.sk().geometryCount(), 8);
    QVERIFY(!r3.mgr.isActive() || r3.mgr.activeToolId() != "sketch.offset");
    Pt2 mn, mx;
    Sketch tmp;
    for (int id = 5; id <= 8; ++id) tmp.addGeometry(*r3.sk().geometry(id));
    QVERIFY(tmp.bbox(mn, mx));
    QVERIFY(near(mn, {3, 3}) && near(mx, {17, 17}));
    QCOMPARE(r3.sketchFeature()->profiles().size(), size_t(2));
    (void)ids;

    // mirror a line about a construction axis with symmetry constraints
    Rig r4;
    r4.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    int axis = r4.sk().addGeometry(Geometry::makeLine({0, -10}, {0, 10}));
    r4.sk().geometry(axis)->construction = true;
    int ln = r4.sk().addGeometry(Geometry::makeLine({2, 0}, {6, 3}));
    r4.session()->refresh();
    r4.mgr.run("sketch.mirror");
    r4.click(4, 1.5);  // object
    r4.key(Qt::Key_Return);
    r4.click(0, 5);  // axis
    QCOMPARE(r4.sk().geometryCount(), 3);
    QVERIFY(near(r4.sk().geometry(3)->start(), {-2, 0}));
    QCOMPARE(r4.countType(ConstraintType::Symmetric), 2);
    (void)ln;
  }

  void constructionToggleAndSelectTool() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    int l = r.sk().addGeometry(Geometry::makeLine({0, 0}, {10, 0}));
    r.session()->refresh();
    // construction mode toggle with nothing selected
    r.mgr.run("sketch.construction");
    QVERIFY(r.session()->constructionMode());
    r.mgr.run("sketch.line");
    r.click(0, 20);
    r.click(10, 20);
    QVERIFY(r.sk().geometry(2)->construction);
    r.key(Qt::Key_Escape);
    r.key(Qt::Key_Escape);
    Rig::pump();
    QCOMPARE(r.mgr.activeToolId(), std::string("sketch.select"));
    // select tool: click selects, Delete removes, drag moves free geometry
    r.click(5, 0);
    QCOMPARE(r.doc.selection().count(), 1);
    QCOMPARE(r.doc.selection().items()[0].kind, core::ShapeKind::SketchCurve);
    QCOMPARE(r.doc.selection().items()[0].index, l);
    r.mgr.run("sketch.construction");
    QVERIFY(r.sk().geometry(l)->construction);
    Rig::pump();
    // window selection
    r.mgr.mousePress(r.at(-5, -5, core::MouseButton::Left));
    r.move(30, 30, 1);
    r.mgr.mouseRelease(r.at(30, 30, core::MouseButton::Left));
    QCOMPARE(r.doc.selection().count(), 2);
    // drag the end point of the free line
    r.doc.selection().clear();
    r.mgr.mousePress(r.at(10, 0, core::MouseButton::Left));
    r.move(12, 1, 1);
    r.move(15, 5, 1);
    r.mgr.mouseRelease(r.at(15, 5, core::MouseButton::Left));
    QVERIFY(near(r.sk().geometry(l)->end(), {15, 5}, 1e-3));
    QVERIFY(near(r.sk().geometry(l)->start(), {0, 0}, 1e-6));
    int undoBefore = r.doc.undoStack().count();
    r.doc.undoStack().undo();
    QVERIFY(near(r.sk().geometry(l)->end(), {10, 0}, 1e-6));
    QCOMPARE(r.doc.undoStack().index(), undoBefore - 1);
    r.doc.undoStack().redo();
    // delete
    r.click(7, 2.5);
    QCOMPARE(r.doc.selection().count(), 1);
    r.key(Qt::Key_Delete);
    QVERIFY(!r.sk().geometry(l));
    QCOMPARE(r.sk().geometryCount(), 1);
  }

  void projectBodyEdges() {
    Rig r;
    r.doc.addFeature(std::make_unique<BoxFeature>());
    r.doc.recompute();
    QCOMPARE(r.doc.liveBodies().size(), size_t(1));
    core::ObjectId bodyId = r.doc.liveBodies()[0];
    const TopoDS_Shape& box = r.doc.bodyShape(bodyId);
    QVERIFY(!box.IsNull());
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    r.mgr.run("sketch.project");
    QVERIFY(r.mgr.isActive());
    // pick the top face (z = 25): its 4 edges project to a square
    TopoDS_Shape top;
    for (const TopoDS_Shape& f : core::subShapes(box, TopAbs_FACE)) {
      auto pl = core::facePlane(f);
      if (pl && std::abs(pl->Location().Z() - 25.0) < 1e-6) top = f;
    }
    QVERIFY(!top.IsNull());
    core::ShapeRef faceRef = core::ShapeRef::subShape(bodyId, box, top);
    r.doc.selection().set(faceRef);
    QCOMPARE(r.sk().geometryCount(), 4);
    for (const Geometry& g : r.sk().geometries()) {
      QVERIFY(g.projected);
      QCOMPARE(g.source.kind, core::ShapeKind::Edge);
      QCOMPARE(g.source.owner, bodyId);
      QCOMPARE(r.sk().statusOf(g.id), GeometryStatus::Projected);
    }
    QCOMPARE(r.sketchFeature()->profiles().size(), size_t(1));
    QVERIFY(near(r.sketchFeature()->profiles()[0].area, 400.0, 1e-6));
    // the same face again adds nothing
    r.doc.selection().set(faceRef);
    QCOMPARE(r.sk().geometryCount(), 4);
    r.key(Qt::Key_Return);
    // projected geometry follows the body: change the box size and recompute
    auto* boxf = r.doc.featureAt(0);
    QJsonObject before = boxf->toJson();
    boxf->prop("size").setDouble(30.0);
    r.doc.undoStack().push(std::make_unique<core::ModifyFeatureCommand>(r.doc, boxf->id(), before, boxf->toJson()));
    QVERIFY2(r.sketchFeature()->status() == core::FeatureStatus::Ok,  // re-matched silently, signature refreshed
             r.sketchFeature()->statusMessage().c_str());
    QVERIFY(near(r.sketchFeature()->profiles()[0].area, 900.0, 1e-6));
    // stored refs now carry the new signature, so the next recompute matches exactly
    r.doc.recomputeAll();
    QCOMPARE(r.sketchFeature()->status(), core::FeatureStatus::Ok);
    // dependency recorded
    auto deps = r.doc.dependenciesOf(r.session()->sketchId());
    QVERIFY(std::count(deps.begin(), deps.end(), boxf->id()));
  }

  void sessionRenderingAndPalette() {
    Rig r;
    r.mgr.run("sketch.create", {"XY"});
    Rig::pump();
    addRectangle(r.sk(), {0, 0}, {10, 10});
    r.sk().addConstraint(Constraint{.type = ConstraintType::Distance, .a = {1}, .value = 10});
    r.session()->refresh();
    size_t n = r.viewer.previews().size();
    QVERIFY(n > 0);
    int texts = 0, polylines = 0;
    for (const auto& [h, item] : r.viewer.previews()) {
      if (item.kind == "text") ++texts;
      if (item.kind == "polyline") ++polylines;
    }
    QVERIFY(polylines >= 4);  // geometry
    QVERIFY(texts >= 5);      // 4 H/V glyphs + dimension label
    r.mgr.run("sketch.toggle-constraints");
    QVERIFY(!r.session()->showConstraints());
    r.mgr.run("sketch.toggle-profiles");
    QVERIFY(r.session()->showProfiles());
    Rig::pump();
    int shapes = 0;
    for (const auto& [h, item] : r.viewer.previews())
      if (item.kind == "shape") ++shapes;
    QCOMPARE(shapes, 1);  // one profile face
    QVERIFY(r.session()->statusText().find("DOF") != std::string::npos);
    QVERIFY(r.viewer.visibilityOverrides().count(r.session()->sketchId()));
    r.mgr.run("sketch.finish");
    QVERIFY(r.viewer.previews().empty());
  }
};

QTEST_GUILESS_MAIN(ToolsTest)
#include "test_tools.moc"
