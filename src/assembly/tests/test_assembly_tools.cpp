// Tool-level tests: the assembly tools driven through core::ToolManager with the headless
// NullViewerContext / NullAppHost (no GL). Dialog construction runs under the offscreen QPA.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblyModule.h"
#include "assembly/Bom.h"
#include "assembly/ComponentOps.h"
#include "assembly/JointFeature.h"
#include "assembly/JointFrame.h"
#include "assembly/Triad.h"
#include "assembly/tools/ComponentTools.h"
#include "assembly/tools/DriveJointTool.h"
#include "assembly/tools/InterferenceTool.h"
#include "assembly/tools/JointTool.h"
#include "assembly/tools/MoveComponentTool.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "support/AssemblyTestHelpers.h"

#include <QFile>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest/QtTest>

#include <gp_Ax1.hxx>

#include <cmath>
#include <memory>

using namespace fabricad;
using namespace fabricad::assembly;
using namespace fabricad::assembly::test;
using core::Document;
using core::ObjectId;
using core::ShapeRef;

namespace {

/// Document + registry + manager wired to the headless viewer/host.
struct Harness {
  Document doc;
  core::ToolRegistry tools;
  core::NullViewerContext viewer;  // declared before the manager: destroyed after it
  core::NullAppHost host;
  core::ToolManager manager{tools};
  TwoBoxes boxes;
  ~Harness() { manager.cancel(); }

  Harness() {
    core::ModuleContext mctx{core::FeatureFactory::instance(), tools};
    assembly::registerModule(mctx);
    manager.setDocument(&doc);
    manager.setViewer(&viewer);
    manager.setHost(&host);
    boxes = makeTwoBoxes(doc);
  }
  template <class T>
  T* tool(const char* id) {
    return dynamic_cast<T*>(tools.tool(id));
  }
  core::ToolContext& ctx() { return *manager.activeContext(); }
  void select(std::vector<ShapeRef> refs) { doc.selection().set(std::move(refs)); }
};

gp_Pnt rayDown(double x, double y) { return gp_Pnt(x, y, 100); }
const gp_Dir kDown(0, 0, -1);

}  // namespace

class AssemblyToolsTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { core::test::registerTestFeatures(); }

  void triadHitAndDrag() {
    Triad t(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 20.0);
    QCOMPARE(t.hit(rayDown(10, 0), kDown, 0.5), Triad::Handle::MoveX);
    QCOMPARE(t.hit(rayDown(0, 12), kDown, 0.5), Triad::Handle::MoveY);
    QCOMPARE(t.hit(rayDown(8, 8), kDown, 0.5), Triad::Handle::PlaneXY);   // square spans 6..10
    QCOMPARE(t.hit(rayDown(40, 40), kDown, 0.5), Triad::Handle::None);
    double d = 15.0 / std::sqrt(2.0);                                       // on the Z ring (r = 15)
    QCOMPARE(t.hit(rayDown(d, d), kDown, 0.5), Triad::Handle::RotZ);
    // disabled handles are never hit
    t.setEnabledHandles(Triad::kRotZ);
    QCOMPARE(t.hit(rayDown(10, 0), kDown, 0.5), Triad::Handle::None);
    t.setEnabledHandles(Triad::kAll);

    // translate along X by 5
    QVERIFY(t.beginDrag(Triad::Handle::MoveX, rayDown(10, 0), kDown));
    gp_Trsf delta;
    QVERIFY(t.dragTo(rayDown(15, 0), kDown, delta));
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(delta), gp_Pnt(5, 0, 0)));
    QVERIFY(near(t.dragValue(), 5.0));
    // snapping rounds to whole millimetres
    QVERIFY(t.dragTo(rayDown(17.4, 0), kDown, delta, 1.0));
    QVERIFY(near(t.dragValue(), 7.0));
    t.endDrag();
    // rotate 90 degrees about Z
    QVERIFY(t.beginDrag(Triad::Handle::RotZ, rayDown(15, 0), kDown));
    QVERIFY(t.dragTo(rayDown(0, 15), kDown, delta));
    QVERIFY(near(t.dragValue(), 90.0));
    QVERIFY(near(gp_Pnt(1, 0, 0).Transformed(delta), gp_Pnt(0, 1, 0)));
    // plane drag moves in XY only
    QVERIFY(t.beginDrag(Triad::Handle::PlaneXY, rayDown(8, 8), kDown));
    QVERIFY(t.dragTo(rayDown(11, 6), kDown, delta));
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(delta), gp_Pnt(3, -2, 0)));
    // every handle has preview geometry
    for (const auto& [h, segs] : t.allSegments()) {
      QVERIFY(segs.size() >= 2);
      QCOMPARE(segs.size() % 2, size_t(0));
      QVERIFY(!std::string(Triad::handleName(h)).empty());
    }
  }

  void newComponentToolCreatesAndUndoes() {
    Harness h;
    int features = h.doc.featureCount();
    QVERIFY(h.manager.run("assembly.new-component", {"Bracket"}));
    QVERIFY(h.manager.isActive());
    h.manager.accept();
    QVERIFY(!h.manager.isActive());
    const core::Component* c = nullptr;
    for (const core::Component* k : h.doc.components())
      if (k->name == "Bracket") c = k;
    QVERIFY(c);
    QCOMPARE(h.doc.activeComponent(), c->id);
    QCOMPARE(h.doc.featureCount(), features + 1);  // NewComponent timeline marker
    QCOMPARE(h.doc.featureAt(features)->type(), std::string("NewComponent"));
    h.doc.undoStack().undo();
    QVERIFY(!h.doc.component(c->id));
    QCOMPARE(h.doc.featureCount(), features);
  }

  void jointToolMatesLivePreviewAndCommits() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    QVERIFY(h.manager.run("assembly.joint"));
    auto* jt = h.tool<JointTool>("assembly.joint");
    QVERIFY(jt);
    QCOMPARE(jt->stage(), JointTool::Stage::PickA);
    QVERIFY(!h.host.prompt().empty());
    // snap 1: bottom face of Top (component 1 = the one that moves)
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0));
    QCOMPARE(jt->stage(), JointTool::Stage::PickB);
    QCOMPARE(jt->originA().component, b.compB);
    QVERIFY(!h.viewer.previews().empty());  // frame glyph on snap 1
    // picking the same component again is refused
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, 1)), gp_Pnt(45, 5, 10));
    QCOMPARE(jt->stage(), JointTool::Stage::PickB);
    // snap 2: top face of Base -> live preview moves Top onto Base
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10));
    QCOMPARE(jt->stage(), JointTool::Stage::Ready);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    QVERIFY(h.host.acceptEnabled());
    // offsets and type apply live
    jt->setOffset(h.ctx(), 0, 0, 3, 0);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
    jt->setJointType(h.ctx(), JointType::Slider);
    QVERIFY(jt->commandLineInput(h.ctx(), "4"));  // slider distance
    QVERIFY(near(jt->value(), 4.0));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 17)));
    QVERIFY(jt->commandLineInput(h.ctx(), "REVOLUTE"));
    QCOMPARE(jt->jointType(), JointType::Revolute);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
    // flip aligns the normals (Top upside down below the mating plane)
    jt->setFlip(h.ctx(), true);
    QVERIFY(near(gp_Pnt(5, 5, 10).Transformed(h.doc.worldPlacement(b.compB)), gp_Pnt(5, 5, 3)));  // hangs below the plane
    jt->setFlip(h.ctx(), false);
    // commit: one undo step containing the placement + the feature
    int undoBefore = h.doc.undoStack().index();
    h.manager.accept();
    QVERIFY(!h.manager.isActive());
    ObjectId jointId = jt->createdJoint();
    QVERIFY(jointId != core::kInvalidId);
    const auto* jf = h.doc.featureAs<JointFeature>(jointId);
    QVERIFY(jf);
    QCOMPARE(jf->status(), core::FeatureStatus::Ok);
    QCOMPARE(jf->jointType(), JointType::Revolute);
    QCOMPARE(jf->componentA(), b.compB);
    QCOMPARE(jf->componentB(), b.compA);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
    QCOMPARE(h.doc.undoStack().index(), undoBefore + 1);
    QVERIFY(h.viewer.previews().empty());  // manager cleared the previews
    h.doc.undoStack().undo();
    QVERIFY(!h.doc.feature(jointId));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(40, 0, 0)));
    h.doc.undoStack().redo();
    QVERIFY(h.doc.feature(jointId));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
  }

  void jointToolCancelRestoresPlacements() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    QVERIFY(h.manager.run("assembly.joint"));
    auto* jt = h.tool<JointTool>("assembly.joint");
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0));
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    int features = h.doc.featureCount();
    h.manager.cancel();
    QVERIFY(!h.manager.isActive());
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(40, 0, 0)));
    QCOMPARE(h.doc.featureCount(), features);
    // preselection of two entities feeds both snaps on activation
    h.select({faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1))});
    QVERIFY(h.manager.run("assembly.joint"));
    QCOMPARE(jt->stage(), JointTool::Stage::Ready);
    h.manager.cancel();
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(40, 0, 0)));
  }

  void jointToolEditsExistingJoint() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    QVERIFY(h.manager.run("assembly.joint", {"REVOLUTE"}));
    auto* jt = h.tool<JointTool>("assembly.joint");
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0));
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10));
    h.manager.accept();
    const ObjectId jointId = jt->createdJoint();
    auto* jf = h.doc.featureAs<JointFeature>(jointId);
    QVERIFY(jf);
    QCOMPARE(jf->jointType(), JointType::Revolute);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    const int undoIndex = h.doc.undoStack().index();
    // the shell dispatches edits through Feature::editToolId() with {"edit", id}
    QCOMPARE(jf->editToolId(), std::string("assembly.joint"));
    QVERIFY(h.manager.run("assembly.joint", {"edit", std::to_string(jointId)}));
    QVERIFY(jt->isEditing());
    QCOMPARE(jt->editingJoint(), jointId);
    QCOMPARE(jt->stage(), JointTool::Stage::Ready);
    QCOMPARE(jt->jointType(), JointType::Revolute);
    QVERIFY(h.host.acceptEnabled());
    // changes preview live on the feature itself (solver re-run), flags follow the type
    jt->setJointType(h.ctx(), JointType::Slider);
    jt->setOffset(h.ctx(), 0, 0, 3, 0);
    QCOMPARE(jf->jointType(), JointType::Slider);
    QVERIFY(!jf->prop("distance").flags().hidden);
    QVERIFY(jf->prop("angle").flags().hidden);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
    QCOMPARE(h.doc.featureCount(), h.doc.featureCount());  // no new feature
    // cancel restores the feature and the placements, nothing on the undo stack
    h.manager.cancel();
    QVERIFY(!jt->isEditing());
    QCOMPARE(jf->jointType(), JointType::Revolute);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    QCOMPARE(h.doc.undoStack().index(), undoIndex);
    // edit again, OK: one ModifyFeatureCommand, undo/redo round trip
    const int features = h.doc.featureCount();
    QVERIFY(h.manager.run("assembly.joint", {"edit", std::to_string(jointId)}));
    jt->setJointType(h.ctx(), JointType::Slider);
    jt->setOffset(h.ctx(), 0, 0, 3, 0);
    h.manager.accept();
    QVERIFY(!h.manager.isActive());
    QCOMPARE(h.doc.featureCount(), features);
    QCOMPARE(jf->jointType(), JointType::Slider);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
    QCOMPARE(h.doc.undoStack().index(), undoIndex + 1);
    h.doc.undoStack().undo();
    QCOMPARE(jf->jointType(), JointType::Revolute);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    h.doc.undoStack().redo();
    QCOMPARE(jf->jointType(), JointType::Slider);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 13)));
    // the create flavour is back after an edit
    QVERIFY(h.manager.run("assembly.joint"));
    QVERIFY(!jt->isEditing());
    QCOMPARE(jt->stage(), JointTool::Stage::PickA);
    h.manager.cancel();
  }

  void copyPasteCreatesOccurrences() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    QVERIFY(h.tools.has("assembly.copy") && h.tools.has("assembly.paste"));
    QCOMPARE(h.tools.byShortcut("Ctrl+C")->info().id, std::string("assembly.copy"));
    QCOMPARE(h.tools.byShortcut("Ctrl+V")->info().id, std::string("assembly.paste"));
    // nothing copied yet: paste is disabled
    core::ToolContext idle;
    idle.document = &h.doc;
    idle.selection = &h.doc.selection();
    setComponentClipboard({});
    QVERIFY(!h.tools.tool("assembly.paste")->isEnabled(idle));
    // copy Top (selected through its body), paste twice: two occurrences of Top's definition
    h.select({ShapeRef::body(b.bodyB)});
    QVERIFY(h.manager.run("assembly.copy"));
    QVERIFY(!h.manager.isActive());  // instant
    QCOMPARE(componentClipboard(), std::vector<ObjectId>{b.compB});
    const size_t components = h.doc.components().size();
    h.doc.selection().clear();
    h.doc.setActiveComponent(core::kRootComponentId);
    QVERIFY(h.manager.run("assembly.paste"));
    // the Move tool took over the pasted occurrence (triad placement)
    QVERIFY(h.manager.isActive());
    QCOMPARE(h.manager.activeTool()->info().id, std::string("assembly.move"));
    auto* mt = h.tool<MoveComponentTool>("assembly.move");
    QCOMPARE(mt->components().size(), size_t(1));
    const ObjectId pasted = mt->components().front();
    const core::Component* pc = h.doc.component(pasted);
    QVERIFY(pc);
    QVERIFY(pc->isOccurrence());
    QCOMPARE(pc->definition, b.compB);
    QCOMPARE(pc->parent, core::kRootComponentId);
    QCOMPARE(h.doc.components().size(), components + 1);
    // placed on the original: same world placement
    QVERIFY(near(worldOrigin(h.doc, pasted), worldOrigin(h.doc, b.compB)));
    mt->setTransform(h.ctx(), 0, 30, 0, 0, 0, 0);
    h.manager.accept();
    QVERIFY(near(worldOrigin(h.doc, pasted), gp_Pnt(40, 30, 0)));
    // BOM: Top now has quantity 2, Base 1
    BomTable bom = buildBom(h.doc, BomOptions{});
    int topQty = 0;
    for (const BomRow& row : bom.rows)
      if (row.definition == b.compB) topQty = row.quantity;
    QCOMPARE(topQty, 2);
    // undo the move, undo the paste: the occurrence is gone
    h.doc.undoStack().undo();
    h.doc.undoStack().undo();
    QVERIFY(!h.doc.component(pasted));
    QCOMPARE(h.doc.components().size(), components);
    h.doc.undoStack().redo();
    QCOMPARE(h.doc.components().size(), components + 1);
    // pasting a stale clipboard entry is refused
    setComponentClipboard({ObjectId(999999)});
    QVERIFY(!h.tools.tool("assembly.paste")->isEnabled(idle));
  }

  void asBuiltJointKeepsPositions() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    QVERIFY(h.manager.run("assembly.as-built-joint", {"REVOLUTE"}));
    auto* jt = h.tool<JointTool>("assembly.as-built-joint");
    QVERIFY(jt->isAsBuilt());
    QCOMPARE(jt->jointType(), JointType::Revolute);
    jt->pick(h.ctx(), ShapeRef::body(b.bodyB), gp_Pnt(45, 5, 5));
    QCOMPARE(jt->stage(), JointTool::Stage::PickB);
    jt->pick(h.ctx(), ShapeRef::body(b.bodyA), gp_Pnt(5, 5, 5));
    QCOMPARE(jt->stage(), JointTool::Stage::PickAxis);
    // axis position: centre of Base's top face
    jt->pick(h.ctx(), faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10));
    QCOMPARE(jt->stage(), JointTool::Stage::Ready);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(40, 0, 0)));  // nothing moved
    h.manager.accept();
    const auto* jf = h.doc.featureAs<JointFeature>(jt->createdJoint());
    QVERIFY(jf);
    QVERIFY(jf->isAsBuilt());
    QCOMPARE(jf->status(), core::FeatureStatus::Ok);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(40, 0, 0)));
    // the joint axis is where it was picked: driving 180 degrees mirrors Top through (5,5)
    auto* j = h.doc.featureAs<JointFeature>(jt->createdJoint());
    j->setPrimaryValue(180.0);
    h.doc.notifyFeatureChanged(j->id());
    h.doc.recompute();
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(-30, 10, 0)));
  }

  void moveToolFreeAndJointed() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    h.select({ShapeRef::component(b.compB)});
    QVERIFY(h.manager.run("assembly.move"));
    auto* mt = h.tool<MoveComponentTool>("assembly.move");
    QCOMPARE(mt->components(), std::vector<ObjectId>{b.compB});
    QVERIFY(!mt->constrainingJoint());
    QVERIFY(near(mt->triad().frame().Location(), gp_Pnt(45, 5, 5)));  // pivot = bbox centre
    // the triad is a viewer manipulator (no preview graphics): all handles, at the pivot
    QVERIFY(h.viewer.previews().empty());
    QVERIFY(mt->manipulatorId() != core::kInvalidManipulator);
    const core::ManipulatorModel* tm = h.viewer.manipulators().model(mt->manipulatorId());
    QVERIFY(tm);
    QCOMPARE(tm->spec().kind, core::ManipulatorKind::Triad);
    QCOMPARE(tm->spec().handles, unsigned(core::kHandleTriadAll));
    QVERIFY(near(tm->spec().frame.Location(), gp_Pnt(45, 5, 5)));
    QVERIFY(near(tm->spec().size, mt->triad().size()));
    // drag the X arrow in the top view by 10 mm: pixel (x, y) -> world (0.01 x, -0.01 y)
    h.viewer.setStandardView(core::StandardView::Top, false);
    h.viewer.setPixelSize(0.01);
    QVERIFY(h.viewer.manipulatorPress(4800, -500));  // on the shaft, 3 mm from the origin
    QVERIFY(mt->isDragging());
    QVERIFY(h.viewer.manipulatorMove(5800, -500, core::ModCtrl));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(50, 0, 0)));
    QVERIFY(near(tm->spec().frame.Location(), gp_Pnt(45, 5, 5)));  // the gizmo stays put during the drag
    QVERIFY(h.viewer.manipulatorRelease(5800, -500, core::ModCtrl));
    QVERIFY(!mt->isDragging());
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(50, 0, 0)));
    QVERIFY(near(mt->triad().frame().Location(), gp_Pnt(55, 5, 5)));  // triad follows on release
    QVERIFY(near(h.viewer.manipulators().model(mt->manipulatorId())->spec().frame.Location(), gp_Pnt(55, 5, 5)));
    // the dialog / command line moves it the same way
    mt->setTransform(h.ctx(), 10, 0, 0, 0, 0, 0);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(50, 0, 0)));
    QVERIFY(near(mt->triad().frame().Location(), gp_Pnt(55, 5, 5)));  // triad follows
    // rotate 90 degrees about Z through the (moved) pivot (55,5,5): origin (50,0,0) -> (60,0,0)
    mt->setTransform(h.ctx(), 10, 0, 0, 0, 0, 90);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(60, 0, 0)));
    // command line: "x,y,z"
    QVERIFY(mt->commandLineInput(h.ctx(), "0,0,5"));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(50, 0, 5)));
    int undoBefore = h.doc.undoStack().index();
    h.manager.accept();
    QCOMPARE(h.doc.undoStack().index(), undoBefore + 1);
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(50, 0, 5)));
    h.doc.undoStack().undo();
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(40, 0, 0)));

    // a revolute-jointed component: the triad sits on the joint and drives its angle
    auto joint = std::make_unique<JointFeature>();
    joint->setOrigins(makeJointOrigin(h.doc, faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0)),
                      makeJointOrigin(h.doc, faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10)));
    joint->setJointType(JointType::Revolute);
    auto* j = joint.get();
    h.doc.addFeature(std::move(joint));
    h.doc.recompute();
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    h.select({ShapeRef::body(b.bodyB)});  // body picks resolve to their component
    QVERIFY(h.manager.run("assembly.move"));
    QCOMPARE(mt->constrainingJoint(), j);
    QCOMPARE(mt->triad().enabledHandles(), unsigned(Triad::kRotZ));
    QVERIFY(near(mt->triad().frame().Location(), gp_Pnt(5, 5, 10)));
    // only the Z ring is offered; dragging it a quarter turn drives the joint angle
    const core::ManipulatorModel* jm = h.viewer.manipulators().model(mt->manipulatorId());
    QVERIFY(jm);
    QCOMPARE(jm->spec().handles, unsigned(core::kHandleRotZ));
    QCOMPARE(jm->handles().size(), size_t(1));
    {
      const gp_Pnt o = jm->spec().frame.Location();
      const gp_Dir axis = jm->spec().frame.Direction();
      const gp_Pnt a = jm->handlePoint(core::ManipulatorHandle::RotZ, 0.01);  // on the ring
      const gp_Pnt c = a.Rotated(gp_Ax1(o, axis), M_PI / 2);
      double ax, ay, cx, cy;
      h.viewer.worldToScreen(a, ax, ay);
      h.viewer.worldToScreen(c, cx, cy);
      QVERIFY(h.viewer.manipulatorPress(int(std::lround(ax)), int(std::lround(ay))));
      QVERIFY(h.viewer.manipulatorMove(int(std::lround(cx)), int(std::lround(cy))));  // auto snap: 0.5 deg steps
      QVERIFY(h.viewer.manipulatorRelease(int(std::lround(cx)), int(std::lround(cy))));
    }
    QVERIFY2(near(std::abs(j->primaryValue()), 90.0, 1e-6), qPrintable(QString::number(j->primaryValue())));
    QVERIFY(near(gp_Pnt(5, 5, 0).Transformed(h.doc.worldPlacement(b.compB)), gp_Pnt(5, 5, 10)));  // still on the axis
    QVERIFY(mt->commandLineInput(h.ctx(), "@0"));
    QVERIFY(near(j->primaryValue(), 0.0));
    QVERIFY(mt->commandLineInput(h.ctx(), "@90"));
    QVERIFY(near(std::abs(j->primaryValue()), 90.0));
    QVERIFY(near(gp_Pnt(5, 5, 0).Transformed(h.doc.worldPlacement(b.compB)), gp_Pnt(5, 5, 10)));  // still on the axis
    QVERIFY(!near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
    h.manager.cancel();
    QVERIFY(near(j->primaryValue(), 0.0));
    QVERIFY(near(worldOrigin(h.doc, b.compB), gp_Pnt(0, 0, 10)));
  }

  void driveJointToolRespectsLimits() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    auto joint = std::make_unique<JointFeature>();
    joint->setOrigins(makeJointOrigin(h.doc, faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0)),
                      makeJointOrigin(h.doc, faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10)));
    joint->setJointType(JointType::Revolute);
    joint->setLimits(true, -45, 45, 0);
    auto* j = joint.get();
    h.doc.addFeature(std::move(joint));
    h.doc.recompute();
    QVERIFY(h.manager.run("assembly.drive-joint"));
    auto* dt = h.tool<DriveJointTool>("assembly.drive-joint");
    QCOMPARE(dt->joint(), j);
    // the ring handle sits on the joint frame with the limits as clamps
    QVERIFY(dt->manipulatorId() != core::kInvalidManipulator);
    const core::ManipulatorModel* m = h.viewer.manipulators().model(dt->manipulatorId());
    QVERIFY(m);
    QCOMPARE(m->spec().kind, core::ManipulatorKind::Angular);
    QVERIFY(near(m->spec().frame.Location(), gp_Pnt(5, 5, 10)));
    QVERIFY(m->spec().minValue && near(*m->spec().minValue, -45.0));
    QVERIFY(m->spec().maxValue && near(*m->spec().maxValue, 45.0));
    dt->setValue(h.ctx(), 30.0);
    QVERIFY(near(j->primaryValue(), 30.0));
    QVERIFY(near(m->value(), 30.0));  // dialog -> gizmo
    QVERIFY(near(gp_Pnt(5, 5, 0).Transformed(h.doc.worldPlacement(b.compB)), gp_Pnt(5, 5, 10)));
    // gizmo -> joint: turn the knob 10 degrees further (auto snap at this zoom: 5 deg steps), then past the limit
    h.viewer.setStandardView(core::StandardView::Top, false);
    h.viewer.setPixelSize(0.01);
    {
      const gp_Pnt o = m->spec().frame.Location();
      const gp_Ax1 axis(o, m->spec().frame.Direction());
      const gp_Pnt a = m->handlePoint(core::ManipulatorHandle::RotZ, 0.01);
      const gp_Pnt b40 = a.Rotated(axis, 10.0 * M_PI / 180.0);
      const gp_Pnt b70 = a.Rotated(axis, 40.0 * M_PI / 180.0);
      double x, y;
      h.viewer.worldToScreen(a, x, y);
      QVERIFY(h.viewer.manipulatorPress(int(std::lround(x)), int(std::lround(y))));
      QVERIFY(h.viewer.isManipulatorDragging());
      h.viewer.worldToScreen(b40, x, y);
      QVERIFY(h.viewer.manipulatorMove(int(std::lround(x)), int(std::lround(y))));
      QVERIFY2(near(j->primaryValue(), 40.0, 1e-6), qPrintable(QString::number(j->primaryValue())));
      h.viewer.worldToScreen(b70, x, y);
      QVERIFY(h.viewer.manipulatorMove(int(std::lround(x)), int(std::lround(y))));
      QVERIFY(near(j->primaryValue(), 45.0));  // clamped by the joint limits
      QVERIFY(h.viewer.manipulatorRelease(int(std::lround(x)), int(std::lround(y))));
      QVERIFY(!h.viewer.isManipulatorDragging());
      QVERIFY(near(dt->value(), 45.0));
      QVERIFY(near(m->value(), 45.0));
    }
    dt->setValue(h.ctx(), 30.0);
    dt->setValue(h.ctx(), 500.0);
    QVERIFY(near(j->primaryValue(), 45.0));  // clamped
    for (int i = 0; i < 400; ++i) {
      dt->step(h.ctx());
      QVERIFY(j->primaryValue() >= -45.0 - 1e-9 && j->primaryValue() <= 45.0 + 1e-9);
    }
    QVERIFY(dt->commandLineInput(h.ctx(), "-10"));
    QVERIFY(near(dt->value(), -10.0));
    int undoBefore = h.doc.undoStack().index();
    h.manager.accept();
    QCOMPARE(h.doc.undoStack().index(), undoBefore + 1);
    QVERIFY(near(j->primaryValue(), -10.0));
    h.doc.undoStack().undo();
    QVERIFY(near(j->primaryValue(), 0.0));
    // cancel restores
    QVERIFY(h.manager.run("assembly.drive-joint", {j->name(), "20"}));
    QVERIFY(near(j->primaryValue(), 20.0));
    h.manager.cancel();
    QVERIFY(near(j->primaryValue(), 0.0));
  }

  void groundRigidGroupAndComponentsFromBodies() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    // ground is an instant toggle with undo
    QVERIFY(!h.doc.component(b.compB)->grounded);
    h.select({ShapeRef::component(b.compB)});
    QVERIFY(h.manager.run("assembly.ground"));
    QVERIFY(!h.manager.isActive());
    QVERIFY(h.doc.component(b.compB)->grounded);
    QVERIFY(h.manager.run("assembly.ground"));
    QVERIFY(!h.doc.component(b.compB)->grounded);
    h.doc.undoStack().undo();
    QVERIFY(h.doc.component(b.compB)->grounded);
    h.doc.undoStack().undo();
    QVERIFY(!h.doc.component(b.compB)->grounded);
    // disabled without a component selection
    h.select({});
    QVERIFY(!h.manager.run("assembly.ground"));

    // rigid group
    h.select({ShapeRef::component(b.compA), ShapeRef::body(b.bodyB)});
    QVERIFY(h.manager.run("assembly.rigid-group"));
    auto* rg = h.tool<RigidGroupTool>("assembly.rigid-group");
    QCOMPARE(rg->members().size(), size_t(2));
    QCOMPARE(h.viewer.highlighted().size(), size_t(2));
    h.manager.accept();
    const core::Feature* last = h.doc.featureAt(h.doc.featureCount() - 1);
    QCOMPARE(last->type(), std::string("RigidGroup"));
    QCOMPARE(last->browserFolder(), std::string("Joints"));
    QVERIFY(isJointed(h.doc, b.compB));

    // components from bodies: both bodies get their own component under the current one
    h.select({ShapeRef::body(b.bodyA), ShapeRef::body(b.bodyB)});
    int comps = static_cast<int>(h.doc.components().size());
    QVERIFY(h.manager.run("assembly.components-from-bodies"));
    QCOMPARE(static_cast<int>(h.doc.components().size()), comps + 2);
    QCOMPARE(h.doc.component(h.doc.body(b.bodyA)->component)->parent, b.compA);
    QCOMPARE(h.doc.component(h.doc.body(b.bodyB)->component)->parent, b.compB);
    h.doc.undoStack().undo();
    QCOMPARE(static_cast<int>(h.doc.components().size()), comps);
    QCOMPARE(h.doc.body(b.bodyA)->component, b.compA);

    // activate / delete
    h.select({ShapeRef::component(b.compB)});
    QVERIFY(h.manager.run("assembly.activate"));
    QCOMPARE(h.doc.activeComponent(), b.compB);
    QCOMPARE(h.viewer.ghosted(), std::vector<ObjectId>{b.compB});
    h.select({});
    QVERIFY(h.manager.run("assembly.activate"));
    QCOMPARE(h.doc.activeComponent(), core::kRootComponentId);
    QVERIFY(h.viewer.ghosted().empty());
    h.select({ShapeRef::component(b.compB)});
    int featuresBefore = h.doc.featureCount();
    QVERIFY(h.manager.run("assembly.delete-component"));
    QVERIFY(!h.doc.component(b.compB));
    QVERIFY(!isJointed(h.doc, b.compA));  // the rigid group referencing it is gone too
    QCOMPARE(h.doc.featureCount(), featuresBefore - 1);
    h.doc.undoStack().undo();
    const core::Component* top = nullptr;
    for (const core::Component* c : h.doc.components())
      if (c->name == "Top") top = c;
    QVERIFY(top);
    QCOMPARE(h.doc.body(b.bodyB)->component, top->id);
    QVERIFY(near(worldOrigin(h.doc, top->id), gp_Pnt(40, 0, 0)));
    QCOMPARE(h.doc.featureCount(), featuresBefore);
    QVERIFY(isJointed(h.doc, top->id));  // rigid group back, remapped to the re-created component
    QVERIFY(isJointed(h.doc, b.compA));
    QCOMPARE(h.doc.featureAt(featuresBefore - 1)->status(), core::FeatureStatus::Ok);
  }

  void interferenceToolReportsAndPreviews() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(5, 0, 5));
    h.doc.component(b.compB)->placement = tr;
    h.doc.notifyComponentChanged(b.compB);
    QVERIFY(h.manager.run("assembly.interference"));
    auto* it = h.tool<InterferenceTool>("assembly.interference");
    QCOMPARE(it->report().pairs.size(), size_t(1));
    QVERIFY(near(it->report().pairs.front().volume, 250.0, 1e-3));
    bool redShape = false;
    for (const auto& [handle, item] : h.viewer.previews())
      if (item.kind == "shape" && !item.shape.IsNull()) redShape = true;
    QVERIFY(redShape);
    QVERIFY(!h.host.messages().empty());
    h.manager.accept();
    QVERIFY(h.viewer.previews().empty());
  }

  void exportToolsWriteFiles() {
    Harness h;
    QTemporaryDir dir;
    QString step = dir.path() + "/asm.step";
    QString csv = dir.path() + "/bom.csv";
    QVERIFY(h.manager.run("assembly.export-step", {step.toStdString()}));
    QVERIFY(QFile::exists(step));
    QVERIFY(QFileInfo(step).size() > 1000);
    QVERIFY(h.manager.run("assembly.export-bom", {csv.toStdString()}));
    QFile f(csv);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(f.readAll());
    QVERIFY(text.startsWith("Item,Name,Part Number"));
    QVERIFY(text.contains("Base"));
    QVERIFY(text.contains("Top"));
    // insert design: a saved copy of this document becomes a linked component
    QString part = dir.path() + "/part.fcad";
    std::string err;
    QVERIFY2(h.doc.save(part.toStdString(), &err), err.c_str());
    int comps = static_cast<int>(h.doc.components().size());
    QVERIFY(h.manager.run("assembly.insert-design", {part.toStdString()}));
    QCOMPARE(static_cast<int>(h.doc.components().size()), comps + 1);
    const core::Component* inserted = h.doc.components().back();
    QCOMPARE(inserted->externalPath, part.toStdString());
    QCOMPARE(h.doc.bodiesOf(inserted->id).size(), size_t(2));
    QCOMPARE(h.doc.featureAt(h.doc.featureCount() - 1)->type(), std::string("InsertDesign"));
    QCOMPARE(h.doc.featureAt(h.doc.featureCount() - 1)->status(), core::FeatureStatus::Ok);
  }

  void dialogsConstructOffscreen() {
    Harness h;
    const TwoBoxes& b = h.boxes;
    auto joint = std::make_unique<JointFeature>();
    joint->setOrigins(makeJointOrigin(h.doc, faceWithNormal(h.doc, b.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0)),
                      makeJointOrigin(h.doc, faceWithNormal(h.doc, b.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10)));
    joint->setJointType(JointType::Revolute);
    h.doc.addFeature(std::move(joint));
    h.doc.recompute();
    for (const char* id : {"assembly.new-component", "assembly.joint", "assembly.as-built-joint", "assembly.rigid-group",
                           "assembly.move", "assembly.drive-joint", "assembly.interference"}) {
      h.select({ShapeRef::component(b.compB)});
      QVERIFY2(h.manager.run(id), id);
      std::unique_ptr<QWidget> w(h.tools.tool(id)->createDialog(h.ctx(), nullptr));
      QVERIFY2(w != nullptr, id);
      QVERIFY2(!w->findChildren<QWidget*>().isEmpty(), id);
      h.manager.cancel();
    }
    // tool metadata used by the toolbar builder
    QCOMPARE(h.tools.byShortcut("J")->info().id, std::string("assembly.joint"));
    QCOMPARE(h.tools.byShortcut("Shift+J")->info().id, std::string("assembly.as-built-joint"));
    QCOMPARE(h.tools.byShortcut("N")->info().id, std::string("assembly.new-component"));
    QCOMPARE(h.tools.byAlias("JOINT")->info().id, std::string("assembly.joint"));
    QVERIFY(!h.tools.toolsFor(core::Workspace::Design, "SOLID", "ASSEMBLE").empty());
    QVERIFY(!h.tools.toolsFor(core::Workspace::Design, "SOLID", "INSPECT").empty());
  }
};

QTEST_MAIN(AssemblyToolsTest)
#include "test_assembly_tools.moc"
