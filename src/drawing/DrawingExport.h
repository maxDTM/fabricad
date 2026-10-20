// FabriCAD drawing: PDF / SVG / DXF / CSV export of sheets. All functions are headless
// (QPainter on QPdfWriter / QSvgGenerator; DXF written by DxfWriter).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

class QPainter;

namespace fabricad::drawing {

class DrawingModel;
struct Sheet;

struct ExportOptions {
  std::vector<core::ObjectId> sheets;  ///< empty = all sheets
  bool frame = true;                   ///< include border + title block
  int pdfResolution = 1200;            ///< dpi used for PDF line precision
};

/// Multi-page PDF (one page per sheet, page size = sheet format).
bool exportPdf(DrawingModel& model, const std::string& path, const ExportOptions& opts, std::string* error = nullptr);
/// One SVG file for one sheet (viewBox in mm).
bool exportSvg(DrawingModel& model, const Sheet& sheet, const std::string& path, const ExportOptions& opts,
               std::string* error = nullptr);
/// DXF R12 of one sheet.
bool exportDxf(DrawingModel& model, const Sheet& sheet, const std::string& path, const ExportOptions& opts,
               std::string* error = nullptr);
/// DXF text of one sheet (for tests).
std::string dxfText(DrawingModel& model, const Sheet& sheet, const ExportOptions& opts);
/// Parts list rows as CSV (first parts list of the sheet, or the document BOM).
bool exportPartsListCsv(DrawingModel& model, const Sheet& sheet, const std::string& path, std::string* error = nullptr);
std::string partsListCsv(DrawingModel& model, const Sheet& sheet);

/// Paint a whole sheet into a painter whose unit is millimetres.
void paintSheet(QPainter& painter, DrawingModel& model, const Sheet& sheet, bool frame);

}  // namespace fabricad::drawing
