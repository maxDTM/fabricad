// Unit tests for JointTypes and AssemblySolver (pure math, headless).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblySolver.h"
#include "assembly/JointTypes.h"

#include <QtTest/QtTest>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <numbers>

using namespace fabricad::assembly;
using fabricad::core::ObjectId;

namespace {

constexpr ObjectId kG = 1, kA = 20, kB = 21, kC = 22;

SolverComponent comp(ObjectId id, ObjectId parent = 1, bool grounded = false, gp_Trsf world = {}) {
  SolverComponent c;
  c.id = id;
  c.parent = parent;
  c.grounded = grounded;
  c.world = world;
  return c;
}

SolverComponent root() { return comp(kG, fabricad::core::kInvalidId, true); }

gp_Trsf translate(double x, double y, double z) {
  gp_Trsf t;
  t.SetTranslation(gp_Vec(x, y, z));
  return t;
}

gp_Trsf rotZ(double deg) {
  gp_Trsf t;
  t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), deg * std::numbers::pi / 180.0);
  return t;
}

bool near(const gp_Pnt& a, const gp_Pnt& b, double tol = 1e-6) { return a.Distance(b) < tol; }
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

}  // namespace

class AssemblySolverTest : public QObject {
  Q_OBJECT
 private slots:
  void motionRoundTrip() {
    for (int t = 0; t < kJointTypeCount; ++t) {
      JointType type = static_cast<JointType>(t);
      JointValues q{37.0, 4.5, -12.0};
      if (type == JointType::Slider || type == JointType::Cylindrical) q = {type == JointType::Slider ? 4.5 : 37.0, 4.5, 0};
      if (type == JointType::Rigid) q = {0, 0, 0};
      if (type == JointType::Planar) q = {4.5, -2.0, 37.0};
      if (type == JointType::Ball) q = {20.0, -15.0, 40.0};
      gp_Trsf m = jointMotion(type, q);
      JointValues back = jointValuesFromMotion(type, m);
      for (int i = 0; i < jointDofCount(type); ++i)
        QVERIFY2(near(q[i], back[i], 1e-6), qPrintable(QString("type %1 dof %2: %3 vs %4").arg(t).arg(i).arg(q[i]).arg(back[i])));
    }
    QCOMPARE(jointDofCount(JointType::Rigid), 0);
    QCOMPARE(jointDofCount(JointType::Planar), 3);
    QCOMPARE(std::string(jointTypeName(JointType::PinSlot)), std::string("Pin-slot"));
    QCOMPARE(jointTypeFromName("Slider"), JointType::Slider);
  }

  void pinSlotSlideIsAlongSlot() {
    gp_Trsf m = jointMotion(JointType::PinSlot, {90.0, 5.0, 0});
    gp_Pnt o = gp_Pnt(0, 0, 0).Transformed(m);
    QVERIFY(near(o, gp_Pnt(5, 0, 0)));  // the pin sits in the slot regardless of its spin
  }

  void rigidFaceMate() {
    // G: box 10x10x10 at origin; A: another box whose bottom face mates onto G's top face.
    SolverJoint j;
    j.id = 100;
    j.compA = kA;
    j.compB = kG;
    j.frameB = gp_Ax3(gp_Pnt(5, 5, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));   // top face of G
    j.frameA = gp_Ax3(gp_Pnt(5, 5, 0), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0));   // bottom face of A (outward normal)
    j.type = JointType::Rigid;
    SolverResult r = AssemblySolver::solve({root(), comp(kA, kG, false, translate(40, 0, 0))}, {j});
    QVERIFY(r.ok());
    QVERIFY(r.moved.count(kA));
    gp_Trsf wa = r.world.at(kA);
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(wa), gp_Pnt(0, 0, 10)));
    QVERIFY(near(gp_Pnt(10, 10, 10).Transformed(wa), gp_Pnt(10, 10, 20)));
    QVERIFY(near(AssemblySolver::residualNorm(j, wa, gp_Trsf()), 0.0, 1e-9));
    QCOMPARE(r.jointStatus.at(100), JointSolveStatus::Ok);
  }

  void flipAlignsNormals() {
    SolverJoint j;
    j.compA = kA;
    j.compB = kG;
    j.frameB = gp_Ax3(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j.frameA = gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0));
    j.flip = true;  // Z axes aligned: A is upside down
    SolverResult r = AssemblySolver::solve({root(), comp(kA)}, {j});
    gp_Pnt up = gp_Pnt(0, 0, 5).Transformed(r.world.at(kA));
    QVERIFY(near(up, gp_Pnt(0, 0, 5)));  // A's +Z points down after flip, so its (0,0,5) ends at z=5
  }

  void revoluteDriveAndOffset() {
    SolverJoint j;
    j.id = 7;
    j.compA = kA;
    j.compB = kG;
    j.frameB = gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j.frameA = gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j.flip = true;
    j.type = JointType::Revolute;
    j.q = {90.0, 0, 0};
    j.offset = translate(0, 0, 3);
    SolverResult r = AssemblySolver::solve({root(), comp(kA)}, {j});
    gp_Pnt p = gp_Pnt(1, 0, 0).Transformed(r.world.at(kA));
    QVERIFY(near(p, gp_Pnt(0, 1, 3)));
    JointValues v = r.jointValues.at(7);
    QVERIFY(near(v[0], 90.0));
    // currentValues inverts placeA
    JointValues cv = AssemblySolver::currentValues(j, r.world.at(kA), gp_Trsf());
    QVERIFY(near(cv[0], 90.0));
  }

  void chainAndBackwardTraversal() {
    // G -(rigid)- A -(slider q=5)- B ; joint 2 is stored with A as compA (moving side) and B as compB,
    // so the tree has to traverse it backwards.
    SolverJoint j1;
    j1.id = 1;
    j1.compA = kA;
    j1.compB = kG;
    j1.frameB = gp_Ax3(gp_Pnt(10, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j1.frameA = gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j1.flip = true;
    SolverJoint j2;
    j2.id = 2;
    j2.compA = kA;
    j2.compB = kB;
    j2.type = JointType::Slider;
    j2.frameA = gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j2.frameB = gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    j2.flip = true;
    j2.q = {5.0, 0, 0};
    SolverResult r = AssemblySolver::solve({root(), comp(kA), comp(kB)}, {j1, j2});
    QVERIFY(r.ok());
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kA)), gp_Pnt(10, 0, 0)));
    // A = B * TransZ(5)  ->  B = A * TransZ(-5)
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kB)), gp_Pnt(10, 0, -5)));
    QVERIFY(near(AssemblySolver::residualNorm(j2, r.world.at(kA), r.world.at(kB)), 0.0, 1e-9));
    QVERIFY(!r.usedNumeric);
  }

  void floatingGroupKeepsAnchor() {
    SolverJoint j;
    j.id = 3;
    j.compA = kA;
    j.compB = kB;
    j.flip = true;
    SolverResult r = AssemblySolver::solve({root(), comp(kA), comp(kB, kG, false, translate(0, 7, 0))}, {j});
    QVERIFY(r.ok());
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kB)), gp_Pnt(0, 7, 0)));  // B stays
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kA)), gp_Pnt(0, 7, 0)));  // A snaps onto B
    QVERIFY(!r.moved.count(kB));
  }

  void consistentLoopIsNotNumeric() {
    // Triangle of rigid joints where the third one agrees with the first two.
    SolverJoint ga;  // A at (10,0,0)
    ga.id = 1; ga.compA = kA; ga.compB = kG; ga.flip = true;
    ga.frameB = gp_Ax3(gp_Pnt(10, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverJoint ab;  // B at A + (0,10,0)
    ab.id = 2; ab.compA = kB; ab.compB = kA; ab.flip = true;
    ab.frameB = gp_Ax3(gp_Pnt(0, 10, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverJoint bg;  // B at (10,10,0) from ground
    bg.id = 3; bg.compA = kB; bg.compB = kG; bg.flip = true;
    bg.frameB = gp_Ax3(gp_Pnt(10, 10, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverResult r = AssemblySolver::solve({root(), comp(kA), comp(kB)}, {ga, ab, bg});
    QVERIFY(r.ok());
    QVERIFY(!r.usedNumeric);
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kB)), gp_Pnt(10, 10, 0)));
  }

  void inconsistentRigidLoopIsConflict() {
    SolverJoint ga;
    ga.id = 1; ga.compA = kA; ga.compB = kG; ga.flip = true;
    ga.frameB = gp_Ax3(gp_Pnt(10, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverJoint ag;  // contradicts: A at (12,0,0)
    ag.id = 2; ag.compA = kA; ag.compB = kG; ag.flip = true;
    ag.frameB = gp_Ax3(gp_Pnt(12, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverResult r = AssemblySolver::solve({root(), comp(kA)}, {ga, ag});
    QVERIFY(!r.ok());
    QVERIFY(r.usedNumeric);
    QVERIFY(r.jointStatus.at(1) == JointSolveStatus::Conflict || r.jointStatus.at(2) == JointSolveStatus::Conflict);
  }

  void fourBarLoopSolvesNumerically() {
    // Four-bar linkage: ground pins at (0,0,0) and (15,0,0); crank A (10), coupler B (12), rocker C (10).
    // Tree propagation with q=0 lays all links flat along X and the closing revolute at the rocker's
    // ground pin is violated by 3 mm; the numeric phase must rotate the links to close the loop.
    SolverJoint ga;
    ga.id = 1; ga.compA = kA; ga.compB = kG; ga.flip = true; ga.type = JointType::Revolute;
    SolverJoint ab;
    ab.id = 2; ab.compA = kB; ab.compB = kA; ab.flip = true; ab.type = JointType::Revolute;
    ab.frameB = gp_Ax3(gp_Pnt(10, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverJoint bc;  // rocker tip (C-local (10,0,0)) on coupler end (B-local (12,0,0))
    bc.id = 3; bc.compA = kC; bc.compB = kB; bc.flip = true; bc.type = JointType::Revolute;
    bc.frameA = gp_Ax3(gp_Pnt(10, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    bc.frameB = gp_Ax3(gp_Pnt(12, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    SolverJoint cg;  // rocker root on the second ground pin
    cg.id = 4; cg.compA = kC; cg.compB = kG; cg.flip = true; cg.type = JointType::Revolute;
    cg.frameB = gp_Ax3(gp_Pnt(15, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    auto check = [&](const SolverResult& r) {
      gp_Pnt aEnd = gp_Pnt(10, 0, 0).Transformed(r.world.at(kA));
      gp_Pnt bStart = gp_Pnt(0, 0, 0).Transformed(r.world.at(kB));
      gp_Pnt bEnd = gp_Pnt(12, 0, 0).Transformed(r.world.at(kB));
      gp_Pnt cTip = gp_Pnt(10, 0, 0).Transformed(r.world.at(kC));
      gp_Pnt cRoot = gp_Pnt(0, 0, 0).Transformed(r.world.at(kC));
      QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kA)), gp_Pnt(0, 0, 0), 1e-3));
      QVERIFY(near(aEnd, bStart, 1e-3));
      QVERIFY(near(bEnd, cTip, 1e-3));
      QVERIFY(near(cRoot, gp_Pnt(15, 0, 0), 1e-3));
    };
    SolverResult r = AssemblySolver::solve({root(), comp(kA), comp(kB), comp(kC)}, {ga, ab, bc, cg});
    QVERIFY2(r.ok(), qPrintable(QString("maxResidual %1").arg(r.maxResidual)));
    QVERIFY(r.usedNumeric);
    check(r);
    QCOMPARE(r.jointStatus.at(4), JointSolveStatus::Numeric);
    // driving the crank (locked motion) to 60 degrees moves coupler and rocker accordingly
    ga.q = {60.0, 0, 0};
    ga.lockMotion = true;
    SolverResult r2 = AssemblySolver::solve({root(), comp(kA, kG, false, r.world.at(kA)), comp(kB, kG, false, r.world.at(kB)),
                                             comp(kC, kG, false, r.world.at(kC))},
                                            {ga, ab, bc, cg});
    QVERIFY2(r2.ok(), qPrintable(QString("maxResidual %1").arg(r2.maxResidual)));
    QVERIFY2(near(r2.jointValues.at(1)[0], 60.0, 1e-3), qPrintable(QString("crank value %1, status %2, residual %3").arg(r2.jointValues.at(1)[0]).arg(jointSolveStatusName(r2.jointStatus.at(1))).arg(r2.maxResidual)));
    check(r2);
    QVERIFY(near(gp_Pnt(10, 0, 0).Transformed(r2.world.at(kA)), gp_Pnt(5, 10 * std::sin(std::numbers::pi / 3), 0), 1e-3));
    QVERIFY2(r.freeDof >= 1, qPrintable(QString::number(r.freeDof)));   // undriven: the crank angle is free
    QCOMPARE(r2.freeDof, 0);                                           // driven crank: no mobility left
  }

  void asBuiltKeepsPlacements() {
    gp_Trsf wa = translate(3, 4, 5).Multiplied(rotZ(30));
    gp_Trsf wb = translate(-1, 2, 0);
    SolverJoint j;
    j.compA = kA;
    j.compB = kB;
    j.type = JointType::Revolute;
    AssemblySolver::asBuiltFrames(j, wa, wb, gp_Ax3(gp_Pnt(1, 1, 1), gp_Dir(0, 1, 0)));
    gp_Trsf placed = AssemblySolver::placeA(wb, j);
    QVERIFY(AssemblySolver::placementsEqual(placed, wa, 1e-9, 1e-9));
    QVERIFY(near(AssemblySolver::residualNorm(j, wa, wb), 0.0, 1e-9));
    // joint axis in world is the picked axis
    gp_Trsf jw = AssemblySolver::jointWorldFrame(wb, j);
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(jw), gp_Pnt(1, 1, 1)));
    gp_Pnt z = gp_Pnt(0, 0, 1).Transformed(jw);
    QVERIFY(near(z, gp_Pnt(1, 2, 1)));
  }

  void suppressedAndUnknownAreSkipped() {
    SolverJoint j;
    j.id = 9;
    j.compA = kA;
    j.compB = kC;  // unknown
    SolverJoint k;
    k.id = 10;
    k.compA = kA;
    k.compB = kG;
    k.enabled = false;
    SolverResult r = AssemblySolver::solve({root(), comp(kA, kG, false, translate(1, 2, 3))}, {j, k});
    QCOMPARE(r.jointStatus.at(9), JointSolveStatus::Skipped);
    QCOMPARE(r.jointStatus.at(10), JointSolveStatus::Skipped);
    QVERIFY(near(gp_Pnt(0, 0, 0).Transformed(r.world.at(kA)), gp_Pnt(1, 2, 3)));
    QVERIFY(r.moved.empty());
  }
};

QTEST_GUILESS_MAIN(AssemblySolverTest)
#include "test_assembly_solver.moc"
