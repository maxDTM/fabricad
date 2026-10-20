// FabriCAD drawing: section cutting for section views (worker-thread safe, no Document).
// Material on the observer side of the plane is removed; the faces lying in the cutting
// plane become hatch regions. See docs/research/algorithms.md section 3.3.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/Geometry2D.h"
#include "drawing/Projection.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>

#include <string>
#include <vector>

namespace fabricad::drawing {

struct SectionInput {
  std::vector<SourceShape> sources;  ///< world-placed solids
  gp_Ax2 frame;                      ///< section view frame; Location() lies on the cut plane,
                                     ///< Direction() points towards the viewer (removed side)
  double hatchSpacing = 3.0;         ///< view-local (model) mm between hatch lines
  double hatchAngle = 45.0;
  bool hatch = true;
};

struct SectionOutput {
  bool ok = false;
  std::string error;
  std::vector<SourceShape> cutSources;              ///< remaining material, same body ids
  std::vector<std::vector<Point2>> loops;           ///< cut-face boundaries (view-local)
  std::vector<Curve2D> hatch;                       ///< hatch segments (view-local, LineClass::Hatch)
  std::vector<Curve2D> outlines;                    ///< cut-face boundaries as curves (LineClass::Section)
};

/// Cut the sources with the half-space in front of the frame's plane and compute hatch loops.
SectionOutput computeSection(const SectionInput& in);

/// Polygonise the wires of a planar face into the frame's 2D coordinates (outer + holes).
std::vector<std::vector<Point2>> faceLoops(const TopoDS_Shape& face, const gp_Ax2& frame, double deflection);

}  // namespace fabricad::drawing
