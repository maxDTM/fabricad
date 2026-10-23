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
#include "part/features/Fillet.h"
#include "part/features/Hole.h"
#include "part/features/Import.h"
#include "part/features/Primitives.h"
#include "part/features/Thread.h"
#include "part/tools/ExportStlTool.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/MeasureTool.h"
#include "part/tools/PropertyPanel.h"
#include "support/TestSketch.h"

#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>

#include <QCheckBox>
#include <QJsonObject>
#include <QComboBox>
#include <QSpinBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

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
    // the prompt follows the picks (p1-check 4, P5-MOCKUP-2): with the profile picked it asks for
    // the distance (drag the handle or type it)
    QVERIFY2(rig.host.prompt().find("type a distance") != std::string::npos, rig.host.prompt().c_str());
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

  void batchModeDefersPreviewAndComputesOnceAtOk() {
    // script runs (core::isBatchMode): no preview while inputs are typed, one computation at OK,
    // the same result as interactively
    core::setBatchMode(true);
    struct Reset {
      ~Reset() { core::setBatchMode(std::nullopt); }
    } reset;
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->isPreviewPending());
    QVERIFY(rig.viewer.previews().empty());
    QVERIFY(rig.host.acceptEnabled());
    QVERIFY(rig.manager.runCommandLine("25"));
    QVERIFY(rig.manager.runCommandLine("direction=Symmetric"));
    QVERIFY(tool->isPreviewPending());
    // a query computes it (OPTIONS reads isValid / message)
    QVERIFY(tool->isValid());
    QVERIFY(!tool->isPreviewPending());
    QVERIFY(rig.manager.runCommandLine("distance=30"));
    QVERIFY(tool->isPreviewPending());
    rig.manager.accept();
    QVERIFY(!rig.manager.isActive());
    QCOMPARE(rig.doc.featureCount(), 2);
    const core::Feature* f = rig.doc.featureAt(1);
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(rig.doc.bodyShape(f->createdBodies()[0])), 10 * 10 * 60));  // symmetric, half length 30
    // the committed result is the one a full recompute gives
    rig.doc.recomputeAll();
    QVERIFY(near(core::volume(rig.doc.bodyShape(f->createdBodies()[0])), 10 * 10 * 60));  // symmetric, half length 30
    // an invalid feature is refused at OK with the reason; the tool stays open
    QVERIFY(rig.manager.run("part.extrude"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool->isPreviewPending());
    rig.manager.accept();
    QVERIFY(rig.manager.isActive());
    QVERIFY(!rig.host.messages().empty());
    QVERIFY(rig.host.messages().back().second.find("OK is disabled for") != std::string::npos);
    rig.manager.cancel();
    QCOMPARE(rig.doc.featureCount(), 2);
  }

  void removingAFeatureReplaysOnlyWhatDependsOnIt() {
    Rig rig;
    core::Document& doc = rig.doc;
    auto extrude = [&](TestSketch* sk, double d) {
      auto ex = std::make_unique<ExtrudeFeature>();
      ex->prop("profiles").addRef(sk->rectProfile(0));
      ex->prop("distance").setDouble(d);
      ex->prop("operation").setEnum("New Body");
      return doc.addFeature(std::move(ex));
    };
    TestSketch* s1 = addRectSketch(doc, "XY", 0, 0, 10, 10);
    extrude(s1, 5);
    TestSketch* unused = addRectSketch(doc, "XY", 50, 0, 60, 10);
    TestSketch* s3 = addRectSketch(doc, "XY", 100, 0, 110, 10);
    core::Feature* e3 = extrude(s3, 7);
    doc.recompute();
    QCOMPARE(doc.firstDirtyIndex(), doc.rollbackIndex());
    // a sketch nothing uses changed no body: nothing to replay
    auto taken = doc.takeFeature(unused->id());
    QVERIFY(taken);
    QCOMPARE(doc.firstDirtyIndex(), doc.rollbackIndex());
    // the sketch of the last extrude: replay from that extrude, which then fails
    const core::ObjectId e3id = e3->id();
    auto taken3 = doc.takeFeature(s3->id());
    QCOMPARE(doc.firstDirtyIndex(), doc.indexOf(e3id));
    doc.recompute();
    QCOMPARE(doc.feature(e3id)->status(), core::FeatureStatus::Error);
    // an extrude (it made a body): replay from its position
    doc.takeFeature(e3id);
    QCOMPARE(doc.firstDirtyIndex(), doc.featureCount());
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
    QCOMPARE(profiles->text(), QString("1 profile"));  // selection chip (5.4)
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

  // G3 PART-02: text that does not evaluate keeps the tool invalid (OK disabled, error shown)
  // and stays in the field instead of snapping back to the last good value.
  void invalidExpressionBlocksAccept() {
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    core::ToolContext* ctx = rig.manager.activeContext();
    std::unique_ptr<QWidget> w(tool->createDialog(*ctx, nullptr));
    auto* panel = qobject_cast<PropertyPanel*>(w.get());
    QVERIFY(panel);
    auto* distance = panel->findChild<QLineEdit*>("prop_distance");
    QVERIFY(tool->isValid() && rig.host.acceptEnabled());
    distance->setText("12 +* mm");
    emit distance->textEdited(distance->text());
    emit distance->editingFinished();
    QCOMPARE(distance->text(), QString("12 +* mm"));  // not reset
    QVERIFY(panel->hasInvalid());
    QVERIFY(!tool->isValid());
    QVERIFY(!rig.host.acceptEnabled());
    QVERIFY2(tool->message().rfind("Distance", 0) == 0, tool->message().c_str());
    rig.manager.accept();
    QCOMPARE(rig.doc.featureCount(), 1);  // refused: only the sketch
    QVERIFY(rig.manager.activeTool() == tool);
    distance->setText("12");
    emit distance->textEdited(distance->text());
    QVERIFY(!panel->hasInvalid() && tool->isValid() && rig.host.acceptEnabled());
    rig.manager.accept();
    QCOMPARE(rig.doc.featureCount(), 2);
  }

  // G3 PART-06: toggling a Bool that unhides rows (Thread Auto size, Full length) rebuilds the
  // dialog; Size / Designation / Class follow each other.
  void threadDialogShowsSizeFields() {
    Rig rig;
    auto cyl = std::make_unique<CylinderFeature>();
    cyl->prop("diameter").setDouble(10);
    cyl->prop("height").setDouble(20);
    cyl->prop("operation").setEnum("New Body");
    rig.doc.addFeature(std::move(cyl));
    rig.doc.recompute();
    core::ObjectId body = rig.doc.featureAt(0)->createdBodies().front();
    core::ShapeRef face;
    for (const auto& f : core::subShapes(rig.doc.bodyShape(body), TopAbs_FACE)) {
      BRepAdaptor_Surface a(TopoDS::Face(f));
      if (a.GetType() == GeomAbs_Cylinder) face = core::ShapeRef::subShape(body, rig.doc.bodyShape(body), f);
    }
    QVERIFY(face.isValid());
    rig.doc.selection().set(face);
    QVERIFY(rig.manager.run("part.thread"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    core::ToolContext* ctx = rig.manager.activeContext();
    std::unique_ptr<QWidget> w(tool->createDialog(*ctx, nullptr));
    auto* panel = qobject_cast<PropertyPanel*>(w.get());
    QVERIFY(panel);
    QVERIFY(!panel->findChild<QComboBox*>("prop_size"));
    QVERIFY(!panel->findChild<QLineEdit*>("prop_length"));
    panel->findChild<QCheckBox*>("prop_autoSize")->setChecked(false);
    auto* size = panel->findChild<QComboBox*>("prop_size");
    QVERIFY(size);
    QVERIFY(panel->findChild<QComboBox*>("prop_sizeDesignation"));
    QVERIFY(panel->findChild<QComboBox*>("prop_threadClass"));
    QVERIFY(!panel->findChild<QComboBox*>("prop_designation"));  // stored field, not shown
    QCOMPARE(size->currentText(), QString("M10"));
    size->setCurrentIndex(size->findText("M8"));
    auto* des = panel->findChild<QComboBox*>("prop_sizeDesignation");
    QCOMPARE(des->currentText(), QString("M8x1.25"));
    QCOMPARE(tool->feature()->enumName("designation"), std::string("M8x1.25"));
    QVERIFY(des->findText("M8x1") >= 0 && des->findText("M10x1.5") < 0);
    des->setCurrentIndex(des->findText("M8x1"));
    QCOMPARE(tool->feature()->enumName("designation"), std::string("M8x1"));
    panel->findChild<QCheckBox*>("prop_fullLength")->setChecked(false);
    QVERIFY(panel->findChild<QLineEdit*>("prop_length"));
    QVERIFY(panel->findChild<QLineEdit*>("prop_offset"));
    auto* cls = panel->findChild<QComboBox*>("prop_threadClass");
    cls->setCurrentIndex(cls->findText("4g6g"));
    auto* tf = dynamic_cast<ThreadFeature*>(tool->feature());
    QVERIFY(tf && !tf->threads().empty());
    QCOMPARE(tf->threads().front().callout(), std::string("M8x1 - 4g6g"));
    // a stored file without the size fields loads its designation into the dialog fields
    QJsonObject j = tf->toJson();
    QJsonObject props = j["properties"].toObject();
    props.remove("size");
    props.remove("sizeDesignation");
    j["properties"] = props;
    ThreadFeature copy;
    copy.fromJson(j);
    QCOMPARE(copy.enumName("size"), std::string("M8"));
    QCOMPARE(copy.enumName("sizeDesignation"), std::string("M8x1"));
    QCOMPARE(copy.enumName("designation"), std::string("M8x1"));
  }

  // PART-04: a Cut on a sketch lying on the body's top face defaults into the body (Flip is set
  // automatically when the user picks Cut); a cut that removes nothing is an error.
  void cutOnTopFaceFlipsIntoBody() {
    Rig rig;
    TestSketch* base = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(base->rectProfile(0));
    ex->prop("distance").setDouble(-10);  // body below z = 0: XY is its top face
    ex->prop("operation").setEnum("New Body");
    core::Feature* exf = rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    core::ObjectId body = exf->createdBodies().front();
    QVERIFY(near(core::volume(rig.doc.bodyShape(body)), 1000));
    TestSketch* sk = addRectSketch(rig.doc, "XY", 2, 2, 4, 4);

    // scripted "operation=Cut": no automatic flip, the cut touches only the face -> error
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(rig.manager.runCommandLine("operation=Cut"));
    QVERIFY(!tool->feature()->flag("flip"));
    QVERIFY(!tool->isValid());
    QVERIFY2(tool->message().find("does not remove material") != std::string::npos, tool->message().c_str());
    rig.manager.cancel();

    // dialog: picking Cut flips into the body
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    core::ToolContext* ctx = rig.manager.activeContext();
    QWidget* w = tool->createDialog(*ctx, nullptr);
    auto* panel = qobject_cast<PropertyPanel*>(w);
    QVERIFY(panel);
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Join"));
    panel->findChild<QComboBox*>("prop_operation")->setCurrentIndex(1);  // Cut
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Cut"));
    QVERIFY(tool->feature()->flag("flip"));
    QVERIFY2(tool->isValid(), tool->message().c_str());
    auto* flip = panel->findChild<QCheckBox*>("prop_flip");
    QVERIFY(flip);
    QVERIFY(flip->isChecked());  // the dialog shows the automatic flip
    // back to Join: the automatic flip is undone
    panel->findChild<QComboBox*>("prop_operation")->setCurrentIndex(0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!tool->feature()->flag("flip"));
    panel->findChild<QComboBox*>("prop_operation")->setCurrentIndex(1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(tool->feature()->flag("flip"));
    rig.manager.accept();
    QVERIFY2(near(core::volume(rig.doc.bodyShape(body)), 1000 - 2 * 2 * 10, 1e-3),
             qPrintable(QString::number(core::volume(rig.doc.bodyShape(body)))));
    delete w;
  }

  // ASM-CORE-13: Box is New Body while it touches nothing, Join when it touches a body.
  void boxOperationFollowsContact() {
    Rig rig;
    TestSketch* base = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(base->rectProfile(0));
    ex->prop("distance").setDouble(10);
    ex->prop("operation").setEnum("New Body");
    rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    QVERIFY(rig.manager.run("part.box"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    core::ToolContext* ctx = rig.manager.activeContext();
    QWidget* w = tool->createDialog(*ctx, nullptr);
    auto* panel = qobject_cast<PropertyPanel*>(w);
    QVERIFY(panel);
    QVERIFY(rig.manager.runCommandLine("x=0"));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Join"));  // overlaps the block
    QVERIFY(rig.manager.runCommandLine("x=100"));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("New Body"));
    QCOMPARE(panel->findChild<QComboBox*>("prop_operation")->currentText(), QString("New Body"));
    QVERIFY(rig.manager.runCommandLine("x=20"));  // box x 10..30: touches the face x = 10
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Join"));
    QVERIFY(rig.manager.runCommandLine("x=100"));
    rig.manager.accept();
    QCOMPARE(rig.doc.liveBodies().size(), size_t(2));
    // a chosen operation is kept
    QVERIFY(rig.manager.run("part.box"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(rig.manager.runCommandLine("operation=New Body"));
    QVERIFY(rig.manager.runCommandLine("x=0"));
    QCOMPARE(tool->feature()->enumName("operation"), std::string("New Body"));
    rig.manager.cancel();
    delete w;
  }

  // PART-01: the pattern quantity spinbox is bounded and does not apply every keystroke.
  void patternQuantitySpinboxBounded() {
    Rig rig;
    TestSketch* base = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(base->rectProfile(0));
    ex->prop("operation").setEnum("New Body");
    rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    QVERIFY(rig.manager.run("part.rectangular-pattern"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QWidget* w = tool->createDialog(*rig.manager.activeContext(), nullptr);
    auto* spin = w->findChild<QSpinBox*>("prop_quantity1");
    QVERIFY(spin);
    QCOMPARE(spin->maximum(), 1000);
    QCOMPARE(spin->minimum(), 1);
    QVERIFY(!spin->keyboardTracking());
    rig.manager.cancel();
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

  /// A planar face of a body with outward normal `n`, with a pick point (world = local here).
  static core::ShapeRef faceRef(const core::Document& doc, core::ObjectId body, const gp_Dir& n, const gp_Pnt& pick) {
    const TopoDS_Shape& shape = doc.bodyShape(body);
    for (const TopoDS_Shape& f : core::subShapes(shape, TopAbs_FACE)) {
      auto pl = core::facePlane(f);
      if (!pl || !pl->Direction().IsEqual(n, 1e-6)) continue;
      core::ShapeRef r = core::ShapeRef::subShape(body, shape, f);
      r.point = core::Vec3::from(pick);
      r.hasPoint = true;
      return r;
    }
    return {};
  }

  void booleansStayInsideTheActiveComponent() {
    // F1: the first Extrude in an empty component must not join into another component's body,
    // even when the profiles touch it; F3: a through-all Hole cuts the body it is placed on only.
    Rig rig;
    core::ObjectId compA = rig.doc.addComponent("A").id;
    rig.doc.setActiveComponent(compA);
    TestSketch* skA = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(skA->rectProfile(0));
    ex->prop("distance").setDouble(10);
    ex->prop("operation").setEnum("New Body");
    core::Feature* exA = rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    core::ObjectId bodyA = exA->createdBodies().front();
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyA)), 1000));

    // an empty component B becomes active; its first extrude shares the face x = 10 with A's box
    core::ObjectId compB = rig.doc.addComponent("B").id;
    rig.doc.setActiveComponent(compB);
    TestSketch* skB = addRectSketch(rig.doc, "XY", 10, 0, 20, 10);
    rig.doc.selection().set(skB->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->feature()->enumName("operation"), std::string("New Body"));  // B has no body yet
    QVERIFY(rig.manager.runCommandLine("10"));
    rig.manager.accept();
    QCOMPARE(rig.doc.bodiesOf(compB).size(), size_t(1));
    QCOMPARE(rig.doc.bodiesOf(compA).size(), size_t(1));
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyA)), 1000));  // untouched
    core::ObjectId bodyB = rig.doc.bodiesOf(compB).front();
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyB)), 1000));

    // an explicit Join in B still does not reach A: a second block touching both boxes
    TestSketch* skB2 = addRectSketch(rig.doc, "XY", 5, 10, 15, 20);
    rig.doc.selection().set(skB2->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QCOMPARE(tool->feature()->enumName("operation"), std::string("Join"));  // B has a body now
    QVERIFY(rig.manager.runCommandLine("10"));
    rig.manager.accept();
    QCOMPARE(rig.doc.bodiesOf(compB).size(), size_t(1));
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyB)), 2000));
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyA)), 1000));

    // F3: hole placed on B's face x = 20, through all along -X, with the root active (as after
    // "Create Components from Bodies"): only B is drilled although the axis passes through A
    rig.doc.setActiveComponent(core::kRootComponentId);
    auto ho = std::make_unique<HoleFeature>();
    ho->prop("points").addRef(faceRef(rig.doc, bodyB, gp_Dir(1, 0, 0), gp_Pnt(20, 5, 5)));
    ho->prop("diameter").setDouble(2);
    ho->prop("extentType").setEnum("All");
    core::Feature* hf = rig.doc.addFeature(std::move(ho));
    rig.doc.recompute();
    QVERIFY2(hf->status() == core::FeatureStatus::Ok, hf->statusMessage().c_str());
    QVERIFY2(near(core::volume(rig.doc.bodyShape(bodyB)), 2000 - M_PI * 1 * 1 * 10, 1e-3),
             qPrintable(QString::number(core::volume(rig.doc.bodyShape(bodyB)))));
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyA)), 1000));
    // an explicit target list still wins
    auto ho2 = std::make_unique<HoleFeature>();
    ho2->prop("points").addRef(faceRef(rig.doc, bodyB, gp_Dir(1, 0, 0), gp_Pnt(20, 5, 2)));
    ho2->prop("diameter").setDouble(2);
    ho2->prop("extentType").setEnum("All");
    ho2->prop("targets").addRef(core::ShapeRef::body(bodyA));
    ho2->prop("targets").addRef(core::ShapeRef::body(bodyB));
    core::Feature* hf2 = rig.doc.addFeature(std::move(ho2));
    rig.doc.recompute();
    QVERIFY2(hf2->status() == core::FeatureStatus::Ok, hf2->statusMessage().c_str());
    QVERIFY(near(core::volume(rig.doc.bodyShape(bodyA)), 1000 - M_PI * 10, 1e-3));
  }

  void stlExportMeshesAfresh() {
    // F2: the mesh written depends only on the requested deflections, not on a triangulation the
    // shape already carries from an earlier export (or the viewer)
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(10);
    ex->prop("operation").setEnum("New Body");
    core::Feature* exF = rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    core::ObjectId body = exF->createdBodies().front();
    auto fi = std::make_unique<FilletFeature>();
    const TopoDS_Shape& shape = rig.doc.bodyShape(body);
    for (const TopoDS_Shape& e : core::subShapes(shape, TopAbs_EDGE)) {
      core::Vec3 mn, mx;
      if (core::bbox(e, mn, mx) && mn.x > 9.9 && mn.z > 9.9) fi->prop("edges").addRef(core::ShapeRef::subShape(body, shape, e));
    }
    fi->prop("radius").setDouble(4);
    core::Feature* ff = rig.doc.addFeature(std::move(fi));
    rig.doc.recompute();
    QVERIFY2(ff->status() == core::FeatureStatus::Ok, ff->statusMessage().c_str());
    QTemporaryDir dir;
    auto size = [&](const char* name) { return QFileInfo(dir.path() + "/" + name).size(); };
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/coarse.stl").toStdString()}));
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/fine.stl").toStdString(), "angle=0.05rad"}));
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/coarse2.stl").toStdString()}));
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/fine2.stl").toStdString(), "deflection=0.05", "angle=0.05rad"}));
    QVERIFY2(size("fine.stl") > size("coarse.stl"), qPrintable(QString("%1 vs %2").arg(size("fine.stl")).arg(size("coarse.stl"))));
    QCOMPARE(size("coarse2.stl"), size("coarse.stl"));  // the default export after a fine one is still the default mesh
    QCOMPARE(size("fine2.stl"), size("fine.stl"));
    // the typed options are parsed for the dialog tool as well
    StlExportSettings st;
    QVERIFY(ExportStlTool::applyOption("angle=0.2rad", st));
    QVERIFY(near(st.angularDeflectionDeg, 0.2 * 180.0 / M_PI));
    QVERIFY(ExportStlTool::applyOption("deflection=0.1", st));
    QVERIFY(near(st.linearDeflection, 0.1));
    QVERIFY(ExportStlTool::applyOption("ascii", st));
    QVERIFY(!st.binary);
    QVERIFY(!ExportStlTool::applyOption("out/a.stl", st));
    // without a path and without a window the command cannot run headless: it ends with a message
    rig.manager.run("part.export-stl");
    QVERIFY(!rig.manager.isActive());
    QVERIFY(!rig.host.messages().empty());
  }

  void importToolTakesPathArguments() {
    // F5: "IMPORT file.step" fills the file path, "filePath=..." types it, OK inserts
    Rig rig;
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 10, 20);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    rig.doc.addFeature(std::move(ex));
    rig.doc.recompute();
    QTemporaryDir dir;
    std::string step = (dir.path() + "/part.step").toStdString();
    QVERIFY(rig.manager.run("part.export-step", {step}));

    Rig rig2;
    QVERIFY(rig2.manager.run("part.import", {step}));
    auto* tool = dynamic_cast<FeatureTool*>(rig2.manager.activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->feature()->prop("filePath").toString(), step);
    QVERIFY(tool->isValid());
    rig2.manager.accept();
    QCOMPARE(rig2.doc.featureCount(), 1);
    QCOMPARE(rig2.doc.featureAt(0)->status(), core::FeatureStatus::Ok);
    QCOMPARE(rig2.doc.liveBodies().size(), size_t(1));

    QVERIFY(rig2.manager.run("part.import"));
    tool = dynamic_cast<FeatureTool*>(rig2.manager.activeTool());
    QVERIFY(!tool->isValid());  // no file yet
    QVERIFY(rig2.manager.runCommandLine("filePath=/nonexistent/x.step"));
    QVERIFY(!tool->isValid());
    QVERIFY(rig2.manager.runCommandLine("filePath=" + step));
    QVERIFY(tool->isValid());
    QVERIFY(rig2.manager.runCommandLine("format=STEP"));
    rig2.manager.accept();
    QCOMPARE(rig2.doc.liveBodies().size(), size_t(2));
    // a bare path typed while the command is active goes to the file as well
    QVERIFY(rig2.manager.run("part.import"));
    tool = dynamic_cast<FeatureTool*>(rig2.manager.activeTool());
    QVERIFY(rig2.manager.runCommandLine(step));
    QCOMPARE(tool->feature()->prop("filePath").toString(), step);
    rig2.manager.cancel();
  }
};

QTEST_MAIN(ToolsTest)
#include "test_tools.moc"
