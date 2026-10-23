// FabriCAD drawing: common base for drawing-workspace tools. A drawing tool is an ITool
// (activated by the ToolManager like every other command) that, while active, installs
// itself as the ICanvasInteraction of the DrawingCanvas bound to the document. Tools work
// headless too: without a canvas they fall back to command-line arguments and default
// placements, which is how the unit tests and the command box drive them.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/Types.h"
#include "drawing/DrawingCanvas.h"
#include "drawing/DrawingModel.h"
#include "drawing/DrawingTypes.h"

#include <QFutureWatcher>
#include <QObject>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QFormLayout;
class QLineEdit;

namespace fabricad::drawing {

/// Runs HLR preview jobs off the GUI thread; stale results (older requests) are dropped.
class PreviewRunner : public QObject {
  Q_OBJECT
 public:
  explicit PreviewRunner(QObject* parent = nullptr);
  ~PreviewRunner() override;
  /// Start a job; `done` is called on the GUI thread with the result unless superseded.
  void request(ViewJob job, std::function<void(const ViewJobResult&)> done);
  /// Run jobs inline (tests, export).
  void setSynchronous(bool sync) { synchronous_ = sync; }
  bool isBusy() const { return busy_; }
  void cancel();

 private:
  bool synchronous_ = false;
  bool busy_ = false;
  int generation_ = 0;
  QFutureWatcher<ViewJobResult>* watcher_ = nullptr;
};

/// ToolInfo for a drawing tool (workspace Drawing, tab DRAWING, icon = id).
core::ToolInfo drawingToolInfo(const std::string& id, const std::string& name, const std::string& tooltip,
                               const std::string& group, int order, bool promoted, const std::string& shortcut = {},
                               std::vector<std::string> aliases = {});

class DrawingToolBase : public QObject, public core::ToolBase, public ICanvasInteraction {
  Q_OBJECT
 public:
  explicit DrawingToolBase(core::ToolInfo info);
  ~DrawingToolBase() override;

  /// Enabled when a document is open and (if a host exists) the Drawing workspace is active.
  bool isEnabled(const core::ToolContext& ctx) const override;
  void deactivate(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;

  /// Tools usable from the Design workspace too (e.g. "New Drawing").
  void setAnyWorkspace(bool any) { anyWorkspace_ = any; }

  /// Parse "x,y" (sheet mm). False if not a point.
  static bool parsePoint(const std::string& text, Point2& out);
  /// Parse "key=value"; false if no '='.
  static bool parseKeyValue(const std::string& text, std::string& key, std::string& value);
  static std::string lower(std::string s);

 protected:
  // --- access ----------------------------------------------------------------------------
  DrawingModel* model(core::ToolContext& ctx) const;
  DrawingCanvas* canvas(core::ToolContext& ctx) const;
  /// Active sheet; when `create` is true and none exists an A3/ISO sheet is added (not undoable
  /// by itself: call inside a model edit).
  Sheet* activeSheet(core::ToolContext& ctx, bool create = false);
  /// View chosen by the user: canvas selection, then preselection (DrawingView refs), then args.
  DrawingView* pickedView(core::ToolContext& ctx, Sheet& sheet) const;
  /// Point for views/annotations when no cursor is available.
  Point2 fallbackPlacement(core::ToolContext& ctx) const;
  /// Installs/removes this tool as canvas interaction. Safe without a canvas.
  void attachCanvas(core::ToolContext& ctx);
  void detachCanvas();
  void refreshCanvas();
  /// Highlights on the canvas while the tool runs (parent view etc.).
  void setCanvasHighlights(std::vector<core::ObjectId> ids);
  core::ToolContext* context() const { return ctx_; }
  /// Whether a dialog shell exists (host with a main window). Without it tools commit on click.
  bool hasDialogShell(core::ToolContext& ctx) const;
  // (parsePoint/parseKeyValue/lower are public, see above)
  /// Preview runner (created on demand, synchronous when the model is).
  PreviewRunner& previews(core::ToolContext& ctx);
  /// Preview curves of a temporary view placed at `position` into a display list.
  static void addPreviewView(DisplayList& out, const DrawingView& v, const Point2& position);
  /// Dashed placeholder rectangle (sheet mm).
  static void addPlaceholder(DisplayList& out, const BBox2& rect);

  // --- dialog helpers (Qt widgets; safe offscreen) -----------------------------------------
  /// Card form: a row whose field does not fit beside its label wraps below it, so nothing is
  /// clipped on the right of the narrow card (DRW2-VIEWS-ANNOT-9).
  static QFormLayout* newForm(QWidget* parent);
  static QComboBox* addOrientationCombo(QFormLayout* form, ViewOrientation current);
  static QComboBox* addScaleCombo(QFormLayout* form, double current);
  static double comboScale(const QComboBox* combo, double fallback);
  /// Display label of an identifier: "IsoTopRight" -> "Iso Top Right", "PartNumber" -> "Part Number".
  static QString spacedName(const std::string& id);

  core::ToolContext* ctx_ = nullptr;
  DrawingCanvas* canvas_ = nullptr;
  std::unique_ptr<PreviewRunner> previews_;
  bool anyWorkspace_ = false;
};

}  // namespace fabricad::drawing
