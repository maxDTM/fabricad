#include "core/GeometryRefs.h"
#include "core/ShapeRef.h"
#include "core/ShapeUtils.h"
#include "support/TestFeatures.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <QtTest/QtTest>

using namespace fabricad::core;

namespace {
TopoDS_Shape topFace(const TopoDS_Shape& box) {
  for (const auto& f : subShapes(box, TopAbs_FACE)) {
    auto pl = facePlane(f);
    if (pl && pl->Direction().Z() > 0.9) return f;
  }
  return {};
}
}  // namespace

class ShapeRefTest : public QObject {
  Q_OBJECT
 private slots:
  void signatureOfBoxFace() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    TopoDS_Shape top = topFace(box);
    QVERIFY(!top.IsNull());
    GeomSignature s = computeSignature(top);
    QCOMPARE(s.geomType, 0);  // plane
    QVERIFY(std::abs(s.size - 200.0) < 1e-6);
    QVERIFY(std::abs(s.center.z - 30.0) < 1e-9);
    QVERIFY(s.direction.z > 0.99);  // oriented normal points out
    QCOMPARE(s.vertexCount, 4);
    GeomSignature back = GeomSignature::fromJson(s.toJson());
    QVERIFY(back == s);
  }

  void resolveByIndexThenBySignature() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    TopoDS_Shape top = topFace(box);
    ShapeRef ref = ShapeRef::subShape(42, box, top);
    QCOMPARE(ref.kind, ShapeKind::Face);
    QVERIFY(ref.index >= 1);
    // exact on the same shape
    ResolveResult r = resolveSubShape(box, ref);
    QVERIFY(r);
    QVERIFY(r.exact);
    QVERIFY(r.shape.IsSame(top));
    // shape changed slightly (taller box): index may still be valid but signature differs ->
    // best match must be the new top face
    TopoDS_Shape box2 = BRepPrimAPI_MakeBox(10, 20, 35).Shape();
    ResolveResult r2 = resolveSubShape(box2, ref);
    QVERIFY(r2);
    auto pl = facePlane(r2.shape);
    QVERIFY(pl);
    QVERIFY(pl->Direction().Z() > 0.9);
    QVERIFY(std::abs(pl->Location().Z() - 35.0) < 1e-6);
    // a hole through the top: topology changes, the top face keeps its identity by signature
    gp_Ax2 ax(gp_Pnt(5, 10, -1), gp_Dir(0, 0, 1));
    TopoDS_Shape cut = BRepAlgoAPI_Cut(box, BRepPrimAPI_MakeCylinder(ax, 2, 100).Shape()).Shape();
    ResolveResult r3 = resolveSubShape(cut, ref);
    QVERIFY(r3);
    auto pl3 = facePlane(r3.shape);
    QVERIFY(pl3 && pl3->Direction().Z() > 0.9);
    QVERIFY(std::abs(pl3->Location().Z() - 30.0) < 1e-6);
    // completely different shape -> no match
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5, 10).Shape();
    ShapeRef edgeRef = ShapeRef::edge(42, 99);
    QVERIFY(!resolveSubShape(cyl, edgeRef));
  }

  void jsonAndFactories() {
    ShapeRef p = ShapeRef::profile(5, Vec3(1, 1, 0), {4, 2});
    QCOMPARE(p.name, std::string("2,4"));
    QVERIFY(p.hasPoint);
    ShapeRef back = ShapeRef::fromJson(p.toJson());
    QVERIFY(back == p);
    QCOMPARE(back.point, p.point);
    ShapeRef sp = ShapeRef::sketchPoint(5, 3, SketchPointRole::Center);
    QCOMPARE(sp.subIndex, 2);
    QVERIFY(ShapeRef::fromJson(sp.toJson()) == sp);
    QCOMPARE(ShapeRef::originPlane("XZ").owner, kOriginId);
    QVERIFY(ShapeRef::originAxis("Z") != ShapeRef::originAxis("X"));
    QVERIFY(ShapeRef::body(1).isBrep());
    QVERIFY(!ShapeRef::component(1).isBrep());
    QCOMPARE(shapeKindFromName("Profile"), ShapeKind::Profile);
    QVERIFY(hasKind(kPlaneKinds, ShapeKind::OriginPlane));
    QVERIFY(!hasKind(kPlaneKinds, ShapeKind::Edge));
  }

  void geometryRefsOnDocument() {
    fabricad::core::test::registerTestFeatures();
    Document doc;
    Feature* box = doc.addFeature(std::make_unique<fabricad::core::test::BoxFeature>());
    doc.recompute();
    ObjectId bodyId = box->createdBodies()[0];
    const TopoDS_Shape& shape = doc.bodyShape(bodyId);
    ShapeRef topRef = makeBrepRef(doc, bodyId, topFace(shape));
    auto pl = resolvePlane(doc, topRef);
    QVERIFY(pl);
    QVERIFY(std::abs(pl->Location().Z() - 30.0) < 1e-6);
    QVERIFY(std::abs(pl->Location().X()) < 1e-6);  // world origin projected onto the plane (stable)
    auto xy = resolvePlane(doc, ShapeRef::originPlane("XY"));
    QVERIFY(xy && xy->Direction().Z() > 0.99);
    auto xz = resolvePlane(doc, ShapeRef::originPlane("XZ"));
    QVERIFY(xz && xz->YDirection().Z() > 0.99);  // plane "up" is +Z (front view)
    auto yz = resolvePlane(doc, ShapeRef::originPlane("YZ"));
    QVERIFY(yz && yz->YDirection().Z() > 0.99);
    auto zAxis = resolveAxis(doc, ShapeRef::originAxis("Z"));
    QVERIFY(zAxis && zAxis->Direction().Z() > 0.99);
    // an edge of the box as axis, a vertex as point
    auto edges = subShapes(shape, TopAbs_EDGE);
    ShapeRef edgeRef = makeBrepRef(doc, bodyId, edges[0]);
    QVERIFY(resolveAxis(doc, edgeRef));
    auto verts = subShapes(shape, TopAbs_VERTEX);
    ShapeRef vRef = makeBrepRef(doc, bodyId, verts[0]);
    QVERIFY(resolvePoint(doc, vRef));
    QVERIFY(resolvePoint(doc, ShapeRef::originPoint()));
    QVERIFY(!resolveShape(doc, ShapeRef::face(999, 1)).IsNull() == false);
    // body ref resolves to the whole shape
    QVERIFY(resolveShape(doc, ShapeRef::body(bodyId)).IsSame(shape));
    QVERIFY(resolveShape(doc, ShapeRef::component(kRootComponentId)).ShapeType() == TopAbs_COMPOUND);
  }

  void shapeUtils() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    QVERIFY(isValidShape(box));
    QVERIFY(!isValidShape(TopoDS_Shape()));
    QVERIFY(std::abs(bboxDiagonal(box) - std::sqrt(100.0 + 400.0 + 900.0)) < 1e-3);
    QVERIFY(std::abs(volume(box) - 6000.0) < 1e-6);
    QVERIFY(std::abs(area(box) - 2 * (200 + 300 + 600)) < 1e-6);
    Vec3 c = centroid(box);
    QVERIFY(std::abs(c.x - 5) < 1e-9 && std::abs(c.y - 10) < 1e-9 && std::abs(c.z - 15) < 1e-9);
    QCOMPARE(subShapes(box, TopAbs_FACE).size(), size_t(6));
    gp_Trsf t;
    t.SetTranslation(gp_Vec(0, 0, 100));
    QVERIFY(std::abs(centroid(transformed(box, t)).z - 115) < 1e-9);
    QCOMPARE(shapeTypeName(box), std::string("Solid"));
    QVERIFY(shapeHash(box) != 0);
    TopoDS_Face pf = planeFace(originPlane("XY"), 10);
    QVERIFY(std::abs(area(pf) - 400.0) < 1e-6);
    QVERIFY(!makeCompound({}).IsNull() == false);
  }
};

QTEST_GUILESS_MAIN(ShapeRefTest)
#include "test_shaperef.moc"
