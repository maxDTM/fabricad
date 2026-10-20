// FabriCAD viewer: the 3D viewport. OWNER: ui agent.
//
// ViewerWidget is a light QWidget container implementing core::IViewerContext. The OCCT
// AIS/V3d view (viewer::OcctView, a QOpenGLWidget adapted from gkv311's MIT sample) is created
// lazily by initializeView(), which the app calls right after the main window is shown, so
// startup never waits for GL. Before that (and in headless tests, QT_QPA_PLATFORM=offscreen)
// every IViewerContext call falls back to an embedded core::NullViewerContext and the widget
// paints a plain gradient.
//
// Responsibilities (docs/ARCHITECTURE.md section 9):
//   * display sync: bodies, sketch/construction/joint outputShape()s, component placements,
//     origin geometry, from Document signals (coalesced; see SceneModel);
//   * Fusion-style navigation (MMB pan, Shift+MMB orbit, wheel zoom at cursor), ViewCube,
//     ortho/perspective, display modes, adaptive grid, sketch mode;
//   * input translation to core::ViewerMouseEvent and ToolManager routing; default
//     selection (click, Ctrl/Shift, window/crossing rubber band, double-click tangent chain)
//     only when the active tool did not consume the event;
//   * preselection (hover) and selection highlighting, mirroring SelectionModel::filter().
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ViewerContext.h"
#include "viewer/SceneModel.h"
#include "viewer/ViewerMath.h"

#include <QPoint>
#include <QWidget>

#include <memory>
#include <set>
#include <string>
#include <vector>

class QPainter;

namespace fabricad::core {
class Document;
class ToolManager;
}  // namespace fabricad::core

namespace fabricad::viewer {

class ViewerListener;

class ViewerWidget : public QWidget, public fabricad::core::IViewerContext {
  Q_OBJECT
 public:
  explicit ViewerWidget(QWidget* parent = nullptr);
  ~ViewerWidget() override;

  void setDocument(fabricad::core::Document* doc);
  void setToolManager(fabricad::core::ToolManager* manager);
  fabricad::core::Document* document() const { return document_; }

  /// Create the OCCT GL view (call after the main window is shown). Returns false when the
  /// platform cannot host it (offscreen/wayland). Safe to call repeatedly.
  bool initializeView();
  /// True once the GL view exists and its context initialised.
  bool hasGlView() const;
  /// Diagnostic string (GL renderer) or the failure reason.
  QString glInfo() const;

  // --- display / navigation options -------------------------------------------------
  void setNavigationPreset(NavPreset p);
  NavPreset navigationPreset() const { return navPreset_; }
  /// Nav-bar modes: LMB drags orbit/pan/zoom instead of selecting (None = normal).
  void setLeftButtonNavigation(NavAction a);
  NavAction leftButtonNavigation() const { return lmbNav_; }
  /// Rotate the camera about the view centre (Shift+arrow keys: 15 degree steps).
  void orbitBy(double yawDeg, double pitchDeg, bool animate = true);
  void setReverseZoom(bool r) { reverseZoom_ = r; }
  bool reverseZoom() const { return reverseZoom_; }
  void setSelectionPriority(SelectionPriority p);
  SelectionPriority selectionPriority() const { return priority_; }
  /// Kinds the user can currently pick (tool filter & SelectionModel filter & priority).
  fabricad::core::ShapeKindMask effectiveFilter() const;

  // persistent visibility of non-body document objects (stored by the app in extension data)
  void setFeatureHidden(fabricad::core::ObjectId id, bool hidden);
  bool isFeatureHidden(fabricad::core::ObjectId id) const { return sceneOptions_.hiddenFeatures.count(id) > 0; }
  const std::set<fabricad::core::ObjectId>& hiddenFeatures() const { return sceneOptions_.hiddenFeatures; }
  void setHiddenFeatures(std::set<fabricad::core::ObjectId> ids);
  void setOriginVisible(bool v);
  bool isOriginVisible() const { return sceneOptions_.showOrigin; }
  /// Folder eyes: "Sketches", "Construction", "Joints".
  void setFolderVisible(const std::string& folder, bool v);
  bool isFolderVisible(const std::string& folder) const;
  /// Whether a feature's display output is visible right now (browser eye state).
  bool isFeatureDisplayed(fabricad::core::ObjectId id) const;

  /// Save the current camera as Home / go back to the default iso Home.
  void setCurrentViewAsHome();
  void resetHome();
  /// The parent for overlay widgets (command dialog, nav bar): this widget.
  QWidget* overlayParent() { return this; }

  // --- headless inspection (tests) ------------------------------------------------------
  const std::vector<SceneItem>& sceneItems() const { return sceneItems_; }
  int syncCount() const { return syncCount_; }
  /// Run a pending display sync now (normally coalesced to the next event loop turn).
  void syncNow();
  const core::NullViewerContext& fallbackContext() const { return null_; }

  // --- core::IViewerContext ------------------------------------------------------------
  core::PreviewHandle showPreviewShape(const TopoDS_Shape& shape, const core::PreviewStyle& style) override;
  core::PreviewHandle showPreviewLines(const std::vector<core::Vec3>& pts, const core::PreviewStyle& style) override;
  core::PreviewHandle showPreviewPolyline(const std::vector<core::Vec3>& pts, const core::PreviewStyle& style) override;
  core::PreviewHandle showPreviewPoints(const std::vector<core::Vec3>& pts, const core::PreviewStyle& style) override;
  core::PreviewHandle showPreviewText(const core::Vec3& at, const std::string& text, const core::PreviewStyle& style) override;
  void updatePreviewShape(core::PreviewHandle h, const TopoDS_Shape& shape) override;
  void updatePreviewLines(core::PreviewHandle h, const std::vector<core::Vec3>& pts) override;
  void removePreview(core::PreviewHandle h) override;
  void clearPreviews() override;
  void setHighlighted(const std::vector<core::ShapeRef>& refs) override;
  void setSelectionFilter(core::ShapeKindMask kinds) override;
  core::ShapeKindMask selectionFilter() const override { return toolFilter_; }
  void setSelectionEnabled(bool enabled) override { selectionEnabled_ = enabled; }
  bool pick(int x, int y, core::ShapeKindMask kinds, core::ShapeRef& out, gp_Pnt* hitPoint = nullptr) override;
  std::vector<core::ShapeRef> pickAll(int x, int y, core::ShapeKindMask kinds) override;
  void enterSketchMode(const gp_Ax3& plane, bool lookAt = true) override;
  void exitSketchMode() override;
  bool inSketchMode() const override { return sketchMode_; }
  gp_Ax3 sketchPlane() const override { return sketchPlane_; }
  void setGridVisible(bool visible) override;
  bool isGridVisible() const override { return gridVisible_; }
  void setGridSnap(bool enabled, double spacingMm) override;
  bool isGridSnap() const { return gridSnap_; }
  double gridSpacing() const;  ///< current snap/minor spacing (fixed or adaptive)
  void lookAt(const gp_Ax3& plane, bool animate = true) override;
  void setStandardView(core::StandardView view, bool animate = true) override;
  void fitAll(bool animate = true) override;
  void fitShapes(const std::vector<core::ShapeRef>& refs, bool animate = true) override;
  void setProjection(core::ProjectionMode mode) override;
  core::ProjectionMode projection() const override { return projection_; }
  void setDisplayMode(core::DisplayMode mode) override;
  core::DisplayMode displayMode() const override { return displayMode_; }
  gp_Dir viewDirection() const override;
  gp_Dir upDirection() const override;
  bool screenToPlane(int x, int y, const gp_Pln& plane, gp_Pnt& out) const override;
  bool screenRay(int x, int y, gp_Pnt& origin, gp_Dir& dir) const override;
  bool worldToScreen(const gp_Pnt& p, double& x, double& y) const override;
  double pixelSize(const gp_Pnt& at) const override;
  void setGhostedComponents(const std::vector<core::ObjectId>& componentIds) override;
  void setFeatureVisibilityOverride(core::ObjectId featureId, std::optional<bool> visible) override;
  void setCursor(core::CursorShape shape) override;
  void requestRedraw() override;
  QWidget* widget() override { return this; }

 signals:
  /// RMB click/gesture not consumed by a tool. `dragged`: RMB was dragged before release
  /// (marking-menu gesture; the app picks the slot from the drag direction).
  void markingMenuRequested(QPoint pressGlobal, QPoint releaseGlobal, bool dragged);
  /// RMB on the ViewCube.
  void viewCubeMenuRequested(QPoint globalPos);
  /// Double-click on an entity the tool did not consume (app: edit sketch / feature).
  void entityDoubleClicked(const fabricad::core::ShapeRef& ref);
  /// User/visibility state of non-body objects changed (browser refreshes eyes).
  void featureVisibilityChanged();
  void viewInitialized();
  void viewInitFailed(const QString& reason);
  /// Camera projection or display mode changed (nav bar refreshes checks).
  void displaySettingsChanged();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  friend class ViewerListener;
  struct Impl;

  void connectDocument();
  void scheduleSync();
  void scheduleOverlaySync();
  void doSync();
  void syncOverlays();
  void applySelectionModes();
  void applyGestureMap();
  void onFilterInputsChanged();

  // input handling (called by the OcctView listener)
  Qt::MouseButtons onMousePress(QMouseEvent* e);
  Qt::MouseButtons onMouseMove(QMouseEvent* e);
  Qt::MouseButtons onMouseRelease(QMouseEvent* e);
  void onMouseDoubleClick(QMouseEvent* e);
  double onWheel(QWheelEvent* e);
  void onLeave();
  void onGlInitialized();
  void onGlFailed(const QString& why);
  void beforeRedraw();
  void paintOverlay(QPainter& p);

  core::ViewerMouseEvent buildEvent(QMouseEvent* e, core::MouseButton button, bool withPick);
  bool detect(int x, int y, core::ShapeKindMask kinds, core::ShapeRef& out, gp_Pnt* hit, bool* onCube = nullptr);
  void defaultClickSelect(const core::ViewerMouseEvent& ev);
  void rectangleSelect(QPoint a, QPoint b, int modifiers);
  void updateHover(const core::ViewerMouseEvent& ev);
  void updateGrid(bool force);

  fabricad::core::Document* document_ = nullptr;
  fabricad::core::ToolManager* manager_ = nullptr;
  std::unique_ptr<Impl> d_;
  core::NullViewerContext null_;  ///< headless fallback (before/without GL)
  std::vector<QMetaObject::Connection> docConnections_;

  SceneModel scene_;
  SceneOptions sceneOptions_;
  std::vector<SceneItem> sceneItems_;
  int syncCount_ = 0;
  bool syncPending_ = false;
  bool overlayPending_ = false;

  core::ShapeKindMask toolFilter_ = core::kAllKinds;
  SelectionPriority priority_ = SelectionPriority::Default;
  bool selectionEnabled_ = true;
  NavPreset navPreset_ = NavPreset::Fusion;
  NavAction lmbNav_ = NavAction::None;
  bool reverseZoom_ = false;

  bool sketchMode_ = false;
  gp_Ax3 sketchPlane_;
  bool gridVisible_ = true;
  bool gridSnap_ = false;
  double gridSnapSpacing_ = 0.0;  ///< 0 = adaptive
  double gridStep_ = 10.0;        ///< current displayed minor step
  core::ProjectionMode projection_ = core::ProjectionMode::Orthographic;
  core::DisplayMode displayMode_ = core::DisplayMode::ShadedEdges;

  std::vector<core::ShapeRef> highlighted_;
  std::optional<core::ShapeRef> hoverRef_;  ///< preselection the viewer itself set

  // mouse state
  bool leftDown_ = false, leftConsumed_ = false, rubber_ = false, suppressRelease_ = false;
  bool rightDown_ = false, rightConsumed_ = false, rightDragged_ = false, rightOnCube_ = false;
  bool navigating_ = false;
  QPoint pressPos_, rubberEnd_, rightPressPos_;
};

/// Call once before QApplication is constructed: forces the xcb platform when unset
/// (OCCT here is GLX-only), shares GL contexts and sets the default surface format.
void setupGlPlatform();

}  // namespace fabricad::viewer
