// Tests for the drawing tools, the QGraphicsView canvas and the workspace widget, driven
// headless (QT_QPA_PLATFORM=offscreen) through the ToolManager and the canvas' simulate*
// input path. The model runs HLR synchronously so results are deterministic.
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingModule.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "drawing/Sheet.h"
#include "drawing/tools/AnnotationTools.h"
#include "drawing/tools/DrawingTools.h"
#include "drawing/tools/ViewTools.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>
#include <memory>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {

#define QVERIFY_RETURN(cond, ret) \
  do { if (!(cond)) { QTest::qFail(#cond, __FILE__, __LINE__); return ret; } } while (false)

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

int countClass(const std::vector<Curve2D>& cs, LineClass cls) {
  int n = 0;
  for (const auto& c : cs)
    if (c.cls == cls) ++n;
  return n;
}

/// Box 10 x 20 x 30 with a vertical hole (r = 2 at x=5, y=5), a sheet, tools, manager, widget.
struct Fixture {
  core::Document doc;
  core::ToolRegistry tools;
  core::NullAppHost host;                       // must outlive the manager (it talks to the host on shutdown)
  std::unique_ptr<core::ToolManager> manager;
  std::unique_ptr<DrawingWorkspaceWidget> widget;
  DrawingModel* model = nullptr;
  DrawingCanvas* canvas = nullptr;
  core::ObjectId bodyId = core::kInvalidId;

  explicit Fixture(bool withHole = true, bool withWidget = true) {
    core::test::registerTestFeatures();
    core::Feature* box = doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    bodyId = doc.liveBodies().front();
    if (withHole) {
      auto hole = std::make_unique<core::test::HoleFeature>();
      hole->prop("body").setRef(core::ShapeRef::body(bodyId));
      doc.addFeature(std::move(hole));
      doc.recompute();
    }
    (void)box;
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    core::ModuleContext mctx{core::FeatureFactory::instance(), tools};
    drawing::registerModule(mctx);
    manager = std::make_unique<core::ToolManager>(tools);
    manager->setDocument(&doc);
    host.setWorkspace(core::Workspace::Drawing);
    manager->setHost(&host);
    if (withWidget) {
      widget = std::make_unique<DrawingWorkspaceWidget>();
      widget->resize(1200, 800);
      widget->show();  // offscreen platform: gives the canvas a real viewport size
      widget->setDocument(&doc);
      widget->setToolManager(manager.get());
      canvas = widget->canvas();
      canvas->setPickPixels(6);
      canvas->zoomToFit();
    }
  }

  ~Fixture() {
    if (manager && manager->isActive()) manager->cancel();
  }

  Sheet& sheet() { return *model->activeSheet(); }

  /// Front base view of the box at (150, 120), scale 1 -> 10 wide x 30 tall on the sheet.
  core::ObjectId makeBaseView() {
    QVERIFY_RETURN(manager->run("drawing.create", {"A3", "ISO"}), core::kInvalidId);
    QVERIFY_RETURN(manager->run("drawing.base-view"), core::kInvalidId);
    canvas->simulateMove({150, 120});
    canvas->simulateClick({150, 120});
    QVERIFY_RETURN(!manager->isActive(), core::kInvalidId);
    QVERIFY_RETURN(sheet().views.size() == 1, core::kInvalidId);
    return sheet().views.front().id;
  }

};

}  // namespace

class ToolsTest : public QObject {
  Q_OBJECT
 private slots:
  void registration() {
    Fixture fx(false, false);
    auto drawingTools = fx.tools.toolsFor(core::Workspace::Drawing);
    QVERIFY(drawingTools.size() >= 25);
    for (const char* id : {"drawing.create", "drawing.new-sheet", "drawing.base-view", "drawing.projected-view",
                           "drawing.section-view", "drawing.detail-view", "drawing.move", "drawing.delete",
                           "drawing.edit-view", "drawing.edit-sheet", "drawing.edit-title-block",
                           "drawing.toggle-hidden-lines", "drawing.toggle-tangent-edges", "drawing.center-mark",
                           "drawing.centerline", "drawing.dimension", "drawing.edit-dimension", "drawing.text",
                           "drawing.leader", "drawing.parts-list", "drawing.balloon", "drawing.export-pdf",
                           "drawing.export-svg", "drawing.export-dxf", "drawing.export-csv"})
      QVERIFY2(fx.tools.has(id), id);
    auto tabs = fx.tools.tabs(core::Workspace::Drawing);
    QCOMPARE(tabs.size(), size_t(1));
    QCOMPARE(tabs[0], std::string("DRAWING"));
    auto groups = fx.tools.groups(core::Workspace::Drawing, "DRAWING");
    for (const char* g : {"CREATE", "MODIFY", "GEOMETRY", "DIMENSIONS", "TEXT", "TABLES", "EXPORT"})
      QVERIFY2(std::find(groups.begin(), groups.end(), g) != groups.end(), g);
    QCOMPARE(fx.tools.byShortcut("D")->info().id, std::string("drawing.dimension"));
    QCOMPARE(fx.tools.byShortcut("P")->info().id, std::string("drawing.projected-view"));
    QCOMPARE(fx.tools.byAlias("BOM")->info().id, std::string("drawing.parts-list"));
    // drawing tools are disabled in the Design workspace, except "New Drawing"
    fx.host.setWorkspace(core::Workspace::Design);
    core::ToolContext ctx = fx.manager->makeContext();
    QVERIFY(!fx.tools.tool("drawing.base-view")->isEnabled(ctx));
    QVERIFY(fx.tools.tool("drawing.create")->isEnabled(ctx));
  }

  void newDrawingAndBaseView() {
    Fixture fx;
    QVERIFY(fx.model->sheets().empty());
    QVERIFY(fx.manager->run("drawing.create", {"A4", "ASME", "1:2"}));
    QVERIFY(!fx.manager->isActive());  // no dialog shell -> committed immediately
    QCOMPARE(fx.model->sheets().size(), size_t(1));
    QCOMPARE(fx.sheet().format.name, std::string("A4"));
    QCOMPARE(fx.sheet().standard, ProjectionStandard::ThirdAngle);
    QVERIFY(near(fx.sheet().scale, 0.5));
    QCOMPARE(fx.host.workspace(), core::Workspace::Drawing);
    QCOMPARE(fx.widget->activeSheetId(), fx.sheet().id);

    // interactive base view: follows the cursor, click places and (without a dialog) commits
    QVERIFY(fx.manager->run("drawing.base-view", {"top", "1:1"}));
    QVERIFY(fx.manager->isActive());
    QVERIFY(fx.canvas->interaction() != nullptr);
    fx.canvas->simulateMove({120, 100});
    fx.canvas->simulateClick({120, 100});
    QVERIFY(!fx.manager->isActive());
    QVERIFY(fx.canvas->interaction() == nullptr);
    QCOMPARE(fx.sheet().views.size(), size_t(1));
    const DrawingView& v = fx.sheet().views.front();
    QCOMPARE(v.type, ViewType::Base);
    QCOMPARE(v.orientation, ViewOrientation::Top);
    QVERIFY(near(v.scale, 1.0));
    QVERIFY(near(v.position.x, 120) && near(v.position.y, 100));
    QVERIFY(v.geometryValid);
    QVERIFY(near(v.bbox.width(), 10.0, 1e-6) && near(v.bbox.height(), 20.0, 1e-6));
    QCOMPARE(countClass(v.curves, LineClass::Visible), 5);  // 4 edges + the hole circle
    // the canvas selected the new view and mirrored it into the document selection
    QCOMPARE(fx.canvas->selection().size(), size_t(1));
    QCOMPARE(fx.doc.selection().count(), 1);
    QCOMPARE(fx.doc.selection().first()->kind, core::ShapeKind::DrawingView);
    // undo removes the view, redo brings it back
    QVERIFY(fx.doc.undoStack().canUndo());
    fx.doc.undoStack().undo();
    QVERIFY(fx.sheet().views.empty());
    fx.doc.undoStack().redo();
    QCOMPARE(fx.sheet().views.size(), size_t(1));
    QVERIFY(fx.sheet().views.front().geometryValid);
  }

  void baseViewHeadlessAndCommandLine() {
    Fixture fx(true, false);  // no widget at all
    QVERIFY(fx.manager->run("drawing.base-view", {"front", "2:1", "hidden=on"}));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.model->sheets().size(), size_t(1));  // sheet created on demand
    const DrawingView& v = fx.sheet().views.front();
    QCOMPARE(v.orientation, ViewOrientation::Front);
    QVERIFY(near(v.scale, 2.0));
    QVERIFY(v.hiddenLines);
    QVERIFY(v.geometryValid);
    QVERIFY(countClass(v.curves, LineClass::Hidden) > 0);  // the hole shows as hidden lines
    // command box alias
    QVERIFY(fx.manager->runCommandLine("BASEVIEW right 200,150"));
    QCOMPARE(fx.sheet().views.size(), size_t(2));
    QVERIFY(near(fx.sheet().views[1].position.x, 200));
  }

  void projectedViews() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    QVERIFY(baseId != core::kInvalidId);
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.projected-view"));
    auto* tool = dynamic_cast<ProjectedViewTool*>(fx.manager->activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->parentId(), baseId);
    fx.canvas->simulateMove({240, 130});   // right of the parent
    fx.canvas->simulateClick({240, 130});
    QCOMPARE(tool->pendingCount(), 1);
    fx.canvas->simulateMove({150, 40});    // above
    fx.canvas->simulateClick({150, 40});
    fx.canvas->simulateMove({260, 40});    // diagonal -> iso
    fx.canvas->simulateClick({260, 40});
    QCOMPARE(tool->pendingCount(), 3);
    fx.manager->accept();
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().views.size(), size_t(4));
    const DrawingView& right = fx.sheet().views[1];
    QCOMPARE(right.type, ViewType::Projected);
    QCOMPARE(right.parent, baseId);
    QCOMPARE(right.alignment, ViewAlignment::Horizontal);
    QVERIFY(near(right.position.y, 120));   // aligned with the parent
    QVERIFY(near(right.position.x, 240));
    QVERIFY(right.geometryValid);
    QVERIFY(near(right.bbox.width(), 20.0, 1e-6));  // ISO first angle: the view on the right shows the depth (20)
    const DrawingView& above = fx.sheet().views[2];
    QCOMPARE(above.alignment, ViewAlignment::Vertical);
    QVERIFY(near(above.position.x, 150));
    const DrawingView& iso = fx.sheet().views[3];
    QCOMPARE(iso.alignment, ViewAlignment::None);
    QVERIFY(iso.geometryValid);
    // all three views are one undo step
    fx.doc.undoStack().undo();
    QCOMPARE(fx.sheet().views.size(), size_t(1));
    fx.doc.undoStack().redo();
    QCOMPARE(fx.sheet().views.size(), size_t(4));
    // command line: directions as arguments with the parent selected
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.projected-view", {"below"}));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().views.size(), size_t(5));
    QCOMPARE(fx.sheet().views.back().direction, ProjectionDirection::Below);
  }

  void sectionView() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.section-view"));
    auto* tool = dynamic_cast<SectionViewTool*>(fx.manager->activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->step(), SectionViewTool::Step::FirstPoint);
    // vertical cut through the hole (x = 5 of the box -> view-local x = 0 -> sheet x = 150)
    fx.canvas->simulateMove({150, 100});
    fx.canvas->simulateClick({150, 100});
    QCOMPARE(tool->step(), SectionViewTool::Step::SecondPoint);
    fx.canvas->simulateMove({150.4, 140});  // ortho snap pulls x back to 150
    fx.canvas->simulateClick({150.4, 140});
    QCOMPARE(tool->step(), SectionViewTool::Step::Place);
    fx.canvas->simulateMove({230, 125});
    fx.canvas->simulateClick({230, 125});
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().views.size(), size_t(2));
    const DrawingView& sec = fx.sheet().views[1];
    QCOMPARE(sec.type, ViewType::Section);
    QCOMPARE(sec.label, std::string("A-A"));
    QCOMPARE(sec.alignment, ViewAlignment::Horizontal);
    QVERIFY(near(sec.position.y, 120));  // aligned with the parent across the cut
    QVERIFY(near(sec.section.p1.x, 0.0, 1e-6) && near(sec.section.p2.x, 0.0, 1e-6));
    QVERIFY2(sec.geometryValid, sec.error.c_str());
    QVERIFY(countClass(sec.curves, LineClass::Hatch) > 0);
    QVERIFY(countClass(sec.curves, LineClass::Section) > 0);
    // half the box remains: 10 deep (y) x 30 tall
    QVERIFY2(near(sec.bbox.height(), 30.0, 1e-6), qPrintable(QString::number(sec.bbox.height())));
    QVERIFY2(near(sec.bbox.width(), 20.0, 1e-6), qPrintable(QString::number(sec.bbox.width())));
    // the parent's display list carries the section line
    DisplayList dl = fx.canvas->displayList();
    QVERIFY(countClass(dl.curves, LineClass::Center) > 0);
  }

  void detailView() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.detail-view"));
    auto* tool = dynamic_cast<DetailViewTool*>(fx.manager->activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->step(), DetailViewTool::Step::Center);
    fx.canvas->simulateClick({150, 110});   // centre near the top of the view
    QCOMPARE(tool->step(), DetailViewTool::Step::Radius);
    fx.canvas->simulateMove({156, 110});
    fx.canvas->simulateClick({156, 110});   // radius 6 mm
    QCOMPARE(tool->step(), DetailViewTool::Step::Place);
    fx.canvas->simulateMove({250, 200});
    fx.canvas->simulateClick({250, 200});
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().views.size(), size_t(2));
    const DrawingView& det = fx.sheet().views[1];
    QCOMPARE(det.type, ViewType::Detail);
    QVERIFY(near(det.scale, 2.0));
    QVERIFY(near(det.detail.radius, 6.0, 1e-6));
    QCOMPARE(det.label, std::string("DETAIL A"));
    QVERIFY(det.geometryValid);
    QVERIFY(countClass(det.curves, LineClass::Detail) == 1);
    QVERIFY(countClass(det.curves, LineClass::Visible) > 0);
  }

  void dimensionTool() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    (void)baseId;
    QVERIFY(fx.manager->run("drawing.dimension"));
    auto* tool = dynamic_cast<DimensionTool*>(fx.manager->activeTool());
    QVERIFY(tool);
    // top edge of the front view: y = 120 - 15 = 105, x from 145 to 155
    fx.canvas->simulateMove({150, 105});
    fx.canvas->simulateClick({150, 105});
    QCOMPARE(tool->pickCount(), 1);
    fx.canvas->simulateMove({150, 90});
    fx.canvas->simulateClick({150, 90});    // place text above
    QCOMPARE(tool->pickCount(), 0);
    QCOMPARE(tool->createdCount(), 1);
    QVERIFY(fx.manager->isActive());        // stays active for the next dimension
    QCOMPARE(fx.sheet().dimensions.size(), size_t(1));
    const Dimension& d = fx.sheet().dimensions.front();
    QCOMPARE(d.type, DimensionType::Linear);
    QVERIFY(d.valid);
    QVERIFY2(near(d.value, 10.0, 1e-6), qPrintable(QString::number(d.value)));
    QVERIFY(near(d.textPos.y, 90));
    // two vertices -> distance (left-top and left-bottom corners => 30)
    fx.canvas->simulateClick({145, 105});
    QCOMPARE(tool->pickCount(), 1);
    fx.canvas->simulateClick({145, 135});
    QCOMPARE(tool->pickCount(), 2);
    fx.canvas->simulateClick({130, 120});
    QCOMPARE(fx.sheet().dimensions.size(), size_t(2));
    QVERIFY2(near(fx.sheet().dimensions[1].value, 30.0, 1e-6), qPrintable(QString::number(fx.sheet().dimensions[1].value)));
    // Esc with no picks ends the tool
    fx.canvas->simulateKey(Qt::Key_Escape);
    QVERIFY(!fx.manager->isActive());
    // the dimension is associative: a different box width changes the measured value
    fx.doc.featureAt(0)->prop("dx").setDouble(14.0);
    fx.doc.notifyFeatureChanged(fx.doc.featureAt(0)->id());
    fx.doc.recompute();
    QVERIFY(fx.model->waitForIdle());
    QVERIFY2(near(fx.sheet().dimensions[0].value, 14.0, 1e-6), qPrintable(QString::number(fx.sheet().dimensions[0].value)));
    // edit via arguments: text override + prefix
    fx.canvas->setSelection({fx.sheet().dimensions[0].id});
    QVERIFY(fx.manager->run("drawing.edit-dimension", {"prefix=2x ", "precision=1"}));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().dimensions[0].prefix, std::string("2x "));
    QCOMPARE(fx.sheet().dimensions[0].precision, 1);
    // undo of the edit restores
    fx.doc.undoStack().undo();
    QVERIFY(fx.sheet().dimensions[0].prefix.empty());
  }

  void centerMarksAndLines() {
    Fixture fx;
    QVERIFY(fx.manager->run("drawing.create"));
    QVERIFY(fx.manager->run("drawing.base-view", {"top"}));
    fx.canvas->simulateClick({150, 120});   // top view: 10 x 20, hole circle at local (0,-5)
    core::ObjectId viewId = fx.sheet().views.front().id;
    const DrawingView& v = fx.sheet().views.front();
    Point2 holeCentre = v.toSheet({0, -5});
    QVERIFY(fx.manager->run("drawing.center-mark"));
    fx.canvas->simulateMove(holeCentre + Point2{2, 0});  // on the circle (r = 2)
    fx.canvas->simulateClick(holeCentre + Point2{2, 0});
    QCOMPARE(fx.sheet().centerMarks.size(), size_t(1));
    QCOMPARE(fx.sheet().centerMarks.front().viewId, viewId);
    fx.canvas->simulateKey(Qt::Key_Escape);
    QVERIFY(!fx.manager->isActive());
    // layout: two crossing chain lines
    auto marks = layoutCenterMark(fx.sheet().centerMarks.front(), v);
    QCOMPARE(marks.size(), size_t(2));
    // auto centre marks: nothing new (the only circle already has one)
    fx.canvas->setSelection({viewId});
    QVERIFY(fx.manager->run("drawing.auto-center-marks"));
    QCOMPARE(fx.sheet().centerMarks.size(), size_t(1));
    // centre line between the left and right edges (x = 145 / 155 on the sheet)
    QVERIFY(fx.manager->run("drawing.centerline"));
    auto* cl = dynamic_cast<CenterLineTool*>(fx.manager->activeTool());
    QVERIFY(cl);
    fx.canvas->simulateClick({145, 118});
    QCOMPARE(cl->pickCount(), 1);
    fx.canvas->simulateClick({155, 118});
    QCOMPARE(cl->pickCount(), 0);
    QCOMPARE(fx.sheet().centerLines.size(), size_t(1));
    auto lines = layoutCenterLine(fx.sheet().centerLines.front(), v);
    QVERIFY(!lines.empty());
    QVERIFY(near(lines.front().a.x, 150, 1e-6));  // midway between the edges
    fx.manager->cancel();
  }

  void textAndLeader() {
    Fixture fx;
    fx.makeBaseView();
    // interactive: click position, type text, Enter
    QVERIFY(fx.manager->run("drawing.text"));
    fx.canvas->simulateClick({60, 60});
    QVERIFY(fx.manager->isActive());
    QVERIFY(fx.manager->runCommandLine("GENERAL NOTES"));  // goes to commandLineInput
    fx.manager->accept();
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().notes.size(), size_t(1));
    QCOMPARE(fx.sheet().notes.front().text, std::string("GENERAL NOTES"));
    QVERIFY(near(fx.sheet().notes.front().position.x, 60));
    // arguments: leader tip, text position, text
    QVERIFY(fx.manager->run("drawing.leader", {"150,105", "190,80", "TOP", "FACE"}));
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().notes.size(), size_t(2));
    QVERIFY(fx.sheet().notes[1].leader);
    QVERIFY(near(fx.sheet().notes[1].leaderTo.x, 150));
    QCOMPARE(fx.sheet().notes[1].text, std::string("TOP FACE"));
    // edit an existing note from the command line
    core::ObjectId noteId = fx.sheet().notes[0].id;
    QVERIFY(fx.manager->run("drawing.text", {"--edit", std::to_string(noteId)}));
    QVERIFY(fx.manager->isActive());
    QVERIFY(fx.manager->runCommandLine("NOTES"));
    fx.manager->accept();
    QCOMPARE(fx.sheet().notes[0].text, std::string("NOTES"));
    fx.doc.undoStack().undo();
    QCOMPARE(fx.sheet().notes[0].text, std::string("GENERAL NOTES"));
    // the canvas renders note items that can be hit
    CanvasHit h = fx.canvas->hitTest({61, 62}, false);
    QCOMPARE(h.kind, HitKind::Note);
    QCOMPARE(h.id, noteId);
  }

  void partsListAndBalloons() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.parts-list"));
    fx.canvas->simulateMove({250, 200});
    fx.canvas->simulateClick({250, 200});
    QVERIFY(!fx.manager->isActive());
    QCOMPARE(fx.sheet().partsLists.size(), size_t(1));
    const PartsList& pl = fx.sheet().partsLists.front();
    QCOMPARE(pl.rows.size(), size_t(1));  // one body
    QCOMPARE(pl.rows.front().item, 1);
    QVERIFY(near(pl.position.x, 250));
    // balloon: attach to the top edge, place above
    QVERIFY(fx.manager->run("drawing.balloon"));
    auto* bt = dynamic_cast<BalloonTool*>(fx.manager->activeTool());
    QVERIFY(bt);
    fx.canvas->simulateClick({150, 105});
    QVERIFY(bt->hasAttachment());
    fx.canvas->simulateMove({175, 80});
    fx.canvas->simulateClick({175, 80});
    QVERIFY(!bt->hasAttachment());
    QCOMPARE(fx.sheet().balloons.size(), size_t(1));
    QCOMPARE(fx.sheet().balloons.front().itemNumber, 1);
    QCOMPARE(fx.sheet().balloons.front().itemKey, fx.bodyId);
    fx.manager->cancel();
    // balloon-all adds nothing new for the already ballooned body
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.balloon-all"));
    QCOMPARE(fx.sheet().balloons.size(), size_t(1));
    // headless parts list goes above the title block
    Fixture fx2(true, false);
    QVERIFY(fx2.manager->run("drawing.base-view"));
    QVERIFY(fx2.manager->run("drawing.parts-list"));
    QCOMPARE(fx2.sheet().partsLists.size(), size_t(1));
    BBox2 tb = fx2.sheet().titleBlockRect();
    const PartsList& pl2 = fx2.sheet().partsLists.front();
    QVERIFY(near(pl2.position.y + pl2.height(), tb.min.y, 1e-6));
    QVERIFY(near(pl2.position.x + pl2.width(), tb.max.x, 1e-6));
  }

  void canvasSelectionDragAndDelete() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->clearSelection();
    // hit testing
    CanvasHit h = fx.canvas->hitTest({150, 105}, true);
    QCOMPARE(h.kind, HitKind::View);
    QCOMPARE(h.id, baseId);
    QVERIFY(h.onCurve);
    QVERIFY(h.curve.isLine());
    QVERIFY(near(h.viewLocal.y, 15.0, 0.5));
    CanvasHit none = fx.canvas->hitTest({40, 40}, true);
    QCOMPARE(none.kind, HitKind::None);
    CanvasHit tb = fx.canvas->hitTest({400, 280}, false);
    QCOMPARE(tb.kind, HitKind::TitleBlock);
    // click selects, shift-click toggles, click on empty clears
    fx.canvas->simulateClick({150, 120});
    QCOMPARE(fx.canvas->selection(), std::vector<core::ObjectId>{baseId});
    fx.canvas->simulateClick({150, 120}, core::ModShift);
    QVERIFY(fx.canvas->selection().empty());
    fx.canvas->simulateClick({150, 120});
    fx.canvas->simulateClick({40, 40});
    QVERIFY(fx.canvas->selection().empty());
    // rubber band
    fx.canvas->simulateDrag({100, 60}, {220, 180});
    QCOMPARE(fx.canvas->selection(), std::vector<core::ObjectId>{baseId});
    // drag moves the view (one undo step)
    int undoCount = fx.doc.undoStack().count();
    fx.canvas->simulateDrag({150, 120}, {180, 140});
    QVERIFY(near(fx.sheet().views.front().position.x, 180) && near(fx.sheet().views.front().position.y, 140));
    QCOMPARE(fx.doc.undoStack().count(), undoCount + 1);
    fx.doc.undoStack().undo();
    QVERIFY(near(fx.sheet().views.front().position.x, 150));
    // a projected child follows the parent and stays aligned
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.projected-view", {"right"}));
    core::ObjectId childId = fx.sheet().views[1].id;
    double childDx = fx.sheet().views[1].position.x - 150;
    fx.canvas->setSelection({baseId});
    fx.canvas->simulateDrag({150, 120}, {150, 150});
    QVERIFY(near(fx.sheet().views.front().position.y, 150));
    QVERIFY(near(fx.sheet().view(childId)->position.y, 150));
    QVERIFY(near(fx.sheet().view(childId)->position.x, 150 + childDx));
    // dragging an aligned child only moves it along its axis
    fx.canvas->setSelection({childId});
    Point2 cp = fx.sheet().view(childId)->position;
    fx.canvas->simulateDrag(cp, cp + Point2{10, 40});
    QVERIFY(near(fx.sheet().view(childId)->position.x, cp.x + 10));
    QVERIFY(near(fx.sheet().view(childId)->position.y, cp.y));
    // Delete key removes the selection through the delete tool
    fx.canvas->setSelection({childId});
    fx.canvas->simulateKey(Qt::Key_Delete);
    QCOMPARE(fx.sheet().views.size(), size_t(1));
    fx.doc.undoStack().undo();
    QCOMPARE(fx.sheet().views.size(), size_t(2));
    // move tool with explicit delta
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.move", {"5,0"}));
    QVERIFY(near(fx.sheet().views.front().position.x, 155));
  }

  void editViewSheetTitleBlock() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    int undoCount = fx.doc.undoStack().count();
    QVERIFY(fx.manager->run("drawing.edit-view", {"scale=1:2", "hidden=on", "tangent=full", "label=FRONT"}));
    QVERIFY(!fx.manager->isActive());
    const DrawingView* v = fx.sheet().view(baseId);
    QVERIFY(near(v->scale, 0.5));
    QVERIFY(v->hiddenLines);
    QCOMPARE(v->tangentEdges, TangentEdgeMode::Full);
    QCOMPARE(v->label, std::string("FRONT"));
    QVERIFY(v->geometryValid);
    QVERIFY(countClass(v->curves, LineClass::Hidden) > 0);
    QCOMPARE(fx.doc.undoStack().count(), undoCount + 1);
    fx.doc.undoStack().undo();
    QVERIFY(near(fx.sheet().view(baseId)->scale, 1.0));
    QVERIFY(!fx.sheet().view(baseId)->hiddenLines);
    // the dialog can be built offscreen
    fx.canvas->setSelection({baseId});
    fx.host.setWorkspace(core::Workspace::Drawing);
    QVERIFY(fx.manager->run("drawing.edit-view"));
    QVERIFY(!fx.manager->isActive());  // no dialog shell: commits immediately
    // toggles
    QVERIFY(fx.manager->run("drawing.toggle-hidden-lines"));
    QVERIFY(fx.sheet().view(baseId)->hiddenLines);
    QVERIFY(fx.manager->run("drawing.toggle-hidden-lines"));
    QVERIFY(!fx.sheet().view(baseId)->hiddenLines);
    // sheet settings
    QVERIFY(fx.manager->run("drawing.edit-sheet", {"name=Assembly", "size=A2", "standard=asme", "scale=1:5", "orientation=portrait"}));
    QCOMPARE(fx.sheet().name, std::string("Assembly"));
    QCOMPARE(fx.sheet().format.name, std::string("A2"));
    QVERIFY(!fx.sheet().format.landscape);
    QVERIFY(fx.sheet().format.height > fx.sheet().format.width);
    QCOMPARE(fx.sheet().standard, ProjectionStandard::ThirdAngle);
    QVERIFY(near(fx.sheet().scale, 0.2));
    // title block
    QVERIFY(fx.manager->run("drawing.edit-title-block", {"Title=Bracket", "DrawnBy=MD", "Rev=B"}));
    QCOMPARE(fx.sheet().titleBlock.field("Title"), std::string("Bracket"));
    QCOMPARE(fx.sheet().titleBlock.field("Rev"), std::string("B"));
    fx.doc.undoStack().undo();
    QVERIFY(fx.sheet().titleBlock.field("Title").empty());
    // the widget lists the sheet with its new name
    QVERIFY(fx.manager->run("drawing.new-sheet", {"A4"}));
    QCOMPARE(fx.model->sheets().size(), size_t(2));
    QCOMPARE(fx.widget->activeSheetId(), fx.model->sheets()[1].id);
    fx.widget->setActiveSheet(fx.model->sheets()[0].id);
    QCOMPARE(fx.canvas->sheetId(), fx.model->sheets()[0].id);
    QVERIFY(fx.manager->run("drawing.delete-sheet"));
    QCOMPARE(fx.model->sheets().size(), size_t(1));
  }

  void exportTools() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.projected-view", {"right", "above"}));
    QVERIFY(fx.manager->run("drawing.dimension", {"150,105", "150,90"}));
    fx.manager->cancel();
    QCOMPARE(fx.sheet().dimensions.size(), size_t(1));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    std::string pdf = dir.filePath("out.pdf").toStdString();
    std::string svg = dir.filePath("out.svg").toStdString();
    std::string dxf = dir.filePath("out.dxf").toStdString();
    std::string csv = dir.filePath("out.csv").toStdString();
    QVERIFY(fx.manager->run("drawing.export-pdf", {pdf}));
    QVERIFY(fx.manager->run("drawing.export-svg", {svg}));
    QVERIFY(fx.manager->run("drawing.export-dxf", {dxf}));
    QVERIFY(fx.manager->run("drawing.export-csv", {csv}));
    for (const auto& p : {pdf, svg, dxf}) {
      QFile f(QString::fromStdString(p));
      QVERIFY2(f.exists() && f.size() > 100, p.c_str());
    }
    QFile c(QString::fromStdString(csv));
    QVERIFY(c.open(QIODevice::ReadOnly));
    QVERIFY(c.readAll().startsWith("Item,"));
    QFile d(QString::fromStdString(dxf));
    QVERIFY(d.open(QIODevice::ReadOnly));
    QByteArray text = d.readAll();
    QVERIFY(text.contains("AC1009"));
    QVERIFY(text.contains("VISIBLE"));
    QVERIFY(text.contains("DIM"));
    // the host saw success messages, no errors
    bool error = false;
    for (const auto& m : fx.host.messages())
      if (m.first == core::MessageLevel::Error) error = true;
    QVERIFY(!error);
  }

  void persistenceRoundTrip() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    QVERIFY(fx.manager->run("drawing.projected-view", {"right"}));
    QVERIFY(fx.manager->run("drawing.dimension", {"150,105", "150,90"}));
    fx.manager->cancel();
    QVERIFY(fx.manager->run("drawing.text", {"50,50", "NOTE"}));
    QTemporaryDir dir;
    std::string path = dir.filePath("d.fcad").toStdString();
    std::string err;
    QVERIFY2(fx.doc.save(path, &err), err.c_str());
    // load into a fresh document + widget
    core::Document doc2;
    QVERIFY2(doc2.load(path, &err), err.c_str());
    DrawingModel* m2 = DrawingModel::forDocument(doc2);
    m2->setSynchronous(true);
    m2->loadFromDocument();
    QCOMPARE(m2->sheets().size(), size_t(1));
    const Sheet& s2 = m2->sheets().front();
    QCOMPARE(s2.views.size(), size_t(2));
    QCOMPARE(s2.dimensions.size(), size_t(1));
    QCOMPARE(s2.notes.size(), size_t(1));
    QVERIFY(m2->waitForIdle());
    QVERIFY(s2.views[0].geometryValid && s2.views[1].geometryValid);
    QVERIFY(s2.dimensions[0].valid);
    QVERIFY(near(s2.dimensions[0].value, 10.0, 1e-6));
    DrawingWorkspaceWidget w2;
    w2.setDocument(&doc2);
    QCOMPARE(w2.canvas()->sheetId(), s2.id);
    QVERIFY(!w2.canvas()->displayList().curves.empty());
    QCOMPARE(DrawingWorkspaceWidget::forDocument(&doc2), &w2);
  }

  void dialogsBuildOffscreen() {
    Fixture fx;
    core::ObjectId baseId = fx.makeBaseView();
    fx.canvas->setSelection({baseId});
    for (const char* id : {"drawing.base-view", "drawing.edit-view", "drawing.edit-sheet", "drawing.edit-title-block",
                           "drawing.dimension", "drawing.text", "drawing.create"}) {
      fx.canvas->setSelection({baseId});
      core::ITool* t = fx.tools.tool(id);
      QVERIFY2(t, id);
      // activate without finishing: emulate a dialog shell by giving the host a window
      struct WindowHost : core::NullAppHost {
        QWidget w;
        QWidget* mainWindow() override { return &w; }
      } wh;
      wh.setWorkspace(core::Workspace::Drawing);
      fx.manager->setHost(&wh);
      QVERIFY2(fx.manager->run(id), id);
      QVERIFY2(fx.manager->isActive(), id);
      std::unique_ptr<QWidget> dlg(t->createDialog(*fx.manager->activeContext(), nullptr));
      QVERIFY2(dlg != nullptr, id);
      fx.manager->cancel();
      QVERIFY(!fx.manager->isActive());
      fx.manager->setHost(&fx.host);
    }
    // cancelling an edit dialog reverts live changes
    fx.canvas->setSelection({baseId});
    struct WindowHost : core::NullAppHost {
      QWidget w;
      QWidget* mainWindow() override { return &w; }
    } wh;
    wh.setWorkspace(core::Workspace::Drawing);
    fx.manager->setHost(&wh);
    QVERIFY(fx.manager->run("drawing.edit-view"));
    QVERIFY(fx.manager->runCommandLine("scale=1:4"));
    QVERIFY(near(fx.sheet().view(baseId)->scale, 0.25));
    fx.manager->cancel();
    QVERIFY(near(fx.sheet().view(baseId)->scale, 1.0));
    QVERIFY(fx.manager->run("drawing.edit-view"));
    QVERIFY(fx.manager->runCommandLine("scale=1:4"));
    fx.manager->accept();
    QVERIFY(near(fx.sheet().view(baseId)->scale, 0.25));
  }
};

QTEST_MAIN(ToolsTest)
#include "test_drawing_tools.moc"
