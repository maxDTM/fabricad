// FabriCAD sketch: sketch text. The font outline of a Text geometry (QPainterPath glyph
// contours: lines and cubic Beziers) in sketch coordinates. The text height is the cap height:
// the outline is scaled so that a capital "H" of the font is exactly `height` tall, which keeps
// sizes identical across fonts and machines. Anchor = left end of the baseline.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/SketchGeometry.h"

#include <string>
#include <vector>

namespace fabricad::sketch {

/// One outline segment: a line (p0 -> p3) or a cubic Bezier (p0, p1, p2, p3).
struct TextSegment {
  bool cubic = false;
  Pt2 p0, p1, p2, p3;
  Pt2 point(double t) const;
};
using TextContour = std::vector<TextSegment>;  ///< closed loop (last p3 == first p0)

/// Default font family of new sketch text.
const char* defaultTextFont();
/// Outline contours of a Text geometry in sketch coordinates (empty without a QGuiApplication
/// or for an empty string). Cached per (text, font, style); thread-safe.
std::vector<TextContour> textContours(const Geometry& g);
/// Polylines of the contours (each closed: first point repeated at the end).
std::vector<std::vector<Pt2>> textPolylines(const Geometry& g, int segmentsPerCurve = 8);
/// Advance width of the text at its height (mm), 0 when unavailable.
double textWidth(const Geometry& g);

}  // namespace fabricad::sketch
