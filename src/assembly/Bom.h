// FabriCAD assembly: bill of materials for the drawing module's parts list and for export.
// Occurrences sharing a definition (or an external file) are grouped into one row with a
// quantity; the root component appears as a row when it owns bodies itself.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace fabricad::assembly {

struct BomRow {
  int item = 0;                               ///< 1-based item number (balloon number)
  core::ObjectId definition = core::kInvalidId;  ///< component the row describes (definition)
  std::vector<core::ObjectId> occurrences;    ///< all instances counted in `quantity`
  std::string name;                           ///< component name ("Bracket")
  std::string partNumber;
  std::string description;
  std::string material;                       ///< component material, else the bodies' materials joined by ", "
  int quantity = 0;
  int level = 0;                              ///< tree depth (0 = direct child of the BOM root)
  double volume = 0.0;                        ///< mm^3 of one instance
  double mass = 0.0;                          ///< g of one instance (0 when no body has a density)
  int bodyCount = 0;
};

struct BomTable {
  std::vector<BomRow> rows;
  int totalQuantity = 0;
};

struct BomOptions {
  core::ObjectId root = core::kRootComponentId;
  bool flatten = true;            ///< true: every component in the sub-tree; false: direct children only
  bool includeRootBodies = true;  ///< add a row for bodies owned by `root` itself
  bool includeEmpty = false;      ///< rows for components without bodies (sub-assemblies, empty components)
};

/// Build the BOM of the current document state.
BomTable buildBom(const core::Document& doc, const BomOptions& options = {});
/// CSV (RFC 4180 quoting): Item,Name,Part Number,Description,Material,Qty,Mass [g]
std::string bomToCsv(const BomTable& table);

}  // namespace fabricad::assembly
