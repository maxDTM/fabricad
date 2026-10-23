// FabriCAD drawing: 1:1 DXF of a planar face, several coplanar faces or a sketch, for laser /
// waterjet / router cutting (production-features.md P3).
//
// Each wire becomes one closed LWPOLYLINE (lines and arcs as bulges) when it has no free-form
// edges, a CIRCLE when it is one full circle, else LINE / ARC / SPLINE entities. Layers: OUTER
// (outer contours), INNER (holes and cut-outs), ETCH (sketch construction geometry) and BEND (a
// placeholder for sheet metal). The face is seen from outside (its normal towards the viewer);
// the origin is the lower-left corner of its bounding box (faces) or the sketch origin. Units
// mm or inches ($INSUNITS in R2000). Free-form edges are written as SPLINE (R2000) or, with an
// arc tolerance, as a polyline of tangent-ish arcs within the tolerance (controllers that do
// not read splines); R12 has no SPLINE, so it always gets arcs (or a fine polyline).
//
// Lives in the drawing module because it writes through DxfWriter (part must not depend on
// drawing); the Design-workspace tool is in drawing/tools/ExportFaceDxfTool.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/DxfWriter.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>

#include <string>
#include <vector>

namespace fabricad::drawing {

struct FaceDxfOptions {
  DxfWriter::Version version = DxfWriter::Version::R2000;
  bool inches = false;          ///< write inches (coordinates / 25.4, $INSUNITS 1)
  double arcTolerance = 0;      ///< > 0: free-form edges as arcs within this distance (mm)
  bool etch = true;             ///< sketch construction geometry on ETCH
};

struct FaceDxfResult {
  bool ok = false;
  std::string error;
  std::string dxf;
  int outer = 0, inner = 0, etch = 0;  ///< contours / entities per layer
  int splines = 0;                     ///< SPLINE entities written
  double width = 0, height = 0;        ///< bounding box in output units
};

/// Faces must be planar and coplanar (same plane, same side).
FaceDxfResult faceDxf(const std::vector<TopoDS_Face>& faces, const FaceDxfOptions& options);
/// Sketch edges (world) on `plane`; closed loops nested an odd number of times are INNER.
FaceDxfResult sketchDxf(const gp_Ax3& plane, const TopoDS_Shape& edges, const TopoDS_Shape& constructionEdges,
                        const FaceDxfOptions& options);
/// Option words of EXPORTDXF (design workspace): r12, r2000, units=mm|in, arcs=0.01, noetch.
bool applyFaceDxfOption(const std::string& word, FaceDxfOptions& options);
/// Preference "export/dxf/version" ("R2000" default or "R12").
DxfWriter::Version faceDxfVersionDefault();
void setFaceDxfVersionDefault(DxfWriter::Version v);

}  // namespace fabricad::drawing
