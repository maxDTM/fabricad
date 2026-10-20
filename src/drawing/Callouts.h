// FabriCAD drawing: second-generation annotations (Fusion 360 drawing "Symbols", "Tables" and
// dimension groups): hole / thread callouts, GD&T feature control frames, datum feature
// symbols, surface texture symbols, ordinate dimensions, revision tables. All are value types
// persisted inside their Sheet (Sheet::callouts); layout produces display lists in sheet mm, so
// the canvas and the PDF / SVG / DXF exporters share one code path.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"
#include "core/Units.h"
#include "drawing/Dimension.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Symbols.h"

#include <QJsonObject>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fabricad::core {
class Document;
}

namespace fabricad::drawing {

struct DrawingView;
struct Sheet;
class DrawingModel;

/// Where a leader / symbol points: a view-local point of a view (follows the view when it moves
/// or is re-scaled) or a free sheet point.
struct AnchorPoint {
  core::ObjectId viewId = core::kInvalidId;
  Point2 local;   ///< view-local (viewId valid)
  Point2 sheet;   ///< sheet mm (no view)
  static AnchorPoint onView(const DrawingView& v, const Point2& sheetPos);
  static AnchorPoint free(const Point2& sheetPos);
  /// Sheet position (falls back to `sheet` when the view is gone).
  Point2 resolve(const Sheet& sheet) const;
  QJsonObject toJson() const;
  static AnchorPoint fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------------------------
// Hole and thread callouts
// ---------------------------------------------------------------------------------------

/// What a hole looks like, from the hole feature's parameters or from the B-rep around the
/// picked circle (coaxial cylinders / cones of the body).
struct HoleInfo {
  bool ok = false;
  enum Type { Simple, Counterbore, Countersink } type = Simple;
  double diameter = 0;        ///< drill diameter (mm)
  bool through = false;
  double depth = 0;           ///< blind: full-diameter depth from the entry face (mm)
  double drillAngle = 0;      ///< drill point angle (deg), 0 = flat bottom
  double cboreDiameter = 0, cboreDepth = 0;
  double csinkDiameter = 0, csinkAngle = 0;
  int count = 1;              ///< identical holes of the same feature ("4x")
  std::string thread;         ///< "M8x1.25" when tapped (feature data or derived)
  std::string threadClass;    ///< "6H"
  double threadDepth = 0;     ///< 0 = full depth / through
  std::string source;         ///< "feature:<name>" or "geometry"
};

/// Analyse the hole whose edge produced the projected circle `circle` of the view.
HoleInfo analyzeHole(const core::Document& doc, const Curve2D& circle);
/// Metric coarse designation ("M8x1.25") for an internal thread whose hole has `diameter` (tap
/// drill = d - P, or the nominal major diameter); empty when no size matches.
std::string metricThreadForHole(double diameter, double* nominal = nullptr, double* pitch = nullptr);

struct HoleCallout {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  DimRef ref;                  ///< the hole's circle in the view
  Point2 position;             ///< sheet mm: end of the leader (start of the shoulder)
  bool thread = false;         ///< thread callout (designation first) instead of a hole callout
  std::string threadOverride;  ///< user designation, e.g. "M8x1.25"; "auto" = from the drill size
  std::string threadClass = "6H";
  std::string overrideText;    ///< non-empty replaces the generated lines ('\n' separated)
  int countOverride = 0;       ///< > 0 replaces the counted number of holes
  int precision = -1;          ///< -1 = document precision
  // cached (refreshCallouts; not persisted)
  HoleInfo info;
  std::vector<RichLine> lines;
  bool orphaned = false;
  QJsonObject toJson() const;
  static HoleCallout fromJson(const QJsonObject& o);
};
/// Callout lines for a hole (Fusion style): "4x Ø6.6 ↧10", "⌴ Ø11 ↧6", "M8x1.25 - 6H ↧12".
std::vector<RichLine> holeCalloutLines(const HoleInfo& info, const HoleCallout& callout, const core::UnitSystem& units);

// ---------------------------------------------------------------------------------------
// GD&T
// ---------------------------------------------------------------------------------------

struct GdtDatumRef {
  std::string letter;
  GdtModifier modifier = GdtModifier::None;
};

struct FcfRow {
  GdtCharacteristic characteristic = GdtCharacteristic::Position;
  bool diameterZone = false;           ///< Ø before the tolerance value
  double tolerance = 0.1;              ///< mm
  std::string toleranceText;           ///< non-empty replaces the formatted value
  GdtModifier modifier = GdtModifier::None;
  std::vector<GdtDatumRef> datums;     ///< primary, secondary, tertiary (<= 3)
};

/// Feature control frame (ISO 1101 tolerance frame) with an optional leader.
struct FeatureControlFrame {
  core::ObjectId id = core::kInvalidId;
  std::vector<FcfRow> rows{FcfRow{}};  ///< two rows with the same characteristic = composite
  Point2 position;                     ///< sheet mm, top-left corner of the frame
  bool leader = true;
  AnchorPoint anchor;                  ///< arrow tip
  std::string above, below;            ///< notes over / under the frame ("2x", "ALL OVER")
  QJsonObject toJson() const;
  static FeatureControlFrame fromJson(const QJsonObject& o);
};

/// Parse "position dia 0.05 M A B C" / "flatness 0.02" into a row (tool command line).
bool parseFcfRow(const std::string& text, FcfRow& out);

/// Datum feature symbol: letter in a square joined to a filled datum triangle on the feature.
struct DatumFeature {
  core::ObjectId id = core::kInvalidId;
  std::string letter = "A";
  Point2 position;          ///< sheet mm, centre of the letter box
  AnchorPoint anchor;       ///< base of the datum triangle (on the feature)
  QJsonObject toJson() const;
  static DatumFeature fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------------------------
// Surface texture (ISO 1302 / ISO 21920-1)
// ---------------------------------------------------------------------------------------

enum class SurfaceProcess { Any, RemovalRequired, RemovalProhibited };
const char* surfaceProcessName(SurfaceProcess p);
SurfaceProcess surfaceProcessFromName(const std::string& s);

struct SurfaceFinish {
  core::ObjectId id = core::kInvalidId;
  SurfaceProcess process = SurfaceProcess::RemovalRequired;
  bool allAround = false;
  std::string roughness = "Ra 3.2";  ///< position a (under the line)
  std::string secondary;             ///< position b
  std::string method;                ///< position c (production method, over the line)
  std::string lay;                   ///< position d: "=", "⊥", "X", "M", "C", "R", "P"
  std::string allowance;             ///< position e (machining allowance)
  AnchorPoint anchor;                ///< point on the surface (symbol tip, or leader arrow tip)
  bool leader = false;
  Point2 position;                   ///< sheet mm: symbol tip when a leader is used
  QJsonObject toJson() const;
  static SurfaceFinish fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------------------------
// Ordinate dimensions
// ---------------------------------------------------------------------------------------

enum class OrdinateAxis { X, Y };  ///< X: measures horizontal positions (vertical leaders)

struct OrdinateMember {
  DimRef ref;
  Point2 textPos;   ///< sheet mm: leader end
  double value = 0; ///< cached (not persisted), model mm from the origin
  bool valid = false;
};

/// Ordinate dimension set: an origin (shown as "0") and members measured from it along one axis.
struct OrdinateDimension {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId viewId = core::kInvalidId;
  OrdinateAxis axis = OrdinateAxis::X;
  DimRef origin;
  Point2 originText;              ///< sheet mm: leader end of the origin
  std::vector<OrdinateMember> members;
  int precision = -1;
  bool valid = false;             ///< cached: origin resolves
  QJsonObject toJson() const;
  static OrdinateDimension fromJson(const QJsonObject& o);
};

/// Re-measure an ordinate set against its view.
void updateOrdinate(OrdinateDimension& o, const DrawingView& view);

// ---------------------------------------------------------------------------------------
// Baseline / chain dimensions (produce ordinary linear dimensions)
// ---------------------------------------------------------------------------------------

/// Baseline: every point is dimensioned from `base`; the dimension lines are stacked from
/// `firstLine` (sheet mm, a point the first dimension line passes through) by `spacing`.
/// `type` Horizontal or Vertical. Points are sorted by distance from the base.
std::vector<Dimension> makeBaselineDimensions(const DrawingView& view, const DimRef& base,
                                              const std::vector<DimRef>& points, DimensionType type,
                                              const Point2& firstLine, double spacing);
/// Chain: consecutive points (sorted along the axis) dimensioned on one line through `line`.
std::vector<Dimension> makeChainDimensions(const DrawingView& view, const std::vector<DimRef>& points,
                                           DimensionType type, const Point2& line);

// ---------------------------------------------------------------------------------------
// Revision table
// ---------------------------------------------------------------------------------------

struct RevisionRow {
  std::string rev, description, date, approved;
};

struct RevisionTable {
  core::ObjectId id = core::kInvalidId;
  Point2 position;   ///< sheet mm, top-left corner
  std::vector<std::string> columns{"Rev", "Description", "Date", "Approved"};
  std::vector<double> columnWidths{12, 70, 24, 22};
  double rowHeight = 7.0;
  double textHeight = 3.0;
  std::vector<RevisionRow> rows;
  double width() const;
  double height() const;
  /// Next revision letter ("A", "B", ... "Z", "AA").
  std::string nextRev() const;
  QJsonObject toJson() const;
  static RevisionTable fromJson(const QJsonObject& o);
};
/// Default position: top-right corner inside the frame.
Point2 defaultRevisionTablePosition(const Sheet& sheet, const RevisionTable& table);

// ---------------------------------------------------------------------------------------
// Container (one per sheet)
// ---------------------------------------------------------------------------------------

struct CalloutSet {
  std::vector<HoleCallout> holeCallouts;
  std::vector<FeatureControlFrame> featureFrames;
  std::vector<DatumFeature> datums;
  std::vector<SurfaceFinish> surfaceFinishes;
  std::vector<OrdinateDimension> ordinates;
  std::vector<RevisionTable> revisionTables;

  bool empty() const;
  bool contains(core::ObjectId id) const;
  bool erase(core::ObjectId id);
  /// Ids of callouts attached to a view (hole callouts, ordinates, anchors on the view).
  std::vector<core::ObjectId> entitiesOf(core::ObjectId viewId) const;
  /// All ids.
  std::vector<core::ObjectId> ids() const;
  /// Move a callout's sheet-space parts by delta. False when the id is not a callout.
  /// `sheet` (optional) lets symbols that sit directly on a view slide along it.
  bool move(core::ObjectId id, const Point2& delta, const Sheet* sheet = nullptr);
  /// A view moved by delta: text positions of callouts attached to it follow.
  void followView(core::ObjectId viewId, const Point2& delta);
  /// View id an entity is attached to (kInvalidId when free).
  core::ObjectId viewOf(core::ObjectId id) const;
  HoleCallout* holeCallout(core::ObjectId id);
  FeatureControlFrame* featureFrame(core::ObjectId id);
  DatumFeature* datum(core::ObjectId id);
  SurfaceFinish* surfaceFinish(core::ObjectId id);
  OrdinateDimension* ordinate(core::ObjectId id);
  RevisionTable* revisionTable(core::ObjectId id);

  /// Written into / read from the sheet JSON object (keys "holeCallouts", "featureFrames"...).
  void writeJson(QJsonObject& sheetJson) const;
  static CalloutSet readJson(const QJsonObject& sheetJson);
};

/// Re-evaluate cached data (hole analysis + callout lines, ordinate values) of a sheet.
void refreshCallouts(const core::Document& doc, Sheet& sheet);

}  // namespace fabricad::drawing
