// FabriCAD drawing: Hole Table (drawing.hole-table, HOLETABLE). Lists the holes of one view by
// tag, X / Y from the lower-left corner of the view (the usual hole-table datum), size, callout
// and quantity. The holes are found by
// HoleAnalysis from the view's circles, so the table works for Hole / Pocket Hole features and
// for imported parts alike. Holes with the same callout share a letter (A1, A2, B1 ...).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"
#include "core/Units.h"
#include "drawing/Geometry2D.h"

#include <QJsonObject>

#include <string>
#include <vector>

namespace fabricad::core {
class Document;
}

namespace fabricad::drawing {

struct DrawingView;

struct HoleTableRow {
  std::string tag;         ///< "A1"
  double x = 0, y = 0;     ///< model mm from the lower-left corner of the view
  double diameter = 0;     ///< drill diameter (mm)
  std::string callout;     ///< plain callout text, lines joined with " / "
  int quantity = 0;        ///< holes of this tag letter
};

struct HoleTable {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  Point2 position;  ///< sheet mm, top-left corner
  std::vector<std::string> columns{"Tag", "X", "Y", "Size", "Description", "Qty"};
  std::vector<double> columnWidths{12, 20, 20, 16, 64, 10};
  double rowHeight = 6.0;
  double textHeight = 2.5;
  // cached (refreshCallouts; not persisted)
  std::vector<HoleTableRow> rows;
  bool orphaned = false;
  double width() const;
  double height() const;
  QJsonObject toJson() const;
  static HoleTable fromJson(const QJsonObject& o);
};

/// Rows for the holes visible as full circles in the view (concentric circles count once).
std::vector<HoleTableRow> holeTableRows(const core::Document& doc, const DrawingView& view, const core::UnitSystem& units);
/// Cell texts of a row (Tag, X, Y, Size, Description, Qty) in the document's units.
std::vector<std::string> holeTableCells(const HoleTableRow& row, const core::UnitSystem& units, int precision = -1);

}  // namespace fabricad::drawing
