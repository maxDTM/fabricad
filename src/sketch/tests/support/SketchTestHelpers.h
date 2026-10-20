// Shared helpers for sketch module tests.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/Sketch.h"

#include <cmath>

namespace fabricad::sketch::test {

inline bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
inline bool near(Pt2 a, Pt2 b, double tol = 1e-6) { return a.distance(b) <= tol; }

/// Axis-aligned rectangle with coincident corners + horizontal/vertical constraints.
/// Returns the four line ids (bottom, right, top, left).
inline std::vector<int> addRectangle(Sketch& sk, Pt2 a, Pt2 b, bool withHV = true) {
  Pt2 p0 = a, p1{b.x, a.y}, p2 = b, p3{a.x, b.y};
  int l0 = sk.addGeometry(Geometry::makeLine(p0, p1));
  int l1 = sk.addGeometry(Geometry::makeLine(p1, p2));
  int l2 = sk.addGeometry(Geometry::makeLine(p2, p3));
  int l3 = sk.addGeometry(Geometry::makeLine(p3, p0));
  int ids[4] = {l0, l1, l2, l3};
  for (int i = 0; i < 4; ++i)
    sk.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {ids[i], PointPos::End}, .b = {ids[(i + 1) % 4], PointPos::Start}});
  if (withHV) {
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l0}});
    sk.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l2}});
    sk.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l1}});
    sk.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l3}});
  }
  return {l0, l1, l2, l3};
}

}  // namespace fabricad::sketch::test
