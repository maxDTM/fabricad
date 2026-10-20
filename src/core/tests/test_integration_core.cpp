// Core changes made during integration (requests from docs/integration-notes/*.md):
// snapshot command inside a macro keeps the undo stack alive, addComponent(Component) with
// a given id, definitionOf/displayedBodies, feature-owned component for created bodies,
// history-name aware RecomputeContext::resolve, non-interrupting view tools, toolStarted,
// QKeySequence-normalised shortcuts.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "support/TestFeatures.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

using namespace fabricad::core;
using fabricad::core::test::BoxFeature;

namespace {

/// Box feature that names its top face "Top" (history-name layer) and reads a Face ref.
class NamedBoxFeature : public Feature {
 public:
  NamedBoxFeature() : Feature("NamedBox") { addLength("size", 10.0); }
  void recompute(RecomputeContext& ctx) override {
    const double s = ctx.length("size");
    TopoDS_Shape box = BRepPrimAPI_MakeBox(s, s, s).Shape();
    ctx.createBody("Box", box);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(box, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
      const TopoDS_Face f = TopoDS::Face(faces(i));
      const Vec3 c = computeSignature(f).center;
      if (c.z > s * 0.99) namedShapes()["Top"] = f;
    }
  }
};

/// Reads a face ref and reports which face it resolved to (z of its centre).
class FaceReaderFeature : public Feature {
 public:
  FaceReaderFeature() : Feature("FaceReader") { addRef("face", mask(ShapeKind::Face)); }
  double resolvedZ = -1;
  bool warned = false;
  void recompute(RecomputeContext& ctx) override {
    ShapeRef r = ref("face");
    TopoDS_Shape s = ctx.resolve(r);
    prop("face").setRef(r);
    warned = ctx.hasWarning();
    resolvedZ = s.IsNull() ? -1 : computeSignature(s).center.z;
  }
};

class StickyTool : public ToolBase {
 public:
  StickyTool() : ToolBase(ToolInfo{.id = "test.sticky", .name = "Sticky", .shortcut = "Delete", .tab = "SOLID", .group = "CREATE"}) {}
  int deactivations = 0;
  void activate(ToolContext& ctx) override { ctx.setPrompt("sticky"); }
  void deactivate(ToolContext&) override { ++deactivations; }
};

}  // namespace

class IntegrationCoreTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { fabricad::core::test::registerTestFeatures(); }

  void snapshotCommandInsideMacroKeepsUndoStack() {
    Document doc;
    doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    UndoStack& undo = doc.undoStack();
    undo.push(std::make_unique<SetParameterCommand>(doc, "w", "5 mm"));
    QCOMPARE(undo.count(), 1);

    undo.beginMacro("Delete component");
    undo.push(std::make_unique<DocumentSnapshotCommand>(doc, "add component", [&doc] { doc.addComponent("Sub"); }));
    undo.push(std::make_unique<SetParameterCommand>(doc, "h", "7 mm"));
    undo.endMacro();
    QCOMPARE(undo.count(), 2);
    QCOMPARE(doc.components().size(), size_t(2));
    QVERIFY(doc.parameter("h"));

    undo.undo();  // restores through Document::fromJson: must not wipe the stack
    QCOMPARE(undo.count(), 2);
    QCOMPARE(undo.index(), 1);
    QCOMPARE(doc.components().size(), size_t(1));
    QVERIFY(!doc.parameter("h"));
    QVERIFY(doc.parameter("w"));
    QVERIFY(undo.canRedo());
    undo.redo();
    QCOMPARE(undo.index(), 2);
    QCOMPARE(doc.components().size(), size_t(2));
    QVERIFY(doc.parameter("h"));
    QCOMPARE(doc.featureCount(), 1);
    QVERIFY(!doc.bodyShape(doc.liveBodies()[0]).IsNull());

    // load() still starts with a clean history
    const QString path = QDir::temp().filePath("fabricad_core_integration.fcad");
    std::string err;
    QVERIFY2(doc.save(path.toStdString(), &err), err.c_str());
    QVERIFY2(doc.load(path.toStdString(), &err), err.c_str());
    QCOMPARE(doc.undoStack().count(), 0);
    QFile::remove(path);
  }

  void addComponentWithId() {
    Document doc;
    Component c;
    c.id = 500;
    c.name = "Bracket";
    c.grounded = true;
    Component& added = doc.addComponent(c);
    QCOMPARE(added.id, ObjectId(500));
    QCOMPARE(added.parent, kRootComponentId);
    QVERIFY(doc.allocateId() > 500);
    QVERIFY(std::find(doc.rootComponent().children.begin(), doc.rootComponent().children.end(), ObjectId(500)) !=
            doc.rootComponent().children.end());
    // duplicate id -> fresh one
    Component dup;
    dup.id = 500;
    dup.name = "Other";
    QVERIFY(doc.addComponent(dup).id != 500);
    // unknown parent -> root
    Component orphan;
    orphan.parent = 99999;
    QCOMPARE(doc.addComponent(orphan).parent, kRootComponentId);
  }

  void occurrenceHelpers() {
    Document doc;
    Component& def = doc.addComponent("Wheel");
    doc.setActiveComponent(def.id);
    doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    QCOMPARE(doc.bodiesOf(def.id).size(), size_t(1));
    Component occ;
    occ.name = "Wheel";
    occ.definition = def.id;
    Component& o1 = doc.addComponent(occ);
    Component occ2;
    occ2.name = "Wheel";
    occ2.definition = o1.id;  // chained occurrence
    Component& o2 = doc.addComponent(occ2);
    QCOMPARE(doc.definitionOf(o1.id), def.id);
    QCOMPARE(doc.definitionOf(o2.id), def.id);
    QCOMPARE(doc.definitionOf(def.id), def.id);
    QCOMPARE(doc.definitionOf(424242), kInvalidId);
    QCOMPARE(doc.displayedBodies(o2.id), doc.bodiesOf(def.id));
    QVERIFY(doc.displayedBodies(424242).empty());
  }

  void featureOwnedComponent() {
    Document doc;
    Component& a = doc.addComponent("A");
    Component& b = doc.addComponent("B");
    doc.setActiveComponent(a.id);
    Feature* f1 = doc.addFeature(std::make_unique<BoxFeature>());
    QCOMPARE(f1->component(), a.id);
    doc.setActiveComponent(b.id);  // activated before the first recompute
    doc.recompute();
    QCOMPARE(doc.body(f1->createdBodies()[0])->component, a.id);  // body stays with the creating component
    // persisted
    const QJsonObject json = doc.toJson();
    Document copy;
    std::string err;
    QVERIFY2(copy.fromJson(json, &err), err.c_str());
    QCOMPARE(copy.feature(f1->id())->component(), a.id);
    QCOMPARE(copy.body(f1->createdBodies()[0])->component, a.id);
  }

  void nameAwareResolve() {
    Document doc;
    auto* box = static_cast<NamedBoxFeature*>(doc.addFeature(std::make_unique<NamedBoxFeature>()));
    doc.recompute();
    const ObjectId bodyId = box->createdBodies()[0];
    QVERIFY(!doc.namedSubShape(bodyId, "Top").IsNull());
    QVERIFY(doc.namedSubShape(bodyId, "Nope").IsNull());

    auto reader = std::make_unique<FaceReaderFeature>();
    ShapeRef r = ShapeRef::face(bodyId, 1);  // wrong index on purpose; the name must win
    r.name = "Top";
    reader->prop("face").setRef(r);
    FaceReaderFeature* rd = reader.get();
    doc.addFeature(std::move(reader));
    doc.recompute();
    QCOMPARE(rd->status(), FeatureStatus::Ok);
    QVERIFY(!rd->warned);
    QVERIFY(qAbs(rd->resolvedZ - 10.0) < 1e-6);
    QVERIFY(rd->ref("face").index != 1 || rd->resolvedZ > 9.0);
    // the top face follows the size change without any re-matching warning
    box->prop("size").setDouble(25.0);
    doc.notifyFeatureChanged(box->id());
    doc.recompute();
    QVERIFY(!rd->warned);
    QVERIFY(qAbs(rd->resolvedZ - 25.0) < 1e-6);
  }

  void viewToolsKeepActiveToolAndToolStarted() {
    Document doc;
    ToolRegistry reg;
    reg.registerTool(std::make_unique<StickyTool>());
    int fitRuns = 0;
    ToolInfo fit{.id = "view.fit", .name = "Fit", .shortcut = "F6", .aliases = {"FIT"}, .needsDocument = false};
    fit.keepsActiveTool = true;
    reg.registerTool(std::make_unique<LambdaTool>(fit, [&](ToolContext&) { ++fitRuns; }));
    ToolInfo other{.id = "app.other", .name = "Other", .aliases = {"OTHER"}};
    reg.registerTool(std::make_unique<LambdaTool>(other, [](ToolContext&) {}));

    ToolManager mgr(reg);
    NullViewerContext viewer;
    NullAppHost host;
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);

    QSignalSpy started(&mgr, &ToolManager::toolStarted);
    QSignalSpy activated(&mgr, &ToolManager::toolActivated);
    QVERIFY(mgr.run("test.sticky"));
    QCOMPARE(activated.count(), 1);
    QCOMPARE(started.count(), 1);
    auto* sticky = static_cast<StickyTool*>(reg.tool("test.sticky"));

    QVERIFY(mgr.run("view.fit"));
    QCOMPARE(fitRuns, 1);
    QCOMPARE(mgr.activeToolId(), std::string("test.sticky"));
    QCOMPARE(sticky->deactivations, 0);
    QVERIFY(mgr.runCommandLine("FIT"));  // command line too
    QCOMPARE(fitRuns, 2);
    QCOMPARE(mgr.activeToolId(), std::string("test.sticky"));
    QCOMPARE(started.count(), 1);  // instant tools do not emit toolStarted

    QVERIFY(mgr.run("app.other"));  // ordinary instant tool cancels the active one
    QCOMPARE(sticky->deactivations, 1);
    QVERIFY(!mgr.isActive());
  }

  void shortcutNormalisation() {
    Document doc;
    ToolRegistry reg;
    reg.registerTool(std::make_unique<StickyTool>());  // shortcut "Delete"
    QVERIFY(reg.byShortcut("Del"));
    QVERIFY(reg.byShortcut("delete"));
    ToolManager mgr(reg);
    mgr.setDocument(&doc);
    QVERIFY(mgr.triggerShortcut("Del"));
    QCOMPARE(mgr.activeToolId(), std::string("test.sticky"));
    mgr.cancel();
    QVERIFY(!mgr.triggerShortcut("Ctrl+Del"));
  }

  void editToolIdDefault() {
    BoxFeature f;
    QVERIFY(f.editToolId().empty());
  }
};

QTEST_GUILESS_MAIN(IntegrationCoreTest)
#include "test_integration_core.moc"
