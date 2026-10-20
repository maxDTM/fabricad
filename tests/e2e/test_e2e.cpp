// FabriCAD end-to-end test (headless). Drives the whole product through the same APIs the
// UI uses: the module registration functions, the ToolRegistry/ToolManager with tool ids,
// mouse events on the sketch plane, command-line input, selection-driven dialogs
// (FeatureTool::setActiveInput = the dialog's selection buttons), the drawing canvas, and
// the export tools. Output files go to $FABRICAD_E2E_OUT (default: ./e2e-out).
//
// Flow: component "Base" -> sketch on XY (fully constrained rectangle + circle) -> extrude 20
// -> sketch on the top face -> cut-extrude a hole -> fillet -> chamfer -> circular pattern ->
// save .fcad -> reload -> identical volume -> edit a sketch dimension -> second component
// "Lever" with its own part -> ground + revolute joint -> drive the joint -> drawing with
// front/top/right/iso + section + dimensions + parts list -> export STEP/STL/PDF/SVG/DXF/CSV.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblyModule.h"
#include "assembly/JointFeature.h"
#include "assembly/tools/JointTool.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingModule.h"
#include "drawing/DrawingWorkspaceWidget.h"
#include "drawing/Projection.h"
#include "drawing/Sheet.h"
#include "part/PartModule.h"
#include "part/features/Import.h"
#include "part/tools/FeatureTool.h"
#include "sketch/Sketch.h"
#include "sketch/SketchFeature.h"
#include "sketch/SketchModule.h"
#include "sketch/SketchSession.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtTest/QtTest>

#include <cmath>
#include <format>
#include <map>
#include <numbers>

using namespace fabricad;
using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

namespace {

constexpr double kPi = std::numbers::pi;

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

QString outDir() {
  QString dir = qEnvironmentVariable("FABRICAD_E2E_OUT");
  if (dir.isEmpty()) dir = QDir::current().filePath("e2e-out");
  QDir().mkpath(dir);
  return dir;
}
std::string outPath(const char* name) { return QDir(outDir()).filePath(name).toStdString(); }

/// Everything the MainWindow owns, minus widgets: registry with all four modules, document,
/// headless viewer/host, tool manager.
struct Rig {
  core::ToolRegistry registry;
  core::Document doc;
  core::NullViewerContext viewer;
  core::NullAppHost host;
  core::ToolManager mgr{registry};

  Rig() {
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    sketch::registerModule(ctx);
    part::registerModule(ctx);
    assembly::registerModule(ctx);
    drawing::registerModule(ctx);
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);
    viewer.setPixelSize(0.25);  // 8 px snap tolerance = 2 mm
  }
  ~Rig() {
    if (mgr.isActive()) mgr.cancel();
    sketch::SketchSession::end();
  }

  static void pump() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  // --- sketch-plane input -------------------------------------------------------------
  sketch::SketchSession* session() { return sketch::SketchSession::current(); }
  sketch::SketchFeature* sketchFeature() { return session() ? session()->feature() : nullptr; }
  core::ViewerMouseEvent at(double x, double y, core::MouseButton b = core::MouseButton::None, int mods = 0) {
    core::ViewerMouseEvent e;
    e.button = b;
    e.modifiers = mods;
    e.hasPlanePoint = true;
    e.planePoint = session()->toWorld({x, y});
    e.x = static_cast<int>(x * 4);
    e.y = static_cast<int>(-y * 4);
    return e;
  }
  void click(double x, double y, int mods = 0) {
    mgr.mouseMove(at(x, y, core::MouseButton::None, mods));
    mgr.mousePress(at(x, y, core::MouseButton::Left, mods));
    mgr.mouseRelease(at(x, y, core::MouseButton::Left, mods));
  }
  void key(int k) {
    core::ViewerKeyEvent e;
    e.key = k;
    mgr.keyPress(e);
  }

  // --- part helpers -------------------------------------------------------------------
  part::FeatureTool* featureTool() { return dynamic_cast<part::FeatureTool*>(mgr.activeTool()); }
  core::ToolContext& ctx() { return *mgr.activeContext(); }
  double volume(ObjectId body) const { return core::volume(doc.bodyShape(body)); }
  const core::Feature* lastFeature() const { return doc.featureAt(doc.featureCount() - 1); }
};

ShapeRef faceWithNormal(const core::Document& doc, ObjectId body, const gp_Dir& n, double zAt = NAN) {
  const TopoDS_Shape& shape = doc.bodyShape(body);
  for (const TopoDS_Shape& f : core::subShapes(shape, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (!pl || !pl->Direction().IsEqual(n, 1e-6)) continue;
    if (!std::isnan(zAt) && !near(core::computeSignature(f).center.z, zAt, 1e-6)) continue;
    return ShapeRef::subShape(body, shape, f);
  }
  return {};
}

/// Cylindrical face whose axis passes through (x, y) (world, component-local here).
ShapeRef cylinderFaceAt(const core::Document& doc, ObjectId body, double x, double y, double radius) {
  const TopoDS_Shape& shape = doc.bodyShape(body);
  for (const TopoDS_Shape& f : core::subShapes(shape, TopAbs_FACE)) {
    BRepAdaptor_Surface s(TopoDS::Face(f));
    if (s.GetType() != GeomAbs_Cylinder) continue;
    gp_Cylinder c = s.Cylinder();
    if (!near(c.Radius(), radius, 1e-6)) continue;
    const gp_Pnt loc = c.Location();
    if (near(loc.X(), x, 1e-6) && near(loc.Y(), y, 1e-6)) return ShapeRef::subShape(body, shape, f);
  }
  return {};
}

/// Straight edges parallel to `dir` with both ends at the given z (NAN = any), optional length.
std::vector<ShapeRef> linearEdges(const core::Document& doc, ObjectId body, const gp_Dir& dir, double z0 = NAN,
                                  double z1 = NAN) {
  std::vector<ShapeRef> out;
  const TopoDS_Shape& shape = doc.bodyShape(body);
  TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
  TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
  for (const TopoDS_Shape& e : core::subShapes(shape, TopAbs_EDGE)) {
    BRepAdaptor_Curve c(TopoDS::Edge(e));
    if (c.GetType() != GeomAbs_Line) continue;
    // skip seam edges of cylindrical faces: only edges between two planar faces are real corners
    bool planarOnly = true;
    if (edgeFaces.Contains(e))
      for (const TopoDS_Shape& f : edgeFaces.FindFromKey(e))
        if (!core::facePlane(f)) planarOnly = false;
    if (!planarOnly) continue;
    if (!c.Line().Direction().IsParallel(dir, 1e-6)) continue;
    const gp_Pnt a = c.Value(c.FirstParameter()), b = c.Value(c.LastParameter());
    if (!std::isnan(z0) && !(near(a.Z(), z0, 1e-6) && near(b.Z(), z0, 1e-6))) continue;
    if (!std::isnan(z1) && !((near(a.Z(), 0, 1e-6) && near(b.Z(), z1, 1e-6)) || (near(b.Z(), 0, 1e-6) && near(a.Z(), z1, 1e-6))))
      continue;
    out.push_back(ShapeRef::subShape(body, shape, e));
  }
  return out;
}

/// Minimal DXF group-code reader: pairs of lines (code, value). Returns entity-type counts.
bool parseDxf(const QString& path, std::map<std::string, int>& entities, std::string* why) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *why = "cannot open";
    return false;
  }
  const QList<QByteArray> lines = f.readAll().split('\n');
  QList<QByteArray> clean;
  for (QByteArray l : lines) clean.push_back(l.trimmed());
  while (!clean.isEmpty() && clean.last().isEmpty()) clean.removeLast();
  if (clean.size() % 2 != 0) {
    *why = "odd number of lines";
    return false;
  }
  bool inEntities = false, sawEof = false;
  std::string section;
  for (int i = 0; i + 1 < clean.size(); i += 2) {
    bool ok = false;
    const int code = clean[i].toInt(&ok);
    if (!ok) {
      *why = std::format("line {}: group code '{}' is not an integer", i + 1, clean[i].toStdString());
      return false;
    }
    const std::string value = clean[i + 1].toStdString();
    if (code == 0) {
      if (value == "SECTION") section.clear();
      else if (value == "ENDSEC") inEntities = false;
      else if (value == "EOF") sawEof = true;
      else if (inEntities) ++entities[value];
    } else if (code == 2 && section.empty()) {
      section = value;
      inEntities = (value == "ENTITIES");
    }
  }
  if (!sawEof) {
    *why = "no EOF";
    return false;
  }
  return true;
}

}  // namespace

class E2ETest : public QObject {
  Q_OBJECT
 private slots:
  void fullWorkflow() {
    Rig r;
    core::Document& doc = r.doc;
    core::ToolManager& mgr = r.mgr;
    QVERIFY2(r.registry.tools().size() > 100, "all module tools registered");

    // ------------------------------------------------------------------ component Base
    QVERIFY(mgr.run("assembly.new-component", {"Base"}));
    mgr.accept();
    ObjectId baseComp = core::kInvalidId;
    for (const core::Component* c : std::as_const(doc).components())
      if (c->name == "Base") baseComp = c->id;
    QVERIFY(baseComp != core::kInvalidId);
    QCOMPARE(doc.activeComponent(), baseComp);

    // ------------------------------------------------------------- sketch 1 on XY plane
    QVERIFY(mgr.run("sketch.create", {"XY"}));
    Rig::pump();
    QVERIFY(r.session());
    sketch::SketchFeature* sk1 = r.sketchFeature();
    QVERIFY(sk1);
    const ObjectId sketch1Id = sk1->id();
    QCOMPARE(sk1->component(), baseComp);

    // rectangle 80 x 60 from the origin (2-point rectangle, typed size). The typed width and
    // height become driving dimensions d1 = 80 and d2 = 60 (Fusion dynamic input).
    QVERIFY(mgr.run("sketch.rectangle"));
    r.click(0, 0);
    QVERIFY(mgr.runCommandLine("80,60"));
    QCOMPARE(sk1->sketch().geometryCount(), 4);
    QVERIFY(sk1->sketch().dimensionByName("d1") && !sk1->sketch().dimensionByName("d1")->driven);
    QVERIFY(sk1->sketch().dimensionByName("d2") && !sk1->sketch().dimensionByName("d2")->driven);
    QVERIFY(near(sk1->sketch().dimensionByName("d1")->value, 80.0, 1e-9));
    QVERIFY(near(sk1->sketch().dimensionByName("d2")->value, 60.0, 1e-9));
    // circle d12 at the centre (typed diameter -> dimension d3); Ctrl suppresses the
    // midpoint-alignment inference so the centre is positioned by explicit dimensions below
    QVERIFY(mgr.run("sketch.circle"));
    r.click(40, 30, core::ModCtrl);
    QVERIFY(mgr.runCommandLine("12"));
    QCOMPARE(sk1->sketch().geometryCount(), 5);
    QVERIFY(sk1->sketch().dimensionByName("d3") && !sk1->sketch().dimensionByName("d3")->driven);
    QVERIFY(near(sk1->sketch().dimensionByName("d3")->value, 12.0, 1e-9));
    // fix the lower-left corner: only the circle centre (2 DOF) is left free
    QVERIFY(mgr.run("sketch.constraint.fix"));
    r.click(0, 0);
    r.key(Qt::Key_Escape);
    QVERIFY2(sk1->sketch().dofs() == 2, qPrintable(QString("dofs after fix: %1").arg(sk1->sketch().dofs())));
    // dimensions: circle centre position (horizontal + vertical)
    QVERIFY(mgr.run("sketch.dimension"));
    r.click(40, 30);    // circle centre ...
    r.click(60, 0);     // ... to the bottom line: point-line distance (vertical position)
    r.click(60, 15);    // label
    QVERIFY(sk1->sketch().dimensionByName("d4"));
    QVERIFY(!sk1->sketch().dimensionByName("d4")->driven);
    QVERIFY(mgr.runCommandLine("30"));
    r.click(40, 30);    // circle centre ...
    r.click(0, 15);     // ... to the left line: horizontal position
    r.click(20, 45);    // label
    QVERIFY(sk1->sketch().dimensionByName("d5"));
    QVERIFY(!sk1->sketch().dimensionByName("d5")->driven);
    QVERIFY(mgr.runCommandLine("40"));
    r.key(Qt::Key_Escape);
    Rig::pump();
    QCOMPARE(sk1->sketch().dofs(), 0);
    QCOMPARE(sk1->statusText(), std::string("Fully constrained"));
    QVERIFY(mgr.run("sketch.finish"));
    QVERIFY(!r.session());
    QCOMPARE(sk1->status(), core::FeatureStatus::Ok);
    QCOMPARE(sk1->profiles().size(), size_t(2));  // ring + disk

    // -------------------------------------------------------------------- extrude 20 mm
    // the ring profile (rectangle minus circle) -> block with a d12 through hole
    std::vector<core::ProfileInfo> profiles = sk1->profiles();
    std::sort(profiles.begin(), profiles.end(), [](const auto& a, const auto& b) { return a.area > b.area; });
    QVERIFY(near(profiles[0].area, 80 * 60 - kPi * 36, 1e-3));
    doc.selection().set(profiles[0].ref);
    QVERIFY(mgr.run("part.extrude"));
    QVERIFY(r.featureTool());
    QVERIFY(mgr.runCommandLine("20"));
    QVERIFY(mgr.runCommandLine("operation=New Body"));
    mgr.accept();
    QVERIFY(!mgr.isActive());
    const core::Feature* extrude = r.lastFeature();
    QCOMPARE(extrude->type(), std::string("Extrude"));
    QCOMPARE(extrude->status(), core::FeatureStatus::Ok);
    QCOMPARE(extrude->createdBodies().size(), size_t(1));
    const ObjectId body = extrude->createdBodies()[0];
    QCOMPARE(doc.body(body)->component, baseComp);
    const double v0 = 20 * (80 * 60 - kPi * 36);
    QVERIFY2(near(r.volume(body), v0, 1e-3), qPrintable(QString::number(r.volume(body))));

    // ------------------------------------------------- sketch 2 on the top face, hole d6
    ShapeRef topFace = faceWithNormal(doc, body, gp_Dir(0, 0, 1), 20.0);
    QVERIFY(topFace.kind == ShapeKind::Face);
    doc.selection().set(topFace);
    QVERIFY(mgr.run("sketch.create"));
    Rig::pump();
    QVERIFY(r.session());
    sketch::SketchFeature* sk2 = r.sketchFeature();
    QVERIFY(sk2 && sk2 != sk1);
    QVERIFY(near(sk2->plane().Location().Z(), 20.0, 1e-6));
    const sketch::Pt2 c2 = sk2->toLocal(gp_Pnt(60, 30, 20));
    QVERIFY(mgr.run("sketch.circle"));
    r.click(c2.x, c2.y);
    QVERIFY(mgr.runCommandLine("6"));
    QCOMPARE(sk2->sketch().geometryCount(), 1);
    QVERIFY(mgr.run("sketch.finish"));
    QCOMPARE(sk2->profiles().size(), size_t(1));

    // cut-extrude through the block
    doc.selection().set(sk2->profiles()[0].ref);
    QVERIFY(mgr.run("part.extrude"));
    QVERIFY(mgr.runCommandLine("operation=Cut"));
    QVERIFY(mgr.runCommandLine("extentType=All"));
    // the face sketch's normal points out of the body: extrude the other way, into the material
    if (sk2->plane().Direction().Z() > 0) QVERIFY(mgr.runCommandLine("flip=true"));
    QVERIFY2(r.featureTool()->isValid(), r.featureTool()->message().c_str());
    mgr.accept();
    const core::Feature* cut = r.lastFeature();
    QCOMPARE(cut->type(), std::string("Extrude"));
    QCOMPARE(cut->status(), core::FeatureStatus::Ok);
    const ObjectId cutId = cut->id();
    const double v1 = v0 - 20 * kPi * 9;
    QVERIFY2(near(r.volume(body), v1, 1e-3), qPrintable(QString::number(r.volume(body))));

    // ------------------------------------------------------- fillet the 4 vertical edges
    std::vector<ShapeRef> vertical = linearEdges(doc, body, gp_Dir(0, 0, 1), NAN, 20.0);
    QCOMPARE(vertical.size(), size_t(4));
    doc.selection().set(vertical);
    QVERIFY(mgr.run("part.fillet"));
    QVERIFY(mgr.runCommandLine("5"));
    QVERIFY(r.featureTool()->isValid());
    mgr.accept();
    QCOMPARE(r.lastFeature()->type(), std::string("Fillet"));
    QCOMPARE(r.lastFeature()->status(), core::FeatureStatus::Ok);
    const double v2 = v1 - 4 * 20 * (25 - kPi * 25 / 4);
    QVERIFY2(near(r.volume(body), v2, 1e-2), qPrintable(QString::number(r.volume(body))));

    // ------------------------------------------------- chamfer the straight top edges
    std::vector<ShapeRef> topEdges;
    for (const ShapeRef& e : linearEdges(doc, body, gp_Dir(1, 0, 0), 20.0)) topEdges.push_back(e);
    for (const ShapeRef& e : linearEdges(doc, body, gp_Dir(0, 1, 0), 20.0)) topEdges.push_back(e);
    QCOMPARE(topEdges.size(), size_t(4));
    doc.selection().set(topEdges);
    QVERIFY(mgr.run("part.chamfer"));
    QVERIFY(mgr.runCommandLine("1"));
    QVERIFY(r.featureTool()->isValid());
    mgr.accept();
    QCOMPARE(r.lastFeature()->type(), std::string("Chamfer"));
    QCOMPARE(r.lastFeature()->status(), core::FeatureStatus::Ok);
    const double v3 = r.volume(body);
    // 4 straight edges (2 x 70 + 2 x 50 after the fillets) x 0.5 mm^2 = 120 mm^3, plus the chamfer
    // running along the tangent-continuous fillet arcs (4 x 7.85 x 0.5 = 15.7 mm^3)
    QVERIFY2(v3 < v2 - 100 && v3 > v2 - 145, qPrintable(QString::number(v2 - v3)));

    // --------------------------------- circular pattern of the hole around the d12 hole
    doc.selection().set(ShapeRef::feature(cutId));
    QVERIFY(mgr.run("part.circular-pattern"));
    part::FeatureTool* pat = r.featureTool();
    QVERIFY(pat && pat->feature());
    QCOMPARE(pat->feature()->refs("objects").size(), size_t(1));
    pat->setActiveInput(r.ctx(), "axis");  // = the dialog's "Axis" selection button
    ShapeRef cyl = cylinderFaceAt(doc, body, 40, 30, 6);
    QVERIFY(cyl.kind == ShapeKind::Face);
    doc.selection().set(cyl);
    QVERIFY(pat->feature()->ref("axis").kind == ShapeKind::Face);
    pat->feature()->prop("quantity").setInt(4);
    QVERIFY(mgr.runCommandLine("type=Full"));
    QVERIFY2(pat->isValid(), pat->message().c_str());
    mgr.accept();
    QCOMPARE(r.lastFeature()->type(), std::string("CircularPattern"));
    QCOMPARE(r.lastFeature()->status(), core::FeatureStatus::Ok);
    const double v4 = v3 - 3 * 20 * kPi * 9;
    QVERIFY2(near(r.volume(body), v4, 1e-2), qPrintable(QString::number(r.volume(body) - v4)));
    QCOMPARE(doc.featureCount(), 8);  // NewComponent, Sketch, Extrude, Sketch, Extrude, Fillet, Chamfer, Pattern

    // ------------------------------------------------------------ save, reload, compare
    const std::string fcad = outPath("bracket.fcad");
    std::string err;
    QVERIFY(mgr.run("part.export-step", {outPath("bracket.step")}));
    QVERIFY2(doc.save(fcad, &err), err.c_str());
    {
      core::Document copy;
      QVERIFY2(copy.load(fcad, &err), err.c_str());
      QCOMPARE(copy.featureCount(), doc.featureCount());
      for (int i = 0; i < copy.featureCount(); ++i) {
        const core::Feature* f = copy.featureAt(i);
        QVERIFY2(f->status() == core::FeatureStatus::Ok,
                 std::format("{}: {} '{}' (in-session: {} '{}')", f->name(), core::featureStatusName(f->status()),
                             f->statusMessage(), core::featureStatusName(doc.featureAt(i)->status()),
                             doc.featureAt(i)->statusMessage())
                     .c_str());
        QCOMPARE(f->type(), doc.featureAt(i)->type());
      }
      QVERIFY(copy.body(body));
      QCOMPARE(copy.body(body)->component, baseComp);
      QVERIFY2(near(core::volume(copy.bodyShape(body)), r.volume(body), 1e-6 * r.volume(body)),
               qPrintable(QString("%1 vs %2").arg(core::volume(copy.bodyShape(body))).arg(r.volume(body))));
    }

    // --------------------------------------------- change a sketch dimension: 80 -> 100
    QVERIFY(mgr.run("sketch.edit", {std::to_string(sketch1Id)}));
    Rig::pump();
    QVERIFY(r.session());
    const sketch::Constraint* d1 = sk1->sketch().dimensionByName("d1");
    QVERIFY(d1);
    QVERIFY(mgr.run("sketch.dimension-edit", {std::to_string(d1->id), "100"}));
    QVERIFY(near(sk1->sketch().dimensionByName("d1")->value, 100.0, 1e-9));
    QVERIFY(mgr.run("sketch.finish"));
    doc.recompute();
    for (const auto& f : doc.features())
      QVERIFY2(f->status() == core::FeatureStatus::Ok,
               std::format("{}: {} '{}'", f->name(), core::featureStatusName(f->status()), f->statusMessage()).c_str());
    const double v5 = r.volume(body);
    if (!near(v5, v4 + 20 * 60 * 20, 100))
      for (int i = 0; i < doc.featureCount(); ++i) {
        const auto& st = doc.bodyStatesAfter(i);
        auto it = st.find(body);
        qWarning("after %s: volume %g", doc.featureAt(i)->name().c_str(), it == st.end() ? 0.0 : core::volume(it->second));
      }
    if (!near(v5, v4 + 20 * 60 * 20, 100)) {
      const core::Feature* pf = r.lastFeature();
      qWarning("pattern axis %s status %s '%s'", pf->ref("axis").toString().c_str(), core::featureStatusName(pf->status()),
               pf->statusMessage().c_str());
      for (const TopoDS_Shape& f : core::subShapes(doc.bodyShape(body), TopAbs_FACE)) {
        BRepAdaptor_Surface sf(TopoDS::Face(f));
        if (sf.GetType() == GeomAbs_Cylinder)
          qWarning("cylinder r=%g at (%g, %g) area %g", sf.Cylinder().Radius(), sf.Cylinder().Location().X(),
                   sf.Cylinder().Location().Y(), core::computeSignature(f).size);
      }
    }
    // +20 x 60 x 20 of block, minus the longer chamfers (~2 x 20 x 0.5)
    QVERIFY2(near(v5, v4 + 20 * 60 * 20, 100), qPrintable(QString::number(v5 - v4)));
    QVERIFY(doc.undoStack().canUndo());

    // ------------------------------------------------- second part in component Lever
    QVERIFY(mgr.run("assembly.new-component", {"Lever"}));
    mgr.accept();
    ObjectId leverComp = core::kInvalidId;
    for (const core::Component* c : std::as_const(doc).components())
      if (c->name == "Lever") leverComp = c->id;
    QVERIFY(leverComp != core::kInvalidId);
    QCOMPARE(doc.activeComponent(), leverComp);
    QVERIFY(mgr.run("sketch.create", {"XY"}));
    Rig::pump();
    sketch::SketchFeature* sk3 = r.sketchFeature();
    QVERIFY(sk3);
    QCOMPARE(sk3->component(), leverComp);
    QVERIFY(mgr.run("sketch.rectangle"));
    r.click(0, 0);
    QVERIFY(mgr.runCommandLine("10,40"));
    QVERIFY(mgr.run("sketch.finish"));
    doc.selection().set(sk3->profiles()[0].ref);
    QVERIFY(mgr.run("part.extrude"));
    QVERIFY(mgr.runCommandLine("5"));
    QVERIFY(mgr.runCommandLine("operation=New Body"));
    mgr.accept();
    const core::Feature* leverExtrude = r.lastFeature();
    QCOMPARE(leverExtrude->status(), core::FeatureStatus::Ok);
    const ObjectId leverBody = leverExtrude->createdBodies()[0];
    QCOMPARE(doc.body(leverBody)->component, leverComp);
    QVERIFY(near(r.volume(leverBody), 10 * 40 * 5, 1e-6));
    doc.setActiveComponent(core::kRootComponentId);

    // ----------------------------------------------------- ground Base, revolute joint
    doc.selection().set(ShapeRef::component(baseComp));
    QVERIFY(mgr.run("assembly.ground"));
    QVERIFY(doc.component(baseComp)->grounded);
    QVERIFY(!doc.component(leverComp)->grounded);

    doc.selection().clear();  // (a leftover selection would be taken as the first snap)
    QVERIFY(mgr.run("assembly.joint"));
    auto* jt = dynamic_cast<assembly::JointTool*>(mgr.activeTool());
    QVERIFY(jt);
    // snap 1 (moving component): centre of the lever's bottom face; snap 2: centre of Base's top face (50, 30, 20)
    const ShapeRef leverBottom = faceWithNormal(doc, leverBody, gp_Dir(0, 0, -1), 0.0);
    const ShapeRef baseTop = faceWithNormal(doc, body, gp_Dir(0, 0, 1), 20.0);
    QVERIFY(leverBottom.kind == ShapeKind::Face && baseTop.kind == ShapeKind::Face);
    jt->pick(r.ctx(), leverBottom, gp_Pnt(5, 20, 0));
    QCOMPARE(jt->stage(), assembly::JointTool::Stage::PickB);
    const core::Vec3 topCentre = core::computeSignature(core::resolveShape(doc, baseTop)).center;  // face centre snap
    jt->pick(r.ctx(), baseTop, topCentre.toPnt());
    QCOMPARE(jt->stage(), assembly::JointTool::Stage::Ready);
    QVERIFY(mgr.runCommandLine("REVOLUTE"));
    QCOMPARE(jt->jointType(), assembly::JointType::Revolute);
    mgr.accept();
    QVERIFY(!mgr.isActive());
    const ObjectId jointId = jt->createdJoint();
    const auto* joint = doc.featureAs<assembly::JointFeature>(jointId);
    QVERIFY(joint);
    QCOMPARE(joint->status(), core::FeatureStatus::Ok);
    QCOMPARE(joint->jointType(), assembly::JointType::Revolute);
    const gp_Pnt leverCentre0 = gp_Pnt(5, 20, 0).Transformed(doc.worldPlacement(leverComp));
    QVERIFY2(near(leverCentre0.X(), topCentre.x, 1e-6) && near(leverCentre0.Y(), topCentre.y, 1e-6) &&
                 near(leverCentre0.Z(), 20, 1e-6),
             qPrintable(QString("%1 %2 %3").arg(leverCentre0.X()).arg(leverCentre0.Y()).arg(leverCentre0.Z())));
    const gp_Pnt tip0 = gp_Pnt(5, 40, 0).Transformed(doc.worldPlacement(leverComp));

    // drive the joint to 30 degrees
    QVERIFY(mgr.run("assembly.drive-joint", {joint->name(), "30"}));
    QVERIFY(near(joint->primaryValue(), 30.0, 1e-9));
    mgr.accept();
    QVERIFY(near(joint->primaryValue(), 30.0, 1e-9));
    const gp_Pnt tip1 = gp_Pnt(5, 40, 0).Transformed(doc.worldPlacement(leverComp));
    const gp_Pnt axisPt = topCentre.toPnt();
    QVERIFY(near(tip0.Distance(axisPt), 20.0, 1e-6));
    QVERIFY(near(tip1.Distance(axisPt), 20.0, 1e-6));
    const gp_Vec a(axisPt, tip0), b(axisPt, tip1);
    QVERIFY2(near(a.Angle(b) * 180.0 / kPi, 30.0, 1e-6), qPrintable(QString::number(a.Angle(b) * 180.0 / kPi)));
    QVERIFY(near(tip1.Z(), 20.0, 1e-6));

    // ----------------------------------------------------------------------- drawing
    drawing::DrawingModel* model = drawing::DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    auto widget = std::make_unique<drawing::DrawingWorkspaceWidget>();
    widget->resize(1200, 800);
    widget->show();
    widget->setDocument(&doc);
    widget->setToolManager(&mgr);
    drawing::DrawingCanvas* canvas = widget->canvas();
    QVERIFY(canvas);

    QVERIFY(mgr.run("drawing.create", {"A3", "ISO"}));
    QCOMPARE(r.host.workspace(), core::Workspace::Drawing);
    QCOMPARE(model->sheets().size(), size_t(1));
    canvas->zoomToFit();
    // 1:2 so that front/top/right/iso + a section fit on A3 (the layout is asserted below)
    QVERIFY(mgr.run("drawing.base-view", {"front", "1:2"}));
    canvas->simulateMove({150, 110});
    canvas->simulateClick({150, 110});
    QVERIFY(!mgr.isActive());
    drawing::Sheet& sheet = *model->activeSheet();
    QCOMPARE(sheet.views.size(), size_t(1));
    const ObjectId baseView = sheet.views.front().id;
    QVERIFY(sheet.views.front().geometryValid);
    QVERIFY(!sheet.views.front().curves.empty());
    // top (above), right (to the right) and iso projections
    canvas->setSelection({baseView});
    QVERIFY(mgr.run("drawing.projected-view", {"above", "right", "iso"}));
    QVERIFY(!mgr.isActive());
    QCOMPARE(sheet.views.size(), size_t(4));
    // section through the d12 hole (sketch centre (40,30)): a vertical cut line in the front view,
    // placed to the left of it (above / right / above-right are taken by the projections)
    const drawing::DrawingView& front = *sheet.view(baseView);
    const drawing::Point2 bp = front.position;
    const double cutX = front.toSheet(drawing::worldToView(front.frame, gp_Pnt(40, 30, 10))).x;
    canvas->setSelection({baseView});
    QVERIFY(mgr.run("drawing.section-view",
                    {std::format("{},{}", cutX, bp.y - 25), std::format("{},{}", cutX, bp.y + 25),
                     std::format("{},{}", bp.x - 100, bp.y)}));
    QVERIFY(!mgr.isActive());
    QCOMPARE(sheet.views.size(), size_t(5));
    int sections = 0;
    for (const drawing::DrawingView& v : sheet.views) {
      QVERIFY2(v.geometryValid, std::format("view {} invalid", v.id).c_str());
      if (v.type == drawing::ViewType::Section) ++sections;
    }
    QCOMPARE(sections, 1);
    // layout: every view inside the frame, no two views overlapping
    const drawing::BBox2 frame = sheet.frameRect();
    for (const drawing::DrawingView& v : sheet.views) {
      drawing::BBox2 b = v.sheetBBox();
      QVERIFY2(b.min.x >= frame.min.x && b.min.y >= frame.min.y && b.max.x <= frame.max.x && b.max.y <= frame.max.y,
               std::format("view {} ({}..{} x {}..{}) outside the frame ({}..{} x {}..{})", v.label, b.min.x, b.max.x, b.min.y,
                           b.max.y, frame.min.x, frame.max.x, frame.min.y, frame.max.y)
                   .c_str());
    }
    for (size_t i = 0; i < sheet.views.size(); ++i)
      for (size_t j = i + 1; j < sheet.views.size(); ++j) {
        drawing::BBox2 a = sheet.views[i].sheetBBox(), b = sheet.views[j].sheetBBox();
        bool overlap = a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
        QVERIFY2(!overlap, std::format("views {} and {} overlap", sheet.views[i].label, sheet.views[j].label).c_str());
      }
    // dimensions on the Base body in the front view: its top edge (width) and left edge (height),
    // picked from the projected curves so the test does not depend on the view scale
    // (the longest one at the extreme: the chamfered fillet corners project as short pieces)
    auto baseEdge = [&](bool horizontal) -> const drawing::Curve2D* {
      const drawing::DrawingView* fv = sheet.view(baseView);
      const drawing::Curve2D* best = nullptr;
      for (const drawing::Curve2D& c : fv->curves) {
        if (c.cls != drawing::LineClass::Visible || c.type != drawing::CurveType::Segment || c.sourceBody != body) continue;
        bool h = std::abs(c.a.y - c.b.y) < 1e-6, vert = std::abs(c.a.x - c.b.x) < 1e-6;
        if (horizontal ? !h : !vert) continue;
        if (!best) {
          best = &c;
          continue;
        }
        double d = horizontal ? c.a.y - best->a.y : best->a.x - c.a.x;  // > 0: more extreme
        if (d > 1e-6 || (std::abs(d) <= 1e-6 && c.length() > best->length())) best = &c;
      }
      return best;
    };
    const drawing::Curve2D* topEdge = baseEdge(true);
    const drawing::Curve2D* leftEdge = baseEdge(false);
    QVERIFY(topEdge && leftEdge);
    QVERIFY2(!topEdge->sourceName.empty(), "projected curves carry the edge's history name");
    const double topLen = topEdge->length(), leftLen = leftEdge->length();
    {
      const drawing::DrawingView* fv = sheet.view(baseView);
      drawing::Point2 tm = fv->toSheet(topEdge->midpoint()), lm = fv->toSheet(leftEdge->midpoint());
      QVERIFY(mgr.run("drawing.dimension", {std::format("{},{}", tm.x, tm.y), std::format("{},{}", tm.x, tm.y - 15)}));
      QVERIFY(mgr.run("drawing.dimension", {std::format("{},{}", lm.x, lm.y), std::format("{},{}", lm.x - 15, lm.y)}));
    }
    if (mgr.isActive()) mgr.cancel();
    QCOMPARE(sheet.dimensions.size(), size_t(2));
    for (const drawing::Dimension& d : sheet.dimensions) QVERIFY(d.valid);
    QVERIFY2(near(sheet.dimensions[0].value, topLen, 1e-6), qPrintable(QString::number(sheet.dimensions[0].value)));
    QVERIFY2(near(sheet.dimensions[1].value, leftLen, 1e-6), qPrintable(QString::number(sheet.dimensions[1].value)));
    for (const drawing::Dimension& d : sheet.dimensions)
      QVERIFY2(!d.refs.empty() && !d.refs.front().name.empty(), "dimension references the edge by history name");
    const ObjectId widthDim = sheet.dimensions[0].id, heightDim = sheet.dimensions[1].id;
    // ------------------------ edit the model under the drawing: block width 100 -> 120
    // Every edge index of the body changes (the hole pattern is recomputed), yet the dimension
    // stays on the same edge and reads 20 mm more; the height dimension is untouched.
    r.host.setWorkspace(core::Workspace::Design);
    QVERIFY(mgr.run("sketch.edit", {std::to_string(sketch1Id)}));
    Rig::pump();
    QVERIFY(r.session());
    const sketch::Constraint* d1b = sk1->sketch().dimensionByName("d1");
    QVERIFY(d1b);
    QVERIFY(mgr.run("sketch.dimension-edit", {std::to_string(d1b->id), "120"}));
    QVERIFY(mgr.run("sketch.finish"));
    doc.recompute();
    r.host.setWorkspace(core::Workspace::Drawing);
    QVERIFY(!model->isBusy());
    {
      const drawing::Dimension* wd = sheet.dimension(widthDim);
      const drawing::Dimension* hd = sheet.dimension(heightDim);
      QVERIFY(wd && hd);
      QVERIFY2(wd->valid && !wd->orphaned, "width dimension follows the edited edge");
      QVERIFY2(near(wd->value, topLen + 20.0, 1e-6), qPrintable(QString("%1 (was %2)").arg(wd->value).arg(topLen)));
      QVERIFY(hd->valid && !hd->orphaned);
      QVERIFY2(near(hd->value, leftLen, 1e-6), qPrintable(QString::number(hd->value)));
    }
    // parts list (placed by click; a click over the title block snaps above it) and balloons
    canvas->setSelection({baseView});
    QVERIFY(mgr.run("drawing.parts-list"));
    canvas->simulateMove({300, 240});
    canvas->simulateClick({300, 240});
    QVERIFY(!mgr.isActive());
    QCOMPARE(sheet.partsLists.size(), size_t(1));
    QCOMPARE(sheet.partsLists.front().rows.size(), size_t(2));  // Base, Lever
    {
      const drawing::PartsList& pl = sheet.partsLists.front();
      const drawing::BBox2 tb = sheet.titleBlockRect();
      QVERIFY2(pl.position.x >= frame.min.x && pl.position.x + pl.width() <= frame.max.x + 1e-6 && pl.position.y >= frame.min.y &&
                   pl.position.y + pl.height() <= frame.max.y + 1e-6,
               "parts list inside the frame");
      bool overTitle = pl.position.x < tb.max.x && tb.min.x < pl.position.x + pl.width() && pl.position.y < tb.max.y &&
                       tb.min.y < pl.position.y + pl.height();
      QVERIFY2(!overTitle, "parts list does not cover the title block");
    }
    canvas->setSelection({baseView});
    QVERIFY(mgr.run("drawing.balloon-all"));
    if (mgr.isActive()) mgr.cancel();
    QVERIFY(!sheet.balloons.empty());

    // ----------------------------------------------------------------------- exports
    const std::string stepA = outPath("assembly.step"), stl = outPath("bracket.stl"), pdf = outPath("drawing.pdf"),
                      svg = outPath("drawing.svg"), dxf = outPath("drawing.dxf"), csv = outPath("parts-list.csv"),
                      bom = outPath("bom.csv"), fcadFinal = outPath("assembly.fcad");
    QVERIFY(mgr.run("drawing.export-pdf", {pdf}));
    QVERIFY(mgr.run("drawing.export-svg", {svg}));
    QVERIFY(mgr.run("drawing.export-dxf", {dxf}));
    QVERIFY(mgr.run("drawing.export-csv", {csv}));
    r.host.setWorkspace(core::Workspace::Design);
    QVERIFY(mgr.run("assembly.export-step", {stepA}));
    QVERIFY(mgr.run("assembly.export-bom", {bom}));
    doc.selection().set(ShapeRef::body(body));
    QVERIFY(mgr.run("part.export-stl", {stl}));
    QVERIFY2(doc.save(fcadFinal, &err), err.c_str());
    for (const auto& m : r.host.messages())
      QVERIFY2(m.first != core::MessageLevel::Error, m.second.c_str());

    for (const std::string& p : {stepA, stl, pdf, svg, dxf, csv, bom, fcadFinal, outPath("bracket.step")}) {
      QFileInfo fi(QString::fromStdString(p));
      const qint64 minSize = fi.suffix() == "csv" ? 20 : 200;  // two-row tables are short
      QVERIFY2(fi.exists() && fi.size() > minSize, p.c_str());
    }
    // PDF / SVG markers
    {
      QFile f(QString::fromStdString(pdf));
      QVERIFY(f.open(QIODevice::ReadOnly));
      QVERIFY(f.read(5) == "%PDF-");
      QFile s(QString::fromStdString(svg));
      QVERIFY(s.open(QIODevice::ReadOnly));
      const QByteArray svgText = s.readAll();
      QVERIFY(svgText.contains("<svg") && svgText.contains("</svg>"));
    }
    // DXF parses as group-code pairs and has geometry + dimension entities
    {
      std::map<std::string, int> ents;
      std::string why;
      QVERIFY2(parseDxf(QString::fromStdString(dxf), ents, &why), why.c_str());
      int geometry = 0;
      for (const char* k : {"LINE", "CIRCLE", "ARC", "POLYLINE", "LWPOLYLINE", "SPLINE"}) geometry += ents[k];
      QVERIFY2(geometry > 20, std::format("{} geometry entities", geometry).c_str());
      QVERIFY2(ents["DIMENSION"] + ents["TEXT"] + ents["MTEXT"] > 0, "dimension/text entities");
    }
    // STEP round trip: the bracket re-imports with the same volume, the assembly file has both parts
    {
      part::ImportFeature::Imported imp;
      QVERIFY2(part::ImportFeature::readFile(outPath("bracket.step"), imp, &err), err.c_str());
      QCOMPARE(imp.shapes.size(), size_t(1));
      QVERIFY2(near(core::volume(imp.shapes[0]), v4, 1e-2), qPrintable(QString::number(core::volume(imp.shapes[0]) - v4)));
      part::ImportFeature::Imported asm2;
      QVERIFY2(part::ImportFeature::readFile(stepA, asm2, &err), err.c_str());
      double total = 0;
      for (const TopoDS_Shape& s : asm2.shapes) total += core::volume(s);
      // the block grew 100 -> 120 under the drawing; the lever is 2000 mm3
      QVERIFY2(near(total, r.volume(body) + 2000, 1e-1), qPrintable(QString::number(total)));
      QVERIFY2(r.volume(body) > v5 + 20000, "the 120 mm block is bigger than the 100 mm one");
      QFile f(QString::fromStdString(stepA));
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QByteArray text = f.readAll();
      QVERIFY(text.startsWith("ISO-10303-21"));
      QVERIFY(text.contains("END-ISO-10303-21"));
      QVERIFY(text.contains("Lever") && text.contains("Base"));
    }
    // STL: mesh of the bracket, re-read volume close to the exact one
    {
      part::ImportFeature::Imported mesh;
      QVERIFY2(part::ImportFeature::readFile(stl, mesh, &err), err.c_str());
      QCOMPARE(mesh.shapes.size(), size_t(1));
      const double vNow = r.volume(body);
      QVERIFY2(near(core::volume(mesh.shapes[0]), vNow, vNow * 0.01), qPrintable(QString::number(core::volume(mesh.shapes[0]))));
    }
    // CSV parts list / BOM
    {
      QFile c(QString::fromStdString(csv));
      QVERIFY(c.open(QIODevice::ReadOnly));
      const QByteArray t = c.readAll();
      QVERIFY(t.startsWith("Item,"));
      QVERIFY(t.contains("Base") && t.contains("Lever"));
      QFile b(QString::fromStdString(bom));
      QVERIFY(b.open(QIODevice::ReadOnly));
      QVERIFY(b.readAll().contains("Lever"));
    }
    // the final document reloads cleanly with the drawing attached
    {
      core::Document copy;
      QVERIFY2(copy.load(fcadFinal, &err), err.c_str());
      QCOMPARE(copy.featureCount(), doc.featureCount());
      for (const auto& f : doc.features())
        QVERIFY2(f->status() == core::FeatureStatus::Ok,
                 std::format("live {}: {} '{}'", f->name(), core::featureStatusName(f->status()), f->statusMessage()).c_str());
      for (const auto& f : copy.features())
        QVERIFY2(f->status() == core::FeatureStatus::Ok,
                 std::format("reloaded {}: {} '{}'", f->name(), core::featureStatusName(f->status()), f->statusMessage()).c_str());
      QVERIFY(!copy.extensionData("drawing").isEmpty());
      const double vNow = r.volume(body);
      QVERIFY(near(core::volume(copy.bodyShape(body)), vNow, 1e-6 * vNow));
    }
    widget.reset();
  }
};

QTEST_MAIN(E2ETest)
#include "test_e2e.moc"
