// Unit tests for the GL-free display model (what the viewer shows for a Document).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/SceneModel.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/tests/support/TestFeatures.h"
#include "support/ViewerTestFeatures.h"

#include <QtTest/QtTest>

using namespace fabricad;
using namespace fabricad::viewer;
using core::ShapeKind;

namespace {
const SceneItem* find(const std::vector<SceneItem>& items, const std::string& key) {
  for (const auto& it : items)
    if (it.key == key) return &it;
  return nullptr;
}
int countStyle(const std::vector<SceneItem>& items, SceneStyle s) {
  int n = 0;
  for (const auto& it : items) n += it.style == s ? 1 : 0;
  return n;
}
}  // namespace

class SceneModelTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { core::test::registerTestFeatures(); }

  void bodiesFollowDocument() {
    core::Document doc;
    auto* box = doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    QCOMPARE(box->status(), core::FeatureStatus::Ok);
    SceneModel scene;
    SceneOptions opt;
    auto items = scene.collect(doc, opt);
    QCOMPARE(items.size(), size_t(1));
    const core::ObjectId bodyId = box->createdBodies().front();
    const SceneItem* b = find(items, "body:" + std::to_string(bodyId));
    QVERIFY(b);
    QCOMPARE(b->style, SceneStyle::Body);
    QCOMPARE(b->ref.kind, ShapeKind::Body);
    QVERIFY(!b->shape.IsNull());
    QVERIFY(b->geomHash != 0);
    const std::size_t h1 = b->geomHash;

    // geometry change -> new hash; same geometry -> same hash
    auto items2 = scene.collect(doc, opt);
    QCOMPARE(find(items2, b->key)->geomHash, h1);
    box->prop("dx").setDouble(15.0);
    doc.notifyFeatureChanged(box->id());
    doc.recompute();
    auto items3 = scene.collect(doc, opt);
    QVERIFY(find(items3, b->key)->geomHash != h1);

    // hidden body disappears
    doc.body(bodyId)->visible = false;
    doc.notifyBodyChanged(bodyId);
    QVERIFY(scene.collect(doc, opt).empty());
    doc.body(bodyId)->visible = true;

    // rolled back before creation -> nothing
    doc.setRollbackIndex(0);
    QVERIFY(scene.collect(doc, opt).empty());
    doc.setRollbackIndex(1);
    QCOMPARE(scene.collect(doc, opt).size(), size_t(1));
  }

  void colorAndComponentPlacement() {
    core::Document doc;
    core::Component& comp = doc.addComponent("Bracket");
    gp_Trsf t;
    t.SetTranslation(gp_Vec(100, 0, 0));
    comp.placement = t;
    doc.setActiveComponent(comp.id);
    auto* box = doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    const core::ObjectId bodyId = box->createdBodies().front();
    QCOMPARE(doc.body(bodyId)->component, comp.id);
    doc.body(bodyId)->color = {1.0f, 0.0f, 0.0f, 1.0f};
    SceneModel scene;
    SceneOptions opt;
    auto items = scene.collect(doc, opt);
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(std::abs(items[0].placement.TranslationPart().X() - 100.0) < 1e-12);
    QCOMPARE(items[0].color[0], 1.0f);
    QVERIFY(!items[0].ghosted);
    // in-context editing of another component ghosts it
    opt.activeComponents = {core::kRootComponentId};
    items = scene.collect(doc, opt);
    QVERIFY(items[0].ghosted);
    opt.activeComponents = {comp.id};
    QVERIFY(!scene.collect(doc, opt)[0].ghosted);
  }

  void sketchesAndConstruction() {
    core::Document doc;
    auto* sk = doc.addFeature(std::make_unique<test::FakeSketch>());
    auto* pl = doc.addFeature(std::make_unique<test::FakePlane>());
    doc.recompute();
    SceneModel scene;
    SceneOptions opt;
    auto items = scene.collect(doc, opt);
    const SceneItem* s = find(items, "feat:" + std::to_string(sk->id()));
    QVERIFY(s);
    QCOMPARE(s->style, SceneStyle::SketchFree);
    QCOMPARE(s->ref.kind, ShapeKind::Sketch);
    const SceneItem* p = find(items, "feat:" + std::to_string(pl->id()));
    QVERIFY(p);
    QCOMPARE(p->style, SceneStyle::ConstructionPlane);
    QCOMPARE(p->ref.kind, ShapeKind::ConstructionPlane);
    QCOMPARE(p->ref.owner, pl->id());

    // eye toggles and folder toggles
    opt.hiddenFeatures.insert(sk->id());
    items = scene.collect(doc, opt);
    QVERIFY(!find(items, "feat:" + std::to_string(sk->id())));
    QVERIFY(!SceneModel::featureVisible(doc, *sk, opt));
    opt.visibilityOverrides[sk->id()] = true;  // tool override wins
    QVERIFY(SceneModel::featureVisible(doc, *sk, opt));
    opt.visibilityOverrides.clear();
    opt.hiddenFeatures.clear();
    opt.showConstruction = false;
    QVERIFY(!find(scene.collect(doc, opt), "feat:" + std::to_string(pl->id())));
    opt.showConstruction = true;

    // suppressed / rolled back features are not displayed
    sk->setSuppressed(true);
    doc.notifyFeatureChanged(sk->id());
    doc.recompute();
    QVERIFY(!find(scene.collect(doc, opt), "feat:" + std::to_string(sk->id())));
    sk->setSuppressed(false);
    doc.notifyFeatureChanged(sk->id());
    doc.recompute();
    QVERIFY(find(scene.collect(doc, opt), "feat:" + std::to_string(sk->id())));

    // styled display keys replace the plain output
    sk->prop("styled").setBool(true);
    doc.notifyFeatureChanged(sk->id());
    doc.recompute();
    items = scene.collect(doc, opt);
    QVERIFY(!find(items, "feat:" + std::to_string(sk->id())));
    QCOMPARE(countStyle(items, SceneStyle::SketchConstruction), 1);
    QCOMPARE(countStyle(items, SceneStyle::SketchFree), 1);
    QVERIFY(find(items, "feat:" + std::to_string(sk->id()) + "/display:construction"));
  }

  void sketchPicking() {
    core::Document doc;
    auto* sk = doc.addFeature(std::make_unique<test::FakeSketch>());
    doc.recompute();
    auto ids = probeCurveIds(*sk->profileSource());
    QCOMPARE(ids, (std::vector<int>{1, 2}));
    SceneModel scene;
    // the very edge -> id; an equal copy (different TShape) -> matched by midpoint
    QCOMPARE(scene.sketchCurveForEdge(doc, sk->id(), sk->profileSource()->curveEdge(2)), 2);
    TopoDS_Edge copy = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    QCOMPARE(scene.sketchCurveForEdge(doc, sk->id(), copy), 1);
    TopoDS_Edge other = BRepBuilderAPI_MakeEdge(gp_Pnt(50, 0, 0), gp_Pnt(60, 0, 0)).Edge();
    QCOMPARE(scene.sketchCurveForEdge(doc, sk->id(), other), -1);
    const auto& pts = scene.sketchPoints(doc, sk->id());
    QCOMPARE(pts.size(), size_t(4));  // start/end of both lines
    QCOMPARE(pts[0].ref.kind, ShapeKind::SketchPoint);
    QCOMPARE(pts[0].ref.owner, sk->id());
    QVERIFY(pts[1].position.IsEqual(gp_Pnt(10, 0, 0), 1e-9));
  }

  void originAndStyles() {
    core::Document doc;
    SceneModel scene;
    SceneOptions opt;
    QVERIFY(scene.collect(doc, opt).empty());
    opt.showOriginPlanes = true;
    auto items = scene.collect(doc, opt);
    QCOMPARE(items.size(), size_t(3));
    QCOMPARE(items[0].ref.kind, ShapeKind::OriginPlane);
    QCOMPARE(items[0].ref.name, std::string("XY"));
    opt.showOrigin = true;
    items = scene.collect(doc, opt);
    QCOMPARE(items.size(), size_t(7));
    QVERIFY(find(items, "origin:Z"));
    QCOMPARE(find(items, "origin:O")->ref.kind, ShapeKind::OriginPoint);
    QCOMPARE(sketchStyleFromDisplayKey("display:fixed").value(), SceneStyle::SketchFixed);
    QVERIFY(!sketchStyleFromDisplayKey("End").has_value());
    QVERIFY(!sketchStyleFromDisplayKey("display:bogus").has_value());
    QVERIFY(styleColor(SceneStyle::ConstructionPlane)[3] < 0.5f);
  }
};

QTEST_GUILESS_MAIN(SceneModelTest)
#include "test_scene_model.moc"
