// Tests for Paste New (independent deep copy of a component with its own feature history)
// and the Components pattern type of Rectangular / Circular Pattern, driven through the
// headless ToolManager like the app does (undo / redo, edit, save / load).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblyModule.h"
#include "assembly/PasteNew.h"
#include "assembly/tools/ComponentTools.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/Naming.h"
#include "part/PartModule.h"
#include "part/features/Pattern.h"
#include "part/features/Primitives.h"
#include "part/tools/FeatureTool.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

using namespace fabricad;
using core::Document;
using core::ObjectId;
using core::ShapeRef;

namespace {

struct Harness {
  Document doc;
  core::ToolRegistry tools;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager manager{tools};
  ~Harness() { manager.cancel(); }
  Harness() {
    core::ModuleContext mctx{core::FeatureFactory::instance(), tools};
    part::registerModule(mctx);
    assembly::registerModule(mctx);
    manager.setDocument(&doc);
    manager.setViewer(&viewer);
    manager.setHost(&host);
  }
};

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

gp_Pnt worldOrigin(const Document& doc, ObjectId comp) { return gp_Pnt(0, 0, 0).Transformed(doc.worldPlacement(comp)); }

/// Adds a feature with `comp` active and recomputes (bodies take the active component).
core::Feature* addIn(Document& doc, ObjectId comp, std::unique_ptr<core::Feature> f) {
  doc.setActiveComponent(comp);
  core::Feature* raw = doc.addFeature(std::move(f));
  doc.recompute();
  doc.setActiveComponent(core::kRootComponentId);
  return raw;
}

std::unique_ptr<part::BoxFeature> box(double l, double w, double h) {
  auto b = std::make_unique<part::BoxFeature>();
  b->prop("length").setDouble(l);
  b->prop("width").setDouble(w);
  b->prop("height").setDouble(h);
  b->prop("operation").setEnum("New Body");
  return b;
}

ShapeRef topFace(const Document& doc, ObjectId body) {
  for (const auto& f : core::subShapes(doc.bodyShape(body), TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && pl->Direction().IsEqual(gp::DZ(), 1e-9)) {
      ShapeRef r = ShapeRef::subShape(body, doc.bodyShape(body), f);
      part::annotateRef(doc, r);
      return r;
    }
  }
  return {};
}

std::vector<ObjectId> componentsNamed(const Document& doc, const std::string& name) {
  std::vector<ObjectId> out;
  for (const core::Component* c : doc.components())
    if (c->name == name) out.push_back(c->id);
  return out;
}

std::vector<ObjectId> bodiesOf(const Document& doc, ObjectId comp) {
  std::vector<ObjectId> out;
  for (ObjectId b : doc.liveBodies())
    if (doc.body(b)->component == comp) out.push_back(b);
  return out;
}

}  // namespace

class PasteNewPatternTest : public QObject {
  Q_OBJECT
 private slots:
  void renameHistoryNames() {
    std::map<std::string, std::string> m{{"Box1", "Box3"}, {"Cylinder1", "Cylinder2"}};
    QCOMPARE(assembly::renameHistoryName("Box1/Top", m), std::string("Box3/Top"));
    QCOMPARE(assembly::renameHistoryName("{Box1/Top|Cylinder1/Side}", m), std::string("{Box3/Top|Cylinder2/Side}"));
    QCOMPARE(assembly::renameHistoryName("Box12/Top", m), std::string("Box12/Top"));  // whole names only
    QCOMPARE(assembly::renameHistoryName("XY", m), std::string("XY"));
  }

  void pasteNewIsIndependent() {
    Harness h;
    Document& doc = h.doc;
    ObjectId comp = doc.addComponent("Bracket").id;
    core::Feature* boxF = addIn(doc, comp, box(40, 20, 5));
    ObjectId body = boxF->createdBodies().front();
    // a hole whose plane is the box's top face (cross-feature reference)
    auto cyl = std::make_unique<part::CylinderFeature>();
    cyl->prop("plane").setRef(topFace(doc, body));
    cyl->prop("diameter").setDouble(6);
    cyl->prop("height").setDouble(-5);
    cyl->prop("operation").setEnum("Cut");
    core::Feature* cylF = addIn(doc, comp, std::move(cyl));
    QVERIFY2(cylF->status() == core::FeatureStatus::Ok, cylF->statusMessage().c_str());
    const double vol = 4000 - M_PI * 9 * 5;
    QVERIFY(near(core::volume(doc.bodyShape(body)), vol));
    gp_Trsf t;
    t.SetTranslation(gp_Vec(0, 50, 0));
    doc.component(comp)->placement = t;
    doc.notifyComponentChanged(comp);

    assembly::setComponentClipboard({comp});
    QVERIFY(h.manager.run("assembly.paste-new"));
    h.manager.cancel();  // Paste New starts Move like Fusion
    std::vector<ObjectId> copies = componentsNamed(doc, "Bracket (1)");
    QCOMPARE(copies.size(), size_t(1));
    ObjectId copy = copies.front();
    QVERIFY(doc.definitionOf(copy) == copy);  // its own definition, not an occurrence
    QVERIFY(worldOrigin(doc, copy).Distance(gp_Pnt(0, 50, 0)) < 1e-9);
    QCOMPARE(doc.featureCount(), 4);
    std::vector<ObjectId> copyBodies = bodiesOf(doc, copy);
    QCOMPARE(copyBodies.size(), size_t(1));
    QVERIFY(copyBodies.front() != body);
    QVERIFY(near(core::volume(doc.bodyShape(copyBodies.front())), vol));
    // the copied features belong to the copy and are healthy
    std::vector<core::Feature*> copiedFeatures;
    for (int i = 0; i < doc.featureCount(); ++i)
      if (doc.featureAt(i)->component() == copy) copiedFeatures.push_back(doc.featureAt(i));
    QCOMPARE(copiedFeatures.size(), size_t(2));
    for (core::Feature* f : copiedFeatures) QVERIFY2(f->status() == core::FeatureStatus::Ok, f->statusMessage().c_str());
    QCOMPARE(copiedFeatures[1]->ref("plane").owner, copyBodies.front());

    // one undo step
    doc.undoStack().undo();
    doc.recompute();
    QVERIFY(componentsNamed(doc, "Bracket (1)").empty());
    QCOMPARE(doc.featureCount(), 2);
    doc.undoStack().redo();
    doc.recompute();
    QCOMPARE(componentsNamed(doc, "Bracket (1)").size(), size_t(1));
    QCOMPARE(doc.featureCount(), 4);

    // editing the copy's box leaves the original alone (independent history)
    core::Feature* copyBox = nullptr;
    for (int i = 0; i < doc.featureCount(); ++i)
      if (doc.featureAt(i)->component() == copy && doc.featureAt(i)->type() == "Box") copyBox = doc.featureAt(i);
    QVERIFY(copyBox);
    copyBox->prop("length").setDouble(60);
    doc.notifyFeatureChanged(copyBox->id());
    doc.recompute();
    copyBodies = bodiesOf(doc, copy);
    QCOMPARE(copyBodies.size(), size_t(1));
    QVERIFY(near(core::volume(doc.bodyShape(copyBodies.front())), 6000 - M_PI * 9 * 5));
    QVERIFY(near(core::volume(doc.bodyShape(body)), vol));

    // a second Paste New gets the next name; save / load keeps both copies
    QVERIFY(h.manager.run("assembly.paste-new"));
    h.manager.cancel();
    QCOMPARE(componentsNamed(doc, "Bracket (2)").size(), size_t(1));
    QTemporaryDir dir;
    const std::string path = dir.filePath("paste.fcad").toStdString();
    QVERIFY(doc.save(path));
    Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QCOMPARE(re.liveBodies().size(), size_t(3));
    QVERIFY(near(core::volume(re.bodyShape(copyBodies.front())), 6000 - M_PI * 9 * 5));
  }

  void rectangularComponentPattern() {
    Harness h;
    Document& doc = h.doc;
    ObjectId peg = doc.addComponent("Peg").id;
    addIn(doc, peg, box(10, 10, 10));
    QVERIFY(h.manager.run("part.rectangular-pattern"));
    auto* tool = dynamic_cast<part::FeatureTool*>(h.manager.activeTool());
    QVERIFY(tool);
    part::PartFeature* f = tool->feature();
    f->prop("patternType").setEnum("Components");
    f->prop("objects").setRefs({ShapeRef::component(peg)});
    f->prop("direction1").setRef(ShapeRef::originAxis("X"));
    f->prop("quantity1").setInt(3);
    f->prop("distance1").setDouble(40);  // extent: instances at 0, 20, 40
    tool->updatePreview(*h.manager.activeContext());
    QVERIFY2(tool->isValid(), tool->message().c_str());
    QCOMPARE(tool->lastOutcome().previews.size(), size_t(2));
    h.manager.accept();
    ObjectId patternId = doc.featureAt(doc.featureCount() - 1)->id();
    auto* pattern = dynamic_cast<part::TransformFeature*>(doc.feature(patternId));
    QVERIFY(pattern);
    QCOMPARE(pattern->status(), core::FeatureStatus::Ok);
    std::vector<ObjectId> pegs = componentsNamed(doc, "Peg");
    QCOMPARE(pegs.size(), size_t(3));
    std::vector<ObjectId> inst = pattern->instanceComponents();
    QCOMPARE(inst.size(), size_t(2));
    for (ObjectId id : inst) QCOMPARE(doc.definitionOf(id), peg);  // occurrences of the same part
    QVERIFY(worldOrigin(doc, inst[0]).Distance(gp_Pnt(20, 0, 0)) < 1e-9);
    QVERIFY(worldOrigin(doc, inst[1]).Distance(gp_Pnt(40, 0, 0)) < 1e-9);

    // parametric: a new distance moves the occurrences on recompute
    pattern->prop("distance1").setDouble(80);
    doc.notifyFeatureChanged(patternId);
    doc.recompute();
    QVERIFY(worldOrigin(doc, inst[1]).Distance(gp_Pnt(80, 0, 0)) < 1e-9);
    pattern->prop("distance1").setDouble(40);
    doc.notifyFeatureChanged(patternId);
    doc.recompute();

    // undo removes the pattern and its occurrences in one step; redo brings both back
    doc.undoStack().undo();
    doc.recompute();
    QCOMPARE(componentsNamed(doc, "Peg").size(), size_t(1));
    QVERIFY(!doc.feature(patternId));
    doc.undoStack().redo();
    doc.recompute();
    QCOMPARE(componentsNamed(doc, "Peg").size(), size_t(3));
    pattern = dynamic_cast<part::TransformFeature*>(doc.feature(patternId));
    QVERIFY(pattern);
    QVERIFY(worldOrigin(doc, pattern->instanceComponents()[1]).Distance(gp_Pnt(40, 0, 0)) < 1e-9);

    // edit through the dialog: quantity 2 removes one occurrence (undoable)
    QVERIFY(h.manager.run("part.rectangular-pattern", {"edit", std::to_string(patternId)}));
    tool = dynamic_cast<part::FeatureTool*>(h.manager.activeTool());
    QVERIFY(tool && tool->isEditing());
    tool->feature()->prop("quantity1").setInt(2);
    tool->updatePreview(*h.manager.activeContext());
    h.manager.accept();
    QCOMPARE(componentsNamed(doc, "Peg").size(), size_t(2));
    QVERIFY(worldOrigin(doc, pattern->instanceComponents()[0]).Distance(gp_Pnt(40, 0, 0)) < 1e-9);
    doc.undoStack().undo();
    doc.recompute();
    QCOMPARE(componentsNamed(doc, "Peg").size(), size_t(3));

    // save / load keeps the occurrences and their link to the pattern
    QTemporaryDir dir;
    const std::string path = dir.filePath("pattern.fcad").toStdString();
    QVERIFY(doc.save(path));
    Document re;
    QVERIFY(re.load(path));
    re.recompute();
    QCOMPARE(componentsNamed(re, "Peg").size(), size_t(3));
    auto* rp = dynamic_cast<part::TransformFeature*>(re.feature(patternId));
    QVERIFY(rp && rp->instanceComponents().size() == 2);
    QVERIFY(worldOrigin(re, rp->instanceComponents()[1]).Distance(gp_Pnt(40, 0, 0)) < 1e-9);

    // switching the pattern back to Features removes the occurrences
    QVERIFY(h.manager.run("part.rectangular-pattern", {"edit", std::to_string(patternId)}));
    tool = dynamic_cast<part::FeatureTool*>(h.manager.activeTool());
    tool->feature()->prop("patternType").setEnum("Bodies");
    tool->feature()->prop("objects").setRefs({ShapeRef::body(bodiesOf(doc, peg).front())});
    tool->updatePreview(*h.manager.activeContext());
    h.manager.accept();
    QCOMPARE(componentsNamed(doc, "Peg").size(), size_t(1));
  }

  void circularComponentPattern() {
    Harness h;
    Document& doc = h.doc;
    ObjectId peg = doc.addComponent("Peg").id;
    addIn(doc, peg, box(10, 10, 10));
    gp_Trsf t;
    t.SetTranslation(gp_Vec(30, 0, 0));
    doc.component(peg)->placement = t;
    doc.notifyComponentChanged(peg);
    QVERIFY(h.manager.run("part.circular-pattern"));
    auto* tool = dynamic_cast<part::FeatureTool*>(h.manager.activeTool());
    QVERIFY(tool);
    part::PartFeature* f = tool->feature();
    f->prop("patternType").setEnum("Components");
    f->prop("objects").setRefs({ShapeRef::component(peg)});
    f->prop("axis").setRef(ShapeRef::originAxis("Z"));
    f->prop("quantity").setInt(4);
    tool->updatePreview(*h.manager.activeContext());
    QVERIFY2(tool->isValid(), tool->message().c_str());
    h.manager.accept();
    auto* pattern = dynamic_cast<part::TransformFeature*>(doc.featureAt(doc.featureCount() - 1));
    QVERIFY(pattern);
    std::vector<ObjectId> inst = pattern->instanceComponents();
    QCOMPARE(inst.size(), size_t(3));
    QVERIFY(worldOrigin(doc, inst[0]).Distance(gp_Pnt(0, 30, 0)) < 1e-9);
    QVERIFY(worldOrigin(doc, inst[1]).Distance(gp_Pnt(-30, 0, 0)) < 1e-9);
    QVERIFY(worldOrigin(doc, inst[2]).Distance(gp_Pnt(0, -30, 0)) < 1e-9);
    // the occurrence is rotated, not only moved: its local X axis points along world Y
    gp_Dir x = gp::DX().Transformed(doc.worldPlacement(inst[0]));
    QVERIFY(x.IsEqual(gp::DY(), 1e-9));
    doc.undoStack().undo();
    doc.recompute();
    QCOMPARE(componentsNamed(doc, "Peg").size(), size_t(1));
  }
};

QTEST_MAIN(PasteNewPatternTest)
#include "test_paste_new_pattern.moc"
