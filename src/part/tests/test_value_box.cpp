// Tests for the on-screen value box of feature tools (FeatureValueBox): the extrude distance box
// mirrors the property, keys typed in the viewport update the feature live, Enter commits, and
// the box follows the dialog / command line. Headless: NullViewerContext has no widget, so only
// the model exists (the overlay is covered by the sketch module's test_value_input).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/PartModule.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/FeatureValueBox.h"
#include "sketch/ValueInput.h"
#include "support/TestSketch.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

struct Rig {
  core::ToolRegistry registry;
  core::Document doc;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager manager{registry};
  Rig() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    part::registerModule(ctx);
    registerTestSketch();
    manager.setDocument(&doc);
    manager.setViewer(&viewer);
    manager.setHost(&host);
  }
  void key(int k, const char* text = "") {
    core::ViewerKeyEvent e;
    e.key = k;
    e.text = text;
    manager.keyPress(e);
  }
  void type(const char* digits) {
    for (const char* c = digits; *c; ++c) key(Qt::Key_0 + (*c - '0'), std::string(1, *c).c_str());
  }
};

}  // namespace

class ValueBoxTest : public QObject {
  Q_OBJECT
 private slots:
  // Test shape: 10 x 10 square on XY extruded 25 mm through the value box -> volume 2500.
  void extrudeDistanceBox() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->valueBox());
    QVERIFY(tool->valueBox()->isActive());
    const sketch::ValueInputModel& m = tool->valueBox()->model();
    QCOMPARE(m.fieldCount(), 1);
    QCOMPARE(m.field(0).id, std::string("distance"));
    QCOMPARE(tool->valueBox()->propertyName(), tool->feature()->primaryNumeric());
    QCOMPARE(m.field(0).kind, sketch::ValueKind::Length);
    QVERIFY(m.field(0).hasAnchor);  // anchored at the preview solid
    QVERIFY(near(m.value("distance"), tool->feature()->length("distance")));
    // a letter is a shortcut, not a value
    rig.key(Qt::Key_E, "e");
    QVERIFY(!m.isTyping());
    // typing applies live to the feature (preview)
    rig.type("2");
    QVERIFY(m.isTyping());
    QVERIFY(near(tool->feature()->length("distance"), 2.0));
    rig.type("5");
    QVERIFY(near(tool->feature()->length("distance"), 25.0));
    QVERIFY(tool->isValid());
    QVERIFY(!rig.viewer.previews().empty());
    // Esc clears the typed text but keeps the tool
    rig.key(Qt::Key_Escape);
    QVERIFY(!m.isTyping());
    QVERIFY(rig.manager.isActive());
    // Enter with a typed value commits the feature
    rig.type("25");
    rig.key(Qt::Key_Return, "\r");
    QVERIFY(!rig.manager.isActive());
    QCOMPARE(rig.doc.featureCount(), 2);
    const core::Feature* f = rig.doc.featureAt(1);
    QCOMPARE(f->type(), std::string("Extrude"));
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QVERIFY(near(f->length("distance"), 25.0));
    QVERIFY(near(core::volume(rig.doc.bodyShape(f->createdBodies()[0])), 10 * 10 * 25));
  }

  void boxFollowsDialogAndExpressions() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    rig.doc.setParameter("h", "8 mm");
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool && tool->valueBox());
    const sketch::ValueInputModel& m = tool->valueBox()->model();
    // the command line (= dialog field) changes the property; the box mirrors it
    QVERIFY(rig.manager.runCommandLine("distance=7"));
    QVERIFY(near(m.value("distance"), 7.0));
    // an expression typed into the box: incomplete text is held back, the full one is applied
    rig.key(Qt::Key_H, "h");  // 'h' alone cannot start a value
    QVERIFY(!m.isTyping());
    rig.type("2");
    rig.key(Qt::Key_Asterisk, "*");
    QVERIFY(near(tool->feature()->length("distance"), 2.0));  // "2*" is incomplete: last good value stays
    rig.key(Qt::Key_H, "h");
    QVERIFY(near(tool->feature()->length("distance"), 16.0));
    QCOMPARE(tool->feature()->prop("distance").expression(), std::string("2*h"));
    rig.key(Qt::Key_Return, "\r");
    QVERIFY(!rig.manager.isActive());
    QVERIFY(near(rig.doc.featureAt(1)->length("distance"), 16.0));
    QCOMPARE(rig.doc.featureAt(1)->prop("distance").expression(), std::string("2*h"));
  }

  void noBoxWithoutNumericInput() {
    Rig rig;
    // Enter on an invalid feature (no profile) is not swallowed by an empty box
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(!tool->isValid());
    rig.type("5");  // typed distance on an invalid feature is accepted by the box ...
    QVERIFY(tool->valueBox());
    rig.key(Qt::Key_Return, "\r");
    QVERIFY(rig.manager.isActive());  // ... but accept() keeps the tool open while invalid
    rig.manager.cancel();
    QVERIFY(!rig.manager.isActive());
  }
};

QTEST_MAIN(ValueBoxTest)
#include "test_value_box.moc"
