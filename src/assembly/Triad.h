// FabriCAD assembly: triad manipulator math (Move/Rotate component gizmo).
// Pure geometry: handle geometry for preview graphics, ray hit testing and drag deltas.
// The tool draws it with IViewerContext::showPreviewLines and feeds ViewerMouseEvent rays in.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <string>
#include <vector>

namespace fabricad::assembly {

class Triad {
 public:
  enum class Handle { None, MoveX, MoveY, MoveZ, RotX, RotY, RotZ, PlaneXY, PlaneYZ, PlaneZX };
  enum HandleMask : unsigned {
    kMoveX = 1u << 0, kMoveY = 1u << 1, kMoveZ = 1u << 2,
    kRotX = 1u << 3, kRotY = 1u << 4, kRotZ = 1u << 5,
    kPlaneXY = 1u << 6, kPlaneYZ = 1u << 7, kPlaneZX = 1u << 8,
    kMoves = kMoveX | kMoveY | kMoveZ,
    kRotations = kRotX | kRotY | kRotZ,
    kPlanes = kPlaneXY | kPlaneYZ | kPlaneZX,
    kAll = kMoves | kRotations | kPlanes,
  };
  static unsigned maskOf(Handle h);
  static const char* handleName(Handle h);
  static std::array<float, 4> handleColor(Handle h);  ///< X red, Y green, Z blue, planes yellow
  static bool isMove(Handle h) { return h == Handle::MoveX || h == Handle::MoveY || h == Handle::MoveZ; }
  static bool isRotation(Handle h) { return h == Handle::RotX || h == Handle::RotY || h == Handle::RotZ; }
  static bool isPlane(Handle h) { return h == Handle::PlaneXY || h == Handle::PlaneYZ || h == Handle::PlaneZX; }

  explicit Triad(const gp_Ax3& frame = gp_Ax3(), double size = 20.0);

  const gp_Ax3& frame() const { return frame_; }
  void setFrame(const gp_Ax3& f) { frame_ = f; }
  double size() const { return size_; }
  void setSize(double s) { size_ = s; }
  unsigned enabledHandles() const { return enabled_; }
  void setEnabledHandles(unsigned mask) { enabled_ = mask; }
  bool isEnabled(Handle h) const { return (enabled_ & maskOf(h)) != 0; }

  /// Axis direction of a move/rotation handle (world).
  gp_Dir axis(Handle h) const;
  /// Preview geometry of a handle as independent segments (p0,p1, p2,p3, ...).
  std::vector<core::Vec3> handleSegments(Handle h) const;
  /// All enabled handles' segments grouped by handle.
  std::vector<std::pair<Handle, std::vector<core::Vec3>>> allSegments() const;

  /// Closest enabled handle within `tolerance` (mm) of the ray, or None.
  Handle hit(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, double tolerance) const;

  /// Start dragging a handle from a ray. Returns false if the ray cannot drive the handle.
  bool beginDrag(Handle h, const gp_Pnt& rayOrigin, const gp_Dir& rayDir);
  /// Delta transform (world) for the current ray: newPlacement = delta * placementAtDragStart.
  /// For moves the delta is a translation along the axis (or in the plane); for rotations a
  /// rotation about the handle axis through the triad origin. `snap` rounds (mm / degrees; 0 = off).
  bool dragTo(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, gp_Trsf& delta, double snap = 0.0) const;
  Handle activeHandle() const { return active_; }
  /// Amount of the current drag (mm for moves, degrees for rotations, 0 for planes).
  double dragValue() const { return value_; }
  void endDrag() { active_ = Handle::None; }

  // --- ray geometry helpers (public for tests / other tools) ---------------------
  /// Parameter of the point on the axis line closest to the ray (false when parallel).
  static bool closestAxisParameter(const gp_Pnt& axisOrigin, const gp_Dir& axisDir, const gp_Pnt& rayOrigin,
                                   const gp_Dir& rayDir, double& t);
  static bool rayPlane(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, const gp_Pnt& planeOrigin, const gp_Dir& normal,
                       gp_Pnt& out);
  static double raySegmentDistance(const gp_Pnt& rayOrigin, const gp_Dir& rayDir, const gp_Pnt& a, const gp_Pnt& b);

 private:
  /// The two in-plane axes of a plane handle (XY -> X,Y; YZ -> Y,Z; ZX -> Z,X).
  void planeAxes(Handle h, gp_Dir& a, gp_Dir& b) const;

  gp_Ax3 frame_;
  double size_;
  unsigned enabled_ = kAll;
  Handle active_ = Handle::None;
  double startParam_ = 0.0;
  gp_Pnt startPoint_;
  gp_Dir startRadial_{1, 0, 0};
  mutable double value_ = 0.0;
};

}  // namespace fabricad::assembly
