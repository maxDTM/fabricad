// FabriCAD sketch: PlaneGCS wrapper. Builds a GCS::System from a Sketch, solves and diagnoses.
// Only SketchSolver.cpp includes the PlaneGCS headers.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/Sketch.h"

#include <optional>
#include <vector>

namespace fabricad::sketch {

/// A temporary pull of a point towards a target (dragging).
struct DragTarget {
  GeoRef ref;    ///< point of a geometry (pos != None) or the whole geometry (pos == None, translated)
  Pt2 target;    ///< where the referenced point should go
  Pt2 delta;     ///< for whole-geometry drags: translation to apply to all its points
};

class SketchSolver {
 public:
  /// Solve the sketch in place. With `diagnose` the DOF / conflict analysis is run
  /// (slower; skip while dragging). `drag` adds low-priority temporary constraints.
  static SolveResult solve(Sketch& sketch, bool diagnose, const DragTarget* drag = nullptr);
};

}  // namespace fabricad::sketch
