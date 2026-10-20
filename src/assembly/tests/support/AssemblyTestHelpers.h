// Shared helpers for the assembly module tests: a two-component fixture and face lookup.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/ShapeRef.h"
#include "core/ShapeUtils.h"
#include "core/tests/support/TestFeatures.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <memory>

namespace fabricad::assembly::test {

struct TwoBoxes {
  core::ObjectId compA = 0, compB = 0, bodyA = 0, bodyB = 0;
};

/// Two components ("Base", grounded, at the origin; "Top" at (40,0,0)) with one 10x10x10 box each.
/// Bodies take the component that is active when they are first computed, so each box is
/// recomputed right after it is added (as the app does).
inline TwoBoxes makeTwoBoxes(core::Document& doc) {
  TwoBoxes t;
  auto addBox = [&](const char* compName, core::ObjectId& comp, core::ObjectId& body) {
    comp = doc.addComponent(compName).id;
    doc.setActiveComponent(comp);
    auto f = std::make_unique<core::test::BoxFeature>();
    f->prop("dx").setDouble(10);
    f->prop("dy").setDouble(10);
    f->prop("dz").setDouble(10);
    core::Feature* raw = doc.addFeature(std::move(f));
    doc.recompute();
    body = raw->createdBodies().front();
  };
  addBox("Base", t.compA, t.bodyA);
  addBox("Top", t.compB, t.bodyB);
  gp_Trsf tr;
  tr.SetTranslation(gp_Vec(40, 0, 0));
  doc.component(t.compB)->placement = tr;
  doc.component(t.compA)->grounded = true;
  doc.setActiveComponent(core::kRootComponentId);
  doc.recompute();
  return t;
}

/// Face of a body (component-local shape) whose plane normal is `n`; invalid ref if none.
inline core::ShapeRef faceWithNormal(const core::Document& doc, core::ObjectId body, const gp_Dir& n) {
  const TopoDS_Shape& shape = doc.bodyShape(body);
  for (const TopoDS_Shape& f : core::subShapes(shape, TopAbs_FACE)) {
    auto pl = core::facePlane(f);
    if (pl && pl->Direction().IsEqual(n, 1e-6)) return core::ShapeRef::subShape(body, shape, f);
  }
  return {};
}

inline gp_Pnt worldOrigin(const core::Document& doc, core::ObjectId comp) {
  return gp_Pnt(0, 0, 0).Transformed(doc.worldPlacement(comp));
}

inline bool near(const gp_Pnt& a, const gp_Pnt& b, double tol = 1e-6) { return a.Distance(b) < tol; }
inline bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

}  // namespace fabricad::assembly::test
