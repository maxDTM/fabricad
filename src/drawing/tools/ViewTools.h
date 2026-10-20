// FabriCAD drawing: sheet and view tools of the Drawing workspace (CREATE / MODIFY groups).
// Interaction follows Fusion 360 (docs/research/ux.md section 11): the view follows the
// cursor until a click places it; the command dialog stays open for options; OK commits.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/DrawingView.h"
#include "drawing/Sheet.h"
#include "drawing/tools/DrawingToolBase.h"

#include <QJsonObject>
#include <QPointer>

#include <map>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;

namespace fabricad::drawing {

/// "New Drawing": creates the first sheet (standard, size, scale) and switches the workspace.
class NewDrawingTool : public DrawingToolBase {
  Q_OBJECT
 public:
  NewDrawingTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

 private:
  void applyArgs(const std::vector<std::string>& args);
  SheetFormat format_;
  ProjectionStandard standard_ = ProjectionStandard::FirstAngle;
  double scale_ = 1.0;
  QPointer<QComboBox> formatCombo_, standardCombo_, scaleCombo_;
};

/// Instant: add a sheet (args: format name, "portrait", "ISO"/"ASME").
class NewSheetTool : public DrawingToolBase {
  Q_OBJECT
 public:
  NewSheetTool();
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override;
};

class DeleteSheetTool : public DrawingToolBase {
  Q_OBJECT
 public:
  DeleteSheetTool();
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override;
};

/// Base view: orientation/scale/style dialog, view follows the cursor, click places, OK commits.
class BaseViewTool : public DrawingToolBase {
  Q_OBJECT
 public:
  BaseViewTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;

  // state for tests
  ViewOrientation orientation() const { return orientation_; }
  double scale() const { return scale_; }
  bool isPlaced() const { return placed_; }

 private:
  void requestPreview();
  void syncFromDialog();
  bool applyToken(const std::string& token);

  std::vector<core::ShapeRef> sources_;
  ViewOrientation orientation_ = ViewOrientation::Front;
  double scale_ = 1.0;
  bool hidden_ = false;
  TangentEdgeMode tangent_ = TangentEdgeMode::Off;
  Point2 pos_;
  bool havePos_ = false;
  bool placed_ = false;
  DrawingView preview_;
  bool previewPending_ = false;
  QPointer<QComboBox> orientationCombo_, scaleCombo_, styleCombo_, tangentCombo_;
};

/// Projected views: pick the parent, move to a side (preview follows), click to place each, OK.
class ProjectedViewTool : public DrawingToolBase {
  Q_OBJECT
 public:
  ProjectedViewTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;

  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  std::vector<core::ObjectId> canvasHighlights() const override;

  int pendingCount() const { return static_cast<int>(pending_.size()); }
  core::ObjectId parentId() const { return parent_; }
  /// Default offset placement for a direction (used by the command line).
  static Point2 autoPosition(const DrawingView& parent, ProjectionDirection dir, double gap = 25.0);
  static bool parseDirection(const std::string& text, ProjectionDirection& out);

 private:
  struct Pending {
    ProjectionDirection dir;
    Point2 pos;
  };
  struct Preview {
    DrawingView view;
    bool valid = false;
    bool pending = false;
  };
  void ensurePreview(ProjectionDirection dir);
  Point2 alignedFor(ProjectionDirection dir, const Point2& cursor) const;
  bool setParent(core::ObjectId id);

  core::ObjectId parent_ = core::kInvalidId;
  DrawingView parentCopy_;
  ProjectionStandard standard_ = ProjectionStandard::FirstAngle;
  std::map<ProjectionDirection, Preview> previews_;
  std::optional<ProjectionDirection> hoverDir_;
  Point2 hoverPos_;
  std::vector<Pending> pending_;
};

/// Section view: parent, two points of the cut line (ortho/vertex snap), placement side.
class SectionViewTool : public DrawingToolBase {
  Q_OBJECT
 public:
  SectionViewTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;

  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  bool canvasKey(DrawingCanvas& c, int key, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  std::vector<core::ObjectId> canvasHighlights() const override;

  enum class Step { Parent, FirstPoint, SecondPoint, Place, Done };
  Step step() const { return step_; }

 private:
  bool setParent(core::ObjectId id);
  Point2 snapPoint(const DrawingCanvas* c, const Point2& sheet, int modifiers) const;
  void computePlacement(const Point2& cursor);
  void requestPreview();
  void commit(core::ToolContext& ctx);

  Step step_ = Step::Parent;
  core::ObjectId parent_ = core::kInvalidId;
  DrawingView parentCopy_;
  ProjectionStandard standard_ = ProjectionStandard::FirstAngle;
  Point2 p1_, p2_;        ///< sheet coordinates
  Point2 cursor_;
  Point2 pos_;            ///< view position (sheet)
  Point2 arrowLocal_{0, 1};
  bool havePos_ = false;
  DrawingView preview_;
  bool previewValid_ = false;
};

/// Detail view: parent, centre, radius, placement (2x parent scale, clipped copy of parent geometry).
class DetailViewTool : public DrawingToolBase {
  Q_OBJECT
 public:
  DetailViewTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;

  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  std::vector<core::ObjectId> canvasHighlights() const override;

  enum class Step { Parent, Center, Radius, Place, Done };
  Step step() const { return step_; }

 private:
  bool setParent(core::ObjectId id);
  void commit(core::ToolContext& ctx);

  Step step_ = Step::Parent;
  core::ObjectId parent_ = core::kInvalidId;
  DrawingView parentCopy_;
  Point2 centerLocal_;
  double radiusLocal_ = 0;
  Point2 cursor_, pos_;
  bool havePos_ = false;
  double scale_ = 0;  ///< 0 = 2x parent
};

/// Edit View (double-click): dialog with live preview; OK commits one undo step.
class EditViewTool : public DrawingToolBase {
  Q_OBJECT
 public:
  EditViewTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  std::vector<core::ObjectId> canvasHighlights() const override { return {viewId_}; }

  /// Apply "key=value" (scale, orientation, style, tangent, label, hatch, radius, exact). False if unknown.
  bool applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value);

 private:
  void applyDialog();
  core::ObjectId viewId_ = core::kInvalidId;
  QJsonObject before_;
  bool committed_ = false;
  QPointer<QLineEdit> labelEdit_;
  QPointer<QComboBox> orientationCombo_, scaleCombo_, styleCombo_, tangentCombo_;
  QPointer<QCheckBox> hatchCheck_, exactCheck_, labelCheck_;
  QPointer<QDoubleSpinBox> radiusSpin_;
};

/// Edit Sheet: name, size, orientation, standard, scale, zones.
class EditSheetTool : public DrawingToolBase {
  Q_OBJECT
 public:
  EditSheetTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  bool applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value);

 private:
  void applyDialog();
  core::ObjectId sheetId_ = core::kInvalidId;
  QJsonObject before_;
  QPointer<QLineEdit> nameEdit_;
  QPointer<QComboBox> formatCombo_, orientationCombo_, standardCombo_, scaleCombo_;
  QPointer<QCheckBox> zonesCheck_, titleCheck_;
};

/// Edit Title Block fields (double-click on the title block).
class EditTitleBlockTool : public DrawingToolBase {
  Q_OBJECT
 public:
  EditTitleBlockTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

 private:
  void applyDialog();
  core::ObjectId sheetId_ = core::kInvalidId;
  QJsonObject before_;
  std::map<std::string, QPointer<QLineEdit>> edits_;
};

/// Instant toggles on the selected views (all views of the sheet when none selected).
class ToggleViewOptionTool : public DrawingToolBase {
  Q_OBJECT
 public:
  enum class Option { HiddenLines, TangentEdges };
  ToggleViewOptionTool(Option option);
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override;

 private:
  Option option_;
};

/// Move (M): objects, base point, destination (or args "dx,dy").
class MoveTool : public DrawingToolBase {
  Q_OBJECT
 public:
  MoveTool();
  void activate(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  std::vector<core::ObjectId> canvasHighlights() const override { return ids_; }
  bool allowsSelection() const override { return true; }

 private:
  void collectIds(core::ToolContext& ctx);
  void commit(core::ToolContext& ctx, const Point2& delta);
  std::vector<core::ObjectId> ids_;
  bool haveBase_ = false;
  Point2 base_, cursor_;
};

/// Delete the selected views/annotations (instant).
class DeleteTool : public DrawingToolBase {
  Q_OBJECT
 public:
  DeleteTool();
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override;
};

/// Object ids targeted by a command: canvas selection, then preselection, then numeric args.
std::vector<core::ObjectId> targetObjects(core::ToolContext& ctx, DrawingCanvas* canvas, const Sheet& sheet);

}  // namespace fabricad::drawing
