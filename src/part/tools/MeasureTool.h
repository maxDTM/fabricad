// FabriCAD part: Measure / Inspect (INSPECT > Measure, shortcut I).
//
// Pick one or two entities (vertex, edge, face, body, component, sketch point/curve,
// construction geometry). One entity reports its own properties (position, length, radius,
// diameter, area, perimeter, volume, mass, centre of mass, bounding box); two entities report
// the minimum distance (BRepExtrema_DistShapeShape) with the delta X/Y/Z between the closest
// points, the centre distance for circles/cylinders, and the angle between planar faces or
// linear edges. Values appear in the command dialog as a two-column table whose rows can be
// copied; the measurement itself is a pure function (measureRefs) so it is unit-testable
// without a GL context.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/Types.h"
#include "core/Units.h"
#include "core/ViewerContext.h"

#include <QObject>
#include <QPointer>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

class QLabel;
class QTableWidget;

namespace fabricad::core {
class Document;
}

namespace fabricad::part {

struct MeasureRow {
  std::string label;   ///< "Distance", "Area", "Centre of mass"...
  std::string value;   ///< formatted in display units ("12.500 mm", "1 234.5 mm2")
  double number = 0;   ///< raw value in base units (mm, mm2, mm3, degrees, g)
};

struct MeasureResult {
  std::vector<MeasureRow> rows;
  std::string summary;              ///< one line for the status bar / prompt
  bool hasDistance = false;         ///< two entities: minimum distance known
  core::Vec3 p1, p2;                ///< closest points (world) when hasDistance
  double distance = 0;              ///< mm
  bool ok() const { return !rows.empty(); }
  const MeasureRow* row(const std::string& label) const;
};

/// World-space shape of a reference (component placement applied). Null when unresolved.
TopoDS_Shape measureShapeOf(const core::Document& doc, const core::ShapeRef& ref);
/// Measure one or two references. Mass uses Body::density (g/cm3) when it is known.
MeasureResult measureRefs(const core::Document& doc, const std::vector<core::ShapeRef>& refs, const core::UnitSystem& units);
/// Clipboard-friendly text ("Distance\t12.500 mm\n...").
std::string measureText(const MeasureResult& r);

class MeasureTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  MeasureTool();
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  /// Picked references (at most two; a third pick starts over, picking a selected one removes it).
  const std::vector<core::ShapeRef>& picks() const { return picks_; }
  const MeasureResult& result() const { return result_; }
  /// Programmatic pick (what a viewport click does).
  void pick(core::ToolContext& ctx, const core::ShapeRef& ref);
  void clearPicks(core::ToolContext& ctx);

 private:
  void recompute(core::ToolContext& ctx);
  void showResult(core::ToolContext& ctx);

  std::vector<core::ShapeRef> picks_;
  MeasureResult result_;
  std::vector<core::PreviewHandle> previews_;
  core::ToolContext* ctx_ = nullptr;
  QPointer<QLabel> summaryLabel_;
  QPointer<QLabel> pickLabel_;
  QPointer<QTableWidget> table_;
};

}  // namespace fabricad::part
