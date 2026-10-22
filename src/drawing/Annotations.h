// FabriCAD drawing: non-dimension annotations: text notes, centre marks/lines, balloons,
// parts lists. All are value types persisted inside their Sheet.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "drawing/Dimension.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"

#include <QJsonObject>

#include <string>
#include <vector>

namespace fabricad::core {
class Document;
}

namespace fabricad::drawing {

struct DrawingView;
struct Sheet;

struct TextNote {
  core::ObjectId id = core::kInvalidId;
  std::string text;
  Point2 position;          ///< sheet mm, text anchor (top-left of the first line)
  double height = 3.5;      ///< mm
  double angle = 0;         ///< degrees CCW
  bool leader = false;
  Point2 leaderTo;          ///< sheet mm, arrow tip
  QJsonObject toJson() const;
  static TextNote fromJson(const QJsonObject& o);
};

/// Cross at the centre of a circle/arc, extended beyond it.
struct CenterMark {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  DimRef ref;                 ///< the circle/arc
  double extension = 2.0;     ///< sheet mm beyond the circle
  QJsonObject toJson() const;
  static CenterMark fromJson(const QJsonObject& o);
};

/// Centre line between two parallel edges (or through two points).
struct CenterLine {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  DimRef a, b;
  double extension = 2.0;     ///< sheet mm beyond the edges
  QJsonObject toJson() const;
  static CenterLine fromJson(const QJsonObject& o);
};

/// Item balloon pointing at a component/body in a view.
struct Balloon {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  core::ObjectId itemKey = core::kInvalidId;  ///< component id (assemblies) or body id (parts)
  Point2 attach;                              ///< view-local arrow tip
  Point2 position;                            ///< sheet mm, circle centre
  /// Curve the tip sits on and where along it (0..1): the tip follows that edge when the model
  /// changes (DRW-ASM-OUTPUT-3). Unset for old files: the tip stays where it was.
  bool hasRef = false;
  DimRef ref;
  double refT = 0.5;
  bool orphaned = false;                      ///< not persisted: the edge is gone
  int itemNumber = 0;                         ///< resolved from the parts list (0 = unknown)
  QJsonObject toJson() const;
  static Balloon fromJson(const QJsonObject& o);
};

/// One parts-list row.
struct BomRow {
  int item = 0;
  int quantity = 1;
  std::string partNumber;
  std::string description;
  std::string material;
  double mass = 0;                       ///< kg, 0 = unknown
  std::vector<core::ObjectId> keys;      ///< component ids (or body ids) this row covers
};

/// Parts list table (bound to the document BOM).
struct PartsList {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;  ///< optional: restrict to that view's sources
  Point2 position;                           ///< sheet mm, top-left corner
  std::vector<std::string> columns{"Item", "Qty", "Part Number", "Description", "Material"};
  std::vector<double> columnWidths{12, 12, 45, 60, 30};
  double rowHeight = 7.0;
  double textHeight = 3.0;
  std::vector<BomRow> rows;                  ///< cached (rebuilt from the document)
  double width() const;
  double height() const;                     ///< header + rows
  QJsonObject toJson() const;
  static PartsList fromJson(const QJsonObject& o);
};

/// Build the BOM from the document: one row per component definition (occurrences grouped,
/// quantity counted); documents without child components get one row per body.
/// Local adapter until assembly::buildBom() exists (see integration notes).
std::vector<BomRow> buildBom(const core::Document& doc, const std::vector<core::ShapeRef>& sources = {});
/// Item number of a component/body key in a BOM (0 if absent).
int bomItemFor(const std::vector<BomRow>& rows, core::ObjectId key);
/// Component that owns a body (or the body's component chain) for balloon lookups.
core::ObjectId bomKeyForBody(const core::Document& doc, core::ObjectId bodyId, bool perBody);

/// Point the balloon tip should be at in the view's current geometry; false when its edge no
/// longer resolves (then the stored tip is kept).
bool resolveBalloonTip(const Balloon& b, const DrawingView& view, Point2& tip);
/// Nearest curve parameter (0..1) of `p` on `c` (sampled).
double nearestCurveParam(const Curve2D& c, const Point2& p);

/// Geometry of a centre mark in sheet coordinates (two crossing chain lines). Empty if unresolved.
std::vector<Curve2D> layoutCenterMark(const CenterMark& m, const DrawingView& view);
std::vector<Curve2D> layoutCenterLine(const CenterLine& l, const DrawingView& view);

}  // namespace fabricad::drawing
