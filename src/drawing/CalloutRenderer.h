// FabriCAD drawing: display-list layout of the callouts in drawing/Callouts.h (hole callouts,
// feature control frames, datum symbols, surface texture symbols, ordinate dimensions, revision
// tables). Shared by the canvas (one item per entity) and the exporters (SheetRenderer::addSheet).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/Callouts.h"
#include "drawing/SheetRenderer.h"

#include <utility>
#include <vector>

namespace fabricad::drawing {

/// Rendering (sheet mm).
class CalloutRenderer {
 public:
  CalloutRenderer(const DrawingStyle& style, const core::UnitSystem& units) : style_(style), units_(units) {}
  void addHoleCallout(DisplayList& out, const Sheet& sheet, const HoleCallout& c) const;
  void addFeatureFrame(DisplayList& out, const Sheet& sheet, const FeatureControlFrame& f) const;
  void addDatum(DisplayList& out, const Sheet& sheet, const DatumFeature& d) const;
  void addSurfaceFinish(DisplayList& out, const Sheet& sheet, const SurfaceFinish& s) const;
  void addOrdinate(DisplayList& out, const Sheet& sheet, const OrdinateDimension& o) const;
  void addRevisionTable(DisplayList& out, const RevisionTable& t) const;
  void addHoleTable(DisplayList& out, const HoleTable& t, const core::UnitSystem& units) const;
  /// Every callout of a sheet, one display list per entity (canvas items, hit testing).
  std::vector<std::pair<core::ObjectId, DisplayList>> entityLists(const Sheet& sheet) const;
  /// Everything appended to one list (export).
  void addAll(DisplayList& out, const Sheet& sheet) const;

  /// Formatted ordinate value text.
  std::string ordinateText(double value, int precision) const;
  /// Frame geometry helpers (exposed for tests): total width of a frame row.
  double frameRowWidth(const FcfRow& row) const;

 private:
  void leaderArrow(DisplayList& out, const Point2& from, const Point2& tip) const;
  DrawingStyle style_;
  core::UnitSystem units_;
};

}  // namespace fabricad::drawing
