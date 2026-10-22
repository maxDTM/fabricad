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
  // Stepped / aligned cuts (SectionLine::points): the cut path in the parent view's local
  // coordinates, the parent's projection frame and the parent-local viewing direction. Empty
  // path = straight cut through frame.Location() (above).
  std::vector<Point2> path;
  gp_Ax2 parentFrame;
  Point2 arrow{0, 1};
  bool aligned = false;
};

struct SectionOutput {
  bool ok = false;
  std::string error;
  std::vector<SourceShape> cutSources;              ///< remaining material, same body ids
  std::vector<std::vector<Point2>> loops;           ///< cut-face boundaries (view-local)
  std::vector<int> loopSource;                      ///< index of the source each loop belongs to
  std::vector<Curve2D> hatch;                       ///< hatch segments (view-local, LineClass::Hatch)
  std::vector<Curve2D> outlines;                    ///< cut-face boundaries as curves (LineClass::Section)
};

/// Hatch angle and spacing for the k-th part of a section (ISO 128-50: adjacent parts are told
/// apart by alternating direction, then by a different pitch). k = 0 gives (angle, spacing).
void hatchVariant(int k, double angle, double spacing, double& angleOut, double& spacingOut);

/// Cut the sources with the half-space in front of the frame's plane and compute hatch loops.
/// With a path of >= 3 points: stepped (offset) section, or aligned section (the arm after the
/// first bend is revolved into the plane of the first arm before projection).
SectionOutput computeSection(const SectionInput& in);

/// Polygonise the wires of a planar face into the frame's 2D coordinates (outer + holes).
std::vector<std::vector<Point2>> faceLoops(const TopoDS_Shape& face, const gp_Ax2& frame, double deflection);

}  // namespace fabricad::drawing
