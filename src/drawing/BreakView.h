// FabriCAD drawing: break views (Fusion 360 "Break View"): long parts drawn with a removed
// middle. A break removes a band of the view (an X range for a horizontal break, a Y range for
// a vertical one, in view-local model mm); the geometry beyond it moves next to the band so only
// `gap` sheet mm remain. Dimensions keep measuring the true model geometry (their extension
// lines follow the shortened view). The mapping is piecewise linear and invertible, so
// DrawingView::toSheet()/fromSheet() stay exact inverses.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"

#include <QJsonObject>

#include <string>
#include <vector>

namespace fabricad::drawing {

struct DrawingView;
struct DisplayList;

enum class BreakOrientation {
  Horizontal,  ///< removes an X range (break lines are vertical), for long horizontal parts
  Vertical,    ///< removes a Y range (break lines are horizontal)
};
const char* breakOrientationName(BreakOrientation o);
BreakOrientation breakOrientationFromName(const std::string& s);

enum class BreakStyle { Structural, Curved };  ///< zig-zag lines / wavy lines
const char* breakStyleName(BreakStyle s);
BreakStyle breakStyleFromName(const std::string& s);

struct ViewBreak {
  BreakOrientation orientation = BreakOrientation::Horizontal;
  double from = 0;   ///< view-local start of the removed band (from < to)
  double to = 0;
  QJsonObject toJson() const;
  static ViewBreak fromJson(const QJsonObject& o);
  bool operator==(const ViewBreak&) const = default;
};

/// Map one view-local coordinate across the breaks of that orientation. `gapLocal` is the gap
/// in view-local units (sheet gap / scale). Values inside a band are compressed into the gap.
double breakMap(const std::vector<ViewBreak>& breaks, BreakOrientation o, double v, double gapLocal);
/// Inverse of breakMap.
double breakUnmap(const std::vector<ViewBreak>& breaks, BreakOrientation o, double v, double gapLocal);
/// True when `v` lies strictly inside a removed band.
bool insideBreak(const std::vector<ViewBreak>& breaks, BreakOrientation o, double v);
/// Sorted, merged, non-degenerate breaks (what the mapping uses).
std::vector<ViewBreak> normalizedBreaks(const std::vector<ViewBreak>& breaks);

/// Sheet-space curves of one view curve with the broken bands removed (exact for segments;
/// arcs and polylines crossing a band are approximated by polylines).
std::vector<Curve2D> brokenCurves(const Curve2D& c, const DrawingView& view);
/// Break symbols (both edges of every gap) in sheet space.
void addBreakLines(DisplayList& out, const DrawingView& view);
/// Across-extent (view-local) of the view's geometry on the cut line at `at` (x = at for a
/// horizontal break, y = at for a vertical one). False when no curve crosses it.
bool breakCutExtent(const DrawingView& view, BreakOrientation o, double at, double& lo, double& hi);

}  // namespace fabricad::drawing
