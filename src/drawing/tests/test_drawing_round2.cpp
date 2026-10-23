// Regression tests for the drawing round-2 tester findings (docs/tests/drawing/ISSUES.md,
// section "Round 2"): DRW2-VIEWS-ANNOT-* and DRW2-ASM-OUTPUT-*.
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/Callouts.h"
#include "drawing/Dimension.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingModule.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "drawing/Projection.h"
#include "drawing/SheetRenderer.h"
#include "drawing/tools/AnnotationTools.h"
#include "drawing/tools/DrawingToolBase.h"
#include "drawing/tools/DrawingTools.h"
#include "support/HoleTestFeatures.h"

#include <QComboBox>
#include <QDate>
#include <QFormLayout>
#include <QWidget>
#include <QtTest/QtTest>

#include <cmath>
#include <memory>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool overlap(const BBox2& a, const BBox2& b) {
  return a.isValid() && b.isValid() && a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

BBox2 rectOf(const Point2& pos, double w, double h) {
  BBox2 b;
  b.add(pos);
  b.add(pos + Point2{w, h});
  return b;
}

bool inside(const BBox2& inner, const BBox2& outer) {
  return inner.min.x >= outer.min.x - 1e-6 && inner.min.y >= outer.min.y - 1e-6 && inner.max.x <= outer.max.x + 1e-6 &&
         inner.max.y <= outer.max.y + 1e-6;
}

/// 10 x 20 x 30 box, synchronous model, A3 sheet.
struct BoxFixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  core::ObjectId bodyId = core::kInvalidId;
  BoxFixture() {
    core::test::registerTestFeatures();
    doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    bodyId = doc.liveBodies().front();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
  }
  Sheet& sheet() { return *model->activeSheet(); }
  DrawingView& base(ViewOrientation o, const Point2& at, double scale = 1.0) {
    DrawingView& v = model->addBaseView(sheet(), {core::ShapeRef::body(bodyId)}, o, scale, at);
    model->updateGeometry();
    return *model->view(v.id);
  }
};

/// Hole block on an A3 sheet with a top view, canvas + tool manager.
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
};

const Curve2D* circleOf(const DrawingView& v, double r) {
  for (const auto& c : v.curves)
    if (c.isCircle() && std::abs(c.radius - r) < 1e-3) return &c;
  return nullptr;
}
}  // namespace

class Round2Test : public QObject {
  Q_OBJECT
 private slots:
  void scaleComboDoesNotComplete() {  // DRW2-ASM-OUTPUT-1
    QWidget w;
    auto* form = new QFormLayout(&w);
    QComboBox* combo = DrawingToolBase::addScaleCombo(form, 1.0);
    QVERIFY(combo->isEditable());
    QVERIFY(combo->completer() == nullptr);  // "1:1" + Enter never becomes "1:100"
    combo->setEditText("1:2");
    QCOMPARE(DrawingToolBase::comboScale(combo, 1.0), 0.5);
  }

  void identifiersAreSpacedForTheUi() {  // DRW2-ASM-OUTPUT-9
    QCOMPARE(DrawingToolBase::spacedName("IsoTopRight"), QString("Iso Top Right"));
    QCOMPARE(DrawingToolBase::spacedName("PartNumber"), QString("Part Number"));
    QCOMPARE(DrawingToolBase::spacedName("DrawnBy"), QString("Drawn By"));
    QCOMPARE(DrawingToolBase::spacedName("Title"), QString("Title"));
  }

  void newSheetStoresItsDate() {  // DRW2-ASM-OUTPUT-6
    BoxFixture fx;
    const std::string today = QDate::currentDate().toString(Qt::ISODate).toStdString();
    QCOMPARE(fx.sheet().titleBlock.field("Date"), today);
    // an edited date is kept by the next sheet (drawing-level field)
    fx.model->setTitleBlockField(fx.sheet(), "Date", "2026-10-22");
    Sheet& s2 = fx.model->addSheet(*sheetFormatByName("A4"), ProjectionStandard::FirstAngle);
    QCOMPARE(s2.titleBlock.field("Date"), std::string("2026-10-22"));
  }

  void dimensionTextBeyondAnExtensionLineClearsIt() {  // DRW2-VIEWS-ANNOT-12
    ToolFixture fx;
    const Curve2D* a = circleOf(fx.view(), 3.3);  // hole at (15, 20)
    const Curve2D* b = circleOf(fx.view(), 2.0);  // hole at (50, 20)
    QVERIFY(a && b);
    Dimension d;
    d.viewId = fx.viewId;
    d.type = DimensionType::Horizontal;
    d.refs = {DimRef::toVertex(Vertex2D::of(*a, a->center, 2)), DimRef::toVertex(Vertex2D::of(*b, b->center, 2))};
    const Point2 sa = fx.view().toSheet(a->center), sb = fx.view().toSheet(b->center);
    const double right = std::max(sa.x, sb.x);
    d.textPos = Point2{right + 1.0, std::min(sa.y, sb.y) - 12.0};  // just past the right extension line
    DimLayout L = layoutDimension(d, fx.view(), fx.model->style(), fx.doc.units());
    QVERIFY(L.ok);
    const double halfText = 0.35 * fx.model->style().dimTextHeight * static_cast<double>(L.text.size());
    QVERIFY2(L.textPos.x - halfText > right + 0.5, qPrintable(QString("text %1, line %2").arg(L.textPos.x).arg(right)));
    // inside the span nothing changes: the text stays between the lines
    d.textPos = Point2{(sa.x + sb.x) * 0.5, std::min(sa.y, sb.y) - 12.0};
    L = layoutDimension(d, fx.view(), fx.model->style(), fx.doc.units());
    QVERIFY(L.textPos.x > std::min(sa.x, sb.x) && L.textPos.x < right);
  }

  void partsListEnterAvoidsViews() {  // DRW2-VIEWS-ANNOT-7, DRW2-ASM-OUTPUT-3
    BoxFixture fx;
    Sheet& s = fx.sheet();
    PartsList pl;
    pl.rows = fx.model->bom(nullptr);
    const Point2 def = PartsListTool::defaultPosition(s, pl);
    // a view right where the default spot is (just above the title block)
    DrawingView& v = fx.base(ViewOrientation::Front, def + Point2{pl.width() * 0.5, pl.height() * 0.5});
    v.autoPlace = false;
    v.position = def + Point2{pl.width() * 0.5, pl.height() * 0.5};
    QVERIFY(overlap(rectOf(def, pl.width(), pl.height()), v.layoutBBox()));
    const Point2 p = PartsListTool::clearPosition(s, pl);
    const BBox2 r = rectOf(p, pl.width(), pl.height());
    QVERIFY(!overlap(r, v.layoutBBox()));
    QVERIFY(!overlap(r, s.titleBlockRect()));
    QVERIFY(inside(r, s.frameRect()));
    // a second list for the same view does not land on the first one
    pl.position = p;
    s.partsLists.push_back(pl);
    const Point2 p2 = PartsListTool::clearPosition(s, pl);
    QVERIFY(!overlap(rectOf(p2, pl.width(), pl.height()), r));
  }

  void revisionRowArgumentPicksTheRow() {  // DRW2-ASM-OUTPUT-4
    ToolFixture fx;
    for (const char* d : {"description=First", "description=Second", "description=Third"})
      QVERIFY(fx.manager->run("drawing.revision", {d}));
    const core::ObjectId tid = fx.sheet().callouts.revisionTables.front().id;
    QVERIFY(fx.manager->run("drawing.edit-callout", {std::to_string(tid), "--row=1", "description=Initial"}));
    const RevisionTable& t = fx.sheet().callouts.revisionTables.front();
    QCOMPARE(t.rows[0].description, std::string("Initial"));
    QCOMPARE(t.rows[2].description, std::string("Third"));
  }

  void smallerSheetKeepsBalloonsAndTablesClear() {  // DRW2-ASM-OUTPUT-2
    BoxFixture fx;
    Sheet& s = fx.sheet();
    fx.base(ViewOrientation::Front, {120, 120});
    Balloon b;
    b.id = 9001;
    b.position = {395, 40};  // near the A3 right edge
    s.balloons.push_back(b);
    SheetFormat a4 = *sheetFormatByName("A4");
    if (a4.landscape) {
      a4.landscape = false;
      std::swap(a4.width, a4.height);
    }
    QVERIFY(fx.model->changeSheetFormat(s, a4));
    const BBox2 f = s.frameRect();
    const double r = fx.model->style().balloonRadius;
    for (const auto& bl : s.balloons)
      QVERIFY2(inside(rectOf(bl.position - Point2{r, r}, 2 * r, 2 * r), f),
               qPrintable(QString("balloon at %1,%2").arg(bl.position.x).arg(bl.position.y)));
  }

  void viewsOutsideFrameAreReported() {  // DRW2-VIEWS-ANNOT-2
    BoxFixture fx;
    Sheet& s = fx.sheet();
    DrawingView& v = fx.base(ViewOrientation::Front, {150, 140});
    QVERIFY(!DrawingToolBase::viewsOutsideFrame(s));
    v.position = {s.frameRect().max.x + 20, 140};
    QVERIFY(DrawingToolBase::viewsOutsideFrame(s));
  }
};

QTEST_MAIN(Round2Test)
#include "test_drawing_round2.moc"
