// FabriCAD drawing: DrawingModel owns the sheets of one Document, persists them in
// Document::extensionData("drawing"), and keeps view geometry up to date by running HLR
// jobs on a QtConcurrent worker (results are posted back to the GUI thread).
//
// Lifetime: one model per Document, created on demand (DrawingModel::forDocument) as a
// QObject child of the Document. Tools reach it through ctx.document.
// Undo: every user edit goes through DrawingModel::edit(), which snapshots the drawing JSON
// before/after and pushes a DrawingSnapshotCommand (drawing JSON is small).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Types.h"
#include "core/UndoStack.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Projection.h"
#include "drawing/SectionCut.h"
#include "drawing/Sheet.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::drawing {

/// Work item for the HLR worker (copies of shape handles; never touches the Document).
struct ViewJob {
  core::ObjectId viewId = core::kInvalidId;
  int generation = 0;
  bool doSection = false;
  ProjectionInput projection;
  SectionInput section;
};

struct ViewJobResult {
  core::ObjectId viewId = core::kInvalidId;
  int generation = 0;
  ProjectionOutput projection;
  std::string error;
};

/// Runs a job (worker thread). Exposed for tests.
ViewJobResult runViewJob(const ViewJob& job);

class DrawingModel : public QObject {
  Q_OBJECT
 public:
  static constexpr const char* kExtensionKey = "drawing";
  static constexpr int kFormatVersion = 1;

  explicit DrawingModel(core::Document& doc);
  ~DrawingModel() override;

  /// The document's model, created on first use (child QObject of the document).
  static DrawingModel* forDocument(core::Document& doc);
  /// nullptr if none was created yet.
  static DrawingModel* find(const core::Document& doc);

  core::Document& document() const { return doc_; }
  DrawingStyle& style() { return style_; }
  const DrawingStyle& style() const { return style_; }

  // --- sheets -----------------------------------------------------------------------
  std::vector<Sheet>& sheets() { return sheets_; }
  const std::vector<Sheet>& sheets() const { return sheets_; }
  Sheet* sheet(core::ObjectId id);
  const Sheet* sheet(core::ObjectId id) const;
  Sheet* activeSheet();
  core::ObjectId activeSheetId() const { return activeSheet_; }
  void setActiveSheet(core::ObjectId id);
  Sheet& addSheet(const SheetFormat& format, ProjectionStandard standard, const std::string& name = {});
  bool removeSheet(core::ObjectId id);
  /// Sheet that contains the view/entity with this id.
  Sheet* sheetOf(core::ObjectId entityId);
  DrawingView* view(core::ObjectId id, Sheet** sheetOut = nullptr);
  const DrawingView* view(core::ObjectId id) const;

  // --- views -------------------------------------------------------------------------
  DrawingView& addBaseView(Sheet& sheet, std::vector<core::ShapeRef> sources, ViewOrientation orientation,
                           double scale, const Point2& position);
  /// Projected from `parentId` towards `direction` (frame from the sheet's standard); nullptr if no parent.
  DrawingView* addProjectedView(Sheet& sheet, core::ObjectId parentId, ProjectionDirection direction,
                                const Point2& position);
  DrawingView* addSectionView(Sheet& sheet, core::ObjectId parentId, const SectionLine& line, const Point2& position);
  DrawingView* addDetailView(Sheet& sheet, core::ObjectId parentId, const DetailSpec& detail, double scale,
                             const Point2& position);
  /// Removes the view, its children and the entities attached to them.
  bool removeView(core::ObjectId id);
  /// Move honouring alignment with the parent; children follow by the same delta.
  void moveView(core::ObjectId id, const Point2& position);
  /// Position constrained by the child's alignment to its parent.
  Point2 alignedPosition(const DrawingView& child, const Point2& desired) const;
  /// Call after editing definition fields (scale, orientation, hidden lines...).
  void viewDefinitionChanged(core::ObjectId id);
  /// Projected view frame for a would-be child of `parent` dragged to `sheetPos`.
  gp_Ax2 frameForProjection(const DrawingView& parent, const Sheet& sheet, ProjectionDirection dir) const;

  // --- entities ------------------------------------------------------------------------
  Dimension& addDimension(Sheet& sheet, Dimension dim);
  TextNote& addNote(Sheet& sheet, TextNote note);
  CenterMark& addCenterMark(Sheet& sheet, CenterMark mark);
  CenterLine& addCenterLine(Sheet& sheet, CenterLine line);
  Balloon& addBalloon(Sheet& sheet, Balloon balloon);
  PartsList& addPartsList(Sheet& sheet, PartsList list);
  /// Remove any non-view entity by id.
  bool removeEntity(core::ObjectId id);
  /// Remove a view or entity.
  bool removeObject(core::ObjectId id);
  /// Re-measure dimensions, renumber balloons, rebuild parts lists of a sheet.
  void refreshAnnotations(Sheet& sheet);
  /// BOM rows for the document (restricted to a view's sources when given).
  std::vector<BomRow> bom(const DrawingView* view = nullptr) const;

  // --- geometry (HLR) --------------------------------------------------------------------
  void invalidateView(core::ObjectId id);
  void invalidateAll();
  /// Start jobs for all invalid views (async unless setSynchronous(true)).
  void updateGeometry();
  void setSynchronous(bool sync) { synchronous_ = sync; }
  bool isSynchronous() const { return synchronous_; }
  bool isBusy() const { return !jobs_.empty(); }
  /// Process events until all jobs finished (tests / export). False on timeout.
  bool waitForIdle(int timeoutMs = 60000);
  /// Source shapes of a view in world coordinates (copies).
  std::vector<SourceShape> collectSources(const DrawingView& v) const;
  /// Worker job for a view definition that need not be in a sheet (tool previews). `exact`
  /// false selects the fast polygonal HLR. The job's generation is 0 (not tracked).
  ViewJob makeJob(const DrawingView& v, bool exact) const;
  /// Move views (alignment honoured, children follow) and annotations by `delta` (sheet mm).
  void moveObjects(Sheet& sheet, const std::vector<core::ObjectId>& ids, const Point2& delta);

  // --- persistence -------------------------------------------------------------------------
  QJsonObject toJson() const;
  void fromJson(const QJsonObject& o);
  void saveToDocument();   ///< writes extensionData (connected to Document::aboutToSave)
  void loadFromDocument(); ///< reads extensionData (connected to loaded/documentReset)
  core::ObjectId allocateId() { return doc_.allocateId(); }

  // --- undo ------------------------------------------------------------------------------
  /// Apply `fn` as one undoable step (snapshot before/after). `undo` may be null (no undo).
  /// mergeKey >= 0 lets consecutive edits with the same key merge (drags).
  void edit(const std::string& text, core::UndoStack* undo, const std::function<void()>& fn, int mergeKey = -1);
  /// Interactive edits (dialogs with live preview): take `before = toJson()` at dialog open,
  /// mutate freely, then either commitSnapshot() (pushes one undo step before->now) or revertTo(before).
  void commitSnapshot(const std::string& text, core::UndoStack* undo, const QJsonObject& before);
  void revertTo(const QJsonObject& before);
  /// Mark the drawing modified and notify listeners of the sheet.
  void notifyChanged(core::ObjectId sheetId);

 signals:
  void sheetsChanged();
  void activeSheetChanged(fabricad::core::ObjectId sheetId);
  void sheetChanged(fabricad::core::ObjectId sheetId);
  void viewGeometryChanged(fabricad::core::ObjectId viewId);
  void busyChanged(bool busy);

 private:
  struct CachedGeometry {
    QJsonObject key;
    int docGeneration = 0;
    std::vector<Curve2D> curves;
    std::vector<Vertex2D> vertices;
    BBox2 bbox;
    core::Vec3 origin3d;
    gp_Ax2 frame;
  };
  struct RunningJob {
    core::ObjectId viewId;
    int generation;
    QFutureWatcher<ViewJobResult>* watcher = nullptr;
  };

  void onRecomputeFinished();
  void onDocumentLoaded();
  bool prepareJob(const Sheet& sheet, DrawingView& v, ViewJob& job) const;
  void startJob(const ViewJob& job);
  void applyResult(const ViewJobResult& r);
  /// Automatic placement of a freshly projected view (see DrawingView::autoPlace).
  void autoArrange(Sheet& sheet, DrawingView& v);
  void finishView(Sheet& sheet, DrawingView& v);
  void updateDetailViews(Sheet& sheet, const DrawingView& parent);
  void computeDetail(DrawingView& detail, const DrawingView& parent);
  void restoreCachedGeometry();
  void cacheGeometry(const DrawingView& v);
  std::string nextSheetName() const;

  core::Document& doc_;
  DrawingStyle style_;
  std::vector<Sheet> sheets_;
  core::ObjectId activeSheet_ = core::kInvalidId;
  bool synchronous_ = false;
  bool loadPending_ = false;
  int docGeneration_ = 0;
  std::map<core::ObjectId, int> generation_;
  std::map<core::ObjectId, CachedGeometry> cache_;
  std::vector<RunningJob> jobs_;
};

/// Undo step that restores drawing JSON snapshots.
class DrawingSnapshotCommand : public core::UndoCommand {
 public:
  DrawingSnapshotCommand(DrawingModel& model, std::string text, QJsonObject before, QJsonObject after,
                         int mergeKey = -1);
  void redo() override;
  void undo() override;
  int mergeId() const override { return mergeKey_; }
  bool mergeWith(const core::UndoCommand& other) override;

 private:
  DrawingModel& model_;
  QJsonObject before_, after_;
  bool first_ = true;
  int mergeKey_;
};

}  // namespace fabricad::drawing
