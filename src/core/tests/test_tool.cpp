#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "support/TestFeatures.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace fabricad::core;

namespace {

/// Two-click "line" tool: records clicks, shows a preview, finishes on accept.
class FakeLineTool : public ToolBase {
 public:
  FakeLineTool()
      : ToolBase(ToolInfo{.id = "test.line", .name = "Line", .tooltip = "Draw a line", .shortcut = "L",
                          .aliases = {"LINE", "L"}, .tab = "SKETCH", .group = "CREATE", .order = 1, .promoted = true}) {}
  int activations = 0, deactivations = 0, presses = 0, moves = 0, selChanges = 0;
  bool accepted = false;
  std::vector<std::string> args;
  void activate(ToolContext& ctx) override {
    ++activations;
    args = ctx.args;
    ctx.setPrompt("Specify first point");
    ctx.selection->setFilter(ShapeKind::SketchPoint | ShapeKind::SketchCurve);
    ctx.viewer->setSelectionFilter(ctx.selection->filter());
  }
  void deactivate(ToolContext&) override { ++deactivations; }
  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    ++presses;
    ctx.viewer->showPreviewPoints({e.planePoint}, PreviewStyle::sketchLine());
    if (presses == 2) ctx.finish();
    return true;
  }
  bool mouseMove(ToolContext&, const ViewerMouseEvent&) override { ++moves; return true; }
  void selectionChanged(ToolContext&) override { ++selChanges; }
  void accept(ToolContext& ctx) override { accepted = true; ctx.finish(); }
  bool commandLineInput(ToolContext&, const std::string& text) override { return text.rfind("@", 0) == 0; }
};

class ExtrudeTool : public ToolBase {
 public:
  ExtrudeTool() : ToolBase(ToolInfo{.id = "part.extrude", .name = "Extrude", .shortcut = "E", .aliases = {"EXT", "EXTRUDE"},
                                     .tab = "SOLID", .group = "CREATE", .order = 3, .promoted = true}) {}
  void activate(ToolContext&) override {}
};

}  // namespace

class ToolTest : public QObject {
  Q_OBJECT
 private slots:
  void registryQueries() {
    ToolRegistry reg;
    QVERIFY(reg.registerTool(std::make_unique<FakeLineTool>()));
    QVERIFY(reg.registerTool(std::make_unique<ExtrudeTool>()));
    QVERIFY(!reg.registerTool(std::make_unique<ExtrudeTool>()));  // replaced, returns false
    reg.registerTool(std::make_unique<LambdaTool>(ToolInfo{.id = "view.fit", .name = "Fit", .shortcut = "F6", .aliases = {"ZOOM"},
                                                           .tab = "", .group = "", .needsDocument = false},
                                                  [](ToolContext&) {}));
    QCOMPARE(reg.tools().size(), size_t(3));
    QVERIFY(reg.tool("part.extrude"));
    QVERIFY(!reg.tool("nope"));
    auto tabs = reg.tabs(Workspace::Design);
    QCOMPARE(tabs.size(), size_t(2));
    QCOMPARE(reg.toolsFor(Workspace::Design, "SOLID", "CREATE").size(), size_t(1));
    QCOMPARE(reg.groups(Workspace::Design, "SKETCH"), std::vector<std::string>{"CREATE"});
    QCOMPARE(reg.byAlias("ext")->info().id, std::string("part.extrude"));
    QCOMPARE(reg.byAlias("zoom")->info().id, std::string("view.fit"));
    QCOMPARE(reg.byShortcut("e")->info().id, std::string("part.extrude"));
    auto found = reg.search("ex");
    QVERIFY(!found.empty());
    QCOMPARE(found[0]->info().id, std::string("part.extrude"));
    found = reg.search("ln");  // subsequence of "line"
    QVERIFY(!found.empty());
    QCOMPARE(found[0]->info().id, std::string("test.line"));
    QVERIFY(reg.search("zzz").empty());
  }

  void managerLifecycle() {
    fabricad::core::test::registerTestFeatures();
    ToolRegistry reg;
    auto* line = new FakeLineTool();
    reg.registerTool(std::unique_ptr<ITool>(line));
    int fits = 0;
    reg.registerTool(std::make_unique<LambdaTool>(ToolInfo{.id = "view.fit", .name = "Fit", .repeatable = false, .needsDocument = false},
                                                  [&](ToolContext& ctx) { ++fits; ctx.viewer->fitAll(); }));
    Document doc;
    NullViewerContext viewer;
    NullAppHost host;
    ToolManager mgr(reg);
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);
    QSignalSpy activated(&mgr, &ToolManager::toolActivated);
    QSignalSpy finished(&mgr, &ToolManager::toolFinished);

    // instant tool
    QVERIFY(mgr.run("view.fit"));
    QCOMPARE(fits, 1);
    QVERIFY(!mgr.isActive());
    QCOMPARE(viewer.fitCount(), 1);
    QCOMPARE(finished.count(), 1);
    QVERIFY(mgr.lastToolId().empty());  // not repeatable

    // interactive tool
    QVERIFY(mgr.run("test.line", {"10"}));
    QVERIFY(mgr.isActive());
    QCOMPARE(mgr.activeToolId(), std::string("test.line"));
    QCOMPARE(line->args, std::vector<std::string>{"10"});
    QCOMPARE(host.prompt(), std::string("Specify first point"));
    QVERIFY(doc.selection().filter() != kAllKinds);
    QCOMPARE(host.history().back(), std::string("test.line"));
    ViewerMouseEvent e;
    e.button = MouseButton::Left;
    e.hasPlanePoint = true;
    e.planePoint = {1, 2, 0};
    QVERIFY(mgr.mouseMove(e));
    QVERIFY(mgr.mousePress(e));
    QCOMPARE(viewer.previews().size(), size_t(1));
    doc.selection().set(ShapeRef::sketchCurve(5, 1));
    QCOMPARE(line->selChanges, 1);
    // typed input routed to the active tool
    QVERIFY(mgr.runCommandLine("@10<30"));
    QVERIFY(mgr.mousePress(e));  // second click finishes
    QVERIFY(!mgr.isActive());
    QCOMPARE(line->deactivations, 1);
    QVERIFY(viewer.previews().empty());  // cleared on finish
    QCOMPARE(doc.selection().filter(), kAllKinds);
    QCOMPARE(viewer.selectionFilter(), kAllKinds);
    QCOMPARE(mgr.lastToolId(), std::string("test.line"));
    QCOMPARE(finished.count(), 2);
    QVERIFY(finished.last()[1].toBool());

    // repeat with Enter, then Esc cancels
    QVERIFY(mgr.repeatLast());
    QCOMPARE(line->activations, 2);
    QCOMPARE(line->args, std::vector<std::string>{"10"});
    mgr.cancel();
    QVERIFY(!mgr.isActive());
    QVERIFY(!finished.last()[1].toBool());

    // command line: alias + args; empty line repeats
    QVERIFY(mgr.runCommandLine("LINE 5 6"));
    QCOMPARE(line->args, (std::vector<std::string>{"5", "6"}));
    mgr.accept();
    QVERIFY(line->accepted);
    QVERIFY(!mgr.isActive());
    QVERIFY(mgr.runCommandLine(""));
    QCOMPARE(line->activations, 4);
    mgr.cancel();
    QVERIFY(!mgr.runCommandLine("BOGUS"));
    QVERIFY(!host.messages().empty());

    // shortcuts prefer SKETCH tools only in sketch mode
    QVERIFY(mgr.triggerShortcut("L"));
    mgr.cancel();
    QVERIFY(!mgr.triggerShortcut("Q"));
    // running another tool cancels the active one
    QVERIFY(mgr.run("test.line"));
    QVERIFY(mgr.run("view.fit"));
    QVERIFY(!mgr.isActive());
    QCOMPARE(activated.count() >= 6, true);
  }

  void nullViewerMapping() {
    NullViewerContext v;
    v.enterSketchMode(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
    QVERIFY(v.inSketchMode());
    QVERIFY(v.isGridVisible());
    gp_Pnt p;
    QVERIFY(v.screenToPlane(10, 20, gp_Pln(v.sketchPlane()), p));
    QVERIFY(std::abs(p.X() - 10) < 1e-9 && std::abs(p.Y() + 20) < 1e-9 && std::abs(p.Z()) < 1e-9);
    double x, y;
    QVERIFY(v.worldToScreen(p, x, y));
    QVERIFY(std::abs(x - 10) < 1e-9 && std::abs(y - 20) < 1e-9);
    v.setStandardView(StandardView::Front, false);
    QVERIFY(v.viewDirection().Y() < -0.99);
    v.exitSketchMode();
    QVERIFY(!v.inSketchMode());
    PreviewHandle h = v.showPreviewLines({{0, 0, 0}, {1, 0, 0}}, PreviewStyle::inference());
    v.updatePreviewLines(h, {{0, 0, 0}, {2, 0, 0}});
    QCOMPARE(v.previews().at(h).points[1].x, 2.0);
    v.removePreview(h);
    QVERIFY(v.previews().empty());
  }
};

QTEST_GUILESS_MAIN(ToolTest)
#include "test_tool.moc"
