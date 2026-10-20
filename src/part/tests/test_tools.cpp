// Tests for the tool layer: FeatureTool create/edit/cancel with NullViewerContext and
// NullAppHost, PropertyPanel widgets, Press Pull dispatch, import/export, edit dispatcher.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "part/features/Import.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/MeasureTool.h"
#include "part/tools/PropertyPanel.h"
#include "support/TestSketch.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {
bool near(double a, double b, double tol = 1e-4) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

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
};
}  // namespace

class ToolsTest : public QObject {
  Q_OBJECT
 private slots:
  void registryContents() {
    Rig rig;
    for (const char* id : {"part.extrude", "part.revolve", "part.sweep", "part.loft", "part.hole", "part.fillet", "part.chamfer",
                           "part.shell", "part.draft", "part.combine", "part.press-pull", "part.offset-face", "part.move",
                           "part.split-body", "part.rectangular-pattern", "part.circular-pattern", "part.mirror",
                           "construct.offset-plane", "construct.plane-at-angle", "construct.midplane",
                           "construct.plane-three-points", "construct.tangent-plane", "construct.axis", "construct.point",
                           "part.import", "part.import-file", "part.export-step", "part.export-stl", "part.edit-feature"})
      QVERIFY2(rig.registry.has(id), id);
    QCOMPARE(rig.registry.byShortcut("E")->info().id, std::string("part.extrude"));
    QCOMPARE(rig.registry.byAlias("EXT")->info().id, std::string("part.extrude"));
    QCOMPARE(rig.registry.byAlias("fillet")->info().id, std::string("part.fillet"));
    auto groups = rig.registry.groups(core::Workspace::Design, "SOLID");
    QVERIFY(std::find(groups.begin(), groups.end(), "CREATE") != groups.end());
    QVERIFY(std::find(groups.begin(), groups.end(), "CONSTRUCT") != groups.end());
    QVERIFY(!rig.registry.toolsFor(core::Workspace::Design, "SOLID", "MODIFY").empty());
    QCOMPARE(toolIdForFeatureType("Extrude"), std::string("part.extrude"));
    // every registered feature type creates
    for (const char* t : {"Extrude", "Revolve", "Fillet", "Hole", "OffsetPlane", "Import", "Mirror"})
      QVERIFY2(core::FeatureFactory::instance().create(t) != nullptr, t);
  }

  void extrudeToolCreateWithPreselectionAndCommandLine() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->feature());
    QCOMPARE(tool->feature()->refs("profiles").size(), size_t(1));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("New Body"));  // no bodies yet
    QVERIFY(tool->isValid());
    QVERIFY(!rig.viewer.previews().empty());  // live preview shown
    QVERIFY(rig.host.acceptEnabled());
    QCOMPARE(rig.host.prompt().rfind("Select", 0), size_t(0));
    // typed value on the command line goes to the distance
    QVERIFY(rig.manager.runCommandLine("25"));
    QVERIFY(near(tool->feature()->length("distance"), 25));
    // expression
    QVERIFY(rig.manager.runCommandLine("distance=2*7 mm"));
    QVERIFY(near(tool->feature()->length("distance"), 14));
    QCOMPARE(tool->feature()->prop("distance").expression(), std::string("2*7 mm"));
    // enum via command line changes structure
    QVERIFY(rig.manager.runCommandLine("direction=Symmetric"));
    rig.manager.accept();
    QVERIFY(!rig.manager.isActive());
    QCOMPARE(rig.doc.featureCount(), 2);
    QVERIFY(rig.viewer.previews().empty());  // cleared by the manager
    const core::Feature* f = rig.doc.featureAt(1);
    QCOMPARE(f->type(), std::string("Extrude"));
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(rig.doc.bodyShape(f->createdBodies()[0])), 10 * 10 * 28));
    QVERIFY(rig.doc.undoStack().canUndo());
    rig.doc.undoStack().undo();
    QCOMPARE(rig.doc.featureCount(), 1);
    rig.doc.undoStack().redo();
    QCOMPARE(rig.doc.featureCount(), 2);
  }

  void extrudeToolSelectionFlowAndInvalidState() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(!tool->isValid());  // no profile yet
    QVERIFY(!rig.host.acceptEnabled());
    // the selection filter is restricted to the profile input
    QVERIFY(core::hasKind(rig.doc.selection().filter(), core::ShapeKind::Profile));
    QVERIFY(!core::hasKind(rig.doc.selection().filter(), core::ShapeKind::Edge));
    // accept while invalid keeps the tool open
    rig.manager.accept();
    QVERIFY(rig.manager.isActive());
    // picking a profile fills the input and clears the selection
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.doc.selection().isEmpty());
    QVERIFY(tool->isValid());
    QCOMPARE(rig.viewer.highlighted().size(), size_t(1));
    // picking it again deselects it
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(!tool->isValid());
    rig.doc.selection().set(sk->rectProfile(0));
    rig.manager.cancel();
    QVERIFY(!rig.manager.isActive());
    QCOMPARE(rig.doc.featureCount(), 1);
  }

  void editToolModifyAndCancel() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    core::ObjectId body = f->createdBodies()[0];
    int undoCount = rig.doc.undoStack().count();

    // edit via the dispatcher
    QVERIFY(rig.manager.run("part.edit-feature", {std::to_string(f->id())}));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->isEditing());
    QCOMPARE(tool->feature(), f);
    QVERIFY(rig.manager.runCommandLine("12"));
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 10 * 10 * 12));  // live
    rig.manager.cancel();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 10 * 10 * 5));  // restored
    QCOMPARE(rig.doc.undoStack().count(), undoCount);

    // edit via preselection of the feature, accept
    rig.doc.selection().set(core::ShapeRef::feature(f->id()));
    QVERIFY(rig.manager.run("part.extrude"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool->isEditing());
    QVERIFY(rig.manager.runCommandLine("20"));
    rig.manager.accept();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 10 * 10 * 20));
    QCOMPARE(rig.doc.undoStack().count(), undoCount + 1);
    rig.doc.undoStack().undo();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 10 * 10 * 5));
    rig.doc.undoStack().redo();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 10 * 10 * 20));
  }

  void propertyPanelWidgets() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    core::ToolContext* ctx = rig.manager.activeContext();
    QWidget* w = tool->createDialog(*ctx, nullptr);
    QVERIFY(w);
    auto* panel = qobject_cast<PropertyPanel*>(w);
    QVERIFY(panel);
    auto* distance = panel->findChild<QLineEdit*>("prop_distance");
    QVERIFY(distance);
    QCOMPARE(distance->text(), QString("10 mm"));
    distance->setText("width*2");
    emit distance->textEdited(distance->text());
    QVERIFY(!tool->isValid() || tool->feature()->prop("distance").expression().empty());  // unknown parameter -> rejected
    rig.doc.setParameter("width", "7 mm");
    distance->setText("width*2");
    emit distance->textEdited(distance->text());
    QCOMPARE(tool->feature()->prop("distance").expression(), std::string("width*2"));
    QVERIFY(near(tool->feature()->length("distance"), 14));
    QVERIFY(tool->isValid());
    // hidden properties are not shown; symmetric measure appears after switching direction
    QVERIFY(!panel->findChild<QComboBox*>("prop_symmetricMeasure"));
    auto* direction = panel->findChild<QComboBox*>("prop_direction");
    QVERIFY(direction);
    direction->setCurrentIndex(2);  // Symmetric
    QVERIFY(panel->findChild<QComboBox*>("prop_symmetricMeasure"));
    QVERIFY(!panel->findChild<QLineEdit*>("prop_distance2"));
    // selection input buttons: profiles is active
    auto* profiles = panel->findChild<QPushButton*>("prop_profiles");
    QVERIFY(profiles);
    QCOMPARE(profiles->text(), QString("1 selected"));
    QVERIFY(profiles->isChecked());
    auto* targets = panel->findChild<QPushButton*>("prop_targets");
    QVERIFY(targets);
    targets->click();
    QCOMPARE(tool->activeInput(), std::string("targets"));
    QVERIFY(core::hasKind(rig.doc.selection().filter(), core::ShapeKind::Body));
    QVERIFY(!core::hasKind(rig.doc.selection().filter(), core::ShapeKind::Profile));
    rig.manager.accept();
    QCOMPARE(rig.doc.featureCount(), 2);
    QVERIFY(near(core::volume(rig.doc.bodyShape(rig.doc.featureAt(1)->createdBodies()[0])), 10 * 10 * 28));
    delete w;
  }

  // Regression: changing an Enum rebuilt the panel from inside QComboBox::currentIndexChanged
  // and deleted the combo while it was still emitting (SIGSEGV in roughly 1 of 40 runs).
  void enumChangeRebuildsPanelSafely() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    core::ToolContext* ctx = rig.manager.activeContext();
    QWidget* w = tool->createDialog(*ctx, nullptr);
    auto* panel = qobject_cast<PropertyPanel*>(w);
    QVERIFY(panel);
    for (int i = 0; i < 40; ++i) {
      auto* direction = panel->findChild<QComboBox*>("prop_direction");
      QVERIFY2(direction, "the rebuilt panel has a direction combo");
      const bool symmetric = (i % 2) == 0;
      direction->setCurrentIndex(symmetric ? 2 : 0);  // Symmetric <-> One Side, rebuilds the rows
      // run the deferred deletes now: a destroyed-while-emitting combo would crash here
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      QCOMPARE(panel->findChild<QComboBox*>("prop_symmetricMeasure") != nullptr, symmetric);
      QCOMPARE(tool->feature()->enumName("direction"), std::string(symmetric ? "Symmetric" : "One Side"));
      QVERIFY(tool->isValid());
    }
    // the command-line path ("direction=...") rebuilds too
    QVERIFY(rig.manager.runCommandLine("direction=Two Sides"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(panel->findChild<QLineEdit*>("prop_distance2"));
    QVERIFY(!panel->findChild<QComboBox*>("prop_symmetricMeasure"));
    rig.manager.cancel();
    delete w;
  }

  void measureToolReportsProperties() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 20);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    core::ObjectId body = f->createdBodies()[0];
    rig.doc.body(body)->density = 7.85;  // steel, g/cm3
    rig.doc.body(body)->material = "Steel";
    QVERIFY(rig.registry.has("inspect.measure"));
    QCOMPARE(rig.registry.byShortcut("I")->info().id, std::string("inspect.measure"));
    QCOMPARE(rig.registry.tool("inspect.measure")->info().group, std::string("INSPECT"));
    // body: volume, area, mass
    MeasureResult r = measureRefs(rig.doc, {core::ShapeRef::body(body)}, rig.doc.units());
    QVERIFY(r.ok());
    QVERIFY(r.row("Volume") && near(r.row("Volume")->number, 10 * 20 * 5));
    QVERIFY(r.row("Surface area") && near(r.row("Surface area")->number, 2 * (200 + 50 + 100)));
    QVERIFY(r.row("Mass") && near(r.row("Mass")->number, 1000 * 7.85 / 1000.0));
    QVERIFY(r.row("Material") && r.row("Material")->value == "Steel");
    QVERIFY(!measureText(r).empty());
    // faces: top (area 200, plane) and a side; distance between the two opposite 10 x 5 faces = 20
    const TopoDS_Shape& shape = rig.doc.bodyShape(body);
    std::vector<core::ShapeRef> faceRefs;
    core::ShapeRef top, yMin, yMax, xMin;
    for (const TopoDS_Shape& face : core::subShapes(shape, TopAbs_FACE)) {
      core::ShapeRef ref = core::makeBrepRef(rig.doc, body, face);
      core::GeomSignature sg = core::computeSignature(face);
      if (sg.center.z > 4.9) top = ref;
      if (sg.center.y < 0.1 && sg.size > 40 && sg.size < 60) yMin = ref;
      if (sg.center.y > 19.9) yMax = ref;
      if (sg.center.x < 0.1 && sg.size > 90) xMin = ref;
    }
    QVERIFY(top.isValid() && yMin.isValid() && yMax.isValid() && xMin.isValid());
    r = measureRefs(rig.doc, {top}, rig.doc.units());
    QVERIFY(r.row("Area") && near(r.row("Area")->number, 200.0));
    QVERIFY(r.row("Type") && r.row("Type")->value == "Plane");
    r = measureRefs(rig.doc, {yMin, yMax}, rig.doc.units());
    QVERIFY(r.hasDistance);
    QVERIFY(near(r.distance, 20.0));
    QVERIFY(r.row("Delta Y") && near(r.row("Delta Y")->number, 20.0));
    QVERIFY(r.row("Angle") && near(r.row("Angle")->number, 0.0));
    QVERIFY(r.row("Plane offset") && near(r.row("Plane offset")->number, 20.0));
    r = measureRefs(rig.doc, {top, xMin}, rig.doc.units());
    QVERIFY(r.row("Angle") && near(r.row("Angle")->number, 90.0));
    // an edge: length 20 along Y
    core::ShapeRef edge;
    for (const TopoDS_Shape& e : core::subShapes(shape, TopAbs_EDGE)) {
      core::GeomSignature sg = core::computeSignature(e);
      if (near(sg.size, 20.0) && sg.center.z > 4.9 && sg.center.x < 0.1) edge = core::makeBrepRef(rig.doc, body, e);
    }
    QVERIFY(edge.isValid());
    r = measureRefs(rig.doc, {edge}, rig.doc.units());
    QVERIFY(r.row("Length") && near(r.row("Length")->number, 20.0));
    QVERIFY(r.row("Delta Y") && near(r.row("Delta Y")->number, 20.0));
    // the tool: picks accumulate to two, a third restarts, re-picking removes
    rig.doc.selection().set(top);
    QVERIFY(rig.manager.run("inspect.measure"));
    auto* mt = dynamic_cast<MeasureTool*>(rig.manager.activeTool());
    QVERIFY(mt);
    QCOMPARE(mt->picks().size(), size_t(1));
    QVERIFY(mt->result().row("Area"));
    mt->pick(*rig.manager.activeContext(), xMin);
    QCOMPARE(mt->picks().size(), size_t(2));
    QVERIFY(mt->result().row("Angle"));
    QVERIFY(!rig.viewer.previews().empty());  // distance line drawn
    mt->pick(*rig.manager.activeContext(), xMin);  // remove
    QCOMPARE(mt->picks().size(), size_t(1));
    mt->pick(*rig.manager.activeContext(), yMin);
    mt->pick(*rig.manager.activeContext(), yMax);  // third pick restarts
    QCOMPARE(mt->picks().size(), size_t(1));
    QVERIFY(mt->commandLineInput(*rig.manager.activeContext(), "clear"));
    QVERIFY(mt->picks().empty());
    core::ToolContext* ctx = rig.manager.activeContext();
    QWidget* w = mt->createDialog(*ctx, nullptr);
    QVERIFY(w);
    delete w;
    rig.manager.accept();
    QVERIFY(!rig.manager.isActive());
    QVERIFY(rig.viewer.previews().empty());
  }

  void pressPullDispatch() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    core::ObjectId body = f->createdBodies()[0];
    // face -> offset face
    core::ShapeRef top = core::ShapeRef::subShape(body, rig.doc.bodyShape(body), f->namedShapes().at("Extrude1/End"));
    rig.doc.selection().set(top);
    QVERIFY(rig.manager.run("part.press-pull"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool->feature());
    QCOMPARE(tool->feature()->type(), std::string("OffsetFace"));
    QVERIFY(rig.manager.runCommandLine("3"));
    rig.manager.accept();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 10 * 10 * 8));
    // nothing selected: waits, then an edge pick makes it a fillet
    QVERIFY(rig.manager.run("part.press-pull"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(!tool->feature());
    const TopoDS_Shape& s = rig.doc.bodyShape(body);
    rig.doc.selection().set(core::ShapeRef::subShape(body, s, core::subShapes(s, TopAbs_EDGE).front()));
    QVERIFY(tool->feature());
    QCOMPARE(tool->feature()->type(), std::string("Fillet"));
    QCOMPARE(tool->feature()->refs("edges").size(), size_t(1));
    QVERIFY(rig.manager.runCommandLine("1"));
    QVERIFY(tool->isValid());
    rig.manager.accept();
    QCOMPARE(rig.doc.featureAt(rig.doc.featureCount() - 1)->type(), std::string("Fillet"));
  }

  void importExportRoundTrip() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 20);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    std::string step = (dir.path() + "/part.step").toStdString();
    std::string stl = (dir.path() + "/part.stl").toStdString();
    QVERIFY(rig.manager.run("part.export-step", {step}));
    QVERIFY(rig.manager.run("part.export-stl", {stl}));
    QVERIFY(QFileInfo::exists(QString::fromStdString(step)));
    QVERIFY(QFileInfo::exists(QString::fromStdString(stl)));

    // import STEP into a fresh document through the Import feature
    core::Document doc2;
    auto imp = std::make_unique<ImportFeature>();
    imp->prop("filePath").setString(step);
    core::Feature* f = doc2.addFeature(std::move(imp));
    doc2.recompute();
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QCOMPARE(f->createdBodies().size(), size_t(1));
    QVERIFY(near(core::volume(doc2.bodyShape(f->createdBodies()[0])), 1000, 1e-3));
    QCOMPARE(doc2.body(f->createdBodies()[0])->name, std::string("part"));

    // import STL: mesh solid, volume close to 1000
    auto imp2 = std::make_unique<ImportFeature>();
    imp2->prop("filePath").setString(stl);
    core::Feature* f2 = doc2.addFeature(std::move(imp2));
    doc2.recompute();
    QCOMPARE(f2->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc2.bodyShape(f2->createdBodies()[0])), 1000, 1e-2));

    // missing file -> error, and the import tool with a path argument
    auto imp3 = std::make_unique<ImportFeature>();
    imp3->prop("filePath").setString("/nonexistent/x.step");
    core::Feature* f3 = doc2.addFeature(std::move(imp3));
    doc2.recompute();
    QCOMPARE(f3->status(), core::FeatureStatus::Error);
    Rig rig3;
    QVERIFY(rig3.manager.run("part.import-file", {step}));
    QCOMPARE(rig3.doc.featureCount(), 1);
    QCOMPARE(rig3.doc.featureAt(0)->status(), core::FeatureStatus::Ok);
  }
};

QTEST_MAIN(ToolsTest)
#include "test_tools.moc"
