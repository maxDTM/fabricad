// FabriCAD assembly: joint type catalogue and the per-type motion transform M(q).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <gp_Trsf.hxx>

#include <array>
#include <string>
#include <vector>

namespace fabricad::assembly {

/// Fusion 360 joint motion types. The order is the Enum property order of JointFeature.
enum class JointType : int {
  Rigid = 0,       ///< 0 DOF
  Revolute = 1,    ///< rotation about joint Z
  Slider = 2,      ///< translation along joint Z
  Cylindrical = 3, ///< rotation about + translation along joint Z
  PinSlot = 4,     ///< rotation about Z + translation along X
  Planar = 5,      ///< translation in XY + rotation about Z
  Ball = 6,        ///< 3 rotations (ZYX Euler)
};
constexpr int kJointTypeCount = 7;

/// Kind of one degree of freedom of a joint.
enum class DofKind { Rotation, Translation };

struct DofInfo {
  DofKind kind;
  std::string label;  ///< "Angle", "Distance", "Pitch"...
  int axis;           ///< 0 = X, 1 = Y, 2 = Z of the joint frame
};

const char* jointTypeName(JointType t);                  ///< "Rigid", "Revolute", ...
JointType jointTypeFromName(const std::string& name);    ///< unknown -> Rigid
std::vector<std::string> jointTypeNames();                ///< for the Enum property
/// The degrees of freedom of a joint type, in q order.
const std::vector<DofInfo>& jointDofs(JointType t);
int jointDofCount(JointType t);

/// Joint motion values. Angles are degrees, distances millimetres. Unused entries are ignored.
using JointValues = std::array<double, 3>;

/// Motion transform M(q) expressed in the joint frame (see docs/research/algorithms.md 4.1):
///   Rigid I; Revolute RotZ(q0); Slider TransZ(q0); Cylindrical RotZ(q0)*TransZ(q1);
///   PinSlot TransX(q1)*RotZ(q0); Planar TransX(q0)*TransY(q1)*RotZ(q2); Ball RotZ(q0)*RotY(q1)*RotX(q2).
gp_Trsf jointMotion(JointType t, const JointValues& q);

/// Inverse problem: given the relative transform between the two (aligned) joint frames,
/// recover the motion values that best explain it (used when dragging a jointed component
/// and for As-built joints). The transform must be expressed in the joint frame.
JointValues jointValuesFromMotion(JointType t, const gp_Trsf& motion);

}  // namespace fabricad::assembly
