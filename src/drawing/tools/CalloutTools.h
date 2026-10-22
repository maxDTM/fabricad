// FabriCAD drawing: tools for the round-2 annotations (Fusion 360 drawing toolbar):
//   DIMENSIONS: drawing.dimension-ordinate, drawing.dimension-baseline, drawing.dimension-chain
//   SYMBOLS:    drawing.hole-callout, drawing.thread-callout, drawing.feature-control-frame,
//               drawing.datum, drawing.surface-texture, drawing.edit-callout (hidden; double-click)
//   TABLES:     drawing.revision-table, drawing.revision
//   CREATE:     drawing.section-view-stepped, drawing.section-view-aligned, drawing.break-view
// Every tool works with the canvas (clicks) and headless: "x,y" arguments / command-line input
// are treated as clicks in sheet mm, "key=value" arguments set options, Enter / "done" ends
// multi-pick tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/Callouts.h"
#include "drawing/tools/DrawingToolBase.h"

#include <QJsonObject>
#include <QPointer>

#include <map>
#include <string>
#include <vector>

class QComboBox;
class QLineEdit;

namespace fabricad::core {
class ToolRegistry;
}

namespace fabricad::drawing {

void registerCalloutTools(core::ToolRegistry& tools);

/// Common click-sequence behaviour of the callout tools.
class ClickSequenceTool : public DrawingToolBase {
  Q_OBJECT
 public:
  using DrawingToolBase::DrawingToolBase;
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  bool wantsDialogButtons() const override { return false; }
  void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) override;
  void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) override;
  bool canvasKey(DrawingCanvas& c, int key, int modifiers) override;
  void canvasPreview(const DrawingCanvas& c, DisplayList& out) override;

  /// Feed one click (sheet mm). Returns false when the click was not usable.
  bool click(const Point2& sheet) { return ctx_ ? onClick(*ctx_, sheet) : false; }
  /// End a multi-pick sequence (Enter).
  void done() { if (ctx_) onDone(*ctx_); }
  int createdCount() const { return created_; }
  const std::vector<core::ObjectId>& createdIds() const { return createdIds_; }

 protected:
  virtual void reset() {}
  /// After reset and canvas attach, before the arguments are applied (pre-selection).
  virtual void begin(core::ToolContext& ctx) { (void)ctx; }
  virtual bool onClick(core::ToolContext& ctx, const Point2& sheet) = 0;
  /// Enter / "done" / OK. Default: finish the tool.
  virtual void onDone(core::ToolContext& ctx) { ctx.finish(); }
  virtual bool setOption(const std::string& key, const std::string& value) { (void)key; (void)value; return false; }
  virtual void preview(DisplayList& out) const { (void)out; }
  virtual std::string prompt() const { return {}; }
  /// Whether more clicks are expected (headless runs finish once the args are consumed).
  virtual bool expectsMore() const { return false; }
  /// Geometry under a sheet point (canvas pick tolerance, or 1 mm over all views headless).
  CanvasHit geometryAt(const Point2& sheet) const;
  /// Reference (vertex preferred, then curve) of a geometry hit.
  static bool refOf(const CanvasHit& h, DimRef& out, Point2* local = nullptr);
  /// Push an undoable edit; `fn` receives the sheet.
  void commit(const std::string& text, const std::function<void(Sheet&)>& fn);
  void updatePrompt();
  /// Finish the tool from inside a click handler (stops argument processing).
  void finishNow(core::ToolContext& ctx);
  bool finished_ = false;

  Point2 cursor_;
  bool haveCursor_ = false;
  int created_ = 0;
  std::vector<core::ObjectId> createdIds_;
};

/// Hole / thread callout: click a hole (circle), click where the text goes.
class HoleCalloutTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  explicit HoleCalloutTool(bool thread);

 protected:
  void reset() override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  bool setOption(const std::string& key, const std::string& value) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return havePick_; }

 private:
  HoleCallout pending() const;
  bool thread_;
  bool havePick_ = false;
  HoleCallout proto_;
  core::ObjectId viewId_ = core::kInvalidId;
};

/// Feature control frame: click the feature (arrow tip), click the frame position. Options:
/// row="position dia 0.05 M A B C" (repeat for composite rows), leader=0, above=, below=.
class FeatureControlFrameTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  FeatureControlFrameTool();
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

 protected:
  void reset() override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  bool setOption(const std::string& key, const std::string& value) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return haveAnchor_; }

 private:
  void readDialog();
  FeatureControlFrame frame_;
  bool rowsSet_ = false;
  bool haveAnchor_ = false;
  QPointer<QLineEdit> rowEdit_;
};

/// Datum identifier: click the feature, click the letter box. Option letter=B.
class DatumTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  DatumTool();

 protected:
  void reset() override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  bool setOption(const std::string& key, const std::string& value) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return haveAnchor_; }

 private:
  std::string nextLetter(const Sheet& sheet) const;
  DatumFeature datum_;
  std::string letter_;
  bool haveAnchor_ = false;
};

/// Surface texture: click the surface, click the symbol position (a leader is added when it is
/// away from the surface). Options: roughness= (or ra=), process=required|prohibited|any,
/// lay=, method=, secondary=, allowance=, allaround=1.
class SurfaceTextureTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  SurfaceTextureTool();

 protected:
  void reset() override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  void onDone(core::ToolContext& ctx) override;
  bool setOption(const std::string& key, const std::string& value) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return false; }

 private:
  void place(const Point2& pos);
  SurfaceFinish symbol_;
  bool haveAnchor_ = false;
};

/// Ordinate dimensions: click the origin, click the origin label (its direction sets the axis),
/// then click features; Enter ends. Option axis=x|y.
class OrdinateTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  OrdinateTool();

 protected:
  void reset() override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  bool setOption(const std::string& key, const std::string& value) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return step_ != 0; }
  void onDone(core::ToolContext& ctx) override;

 private:
  int step_ = 0;  ///< 0 origin, 1 origin label, 2 members
  core::ObjectId viewId_ = core::kInvalidId, ordinateId_ = core::kInvalidId;
  /// The origin label is only committed together with the first member (DRW-VIEWS-ANNOT-14).
  OrdinateDimension pending_;
  bool committed_ = false;
  DimRef origin_;
  Point2 originSheet_;
  int forcedAxis_ = -1;
};

/// Baseline / chain dimensions: select a linear dimension (click it or pre-select it), then
/// click features; each click adds a dimension. Option spacing= (baseline).
class BaselineChainTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  explicit BaselineChainTool(bool chain);

 protected:
  void reset() override;
  void begin(core::ToolContext& ctx) override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  bool setOption(const std::string& key, const std::string& value) override;
  std::string prompt() const override;
  bool expectsMore() const override { return false; }

 private:
  bool setBase(const Sheet& sheet, core::ObjectId dimId);
  bool chain_;
  bool haveBase_ = false;
  core::ObjectId viewId_ = core::kInvalidId;
  DimRef base_, last_;
  DimensionType type_ = DimensionType::Horizontal;
  Point2 line_;
  double spacing_ = 8.0;
  int index_ = 1;
  /// Chain: spans (sheet mm along the measured axis) already taken on the current dimension
  /// line; a segment that would overlap one moves to the next line out (DRW-VIEWS-ANNOT-4).
  std::vector<std::pair<double, double>> occupied_;
};

/// Break view: select a view, click the two edges of the band to remove. Options:
/// orientation=horizontal|vertical, gap=, style=structural|curved.
class BreakViewTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  BreakViewTool();

 protected:
  void reset() override;
  void begin(core::ToolContext& ctx) override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  bool setOption(const std::string& key, const std::string& value) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return haveFirst_ || viewId_ == core::kInvalidId; }

 private:
  core::ObjectId viewId_ = core::kInvalidId;
  bool haveFirst_ = false;
  Point2 first_;
  int orientation_ = -1;  ///< -1 = from the clicks
  double gap_ = -1;
  int style_ = -1;
};

/// Stepped / aligned section view: parent view, click the cut path (Enter ends it), click the
/// view position.
class ComplexSectionTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  explicit ComplexSectionTool(bool aligned);
  std::vector<core::ObjectId> canvasHighlights() const override;

 protected:
  void reset() override;
  void begin(core::ToolContext& ctx) override;
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  void onDone(core::ToolContext& ctx) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override;
  bool expectsMore() const override { return true; }

 private:
  Point2 snap(const Point2& p) const;
  bool place(core::ToolContext& ctx, const Point2& sheet);
  bool aligned_;
  core::ObjectId parent_ = core::kInvalidId;
  std::vector<Point2> path_;  ///< sheet mm
  bool pathDone_ = false;
};

/// Revision table: click its top-left corner (Enter: default top-right of the frame).
class RevisionTableTool : public ClickSequenceTool {
  Q_OBJECT
 public:
  RevisionTableTool();

 protected:
  bool onClick(core::ToolContext& ctx, const Point2& sheet) override;
  void onDone(core::ToolContext& ctx) override;
  void preview(DisplayList& out) const override;
  std::string prompt() const override { return "Click the top-left corner of the revision table (Enter: top right)"; }
  bool expectsMore() const override { return true; }

 private:
  bool create(core::ToolContext& ctx, const Point2* pos);
};

/// Add a revision row (instant): description=, approved=, date=, rev=. Creates the table if
/// the sheet has none.
class AddRevisionTool : public DrawingToolBase {
  Q_OBJECT
 public:
  AddRevisionTool();
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override;
};

/// Edit any callout with key=value settings (double-click opens it with a settings field).
class EditCalloutTool : public DrawingToolBase {
  Q_OBJECT
 public:
  EditCalloutTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  /// Apply one setting to the callout being edited.
  bool applySetting(core::ToolContext& ctx, const std::string& key, const std::string& value);

 private:
  /// Revision tables (DRW-ASM-OUTPUT-5): a form per row instead of the key=value field.
  QWidget* createRevisionForm(QWidget* parent, const RevisionTable& t);
  void loadRevisionRow();
  void storeRevisionRow();
  core::ObjectId id_ = core::kInvalidId;
  QJsonObject before_;
  QPointer<QLineEdit> edit_;
  int revRow_ = -1;  ///< revision row edited by the settings (-1 = last)
  QPointer<QComboBox> rowCombo_;
  QPointer<QLineEdit> revEdit_, descEdit_, dateEdit_, approvedEdit_;
};

}  // namespace fabricad::drawing
