// Document-level tests: joint features, snap frames, rigid groups, BOM, interference,
// STEP export, external designs, component helpers. Headless (QTEST_GUILESS_MAIN).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblyModule.h"
#include "assembly/Bom.h"
#include "assembly/ComponentOps.h"
#include "assembly/Interference.h"
#include "assembly/JointFeature.h"
#include "assembly/JointFrame.h"
#include "assembly/StepExport.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"
#include "support/AssemblyTestHelpers.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cmath>

using namespace fabricad;
using namespace fabricad::assembly;
using namespace fabricad::assembly::test;
using core::Document;
using core::ObjectId;
using core::ShapeRef;


class AssemblyFeaturesTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    core::test::registerTestFeatures();
    core::ToolRegistry tools;
    core::ModuleContext ctx{core::FeatureFactory::instance(), tools};
    assembly::registerModule(ctx);
    QVERIFY(core::FeatureFactory::instance().has("Joint"));
    QVERIFY(core::FeatureFactory::instance().has("RigidGroup"));
    QVERIFY(core::FeatureFactory::instance().has("NewComponent"));
    QVERIFY(core::FeatureFactory::instance().has("InsertDesign"));
    QVERIFY(tools.has("assembly.joint"));
    QVERIFY(tools.has("assembly.new-component"));
    QVERIFY(tools.has("assembly.ground"));
    QVERIFY(tools.has("assembly.move"));
    QVERIFY(tools.has("assembly.interference"));
    QVERIFY(tools.has("assembly.export-step"));
    QVERIFY(tools.has("assembly.drive-joint"));
    QVERIFY(tools.has("assembly.as-built-joint"));
    QVERIFY(tools.has("assembly.rigid-group"));
    QCOMPARE(tools.byShortcut("J")->info().id, std::string("assembly.joint"));
  }

  void snapCandidatesOnFace() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    ShapeRef top = faceWithNormal(doc, t.bodyA, gp_Dir(0, 0, 1));
    QVERIFY(top.isValid());
    auto cands = snapCandidates(doc, top);
    QVERIFY(cands.size() >= 9);  // centre + 4 midpoints + 4 corners
    auto best = nearestCandidate(cands, gp_Pnt(5, 5, 10));
    QVERIFY(best);
    QCOMPARE(best->mode, SnapMode::FaceCenter);
    QVERIFY(near(best->frame.Location(), gp_Pnt(5, 5, 10)));
    QVERIFY(best->frame.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9));
    auto corner = nearestCandidate(cands, gp_Pnt(0.2, 0.1, 10));
    QVERIFY(corner->mode == SnapMode::EdgeStart || corner->mode == SnapMode::EdgeEnd);
    QVERIFY(near(corner->frame.Location(), gp_Pnt(0, 0, 10)));
    auto mid = nearestCandidate(cands, gp_Pnt(5, 0.3, 10));
    QCOMPARE(mid->mode, SnapMode::EdgeMidpoint);
    QVERIFY(near(mid->frame.Location(), gp_Pnt(5, 0, 10)));
    // joint origin from a world pick on component B (placed at x=40): local coordinates
    ShapeRef topB = faceWithNormal(doc, t.bodyB, gp_Dir(0, 0, 1));
    JointOrigin jo = makeJointOrigin(doc, topB, gp_Pnt(45, 5, 10));
    QVERIFY(jo.valid);
    QCOMPARE(jo.component, t.compB);
    QCOMPARE(jo.mode, SnapMode::FaceCenter);
    QVERIFY(near(jo.frame.Location(), gp_Pnt(5, 5, 10)));
    QVERIFY(near(jo.toWorld(doc).Location(), gp_Pnt(45, 5, 10)));
    // JSON round trip
    JointOrigin back = JointOrigin::fromJson(jo.toJson());
    QCOMPARE(back.component, jo.component);
    QCOMPARE(back.mode, jo.mode);
    QVERIFY(near(back.frame.Location(), jo.frame.Location()));
    QVERIFY(back.ref == jo.ref);
  }

  void snapCandidatesOnCylinderAndEdge() {
    Document doc;
    doc.addComponent("C");
    doc.setActiveComponent(doc.components().back()->id);
    auto box = std::make_unique<core::test::BoxFeature>();
    core::Feature* fb = doc.addFeature(std::move(box));
    doc.recompute();
    ObjectId body = fb->createdBodies().front();
    auto hole = std::make_unique<core::test::HoleFeature>();
    hole->prop("body").setRef(ShapeRef::body(body));
    hole->prop("cx").setDouble(5);
    hole->prop("cy").setDouble(10);
    hole->prop("radius").setDouble(2);
    doc.addFeature(std::move(hole));
    doc.recompute();
    // cylindrical face
    const TopoDS_Shape& shape = doc.bodyShape(body);
    ShapeRef cyl;
    for (const TopoDS_Shape& f : core::subShapes(shape, TopAbs_FACE))
      if (!core::facePlane(f)) cyl = ShapeRef::subShape(body, shape, f);
    QVERIFY(cyl.isValid());
    auto cands = snapCandidates(doc, cyl);
    bool hasMiddle = false, hasEnd = false;
    for (const auto& c : cands) {
      if (c.mode == SnapMode::AxisMiddle) {
        hasMiddle = true;
        QVERIFY(near(c.frame.Location(), gp_Pnt(5, 10, 15)));
        QVERIFY(std::abs(std::abs(c.frame.Direction().Z()) - 1.0) < 1e-9);
      }
      if (c.mode == SnapMode::AxisStart || c.mode == SnapMode::AxisEnd) hasEnd = true;
      if (c.mode == SnapMode::CircleCenter) QVERIFY(std::abs(c.frame.Location().X() - 5) < 1e-6);
    }
    QVERIFY(hasMiddle);
    QVERIFY(hasEnd);
    // a straight edge: midpoint with Z along the edge
    ShapeRef edge;
    for (const TopoDS_Shape& e : core::subShapes(shape, TopAbs_EDGE)) {
      ShapeRef r = ShapeRef::subShape(body, shape, e);
      if (r.signature.geomType == 0 && std::abs(r.signature.direction.z) > 0.9) {
        edge = r;
        break;
      }
    }
    QVERIFY(edge.isValid());
    auto ec = snapCandidates(doc, edge);
    QCOMPARE(ec.size(), size_t(3));
    QCOMPARE(ec.front().mode, SnapMode::EdgeMidpoint);
    QVERIFY(std::abs(std::abs(ec.front().frame.Direction().Z()) - 1.0) < 1e-9);
  }

  void rigidJointMatesFacesAndRoundTrips() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(40, 0, 0)));
    auto joint = std::make_unique<JointFeature>();
    JointOrigin a = makeJointOrigin(doc, faceWithNormal(doc, t.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0));
    JointOrigin b = makeJointOrigin(doc, faceWithNormal(doc, t.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10));
    QVERIFY(a.valid && b.valid);
    joint->setOrigins(a, b);
    joint->setJointType(JointType::Rigid);
    QCOMPARE(joint->componentA(), t.compB);
    QCOMPARE(joint->componentB(), t.compA);
    core::Feature* jf = doc.addFeature(std::move(joint));
    QCOMPARE(jf->browserFolder(), std::string("Joints"));
    doc.recompute();
    QCOMPARE(jf->status(), core::FeatureStatus::Ok);
    QVERIFY2(near(worldOrigin(doc, t.compB), gp_Pnt(0, 0, 10)), qPrintable(QString::number(worldOrigin(doc, t.compB).X())));
    QVERIFY(!jf->outputShape().IsNull());  // glyph
    QVERIFY(isJointed(doc, t.compB));
    QVERIFY(!isJointed(doc, core::kRootComponentId));
    // dependencies: the joint depends on the features that made the bodies
    auto deps = doc.dependenciesOf(jf->id());
    QCOMPARE(deps.size(), size_t(2));

    // offset + flip edits move the component
    auto* j = dynamic_cast<JointFeature*>(jf);
    j->setOffset(0, 0, 2, 0);
    doc.notifyFeatureChanged(j->id());
    doc.recompute();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(0, 0, 12)));

    // JSON round trip into a fresh document
    QJsonObject json = doc.toJson();
    Document doc2;
    std::string err;
    QVERIFY2(doc2.fromJson(json, &err), err.c_str());
    const core::Feature* jf2 = doc2.feature(jf->id());
    QVERIFY(jf2);
    QCOMPARE(jf2->type(), std::string("Joint"));
    QVERIFY(dynamic_cast<const JointFeature*>(jf2));
    QCOMPARE(jf2->status(), core::FeatureStatus::Ok);
    QVERIFY(near(worldOrigin(doc2, t.compB), gp_Pnt(0, 0, 12)));

    // free move of a jointed component snaps back on recompute
    gp_Trsf away;
    away.SetTranslation(gp_Vec(100, 0, 0));
    doc.component(t.compB)->placement = away;
    doc.notifyComponentChanged(t.compB);
    doc.markDirty(jf->id());
    doc.recompute();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(0, 0, 12)));
  }

  void revoluteDriveAndLimits() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    auto joint = std::make_unique<JointFeature>();
    JointOrigin a = makeJointOrigin(doc, faceWithNormal(doc, t.bodyB, gp_Dir(0, 0, -1)), gp_Pnt(45, 5, 0));
    JointOrigin b = makeJointOrigin(doc, faceWithNormal(doc, t.bodyA, gp_Dir(0, 0, 1)), gp_Pnt(5, 5, 10));
    joint->setOrigins(a, b);
    joint->setJointType(JointType::Revolute);
    joint->setPrimaryValue(90.0);
    auto* j = joint.get();
    doc.addFeature(std::move(joint));
    doc.recompute();
    QCOMPARE(j->status(), core::FeatureStatus::Ok);
    // B's corner (0,0,0) rotates about the face centre (5,5): with Z opposed the sense is mirrored
    gp_Pnt corner = worldOrigin(doc, t.compB);
    QVERIFY(std::abs(corner.Z() - 10) < 1e-6);
    QVERIFY(near(gp_Pnt(5, 5, 0).Transformed(doc.worldPlacement(t.compB)), gp_Pnt(5, 5, 10)));
    QVERIFY(std::abs(corner.Distance(gp_Pnt(5, 5, 10)) - std::sqrt(50.0)) < 1e-6);
    QVERIFY(near(corner, gp_Pnt(0, 10, 10)) || near(corner, gp_Pnt(10, 0, 10)));
    gp_Pnt at90 = corner;
    // drive to 0: corner back at (0,0,10)
    j->setPrimaryValue(0.0);
    doc.notifyFeatureChanged(j->id());
    doc.recompute();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(0, 0, 10)));
    // limits clamp the value
    j->setLimits(true, -45, 45, 0);
    j->setPrimaryValue(90.0);
    QCOMPARE(j->primaryValue(), 45.0);
    doc.notifyFeatureChanged(j->id());
    doc.recompute();
    QVERIFY(!near(worldOrigin(doc, t.compB), at90));
    // the solver reports the realised value
    AssemblyInput in = gatherAssembly(doc);
    QCOMPARE(in.joints.size(), size_t(1));
    SolverResult r = AssemblySolver::solve(in.components, in.joints);
    QVERIFY(std::abs(std::abs(r.jointValues.at(j->id())[0]) - 45.0) < 1e-6);
    // slider type switch: distance property drives
    j->setJointType(JointType::Slider);
    j->setLimits(false, 0, 0, 0);
    j->setPrimaryValue(7.0);
    doc.notifyFeatureChanged(j->id());
    doc.recompute();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(0, 0, 17)));
    QCOMPARE(j->iconName(), std::string("joint-slider"));
  }

  void asBuiltJointKeepsPositions() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    auto joint = std::make_unique<JointFeature>();
    joint->setComponents(t.compB, t.compA);
    joint->setJointType(JointType::Revolute);
    joint->setAsBuilt(true);
    SolverJoint sj;
    sj.type = JointType::Revolute;
    AssemblySolver::asBuiltFrames(sj, doc.worldPlacement(t.compB), doc.worldPlacement(t.compA),
                                  gp_Ax3(gp_Pnt(25, 5, 5), gp_Dir(0, 0, 1)));
    JointOrigin a, b;
    a.component = t.compB;
    a.frame = sj.frameA;
    a.valid = true;
    b.component = t.compA;
    b.frame = sj.frameB;
    b.valid = true;
    joint->setOrigins(a, b);
    auto* j = joint.get();
    doc.addFeature(std::move(joint));
    doc.recompute();
    QCOMPARE(j->status(), core::FeatureStatus::Ok);
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(40, 0, 0)));
    j->setPrimaryValue(180.0);
    doc.notifyFeatureChanged(j->id());
    doc.recompute();
    // rotated 180 deg about the axis through (25,5): x 40 -> 10
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(10, 10, 0)));
  }

  void rigidGroupFollowsAnchor() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    doc.component(t.compA)->grounded = false;
    auto group = std::make_unique<RigidGroupFeature>();
    group->setMembers(doc, {t.compA, t.compB});
    auto* g = group.get();
    doc.addFeature(std::move(group));
    doc.recompute();
    QCOMPARE(g->status(), core::FeatureStatus::Ok);
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(40, 0, 0)));
    // move the anchor: the member follows
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(0, 0, 5));
    doc.component(t.compA)->placement = tr;
    doc.notifyComponentChanged(t.compA);
    doc.markDirty(g->id());
    doc.recompute();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(40, 0, 5)));
    // round trip keeps the captured relative placements
    Document doc2;
    QVERIFY(doc2.fromJson(doc.toJson()));
    QVERIFY(near(worldOrigin(doc2, t.compB), gp_Pnt(40, 0, 5)));
    QCOMPARE(dynamic_cast<RigidGroupFeature*>(doc2.feature(g->id()))->members().size(), size_t(2));
  }

  void bomGroupsOccurrences() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    doc.component(t.compB)->partNumber = "T-100";
    doc.component(t.compB)->material = "Steel";
    doc.body(t.bodyA)->material = "ABS";
    doc.body(t.bodyA)->density = 1.0;
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(0, 40, 0));
    ObjectId occ = createOccurrence(doc, nullptr, t.compB, core::kRootComponentId, tr);
    QVERIFY(occ != core::kInvalidId);
    QCOMPARE(definitionOf(doc, occ), t.compB);
    QCOMPARE(displayedBodies(doc, occ).size(), size_t(1));
    QCOMPARE(componentLabel(doc, occ), std::string("Top:2"));
    BomTable bom = buildBom(doc);
    QCOMPARE(bom.rows.size(), size_t(2));
    QCOMPARE(bom.totalQuantity, 3);
    const BomRow* base = nullptr;
    const BomRow* top = nullptr;
    for (const auto& r : bom.rows) (r.name == "Base" ? base : top) = &r;
    QVERIFY(base && top);
    QCOMPARE(base->quantity, 1);
    QCOMPARE(base->material, std::string("ABS"));
    QVERIFY(std::abs(base->mass - 1.0) < 1e-9);  // 1000 mm^3 * 1 g/cm^3 = 1 g
    QCOMPARE(top->quantity, 2);
    QCOMPARE(top->partNumber, std::string("T-100"));
    QCOMPARE(top->material, std::string("Steel"));
    QCOMPARE(top->occurrences.size(), size_t(2));
    QVERIFY(std::abs(top->volume - 1000.0) < 1e-6);
    std::string csv = bomToCsv(bom);
    QVERIFY(csv.find("Item,Name,Part Number") == 0);
    QVERIFY(csv.find("T-100") != std::string::npos);
    // world shape of the occurrence is at the occurrence placement
    TopoDS_Shape ws = componentWorldShape(doc, occ);
    QVERIFY(!ws.IsNull());
    core::Vec3 c = core::centroid(ws);
    QVERIFY(std::abs(c.y - 45.0) < 1e-6);
  }

  void interferenceFindsOverlap() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    InterferenceReport none = checkInterference(doc);
    QVERIFY(none.pairs.empty());
    QCOMPARE(none.pairsSkipped, 1);
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(5, 0, 5));
    doc.component(t.compB)->placement = tr;
    InterferenceReport rep = checkInterference(doc, {t.compA, t.compB});
    QCOMPARE(rep.pairs.size(), size_t(1));
    QVERIFY2(std::abs(rep.pairs[0].volume - 250.0) < 1e-3, qPrintable(QString::number(rep.pairs[0].volume)));
    QVERIFY(!rep.pairs[0].shape.IsNull());
    QCOMPARE(rep.pairsTested, 1);
    // touching faces are not an interference
    gp_Trsf touch;
    touch.SetTranslation(gp_Vec(10, 0, 0));
    doc.component(t.compB)->placement = touch;
    QVERIFY(checkInterference(doc).pairs.empty());
  }

  void stepExportRoundTrip() {
    Document doc;
    doc.setName("Asm");
    TwoBoxes t = makeTwoBoxes(doc);
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(0, 40, 0));
    createOccurrence(doc, nullptr, t.compB, core::kRootComponentId, tr);
    QTemporaryDir dir;
    std::string path = (dir.path() + "/asm.step").toStdString();
    std::string err;
    StepExportStats stats;
    QVERIFY2(exportAssemblyStep(doc, path, {}, &err, &stats), err.c_str());
    QCOMPARE(stats.parts, 2);
    QCOMPARE(stats.instances, 3);
    QVERIFY(QFileInfo(QString::fromStdString(path)).size() > 1000);

    Handle(TDocStd_Document) xdoc;
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", xdoc);
    STEPCAFControl_Reader reader;
    QCOMPARE(reader.ReadFile(path.c_str()), IFSelect_RetDone);
    QVERIFY(reader.Transfer(xdoc));
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(xdoc->Main());
    TDF_LabelSequence free;
    st->GetFreeShapes(free);
    QCOMPARE(free.Length(), 1);
    TopoDS_Shape all = st->GetShape(free.First());
    int solids = 0;
    for (TopExp_Explorer ex(all, TopAbs_SOLID); ex.More(); ex.Next()) ++solids;
    QCOMPARE(solids, 3);
    QVERIFY(std::abs(core::volume(all) - 3000.0) < 1e-3);
    // names survived
    TDF_LabelSequence labels;
    st->GetShapes(labels);
    bool foundTop = false, foundBase = false;
    for (int i = 1; i <= labels.Length(); ++i) {
      Handle(TDataStd_Name) n;
      if (labels.Value(i).FindAttribute(TDataStd_Name::GetID(), n)) {
        TCollection_AsciiString s(n->Get());
        if (s == "Top") foundTop = true;
        if (s == "Base") foundBase = true;
      }
    }
    QVERIFY(foundTop);
    QVERIFY(foundBase);
  }

  void insertExternalDesign() {
    QTemporaryDir dir;
    std::string path = (dir.path() + "/part.fcad").toStdString();
    {
      Document part;
      part.setName("Bolt");
      auto box = std::make_unique<core::test::BoxFeature>();
      box->prop("dx").setDouble(2);
      box->prop("dy").setDouble(3);
      box->prop("dz").setDouble(4);
      core::Feature* f = part.addFeature(std::move(box));
      part.recompute();
      part.body(f->createdBodies().front())->material = "Brass";
      std::string err;
      QVERIFY2(part.save(path, &err), err.c_str());
    }
    Document doc;
    NewComponentSpec spec;
    spec.name = "Bolt";
    spec.externalPath = path;
    spec.activate = false;
    ObjectId comp = createComponent(doc, &doc.undoStack(), spec);
    QVERIFY(comp != core::kInvalidId);
    QCOMPARE(doc.featureCount(), 1);  // NewComponent marker
    auto ins = std::make_unique<InsertDesignFeature>();
    ins->setComponent(comp);
    ins->setPath(path);
    core::Feature* fi = doc.addFeature(std::move(ins));
    doc.recompute();
    QCOMPARE(fi->status(), core::FeatureStatus::Ok);
    QCOMPARE(doc.bodiesOf(comp).size(), size_t(1));
    ObjectId body = doc.bodiesOf(comp).front();
    QVERIFY(std::abs(core::volume(doc.bodyShape(body)) - 24.0) < 1e-6);
    QCOMPARE(doc.body(body)->material, std::string("Brass"));
    // recompute keeps the same body id (slot matching) and uses the cache
    doc.markAllDirty();
    doc.recompute();
    QCOMPARE(doc.bodiesOf(comp).front(), body);
    // BOM row for the external component
    BomTable bom = buildBom(doc);
    QCOMPARE(bom.rows.size(), size_t(1));
    QCOMPARE(bom.rows[0].name, std::string("Bolt"));
    QCOMPARE(bom.rows[0].material, std::string("Brass"));
    // undo the component creation (snapshot command) removes it and its marker
    doc.undoStack().undo();  // AddFeature is not on the stack; this undoes the component
    QVERIFY(doc.component(comp) == nullptr);
    // missing file -> error status
    Document doc3;
    auto ins3 = std::make_unique<InsertDesignFeature>();
    ins3->setComponent(core::kRootComponentId);
    ins3->setPath("/nonexistent/x.fcad");
    core::Feature* f3 = doc3.addFeature(std::move(ins3));
    doc3.recompute();
    QCOMPARE(f3->status(), core::FeatureStatus::Error);
  }

  void componentHelpersAndCommands() {
    Document doc;
    TwoBoxes t = makeTwoBoxes(doc);
    QCOMPARE(componentOfRef(doc, ShapeRef::body(t.bodyB)), t.compB);
    QCOMPARE(componentOfRef(doc, ShapeRef::component(t.compA)), t.compA);
    QCOMPARE(componentOfRef(doc, ShapeRef::originPlane("XY")), core::kRootComponentId);
    auto centre = componentCenter(doc, t.compB);
    QVERIFY(centre && near(*centre, gp_Pnt(45, 5, 5)));
    QCOMPARE(leafComponents(doc).size(), size_t(2));
    // ground command with undo
    doc.undoStack().push(makeGroundCommand(doc, t.compB, true));
    QVERIFY(doc.component(t.compB)->grounded);
    doc.undoStack().undo();
    QVERIFY(!doc.component(t.compB)->grounded);
    // placements command
    auto before = snapshotPlacements(doc);
    gp_Trsf w;
    w.SetTranslation(gp_Vec(1, 2, 3));
    QVERIFY(setWorldPlacement(doc, t.compB, w));
    QVERIFY(!setWorldPlacement(doc, t.compB, w));  // unchanged
    auto after = snapshotPlacements(doc);
    doc.undoStack().push(makePlacementsCommand(doc, before, after));
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(1, 2, 3)));
    doc.undoStack().undo();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(40, 0, 0)));
    doc.undoStack().redo();
    QVERIFY(near(worldOrigin(doc, t.compB), gp_Pnt(1, 2, 3)));
    // nested placement: child world = parent * local
    ObjectId child = doc.addComponent("Child", t.compB).id;
    gp_Trsf local;
    local.SetTranslation(gp_Vec(10, 0, 0));
    doc.component(child)->placement = local;
    QVERIFY(near(worldOrigin(doc, child), gp_Pnt(11, 2, 3)));
    gp_Trsf target;
    target.SetTranslation(gp_Vec(0, 0, 0));
    setWorldPlacement(doc, child, target);
    QVERIFY(near(worldOrigin(doc, child), gp_Pnt(0, 0, 0)));
    QVERIFY(isAncestorOf(doc, t.compB, child));
    QVERIFY(!isAncestorOf(doc, child, t.compB));
    // create components from bodies + delete with undo
    NewComponentSpec spec;
    spec.name = "FromBody";
    spec.bodies = {t.bodyA};
    spec.parent = core::kRootComponentId;
    int featuresBefore = doc.featureCount();
    ObjectId nc = createComponent(doc, &doc.undoStack(), spec);
    QCOMPARE(doc.body(t.bodyA)->component, nc);
    QCOMPARE(doc.activeComponent(), nc);
    QCOMPARE(doc.featureCount(), featuresBefore + 1);
    QVERIFY(deleteComponent(doc, &doc.undoStack(), nc));
    QVERIFY(!doc.component(nc));
    QCOMPARE(doc.body(t.bodyA)->component, core::kRootComponentId);  // bodies move to the parent
    doc.undoStack().undo();  // re-created (fresh id, see integration notes), body back inside
    const core::Component* restored = nullptr;
    for (const core::Component* c : doc.components())
      if (c->name == "FromBody") restored = c;
    QVERIFY(restored);
    QCOMPARE(doc.body(t.bodyA)->component, restored->id);
    QCOMPARE(doc.featureCount(), featuresBefore + 1);  // the marker came back
    doc.undoStack().redo();
    QVERIFY(!doc.component(restored->id));
    QCOMPARE(doc.body(t.bodyA)->component, core::kRootComponentId);
  }
};

QTEST_GUILESS_MAIN(AssemblyFeaturesTest)
#include "test_assembly_features.moc"
