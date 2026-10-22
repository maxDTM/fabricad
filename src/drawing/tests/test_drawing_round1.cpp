// Regression tests for the drawing round-1 tester findings (docs/tests/drawing/ISSUES.md):
// DRW-VIEWS-ANNOT-* and DRW-ASM-OUTPUT-*. Some run on the testers' repro model
// (tests/data/round1-views-annot.fcad: plate + boss + counterbore / tapped / drilled holes, pin).
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/BreakView.h"
#include "drawing/Callouts.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingExport.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingModule.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "drawing/SectionCut.h"
#include "drawing/SheetRenderer.h"
#include "drawing/tools/DrawingTools.h"
#include "assembly/AssemblyModule.h"
#include "part/PartModule.h"
#include "sketch/SketchModule.h"
#include "support/HoleTestFeatures.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>
#include <memory>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

bool overlap(const BBox2& a, const BBox2& b) {
  return a.isValid() && b.isValid() && a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

QString dataPath(const char* name) { return QString::fromLocal8Bit(qgetenv("FABRICAD_TEST_DATA")) + "/" + name; }

void registerPartModules() {
  static bool done = false;
  if (done) return;
  done = true;
  static core::ToolRegistry tools;
  core::ModuleContext mctx{core::FeatureFactory::instance(), tools};
  sketch::registerModule(mctx);
  part::registerModule(mctx);
  assembly::registerModule(mctx);
}

/// The views-and-annotations tester's model with its drawing (A3, 5 views, 11 dimensions).
struct ReproFixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  bool ok = false;
  ReproFixture() {
    registerPartModules();
    std::string err;
    ok = doc.load(dataPath("round1-views-annot.fcad").toStdString(), &err);
    if (!ok) qWarning("load failed: %s", err.c_str());
    doc.recompute();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    QCoreApplication::processEvents();  // the deferred load of Document::loaded
    model->loadFromDocument();
    model->updateGeometry();
    model->waitForIdle();
  }
  Sheet& sheet() { return model->sheets().front(); }
  core::ObjectId featureId(const std::string& name) {
    for (const auto& f : doc.features())
      if (f->name() == name) return f->id();
    return core::kInvalidId;
  }
};

/// 10 x 20 x 30 box, synchronous model, A3 sheet.
struct BoxFixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  core::ObjectId boxFeature = core::kInvalidId;
  core::ObjectId bodyId = core::kInvalidId;
  BoxFixture(ProjectionStandard st = ProjectionStandard::FirstAngle) {
    core::test::registerTestFeatures();
    core::Feature* f = doc.addFeature(std::make_unique<core::test::BoxFeature>());
    boxFeature = f->id();
    doc.recompute();
    bodyId = doc.liveBodies().front();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    model->addSheet(*sheetFormatByName("A3"), st);
  }
  Sheet& sheet() { return *model->activeSheet(); }
  DrawingView& base(ViewOrientation o, const Point2& at, double scale = 1.0) {
    DrawingView& v = model->addBaseView(sheet(), {core::ShapeRef::body(bodyId)}, o, scale, at);
    model->updateGeometry();
    return *model->view(v.id);
  }
};

/// Hole block on an A3 sheet with a top view, canvas + tool manager (like the callout tests).
struct ToolFixture {
  core::Document doc;
  core::ToolRegistry tools;
  core::NullAppHost host;
  std::unique_ptr<core::ToolManager> manager;
  std::unique_ptr<DrawingWorkspaceWidget> widget;
  DrawingModel* model = nullptr;
  DrawingCanvas* canvas = nullptr;
  core::ObjectId viewId = core::kInvalidId;

  ToolFixture() {
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
  ~ToolFixture() {
    if (manager && manager->isActive()) manager->cancel();
  }
  Sheet& sheet() { return *model->activeSheet(); }
  const DrawingView& view() { return *model->view(viewId); }
  Point2 at(double x, double y) { return view().toSheet(worldToView(view().frame, gp_Pnt(x, y, 20))); }
  Point2 rim(double x, double y, double r) { return at(x + r, y); }
  bool messaged(const std::string& part) const {
    for (const auto& [lvl, text] : host.messages())
      if (text.find(part) != std::string::npos) return true;
    return false;
  }
};

const Curve2D* circleOf(const DrawingView& v, double r) {
  for (const auto& c : v.curves)
    if (c.isCircle() && near(c.radius, r, 1e-3)) return &c;
  return nullptr;
}
}  // namespace

class Round1Test : public QObject {
  Q_OBJECT
 private slots:
  // ---- views and annotations ------------------------------------------------------------

  void projectedViewNeverOverlapsItsParent() {  // DRW-VIEWS-ANNOT-1
    BoxFixture fx;
    // front view (10 x 30) placed low on the sheet: the top view below it does not fit above
    // the frame line, the family is moved up instead of pushing the child into the parent
    const Sheet& s = fx.sheet();
    const double frameBottom = s.frameRect().max.y - 5.0;
    DrawingView& base = fx.base(ViewOrientation::Front, {60, frameBottom - 20});
    core::ObjectId baseId = base.id;
    DrawingView* below = fx.model->addProjectedView(fx.sheet(), baseId, ProjectionDirection::Below, {60, frameBottom});
    QVERIFY(below);
    core::ObjectId childId = below->id;
    fx.model->updateGeometry();
    const DrawingView* p = fx.model->view(baseId);
    const DrawingView* c = fx.model->view(childId);
    QVERIFY(p->geometryValid && c->geometryValid);
    BBox2 pb = p->sheetBBox(), cb = c->sheetBBox();
    QVERIFY2(cb.min.y - pb.max.y >= 15.0 - 1e-6, qPrintable(QString("gap %1").arg(cb.min.y - pb.max.y)));
    QVERIFY(cb.max.y <= s.frameRect().max.y);
    QVERIFY(near(c->position.x, p->position.x));
  }

  void holeCalloutCounterboreDepthFromFeature() {  // DRW-VIEWS-ANNOT-2 (+ root cause of -13)
    ReproFixture fx;
    QVERIFY(fx.ok);
    const DrawingView* top = fx.model->view(44);
    QVERIFY(top && top->geometryValid);
    const Curve2D* cbore = nullptr;
    for (const auto& c : top->curves)
      if (c.isCircle() && near(c.radius, 3.0, 1e-3) && c.sourceName.find("Hole2/H1") != std::string::npos) cbore = &c;
    QVERIFY(cbore);
    HoleInfo info = analyzeHole(fx.doc, *cbore);
    QVERIFY(info.ok);
    QCOMPARE(info.type, HoleInfo::Counterbore);
    QVERIFY(info.through);
    QVERIFY2(near(info.cboreDepth, 6.0, 1e-6), qPrintable(QString::number(info.cboreDepth)));
    QVERIFY(near(info.diameter, 6.0, 1e-6) && near(info.cboreDiameter, 11.0, 1e-6));
  }

  void alignedTextIsNeverUpsideDown() {  // DRW-VIEWS-ANNOT-3
    QVERIFY(near(readableTextAngle(-300), 60));
    QVERIFY(near(readableTextAngle(212), 32));
    QVERIFY(near(readableTextAngle(-148), 32));
    QVERIFY(near(readableTextAngle(90), 90));
    QVERIFY(near(readableTextAngle(-90), 90));
    QVERIFY(near(readableTextAngle(180), 0));
    QVERIFY(near(readableTextAngle(-30), -30));
    // a real aligned dimension rising to the right, text placed on the upper left
    ToolFixture fx;
    const Curve2D* a = circleOf(fx.view(), 3.3);  // hole at (15, 20)
    const Curve2D* b = circleOf(fx.view(), 2.0);  // hole at (50, 20)
    QVERIFY(a && b);
    Dimension d;
    d.viewId = fx.viewId;
    d.type = DimensionType::Aligned;
    d.refs = {DimRef::toVertex(Vertex2D::of(*a, a->center, 2)), DimRef::toVertex(Vertex2D::of(*b, b->center + Point2{0, 10}, 2))};
    // second ref: the corner above the drilled hole makes the line rise
    const Vertex2D* corner = nullptr;
    for (const auto& v : fx.view().vertices)
      if (!corner || v.p.x + v.p.y > corner->p.x + corner->p.y) corner = &v;
    QVERIFY(corner);
    d.refs[1] = DimRef::toVertex(*corner);
    Point2 sa = fx.view().toSheet(a->center), sb = fx.view().toSheet(corner->p);
    d.textPos = (sa + sb) * 0.5 + Point2{-5, -5};
    DimLayout L = layoutDimension(d, fx.view(), fx.model->style(), fx.doc.units());
    QVERIFY(L.ok);
    QVERIFY2(L.textAngle > -90.0 && L.textAngle <= 90.0, qPrintable(QString::number(L.textAngle)));
  }

  void chainSegmentInsideBaseSpanMovesToNextLine() {  // DRW-VIEWS-ANNOT-4
    ToolFixture fx;
    const Vertex2D* corner = nullptr;
    for (const auto& v : fx.view().vertices)
      if (!corner || v.p.x + v.p.y < corner->p.x + corner->p.y) corner = &v;
    const Curve2D* far = circleOf(fx.view(), 2.0);  // x = 50
    QVERIFY(corner && far);
    Dimension d;
    d.viewId = fx.viewId;
    d.type = DimensionType::Horizontal;
    d.refs = {DimRef::toVertex(*corner), DimRef::toCurve(*far)};
    d.textPos = {fx.at(25, 0).x, fx.view().sheetBBox().max.y + 10};
    core::ObjectId base = fx.model->addDimension(fx.sheet(), d).id;
    QVERIFY(near(fx.sheet().dimension(base)->value, 50));
    // chain on from the x = 50 hole back to x = 15: inside the 0..50 span of the base
    fx.canvas->setSelection({base});
    QVERIFY(fx.manager->run("drawing.dimension-chain"));
    fx.canvas->simulateClick(fx.rim(15, 20, 3.3));
    fx.manager->accept();
    QCOMPARE(fx.sheet().dimensions.size(), size_t(2));
    const Dimension& c = fx.sheet().dimensions[1];
    QVERIFY(near(c.value, 35));
    QVERIFY2(std::abs(c.textPos.y - d.textPos.y) >= 7.9, qPrintable(QString("%1 vs %2").arg(c.textPos.y).arg(d.textPos.y)));
    // the texts do not collide
    SheetRenderer r(fx.model->style(), fx.doc.units());
    DimLayout la = layoutDimension(*fx.sheet().dimension(base), fx.view(), fx.model->style(), fx.doc.units());
    DimLayout lc = layoutDimension(c, fx.view(), fx.model->style(), fx.doc.units());
    QVERIFY(std::abs(la.textPos.y - lc.textPos.y) > fx.model->style().dimTextHeight);
  }

  void deleteKeyIsTheDrawingDeleteShortcut() {  // DRW-VIEWS-ANNOT-5
    ToolFixture fx;
    const core::ITool* t = fx.tools.tool("drawing.delete");
    QVERIFY(t);
    QCOMPARE(t->info().shortcut, std::string("Del"));
    // the router hands Delete to the shortcut lookup: in the Drawing workspace it runs this tool
    Dimension d;
    d.viewId = fx.viewId;
    d.type = DimensionType::Diameter;
    d.refs = {DimRef::toCurve(*circleOf(fx.view(), 3.3))};
    d.textPos = fx.at(30, 45);
    core::ObjectId id = fx.model->addDimension(fx.sheet(), d).id;
    fx.canvas->setSelection({id});
    QVERIFY(fx.manager->triggerShortcut("Delete"));
    QVERIFY(!fx.sheet().dimension(id));
  }

  void unrelatedDeleteKeepsThicknessDimension() {  // DRW-VIEWS-ANNOT-6
    ReproFixture fx;
    QVERIFY(fx.ok);
    Sheet& s = fx.sheet();
    const Dimension* thick = s.dimension(74);
    QVERIFY(thick && thick->valid);
    QVERIFY(near(thick->value, 24, 1e-6));
    core::ObjectId e3 = fx.featureId("Extrude3");
    QVERIFY(e3 != core::kInvalidId);
    fx.doc.undoStack().push(std::make_unique<core::RemoveFeatureCommand>(fx.doc, e3));
    fx.doc.recompute();
    fx.model->waitForIdle();
    thick = s.dimension(74);
    QVERIFY(thick);
    QVERIFY2(thick->valid && !thick->orphaned, "thickness dimension orphaned by an unrelated delete");
    QVERIFY(near(thick->value, 24, 1e-6));
    // dimensions to the removed holes do orphan
    QVERIFY(s.dimension(57)->orphaned && s.dimension(58)->orphaned);
    QCOMPARE(std::string("{A|B}"), splitEdgeBaseName("{A|B}#2"));
    QCOMPARE(std::string("{A|B}"), splitEdgeBaseName("{A|B}"));
    QCOMPARE(std::string(), splitEdgeBaseName("Edge#2"));
  }

  void thirdAngleSwitchMirrorsProjectedViews() {  // DRW-VIEWS-ANNOT-7
    BoxFixture fx(ProjectionStandard::FirstAngle);
    DrawingView& base = fx.base(ViewOrientation::Front, {150, 120});
    core::ObjectId baseId = base.id;
    core::ObjectId below = fx.model->addProjectedView(fx.sheet(), baseId, ProjectionDirection::Below, {150, 200})->id;
    core::ObjectId right = fx.model->addProjectedView(fx.sheet(), baseId, ProjectionDirection::Right, {230, 120})->id;
    fx.model->updateGeometry();
    const gp_Ax2 belowFrame = fx.model->view(below)->customFrame;
    QVERIFY(fx.model->changeSheetStandard(fx.sheet(), ProjectionStandard::ThirdAngle));
    fx.model->updateGeometry();
    const DrawingView* b = fx.model->view(baseId);
    const DrawingView* top = fx.model->view(below);
    const DrawingView* side = fx.model->view(right);
    QVERIFY(top->position.y < b->position.y);  // the top view moved above the front view
    QVERIFY(near(top->position.x, b->position.x));
    QCOMPARE(top->direction, ProjectionDirection::Above);
    QVERIFY(side->position.x < b->position.x);  // the left-side view moved to the left
    QCOMPARE(side->direction, ProjectionDirection::Left);
    QVERIFY(top->customFrame.Direction().IsEqual(belowFrame.Direction(), 1e-9));  // same view content
    // consistent with a view projected fresh under the new standard
    gp_Ax2 fresh = fx.model->frameForProjection(*b, fx.sheet(), ProjectionDirection::Above);
    QVERIFY(fresh.Direction().IsEqual(top->customFrame.Direction(), 1e-9));
    QVERIFY(!overlap(top->sheetBBox(), b->sheetBBox()));
    QVERIFY(!fx.model->changeSheetStandard(fx.sheet(), ProjectionStandard::ThirdAngle));
  }

  void baseScaleChangeReflowsChildren() {  // DRW-VIEWS-ANNOT-8
    BoxFixture fx;
    DrawingView& base = fx.base(ViewOrientation::Front, {120, 110});
    core::ObjectId baseId = base.id;
    core::ObjectId below = fx.model->addProjectedView(fx.sheet(), baseId, ProjectionDirection::Below, {120, 170})->id;
    core::ObjectId right = fx.model->addProjectedView(fx.sheet(), baseId, ProjectionDirection::Right, {170, 110})->id;
    fx.model->updateGeometry();
    fx.model->view(baseId)->scale = 4.0;
    fx.model->viewDefinitionChanged(baseId);
    fx.model->updateGeometry();
    const Sheet& s = fx.sheet();
    std::vector<const DrawingView*> vs = {fx.model->view(baseId), fx.model->view(below), fx.model->view(right)};
    for (const DrawingView* v : vs) {
      QVERIFY(v->geometryValid);
      QVERIFY(near(v->scale, 4.0));
      QVERIFY2(!overlap(v->sheetBBox(), s.titleBlockRect()), v->label.c_str());
    }
    for (size_t i = 0; i < vs.size(); ++i)
      for (size_t j = i + 1; j < vs.size(); ++j)
        QVERIFY2(!overlap(vs[i]->sheetBBox(), vs[j]->sheetBBox()), (vs[i]->label + " / " + vs[j]->label).c_str());
    QVERIFY(near(vs[1]->position.x, vs[0]->position.x));  // alignment kept
    QVERIFY(near(vs[2]->position.y, vs[0]->position.y));
  }

  void sectionLabelStaysOffTheTitleBlock() {  // DRW-VIEWS-ANNOT-9
    BoxFixture fx;
    Sheet& s = fx.sheet();
    const BBox2 tb = s.titleBlockRect();
    DrawingView& base = fx.base(ViewOrientation::Top, {tb.min.x + 40, tb.min.y - 50});
    core::ObjectId baseId = base.id;
    SectionLine line;
    line.p1 = {-20, 0};
    line.p2 = {20, 0};
    line.arrow = {0, 1};
    // clicked on the title block: below the parent
    DrawingView* sec = fx.model->addSectionView(s, baseId, line, {tb.min.x + 40, tb.center().y});
    QVERIFY(sec);
    core::ObjectId secId = sec->id;
    fx.model->updateGeometry();
    const DrawingView* v = fx.model->view(secId);
    QVERIFY(v->geometryValid);
    QVERIFY2(!overlap(v->layoutBBox(), tb), "section view or its label overlaps the title block");
    QVERIFY(!overlap(v->sheetBBox(), fx.model->view(baseId)->sheetBBox()));
  }

  void adjacentPartsGetDifferentHatching() {  // DRW-VIEWS-ANNOT-11
    double a0, s0, a1, s1, a2, s2;
    hatchVariant(0, 45, 3, a0, s0);
    hatchVariant(1, 45, 3, a1, s1);
    hatchVariant(2, 45, 3, a2, s2);
    QVERIFY(near(a0, 45) && near(s0, 3));
    QVERIFY(near(a1, 135) && near(s1, 3));
    QVERIFY(near(a2, 45) && s2 < 3);
    // two touching boxes cut together: hatch lines in two directions
    core::Document doc;
    core::test::registerTestFeatures();
    doc.addFeature(std::make_unique<core::test::BoxFeature>());
    auto second = std::make_unique<core::test::BoxFeature>();
    doc.addFeature(std::move(second));
    doc.recompute();
    auto bodies = doc.liveBodies();
    QCOMPARE(bodies.size(), size_t(2));
    SectionInput in;
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(10, 0, 0));
    in.sources = {{bodies[0], doc.bodyShape(bodies[0]), {}},
                  {bodies[1], BRepBuilderAPI_Transform(doc.bodyShape(bodies[1]), shift, true).Shape(), {}}};
    in.frame = gp_Ax2(gp_Pnt(10, 10, 15), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    SectionOutput out = computeSection(in);
    QVERIFY2(out.ok, out.error.c_str());
    bool d45 = false, d135 = false;
    for (const auto& h : out.hatch) {
      double ang = std::fmod((h.b - h.a).angleDeg(), 180.0);
      if (near(ang, 45, 1e-3)) d45 = true;
      if (near(ang, 135, 1e-3)) d135 = true;
    }
    QVERIFY(d45 && d135);
  }

  void anglePrecisionLikeLengths() {  // DRW-VIEWS-ANNOT-12
    core::UnitSystem u;
    u.precision = 3;
    Dimension d;
    d.type = DimensionType::Angle;
    d.valid = true;
    d.value = 90.0;
    QCOMPARE(formatDimensionText(d, u), std::string("90\xC2\xB0"));
    d.value = 37.5;
    QCOMPARE(formatDimensionText(d, u), std::string("37.5\xC2\xB0"));
  }

  void ordinateWithoutFeaturesLeavesNothing() {  // DRW-VIEWS-ANNOT-14
    ToolFixture fx;
    QVERIFY(fx.manager->run("drawing.dimension-ordinate"));
    Point2 corner = fx.at(0, 0);
    fx.canvas->simulateClick(corner);
    fx.canvas->simulateClick(corner + Point2{0, 25});
    fx.canvas->simulateKey(Qt::Key_Return);
    QVERIFY(!fx.manager->isActive());
    QVERIFY(fx.sheet().callouts.ordinates.empty());
    QVERIFY(fx.messaged("discarded"));
  }

  void breakLinesSpanOnlyTheMaterial() {  // DRW-VIEWS-ANNOT-15
    DrawingView v;
    v.geometryValid = true;
    // L-shape: a 100 x 10 bar with a 10 x 40 post at its left end
    v.curves = {Curve2D::segment({0, 0}, {100, 0}), Curve2D::segment({100, 0}, {100, 10}),
                Curve2D::segment({100, 10}, {10, 10}), Curve2D::segment({10, 10}, {10, 50}),
                Curve2D::segment({10, 50}, {0, 50}), Curve2D::segment({0, 50}, {0, 0})};
    for (const auto& c : v.curves) v.bbox.add(c.bbox());
    v.breaks.push_back({BreakOrientation::Horizontal, 40, 70});
    double lo = 0, hi = 0;
    QVERIFY(breakCutExtent(v, BreakOrientation::Horizontal, 40, lo, hi));
    QVERIFY(near(lo, 0) && near(hi, 10));
    DisplayList out;
    addBreakLines(out, v);
    QCOMPARE(out.curves.size(), size_t(2));
    for (const auto& c : out.curves) {
      BBox2 b = c.bbox();
      QVERIFY2(b.height() < 10 + 2 * 3.0 + 1e-6, qPrintable(QString::number(b.height())));  // not the 50 mm view height
    }
  }

  void parentDeleteAsksAndReports() {  // DRW-VIEWS-ANNOT-17
    ToolFixture fx;
    SectionLine line;
    line.p1 = {-40, 0};
    line.p2 = {40, 0};
    line.arrow = {0, 1};
    core::ObjectId sec = core::kInvalidId;
    fx.model->edit("Section", &fx.doc.undoStack(), [&]() { sec = fx.model->addSectionView(fx.sheet(), fx.viewId, line, {150, 220})->id; });
    fx.model->updateGeometry();
    fx.host.setYesNoAnswer(false);
    fx.canvas->setSelection({fx.viewId});
    fx.manager->run("drawing.delete");
    QVERIFY(fx.model->view(fx.viewId) && fx.model->view(sec));  // declined: nothing removed
    fx.host.setYesNoAnswer(true);
    fx.canvas->setSelection({fx.viewId});
    fx.manager->run("drawing.delete");
    QVERIFY(!fx.model->view(fx.viewId) && !fx.model->view(sec));
    QVERIFY(fx.messaged("1 dependent view"));
  }

  // ---- assembly and output ----------------------------------------------------------------

  void svgHiddenDashesInMillimetres() {  // DRW-ASM-OUTPUT-1
    BoxFixture fx;
    DrawingView& v = fx.base(ViewOrientation::IsoTopRight, {150, 140});
    v.hiddenLines = true;
    fx.model->viewDefinitionChanged(v.id);
    fx.model->updateGeometry();
    QTemporaryDir dir;
    const std::string path = (dir.path() + "/h.svg").toStdString();
    QVERIFY(exportSvg(*fx.model, fx.sheet(), path, {}, nullptr));
    QFile f(QString::fromStdString(path));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString svg = QString::fromUtf8(f.readAll());
    QRegularExpression re("stroke-dasharray=\"([0-9.]+),([0-9.]+)\"");
    auto it = re.globalMatch(svg);
    int n = 0;
    while (it.hasNext()) {
      auto m = it.next();
      double dash = m.captured(1).toDouble(), gap = m.captured(2).toDouble();
      ++n;
      // drawn inside a group scaled to sheet mm: ISO 128 hidden line 3 / 1.5 mm
      QVERIFY2(dash <= 3.0 + 1e-6 && gap <= 1.5 + 1e-6, qPrintable(m.captured(0)));
    }
    QVERIFY(n > 0);
    // and the PDF/SVG title is the title block's
    QVERIFY(svg.contains("<title>"));
  }

  void formatChangeKeepsTablesAndViewsOnTheSheet() {  // DRW-ASM-OUTPUT-2
    BoxFixture fx;
    Sheet& s = fx.sheet();
    DrawingView& v = fx.base(ViewOrientation::Front, {350, 60});
    core::ObjectId vid = v.id;
    const BBox2 f = s.frameRect(), tb = s.titleBlockRect();
    PartsList& added = fx.model->addPartsList(s, PartsList{});
    added.position = {f.max.x - added.width(), tb.min.y - added.height()};  // snapped above the title block
    core::ObjectId plId = added.id;
    SheetFormat a4 = *sheetFormatByName("A4");
    a4.landscape = false;
    std::swap(a4.width, a4.height);
    QVERIFY(fx.model->changeSheetFormat(s, a4));
    const BBox2 nf = s.frameRect(), ntb = s.titleBlockRect();
    const PartsList* p = s.partsList(plId);
    QVERIFY(p);
    QVERIFY(p->position.x >= nf.min.x - 1e-6 && p->position.x + p->width() <= nf.max.x + 1e-6);
    QVERIFY2(near(p->position.y + p->height(), ntb.min.y, 1e-6),
             qPrintable(QString("pl y %1 h %2 tb %3 frame %4..%5").arg(p->position.y).arg(p->height()).arg(ntb.min.y).arg(nf.min.y).arg(nf.max.y)));  // re-snapped above the title block
    BBox2 vb = fx.model->view(vid)->sheetBBox();
    QVERIFY(vb.min.x >= nf.min.x && vb.max.x <= nf.max.x && vb.min.y >= nf.min.y && vb.max.y <= nf.max.y);
  }

  void balloonTipFollowsItsEdge() {  // DRW-ASM-OUTPUT-3
    BoxFixture fx;
    DrawingView& v = fx.base(ViewOrientation::Front, {150, 140});
    core::ObjectId vid = v.id;
    // left vertical edge (world x = 0)
    const Curve2D* left = nullptr;
    for (const auto& c : v.curves)
      if (c.type == CurveType::Segment && near(c.a.x, c.b.x) && (!left || c.a.x < left->a.x)) left = &c;
    QVERIFY(left);
    Balloon b;
    b.viewId = vid;
    b.itemKey = fx.bodyId;
    b.hasRef = true;
    b.ref = DimRef::toCurve(*left);
    b.refT = 0.5;
    b.attach = left->midpoint();
    b.position = {120, 120};
    core::ObjectId bid = fx.model->addBalloon(fx.sheet(), b).id;
    const double worldX0 = fx.model->view(vid)->toSheet(left->midpoint()).x;
    // widen the box 10 -> 20: the view re-centres, the left edge stays at world x = 0
    fx.doc.undoStack().push(std::make_unique<core::ModifyFeatureCommand>(
        fx.doc, fx.boxFeature, [](core::Feature& f) { f.prop("dx").setDouble(20.0); }));
    fx.model->waitForIdle();
    const DrawingView* nv = fx.model->view(vid);
    const Balloon* nb = fx.sheet().balloon(bid);
    QVERIFY(nb && !nb->orphaned);
    double minX = 1e300;
    for (const auto& c : nv->curves) minX = std::min(minX, c.bbox().min.x);
    QVERIFY2(near(nb->attach.x, minX, 1e-6), qPrintable(QString("%1 vs %2").arg(nb->attach.x).arg(minX)));
    QVERIFY(!near(nv->toSheet(nb->attach).x, worldX0, 1.0));  // moved with the edge on the sheet
    // persisted
    Balloon r = Balloon::fromJson(nb->toJson());
    QVERIFY(r.hasRef && near(r.refT, 0.5));
  }

  void hiddenUnderVisibleIsRemoved() {  // DRW-ASM-OUTPUT-4
    std::vector<Curve2D> cs = {Curve2D::segment({0, 0}, {10, 0}), Curve2D::segment({-5, 0}, {15, 0}, LineClass::Hidden),
                               Curve2D::circle({0, 5}, 2), Curve2D::circle({0, 5}, 2, LineClass::Hidden),
                               Curve2D::segment({5, -5}, {5, 5}, LineClass::Hidden)};
    removeHiddenUnderVisible(cs, 1e-4);
    int hidden = 0;
    double hiddenOnAxis = 0;
    for (const auto& c : cs) {
      if (c.cls != LineClass::Hidden) continue;
      ++hidden;
      if (c.type == CurveType::Arc) QFAIL("hidden circle under a visible circle kept");
      if (near(c.a.y, 0) && near(c.b.y, 0)) hiddenOnAxis += c.length();
    }
    QCOMPARE(hidden, 3);  // two stubs of the long line + the crossing line (not tangential)
    QVERIFY(near(hiddenOnAxis, 10, 1e-6));
    // the real projection: no hidden segment of a box lies on a visible one
    BoxFixture fx;
    DrawingView& v = fx.base(ViewOrientation::Front, {150, 140});
    v.hiddenLines = true;
    fx.model->viewDefinitionChanged(v.id);
    fx.model->updateGeometry();
    for (const auto& c : fx.model->view(v.id)->curves) QVERIFY(c.cls != LineClass::Hidden);
  }

  void revisionRowsAreEditable() {  // DRW-ASM-OUTPUT-5
    ToolFixture fx;
    QVERIFY(fx.manager->run("drawing.revision", {"description=First"}));
    QVERIFY(fx.manager->run("drawing.revision", {"description=Second"}));
    auto& tables = fx.sheet().callouts.revisionTables;
    QCOMPARE(tables.size(), size_t(1));
    core::ObjectId tid = tables.front().id;
    QVERIFY(fx.manager->run("drawing.edit-callout", {std::to_string(tid), "row=A", "description=Initial release", "approved=QA"}));
    QCOMPARE(fx.sheet().callouts.revisionTables.front().rows[0].description, std::string("Initial release"));
    QCOMPARE(fx.sheet().callouts.revisionTables.front().rows[0].approved, std::string("QA"));
    QCOMPARE(fx.sheet().callouts.revisionTables.front().rows[1].description, std::string("Second"));
    // a click inside a cell hits the table (double-click edits it instead of the sheet)
    const RevisionTable& t = fx.sheet().callouts.revisionTables.front();
    Point2 inCell = t.position + Point2{t.columnWidths[0] + 20, t.rowHeight * 1.5};
    CanvasHit h = fx.canvas->hitTest(inCell, false);
    QCOMPARE(h.kind, HitKind::Callout);
    QCOMPARE(h.id, tid);
  }

  void titleBlockFieldsAreShared() {  // DRW-ASM-OUTPUT-6
    BoxFixture fx;
    Sheet& s1 = fx.sheet();
    fx.model->setTitleBlockField(s1, "Title", "Plate & Block");
    fx.model->setTitleBlockField(s1, "Company", "ACME");
    core::ObjectId s1id = s1.id;
    Sheet& s2 = fx.model->addSheet(*sheetFormatByName("A4"), ProjectionStandard::FirstAngle);
    core::ObjectId s2id = s2.id;
    QCOMPARE(s2.titleBlock.field("Title"), std::string("Plate & Block"));
    QCOMPARE(s2.titleBlock.field("Company"), std::string("ACME"));
    fx.model->setTitleBlockField(*fx.model->sheet(s1id), "Company", "ACME Ltd");
    QCOMPARE(fx.model->sheet(s2id)->titleBlock.field("Company"), std::string("ACME Ltd"));
    // per-sheet fields stay per sheet
    fx.model->setTitleBlockField(*fx.model->sheet(s1id), "Scale", "1:5");
    QCOMPARE(fx.model->sheet(s2id)->titleBlock.field("Scale"), std::string());
    QCOMPARE(documentTitle(*fx.model, *fx.model->sheet(s2id)), std::string("Plate & Block"));  // DRW-ASM-OUTPUT-10
  }

  void titleBlockScaleIsTheMainViewScale() {  // DRW-ASM-OUTPUT-7
    BoxFixture fx;
    Sheet& s = fx.sheet();
    s.scale = 0.5;
    QCOMPARE(titleBlockValues(*fx.model, s)["Scale"], std::string("1:2"));
    fx.base(ViewOrientation::Front, {150, 140}, 1.0);
    QCOMPARE(titleBlockValues(*fx.model, s)["Scale"], std::string("1:1"));
  }

  void oneSheetExportsNameTheSheet() {  // DRW-ASM-OUTPUT-8
    ToolFixture fx;
    auto* svg = dynamic_cast<ExportTool*>(fx.tools.tool("drawing.export-svg"));
    auto* pdf = dynamic_cast<ExportTool*>(fx.tools.tool("drawing.export-pdf"));
    QVERIFY(svg && pdf);
    core::ToolContext ctx = fx.manager->makeContext();
    QVERIFY(svg->suggestedExportName(ctx).find("Sheet") == std::string::npos);  // one sheet: plain name
    QVERIFY(fx.manager->run("drawing.new-sheet"));
    ctx = fx.manager->makeContext();
    const std::string name = svg->suggestedExportName(ctx);
    QVERIFY2(name.size() > 7 && name.substr(name.size() - 7) == "-Sheet2", name.c_str());
    QVERIFY(pdf->suggestedExportName(ctx).find("Sheet2") == std::string::npos);  // PDF holds all sheets
    QTemporaryDir dir;
    QVERIFY(fx.manager->run("drawing.export-svg", {(dir.path() + "/x.svg").toStdString()}));
    QVERIFY(fx.messaged("only"));
  }

  void dxfHeaderHasExtents() {  // DRW-ASM-OUTPUT-9
    BoxFixture fx;
    fx.base(ViewOrientation::Front, {150, 140});
    std::string dxf = dxfText(*fx.model, fx.sheet(), {});
    QVERIFY(dxf.find("$EXTMAX") != std::string::npos);
    QVERIFY(dxf.find("$INSUNITS") == std::string::npos);
    QVERIFY(dxf.find("420") != std::string::npos);
  }
};

QTEST_MAIN(Round1Test)
#include "test_drawing_round1.moc"
