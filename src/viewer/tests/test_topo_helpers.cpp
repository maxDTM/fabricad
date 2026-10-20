// Unit tests for selection topology helpers (tangent chains).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/TopoHelpers.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <QtTest/QtTest>

using namespace fabricad::viewer;

class TopoHelpersTest : public QObject {
  Q_OBJECT
 private slots:
  void boxEdgesAreNotTangent() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    TopExp_Explorer ex(box, TopAbs_EDGE);
    auto chain = tangentChain(box, TopoDS::Edge(ex.Current()));
    QCOMPARE(chain.size(), size_t(1));
  }

  void lineArcLineChain() {
    // line -> tangent arc -> tangent line -> corner -> line
    TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    Handle(Geom_TrimmedCurve) arc = GC_MakeArcOfCircle(gp_Pnt(10, 0, 0), gp_Vec(1, 0, 0), gp_Pnt(20, 10, 0)).Value();
    TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(arc).Edge();
    TopoDS_Edge e3 = BRepBuilderAPI_MakeEdge(gp_Pnt(20, 10, 0), gp_Pnt(20, 20, 0)).Edge();
    TopoDS_Edge e4 = BRepBuilderAPI_MakeEdge(gp_Pnt(20, 20, 0), gp_Pnt(30, 20, 0)).Edge();
    BRepBuilderAPI_MakeWire mw;
    mw.Add(e1);
    mw.Add(e2);
    mw.Add(e3);
    mw.Add(e4);
    QVERIFY(mw.IsDone());
    TopoDS_Shape wire = mw.Wire();
    std::vector<TopoDS_Edge> edges;
    for (TopExp_Explorer ex(wire, TopAbs_EDGE); ex.More(); ex.Next()) edges.push_back(TopoDS::Edge(ex.Current()));
    QCOMPARE(edges.size(), size_t(4));
    auto chain = tangentChain(wire, edges[0]);
    QCOMPARE(chain.size(), size_t(3));
    auto fromLast = tangentChain(wire, edges[3]);
    QCOMPARE(fromLast.size(), size_t(1));
    QVERIFY(tangentChain(TopoDS_Shape(), edges[0]).empty());
  }
};

QTEST_GUILESS_MAIN(TopoHelpersTest)
#include "test_topo_helpers.moc"
