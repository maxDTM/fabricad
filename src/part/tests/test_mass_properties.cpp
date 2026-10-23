// Mass properties (production-features P1 §2.2): volume, mass, CoG and inertia against closed
// forms, occurrence placement through the document, density-0 reporting and report formats.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "part/MassProperties.h"
#include "part/PartModule.h"
#include "part/features/Extrude.h"
#include "support/TestSketch.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <QtTest/QtTest>
#include <gp_Ax2.hxx>

#include <cmath>

using namespace fabricad;
using namespace fabricad::part;
using namespace fabricad::part::test;

namespace {
bool near(double a, double b, double rel = 1e-6) { return std::abs(a - b) <= rel * std::max(1.0, std::abs(b)); }

/// Box l x w x h (mm) with its min corner at (x, y, z).
TopoDS_Shape box(double x, double y, double z, double l, double w, double h) {
  return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), l, w, h).Shape();
}

bool positiveDefinite(const std::array<double, 6>& t) {
  // Sylvester's criterion on the symmetric 3x3 matrix
  const double a = tensorAt(t, 0, 0), b = tensorAt(t, 0, 1), c = tensorAt(t, 0, 2);
  const double e = tensorAt(t, 1, 1), f = tensorAt(t, 1, 2), i = tensorAt(t, 2, 2);
  const double d1 = a, d2 = a * e - b * b, d3 = a * (e * i - f * f) - b * (b * i - f * c) + c * (b * f - e * c);
  return d1 > 0 && d2 > 0 && d3 > 0;
}
}  // namespace

class TestMassProperties : public QObject {
  Q_OBJECT
 private slots:
  void steelBoxClosedForm() {
    // 10 x 20 x 30 mm steel (7.85 g/cm3): V = 6000 mm3, m = 47.1 g
    MassProperties p = computeMassProperties({{box(0, 0, 0, 10, 20, 30), 7.85, "Box"}});
    QVERIFY(near(p.volume, 6000.0));
    QVERIFY(near(p.mass, 47.1));
    QVERIFY(near(p.area, 2 * (10 * 20 + 10 * 30 + 20 * 30)));
    QVERIFY(near(p.cog.x, 5) && near(p.cog.y, 10) && near(p.cog.z, 15));
    const double m = 47.1;
    const double Ixx = m / 12 * (20 * 20 + 30 * 30), Iyy = m / 12 * (10 * 10 + 30 * 30), Izz = m / 12 * (10 * 10 + 20 * 20);
    QVERIFY2(near(p.inertiaCog[0], Ixx, 1e-6), qPrintable(QString::number(p.inertiaCog[0])));
    QVERIFY(near(p.inertiaCog[1], Iyy, 1e-6));
    QVERIFY(near(p.inertiaCog[2], Izz, 1e-6));
    QVERIFY(std::abs(p.inertiaCog[3]) < 1e-6 * Ixx && std::abs(p.inertiaCog[4]) < 1e-6 * Ixx);
    // about the origin: parallel axis, products of inertia -m*cx*cy (tensor convention)
    QVERIFY(near(p.inertiaOrigin[0], Ixx + m * (10 * 10 + 15 * 15)));
    QVERIFY(near(p.inertiaOrigin[3], -m * 5 * 10));
    QVERIFY(near(p.inertiaOrigin[4], -m * 5 * 15));
    QVERIFY(near(p.inertiaOrigin[5], -m * 10 * 15));
    QVERIFY(positiveDefinite(p.inertiaCog));
    QVERIFY(positiveDefinite(p.inertiaOrigin));
    // principal moments sorted ascending, axes unit length
    QVERIFY(p.principalMoments[0] <= p.principalMoments[1] && p.principalMoments[1] <= p.principalMoments[2]);
    QVERIFY(near(p.principalMoments[0], Izz, 1e-6));
    QVERIFY(near(p.principalMoments[2], Ixx, 1e-6));
    QVERIFY(near(std::abs(p.principalAxes[2].x), 1.0, 1e-6));
    QVERIFY(near(p.radiusOfGyration[0], std::sqrt(Izz / m)));
    QVERIFY(p.massBased);
    QCOMPARE(p.bodiesWithoutDensity, 0);
  }

  void cylinderAndTwoBodies() {
    // aluminium cylinder R5 H40 along Z plus a steel box: mass-weighted CoG
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, 0, 0), gp_Dir(0, 0, 1)), 5, 40).Shape();
    MassProperties pc = computeMassProperties({{cyl, 2.70, "Cyl"}});
    const double v = M_PI * 25 * 40, m = v * 2.70e-3;
    QVERIFY(near(pc.volume, v, 1e-6));
    QVERIFY(near(pc.inertiaCog[2], 0.5 * m * 25, 1e-5));
    QVERIFY(near(pc.inertiaCog[0], m / 12 * (3 * 25 + 40 * 40), 1e-5));
    MassProperties p = computeMassProperties({{box(0, 0, 0, 10, 20, 30), 7.85, "Box"}, {cyl, 2.70, "Cyl"}});
    const double mt = 47.1 + m;
    QVERIFY(near(p.mass, mt, 1e-6));
    QVERIFY(near(p.cog.x, (47.1 * 5 + m * 100) / mt, 1e-6));
    QVERIFY(near(p.cog.z, (47.1 * 15 + m * 20) / mt, 1e-6));
    QVERIFY(positiveDefinite(p.inertiaCog));
    QVERIFY(std::abs(tensorAt(p.inertiaCog, 0, 1) - tensorAt(p.inertiaCog, 1, 0)) < 1e-12);
  }

  void densityZeroBodies() {
    MassProperties p = computeMassProperties({{box(0, 0, 0, 10, 10, 10), 1.0, "A"}, {box(50, 0, 0, 10, 10, 10), 0.0, "B"}});
    QCOMPARE(p.bodies, 2);
    QCOMPARE(p.bodiesWithoutDensity, 1);
    QVERIFY(near(p.volume, 2000));
    QVERIFY(near(p.mass, 1.0));
    QVERIFY(near(p.cog.x, 5));  // B has no mass
    QVERIFY(QString::fromStdString(massPropertiesMarkdown(p)).contains("1 bodies without density"));
    // no density anywhere: volume centroid, mass 0
    MassProperties q = computeMassProperties({{box(0, 0, 0, 10, 10, 10), 0, "A"}, {box(50, 0, 0, 10, 10, 10), 0, "B"}});
    QVERIFY(!q.massBased);
    QCOMPARE(q.mass, 0.0);
    QVERIFY(near(q.cog.x, 30));
  }

  void documentOccurrences() {
    core::ToolRegistry registry;
    core::Document doc;
    core::ModuleContext ctx{core::FeatureFactory::instance(), registry};
    part::registerModule(ctx);
    registerTestSketch();
    // body in a child component placed at x = 100
    core::Component& comp = doc.addComponent("Plate");
    const core::ObjectId compId = comp.id;
    gp_Trsf t;
    t.SetTranslation(gp_Vec(100, 0, 0));
    comp.placement = t;
    doc.setActiveComponent(compId);
    TestSketch* sk = addRectSketch(doc, "XY", 0, 0, 20, 10);
    auto ex = std::make_unique<ExtrudeFeature>();
    ex->prop("profiles").addRef(sk->rectProfile(0));
    ex->prop("distance").setDouble(5);
    ex->prop("operation").setEnum("New Body");
    core::Feature* f = doc.addFeature(std::move(ex));
    doc.recompute();
    QVERIFY(!f->createdBodies().empty());
    const core::ObjectId bodyId = f->createdBodies().front();
    core::Body* b = doc.body(bodyId);
    QVERIFY(b);
    QCOMPARE(b->component, compId);
    b->density = 2.70;
    std::vector<MassItem> items = massItemsFor(doc, {});
    QCOMPARE(items.size(), size_t(1));
    MassProperties p = computeMassProperties(items);
    QVERIFY(near(p.volume, 1000));
    QVERIFY(near(p.mass, 2.7));
    QVERIFY(near(p.cog.x, 110));
    // an occurrence of the plate at y = 50 doubles the mass
    core::Component occ;
    occ.name = "Plate";
    occ.definition = compId;
    occ.parent = core::kRootComponentId;
    gp_Trsf t2;
    t2.SetTranslation(gp_Vec(0, 50, 0));
    occ.placement = t2;
    doc.addComponent(occ);
    p = computeMassProperties(massItemsFor(doc, {}));
    QVERIFY(near(p.mass, 5.4));
    QVERIFY(near(p.cog.y, 5 + 25));  // (5 + 55) / 2
    QVERIFY(near(p.cog.x, 60));      // (110 + 10) / 2
  }

  void reports() {
    MassProperties p = computeMassProperties({{box(0, 0, 0, 10, 20, 30), 7.85, "Box"}});
    const QString csv = QString::fromStdString(massPropertiesCsv(p));
    QVERIFY(csv.startsWith("Property,Value,Unit\n"));
    QVERIFY(csv.contains("Mass,47.100000,g"));
    QVERIFY(csv.contains("\"5.000000, 10.000000, 15.000000\""));
    const QString md = QString::fromStdString(massPropertiesMarkdown(p));
    QVERIFY(md.contains("| Mass | 47.100 | g |"));
    const QString tsv = QString::fromStdString(massPropertiesText(p, true));
    QVERIFY(tsv.contains("Mass\t0.104\tlb"));
  }
};

QTEST_GUILESS_MAIN(TestMassProperties)
#include "test_mass_properties.moc"
