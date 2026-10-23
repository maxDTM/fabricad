#include "core/Commands.h"
#include "core/Document.h"
#include "core/Materials.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"
#include "support/TestFeatures.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad::core;
using namespace fabricad::core::test;
namespace core = fabricad::core;

namespace {
/// References another feature directly (like an extrude its sketch); counts its builds.
class ConsumerFeature : public Feature {
 public:
  ConsumerFeature() : Feature("TestConsumer") { addRef("src", mask(ShapeKind::Sketch)); }
  void recompute(RecomputeContext&) override { ++builds; }
  int builds = 0;
};
}  // namespace

class DocumentTest : public QObject {
  Q_OBJECT
 private slots:

  // production-features P1 §2.2: material library fields, search and the user library
  void materialLibraryUserFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray old = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    core::reloadMaterialLibrary();
    QVERIFY(QString::fromStdString(core::userMaterialLibraryPath()).startsWith(dir.path()));
    QCOMPARE(core::materialLibrary().size(), core::builtinMaterials().size());
    const core::Material* al = core::findMaterial("aluminum 6061");
    QVERIFY(al && al->standard == "EN AW-6061-T6" && al->yieldMPa == 276 && al->source == "builtin");
    // search: name, category and standard, case-insensitive; category filter
    QVERIFY(!core::searchMaterials("6061").empty());
    QVERIFY(!core::searchMaterials("astm a36").empty());
    QCOMPARE(core::searchMaterials("", "Wood").size(), size_t(11));
    QVERIFY(core::findMaterial("PETG") && core::findMaterial("PA12 (SLS/MJF)") && core::findMaterial("Walnut"));
    QVERIFY(core::searchMaterials("steel", "Plastic").empty());
    // a user material is added, a user entry overrides a built-in by name, both survive a reload
    core::Material pet{"PETG-CF Custom", "Plastic", 1.27, {0.2f, 0.4f, 0.8f, 1.0f}, "ISO 1043 PETG", 50, 25.0};
    std::string err;
    QVERIFY2(core::saveUserMaterial(pet, &err), err.c_str());
    core::Material steel = *core::findMaterial("Steel");
    steel.density = 7.80;
    QVERIFY(core::saveUserMaterial(steel, &err));
    QVERIFY(QFileInfo::exists(QString::fromStdString(core::userMaterialLibraryPath())));
    core::reloadMaterialLibrary();  // "restart"
    const core::Material* p = core::findMaterial("petg-cf custom");
    QVERIFY(p && p->source == "user" && std::abs(p->density - 1.27) < 1e-12 && p->costPerKg == 25.0);
    QVERIFY(std::abs(core::findMaterial("Steel")->density - 7.80) < 1e-12);
    QCOMPARE(core::materialLibrary().size(), core::builtinMaterials().size() + 1);
    QVERIFY(core::removeUserMaterial("Steel", &err));
    QVERIFY(std::abs(core::findMaterial("Steel")->density - 7.85) < 1e-12);
    QVERIFY(!core::removeUserMaterial("Water", &err));  // built-in
    // a broken file is ignored
    {
      QFile f(QString::fromStdString(core::userMaterialLibraryPath()));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("{not json");
    }
    core::reloadMaterialLibrary();
    QCOMPARE(core::materialLibrary().size(), core::builtinMaterials().size());
    if (old.isEmpty()) qunsetenv("XDG_CONFIG_HOME");
    else qputenv("XDG_CONFIG_HOME", old);
    core::reloadMaterialLibrary();
  }
  void initTestCase() { registerTestFeatures(); }

  void addAndRecompute() {
    Document doc;
    QSignalSpy added(&doc, &Document::featureAdded);
    QSignalSpy bodyChanged(&doc, &Document::bodyChanged);
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    QVERIFY(box);
    QCOMPARE(box->name(), std::string("TestBox1"));
    QCOMPARE(added.count(), 1);
    QCOMPARE(doc.rollbackIndex(), 1);
    QVERIFY(doc.needsRecompute());
    doc.recompute();
    QVERIFY(!doc.needsRecompute());
    QCOMPARE(box->status(), FeatureStatus::Ok);
    QCOMPARE(box->createdBodies().size(), size_t(1));
    ObjectId bodyId = box->createdBodies()[0];
    QVERIFY(doc.body(bodyId));
    QCOMPARE(doc.body(bodyId)->creator, box->id());
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - 10 * 20 * 30) < 1e-6);
    QVERIFY(bodyChanged.count() >= 1);
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    QVERIFY(doc.isModified());
  }

  void featureChainAndRollback() {
    Document doc;
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    ObjectId bodyId = box->createdBodies()[0];
    double full = volume(doc.bodyShape(bodyId));

    auto hole = std::make_unique<HoleFeature>();
    hole->prop("body").setRef(ShapeRef::body(bodyId));
    Feature* holeF = doc.addFeature(std::move(hole));
    doc.recompute();
    QCOMPARE(holeF->status(), FeatureStatus::Ok);
    double cut = volume(doc.bodyShape(bodyId));
    QVERIFY(cut < full);
    QVERIFY(holeF->touchedBodies().size() == 1);

    // dependencies: hole depends on box (through the body)
    auto deps = doc.dependenciesOf(holeF->id());
    QCOMPARE(deps.size(), size_t(1));
    QCOMPARE(deps[0], box->id());
    auto dependents = doc.dependentsOf(box->id());
    QCOMPARE(dependents.size(), size_t(1));
    QCOMPARE(dependents[0], holeF->id());
    std::string why;
    QVERIFY(!doc.canMoveFeature(holeF->id(), 0, &why));
    QVERIFY(!why.empty());

    // rollback before the hole: body shape is the full box again
    doc.setRollbackIndex(1);
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - full) < 1e-6);
    QCOMPARE(holeF->status(), FeatureStatus::Stale);
    QVERIFY(!doc.isFeatureActive(holeF->id()));
    // features added at the marker go between box and hole
    Feature* box2 = doc.addFeature(std::make_unique<BoxFeature>());
    QCOMPARE(doc.indexOf(box2->id()), 1);
    QCOMPARE(doc.indexOf(holeF->id()), 2);
    doc.setRollbackIndex(3);  // forward: recomputes
    QCOMPARE(holeF->status(), FeatureStatus::Ok);
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - cut) < 1e-6);
    QCOMPARE(doc.liveBodies().size(), size_t(2));

    // suppress the hole
    holeF->setSuppressed(true);
    doc.notifyFeatureChanged(holeF->id());
    doc.recompute();
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - full) < 1e-6);
  }

  void errorsAndPropagation() {
    Document doc;
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    box->prop("dx").setDouble(-1.0);
    auto hole = std::make_unique<HoleFeature>();
    Feature* holeF = doc.addFeature(std::move(hole));  // no body ref -> error of its own
    doc.recompute();
    QCOMPARE(box->status(), FeatureStatus::Error);
    QVERIFY(box->statusMessage().find("positive") != std::string::npos);
    QCOMPARE(holeF->status(), FeatureStatus::Error);
    QCOMPARE(doc.liveBodies().size(), size_t(0));
    // fix the box
    box->prop("dx").setDouble(10.0);
    doc.notifyFeatureChanged(box->id());
    doc.recompute();
    QCOMPARE(box->status(), FeatureStatus::Ok);
    // Failing feature upstream of a hole -> hole warns
    Document doc2;
    Feature* b = doc2.addFeature(std::make_unique<BoxFeature>());
    doc2.recompute();
    doc2.addFeature(std::make_unique<FailingFeature>());
    auto h2 = std::make_unique<HoleFeature>();
    h2->prop("body").setRef(ShapeRef::body(b->createdBodies()[0]));
    Feature* h2f = doc2.addFeature(std::move(h2));
    doc2.recompute();
    QCOMPARE(h2f->status(), FeatureStatus::Ok);  // the failing feature is not an input of the hole
  }

  void suppressedOrFailedInputSkipsDependent() {
    // DOC-05: a suppressed or failed direct input skips the dependent and flags it
    Document doc;
    Feature* src = doc.addFeature(std::make_unique<BoxFeature>());
    auto c = std::make_unique<ConsumerFeature>();
    c->prop("src").setRef(ShapeRef::feature(src->id(), ShapeKind::Sketch));
    auto* consumer = static_cast<ConsumerFeature*>(doc.addFeature(std::move(c)));
    auto c2 = std::make_unique<ConsumerFeature>();
    c2->prop("src").setRef(ShapeRef::feature(consumer->id(), ShapeKind::Sketch));
    auto* second = static_cast<ConsumerFeature*>(doc.addFeature(std::move(c2)));
    doc.recompute();
    QCOMPARE(consumer->status(), FeatureStatus::Ok);
    QCOMPARE(consumer->builds, 1);
    src->setSuppressed(true);
    doc.notifyFeatureChanged(src->id());
    doc.recompute();
    QCOMPARE(consumer->status(), FeatureStatus::Error);
    QVERIFY2(consumer->statusMessage().find("suppressed") != std::string::npos, consumer->statusMessage().c_str());
    QCOMPARE(consumer->builds, 1);
    QCOMPARE(second->status(), FeatureStatus::Error);  // cascades
    QVERIFY(second->statusMessage().find("failed") != std::string::npos);
    src->setSuppressed(false);
    src->prop("dx").setDouble(-1.0);  // fails on its own
    doc.notifyFeatureChanged(src->id());
    doc.recompute();
    QCOMPARE(src->status(), FeatureStatus::Error);
    QCOMPARE(consumer->status(), FeatureStatus::Error);
    QVERIFY(consumer->statusMessage().find("failed") != std::string::npos);
    src->prop("dx").setDouble(10.0);
    doc.notifyFeatureChanged(src->id());
    doc.recompute();
    QCOMPARE(consumer->status(), FeatureStatus::Ok);
    QCOMPARE(second->status(), FeatureStatus::Ok);
    QCOMPARE(consumer->builds, 2);
  }

  void parametersAndExpressions() {
    Document doc;
    QVERIFY(doc.setParameter("width", "40 mm"));
    QVERIFY(doc.setParameter("half", "width / 2"));
    QVERIFY(!doc.setParameter("mm", "1"));
    QVERIFY(doc.parameter("half")->ok);
    QCOMPARE(doc.parameter("half")->value.value, 20.0);
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    box->prop("dx").setExpression("half");
    box->prop("dy").setExpression("width + 10");
    doc.recompute();
    QCOMPARE(box->status(), FeatureStatus::Ok);
    QCOMPARE(box->prop("dx").toDouble(), 20.0);
    QCOMPARE(box->prop("dy").toDouble(), 50.0);
    QVERIFY(std::abs(volume(doc.bodyShape(box->createdBodies()[0])) - 20 * 50 * 30) < 1e-6);
    // change a parameter -> everything dirty
    doc.setParameter("width", "20 mm");
    QVERIFY(doc.needsRecompute());
    doc.recompute();
    QCOMPARE(box->prop("dx").toDouble(), 10.0);
    // bad expression -> error status
    box->prop("dz").setExpression("nonsense * 2");
    doc.notifyFeatureChanged(box->id());
    doc.recompute();
    QCOMPARE(box->status(), FeatureStatus::Error);
    // cyclic parameters fail gracefully
    doc.setParameter("a", "b + 1");
    doc.setParameter("b", "a + 1");
    QVERIFY(!doc.parameter("a")->ok);
    QVERIFY(doc.removeParameter("a"));
  }

  void undoCommands() {
    Document doc;
    UndoStack& undo = doc.undoStack();
    undo.push(std::make_unique<AddFeatureCommand>(doc, std::make_unique<BoxFeature>()));
    QCOMPARE(doc.featureCount(), 1);
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    ObjectId boxId = doc.featureAt(0)->id();
    ObjectId bodyId = doc.featureAt(0)->createdBodies()[0];
    undo.push(std::make_unique<ModifyFeatureCommand>(doc, boxId, [](Feature& f) { f.prop("dx").setDouble(5.0); }));
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - 5 * 20 * 30) < 1e-6);
    undo.undo();
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - 10 * 20 * 30) < 1e-6);
    undo.redo();
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - 5 * 20 * 30) < 1e-6);
    const std::string bodyName = doc.body(bodyId)->name;
    doc.body(bodyId)->color = {1.0f, 0.0f, 0.0f, 1.0f};
    doc.body(bodyId)->material = "ABS";
    Feature* other = doc.addFeature(std::make_unique<BoxFeature>());  // takes the next name
    doc.recompute();
    undo.push(std::make_unique<RemoveFeatureCommand>(doc, boxId));
    QCOMPARE(doc.featureCount(), 1);
    QVERIFY(!doc.body(bodyId));
    undo.undo();
    QCOMPARE(doc.featureCount(), 2);
    QVERIFY(doc.body(bodyId));
    // DOC-07: the body comes back as it was, not as a fresh "Body2"
    QCOMPARE(doc.body(bodyId)->name, bodyName);
    QCOMPARE(doc.body(bodyId)->color[1], 0.0f);
    QCOMPARE(doc.body(bodyId)->material, std::string("ABS"));
    doc.takeFeature(other->id());
    doc.recompute();
    QCOMPARE(doc.featureCount(), 1);
    QVERIFY(std::abs(volume(doc.bodyShape(bodyId)) - 5 * 20 * 30) < 1e-6);
    undo.undo();  // modify
    undo.undo();  // add
    QCOMPARE(doc.featureCount(), 0);
    QVERIFY(!undo.canUndo());
    undo.redo();
    QCOMPARE(doc.featureCount(), 1);
    // parameter command
    undo.push(std::make_unique<SetParameterCommand>(doc, "w", "7 mm"));
    QVERIFY(doc.parameter("w"));
    undo.undo();
    QVERIFY(!doc.parameter("w"));
    // rollback command
    undo.push(std::make_unique<SetRollbackCommand>(doc, 0));
    QCOMPARE(doc.liveBodies().size(), size_t(0));
    undo.undo();
    QCOMPARE(doc.liveBodies().size(), size_t(1));
    // body edit
    undo.push(makeBodyEditCommand(doc, bodyId, [](Body& b) { b.visible = false; b.name = "Base"; }));
    QVERIFY(!doc.body(bodyId)->visible);
    QCOMPARE(doc.liveBodies(true).size(), size_t(0));
    undo.undo();
    QVERIFY(doc.body(bodyId)->visible);
  }

  void jsonRoundTrip() {
    Document doc;
    doc.setName("Bracket");
    doc.setParameter("width", "40 mm", "overall width");
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    box->prop("dx").setExpression("width");
    doc.recompute();
    ObjectId bodyId = box->createdBodies()[0];
    auto hole = std::make_unique<HoleFeature>();
    hole->prop("body").setRef(ShapeRef::body(bodyId));
    Feature* holeF = doc.addFeature(std::move(hole));
    doc.recompute();
    Component& sub = doc.addComponent("Sub");
    sub.placement.SetTranslation(gp_Vec(1, 2, 3));
    doc.body(bodyId)->color = {1, 0, 0, 1};
    QJsonObject ext;
    ext["sheets"] = 2;
    doc.setExtensionData("drawing", ext);
    double v = volume(doc.bodyShape(bodyId));

    QJsonObject json = doc.toJson();
    Document copy;
    std::string err;
    QVERIFY2(copy.fromJson(json, &err), err.c_str());
    QCOMPARE(copy.name(), std::string("Bracket"));
    QCOMPARE(copy.featureCount(), 2);
    QCOMPARE(copy.featureAt(0)->type(), std::string("TestBox"));
    QCOMPARE(copy.featureAt(0)->id(), box->id());
    QCOMPARE(copy.featureAt(1)->id(), holeF->id());
    QCOMPARE(copy.featureAt(0)->prop("dx").expression(), std::string("width"));
    QVERIFY(copy.body(bodyId));
    QCOMPARE(copy.body(bodyId)->color[0], 1.0f);
    QVERIFY(std::abs(volume(copy.bodyShape(bodyId)) - v) < 1e-6);
    QCOMPARE(copy.components().size(), size_t(2));
    QVERIFY(copy.component(sub.id));
    QCOMPARE(copy.component(sub.id)->placement.TranslationPart().Y(), 2.0);
    QCOMPARE(copy.extensionData("drawing")["sheets"].toInt(), 2);
    QVERIFY(!copy.isModified());
    // new ids do not collide
    QVERIFY(copy.allocateId() > holeF->id());

    // unknown feature type -> placeholder, still saved back
    QJsonObject j2 = json;
    QJsonArray feats = j2["features"].toArray();
    QJsonObject f0 = feats[0].toObject();
    f0["type"] = "FutureFeature";
    feats[0] = f0;
    j2["features"] = feats;
    Document d3;
    QVERIFY(d3.fromJson(j2, &err));
    QCOMPARE(d3.featureAt(0)->status(), FeatureStatus::Error);
    QCOMPARE(d3.toJson()["features"].toArray()[0].toObject()["type"].toString(), QString("FutureFeature"));
  }

  void saveAndLoad() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    std::string path = (dir.path() + "/test.fcad").toStdString();
    Document doc;
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    std::string err;
    QVERIFY2(doc.save(path, &err), err.c_str());
    QVERIFY(!doc.isModified());
    QCOMPARE(doc.filePath(), path);
    Document other;
    QSignalSpy reset(&other, &Document::documentReset);
    QSignalSpy loaded(&other, &Document::loaded);
    QVERIFY2(other.load(path, &err), err.c_str());
    QCOMPARE(reset.count(), 1);
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(other.featureCount(), 1);
    QCOMPARE(other.featureAt(0)->id(), box->id());
    QCOMPARE(other.liveBodies().size(), size_t(1));
    QVERIFY(!other.load("/nonexistent/file.fcad", &err));
    QVERIFY(!err.empty());
    other.clear();
    QCOMPARE(other.featureCount(), 0);
    QCOMPARE(other.components().size(), size_t(1));
  }

  void components() {
    Document doc;
    Component& a = doc.addComponent("A");
    Component& b = doc.addComponent("B", a.id);
    a.placement.SetTranslation(gp_Vec(10, 0, 0));
    b.placement.SetTranslation(gp_Vec(0, 5, 0));
    gp_Trsf w = doc.worldPlacement(b.id);
    QCOMPARE(w.TranslationPart().X(), 10.0);
    QCOMPARE(w.TranslationPart().Y(), 5.0);
    doc.setActiveComponent(b.id);
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    ObjectId bodyId = box->createdBodies()[0];
    QCOMPARE(doc.body(bodyId)->component, b.id);
    QCOMPARE(doc.bodiesOf(b.id).size(), size_t(1));
    QVERIFY(doc.removeComponent(a.id));  // removes b too, body moves to root
    QCOMPARE(doc.body(bodyId)->component, kRootComponentId);
    QCOMPARE(doc.activeComponent(), kRootComponentId);
    QVERIFY(!doc.removeComponent(kRootComponentId));
  }

  void componentRecordDependsOnBodyProducers() {
    // DOC-06: a record of a component (New Component) cannot move before the feature that
    // made the component's body
    Document doc;
    Component& c = doc.addComponent("C");
    doc.setActiveComponent(c.id);
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    doc.setActiveComponent(kRootComponentId);
    auto rec = std::make_unique<ConsumerFeature>();
    rec->prop("src").setRef(ShapeRef::component(c.id));
    Feature* record = doc.addFeature(std::move(rec));
    doc.recompute();
    const auto deps = doc.dependenciesOf(record->id());
    QCOMPARE(deps.size(), size_t(1));
    QCOMPARE(deps[0], box->id());
    std::string why;
    QVERIFY(!doc.canMoveFeature(record->id(), 0, &why));
    QVERIFY(why.find(box->name()) != std::string::npos);
    QVERIFY(!doc.moveFeature(record->id(), 0));
    QCOMPARE(doc.indexOf(record->id()), 1);
  }

  void modifiedFollowsUndoCleanState() {
    // DOC-09: undo/redo back to the saved step clears the modified flag; leaving it sets it
    QTemporaryDir dir;
    Document doc;
    UndoStack& undo = doc.undoStack();
    undo.push(std::make_unique<AddFeatureCommand>(doc, std::make_unique<BoxFeature>()));
    QVERIFY(doc.isModified());
    QVERIFY(doc.save(dir.filePath("a.fcad").toStdString()));
    QVERIFY(!doc.isModified());
    const ObjectId boxId = doc.featureAt(0)->id();
    undo.push(std::make_unique<ModifyFeatureCommand>(doc, boxId, [](Feature& f) { f.prop("dx").setDouble(5.0); }));
    QVERIFY(doc.isModified());
    undo.undo();
    QVERIFY(!doc.isModified());  // back at the saved step
    undo.redo();
    QVERIFY(doc.isModified());
    undo.undo();
    undo.undo();
    QVERIFY(doc.isModified());  // before the saved step
    undo.redo();
    QVERIFY(!doc.isModified());
    // a change no step records (a recovered file): no undo gets back to the saved state
    doc.setModified(true);
    undo.resetClean();
    QVERIFY(doc.isModified());
    QVERIFY(!undo.isClean());
    undo.undo();
    undo.redo();
    QVERIFY(doc.isModified());
    // a merge into the saved step leaves it
    UndoStack s;
    struct Merging : UndoCommand {
      void redo() override {}
      void undo() override {}
      int mergeId() const override { return 7; }
      bool mergeWith(const UndoCommand&) override { return true; }
    };
    s.push(std::make_unique<Merging>());
    s.setClean();
    QSignalSpy cleanSpy(&s, &UndoStack::cleanChanged);
    s.push(std::make_unique<Merging>());
    QVERIFY(!s.isClean());
    QCOMPARE(cleanSpy.count(), 1);
  }

  void moveFeature() {
    Document doc;
    Feature* b1 = doc.addFeature(std::make_unique<BoxFeature>());
    Feature* b2 = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    QVERIFY(doc.moveFeature(b2->id(), 0));
    QCOMPARE(doc.indexOf(b2->id()), 0);
    QCOMPARE(doc.indexOf(b1->id()), 1);
    QCOMPARE(doc.rollbackIndex(), 2);
    doc.recompute();
    QCOMPARE(doc.liveBodies().size(), size_t(2));
  }
};

QTEST_GUILESS_MAIN(DocumentTest)
#include "test_document.moc"
