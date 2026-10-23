// STEP import keeping the assembly structure (status report "assembly structure kept on STEP
// import"): an assembly STEP file becomes components, occurrences and named bodies instead of a
// flat list of bodies. Files are written with the assembly STEP export, read back through the
// "part.import-file" tool (what File > Open of a .step runs).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblyModule.h"
#include "assembly/ComponentOps.h"
#include "assembly/StepExport.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/PartModule.h"
#include "part/features/Import.h"
#include "support/AssemblyTestHelpers.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <memory>

using namespace fabricad;
using namespace fabricad::assembly;
using namespace fabricad::assembly::test;
using core::Document;
using core::ObjectId;

namespace {

struct ImportRig {
  core::ToolRegistry tools;
  Document doc;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager manager{tools};
  ImportRig() {
    core::ModuleContext mctx{core::FeatureFactory::instance(), tools};
    part::registerModule(mctx);
    assembly::registerModule(mctx);
    manager.setDocument(&doc);
    manager.setViewer(&viewer);
    manager.setHost(&host);
  }
  ~ImportRig() { manager.cancel(); }
};

/// Asm: a root body, Base (grounded box), Top at (40,0,0), a second Top at (0,40,0), and Sub at
/// (100,0,0) holding an occurrence of Base 20 mm up. 5 boxes of 1000 mm3 in the world.
std::string writeAssembly(const QTemporaryDir& dir) {
  Document src;
  src.setName("Asm");
  TwoBoxes t = makeTwoBoxes(src);
  gp_Trsf tr;
  tr.SetTranslation(gp_Vec(0, 40, 0));
  createOccurrence(src, nullptr, t.compB, core::kRootComponentId, tr);
  const ObjectId sub = src.addComponent("Sub").id;
  gp_Trsf subAt;
  subAt.SetTranslation(gp_Vec(100, 0, 0));
  src.component(sub)->placement = subAt;
  gp_Trsf up;
  up.SetTranslation(gp_Vec(0, 0, 20));
  createOccurrence(src, nullptr, t.compA, sub, up);
  src.setActiveComponent(core::kRootComponentId);
  auto box = std::make_unique<core::test::BoxFeature>();
  box->prop("dx").setDouble(10);
  box->prop("dy").setDouble(10);
  box->prop("dz").setDouble(10);
  src.addFeature(std::move(box));
  src.recompute();
  const std::string path = (dir.path() + "/asm.step").toStdString();
  std::string err;
  if (!exportAssemblyStep(src, path, {}, &err)) qWarning("export failed: %s", err.c_str());
  return path;
}

const core::Component* findComponent(const Document& doc, const std::string& name, ObjectId parent, bool occurrence) {
  for (const core::Component* c : doc.components())
    if (c->name == name && c->parent == parent && c->isOccurrence() == occurrence) return c;
  return nullptr;
}

double worldVolume(const Document& doc) {
  double v = 0;
  for (const BodyInstance& bi : bodyInstances(doc, {}, true)) v += core::volume(bi.worldShape);
  return v;
}

}  // namespace

class StepImportTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { core::test::registerTestFeatures(); }

  void assemblyKeepsComponentsAndOccurrences() {
    QTemporaryDir dir;
    const std::string path = writeAssembly(dir);
    QVERIFY(QFileInfo::exists(QString::fromStdString(path)));
    ImportRig rig;
    QVERIFY(rig.manager.run("part.import-file", {path}));
    Document& doc = rig.doc;
    QCOMPARE(doc.featureCount(), 1);
    const core::Feature* f = doc.featureAt(0);
    QVERIFY2(f->status() == core::FeatureStatus::Ok, f->statusMessage().c_str());

    // the file's top product is the design: its children are the components
    const core::Component* base = findComponent(doc, "Base", core::kRootComponentId, false);
    const core::Component* top = findComponent(doc, "Top", core::kRootComponentId, false);
    const core::Component* top2 = findComponent(doc, "Top", core::kRootComponentId, true);
    const core::Component* sub = findComponent(doc, "Sub", core::kRootComponentId, false);
    QVERIFY(base && top && top2 && sub);
    QCOMPARE(top2->definition, top->id);  // shared part definition -> occurrence
    QVERIFY(near(worldOrigin(doc, top->id), gp_Pnt(40, 0, 0)));
    QVERIFY(near(worldOrigin(doc, top2->id), gp_Pnt(0, 40, 0)));
    QCOMPARE(sub->children.size(), size_t(1));
    const core::Component* inner = doc.component(sub->children.front());
    QVERIFY(inner && inner->isOccurrence());
    QCOMPARE(inner->definition, base->id);
    QVERIFY(near(worldOrigin(doc, inner->id), gp_Pnt(100, 0, 20)));

    // bodies: one per part definition, in component-local coordinates, named after the product;
    // the root's own body (exported as "Asm bodies") is back in the root
    QCOMPARE(f->createdBodies().size(), size_t(3));
    QCOMPARE(doc.bodiesOf(base->id).size(), size_t(1));
    QCOMPARE(doc.bodiesOf(top->id).size(), size_t(1));
    QCOMPARE(doc.bodiesOf(top2->id).size(), size_t(0));
    QCOMPARE(doc.body(doc.bodiesOf(top->id).front())->name, std::string("Top"));
    std::vector<ObjectId> rootBodies = doc.bodiesOf(core::kRootComponentId);
    QCOMPARE(rootBodies.size(), size_t(1));
    QCOMPARE(doc.body(rootBodies.front())->name, std::string("Asm"));
    core::Vec3 mn, mx;
    QVERIFY(core::bbox(doc.bodyShape(doc.bodiesOf(top->id).front()), mn, mx));
    QVERIFY(near(mn.x, 0.0) && near(mx.x, 10.0));  // local, not at x=40
    QVERIFY(near(worldVolume(doc), 5000.0, 1e-3));

    // one undo step removes the components with the import, redo brings them back
    const size_t comps = doc.components().size();
    QCOMPARE(comps, size_t(6));  // root + 5
    rig.doc.undoStack().undo();
    QCOMPARE(doc.featureCount(), 0);
    QCOMPARE(doc.components().size(), size_t(1));
    rig.doc.undoStack().redo();
    QCOMPARE(doc.featureCount(), 1);
    QCOMPARE(doc.components().size(), comps);
    QVERIFY(near(worldVolume(doc), 5000.0, 1e-3));

    // save / load: the structure is kept by the feature, the bodies land in the same components
    const std::string fcad = (dir.path() + "/asm.fcad").toStdString();
    std::string err;
    QVERIFY2(doc.save(fcad, &err), err.c_str());
    Document loaded;
    QVERIFY2(loaded.load(fcad, &err), err.c_str());
    QCOMPARE(loaded.featureAt(0)->status(), core::FeatureStatus::Ok);
    QCOMPARE(loaded.components().size(), comps);
    QVERIFY(near(worldVolume(loaded), 5000.0, 1e-3));
    const core::Component* ltop = findComponent(loaded, "Top", core::kRootComponentId, false);
    QVERIFY(ltop);
    QCOMPARE(loaded.bodiesOf(ltop->id).size(), size_t(1));
  }

  void insertIntoDesignMakesOneComponent() {
    QTemporaryDir dir;
    const std::string path = writeAssembly(dir);
    ImportRig rig;
    auto box = std::make_unique<core::test::BoxFeature>();
    rig.doc.addFeature(std::move(box));
    rig.doc.recompute();
    QVERIFY(rig.manager.run("part.import-file", {path}));
    const core::Component* wrap = findComponent(rig.doc, "Asm", core::kRootComponentId, false);
    QVERIFY(wrap);
    QCOMPARE(wrap->children.size(), size_t(4));  // Base, Top, Top:2, Sub
    QCOMPARE(rig.doc.bodiesOf(wrap->id).size(), size_t(1));
    QCOMPARE(rig.doc.bodiesOf(core::kRootComponentId).size(), size_t(1));  // the design's own box
  }

  void flatWhenStructureOffOrPlainPart() {
    QTemporaryDir dir;
    const std::string path = writeAssembly(dir);
    // keepStructure off: the old behaviour, one body per solid instance in the active component
    Document doc;
    auto imp = std::make_unique<part::ImportFeature>();
    imp->prop("filePath").setString(path);
    imp->prop("keepStructure").setBool(false);
    QVERIFY(!imp->wantsCommitPreparation());
    core::Feature* f = doc.addFeature(std::move(imp));
    doc.recompute();
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QCOMPARE(f->createdBodies().size(), size_t(5));
    QCOMPARE(doc.components().size(), size_t(1));
    // a single-part STEP has no structure: no components either
    Document part;
    part.setName("plain");
    auto box = std::make_unique<core::test::BoxFeature>();
    part.addFeature(std::move(box));
    part.recompute();
    const std::string plain = (dir.path() + "/plain.step").toStdString();
    std::string err;
    QVERIFY2(exportAssemblyStep(part, plain, {}, &err), err.c_str());
    std::shared_ptr<const part::ImportFeature::StepAssembly> a = part::ImportFeature::readStepAssembly(plain, &err);
    QVERIFY2(a, err.c_str());
    QVERIFY(!a->hasStructure());
    ImportRig rig;
    QVERIFY(rig.manager.run("part.import-file", {plain}));
    QCOMPARE(rig.doc.components().size(), size_t(1));
    QCOMPARE(rig.doc.featureAt(0)->createdBodies().size(), size_t(1));
  }

  void editTurningStructureOffRemovesComponents() {
    QTemporaryDir dir;
    const std::string path = writeAssembly(dir);
    ImportRig rig;
    QVERIFY(rig.manager.run("part.import-file", {path}));
    auto* imp = dynamic_cast<part::ImportFeature*>(rig.doc.featureAt(0));
    QVERIFY(imp);
    QCOMPARE(imp->structureComponents().size(), size_t(5));
    imp->prop("keepStructure").setBool(false);
    QVERIFY(imp->wantsCommitPreparation());
    imp->prepareCommit(rig.doc, nullptr);
    QCOMPARE(rig.doc.components().size(), size_t(1));
    QVERIFY(imp->structureComponents().empty());
  }
};

// P3: AP242 product properties of components survive export / import; the NIST CTC-01 AP242
// sample (tests/fixtures, public domain, NIST MBE PMI project) imports cleanly.
class StepInteropTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { core::test::registerTestFeatures(); }

  void ap242ComponentProperties() {
    QTemporaryDir dir;
    Document src;
    src.setName("Asm");
    TwoBoxes t = makeTwoBoxes(src);
    core::Component* a = src.component(t.compA);
    a->partNumber = "FC-2001";
    a->description = "Base block";
    a->material = "Steel S235";
    src.recompute();
    const std::string path = (dir.path() + "/props.step").toStdString();
    StepExportOptions o;
    o.schema = "AP242";
    std::string err;
    QVERIFY2(exportAssemblyStep(src, path, o, &err), err.c_str());
    QFile f(QString::fromStdString(path));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    QVERIFY(text.contains("AP242"));
    QVERIFY(text.contains("'FC-2001'"));
    ImportRig rig;
    QVERIFY(rig.manager.run("part.import-file", {path}));
    const core::Component* base = findComponent(rig.doc, a->name, core::kRootComponentId, false);
    QVERIFY(base);
    QCOMPARE(base->partNumber, std::string("FC-2001"));
    QCOMPARE(base->description, std::string("Base block"));
    QCOMPARE(base->material, std::string("Steel S235"));
    const core::Component* other = findComponent(rig.doc, src.component(t.compB)->name, core::kRootComponentId, false);
    QVERIFY(other && other->partNumber.empty());
  }

  void nistCtc01Ap242Fixture() {
    std::string root = __FILE__;
    root = root.substr(0, root.rfind("/src/"));
    const std::string path = root + "/tests/fixtures/nist_ctc_01_asme1_ap242-e1.stp";
    QVERIFY(QFileInfo::exists(QString::fromStdString(path)));
    part::ImportFeature::Imported imp;
    std::string err;
    QVERIFY2(part::ImportFeature::readFile(path, imp, &err), err.c_str());
    QVERIFY(!imp.shapes.empty());
    QVERIFY(imp.report.entities > 1000);
    QCOMPARE(imp.report.failedFaces, 0);
    QVERIFY(imp.report.valid);
    QVERIFY(imp.report.solids >= 1);
    QVERIFY(imp.report.faces > 50);
    // names through the assembly reader
    auto a = part::ImportFeature::readStepAssembly(path, &err);
    QVERIFY2(a, err.c_str());
    bool named = !a->topName.empty();
    for (const auto& [k, p] : a->parts) named = named || !p.name.empty();
    QVERIFY(named);
  }
};

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  int rc = 0;
  {
    StepImportTest t;
    rc |= QTest::qExec(&t, argc, argv);
  }
  {
    StepInteropTest t;
    rc |= QTest::qExec(&t, argc, argv);
  }
  return rc;
}
#include "test_step_import.moc"
