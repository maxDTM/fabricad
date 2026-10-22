// FabriCAD sketch: snapping and auto-constraint inference while drawing (Fusion 360 style).
// Pure 2D logic on the Sketch model; tools turn the result into preview glyphs and constraints.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/Sketch.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fabricad::sketch {

enum class InferenceGlyph { None, Coincident, Midpoint, Center, OnCurve, Horizontal, Vertical, Parallel, Perpendicular, Tangent };
const char* inferenceGlyphText(InferenceGlyph g);

struct InferenceInput {
  Pt2 cursor;
  double tolerance = 1.0;                 ///< mm (pixels * pixelSize)
  std::optional<Pt2> from;                ///< previous point of the segment being drawn
  std::optional<Pt2> referenceDirection;  ///< direction of the previous segment (for parallel/perpendicular)
  std::optional<int> referenceCurve;      ///< id of that previous segment
  std::set<int> exclude;                  ///< geometry ids not to snap to (the entity being drawn)
  bool suppress = false;                  ///< Ctrl held: no snapping
  bool allowPoints = true;
  bool allowCurves = true;
  bool allowAlignment = true;
  bool allowDirections = true;
};

struct InferenceResult {
  Pt2 point;                                   ///< snapped cursor
  std::optional<GeoRef> onPoint;               ///< coincident with this point
  bool onOrigin = false;                       ///< snapped to the sketch origin (0,0)
  std::optional<GeoRef> onCurve;               ///< lies on this curve
  bool horizontal = false;                     ///< segment from->point is horizontal
  bool vertical = false;
  std::optional<int> parallelTo;               ///< segment parallel to this line
  std::optional<int> perpendicularTo;
  std::optional<GeoRef> alignedXWith;          ///< point.x == that point's x (vertical tracking line)
  std::optional<GeoRef> alignedYWith;          ///< point.y == that point's y (horizontal tracking line)
  std::vector<std::pair<Pt2, Pt2>> guides;     ///< dashed tracking lines (sketch coords)
  InferenceGlyph glyph = InferenceGlyph::None;
  bool snapped() const { return onPoint || onOrigin || onCurve || horizontal || vertical || parallelTo || perpendicularTo || alignedXWith || alignedYWith; }
};

InferenceResult infer(const Sketch& sketch, const InferenceInput& in);

/// Name that marks the Fix constraint pinning a point to the sketch origin (drawn as a coincident dot).
inline constexpr const char* kOriginFixName = "origin";
inline bool isOriginFix(const Constraint& c) { return c.type == ConstraintType::Fix && c.name == kOriginFixName; }

/// Constraints an inference result implies for a newly created point / line.
/// `newPoint`: the created point (e.g. line end). `newLine`: the created line (for H/V/parallel), -1 if none.
void applyInference(Sketch& sketch, const InferenceResult& r, const GeoRef& newPoint, int newLine);

}  // namespace fabricad::sketch
