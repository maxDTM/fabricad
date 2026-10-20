// FabriCAD sketch: entity transforms and pattern / chamfer operations on a Sketch (Fusion 360
// "Move/Copy", "Rectangular Pattern", "Circular Pattern", "Sketch Chamfer"). Pure model code
// (no Qt widgets, no OCCT); the tools in tools/TransformTools.cpp drive it.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/Sketch.h"

#include <optional>
#include <vector>

namespace fabricad::sketch {

/// Similarity transform: rotate by `angle` (radians) and scale by `scale` about `origin`, then
/// translate by `translation`.
struct SketchTransform {
  Pt2 origin;
  double angle = 0;
  double scale = 1;
  Pt2 translation;
  Pt2 apply(Pt2 p) const;
  static SketchTransform translate(Pt2 d) { return {{}, 0, 1, d}; }
  static SketchTransform rotate(Pt2 center, double angle) { return {center, angle, 1, {}}; }
  static SketchTransform scaling(Pt2 base, double factor) { return {base, 0, factor, {}}; }
  bool rotates() const { return std::abs(std::remainder(angle, 2 * kPi)) > 1e-12; }
  bool scales() const { return std::abs(scale - 1) > 1e-12; }
};

/// Move (copy == false) or copy (copy == true) the entities. Moving keeps the constraints among
/// the moved entities and removes those that tie them to entities left behind (Fusion
/// behaviour); rotating drops orientation constraints (horizontal / vertical, X/Y distances,
/// angles to the X axis) of the moved set, scaling multiplies its length dimensions (their
/// expressions are replaced by the scaled value). Copies get copies of the internal constraints.
/// Projected / fixed entities are never moved (but may be copied). Returns the resulting ids
/// (the moved ids, or the ids of the copies in input order).
std::vector<int> transformEntities(Sketch& sk, const std::vector<int>& ids, const SketchTransform& t, bool copy);

struct RectangularPatternSpec {
  Pt2 direction1{1, 0};
  int count1 = 2;          ///< instances along direction 1 (including the original)
  double spacing1 = 10;    ///< distance between instances (mm)
  Pt2 direction2{0, 1};
  int count2 = 1;
  double spacing2 = 10;
  bool symmetric = false;  ///< instances on both sides of the original (count per side)
};
/// Rectangular sketch pattern; returns the ids of the new instances (originals excluded).
std::vector<int> rectangularPattern(Sketch& sk, const std::vector<int>& ids, const RectangularPatternSpec& spec);

struct CircularPatternSpec {
  Pt2 center;
  int count = 6;            ///< instances including the original
  double totalAngle = 360;  ///< degrees; 360 = evenly around the full circle
};
/// Circular sketch pattern; returns the ids of the new instances (originals excluded).
std::vector<int> circularPattern(Sketch& sk, const std::vector<int>& ids, const CircularPatternSpec& spec);

/// Chamfer between two lines that meet (or would meet when extended): both lines are trimmed
/// back from the corner by d1 / d2 (the sides of the pick points are kept) and a chamfer line
/// is inserted with coincident ends. Returns the chamfer line id.
std::optional<int> chamfer(Sketch& sk, int line1, Pt2 near1, int line2, Pt2 near2, double d1, double d2);

}  // namespace fabricad::sketch
