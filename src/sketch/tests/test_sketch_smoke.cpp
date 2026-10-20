// Smoke test for the sketch module (replace/extend freely).
#include "core/Registry.h"
#include "core/Tool.h"
#include "sketch/SketchModule.h"

#include <QtTest/QtTest>

class SketchSmokeTest : public QObject {
  Q_OBJECT
 private slots:
  void registers() {
    fabricad::core::ToolRegistry tools;
    fabricad::core::ModuleContext ctx{fabricad::core::FeatureFactory::instance(), tools};
    fabricad::sketch::registerModule(ctx);
    QVERIFY(true);
  }
};

QTEST_GUILESS_MAIN(SketchSmokeTest)
#include "test_sketch_smoke.moc"
