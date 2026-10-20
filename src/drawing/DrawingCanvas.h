// FabriCAD drawing: QGraphicsView-based 2D sheet canvas (no OpenGL). Shows one sheet of a
// DrawingModel, handles selection / direct manipulation (drag views, dimensions, notes),
// zoom/pan, and hands mouse/keyboard input to the ICanvasInteraction installed by the
// active drawing tool. All public coordinates are sheet millimetres (origin top-left, Y down).
// The canvas is fully driveable without a window (simulate* methods) for headless tests.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/Types.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"
#include "drawing/SheetRenderer.h"

#include <QGraphicsItem>
#include <QGraphicsView>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class QGraphicsScene;

namespace fabricad::core {
class ToolManager;
}

namespace fabricad::drawing {

class DrawingModel;
class DrawingCanvas;
struct Sheet;
struct DrawingView;

/// Kind of object under the cursor.
enum class HitKind { None, View, Dimension, Note, CenterMark, CenterLine, Balloon, PartsList, TitleBlock, Callout };
const char* hitKindName(HitKind k);

/// Result of a hit test, in sheet coordinates.
struct CanvasHit {
  HitKind kind = HitKind::None;
  core::ObjectId id = core::kInvalidId;      ///< object hit (view or entity id)
  core::ObjectId viewId = core::kInvalidId;  ///< view whose geometry was hit (View / view-bound entities)
  Point2 sheetPos;                           ///< where the test was made
  Point2 viewLocal;                          ///< sheetPos in the view's local coordinates (View hits)
  // Projected geometry under the cursor (View hits only):
  bool onCurve = false;
  size_t curveIndex = 0;
  Curve2D curve;
  bool onVertex = false;
  Vertex2D vertex;
  bool isObject() const { return kind != HitKind::None; }
  bool hasGeometry() const { return onCurve || onVertex; }
};

/// Mouse/keyboard behaviour installed by a drawing tool while it is active. The canvas
/// handles navigation (wheel zoom, middle-drag pan) itself; everything else goes here. When
/// no interaction is installed the canvas runs its default select/drag behaviour.
class ICanvasInteraction {
 public:
  virtual ~ICanvasInteraction() = default;
  virtual void canvasPress(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
    (void)c; (void)sheet; (void)button; (void)modifiers;
  }
  virtual void canvasMove(DrawingCanvas& c, const Point2& sheet, int buttons, int modifiers) {
    (void)c; (void)sheet; (void)buttons; (void)modifiers;
  }
  virtual void canvasRelease(DrawingCanvas& c, const Point2& sheet, core::MouseButton button, int modifiers) {
    (void)c; (void)sheet; (void)button; (void)modifiers;
  }
  virtual void canvasDoubleClick(DrawingCanvas& c, const Point2& sheet, int modifiers) { (void)c; (void)sheet; (void)modifiers; }
  /// Return true when consumed (otherwise the canvas handles Esc/Enter/Delete itself).
  virtual bool canvasKey(DrawingCanvas& c, int key, int modifiers) { (void)c; (void)key; (void)modifiers; return false; }
  /// Transient graphics (sheet mm) drawn above the sheet; rebuilt after every event.
  virtual void canvasPreview(const DrawingCanvas& c, DisplayList& out) { (void)c; (void)out; }
  /// Objects the tool wants highlighted (e.g. the chosen parent view).
  virtual std::vector<core::ObjectId> canvasHighlights() const { return {}; }
  /// Whether a plain click may also change the canvas selection.
  virtual bool allowsSelection() const { return false; }
};

/// Scene item painting one display list (a view, an annotation, the sheet frame...).
class DisplayListItem : public QGraphicsItem {
 public:
  DisplayListItem(HitKind kind, core::ObjectId id, DisplayList list, const std::string& font);
  HitKind kind() const { return kind_; }
  core::ObjectId id() const { return id_; }
  const DisplayList& list() const { return list_; }
  void setList(DisplayList list);
  void setState(bool selected, bool hovered, bool highlighted);
  void setDragOffset(const Point2& d);
  void setColor(const QColor& c) { color_ = c; update(); }
  void setErrorText(const std::string& t) { error_ = t; update(); }
  void setBusy(bool b) { busy_ = b; update(); }
  QRectF boundingRect() const override { return bounds_; }
  void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;
  int type() const override { return UserType + 1; }

 private:
  void updateBounds();
  HitKind kind_;
  core::ObjectId id_;
  DisplayList list_;
  std::string font_;
  QRectF bounds_;
  QColor color_;
  bool selected_ = false, hovered_ = false, highlighted_ = false, busy_ = false;
  std::string error_;
};

class DrawingCanvas : public QGraphicsView {
  Q_OBJECT
 public:
  explicit DrawingCanvas(QWidget* parent = nullptr);
  ~DrawingCanvas() override;

  // --- model binding --------------------------------------------------------------------
  void setModel(DrawingModel* model);
  DrawingModel* model() const { return model_; }
  void setSheet(core::ObjectId sheetId);
  core::ObjectId sheetId() const { return sheetId_; }
  Sheet* sheet() const;
  void setToolManager(core::ToolManager* manager) { manager_ = manager; }
  core::ToolManager* toolManager() const { return manager_; }
  /// Undo stack used for direct manipulation (defaults to the document's).
  void setUndoStack(core::UndoStack* undo) { undo_ = undo; }

  // --- interaction ----------------------------------------------------------------------
  void setInteraction(ICanvasInteraction* i);
  ICanvasInteraction* interaction() const { return interaction_; }
  void clearInteraction() { setInteraction(nullptr); }
  /// Re-run the active interaction's preview and repaint.
  void updatePreview();
  /// Extra highlight set (tool-chosen parent view, dialog selection...).
  void setHighlighted(std::vector<core::ObjectId> ids);

  // --- selection (object ids on this sheet; mirrored into Document::selection()) ----------
  const std::vector<core::ObjectId>& selection() const { return selection_; }
  void setSelection(std::vector<core::ObjectId> ids);
  void select(core::ObjectId id, bool add = false);
  void clearSelection();
  bool isSelected(core::ObjectId id) const;
  /// Selected view ids / entity ids.
  std::vector<core::ObjectId> selectedViews() const;
  core::ObjectId hoveredId() const { return hover_; }

  // --- hit testing / snapping -----------------------------------------------------------
  /// Tolerance in sheet mm for picks (= pickPixels * mm per pixel, min 0.3 mm).
  double pickTolerance() const;
  /// Topmost object at the position, with projected geometry details for views.
  CanvasHit hitTest(const Point2& sheet, bool wantGeometry = true) const;
  /// Only projected geometry of one view (curve / vertex within tolerance).
  CanvasHit hitViewGeometry(const DrawingView& view, const Point2& sheet) const;
  /// Snap to nearby view vertices (curve ends / arc centres) within tolerance. False if none.
  bool snapToVertex(const Point2& sheet, Point2& out, core::ObjectId* viewId = nullptr) const;
  /// Rectangle (sheet mm) occupied by a view, including its label.
  BBox2 viewRect(const DrawingView& v) const;

  // --- navigation ------------------------------------------------------------------------
  void zoomToFit();
  void zoomBy(double factor, const std::optional<Point2>& aboutSheet = std::nullopt);
  void setZoom(double pixelsPerMm);
  double zoom() const;              ///< pixels per sheet mm
  double mmPerPixel() const { double z = zoom(); return z > 0 ? 1.0 / z : 1.0; }
  Point2 sheetFromWidget(const QPoint& p) const;
  QPointF widgetFromSheet(const Point2& p) const;
  /// Sheet point to use when a tool needs "somewhere sensible" (centre of the free frame area).
  Point2 defaultPlacement() const;

  // --- scene maintenance ---------------------------------------------------------------
  /// Rebuild all items from the model (called on sheetChanged / viewGeometryChanged).
  void rebuild();
  /// Apply a deferred rebuild now (hit tests after a model edit see fresh items).
  void flushPendingRebuild() const;
  /// Pixels of tolerance for picking.
  void setPickPixels(int px) { pickPixels_ = px; }
  /// Rendering of the sheet content as a display list (for tests / thumbnails).
  DisplayList displayList() const;
  const DrawingStyle& style() const;

  // --- scripted / test input (same path as real mouse events) -----------------------------
  void simulatePress(const Point2& sheet, core::MouseButton b = core::MouseButton::Left, int modifiers = 0);
  void simulateMove(const Point2& sheet, int buttons = 0, int modifiers = 0);
  void simulateRelease(const Point2& sheet, core::MouseButton b = core::MouseButton::Left, int modifiers = 0);
  void simulateClick(const Point2& sheet, int modifiers = 0);
  void simulateDoubleClick(const Point2& sheet, int modifiers = 0);
  void simulateKey(int key, int modifiers = 0);
  /// Drag with the left button from a to b (press, moves, release).
  void simulateDrag(const Point2& a, const Point2& b, int modifiers = 0);

 signals:
  void selectionChanged();
  void hoverChanged(fabricad::core::ObjectId id);
  void zoomChanged(double pixelsPerMm);
  void cursorMoved(fabricad::drawing::Point2 sheet);
  /// Request to run a tool (double-click edits, Delete). The workspace widget forwards to the ToolManager.
  void toolRequested(const std::string& toolId, std::vector<std::string> args);
  void statusMessage(const QString& text);

 protected:
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void drawBackground(QPainter* painter, const QRectF& rect) override;

 private:
  struct DragState {
    bool active = false;
    bool moved = false;
    Point2 start;
    Point2 last;
    core::ObjectId id = core::kInvalidId;
    HitKind kind = HitKind::None;
    std::vector<core::ObjectId> ids;  ///< objects being dragged (selection)
    bool rubberBand = false;
  };

  void handlePress(const Point2& sheet, core::MouseButton b, int modifiers);
  void handleMove(const Point2& sheet, int buttons, int modifiers);
  void handleRelease(const Point2& sheet, core::MouseButton b, int modifiers);
  void handleDoubleClick(const Point2& sheet, int modifiers);
  bool handleKey(int key, int modifiers);
  void applyDrag(const Point2& delta, bool commit);
  void requestTool(const std::string& id, std::vector<std::string> args = {});
  void mirrorSelection();
  void updateItemStates();
  void updateHover(const Point2& sheet);
  DisplayListItem* item(core::ObjectId id) const;
  void rebuildSheetItems();
  void connectModel();
  void onSheetChanged(core::ObjectId id);
  void onSheetsChanged();
  void onBusyChanged(bool busy);
  bool isMovable(HitKind k) const;
  BBox2 selectionRect() const;

  DrawingModel* model_ = nullptr;
  core::ObjectId sheetId_ = core::kInvalidId;
  core::ToolManager* manager_ = nullptr;
  core::UndoStack* undo_ = nullptr;
  QGraphicsScene* scene_ = nullptr;
  ICanvasInteraction* interaction_ = nullptr;
  DisplayListItem* sheetItem_ = nullptr;      ///< paper + frame + title block
  DisplayListItem* previewItem_ = nullptr;    ///< tool preview (top)
  DisplayListItem* rubberItem_ = nullptr;     ///< rubber band / drag ghost
  std::map<core::ObjectId, DisplayListItem*> items_;
  std::vector<core::ObjectId> selection_;
  std::vector<core::ObjectId> highlighted_;
  core::ObjectId hover_ = core::kInvalidId;
  DragState drag_;
  bool panning_ = false;
  QPoint panLast_;
  int pickPixels_ = 6;
  bool rebuildPending_ = false;
  bool fitPending_ = true;
  bool mirroring_ = false;
  Point2 lastCursor_;
  std::vector<QMetaObject::Connection> modelConnections_;
};

}  // namespace fabricad::drawing
