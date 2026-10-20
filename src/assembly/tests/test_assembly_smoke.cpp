// Smoke test for the assembly module (replace/extend freely).
#include "assembly/AssemblyModule.h"
#include "core/Registry.h"
#include "core/Tool.h"

#include <QtTest/QtTest>

class AssemblySmokeTest : public QObject {
  Q_OBJECT
 private slots:
  void registers() {
    fabricad::core::ToolRegistry tools;
    fabricad::core::ModuleContext ctx{fabricad::core::FeatureFactory::instance(), tools};
    fabricad::assembly::registerModule(ctx);
    QVERIFY(true);
  }
};

QTEST_GUILESS_MAIN(AssemblySmokeTest)
#include "test_assembly_smoke.moc"
