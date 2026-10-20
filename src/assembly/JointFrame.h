// FabriCAD assembly: joint origins ("snap points") derived from picked geometry.
//
// Hovering a face/edge/vertex in the Joint tool offers Fusion-style snap candidates: face
// centre, edge midpoint/endpoints, circle centre, cylinder axis start/middle/end, vertex...
// Each candidate is a frame (gp_Ax3) in the coordinates of the component that owns the
// picked body: Z = face normal / axis direction, origin = the snap point. Body shapes are
// stored component-local, so no placement is applied here; JointOrigin::toWorld() does that.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/ShapeRef.h"
#include "core/Types.h"

#include <QJsonObject>
#include <QJsonValue>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::assembly {

enum class SnapMode {
  Auto,             ///< nearest candidate to the pick point
  FaceCenter,
  PickPoint,        ///< projected pick point on the face (Z = normal there)
  EdgeMidpoint,
  EdgeStart,
  EdgeEnd,
  CircleCenter,     ///< circular/elliptical edge or arc centre (Z = circle axis)
  AxisStart,        ///< cylinder/cone/torus axis, at the face's extent
  AxisMiddle,
  AxisEnd,
  Vertex,
  BodyCenter,       ///< bounding-box centre, component axes
  ComponentOrigin,  ///< the component's own origin
  Geometry,         ///< origin plane/axis/point or construction geometry (resolvePlane/Axis/Point)
};
const char* snapModeName(SnapMode m);
SnapMode snapModeFromName(const std::string& name);

struct SnapCandidate {
  SnapMode mode = SnapMode::Auto;
  gp_Ax3 frame;        ///< component-local
  std::string label;   ///< "Face centre", "Edge midpoint" ...
};

/// A joint origin: the picked reference plus the frame it produced.
struct JointOrigin {
  core::ShapeRef ref;                         ///< picked entity (B-rep sub-shape, Component, origin geometry)
  core::ObjectId component = core::kInvalidId;  ///< owning component (instance)
  SnapMode mode = SnapMode::Auto;
  core::Vec3 pickPoint;                       ///< component-local pick point (for Auto/PickPoint)
  gp_Ax3 frame;                               ///< component-local frame (cached, refreshed from geometry)
  bool valid = false;

  QJsonObject toJson() const;
  static JointOrigin fromJson(const QJsonValue& v);
  /// Frame in world coordinates (component placement applied).
  gp_Ax3 toWorld(const core::Document& doc) const;
};

/// Candidate frames for a picked entity (component-local). Empty when the ref does not resolve.
std::vector<SnapCandidate> snapCandidates(const core::Document& doc, const core::ShapeRef& ref,
                                          const core::BodyStateMap* state = nullptr);
/// Frame for an explicit mode (nullopt if the mode does not apply to this entity).
std::optional<gp_Ax3> snapFrame(const core::Document& doc, const core::ShapeRef& ref, SnapMode mode,
                                const gp_Pnt& localPick, const core::BodyStateMap* state = nullptr);
/// Build a joint origin from a pick. `worldPick` is where the user clicked; with mode Auto the
/// nearest candidate wins (falls back to PickPoint on faces, geometry frame otherwise).
JointOrigin makeJointOrigin(const core::Document& doc, const core::ShapeRef& ref, const gp_Pnt& worldPick,
                            SnapMode mode = SnapMode::Auto, const core::BodyStateMap* state = nullptr);
/// Recompute `origin.frame` from the current geometry. Returns false when the reference no longer
/// resolves (the cached frame is kept and `warning` explains).
bool refreshJointOrigin(const core::Document& doc, JointOrigin& origin, const core::BodyStateMap* state = nullptr,
                        std::string* warning = nullptr);
/// Candidate nearest to a (component-local) point, or nullopt for an empty list.
std::optional<SnapCandidate> nearestCandidate(const std::vector<SnapCandidate>& candidates, const gp_Pnt& localPoint);

/// A right-handed frame with the given Z; X is chosen stably (prefers the world X/Y axis).
gp_Ax3 frameFromNormal(const gp_Pnt& origin, const gp_Dir& z);

}  // namespace fabricad::assembly
