// Smoke test for the part module (replace/extend freely).
#include "core/Registry.h"
#include "core/Tool.h"
#include "part/PartModule.h"

#include <QtTest/QtTest>

class PartSmokeTest : public QObject {
  Q_OBJECT
 private slots:
  void registers() {
    fabricad::core::ToolRegistry tools;
    fabricad::core::ModuleContext ctx{fabricad::core::FeatureFactory::instance(), tools};
    fabricad::part::registerModule(ctx);
    QVERIFY(true);
  }
};

QTEST_GUILESS_MAIN(PartSmokeTest)
#include "test_part_smoke.moc"
