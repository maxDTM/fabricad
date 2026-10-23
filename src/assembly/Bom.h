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
  std::string revision;
  std::string vendor;
  double cost = 0.0;                          ///< unit cost (Component::cost), 0 = unknown
  std::string itemPath;                       ///< structured BOM: "1", "2.1", ...; flat: the item number
  core::ObjectId parent = core::kInvalidId;   ///< structured BOM: the parent component of these instances
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
  /// Structured (indented) BOM: rows in tree order, one row per (parent, definition) with the
  /// quantity per parent, sub-assemblies listed with their children below them; item paths
  /// "1", "1.1". False (default): today's flat list grouped by definition over the sub-tree.
  bool structured = false;
};

/// Columns of the CSV / parts list. Script names in parentheses (EXPORTBOM columns=...).
enum class BomColumn {
  Item,         ///< (item)
  Level,        ///< (level) tree depth, 1 = top
  Name,         ///< (name) indented by level in a structured BOM
  PartNumber,   ///< (partNumber) the name when empty, as the on-sheet parts list prints it
  Description,  ///< (description)
  Material,     ///< (material)
  Qty,          ///< (qty)
  Mass,         ///< (mass) g per instance
  MassTotal,    ///< (massTotal) g, mass * qty
  Volume,       ///< (volume) cm^3 per instance
  Revision,     ///< (revision)
  Vendor,       ///< (vendor)
  Cost,         ///< (cost) per instance
  CostTotal,    ///< (costTotal)
};

/// Today's seven columns: Item,Name,Part Number,Description,Material,Qty,Mass [g].
const std::vector<BomColumn>& defaultBomColumns();
std::string bomColumnHeader(BomColumn c);   ///< "Mass total [g]"
std::string bomColumnKey(BomColumn c);      ///< "massTotal"
/// Parse "item,name,qty,massTotal" (case-insensitive keys or headers). Empty `text` = defaults.
bool parseBomColumns(const std::string& text, std::vector<BomColumn>& out, std::string* err = nullptr);
/// One cell as text (CSV-unquoted).
std::string bomCell(const BomRow& row, BomColumn c, bool structured = false);

/// Build the BOM of the current document state.
BomTable buildBom(const core::Document& doc, const BomOptions& options = {});
/// CSV (RFC 4180 quoting): Item,Name,Part Number,Description,Material,Qty,Mass [g]
std::string bomToCsv(const BomTable& table);
/// CSV with the chosen columns; `structured` indents names by level.
std::string bomToCsv(const BomTable& table, const std::vector<BomColumn>& columns, bool structured = false);

}  // namespace fabricad::assembly
