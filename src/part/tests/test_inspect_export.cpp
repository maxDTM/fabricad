// Tests for the rm/feat round: Section Analysis (live clipping plane with a manipulator), STL
// quality presets, STEP without pcurves, cross-component notes on Join/Cut/Hole, the Offset
// Plane world direction and the Hole's preselected start face (COMPARISON.md recommendations 3,
// 6, 7, 8).
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
#include "part/PartModule.h"
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/features/Hole.h"
#include "part/features/Import.h"
#include "part/tools/ExportStlTool.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/SectionTool.h"
#include "support/TestSketch.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

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
  core::ObjectId box(double x0, double y0, double x1, double y1, double h, const char* plane = "XY") {
    TestSketch* sk = addRectSketch(doc, plane, x0, y0, x1, y1);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(h);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = doc.addFeature(std::move(ex));
    doc.recompute();
    return f->createdBodies().empty() ? core::kInvalidId : f->createdBodies().front();
  }
  bool hasMessage(const std::string& part) const {
    for (const auto& [lvl, m] : host.messages())
      if (m.find(part) != std::string::npos) return true;
    return false;
  }
};

core::ShapeRef faceWithNormal(const core::Document& doc, core::ObjectId body, const gp_Dir& n) {
  const TopoDS_Shape& shape = doc.bodyShape(body);
  for (const TopoDS_Shape& f : core::subShapes(shape, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && pl->Direction().IsEqual(n, 1e-6)) return core::ShapeRef::subShape(body, shape, f);
  }
  return {};
}
}  // namespace

class InspectExportTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

  // ---- Section Analysis ------------------------------------------------------------------------
  void sectionPlaneMath() {
    const gp_Ax3 xz = core::originPlane("XZ");
    gp_Pln p = sectionPlaneFor(xz, 5.0, false);
    QVERIFY(p.Axis().Direction().IsEqual(xz.Direction(), 1e-12));
    QVERIFY(near(gp_Vec(gp::Origin(), p.Location()).Dot(gp_Vec(xz.Direction())), 5.0));
    gp_Pln f = sectionPlaneFor(xz, 5.0, true);
    QVERIFY(f.Axis().Direction().IsOpposite(xz.Direction(), 1e-12));
    QVERIFY(f.Distance(p.Location()) < 1e-9);  // same place, other side kept
  }

  void sectionToolLifecycle() {
    Rig rig;
    rig.box(0, 0, 10, 10, 10);
    QVERIFY(rig.registry.has("inspect.section"));
    QVERIFY(rig.registry.tool("inspect.section")->info().promoted);
    QCOMPARE(rig.registry.tool("inspect.section")->info().group, std::string("INSPECT"));
    QVERIFY(!rig.registry.tool("inspect.section-off")->isEnabled(rig.manager.makeContext()));  // nothing to remove yet

    // a plane typed on the command line, then an offset: the viewer clips at z = 4
    QVERIFY(rig.manager.run("inspect.section"));
    QVERIFY(rig.manager.isActive());
    QVERIFY(!rig.viewer.sectionPlane());
    QVERIFY(rig.manager.runCommandLine("XY"));
    QVERIFY(rig.viewer.sectionPlane());
    QVERIFY(rig.manager.runCommandLine("4"));
    QVERIFY(near(rig.viewer.sectionPlane()->Location().Z(), 4.0));
    QVERIFY(rig.viewer.sectionPlane()->Axis().Direction().IsEqual(gp::DZ(), 1e-12));
    // the arrow manipulator sits on the base plane with the offset as its value
    QCOMPARE(rig.viewer.manipulators().count(), size_t(1));
    auto* tool = dynamic_cast<SectionTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    QVERIFY(near(tool->offset(), 4.0));
    // dragging the arrow moves the cut (the gizmo drives the plane live); Front view, 0.1 mm/px
    rig.viewer.setPixelSize(0.1);
    rig.viewer.setStandardView(core::StandardView::Front, false);
    auto pixel = [&](const gp_Pnt& p) {
      double x, y;
      rig.viewer.worldToScreen(p, x, y);
      return QPoint(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    };
    const QPoint from = pixel(gp_Pnt(0, 0, 4 + 3)), to = pixel(gp_Pnt(0, 0, 7 + 3));
    QVERIFY(rig.viewer.manipulatorPress(from.x(), from.y(), core::ModCtrl));
    rig.viewer.manipulatorMove(to.x(), to.y(), core::ModCtrl);
    QVERIFY(near(rig.viewer.sectionPlane()->Location().Z(), 7.0, 1e-2));  // live while dragging
    rig.viewer.manipulatorRelease(to.x(), to.y(), core::ModCtrl);
    QVERIFY(near(tool->offset(), 7.0, 1e-2));
    QVERIFY(rig.manager.runCommandLine("7"));  // exact value for the checks below
    QVERIFY(rig.manager.runCommandLine("FLIP"));
    QVERIFY(rig.viewer.sectionPlane()->Axis().Direction().IsEqual(-gp::DZ(), 1e-12));
    // OK keeps the section after the tool ends
    rig.manager.accept();
    QVERIFY(!rig.manager.isActive());
    QVERIFY(rig.viewer.sectionPlane());
    QCOMPARE(rig.viewer.manipulators().count(), size_t(0));
    QVERIFY(rig.registry.tool("inspect.section-off")->isEnabled(rig.manager.makeContext()));

    // a second run resumes it; Esc restores what was there (offset 7, flipped)
    QVERIFY(rig.manager.run("inspect.section"));
    tool = dynamic_cast<SectionTool*>(rig.manager.activeTool());
    QVERIFY(tool && tool->hasPlane());
    QVERIFY(near(tool->offset(), 7.0));
    QVERIFY(tool->flipped());
    QVERIFY(rig.manager.runCommandLine("2"));
    QVERIFY(near(rig.viewer.sectionPlane()->Location().Z(), 2.0));
    rig.manager.cancel();
    QVERIFY(near(rig.viewer.sectionPlane()->Location().Z(), 7.0));

    // SECTION OFF / Remove Section clear it
    QVERIFY(rig.manager.runCommandLine("SECTION OFF"));
    QVERIFY(!rig.viewer.sectionPlane());
    QVERIFY(!rig.manager.isActive());
    QVERIFY(rig.manager.runCommandLine("SECTION XZ 3"));
    QVERIFY(rig.viewer.sectionPlane());
    rig.manager.accept();
    QVERIFY(rig.manager.run("inspect.section-off"));
    QVERIFY(!rig.viewer.sectionPlane());
  }

  void sectionOnAFace() {
    // a picked body face: the cut starts on it and moves along its normal
    Rig rig;
    core::ObjectId b = rig.box(0, 0, 10, 10, 10);
    rig.doc.selection().set(faceWithNormal(rig.doc, b, gp::DX()));
    QVERIFY(rig.manager.run("inspect.section"));
    QVERIFY(rig.viewer.sectionPlane());
    QVERIFY(near(std::abs(rig.viewer.sectionPlane()->Location().X()), 10.0));
    QVERIFY(rig.manager.runCommandLine("-3"));
    // the face plane may come with either normal: the cut is 3 mm from x = 10 along it
    const gp_Pln p = *rig.viewer.sectionPlane();
    QVERIFY(p.Distance(gp_Pnt(7, 5, 5)) < 1e-6 || p.Distance(gp_Pnt(13, 5, 5)) < 1e-6);
    rig.manager.accept();
    QVERIFY(rig.viewer.sectionPlane());
  }

  // ---- STL presets (recommendation 3) ------------------------------------------------------------
  void stlQualityPresets() {
    StlExportSettings d;
    QVERIFY(near(d.linearDeflection, 0.05));
    QVERIFY(near(d.angularDeflectionDeg, 0.2 * 180.0 / M_PI));  // 0.2 rad, not 20 deg
    QCOMPARE(stlQualityOf(d), StlQuality::Medium);
    StlExportSettings s;
    QVERIFY(ExportStlTool::applyOption("quality=fine", s));
    QCOMPARE(stlQualityOf(s), StlQuality::Fine);
    QVERIFY(near(s.linearDeflection, 0.01));
    QVERIFY(ExportStlTool::applyOption("coarse", s));
    QCOMPARE(stlQualityOf(s), StlQuality::Coarse);
    QVERIFY(ExportStlTool::applyOption("preset=medium", s));
    QCOMPARE(stlQualityOf(s), StlQuality::Medium);
    QVERIFY(ExportStlTool::applyOption("angle=3", s));  // explicit words override a preset
    QCOMPARE(stlQualityOf(s), StlQuality::Custom);
    QCOMPARE(std::string(stlQualityName(StlQuality::Fine)), std::string("fine"));
    QVERIFY(!parseStlQuality("ultra"));

    // the presets give increasingly fine meshes of a cylinder; Medium equals the default export
    Rig rig;
    auto cyl = std::make_unique<ExtrudeFeature>();
    TestSketch* sk = addRectSketch(rig.doc, "XY", 0, 0, 2, 2);  // square post with a fillet-free body
    cyl->prop("profiles").addRef(sk->rectProfile(0));
    cyl->prop("distance").setDouble(5);
    cyl->prop("operation").setEnum("New Body");
    rig.doc.addFeature(std::move(cyl));
    rig.doc.recompute();
    auto hole = std::make_unique<HoleFeature>();
    core::ObjectId body = rig.doc.liveBodies().front();
    core::ShapeRef top = faceWithNormal(rig.doc, body, gp::DZ());
    top.point = core::Vec3{1, 1, 5};
    top.hasPoint = true;
    hole->prop("points").addRef(top);
    hole->prop("diameter").setDouble(1.2);
    hole->prop("depth").setDouble(3);
    rig.doc.addFeature(std::move(hole));
    rig.doc.recompute();
    QTemporaryDir dir;
    auto size = [&](const char* name) { return QFileInfo(dir.path() + "/" + name).size(); };
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/c.stl").toStdString(), "quality=coarse"}));
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/m.stl").toStdString(), "quality=medium"}));
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/f.stl").toStdString(), "quality=fine"}));
    QVERIFY(rig.manager.run("part.export-stl", {(dir.path() + "/d.stl").toStdString()}));
    QVERIFY2(size("c.stl") < size("m.stl"), qPrintable(QString("%1 %2").arg(size("c.stl")).arg(size("m.stl"))));
    QVERIFY2(size("m.stl") < size("f.stl"), qPrintable(QString("%1 %2").arg(size("m.stl")).arg(size("f.stl"))));
    QCOMPARE(size("d.stl"), size("m.stl"));
    QVERIFY(rig.hasMessage("STL mesh (fine)"));
  }

  // a damaged STL (NaN / infinite / huge coordinates) is a clean error, not an endless OCCT loop
  void stlWithInvalidCoordinates() {
    QTemporaryDir dir;
    auto write = [&](const char* name, const char* vertex) {
      const QString path = dir.path() + "/" + name;
      QFile f(path);
      f.open(QIODevice::WriteOnly);
      f.write(QByteArray("solid t\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex ") +
              vertex + "\n  endloop\n endfacet\nendsolid t\n");
      return path.toStdString();
    };
    ImportFeature::Imported out;
    std::string err;
    QVERIFY(ImportFeature::readFile(write("ok.stl", "0 1 0"), out, &err));
    QCOMPARE(out.shapes.size(), size_t(1));
    QVERIFY(!ImportFeature::readFile(write("nan.stl", "nan 1 0"), out, &err));
    QVERIFY2(err.find("invalid vertex") != std::string::npos || err.find("could not read") != std::string::npos, err.c_str());
    QVERIFY(!ImportFeature::readFile(write("huge.stl", "0 1e30 0"), out, &err));
    QVERIFY(!ImportFeature::readFile(write("junk.stl", "x y z"), out, &err));
  }

  // ---- STEP without pcurves (recommendation 8) ---------------------------------------------------
  void stepWithoutPcurves() {
    bool pc = true;
    QVERIFY(applyStepOption("pcurves=off", pc));
    QVERIFY(!pc);
    QVERIFY(applyStepOption("pcurves=on", pc));
    QVERIFY(pc);
    QVERIFY(applyStepOption("nopcurves", pc));
    QVERIFY(!pc);
    QVERIFY(!applyStepOption("out.step", pc));
    QVERIFY(stepPcurvesDefault());  // the preference defaults to on

    Rig rig;
    rig.box(0, 0, 10, 20, 5);
    QTemporaryDir dir;
    const std::string with = (dir.path() + "/with.step").toStdString(), without = (dir.path() + "/without.step").toStdString(),
                      pref = (dir.path() + "/pref.step").toStdString();
    QVERIFY(rig.manager.run("part.export-step", {with}));
    QVERIFY(rig.manager.run("part.export-step", {without, "pcurves=off"}));
    auto read = [](const std::string& p) {
      QFile f(QString::fromStdString(p));
      return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    const QByteArray a = read(with), b = read(without);
    QVERIFY(a.contains("PCURVE"));
    QVERIFY(!b.contains("PCURVE"));
    QVERIFY2(b.size() < a.size(), qPrintable(QString("%1 vs %2").arg(b.size()).arg(a.size())));
    // the preference applies when no word is typed; the file still reads back as the same solid
    setStepPcurvesDefault(false);
    QVERIFY(rig.manager.run("part.export-step", {pref}));
    setStepPcurvesDefault(true);
    QVERIFY(!read(pref).contains("PCURVE"));
    core::Document doc2;
    auto imp = std::make_unique<ImportFeature>();
    imp->prop("filePath").setString(without);
    core::Feature* f = doc2.addFeature(std::move(imp));
    doc2.recompute();
    QCOMPARE(f->status(), core::FeatureStatus::Ok);
    QVERIFY(near(core::volume(doc2.bodyShape(f->createdBodies().front())), 1000, 1e-6));
  }

  // a damaged STEP file whose LINE vectors refer to missing DIRECTION entities: OCCT's
  // StepToGeom::MakeVectorWithMagnitude dereferenced a null handle and the app died with SIGSEGV
  // (found by fuzzing). Now it is a clean error and the reader keeps working.
  void damagedStepIsACleanError() {
    Rig rig;
    rig.box(0, 0, 10, 20, 5);
    QTemporaryDir dir;
    const std::string good = (dir.path() + "/good.step").toStdString();
    QVERIFY(rig.manager.run("part.export-step", {good, "pcurves=off"}));
    QFile f(QString::fromStdString(good));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    QSet<QString> directions;  // VECTOR('',#dir,len): drop every #dir entity
    static const QRegularExpression rx("VECTOR\\('[^']*',(#\\d+),");
    for (auto it = rx.globalMatch(text); it.hasNext();) directions.insert(it.next().captured(1));
    QVERIFY(!directions.isEmpty());
    QStringList kept;
    for (const QString& line : text.split('\n')) {
      const QString id = line.section(' ', 0, 0);
      if (!directions.contains(id)) kept << line;
    }
    const std::string bad = (dir.path() + "/bad.step").toStdString();
    QFile g(QString::fromStdString(bad));
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write(kept.join('\n').toUtf8());
    g.close();
    ImportFeature::Imported out;
    std::string err;
    QVERIFY(!ImportFeature::readFile(bad, out, &err));
    QVERIFY(!err.empty());
    (void)ImportFeature::readStepAssembly(bad, &err);
    // the fuzzer's file (a DIRECTION entity cut in half): SIGSEGV in StepToGeom under the XCAF
    // reader (STEP import with structure) before the fix
    const QString data = qEnvironmentVariable("FABRICAD_TEST_DATA");
    if (!data.isEmpty()) {
      const std::string crashed = (data + "/damaged-missing-direction.step").toStdString();
      // with OCCT's signal handlers installed its own OCC_CATCH_SIGNALS blocks skip the broken
      // face, so the result is a partial import or a clean error; what matters is that it returns
      (void)ImportFeature::readFile(crashed, out, &err);
      (void)ImportFeature::readStepAssembly(crashed, &err);
    }
    QVERIFY(ImportFeature::readFile(good, out, &err));  // the reader still works afterwards
    QCOMPARE(out.shapes.size(), size_t(1));
  }

  // ---- cross-component notes (recommendation 6) --------------------------------------------------
  void crossComponentNotes() {
    Rig rig;
    core::ObjectId compA = rig.doc.addComponent("A").id;
    rig.doc.setActiveComponent(compA);
    core::ObjectId a = rig.box(0, 0, 10, 10, 10);
    core::ObjectId compB = rig.doc.addComponent("B").id;
    rig.doc.setActiveComponent(compB);
    core::ObjectId b = rig.box(20, 0, 30, 10, 10);
    QVERIFY(a != core::kInvalidId && b != core::kInvalidId);
    rig.doc.body(a)->name = "Plate";
    // a cut in B whose tool passes through both boxes: B is cut, A is left alone and the status
    // line says so
    TestSketch* sk = addRectSketch(rig.doc, "XY", 5, 2, 25, 8);
    rig.doc.selection().set(sk->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool);
    tool->feature()->prop("operation").setEnum("Cut");
    QVERIFY(rig.manager.runCommandLine("10"));
    rig.manager.accept();
    QVERIFY(!rig.manager.isActive());
    QVERIFY(near(core::volume(rig.doc.bodyShape(a)), 1000));
    QVERIFY(core::volume(rig.doc.bodyShape(b)) < 1000 - 1);
    QVERIFY(rig.hasMessage("touches body 'Plate' of component 'A' but left it alone"));
    // an explicit target in the other component: the cut reaches A and says so
    TestSketch* sk2 = addRectSketch(rig.doc, "XY", 1, 1, 3, 3);
    rig.doc.selection().set(sk2->rectProfile(0));
    QVERIFY(rig.manager.run("part.extrude"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    tool->feature()->prop("operation").setEnum("Cut");
    tool->feature()->prop("targets").addRef(core::ShapeRef::body(a));
    QVERIFY(rig.manager.runCommandLine("10"));
    rig.manager.accept();
    QVERIFY(core::volume(rig.doc.bodyShape(a)) < 1000 - 1);
    QVERIFY(rig.hasMessage("cuts body 'Plate' of component 'A' (another component)"));
    // a single component says nothing
    Rig one;
    one.box(0, 0, 10, 10, 10);
    TestSketch* s1 = addRectSketch(one.doc, "XY", 2, 2, 4, 4);
    one.doc.selection().set(s1->rectProfile(0));
    QVERIFY(one.manager.run("part.extrude"));
    QVERIFY(one.manager.runCommandLine("5"));
    one.manager.accept();
    QVERIFY(!one.hasMessage("another component"));
  }

  // ---- conventions (recommendation 7) --------------------------------------------------------------
  void offsetPlaneWorldDirection() {
    core::Document doc;
    auto make = [&](int dir, double d) {
      auto f = std::make_unique<OffsetPlaneFeature>();
      f->prop("plane").setRef(core::ShapeRef::originPlane("XZ"));
      f->prop("distance").setDouble(d);
      f->prop("direction").setEnum(dir);
      core::Feature* raw = doc.addFeature(std::move(f));
      doc.recompute();
      return raw;
    };
    QCOMPARE(OffsetPlaneFeature::offsetPlaneDirections().size(), size_t(7));
    // XZ's own normal is -Y: "Plane normal" puts +175 at y = -175
    core::Feature* n = make(0, 175);
    auto pn = core::resolvePlane(doc, core::ShapeRef::feature(n->id(), core::ShapeKind::ConstructionPlane));
    QVERIFY(pn);
    QVERIFY(near(pn->Location().Y(), -175));
    // +Y: the offset and the new plane's normal follow world +Y
    core::Feature* y = make(3, 175);
    QCOMPARE(y->status(), core::FeatureStatus::Ok);
    auto py = core::resolvePlane(doc, core::ShapeRef::feature(y->id(), core::ShapeKind::ConstructionPlane));
    QVERIFY(py);
    QVERIFY(near(py->Location().Y(), 175));
    QVERIFY(py->Direction().IsEqual(gp::DY(), 1e-9));
    // a world axis the plane is not perpendicular to is an error
    core::Feature* x = make(1, 10);
    QCOMPARE(x->status(), core::FeatureStatus::Error);
    QVERIFY(QString::fromStdString(x->statusMessage()).contains("not perpendicular"));
  }

  void throughAllCounterboreLeavesMaterialAboveAlone() {
    // DRW-VIEWS-ANNOT-13: a through-all counterbore next to a boss must not notch the boss: the
    // plate with the boss loses exactly what the plate alone loses
    auto run = [](bool withBoss) {
      Rig rig;
      core::ObjectId plate = rig.box(0, 0, 40, 40, 10);
      if (withBoss) {
        TestSketch* bs = addRectSketch(rig.doc, "XY", 20, 0, 30, 10);
        auto boss = std::make_unique<ExtrudeFeature>();
        boss->prop("profiles").addRef(bs->rectProfile(0));
        boss->prop("startOffset").setDouble(10);
        boss->prop("distance").setDouble(10);
        boss->prop("operation").setEnum("Join");
        rig.doc.addFeature(std::move(boss));
        rig.doc.recompute();
      }
      const double before = core::volume(rig.doc.bodyShape(plate));
      core::ShapeRef top;
      for (const TopoDS_Shape& f : core::subShapes(rig.doc.bodyShape(plate), TopAbs_FACE)) {
        auto pl = core::facePlane(f);
        core::Vec3 mn, mx;
        if (pl && pl->Direction().IsParallel(gp::DZ(), 1e-9) && std::abs(pl->Location().Z() - 10) < 1e-9 && core::bbox(f, mn, mx) &&
            mx.x - mn.x > 30)
          top = core::ShapeRef::subShape(plate, rig.doc.bodyShape(plate), f);
      }
      top.point = core::Vec3{15, 5, 10};  // counterbore radius 5.5 reaches x = 20.5, 0.5 mm under the boss
      top.hasPoint = true;
      auto ho = std::make_unique<HoleFeature>();
      ho->prop("points").addRef(top);
      ho->prop("holeType").setEnum("Counterbore");
      ho->prop("cboreDiameter").setDouble(11);
      ho->prop("cboreDepth").setDouble(6);
      ho->prop("extentType").setEnum("All");
      core::Feature* hf = rig.doc.addFeature(std::move(ho));
      rig.doc.recompute();
      return std::pair<double, bool>{before - core::volume(rig.doc.bodyShape(plate)), hf->status() == core::FeatureStatus::Ok};
    };
    const auto [alone, okAlone] = run(false);
    const auto [boss, okBoss] = run(true);
    QVERIFY(okAlone && okBoss);
    QVERIFY(alone > 600);
    QVERIFY2(near(boss, alone, 1e-9), qPrintable(QString("%1 vs %2").arg(boss).arg(alone)));
  }

  void editFeatureByName() {
    // be/drawing observation: EDITFEATURE <name> works without selecting the feature first
    Rig rig;
    rig.box(0, 0, 10, 10, 10);
    QVERIFY(rig.manager.runCommandLine("EDITFEATURE Extrude1"));
    QCOMPARE(rig.manager.activeToolId(), std::string("part.extrude"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool && tool->isEditing());
    rig.manager.cancel();
    QVERIFY(rig.manager.runCommandLine("EDITFEATURE feature:extrude1"));
    QCOMPARE(rig.manager.activeToolId(), std::string("part.extrude"));
    rig.manager.cancel();
    rig.manager.runCommandLine("EDITFEATURE Nope");
    QVERIFY(!rig.manager.isActive());
    QVERIFY(rig.hasMessage("no feature 'Nope' to edit"));
  }

  void holePreselectedFaceIsTheStartFace() {
    Rig rig;
    core::ObjectId b = rig.box(0, 0, 20, 20, 10);
    core::ShapeRef top = faceWithNormal(rig.doc, b, gp::DZ());
    rig.doc.selection().set(top);
    QVERIFY(rig.manager.run("part.hole"));
    auto* tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool && tool->feature());
    QVERIFY(tool->feature()->prop("face").ref().isValid());
    QCOMPARE(tool->feature()->prop("face").ref().owner, b);
    // a selected face carries its pick point: that is also the first placement (Fusion: the
    // click on the face places the hole); without a point only the start face is set
    QCOMPARE(tool->feature()->prop("points").refs().size(), size_t(1));
    rig.manager.cancel();
    HoleFeature probe;
    core::ShapeRef bare = core::ShapeRef::face(b, 1);
    bare.hasPoint = false;
    QCOMPARE(probe.preselectionInputs(bare), (std::vector<std::string>{"face"}));
    bare.hasPoint = true;
    QCOMPARE(probe.preselectionInputs(bare), (std::vector<std::string>{"face", "points"}));
    QVERIFY(probe.preselectionInputs(core::ShapeRef::body(b)).empty());
    // with a pick point the face is both the start face and the first placement
    top.point = core::Vec3{5, 5, 10};
    top.hasPoint = true;
    rig.doc.selection().set(top);
    QVERIFY(rig.manager.run("part.hole"));
    tool = dynamic_cast<FeatureTool*>(rig.manager.activeTool());
    QVERIFY(tool->feature()->prop("face").ref().isValid());
    QCOMPARE(tool->feature()->prop("points").refs().size(), size_t(1));
    QVERIFY(tool->isValid());
    rig.manager.accept();
    QVERIFY(core::volume(rig.doc.bodyShape(b)) < 4000 - 1);
  }
};

QTEST_MAIN(InspectExportTest)
#include "test_inspect_export.moc"
