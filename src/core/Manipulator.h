// FabriCAD core: on-canvas drag manipulators, headless math layer.
//
// A manipulator is a gizmo the user drags in the 3D view to change one value of the active
// command (Fusion 360: the extrude arrow, the revolve ring, the move triad). The viewer owns
// the pixels; everything that decides *what the drag means* lives here, GL-free, so it is
// unit-tested with plain rays and reused by core::NullViewerContext (tests) and
// viewer::ViewerWidget (OCCT):
//
//   ManipulatorSpec       what to show: kind, frame, size, enabled handles, value, snapping, limits
//   ManipulatorModel      one gizmo: glyph geometry, ray hit test, drag -> value/transform mapping
//   ManipulatorController the set of live gizmos of a view: hover, drag state, event dispatch
//   ManipulatorEvent      what a tool receives through its ManipulatorCallback
//
// Kinds and their value:
//   Linear   arrow along frame Z, placed at origin + Z * value (mm). Dragging moves it along the
//            axis; the value may go negative (the extrude distance flips sides) unless clamped.
//   Angular  ring of radius `size` in the frame XY plane; a handle sphere sits at `value` degrees
//            from +X (right-handed about Z). Dragging turns it; angles accumulate past 180.
//   Triad    move/rotate gizmo: three arrows, three rings, three plane squares; the event carries
//            the delta transform since the drag started (world) and the handle's amount.
//   Point    a free sphere; dragging moves it in the view plane (or the frame XY plane when
//            `planar`), the event carries the new position.
// Snapping (Fusion-like): the dragged value snaps to a 1-2-5 increment derived from the zoom
// level (`increment` = 0) or to the given increment; Shift makes the increment ten times
// coarser, Ctrl drags freely. Values are clamped to [minValue, maxValue] when set.
// Lengths are mm, angles degrees, screen quantities pixels (sizes derive from pixelSize()).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fabricad::core {

enum class ManipulatorKind { Linear, Angular, Triad, Point };
const char* manipulatorKindName(ManipulatorKind k);

/// Handles a gizmo can expose. Linear uses MoveZ, Angular RotZ, Point Point, Triad any subset.
enum class ManipulatorHandle { None, MoveX, MoveY, MoveZ, RotX, RotY, RotZ, PlaneXY, PlaneYZ, PlaneZX, Point };
enum ManipulatorHandleMask : unsigned {
  kHandleMoveX = 1u << 0, kHandleMoveY = 1u << 1, kHandleMoveZ = 1u << 2,
  kHandleRotX = 1u << 3, kHandleRotY = 1u << 4, kHandleRotZ = 1u << 5,
  kHandlePlaneXY = 1u << 6, kHandlePlaneYZ = 1u << 7, kHandlePlaneZX = 1u << 8,
  kHandlePoint = 1u << 9,
  kHandleMoves = kHandleMoveX | kHandleMoveY | kHandleMoveZ,
  kHandleRotations = kHandleRotX | kHandleRotY | kHandleRotZ,
  kHandlePlanes = kHandlePlaneXY | kHandlePlaneYZ | kHandlePlaneZX,
  kHandleTriadAll = kHandleMoves | kHandleRotations | kHandlePlanes,
};
unsigned manipulatorHandleMask(ManipulatorHandle h);
const char* manipulatorHandleName(ManipulatorHandle h);
bool isMoveHandle(ManipulatorHandle h);
bool isRotationHandle(ManipulatorHandle h);
bool isPlaneHandle(ManipulatorHandle h);

/// Everything a tool says about a gizmo. The viewer fills in nothing else.
struct ManipulatorSpec {
  ManipulatorKind kind = ManipulatorKind::Linear;
  /// Anchor frame (world): origin, Z = drag axis / rotation axis / plane normal, X = zero angle.
  gp_Ax3 frame{gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)};
  /// Size in mm (arrow length, ring radius, triad arrow length). 0 = automatic (kAutoSizePixels on screen).
  double size = 0.0;
  /// Triad only: enabled handles (ManipulatorHandleMask bits).
  unsigned handles = kHandleTriadAll;
  /// Current value (Linear: mm along Z; Angular: degrees from X). Ignored by Triad/Point.
  double value = 0.0;
  /// Snap increment (mm / deg). 0 = automatic from the zoom level.
  double increment = 0.0;
  std::optional<double> minValue;  ///< clamp (e.g. 0 for a radius)
  std::optional<double> maxValue;
  /// Point: drag in the frame XY plane (true) or in the view plane (false).
  bool planar = false;
  /// Caption for the value readout ("Distance"); empty = none.
  std::string label;
  /// Show the value next to the handle while dragging.
  bool showValue = true;
  /// Caller's identifier, echoed in events (which property this gizmo drives).
  int tag = 0;
};

using ManipulatorId = int;
constexpr ManipulatorId kInvalidManipulator = 0;

enum class ManipulatorPhase { Hover, Leave, DragBegin, Drag, DragEnd, DragCancel };
const char* manipulatorPhaseName(ManipulatorPhase p);

struct ManipulatorEvent {
  ManipulatorId id = kInvalidManipulator;
  int tag = 0;
  ManipulatorPhase phase = ManipulatorPhase::Hover;
  ManipulatorHandle handle = ManipulatorHandle::None;
  /// Linear/Angular: the new value. Triad: amount of the active handle since DragBegin
  /// (mm for moves/planes, degrees for rotations). Point: distance moved.
  double value = 0.0;
  /// Value when the drag started (Linear/Angular).
  double startValue = 0.0;
  /// World delta transform since DragBegin (all kinds; identity on Hover/Leave).
  gp_Trsf transform;
  /// Point: the new position. Others: the handle position.
  gp_Pnt point;
  int modifiers = 0;  ///< KeyModifier bits held
};
using ManipulatorCallback = std::function<void(const ManipulatorEvent&)>;

/// Renderable piece of a gizmo. The viewer draws it; NullViewerContext only records it.
struct ManipulatorGlyph {
  enum class Shape { Segments, Polyline, Cylinder, Cone, Sphere, Torus, Quad };
  ManipulatorHandle handle = ManipulatorHandle::None;
  Shape shape = Shape::Segments;
  /// Cylinder/Cone: origin = base centre, Z = axis. Sphere: origin = centre. Torus: centre + axis.
  gp_Ax3 frame;
  double radius = 0.0;   ///< cylinder/cone base radius, sphere radius, torus major radius
  double radius2 = 0.0;  ///< torus minor radius
  double length = 0.0;   ///< cylinder/cone height
  std::vector<Vec3> points;  ///< Segments (pairs), Polyline, Quad (4 corners)
  std::array<float, 4> color{0.25f, 0.55f, 0.95f, 1.0f};
  bool dashed = false;
};

/// Colours: default handle, hovered/active handle, triad X/Y/Z/plane.
std::array<float, 4> manipulatorColor(ManipulatorHandle h, ManipulatorKind kind, bool highlighted);
constexpr std::array<float, 4> kManipulatorHighlight{1.0f, 0.78f, 0.15f, 1.0f};

/// On-screen size of an automatically sized gizmo and the pick tolerance, in pixels.
constexpr double kAutoSizePixels = 70.0;
constexpr double kHitPixels = 8.0;

/// One gizmo: geometry, picking and drag mapping. Pure math; all rays are world coordinates and
/// `pixelSize` is the world size of one pixel at the gizmo (IViewerContext::pixelSize()).
class ManipulatorModel {
 public:
  struct Hit {
    ManipulatorHandle handle = ManipulatorHandle::None;
    double distance = 0.0;  ///< mm from the ray to the glyph (0 = inside a filled handle)
    explicit operator bool() const { return handle != ManipulatorHandle::None; }
  };

  explicit ManipulatorModel(ManipulatorSpec spec = {});

  const ManipulatorSpec& spec() const { return spec_; }
  /// Replace the spec. While a drag is in progress the dragged value is kept (the tool is being
  /// told the value it just produced) but frame, size, handles and limits are taken over.
  void setSpec(const ManipulatorSpec& spec);
  /// Dialog -> gizmo: move the handle to a value (ignored while that handle is being dragged).
  void setValue(double value);
  double value() const { return spec_.value; }

  double effectiveSize(double pixelSize) const;
  /// Frame the handles are drawn in: Linear is translated along Z by the value, others = spec frame.
  gp_Ax3 activeFrame() const;
  /// World position of a handle (label anchor). Linear: arrow base; Angular: handle sphere;
  /// Point: the sphere; Triad: the arrow tip / ring point / square centre.
  gp_Pnt handlePoint(ManipulatorHandle h, double pixelSize) const;
  /// Handles this model exposes (enabled mask for Triad, the single handle otherwise).
  std::vector<ManipulatorHandle> handles() const;
  /// Geometry to draw; `highlight` is drawn in the highlight colour.
  std::vector<ManipulatorGlyph> glyphs(double pixelSize, ManipulatorHandle highlight = ManipulatorHandle::None) const;

  /// Closest handle within kHitPixels * pixelSize of the ray.
  Hit hit(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, double pixelSize) const;

  /// Start dragging `h` from a ray. False when the ray cannot drive the handle (e.g. parallel).
  bool beginDrag(ManipulatorHandle h, const gp_Pnt& rayOrigin, const gp_Dir& rayDir, double pixelSize);
  /// Map the current ray to a value/transform. False when the ray cannot drive the handle (state unchanged).
  bool dragTo(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, int modifiers, double pixelSize);
  void endDrag();
  /// Abort: restores the value at DragBegin.
  void cancelDrag();
  bool isDragging() const { return active_ != ManipulatorHandle::None; }
  ManipulatorHandle activeHandle() const { return active_; }
  double startValue() const { return startValue_; }
  /// World delta since DragBegin (identity when idle).
  const gp_Trsf& dragTransform() const { return transform_; }
  /// Point kind: current position (spec origin when idle). Others: active handle point.
  gp_Pnt point() const { return point_; }
  /// Triad: amount of the active handle (mm / deg); Linear/Angular: value - startValue.
  double dragAmount() const { return amount_; }

  // --- snapping ------------------------------------------------------------------------------
  /// Round to a multiple of `increment` (increment <= 0: unchanged).
  static double snapValue(double value, double increment);
  /// 1-2-5 series step >= value (value <= 0 -> 1).
  static double niceIncrement(double value);
  /// Angular increments from {0.1 .. 90} degrees, the first >= value.
  static double niceAngleIncrement(double value);
  /// Automatic increment for the zoom level: about kSnapPixels of mouse travel per step.
  static double autoIncrement(double pixelSize, bool angular, double ringRadius);
  /// The increment the current drag uses: spec increment or automatic, x10 with Shift, 0 with Ctrl.
  double incrementFor(int modifiers, double pixelSize) const;
  static constexpr double kSnapPixels = 3.0;

  // --- ray helpers (public for tests / other math) --------------------------------------------
  /// Parameter of the point on the axis line closest to the ray (false when parallel).
  static bool closestAxisParameter(const gp_Pnt& axisOrigin, const gp_Dir& axisDir, const gp_Pnt& rayOrigin,
                                   const gp_Dir& rayDir, double& t);
  static bool rayPlane(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, const gp_Pnt& planeOrigin, const gp_Dir& normal,
                       gp_Pnt& out);
  static double raySegmentDistance(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, const gp_Pnt& a, const gp_Pnt& b);
  static double rayPointDistance(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, const gp_Pnt& p);

 private:
  gp_Dir axisOf(ManipulatorHandle h) const;
  void planeAxes(ManipulatorHandle h, gp_Dir& a, gp_Dir& b) const;
  bool angleOnPlane(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, const gp_Pnt& origin, const gp_Dir& normal,
                    const gp_Dir& xDir, double& angleDeg) const;
  double applyLimits(double v) const;
  void arrowGlyphs(std::vector<ManipulatorGlyph>& out, ManipulatorHandle h, const gp_Pnt& base, const gp_Dir& dir,
                   double size, const std::array<float, 4>& color) const;

  ManipulatorSpec spec_;
  ManipulatorHandle active_ = ManipulatorHandle::None;
  double startValue_ = 0.0;
  double startParam_ = 0.0;
  gp_Pnt startPoint_;
  gp_Dir dragNormal_{0, 0, 1};
  double lastAngle_ = 0.0;
  double accumAngle_ = 0.0;
  gp_Trsf transform_;
  gp_Pnt point_;
  double amount_ = 0.0;
};

/// The live gizmos of one view. Hosts feed it pointer events as world rays (ViewerWidget from
/// Qt, NullViewerContext from test pixels through its own camera) and redraw when the change
/// listener fires. Callbacks may add/update/remove manipulators re-entrantly.
class ManipulatorController {
 public:
  using PixelSizeFn = std::function<double(const gp_Pnt&)>;

  ManipulatorId add(const ManipulatorSpec& spec, ManipulatorCallback callback);
  void update(ManipulatorId id, const ManipulatorSpec& spec);
  void setValue(ManipulatorId id, double value);
  void remove(ManipulatorId id);
  void clear();
  bool has(ManipulatorId id) const { return models_.count(id) > 0; }
  std::size_t count() const { return models_.size(); }
  std::vector<ManipulatorId> ids() const;
  const ManipulatorModel* model(ManipulatorId id) const;
  ManipulatorModel* model(ManipulatorId id);
  /// First manipulator with this tag (kInvalidManipulator if none).
  ManipulatorId findByTag(int tag) const;

  // --- interaction --------------------------------------------------------------------------------
  /// Hover / press / drag / release with a world ray. `pixelSize` gives the pixel size at a point.
  /// press() returns true when a handle was grabbed (the host must not start selection); move()
  /// returns true while dragging or hovering a handle; release() returns true when a drag ended.
  bool press(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, int modifiers, const PixelSizeFn& pixelSize);
  bool move(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, int modifiers, const PixelSizeFn& pixelSize);
  bool release(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, int modifiers, const PixelSizeFn& pixelSize);
  /// Abort the drag (Esc): the value returns to what it was at DragBegin, the tool gets DragCancel.
  void cancelDrag();
  /// Pointer left the view: clear the hover.
  void leave();

  bool isDragging() const { return dragging_ != kInvalidManipulator; }
  ManipulatorId draggingId() const { return dragging_; }
  ManipulatorId hoveredId() const { return hovered_; }
  ManipulatorHandle hoveredHandle() const { return hoveredHandle_; }
  /// Handle to highlight for a given manipulator (dragged or hovered one).
  ManipulatorHandle highlightFor(ManipulatorId id) const;

  /// Called whenever something visible changed (added/updated/removed/hover/drag). One listener.
  void setChangeListener(std::function<void()> fn) { onChange_ = std::move(fn); }

  /// Number of events dispatched so far (tests).
  int eventCount() const { return eventCount_; }

 private:
  struct Entry {
    ManipulatorModel model;
    ManipulatorCallback callback;
  };
  struct Target {
    ManipulatorId id = kInvalidManipulator;
    ManipulatorModel::Hit hit;
  };
  Target hitTest(const gp_Pnt& ro, const gp_Dir& rd, const PixelSizeFn& pixelSize) const;
  void dispatch(ManipulatorId id, ManipulatorPhase phase, int modifiers, double pixelSize);
  void changed();
  void flushRemovals();

  std::map<ManipulatorId, Entry> models_;
  ManipulatorId next_ = 1;
  ManipulatorId hovered_ = kInvalidManipulator;
  ManipulatorHandle hoveredHandle_ = ManipulatorHandle::None;
  ManipulatorId dragging_ = kInvalidManipulator;
  int dispatching_ = 0;
  std::vector<ManipulatorId> pendingRemove_;
  bool pendingClear_ = false;
  std::function<void()> onChange_;
  int eventCount_ = 0;
};

}  // namespace fabricad::core
