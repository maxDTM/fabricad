// FabriCAD core: IViewerContext, the 3D view as seen by tools and modules.
//
// The viewer module (src/viewer) implements this on top of OCCT AIS/V3d. Modules code
// against the interface only, so everything except the viewer can be built and
// unit-tested without OpenGL (NullViewerContext below).
//
// Division of labour:
//   * Persistent display of bodies, sketches, construction geometry and components is
//     the viewer's job: it listens to Document signals (bodyChanged, featureStatusChanged,
//     componentChanged, documentReset) and keeps AIS objects in sync. Tools do not
//     display document geometry themselves.
//   * Tools use the *preview* API for transient graphics (rubber-band lines, preview
//     solids, inference glyphs, dimension text). Previews vanish when a tool finishes
//     (ToolManager calls clearPreviews()).
//   * Picking goes through the SelectionModel (viewer writes picks; tools read them in
//     selectionChanged()) or through pick() for explicit hit tests.
// Coordinates: screen x,y are widget pixels (origin top-left). World coordinates are mm.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Manipulator.h"
#include "core/ShapeRef.h"
#include "core/Types.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <array>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

class QWidget;

namespace fabricad::core {

enum class StandardView { Front, Back, Left, Right, Top, Bottom, Iso, Home };
enum class DisplayMode { Shaded, ShadedEdges, Wireframe, HiddenLine };
enum class ProjectionMode { Orthographic, Perspective };
enum class CursorShape { Arrow, Cross, Pick, Hand, Busy, Forbidden };

/// What a preview item means (docs/plans/ui-redesign.md 2.3, 4.5). The viewer resolves the role
/// through its theme palette (ViewerPalette); the RGBA `color` stays as the fallback for
/// headless tests and for previews without a role. A preset whose colour a tool changes keeps its
/// role only for the operation roles (Live, Cut, NewBody); SketchFree, Inference and Dimension
/// fall back to the tool's colour when it differs from the preset's.
enum class ColorRole : std::uint8_t {
  None,        ///< use `color` as given
  Live,        ///< live operation preview (extrude/revolve join, intersect): gold two-tone + halo
  Cut,         ///< cut preview: error hue, dashed edge
  NewBody,     ///< new-body preview: info hue
  SketchFree,  ///< rubber-band / under-constrained sketch curve
  Inference,   ///< guide lines and glyphs (neutral)
  Dimension,   ///< dimension lines and text
  // sketch session (curve states by constraint status, docs/plans/ui-redesign.md 4.5 sk.*)
  SketchConstrained,   ///< fully constrained curve or point
  SketchConstruction,  ///< construction curve (dashed)
  SketchProjected,     ///< projected / referenced geometry
  SketchFixed,         ///< fixed (locked) geometry
  SketchConflict,      ///< over-constrained or failed; also the trim/remove preview
  SketchSelected,      ///< selected curve, point or dimension (selection edge)
  SketchGlyph,         ///< constraint glyphs, spline control polygons, dynamic text (neutral)
  SketchProfile,       ///< closed-profile tint inside the open sketch (no boundary lines)
  SketchOrigin,        ///< the sketch origin point
  Preselect,           ///< hover highlight and snap marker (preselection edge)
};

/// Appearance of a preview item.
struct PreviewStyle {
  std::array<float, 4> color{1.0f, 0.6f, 0.2f, 0.5f};  ///< RGBA 0..1 (alpha = opacity)
  float lineWidth = 1.5f;
  bool shaded = true;      ///< shapes: shaded faces (false = edges only)
  bool dashed = false;     ///< lines: dashed pattern (inference lines, construction)
  float pointSize = 5.0f;  ///< points
  float textHeight = 12.0f;  ///< text, in pixels
  bool onTop = false;      ///< draw over everything (glyphs, dimensions)
  ColorRole role = ColorRole::None;

  static PreviewStyle join() { PreviewStyle s{{0.4f, 0.8f, 0.4f, 0.5f}}; s.role = ColorRole::Live; return s; }
  static PreviewStyle cut() { PreviewStyle s{{0.88f, 0.25f, 0.25f, 0.5f}}; s.role = ColorRole::Cut; return s; }
  static PreviewStyle newBody() { PreviewStyle s{{0.35f, 0.6f, 1.0f, 0.5f}}; s.role = ColorRole::NewBody; return s; }
  static PreviewStyle sketchLine() { PreviewStyle s{{0.12f, 0.44f, 0.85f, 1.0f}}; s.shaded = false; s.lineWidth = 2.0f; s.onTop = true; s.role = ColorRole::SketchFree; return s; }
  static PreviewStyle inference() { PreviewStyle s{{0.4f, 0.4f, 0.4f, 1.0f}}; s.dashed = true; s.onTop = true; s.role = ColorRole::Inference; return s; }
};

using PreviewHandle = int;
constexpr PreviewHandle kInvalidPreview = 0;

class IViewerContext {
 public:
  virtual ~IViewerContext() = default;

  // --- transient preview graphics -------------------------------------------
  virtual PreviewHandle showPreviewShape(const TopoDS_Shape& shape, const PreviewStyle& style) = 0;
  /// Pairs of points (p0,p1, p2,p3, ...) drawn as independent segments.
  virtual PreviewHandle showPreviewLines(const std::vector<Vec3>& segmentPoints, const PreviewStyle& style) = 0;
  /// Connected polyline.
  virtual PreviewHandle showPreviewPolyline(const std::vector<Vec3>& points, const PreviewStyle& style) = 0;
  virtual PreviewHandle showPreviewPoints(const std::vector<Vec3>& points, const PreviewStyle& style) = 0;
  /// Screen-aligned text at a world position.
  virtual PreviewHandle showPreviewText(const Vec3& at, const std::string& text, const PreviewStyle& style) = 0;
  /// Replace the geometry of an existing preview (same style). No-op for unknown handles.
  virtual void updatePreviewShape(PreviewHandle h, const TopoDS_Shape& shape) = 0;
  virtual void updatePreviewLines(PreviewHandle h, const std::vector<Vec3>& segmentPoints) = 0;
  virtual void removePreview(PreviewHandle h) = 0;
  virtual void clearPreviews() = 0;

  // --- highlighting / selection ----------------------------------------------
  /// Persistent highlight of document entities (e.g. a dialog's active selection set).
  /// Replaces the previous highlight set. Empty vector clears.
  virtual void setHighlighted(const std::vector<ShapeRef>& refs) = 0;
  /// Which kinds the viewer lets the user pick (mirrors SelectionModel::filter()).
  virtual void setSelectionFilter(ShapeKindMask kinds) = 0;
  virtual ShapeKindMask selectionFilter() const = 0;
  /// Temporarily disable picking (e.g. while dragging a manipulator).
  virtual void setSelectionEnabled(bool enabled) = 0;
  /// Explicit hit test at a pixel. Returns false if nothing pickable is there.
  virtual bool pick(int x, int y, ShapeKindMask kinds, ShapeRef& out, gp_Pnt* hitPoint = nullptr) = 0;
  /// All pickable entities at a pixel (for "select other").
  virtual std::vector<ShapeRef> pickAll(int x, int y, ShapeKindMask kinds) = 0;

  // --- on-canvas manipulators (core/Manipulator.h) ------------------------------
  // A tool shows a gizmo bound to one of its values (extrude arrow, revolve ring, move
  // triad). The viewer draws it, picks it ahead of selection, highlights it on hover and
  // reports drags through the callback; the tool updates its value / preview from the
  // events and calls setManipulatorValue() when the dialog changes the value. All
  // manipulators are removed when the tool finishes (ToolManager calls clearManipulators()).
  virtual ManipulatorId showManipulator(const ManipulatorSpec& spec, ManipulatorCallback callback) = 0;
  /// Re-anchor / re-configure (frame, size, handles, limits). Keeps the value of a handle that is being dragged.
  virtual void updateManipulator(ManipulatorId id, const ManipulatorSpec& spec) = 0;
  /// Dialog -> gizmo sync. Ignored while that gizmo is being dragged.
  virtual void setManipulatorValue(ManipulatorId id, double value) = 0;
  virtual void removeManipulator(ManipulatorId id) = 0;
  virtual void clearManipulators() = 0;
  /// The headless interaction layer behind the gizmos (hover/drag state, models). Tests inspect it.
  virtual ManipulatorController& manipulators() = 0;
  const ManipulatorController& manipulators() const { return const_cast<IViewerContext*>(this)->manipulators(); }
  /// Pointer routing for the gizmos in widget pixels, built on screenRay()/pixelSize(). Viewer
  /// implementations call these before the active tool sees the event; headless tests drive
  /// drags with them. Return true when the event was consumed (a handle is hovered/dragged).
  bool manipulatorPress(int x, int y, int modifiers = 0);
  bool manipulatorMove(int x, int y, int modifiers = 0);
  bool manipulatorRelease(int x, int y, int modifiers = 0);
  /// Esc while dragging: restore the value at DragBegin.
  void manipulatorCancel();
  bool isManipulatorDragging() const { return manipulators().isDragging(); }

  // --- sketch mode -------------------------------------------------------------
  /// Look at the plane, show the grid, restrict picking to sketch entities + bodies,
  /// and make ViewerMouseEvent::planePoint valid. Idempotent.
  virtual void enterSketchMode(const gp_Ax3& plane, bool lookAt = true) = 0;
  virtual void exitSketchMode() = 0;
  virtual bool inSketchMode() const = 0;
  virtual gp_Ax3 sketchPlane() const = 0;
  virtual void setGridVisible(bool visible) = 0;
  virtual bool isGridVisible() const = 0;
  /// Snap to grid in sketch mode (viewer applies to planePoint when enabled).
  virtual void setGridSnap(bool enabled, double spacingMm) = 0;

  // --- camera -------------------------------------------------------------------
  virtual void lookAt(const gp_Ax3& plane, bool animate = true) = 0;
  virtual void setStandardView(StandardView view, bool animate = true) = 0;
  virtual void fitAll(bool animate = true) = 0;
  virtual void fitShapes(const std::vector<ShapeRef>& refs, bool animate = true) = 0;
  virtual void setProjection(ProjectionMode mode) = 0;
  virtual ProjectionMode projection() const = 0;
  virtual void setDisplayMode(DisplayMode mode) = 0;
  virtual DisplayMode displayMode() const = 0;
  /// Direction from the scene towards the eye (unit).
  virtual gp_Dir viewDirection() const = 0;
  virtual gp_Dir upDirection() const = 0;

  // --- coordinate mapping ----------------------------------------------------------
  virtual bool screenToPlane(int x, int y, const gp_Pln& plane, gp_Pnt& out) const = 0;
  virtual bool screenRay(int x, int y, gp_Pnt& origin, gp_Dir& dir) const = 0;
  virtual bool worldToScreen(const gp_Pnt& p, double& x, double& y) const = 0;
  /// World size of one pixel at `at` (for snap tolerances: 8 px -> 8*pixelSize mm).
  virtual double pixelSize(const gp_Pnt& at) const = 0;
  /// Pixel size the viewer draws and hit-tests gizmos with (it may scale them up). Use it when
  /// placing UI on a gizmo handle so it lines up with what is drawn.
  virtual double gizmoPixelSize(const gp_Pnt& at) const { return pixelSize(at); }

  // --- section analysis (INSPECT > Section) -------------------------------------------------
  /// A live clipping plane through the bodies: the half-space the plane normal points into is
  /// cut away and the cut is capped. It stays on after the Section tool finishes (Fusion's
  /// section analysis), until it is cleared (nullopt). Only bodies are clipped; sketches,
  /// construction geometry, previews and gizmos are not. The base class keeps the state, so
  /// headless contexts report it; ViewerWidget also draws it.
  virtual void setSectionPlane(const std::optional<gp_Pln>& plane) { sectionPlane_ = plane; }
  virtual std::optional<gp_Pln> sectionPlane() const { return sectionPlane_; }

  // --- misc -----------------------------------------------------------------------------
  /// In-context editing: components not in the list are drawn ghosted (30% opacity). Empty = none ghosted.
  virtual void setGhostedComponents(const std::vector<ObjectId>& componentIds) = 0;
  /// Per-feature display override (hide a sketch while editing it, etc.). nullopt = follow the document.
  virtual void setFeatureVisibilityOverride(ObjectId featureId, std::optional<bool> visible) = 0;
  /// Persistent display state of a sketch/construction feature (the browser eye). Unlike the
  /// override above, the user can show it again from the browser. ViewerWidget implements both
  /// (same signatures); the defaults do nothing.
  virtual void setFeatureHidden(ObjectId featureId, bool hidden) { (void)featureId; (void)hidden; }
  virtual bool isFeatureHidden(ObjectId featureId) const { (void)featureId; return false; }
  virtual void setCursor(CursorShape shape) = 0;
  virtual void requestRedraw() = 0;
  /// Host widget (nullptr in headless contexts). Use as dialog parent.
  virtual QWidget* widget() { return nullptr; }

 protected:
  std::optional<gp_Pln> sectionPlane_;
};

/// Headless implementation: records calls, keeps state, draws nothing. Used by unit tests
/// of sketch/part/assembly tools and by the app before the GL view exists.
class NullViewerContext : public IViewerContext {
 public:
  struct PreviewItem {
    std::string kind;  ///< "shape", "lines", "polyline", "points", "text"
    TopoDS_Shape shape;
    std::vector<Vec3> points;
    std::string text;
    PreviewStyle style;
  };

  PreviewHandle showPreviewShape(const TopoDS_Shape& shape, const PreviewStyle& style) override;
  PreviewHandle showPreviewLines(const std::vector<Vec3>& pts, const PreviewStyle& style) override;
  PreviewHandle showPreviewPolyline(const std::vector<Vec3>& pts, const PreviewStyle& style) override;
  PreviewHandle showPreviewPoints(const std::vector<Vec3>& pts, const PreviewStyle& style) override;
  PreviewHandle showPreviewText(const Vec3& at, const std::string& text, const PreviewStyle& style) override;
  void updatePreviewShape(PreviewHandle h, const TopoDS_Shape& shape) override;
  void updatePreviewLines(PreviewHandle h, const std::vector<Vec3>& pts) override;
  void removePreview(PreviewHandle h) override;
  void clearPreviews() override;
  void setHighlighted(const std::vector<ShapeRef>& refs) override { highlighted_ = refs; }
  void setSelectionFilter(ShapeKindMask kinds) override { filter_ = kinds; }
  ShapeKindMask selectionFilter() const override { return filter_; }
  void setSelectionEnabled(bool e) override { selectionEnabled_ = e; }
  bool pick(int, int, ShapeKindMask, ShapeRef&, gp_Pnt*) override { return false; }
  std::vector<ShapeRef> pickAll(int, int, ShapeKindMask) override { return {}; }
  ManipulatorId showManipulator(const ManipulatorSpec& spec, ManipulatorCallback callback) override {
    return manipulators_.add(spec, std::move(callback));
  }
  void updateManipulator(ManipulatorId id, const ManipulatorSpec& spec) override { manipulators_.update(id, spec); }
  void setManipulatorValue(ManipulatorId id, double value) override { manipulators_.setValue(id, value); }
  void removeManipulator(ManipulatorId id) override { manipulators_.remove(id); }
  void clearManipulators() override { manipulators_.clear(); }
  ManipulatorController& manipulators() override { return manipulators_; }
  void enterSketchMode(const gp_Ax3& plane, bool lookAt = true) override;
  void exitSketchMode() override { sketchMode_ = false; }
  bool inSketchMode() const override { return sketchMode_; }
  gp_Ax3 sketchPlane() const override { return sketchPlane_; }
  void setGridVisible(bool v) override { grid_ = v; }
  bool isGridVisible() const override { return grid_; }
  void setGridSnap(bool enabled, double spacing) override { gridSnap_ = enabled; gridSpacing_ = spacing; }
  void lookAt(const gp_Ax3& plane, bool) override { viewPlane_ = plane; }
  void setStandardView(StandardView v, bool) override;
  void fitAll(bool) override { ++fitCount_; }
  void fitShapes(const std::vector<ShapeRef>&, bool) override { ++fitCount_; }
  void setProjection(ProjectionMode m) override { projection_ = m; }
  ProjectionMode projection() const override { return projection_; }
  void setDisplayMode(DisplayMode m) override { displayMode_ = m; }
  DisplayMode displayMode() const override { return displayMode_; }
  gp_Dir viewDirection() const override { return viewPlane_.Direction(); }
  gp_Dir upDirection() const override { return viewPlane_.YDirection(); }
  /// Maps pixel (x,y) to viewPlane_ (or sketch plane) coordinates at 1 mm/pixel, y up.
  bool screenToPlane(int x, int y, const gp_Pln& plane, gp_Pnt& out) const override;
  bool screenRay(int x, int y, gp_Pnt& origin, gp_Dir& dir) const override;
  bool worldToScreen(const gp_Pnt& p, double& x, double& y) const override;
  double pixelSize(const gp_Pnt&) const override { return pixelSize_; }
  void setGhostedComponents(const std::vector<ObjectId>& ids) override { ghosted_ = ids; }
  void setFeatureVisibilityOverride(ObjectId id, std::optional<bool> v) override;
  void setFeatureHidden(ObjectId id, bool hidden) override {
    if (hidden) hiddenFeatures_.insert(id);
    else hiddenFeatures_.erase(id);
  }
  bool isFeatureHidden(ObjectId id) const override { return hiddenFeatures_.count(id) > 0; }
  void setCursor(CursorShape c) override { cursor_ = c; }
  void requestRedraw() override { ++redrawCount_; }

  // test inspection
  const std::map<PreviewHandle, PreviewItem>& previews() const { return previews_; }
  const std::vector<ShapeRef>& highlighted() const { return highlighted_; }
  const std::vector<ObjectId>& ghosted() const { return ghosted_; }
  int redrawCount() const { return redrawCount_; }
  int fitCount() const { return fitCount_; }
  CursorShape cursor() const { return cursor_; }
  void setPixelSize(double s) { pixelSize_ = s; }
  const std::map<ObjectId, std::optional<bool>>& visibilityOverrides() const { return overrides_; }

 private:
  std::map<PreviewHandle, PreviewItem> previews_;
  PreviewHandle nextHandle_ = 1;
  ManipulatorController manipulators_;
  std::vector<ShapeRef> highlighted_;
  ShapeKindMask filter_ = kAllKinds;
  bool selectionEnabled_ = true;
  bool sketchMode_ = false;
  gp_Ax3 sketchPlane_;
  gp_Ax3 viewPlane_{gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)};
  bool grid_ = false;
  bool gridSnap_ = false;
  double gridSpacing_ = 1.0;
  ProjectionMode projection_ = ProjectionMode::Orthographic;
  DisplayMode displayMode_ = DisplayMode::ShadedEdges;
  double pixelSize_ = 1.0;
  std::vector<ObjectId> ghosted_;
  std::map<ObjectId, std::optional<bool>> overrides_;
  std::set<ObjectId> hiddenFeatures_;
  CursorShape cursor_ = CursorShape::Arrow;
  int redrawCount_ = 0;
  int fitCount_ = 0;
};

}  // namespace fabricad::core
