// FabriCAD drawing: annotation tools (GEOMETRY / DIMENSIONS / TEXT / TABLES groups):
// dimensions (auto-typed, associative), centre marks/lines, text notes and leaders, parts
// lists and balloons.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/Annotations.h"
#include "drawing/Dimension.h"
#include "drawing/tools/DrawingToolBase.h"

#include <QJsonObject>
#include <QPointer>

#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace fabricad::drawing {

class DrawingModel;
struct Sheet;
/// Text anchor for a new dimension moved outward (away from the measured geometry) until its
/// text clears section-line letters, view labels and other dimension texts (UX-43).
Point2 declutterDimensionText(const DrawingModel& model, const Sheet& sheet, const DrawingView& view, Dimension d);

/// Dimension (D): click projected geometry (1 or 2 picks), click to place the text. Type is
/// inferred (line -> linear, circle -> diameter, arc -> radius, two lines -> angle/distance,
/// two points -> distance). Stays active until Esc.
class DimensionTool : public DrawingToolBase {
  Q_OBJECT
 public:
  DimensionTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  bool wantsDialogButtons() const override { return false; }

  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  bool canvasKey(DrawingCanvas& c, int key, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;

  int pickCount() const { return static_cast<int>(picks_.size()); }
  int createdCount() const { return created_; }
  /// Programmatic pick (tests / scripting): geometry of `view` nearest to sheet point.
  bool pickAt(core::ToolContext& ctx, const Point2& sheet);
  /// Programmatic placement of the text; commits the dimension.
  bool placeAt(core::ToolContext& ctx, const Point2& sheet);

 private:
  bool buildDimension(Dimension& out, const Point2& textPos) const;
  void reset();
  core::ObjectId viewId_ = core::kInvalidId;
  std::vector<DimPick> picks_;
  Point2 cursor_;
  bool haveCursor_ = false;
  DimensionType forcedType_ = DimensionType::Linear;
  bool forceType_ = false;
  int created_ = 0;
  QPointer<QComboBox> typeCombo_;
};

/// Edit a dimension's text override / prefix / suffix / precision / tolerance and ISO fit (double-click).
/// Settings: text, prefix, suffix, precision, type, tolerance=<mode>, upper, lower, pm, fit, fitdisplay, tolprecision.
class EditDimensionTool : public DrawingToolBase {
  Q_OBJECT
 public:
  EditDimensionTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  bool applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value);

 private:
  void applyDialog();
  core::ObjectId dimId_ = core::kInvalidId;
  QJsonObject before_;
  QPointer<QLineEdit> textEdit_, prefixEdit_, suffixEdit_;
  QPointer<QSpinBox> precisionSpin_;
  QPointer<QComboBox> typeCombo_;
  QPointer<QComboBox> tolCombo_;
  QPointer<QLineEdit> upperEdit_, lowerEdit_, fitEdit_;
};

/// Centre mark: click circles/arcs (stays active). Args: view label + "all" marks every circle.
class CenterMarkTool : public DrawingToolBase {
  Q_OBJECT
 public:
  CenterMarkTool();
  void activate(core::ToolContext& ctx) override;
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  /// Add centre marks for all full circles of a view (one undo step). Returns the count.
  int markAllCircles(core::ToolContext& ctx, core::ObjectId viewId);
  bool markAt(core::ToolContext& ctx, const Point2& sheet);

 private:
  CanvasHit hover_;
  bool haveHover_ = false;
};

/// Centre line: click two parallel edges of a view (stays active).
class CenterLineTool : public DrawingToolBase {
  Q_OBJECT
 public:
  CenterLineTool();
  void activate(core::ToolContext& ctx) override;
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  bool canvasKey(DrawingCanvas& c, int key, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  bool pickAt(core::ToolContext& ctx, const Point2& sheet);
  int pickCount() const { return first_ ? 1 : 0; }

 private:
  bool first_ = false;
  core::ObjectId viewId_ = core::kInvalidId;
  Curve2D firstCurve_;
  CanvasHit hover_;
  bool haveHover_ = false;
};

/// Text note (and leader note): click the position (leader: arrow tip first), type the text.
class TextTool : public DrawingToolBase {
  Q_OBJECT
 public:
  explicit TextTool(bool leader);
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;

  bool hasPosition() const { return havePos_; }
  void setText(const std::string& t) { text_ = t; }
  void setPosition(const Point2& p) { pos_ = p; havePos_ = true; }

 private:
  void applyDialog();
  bool leader_;
  bool editing_ = false;
  core::ObjectId noteId_ = core::kInvalidId;
  QJsonObject before_;
  std::string text_;
  double height_ = 3.5;
  Point2 pos_, tip_, cursor_;
  bool havePos_ = false, haveTip_ = false;
  QPointer<QPlainTextEdit> textEdit_;
  QPointer<QDoubleSpinBox> heightSpin_;
};

/// Parts list: click the top-left corner (bound to the selected view's sources, else the document).
class PartsListTool : public DrawingToolBase {
  Q_OBJECT
 public:
  PartsListTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  /// Default position: above the title block, right-aligned with the frame.
  static Point2 defaultPosition(const Sheet& sheet, const PartsList& list);
  /// Clamp a requested top-left corner into the frame; a list that would overlap the title block
  /// snaps to its top-right corner (defaultPosition).
  static Point2 snapPosition(const Sheet& sheet, const PartsList& list, const Point2& pos);

 private:
  void commit(core::ToolContext& ctx, const Point2& pos);
  core::ObjectId viewId_ = core::kInvalidId;
  PartsList preview_;
  Point2 cursor_;
  bool haveCursor_ = false;
};

/// Balloon: click geometry of a view (identifies the component/body), click the balloon position.
class BalloonTool : public DrawingToolBase {
  Q_OBJECT
 public:
  BalloonTool();
  void activate(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  bool canvasKey(DrawingCanvas& c, int key, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;
  bool attachAt(core::ToolContext& ctx, const Point2& sheet);
  bool placeAt(core::ToolContext& ctx, const Point2& sheet);
  bool hasAttachment() const { return haveAttach_; }
  /// Balloon every BOM item of a view at once (around the view). Returns the count.
  int balloonAll(core::ToolContext& ctx, core::ObjectId viewId);

 private:
  core::ObjectId viewId_ = core::kInvalidId;
  core::ObjectId itemKey_ = core::kInvalidId;
  Point2 attachLocal_;
  DimRef attachRef_;
  double attachT_ = 0.5;
  Point2 cursor_;
  bool haveAttach_ = false;
  bool haveCursor_ = false;
};

}  // namespace fabricad::drawing
