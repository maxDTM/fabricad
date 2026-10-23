// Tests for the P3 interop formats (production-features.md section 4): OBJ / glTF / 3MF mesh
// export reopened by readers, IGES round trip, STEP AP242 names / colours / product properties,
// the import report and sew / fix / unify options, and the script verbs.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "part/features/Import.h"
#include "part/io/IgesIO.h"
#include "part/io/MeshExport.h"
#include "part/io/StepIO.h"
#include "part/tools/ExportMeshTool.h"
#include "support/TestSketch.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Message_ProgressRange.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <RWGltf_CafReader.hxx>
#include <RWObj_CafReader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <StepData_StepModel.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <XSControl_WorkSession.hxx>
#include <gp_Ax2.hxx>

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest/QtTest>

#include <cmath>
#include <map>
#include <set>

using namespace fabricad;
using namespace fabricad::part;

namespace {

/// 100 x 50 x 6 plate with four 6.6 mm holes (the P3 acceptance part).
TopoDS_Shape plate() {
  TopoDS_Shape s = BRepPrimAPI_MakeBox(100, 50, 6).Shape();
  for (auto [x, y] : {std::pair{10.0, 10.0}, {90.0, 10.0}, {10.0, 40.0}, {90.0, 40.0}}) {
    TopoDS_Shape c = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, -1), gp_Dir(0, 0, 1)), 3.3, 8).Shape();
    s = BRepAlgoAPI_Cut(s, c).Shape();
  }
  return s;
}

double volume(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  return g.Mass();
}

long long triangleCount(const TopoDS_Shape& s) {
  long long n = 0;
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
    TopLoc_Location loc;
    Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
    if (!t.IsNull()) n += t->NbTriangles();
  }
  return n;
}

std::set<std::string> labelNames(const Handle(TDocStd_Document)& doc) {
  std::set<std::string> out;
  Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  for (TDF_ChildIterator it(st->Label(), Standard_True); it.More(); it.Next()) {
    Handle(TDataStd_Name) n;
    if (it.Value().FindAttribute(TDataStd_Name::GetID(), n)) out.insert(TCollection_AsciiString(n->Get()).ToCString());
  }
  return out;
}

/// Colours of the document (colour tool and visual materials), as 0..255 RGB triples.
std::set<std::array<int, 3>> docColors(const Handle(TDocStd_Document)& doc) {
  std::set<std::array<int, 3>> out;
  auto add = [&](const Quantity_Color& c) {
    out.insert({int(std::lround(c.Red() * 255)), int(std::lround(c.Green() * 255)), int(std::lround(c.Blue() * 255))});
  };
  Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_ColorTool) ct = XCAFDoc_DocumentTool::ColorTool(doc->Main());
  Handle(XCAFDoc_VisMaterialTool) mt = XCAFDoc_DocumentTool::VisMaterialTool(doc->Main());
  for (TDF_ChildIterator it(st->Label(), Standard_True); it.More(); it.Next()) {
    Quantity_Color c;
    if (ct->GetColor(it.Value(), XCAFDoc_ColorSurf, c) || ct->GetColor(it.Value(), XCAFDoc_ColorGen, c)) add(c);
    if (Handle(XCAFDoc_VisMaterial) m = mt->GetShapeMaterial(it.Value()); !m.IsNull()) add(m->BaseColor().GetRGB());
  }
  return out;
}

Handle(TDocStd_Document) newDoc() {
  Handle(TDocStd_Document) d;
  XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", d);
  return d;
}

std::vector<io::MeshItem> twoItems() {
  io::MeshItem a;
  a.shape = plate();
  a.name = "Plate";
  a.color = {1.0f, 0.0f, 0.0f, 1.0f};
  a.component = "Base";
  io::MeshItem b;
  b.shape = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 10), 20, 20, 20).Shape();
  b.name = "Cube";
  b.color = {0.0f, 0.0f, 1.0f, 1.0f};
  return {a, b};
}

}  // namespace

class TestExportFormats : public QObject {
  Q_OBJECT
 private slots:
  void formatFromExtension() {
    QCOMPARE(io::meshFormatFromPath("a.OBJ"), std::optional(io::MeshFormat::Obj));
    QCOMPARE(io::meshFormatFromPath("x/y.glb"), std::optional(io::MeshFormat::Glb));
    QCOMPARE(io::meshFormatFromPath("y.gltf"), std::optional(io::MeshFormat::Gltf));
    QCOMPARE(io::meshFormatFromPath("p.3mf"), std::optional(io::MeshFormat::ThreeMf));
    QCOMPARE(io::meshFormatFromPath("p.stl"), std::optional(io::MeshFormat::Stl));
    QVERIFY(!io::meshFormatFromPath("p.step"));
  }

  void objReopens() {
    QTemporaryDir dir;
    const std::string path = dir.filePath("parts.obj").toStdString();
    io::MeshExportStats stats;
    std::string err;
    QVERIFY2(io::exportMesh(twoItems(), path, io::MeshFormat::Obj, {}, &err, &stats), err.c_str());
    QVERIFY(QFileInfo::exists(dir.filePath("parts.mtl")));
    QCOMPARE(stats.objects, 2);
    QVERIFY(stats.triangles > 100);
    Handle(TDocStd_Document) doc = newDoc();
    RWObj_CafReader r;
    r.SetDocument(doc);
    r.SetFileLengthUnit(0.001);
    r.SetSystemLengthUnit(0.001);
    QVERIFY(r.Perform(path.c_str(), Message_ProgressRange()));
    QCOMPARE(triangleCount(r.SingleShape()), stats.triangles);
    {
      QFile f(QString::fromStdString(path));
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QByteArray obj = f.readAll();
      QVERIFY2(obj.contains("Plate") && obj.contains("Cube"), "OBJ object / group names");
      QVERIFY(obj.contains("mtllib"));
    }
    const auto colors = docColors(doc);
    QVERIFY(colors.count({255, 0, 0}));
    QVERIFY(colors.count({0, 0, 255}));
  }

  void gltfReopens() {
    QTemporaryDir dir;
    for (const char* file : {"parts.glb", "parts.gltf"}) {
      const std::string path = dir.filePath(file).toStdString();
      io::MeshExportStats stats;
      std::string err;
      const io::MeshFormat f = *io::meshFormatFromPath(path);
      QVERIFY2(io::exportMesh(twoItems(), path, f, {}, &err, &stats), err.c_str());
      Handle(TDocStd_Document) doc = newDoc();
      RWGltf_CafReader r;
      r.SetDocument(doc);
      QVERIFY(r.Perform(path.c_str(), Message_ProgressRange()));
      QCOMPARE(triangleCount(r.SingleShape()), stats.triangles);
      const auto names = labelNames(doc);
      QVERIFY2(names.count("Base"), "component node name");
      QVERIFY2(names.count("Plate"), "body node name");
      QVERIFY2(names.count("Cube"), "free body node name");
      const auto colors = docColors(doc);
      QVERIFY(colors.count({255, 0, 0}));
      QVERIFY(colors.count({0, 0, 255}));
    }
  }

  void threeMfPackage() {
    QTemporaryDir dir;
    const std::string path = dir.filePath("print.3mf").toStdString();
    io::MeshExportOptions o;
    o.title = "Bracket";
    io::MeshExportStats stats;
    std::string err;
    const auto items = twoItems();
    QVERIFY2(io::exportMesh(items, path, io::MeshFormat::ThreeMf, o, &err, &stats), err.c_str());
    const auto zip = io::readZipStored(path);
    QVERIFY(zip.count("[Content_Types].xml"));
    QVERIFY(zip.count("_rels/.rels"));
    QVERIFY(zip.count("3D/3dmodel.model"));
    QVERIFY(zip.at("_rels/.rels").find("/3D/3dmodel.model") != std::string::npos);
    // core XML: model unit, resources/object/mesh/vertices/triangles, build items
    QXmlStreamReader x(QByteArray::fromStdString(zip.at("3D/3dmodel.model")));
    int vertices = 0, triangles = 0, objects = 0, items3 = 0, components = 0, bases = 0;
    bool unitOk = false, titleOk = false;
    std::vector<int> objVerts;
    std::map<std::pair<int, int>, int> edgeUse;  // per object: directed edge uses (watertight check)
    int curVerts = 0;
    bool watertight = true;
    while (!x.atEnd()) {
      x.readNext();
      if (!x.isStartElement()) {
        if (x.isEndElement() && x.name() == u"mesh") {
          for (const auto& [e, n] : edgeUse)
            if (n != 1 || !edgeUse.count({e.second, e.first})) watertight = false;
          edgeUse.clear();
        }
        continue;
      }
      const auto n = x.name();
      if (n == u"model") unitOk = x.attributes().value("unit") == u"millimeter" &&
                                  x.namespaceUri() == u"http://schemas.microsoft.com/3dmanufacturing/core/2015/02";
      else if (n == u"metadata" && x.attributes().value("name") == u"Title") titleOk = x.readElementText() == "Bracket";
      else if (n == u"object") ++objects;
      else if (n == u"vertices") curVerts = 0;
      else if (n == u"vertex") ++vertices, ++curVerts;
      else if (n == u"triangle") {
        ++triangles;
        const int v[3] = {x.attributes().value("v1").toInt(), x.attributes().value("v2").toInt(), x.attributes().value("v3").toInt()};
        for (int k = 0; k < 3; ++k) {
          QVERIFY(v[k] >= 0 && v[k] < curVerts);
          ++edgeUse[{v[k], v[(k + 1) % 3]}];
        }
      } else if (n == u"item") ++items3;
      else if (n == u"component") ++components;
      else if (n == u"base") ++bases;
    }
    QVERIFY2(!x.hasError(), qPrintable(x.errorString()));
    QVERIFY(unitOk);
    QVERIFY(titleOk);
    QCOMPARE(objects, 3);     // two meshes + the component "Base"
    QCOMPARE(components, 1);
    QCOMPARE(items3, 2);      // Base and the free Cube
    QCOMPARE(bases, 2);
    QCOMPARE(triangles, stats.triangles);
    QCOMPARE(vertices, stats.vertices);
    QVERIFY2(watertight, "every mesh edge is shared by exactly two triangles with opposite directions");
    // shared vertices: a closed mesh has V - E + F = 2 per genus-0 body (cube: 8 corners)
    io::IndexedMesh cube = io::triangulate(items[1].shape, 0.05, 11.46);
    QCOMPARE(int(cube.vertices.size()), 8);
    QCOMPARE(int(cube.triangles.size()), 12);
  }

  void zipCrc() {
    QCOMPARE(io::crc32("123456789"), 0xCBF43926u);
    QTemporaryDir dir;
    const std::string p = dir.filePath("a.zip").toStdString();
    QVERIFY(io::writeZip(p, {{"a.txt", "hello"}, {"b/c.txt", std::string(1000, 'x')}}));
    auto m = io::readZipStored(p);
    QCOMPARE(m.size(), size_t(2));
    QCOMPARE(m["a.txt"], std::string("hello"));
  }

  void igesRoundTrip() {
    QTemporaryDir dir;
    const TopoDS_Shape s = plate();
    const double v0 = volume(s);
    for (bool brep : {true, false}) {
      const std::string path = dir.filePath(brep ? "plate.igs" : "plate_faces.igs").toStdString();
      std::string err;
      QVERIFY2(io::exportIges({s}, path, brep, &err), err.c_str());
      std::vector<TopoDS_Shape> back;
      io::ImportReport rep;
      QVERIFY2(io::readIges(path, {}, back, &rep, &err), err.c_str());
      QCOMPARE(back.size(), size_t(1));
      QVERIFY(rep.entities > 0);
      QCOMPARE(rep.solids, 1);
      QCOMPARE(rep.failedFaces, 0);
      QCOMPARE(rep.faces, 10);  // 6 box faces + 4 holes
      const double v1 = volume(back.front());
      QVERIFY2(std::abs(v1 - v0) / v0 < 1e-6, qPrintable(QString("IGES %1: %2 vs %3").arg(brep).arg(v1).arg(v0)));
    }
  }

  void igesThroughImportFeature() {
    QTemporaryDir dir;
    const std::string path = dir.filePath("p.iges").toStdString();
    QVERIFY(io::exportIges({plate()}, path, false));
    ImportFeature::Imported out;
    std::string err;
    QVERIFY2(ImportFeature::readFile(path, out, &err), err.c_str());
    QCOMPARE(out.shapes.size(), size_t(1));
    QCOMPARE(out.names.front(), std::string("p"));
    QCOMPARE(out.report.format, std::string("IGES"));
    QVERIFY(out.report.sewn);
  }

  void stepSchemaWords() {
    std::string s = "AP214";
    QVERIFY(io::applyStepSchemaOption("schema=ap242", s));
    QCOMPARE(s, std::string("AP242"));
    QVERIFY(io::applyStepSchemaOption("AP203", s));
    QCOMPARE(s, std::string("AP203"));
    QVERIFY(!io::applyStepSchemaOption("a.step", s));
    bool pc = true;
    QVERIFY(applyStepOption("schema=ap242", pc));  // skipped as a path by the assembly exporter
    QVERIFY(pc);
    io::ImportOptions o;
    QVERIFY(io::applyImportOption("sew=on", o) && o.sew);
    QVERIFY(io::applyImportOption("fix=off", o) && !o.fix);
    QVERIFY(io::applyImportOption("unify", o) && o.unify);
    QVERIFY(!io::applyImportOption("a.step", o));
  }

  void stepAp242RoundTrip() {
    QTemporaryDir dir;
    for (const char* schema : {"AP242", "AP214", "AP203"}) {
      const std::string path = dir.filePath(QString("p_%1.step").arg(schema)).toStdString();
      StepBodyExport o;
      o.schema = schema;
      o.colors = {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};
      o.properties["Plate"] = {"FC-1001", "Base plate", "Aluminium 6061", "B"};
      std::string err;
      QVERIFY2(exportStep({plate(), BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 10), 20, 20, 20).Shape()}, {"Plate", "Cube"}, path, o, &err), err.c_str());
      QFile f(QString::fromStdString(path));
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QByteArray text = f.readAll();
      if (std::string(schema) == "AP242") QVERIFY2(text.contains("AP242"), "FILE_SCHEMA names AP242");
      if (std::string(schema) == "AP203") QVERIFY2(text.contains("CONFIG_CONTROL_DESIGN"), "AP203 schema");
      QVERIFY(text.contains("DESCRIPTIVE_REPRESENTATION_ITEM('material','Aluminium 6061')"));
      // STEPCAFControl_Reader: names, colours, no failures
      Handle(TDocStd_Document) doc = newDoc();
      STEPCAFControl_Reader r;
      r.SetNameMode(true);
      r.SetColorMode(true);
      QCOMPARE(r.ReadFile(path.c_str()), IFSelect_RetDone);
      QVERIFY(r.Transfer(doc));
      const auto names = labelNames(doc);
      QVERIFY(names.count("Plate"));
      QVERIFY(names.count("Cube"));
      if (std::string(schema) != "AP203") {  // AP203 has no presentation (colours)
        const auto colors = docColors(doc);
        QVERIFY(colors.count({255, 0, 0}));
        QVERIFY(colors.count({0, 0, 255}));
      }
      const auto props = io::readProductProperties(Handle(StepData_StepModel)::DownCast(r.ChangeReader().Model()));
      QVERIFY(props.count("Plate"));
      QCOMPARE(props.at("Plate").partNumber, std::string("FC-1001"));
      QCOMPARE(props.at("Plate").description, std::string("Base plate"));
      QCOMPARE(props.at("Plate").material, std::string("Aluminium 6061"));
      QCOMPARE(props.at("Plate").revision, std::string("B"));
      QVERIFY(!props.count("Cube"));
      // the import report of the same file: zero failed faces, valid
      ImportFeature::Imported imp;
      QVERIFY2(ImportFeature::readFile(path, imp, &err), err.c_str());
      QCOMPARE(imp.shapes.size(), size_t(2));
      QVERIFY(imp.report.entities > 50);
      QCOMPARE(imp.report.failedFaces, 0);
      QVERIFY(imp.report.valid);
      QCOMPARE(imp.report.faces, 16);
      QVERIFY(imp.report.text().find("failed faces: 0") != std::string::npos);
      QVERIFY(std::abs(volume(imp.shapes.front()) - volume(plate())) / volume(plate()) < 1e-6);
    }
  }

  void importUnifyOption() {
    // two boxes fused without unification have split coplanar faces; unify merges them
    QTemporaryDir dir;
    const std::string path = dir.filePath("split.step").toStdString();
    TopoDS_Shape a = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(10, 0, 0), 10, 10, 10).Shape();
    BRepAlgoAPI_Fuse fuse(a, b);
    QVERIFY(exportStep({fuse.Shape()}, {"Split"}, path, nullptr));
    ImportFeature::Imported plain, unified;
    std::string err;
    QVERIFY(ImportFeature::readFile(path, plain, &err));
    io::ImportOptions o;
    o.unify = true;
    QVERIFY(ImportFeature::readFile(path, o, unified, &err));
    QVERIFY(plain.report.faces > 6);
    QCOMPARE(unified.report.faces, 6);
    QVERIFY(unified.report.unified);
  }

  void scriptVerbs() {
    core::ToolRegistry registry;
    core::Document doc;
    core::NullViewerContext viewer;
    core::NullAppHost host;
    core::ToolManager manager{registry};
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    part::registerModule(ctx);
    part::test::registerTestSketch();
    manager.setDocument(&doc);
    manager.setViewer(&viewer);
    manager.setHost(&host);
    part::test::TestSketch* sk = part::test::addRectSketch(doc, "XY", 0, 0, 40, 20);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    doc.addFeature(std::move(ex));
    doc.recompute();
    QTemporaryDir dir;
    const QString d = dir.path() + "/";
    for (const QString& cmd : {QString("EXPORTOBJ %1a.obj quality=coarse"), QString("EXPORTGLTF %1a.glb"), QString("EXPORT3MF %1a.3mf"),
                               QString("EXPORTMESH %1b.3mf"), QString("EXPORTMESH %1b.stl"), QString("EXPORTIGES %1a.igs"),
                               QString("EXPORTIGES %1f.igs faces"), QString("EXPORTSTEP %1a.step schema=ap242"), QString("EXPORTOBJ %1y.obj yup")}) {
      QVERIFY2(manager.runCommandLine(cmd.arg(d).toStdString()), qPrintable(cmd));
    }
    for (const char* f : {"a.obj", "a.mtl", "a.glb", "a.3mf", "b.3mf", "b.stl", "a.igs", "f.igs", "a.step", "y.obj"})
      QVERIFY2(QFileInfo::exists(d + f), f);
    QFile step(d + "a.step");
    QVERIFY(step.open(QIODevice::ReadOnly));
    QVERIFY(step.readAll().contains("AP242"));
    // IMPORT of the IGES with options through the feature verb
    QVERIFY(manager.runCommandLine(("IMPORT " + d + "a.igs fix=off report=" + d + "report.txt").toStdString()));
    manager.accept();
    QVERIFY2(QFileInfo::exists(d + "report.txt"), "report=file");
    QFile rep(d + "report.txt");
    QVERIFY(rep.open(QIODevice::ReadOnly));
    QVERIFY(rep.readAll().contains("IGES"));
    // a mesh verb with the wrong extension is refused
    QVERIFY(!manager.runCommandLine(("EXPORT3MF " + d + "wrong.obj").toStdString()) || !QFileInfo::exists(d + "wrong.obj"));
  }
};

QTEST_MAIN(TestExportFormats)
#include "test_export_formats.moc"
