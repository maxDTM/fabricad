// FabriCAD drawing: registration of all drawing-workspace tools plus the export tools.
// Tool ids (all workspace Drawing, tab "DRAWING"):
//   CREATE:     drawing.create, drawing.new-sheet, drawing.base-view, drawing.projected-view (P),
//               drawing.section-view, drawing.detail-view, drawing.move (M), drawing.delete
//   MODIFY:     drawing.edit-view, drawing.edit-sheet, drawing.edit-title-block,
//               drawing.toggle-hidden-lines, drawing.toggle-tangent-edges, drawing.delete-sheet
//   GEOMETRY:   drawing.center-mark, drawing.centerline, drawing.auto-center-marks
//   DIMENSIONS: drawing.dimension (D), drawing.edit-dimension
//   TEXT:       drawing.text (T), drawing.leader (L)
//   TABLES:     drawing.parts-list, drawing.balloon, drawing.balloon-all
//   EXPORT:     drawing.export-pdf, drawing.export-svg, drawing.export-dxf, drawing.export-csv
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/tools/DrawingToolBase.h"

#include <string>

namespace fabricad::core {
class ToolRegistry;
}

namespace fabricad::drawing {

/// Register every drawing tool. Called by registerModule().
void registerDrawingTools(core::ToolRegistry& tools);

/// Instant export tool (PDF/SVG/DXF/CSV). Path from args[0] or a file dialog when a window exists.
class ExportTool : public DrawingToolBase {
  Q_OBJECT
 public:
  enum class Format { Pdf, Svg, Dxf, Csv };
  explicit ExportTool(Format format);
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override;
  /// Export to an explicit path (used by activate and tests). Returns false with an error message.
  bool exportTo(core::ToolContext& ctx, const std::string& path, std::string* error);
  /// File name (no suffix) offered in the save dialog: the document name (or the title block
  /// title of an unnamed one), plus the sheet name for one-sheet formats with several sheets.
  std::string suggestedExportName(core::ToolContext& ctx);

 private:
  Format format_;
};

}  // namespace fabricad::drawing
