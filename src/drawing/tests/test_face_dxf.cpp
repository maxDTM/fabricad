// Tests for the P3 1:1 face / sketch DXF (drawing/FaceDxf) and the DxfWriter R2000 model mode;
// the sheet DXF (R12) must stay byte for byte what main wrote (tests/fixtures/sheet_r12_golden.dxf,
// generated from main's DxfWriter with the same sample as below).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DxfWriter.h"
#include "drawing/FaceDxf.h"
#include "drawing/SheetRenderer.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_Ellipse.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>

#include <QFile>
#include <QtTest/QtTest>

#include <cmath>
#include <map>
#include <sstream>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {

std::string sheetSample() {
  DxfWriter w(210.0, 297.0);
  DisplayList dl;
  dl.curves.push_back(Curve2D::segment({10, 20}, {110, 20}, LineClass::Visible));
  dl.curves.push_back(Curve2D::segment({10, 20}, {10, 70}, LineClass::Hidden));
  dl.curves.push_back(Curve2D::circle({60, 45}, 3.3, LineClass::Visible));
  dl.curves.push_back(Curve2D::arc({60, 45}, 8, 30, 120, LineClass::Center));
  dl.curves.push_back(Curve2D::arc({60, 45}, 9, 200, -90, LineClass::Tangent));
  dl.curves.push_back(Curve2D::polyline({{0, 0}, {5, 5}, {10, 0}}, LineClass::Hatch));
  DisplayText t;
  t.text = "\xC3\x98" "6.6 \xC2\xB1" "0.1 45\xC2\xB0 100%%";
  t.pos = {20, 30};
  t.height = 3.5;
  t.halign = HAlign::Center;
  t.valign = VAlign::Middle;
  t.cls = LineClass::Annotation;
  dl.texts.push_back(t);
  DisplayFill f;
  f.cls = LineClass::Dimension;
  f.polygon = {{1, 1}, {4, 1}, {2.5, 3}};
  dl.fills.push_back(f);
  w.addDisplayList(dl);
  return w.finish();
}

std::string fixture(const char* name) {
  std::string root = __FILE__;
  root = root.substr(0, root.rfind("/src/"));
  QFile f(QString::fromStdString(root + "/tests/fixtures/" + name));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll().toStdString();
}

struct Entity {
  std::string type, layer;
  std::vector<std::pair<int, std::string>> groups;
  std::vector<double> all(int code) const {
    std::vector<double> v;
    for (const auto& [c, s] : groups)
      if (c == code) v.push_back(std::stod(s));
    return v;
  }
  double one(int code, double def = 0) const {
    auto v = all(code);
    return v.empty() ? def : v.front();
  }
};

/// Group-code pairs of the ENTITIES section; R12 VERTEX entities are folded into their POLYLINE.
std::vector<Entity> entities(const std::string& dxf) {
  std::vector<std::pair<int, std::string>> g;
  std::istringstream in(dxf);
  std::string a, b;
  while (std::getline(in, a) && std::getline(in, b)) g.emplace_back(std::stoi(a), b);
  std::vector<Entity> out;
  bool inEnt = false;
  for (size_t i = 0; i < g.size(); ++i) {
    if (g[i].first == 2 && g[i].second == "ENTITIES") inEnt = true;
    if (!inEnt) continue;
    if (g[i].first == 0) {
      if (g[i].second == "ENDSEC") break;
      if (g[i].second == "SEQEND") continue;
      if (g[i].second == "VERTEX" && !out.empty()) continue;
      out.push_back({g[i].second, {}, {}});
      continue;
    }
    if (out.empty()) continue;
    if (g[i].first == 8 && out.back().layer.empty()) out.back().layer = g[i].second;
    out.back().groups.push_back(g[i]);
  }
  return out;
}

std::string header(const std::string& dxf, const std::string& var) {
  const size_t p = dxf.find(var + "\n");
  if (p == std::string::npos) return {};
  std::istringstream in(dxf.substr(p + var.size() + 1));
  std::string code, value;
  std::getline(in, code);
  std::getline(in, value);
  return value;
}

/// 100 x 50 x 6 plate with four 6.6 holes; its top face (z = 6).
TopoDS_Face plateTop() {
  TopoDS_Shape s = BRepPrimAPI_MakeBox(gp_Pnt(20, 30, 0), 100, 50, 6).Shape();
  for (auto [x, y] : {std::pair{30.0, 40.0}, {110.0, 40.0}, {30.0, 70.0}, {110.0, 70.0}})
    s = BRepAlgoAPI_Cut(s, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, -1), gp_Dir(0, 0, 1)), 3.3, 8).Shape()).Shape();
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
    BRepAdaptor_Surface a(TopoDS::Face(ex.Current()));
    if (a.GetType() == GeomAbs_Plane && std::abs(a.Value(a.FirstUParameter(), a.FirstVParameter()).Z() - 6) < 1e-9) {
      gp_Dir n = a.Plane().Axis().Direction();
      if (ex.Current().Orientation() == TopAbs_REVERSED) n.Reverse();
      if (n.Z() > 0.5) return TopoDS::Face(ex.Current());
    }
  }
  return {};
}

}  // namespace

class TestFaceDxf : public QObject {
  Q_OBJECT
 private slots:
  void sheetR12Unchanged() {
    const std::string golden = fixture("sheet_r12_golden.dxf");
    QVERIFY(!golden.empty());
    QVERIFY2(sheetSample() == golden, "sheet DXF R12 output is byte-identical to main");
  }

  void plateR2000Millimetres() {
    FaceDxfOptions o;
    FaceDxfResult r = faceDxf({plateTop()}, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(header(r.dxf, "$ACADVER"), std::string("AC1015"));
    QCOMPARE(header(r.dxf, "$INSUNITS"), std::string("4"));
    QVERIFY(r.dxf.find("AcDbPolyline") != std::string::npos);
    QVERIFY(r.dxf.find("*Model_Space") != std::string::npos);
    QCOMPARE(r.outer, 1);
    QCOMPARE(r.inner, 4);
    QVERIFY(std::abs(r.width - 100) < 1e-6 && std::abs(r.height - 50) < 1e-6);
    int polys = 0, circles = 0;
    for (const Entity& e : entities(r.dxf)) {
      if (e.type == "LWPOLYLINE") {
        ++polys;
        QCOMPARE(e.layer, std::string("OUTER"));
        QCOMPARE(int(e.one(70)), 1);  // closed
        QCOMPARE(int(e.one(90)), 4);
        auto xs = e.all(10), ys = e.all(20);
        QCOMPARE(*std::min_element(xs.begin(), xs.end()), 0.0);  // origin at the bounding box corner
        QVERIFY(std::abs(*std::max_element(xs.begin(), xs.end()) - 100.0) < 1e-6);
        QVERIFY(std::abs(*std::max_element(ys.begin(), ys.end()) - 50.0) < 1e-6);
      } else if (e.type == "CIRCLE") {
        ++circles;
        QCOMPARE(e.layer, std::string("INNER"));
        QVERIFY(std::abs(e.one(40) - 3.3) < 1e-6);
        QVERIFY(std::abs(e.one(10) - 10) < 1e-6 || std::abs(e.one(10) - 90) < 1e-6);
      }
    }
    QCOMPARE(polys, 1);
    QCOMPARE(circles, 4);
    // handles are unique
    std::map<std::string, int> handles;
    for (const Entity& e : entities(r.dxf))
      for (const auto& [c, v] : e.groups)
        if (c == 5) ++handles[v];
    for (const auto& [h, n] : handles) QCOMPARE(n, 1);
  }

  void plateInchesAndR12() {
    FaceDxfOptions o;
    o.inches = true;
    FaceDxfResult r = faceDxf({plateTop()}, o);
    QVERIFY(r.ok);
    QCOMPARE(header(r.dxf, "$INSUNITS"), std::string("1"));
    QVERIFY(std::abs(r.width - 100 / 25.4) < 1e-9);
    o.version = DxfWriter::Version::R12;
    o.inches = false;
    r = faceDxf({plateTop()}, o);
    QVERIFY(r.ok);
    QCOMPARE(header(r.dxf, "$ACADVER"), std::string("AC1009"));
    QVERIFY(r.dxf.find("LWPOLYLINE") == std::string::npos);
    QVERIFY(r.dxf.find("AcDb") == std::string::npos);
    bool closedPoly = false;
    for (const Entity& e : entities(r.dxf))
      if (e.type == "POLYLINE" && int(e.one(70)) == 1) closedPoly = true;
    QVERIFY(closedPoly);
  }

  void slotBulges() {
    // a slot (two lines, two half circles) is one closed polyline with two bulges of +-1
    gp_Pnt a(0, 0, 0), b(40, 0, 0), c(40, 10, 0), d(0, 10, 0);
    BRepBuilderAPI_MakeWire w;
    w.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(40, 5, 0), gp_Dir(0, 0, 1)), 5), b, c).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(c, d).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0, 5, 0), gp_Dir(0, 0, 1)), 5), d, a).Edge());
    TopoDS_Face f = BRepBuilderAPI_MakeFace(w.Wire(), true).Face();
    FaceDxfResult r = faceDxf({f}, {});
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(std::abs(r.width - 50) < 2e-3);  // arc extremes from a 0.001 mm deflection sampling
    int found = 0;
    for (const Entity& e : entities(r.dxf))
      if (e.type == "LWPOLYLINE") {
        ++found;
        auto bulges = e.all(42);
        QCOMPARE(bulges.size(), size_t(2));
        for (double bl : bulges) QVERIFY2(std::abs(std::abs(bl) - 1.0) < 1e-9, "half circle bulge");
        QVERIFY(bulges[0] * bulges[1] > 0);  // both arcs turn the same way round the contour
      }
    QCOMPARE(found, 1);
  }

  void ellipseSplineAndArcs() {
    Handle(Geom_Ellipse) el = new Geom_Ellipse(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 30, 12);
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(el).Edge();
    TopoDS_Face f = BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(e).Wire(), true).Face();
    FaceDxfResult r = faceDxf({f}, {});
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.splines, 1);
    QVERIFY(r.dxf.find("AcDbSpline") != std::string::npos);
    FaceDxfOptions o;
    o.arcTolerance = 0.01;
    r = faceDxf({f}, o);
    QVERIFY(r.ok);
    QCOMPARE(r.splines, 0);
    QVERIFY(r.dxf.find("SPLINE") == std::string::npos);
    // every polyline vertex lies on the ellipse (shifted by the bounding-box origin -30, -12)
    int verts = 0;
    for (const Entity& en : entities(r.dxf)) {
      if (en.type != "LWPOLYLINE") continue;
      auto xs = en.all(10), ys = en.all(20);
      for (size_t i = 0; i < xs.size(); ++i) {
        GeomAPI_ProjectPointOnCurve p(gp_Pnt(xs[i] - 30, ys[i] - 12, 0), el);
        QVERIFY(p.NbPoints() > 0 && p.LowerDistance() < 0.01);
        ++verts;
      }
      QVERIFY(!en.all(42).empty());  // arcs, not just a polyline
    }
    QVERIFY(verts >= 8 && verts < 200);
  }

  void sketchNesting() {
    BRep_Builder b;
    TopoDS_Compound edges, constr;
    b.MakeCompound(edges);
    b.MakeCompound(constr);
    gp_Pnt p[4] = {{0, 0, 0}, {60, 0, 0}, {60, 30, 0}, {0, 30, 0}};
    for (int i = 0; i < 4; ++i) b.Add(edges, BRepBuilderAPI_MakeEdge(p[i], p[(i + 1) % 4]).Edge());
    b.Add(edges, BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(15, 15, 0), gp_Dir(0, 0, 1)), 4)).Edge());
    b.Add(constr, BRepBuilderAPI_MakeEdge(gp_Pnt(30, 0, 0), gp_Pnt(30, 30, 0)).Edge());
    FaceDxfResult r = sketchDxf(gp_Ax3(), edges, constr, {});
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.outer, 1);
    QCOMPARE(r.inner, 1);
    QCOMPARE(r.etch, 1);
    std::map<std::string, std::string> layerOf;
    for (const Entity& e : entities(r.dxf)) layerOf[e.type] = e.layer;
    QCOMPARE(layerOf["CIRCLE"], std::string("INNER"));
    QCOMPARE(layerOf["LWPOLYLINE"], std::string("OUTER"));
    QCOMPARE(layerOf["LINE"], std::string("ETCH"));
  }

  void optionWords() {
    FaceDxfOptions o;
    QVERIFY(applyFaceDxfOption("r12", o) && o.version == DxfWriter::Version::R12);
    QVERIFY(applyFaceDxfOption("units=in", o) && o.inches);
    QVERIFY(applyFaceDxfOption("arcs=0.02", o) && std::abs(o.arcTolerance - 0.02) < 1e-12);
    QVERIFY(applyFaceDxfOption("noetch", o) && !o.etch);
    QVERIFY(!applyFaceDxfOption("plate.dxf", o));
  }

  void nonPlanarRefused() {
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5, 10).Shape();
    for (TopExp_Explorer ex(cyl, TopAbs_FACE); ex.More(); ex.Next()) {
      BRepAdaptor_Surface a(TopoDS::Face(ex.Current()));
      if (a.GetType() == GeomAbs_Cylinder) {
        FaceDxfResult r = faceDxf({TopoDS::Face(ex.Current())}, {});
        QVERIFY(!r.ok);
        QVERIFY(r.error.find("planar") != std::string::npos);
      }
    }
  }
};

QTEST_MAIN(TestFaceDxf)
#include "test_face_dxf.moc"
