// Smoke test for the drawing module: registration, workspace widget construction offscreen,
// and the canvas with an empty document. Needs QApplication (QTEST_MAIN, offscreen platform).
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Tool.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingModule.h"
#include "drawing/DrawingWorkspaceWidget.h"

#include <QtTest/QtTest>

using namespace fabricad;

class DrawingSmokeTest : public QObject {
  Q_OBJECT
 private slots:
  void registers() {
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    drawing::registerModule(ctx);
    QVERIFY(tools.has("drawing.base-view"));
    QVERIFY(tools.has("drawing.export-pdf"));
    for (core::ITool* t : tools.toolsFor(core::Workspace::Drawing)) {
      QCOMPARE(t->info().tab, std::string("DRAWING"));
      QVERIFY(t->info().id.rfind("drawing.", 0) == 0);
      QVERIFY(!t->info().group.empty());
    }
    // registering twice replaces (no duplicates)
    size_t n = tools.toolsFor(core::Workspace::Drawing).size();
    drawing::registerModule(ctx);
    QCOMPARE(tools.toolsFor(core::Workspace::Drawing).size(), n);
  }

  void widgetWithEmptyDocument() {
    core::Document doc;
    drawing::DrawingWorkspaceWidget w;
    w.resize(800, 600);
    QVERIFY(w.canvas() != nullptr);
    QVERIFY(w.model() == nullptr);
    w.setDocument(&doc);
    QVERIFY(w.model() != nullptr);
    QCOMPARE(drawing::DrawingWorkspaceWidget::activeCanvas(&doc), w.canvas());
    QCOMPARE(w.activeSheetId(), core::kInvalidId);
    QVERIFY(w.canvas()->displayList().empty());
    // default placement works without a sheet; hit tests are empty
    drawing::CanvasHit h = w.canvas()->hitTest({100, 100});
    QCOMPARE(h.kind, drawing::HitKind::None);
    w.canvas()->simulateClick({100, 100});
    w.canvas()->simulateKey(Qt::Key_Escape);
    w.setDocument(nullptr);
    QVERIFY(drawing::DrawingWorkspaceWidget::activeCanvas(&doc) == nullptr);
  }
};

QTEST_MAIN(DrawingSmokeTest)
#include "test_drawing_smoke.moc"
