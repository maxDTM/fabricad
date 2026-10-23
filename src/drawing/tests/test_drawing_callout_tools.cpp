// Tests for the round-2 drawing tools (hole / thread callouts, GD&T, datum, surface texture,
// ordinate / baseline / chain dimensions, break view, stepped / aligned sections, revision
// table, annotation and tolerance editing), driven through the ToolManager and the canvas'
// simulate* input path (offscreen).
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingModule.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "drawing/Sheet.h"
#include "drawing/SheetRenderer.h"
#include "drawing/tools/CalloutTools.h"
#include "support/HoleTestFeatures.h"

#include <QtTest/QtTest>

#include <cmath>
#include <memory>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

std::string plain(const std::vector<RichLine>& lines) {
  std::string s;
  for (const auto& l : lines) s += richLinePlainText(l) + "|";
  return s;
}

std::string pt(const Point2& p) { return std::to_string(p.x) + "," + std::to_string(p.y); }

/// Hole block, an A3 sheet with a top view at (150, 130), scale 1; canvas + tool manager.
struct Fixture {
  core::Document doc;
  core::ToolRegistry tools;
  core::NullAppHost host;
  std::unique_ptr<core::ToolManager> manager;
  std::unique_ptr<DrawingWorkspaceWidget> widget;
  DrawingModel* model = nullptr;
  DrawingCanvas* canvas = nullptr;
  core::ObjectId viewId = core::kInvalidId;

  Fixture() {
    doc.addFeature(std::make_unique<test::HoleBlockFeature>());
    doc.recompute();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    core::ModuleContext mctx{core::FeatureFactory::instance(), tools};
    drawing::registerModule(mctx);
    manager = std::make_unique<core::ToolManager>(tools);
    manager->setDocument(&doc);
    host.setWorkspace(core::Workspace::Drawing);
    manager->setHost(&host);
    widget = std::make_unique<DrawingWorkspaceWidget>();
    widget->resize(1200, 800);
    widget->show();
    widget->setDocument(&doc);
    widget->setToolManager(manager.get());
    canvas = widget->canvas();
    canvas->setPickPixels(6);
    manager->run("drawing.create", {"A3", "ISO"});
    model->edit("Base view", &doc.undoStack(), [&]() {
      viewId = model->addBaseView(*model->activeSheet(), {}, ViewOrientation::Top, 1.0, {150, 130}).id;
    });
    model->updateGeometry();
    canvas->zoomToFit();
  }
  ~Fixture() {
    if (manager && manager->isActive()) manager->cancel();
  }
  Sheet& sheet() { return *model->activeSheet(); }
  const DrawingView& view() { return *model->view(viewId); }
  /// Sheet point of a world XY position on the top face.
  Point2 at(double x, double y) {
    gp_Pnt p(x, y, 20);
    return view().toSheet(worldToView(view().frame, p));
  }
  /// Sheet point on the rim of the hole at world (x, y) with radius r (east side).
  Point2 rim(double x, double y, double r) { return at(x + r, y); }
  CanvasHit hit(const Point2& p) { return canvas->hitTest(p, false); }
};
}  // namespace

class CalloutToolsTest : public QObject {
  Q_OBJECT
 private slots:
  void registration() {
    Fixture fx;
    for (const char* id : {"drawing.hole-callout", "drawing.thread-callout", "drawing.feature-control-frame", "drawing.datum",
                           "drawing.surface-texture", "drawing.dimension-ordinate", "drawing.dimension-baseline",
                           "drawing.dimension-chain", "drawing.break-view", "drawing.section-view-stepped",
                           "drawing.section-view-aligned", "drawing.revision-table", "drawing.revision",
                           "drawing.edit-callout"}) {
      QVERIFY2(fx.tools.tool(id), id);
      QCOMPARE(fx.tools.tool(id)->info().workspace, core::Workspace::Drawing);
    }
    QVERIFY(fx.tools.tool("drawing.edit-callout")->info().hidden);
    QVERIFY(fx.tools.byAlias("FCF"));
    QVERIFY(fx.view().geometryValid);
  }

  void holeCalloutWithCanvas() {
    Fixture fx;
    QVERIFY(fx.manager->run("drawing.hole-callout"));
    Point2 rim = fx.rim(15, 20, 3.3);
    fx.canvas->simulateMove(rim);
    fx.canvas->simulateClick(rim);
    Point2 place = rim + Point2{20, -25};
    fx.canvas->simulateMove(place);
    fx.canvas->simulateClick(place);
    QVERIFY(fx.manager->isActive());  // stays active for the next hole (Fusion behaviour)
    fx.manager->cancel();
    auto& calls = fx.sheet().callouts.holeCallouts;
    QCOMPARE(calls.size(), size_t(1));
    QCOMPARE(plain(calls[0].lines), std::string("\xC3\x98" "6.6 DEEP 15|CBORE  \xC3\x98" "11 DEEP 6|"));
    QVERIFY(near(calls[0].position.x, place.x, 1e-9));
    // it is a canvas object: hit, drag, delete-able, undoable
    core::ObjectId id = calls[0].id;
    Point2 onText = place + Point2{3.0, 0};
    CanvasHit h = fx.hit(onText);
    QCOMPARE(h.kind, HitKind::Callout);
    QCOMPARE(h.id, id);
    QCOMPARE(h.viewId, fx.viewId);
    fx.canvas->simulateDrag(onText, onText + Point2{5, 5});
    QVERIFY(near(fx.sheet().callouts.holeCallout(id)->position.x, place.x + 5, 1e-6));
    fx.doc.undoStack().undo();  // the drag
    QVERIFY(near(fx.sheet().callouts.holeCallout(id)->position.x, place.x, 1e-6));
    fx.doc.undoStack().undo();  // the creation
    QVERIFY(fx.sheet().callouts.holeCallouts.empty());
  }

  void threadCalloutFromArgs() {
    Fixture fx;
    QVERIFY(fx.manager->run("drawing.thread-callout", {pt(fx.rim(50, 20, 2)), pt(fx.at(70, 50))}));
    fx.manager->cancel();
    auto& calls = fx.sheet().callouts.holeCallouts;
    QCOMPARE(calls.size(), size_t(1));
    QVERIFY(calls[0].thread);
    QCOMPARE(plain(calls[0].lines), std::string("M4x0.7 - 6H DEEP 8|\xC3\x98" "4 DEEP 8|"));
    // edit: designation and count
    fx.canvas->setSelection({calls[0].id});
    QVERIFY(fx.manager->run("drawing.edit-callout", {"thread=M4x0.5", "count=2"}));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(plain(fx.sheet().callouts.holeCallouts[0].lines), std::string("2x M4x0.5 - 6H DEEP 8|\xC3\x98" "4 DEEP 8|"));
  }

  void gdtSymbols() {
    Fixture fx;
    Point2 edge = fx.at(30, 40);  // top (back) edge of the block
    QVERIFY(fx.manager->run("drawing.feature-control-frame", {"row=position dia 0.1 M A B", pt(edge), "220,60"}));
    fx.manager->cancel();
    auto& frames = fx.sheet().callouts.featureFrames;
    QCOMPARE(frames.size(), size_t(1));
    QCOMPARE(frames[0].rows[0].characteristic, GdtCharacteristic::Position);
    QCOMPARE(frames[0].rows[0].datums.size(), size_t(2));
    QCOMPARE(frames[0].anchor.viewId, fx.viewId);
    QVERIFY(near(frames[0].anchor.resolve(fx.sheet()).x, edge.x, 1e-6));
    // datums get consecutive letters
    QVERIFY(fx.manager->run("drawing.datum", {pt(fx.at(0, 20)), pt(fx.at(-15, 20)), pt(fx.at(30, 0)), pt(fx.at(30, -15))}));
    fx.manager->cancel();
    auto& datums = fx.sheet().callouts.datums;
    QCOMPARE(datums.size(), size_t(2));
    QCOMPARE(datums[0].letter, std::string("A"));
    QCOMPARE(datums[1].letter, std::string("B"));
    QVERIFY(fx.manager->run("drawing.datum", {"letter=x", pt(fx.at(60, 20)), pt(fx.at(75, 20))}));
    fx.manager->cancel();
    QCOMPARE(datums.back().letter, std::string("X"));
    // surface texture with a leader
    QVERIFY(fx.manager->run("drawing.surface-texture", {"ra=1.6", "process=required", "lay=M", pt(edge), pt(edge + Point2{15, -20})}));
    fx.manager->cancel();
    auto& sf = fx.sheet().callouts.surfaceFinishes;
    QCOMPARE(sf.size(), size_t(1));
    QCOMPARE(sf[0].roughness, std::string("Ra 1.6"));
    QVERIFY(sf[0].leader);
    QCOMPARE(sf[0].lay, std::string("M"));
    // a symbol placed on the surface (click, then Enter) has no leader
    QVERIFY(fx.manager->run("drawing.surface-texture", {"roughness=Rz 6.3"}));
    fx.canvas->simulateClick(edge);
    fx.manager->accept();
    QCOMPARE(sf.size(), size_t(2));
    QVERIFY(!sf[1].leader);
    QCOMPARE(sf[1].roughness, std::string("Rz 6.3"));
    // the callouts are on the canvas and in the export display list
    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList all;
    r.addSheet(all, fx.sheet(), titleBlockValues(*fx.model, fx.sheet()));
    int found = 0;
    for (const auto& t : all.texts)
      if (t.text == "Ra 1.6" || t.text == "Rz 6.3" || t.text == "X") ++found;
    QCOMPARE(found, 3);
  }

  void ordinateTool() {
    Fixture fx;
    QVERIFY(fx.manager->run("drawing.dimension-ordinate"));
    Point2 corner = fx.at(0, 0);
    fx.canvas->simulateClick(corner);
    fx.canvas->simulateClick(corner + Point2{0, 25});  // below: X ordinates
    for (double x : {15.0, 35.0, 50.0}) fx.canvas->simulateClick(fx.rim(x, 20, x == 15 ? 3.3 : (x == 35 ? 2.5 : 2.0)));
    fx.canvas->simulateKey(Qt::Key_Return);
    QVERIFY(!fx.manager->isActive());
    auto& ords = fx.sheet().callouts.ordinates;
    QCOMPARE(ords.size(), size_t(1));
    QCOMPARE(ords[0].axis, OrdinateAxis::X);
    QCOMPARE(ords[0].members.size(), size_t(3));
    QVERIFY(near(ords[0].members[0].value, 15) && near(ords[0].members[1].value, 35) && near(ords[0].members[2].value, 50));
    for (const auto& m : ords[0].members) QVERIFY(near(m.textPos.y, ords[0].originText.y));
    // undo removes the last member only
    fx.doc.undoStack().undo();
    QCOMPARE(fx.sheet().callouts.ordinates[0].members.size(), size_t(2));
  }

  void baselineAndChainTools() {
    Fixture fx;
    // base: corner to the counterbored hole (15 mm)
    const Vertex2D* corner = nullptr;
    for (const auto& v : fx.view().vertices)
      if (!corner || v.p.x + v.p.y < corner->p.x + corner->p.y) corner = &v;
    const Curve2D* hole = nullptr;
    for (const auto& c : fx.view().curves)
      if (c.isCircle() && near(c.radius, 3.3)) hole = &c;
    QVERIFY(corner && hole);
    Dimension d;
    d.viewId = fx.viewId;
    d.type = DimensionType::Horizontal;
    d.refs = {DimRef::toVertex(*corner), DimRef::toCurve(*hole)};
    d.textPos = {fx.at(7.5, 0).x, fx.view().sheetBBox().max.y + 10};
    core::ObjectId base = fx.model->addDimension(fx.sheet(), d).id;
    QVERIFY(near(fx.sheet().dimension(base)->value, 15));

    fx.canvas->setSelection({base});
    QVERIFY(fx.manager->run("drawing.dimension-baseline", {"spacing=7"}));
    fx.canvas->simulateClick(fx.rim(35, 20, 2.5));
    fx.canvas->simulateClick(fx.rim(50, 20, 2.0));
    fx.manager->accept();
    QVERIFY(!fx.manager->isActive());
    auto& dims = fx.sheet().dimensions;
    QCOMPARE(dims.size(), size_t(3));
    QVERIFY(near(dims[1].value, 35) && near(dims[2].value, 50));
    QVERIFY(near(dims[1].textPos.y, d.textPos.y + 7) && near(dims[2].textPos.y, d.textPos.y + 14));

    // chain from the same base: 20 then 15
    fx.canvas->setSelection({base});
    QVERIFY(fx.manager->run("drawing.dimension-chain"));
    fx.canvas->simulateClick(fx.rim(35, 20, 2.5));
    fx.canvas->simulateClick(fx.rim(50, 20, 2.0));
    fx.manager->accept();
    QCOMPARE(fx.sheet().dimensions.size(), size_t(5));
    QVERIFY(near(fx.sheet().dimensions[3].value, 20) && near(fx.sheet().dimensions[4].value, 15));
    QVERIFY(near(fx.sheet().dimensions[3].textPos.y, d.textPos.y));

    // tolerance / fit through the edit-dimension tool
    QVERIFY(fx.manager->run("drawing.edit-dimension", {std::to_string(base), "fit=H7", "fitdisplay=Deviations"}));
    const Dimension* bd = fx.sheet().dimension(base);
    QCOMPARE(bd->tolerance.mode, ToleranceMode::Fit);
    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList l;
    r.addDimension(l, fx.view(), *bd);
    bool dev = false;
    for (const auto& t : l.texts)
      if (t.text == "+0.018") dev = true;  // H7 for 10..18 mm
    QVERIFY(dev);
    QVERIFY(fx.manager->run("drawing.edit-dimension", {std::to_string(base), "pm=0.05"}));
    QCOMPARE(fx.sheet().dimension(base)->tolerance.mode, ToleranceMode::Symmetric);
    QVERIFY(near(fx.sheet().dimension(base)->tolerance.upper, 0.05));
  }

  void breakViewTool() {
    Fixture fx;
    fx.canvas->setSelection({fx.viewId});
    QVERIFY(fx.manager->run("drawing.break-view", {"gap=5", pt(fx.at(20, 30)), pt(fx.at(40, 30))}));
    QVERIFY(!fx.manager->isActive());
    const DrawingView& v = fx.view();
    QCOMPARE(v.breaks.size(), size_t(1));
    QCOMPARE(v.breaks[0].orientation, BreakOrientation::Horizontal);
    QVERIFY(near(v.breaks[0].to - v.breaks[0].from, 20, 1e-6));
    QVERIFY(near(v.breakGap, 5));
    QVERIFY(near(v.sheetBBox().width(), 60 - 20 + 5, 1e-3));
    fx.doc.undoStack().undo();
    QVERIFY(fx.view().breaks.empty());
  }

  void complexSectionTools() {
    Fixture fx;
    // stepped: through the counterbore at y = 20, step down to y = 10, out
    std::vector<std::string> args = {pt(fx.at(-5, 20)), pt(fx.at(25, 20)), pt(fx.at(25, 10)), pt(fx.at(65, 10)), "done",
                                     pt(fx.at(30, -40))};
    QVERIFY(fx.manager->run("drawing.section-view-stepped", args));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().views.size(), size_t(2));
    const DrawingView& sec = fx.sheet().views.back();
    QCOMPARE(sec.type, ViewType::Section);
    QCOMPARE(sec.section.points.size(), size_t(4));
    QVERIFY(!sec.section.aligned);
    const std::string secLabel = sec.label;  // `sec` dangles once the next view is added
    fx.model->updateGeometry();
    QVERIFY2(fx.sheet().views.back().geometryValid, fx.sheet().views.back().error.c_str());
    // aligned: arm through the countersink, bend at the counterbore, arm upwards
    args = {pt(fx.at(70, 20)), pt(fx.at(15, 20)), pt(fx.at(15, 45)), pt(fx.at(30, -50))};
    QVERIFY(fx.manager->run("drawing.section-view-aligned", args));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().views.size(), size_t(3));
    const DrawingView& al = fx.sheet().views.back();
    QVERIFY(al.section.aligned);
    QCOMPARE(al.section.points.size(), size_t(3));
    fx.model->updateGeometry();
    QVERIFY2(fx.sheet().views.back().geometryValid, fx.sheet().views.back().error.c_str());
    QVERIFY(fx.sheet().views.back().label != secLabel);
  }

  void revisionTools() {
    Fixture fx;
    QVERIFY(fx.manager->run("drawing.revision-table", {"done"}));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().callouts.revisionTables.size(), size_t(1));
    QVERIFY(fx.manager->run("drawing.revision", {"description=Initial release", "approved=QA", "date=2026-10-01"}));
    QVERIFY(fx.manager->run("drawing.revision", {"Hole depth changed"}));
    const RevisionTable& t = fx.sheet().callouts.revisionTables.front();
    QCOMPARE(t.rows.size(), size_t(2));
    QCOMPARE(t.rows[0].rev, std::string("A"));
    QCOMPARE(t.rows[1].rev, std::string("B"));
    QCOMPARE(t.rows[1].description, std::string("Hole depth changed"));
    QVERIFY(!t.rows[1].date.empty());
    QCOMPARE(titleBlockValues(*fx.model, fx.sheet())["Rev"], std::string("B"));
    // a second table is refused
    QVERIFY(fx.manager->run("drawing.revision-table", {"done"}));
    QCOMPARE(fx.sheet().callouts.revisionTables.size(), size_t(1));
    // editing the last row
    fx.canvas->setSelection({t.id});
    QVERIFY(fx.manager->run("drawing.edit-callout", {"approved=MD"}));
    QCOMPARE(fx.sheet().callouts.revisionTables.front().rows[1].approved, std::string("MD"));
  }
};

QTEST_MAIN(CalloutToolsTest)
#include "test_drawing_callout_tools.moc"
