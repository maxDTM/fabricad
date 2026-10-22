// FabriCAD assembly: joint type catalogue and motion transforms.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/JointTypes.h"

#include <gp_Ax1.hxx>
#include <gp_Mat.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fabricad::assembly {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kRadToDeg = 180.0 / std::numbers::pi;

const char* const kNames[kJointTypeCount] = {"Rigid", "Revolute", "Slider", "Cylindrical", "Pin-slot", "Planar", "Ball"};

gp_Trsf rotation(int axis, double deg) {
  gp_Trsf t;
  gp_Dir d = axis == 0 ? gp_Dir(1, 0, 0) : axis == 1 ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1);
  t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), d), deg * kDegToRad);
  return t;
}

gp_Trsf translation(int axis, double mm) {
  gp_Trsf t;
  gp_Vec v = axis == 0 ? gp_Vec(mm, 0, 0) : axis == 1 ? gp_Vec(0, mm, 0) : gp_Vec(0, 0, mm);
  t.SetTranslation(v);
  return t;
}

/// Rotation angle (deg) of a transform about the given axis, read from the rotated X/Y axes.
double angleAbout(const gp_Trsf& t, int axis) {
  gp_Mat m = t.VectorialPart();
  // pick a unit vector perpendicular to the axis, rotate it, measure the signed angle
  gp_XYZ ax(axis == 0, axis == 1, axis == 2);
  gp_XYZ ref(axis == 0 ? 0 : 1, axis == 0 ? 1 : 0, 0);
  if (axis == 2) ref = gp_XYZ(1, 0, 0);
  gp_XYZ rotated = m * ref;
  rotated = rotated - ax * rotated.Dot(ax);
  if (rotated.Modulus() < 1e-12) return 0.0;
  rotated.Normalize();
  double c = std::clamp(ref.Dot(rotated), -1.0, 1.0);
  double s = ax.Dot(ref.Crossed(rotated));
  return std::atan2(s, c) * kRadToDeg;
}

}  // namespace

const char* jointTypeName(JointType t) {
  int i = static_cast<int>(t);
  return (i >= 0 && i < kJointTypeCount) ? kNames[i] : kNames[0];
}

JointType jointTypeFromName(const std::string& name) {
  for (int i = 0; i < kJointTypeCount; ++i)
    if (name == kNames[i]) return static_cast<JointType>(i);
  if (name == "PinSlot" || name == "Pin slot") return JointType::PinSlot;
  return JointType::Rigid;
}

std::vector<std::string> jointTypeNames() {
  return std::vector<std::string>(std::begin(kNames), std::end(kNames));
}

const std::vector<DofInfo>& jointDofs(JointType t) {
  static const std::vector<DofInfo> none;
  static const std::vector<DofInfo> revolute{{DofKind::Rotation, "Angle", 2}};
  static const std::vector<DofInfo> slider{{DofKind::Translation, "Distance", 2}};
  static const std::vector<DofInfo> cylindrical{{DofKind::Rotation, "Angle", 2}, {DofKind::Translation, "Distance", 2}};
  static const std::vector<DofInfo> pinSlot{{DofKind::Rotation, "Angle", 2}, {DofKind::Translation, "Slide", 0}};
  static const std::vector<DofInfo> planar{{DofKind::Translation, "X", 0}, {DofKind::Translation, "Y", 1},
                                           {DofKind::Rotation, "Angle", 2}};
  static const std::vector<DofInfo> ball{{DofKind::Rotation, "Yaw", 2}, {DofKind::Rotation, "Pitch", 1},
                                         {DofKind::Rotation, "Roll", 0}};
  switch (t) {
    case JointType::Rigid: return none;
    case JointType::Revolute: return revolute;
    case JointType::Slider: return slider;
    case JointType::Cylindrical: return cylindrical;
    case JointType::PinSlot: return pinSlot;
    case JointType::Planar: return planar;
    case JointType::Ball: return ball;
  }
  return none;
}

int jointDofCount(JointType t) { return static_cast<int>(jointDofs(t).size()); }

double wrapAngle180(double deg) {
  if (!std::isfinite(deg)) return deg;
  double r = std::fmod(deg, 360.0);
  if (r > 180.0) r -= 360.0;
  if (r <= -180.0) r += 360.0;
  return r;
}

gp_Trsf jointMotion(JointType t, const JointValues& q) {
  switch (t) {
    case JointType::Rigid: return gp_Trsf();
    case JointType::Revolute: return rotation(2, q[0]);
    case JointType::Slider: return translation(2, q[0]);
    case JointType::Cylindrical: return rotation(2, q[0]).Multiplied(translation(2, q[1]));
    case JointType::PinSlot: return translation(0, q[1]).Multiplied(rotation(2, q[0]));
    case JointType::Planar: return translation(0, q[0]).Multiplied(translation(1, q[1])).Multiplied(rotation(2, q[2]));
    case JointType::Ball: return rotation(2, q[0]).Multiplied(rotation(1, q[1])).Multiplied(rotation(0, q[2]));
  }
  return gp_Trsf();
}

JointValues jointValuesFromMotion(JointType t, const gp_Trsf& m) {
  JointValues q{0, 0, 0};
  gp_XYZ tr = m.TranslationPart();
  switch (t) {
    case JointType::Rigid: break;
    case JointType::Revolute: q[0] = angleAbout(m, 2); break;
    case JointType::Slider: q[0] = tr.Z(); break;
    case JointType::Cylindrical:
      q[0] = angleAbout(m, 2);
      q[1] = tr.Z();
      break;
    case JointType::PinSlot: {
      // M = TransX(d) * RotZ(a): the pin sits at (d,0,0) of the slot frame and spins about Z
      q[0] = angleAbout(m, 2);
      q[1] = tr.X();
      break;
    }
    case JointType::Planar:
      q[0] = tr.X();
      q[1] = tr.Y();
      q[2] = angleAbout(m, 2);
      break;
    case JointType::Ball: {
      // ZYX Euler angles (yaw about Z, pitch about Y, roll about X)
      gp_Quaternion rot = m.GetRotation();
      double a, b, c;
      rot.GetEulerAngles(gp_Extrinsic_XYZ, c, b, a);  // extrinsic XYZ == intrinsic ZYX
      q[0] = a * kRadToDeg;
      q[1] = b * kRadToDeg;
      q[2] = c * kRadToDeg;
      break;
    }
  }
  return q;
}

}  // namespace fabricad::assembly
