// FabriCAD assembly: triad manipulator math.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/Triad.h"

#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fabricad::assembly {

using core::Vec3;

namespace {
constexpr int kRingSegments = 48;
constexpr double kPlaneInner = 0.3;  ///< plane squares span [inner, outer] * size along both axes
constexpr double kPlaneOuter = 0.5;
}

unsigned Triad::maskOf(Handle h) {
  switch (h) {
    case Handle::MoveX: return kMoveX;
    case Handle::MoveY: return kMoveY;
    case Handle::MoveZ: return kMoveZ;
    case Handle::RotX: return kRotX;
    case Handle::RotY: return kRotY;
    case Handle::RotZ: return kRotZ;
    case Handle::PlaneXY: return kPlaneXY;
    case Handle::PlaneYZ: return kPlaneYZ;
    case Handle::PlaneZX: return kPlaneZX;
    default: return 0;
  }
}

const char* Triad::handleName(Handle h) {
  switch (h) {
    case Handle::MoveX: return "Move X";
    case Handle::MoveY: return "Move Y";
    case Handle::MoveZ: return "Move Z";
    case Handle::RotX: return "Rotate X";
    case Handle::RotY: return "Rotate Y";
    case Handle::RotZ: return "Rotate Z";
    case Handle::PlaneXY: return "Move XY";
    case Handle::PlaneYZ: return "Move YZ";
    case Handle::PlaneZX: return "Move ZX";
    default: return "None";
  }
}

std::array<float, 4> Triad::handleColor(Handle h) {
  switch (h) {
    case Handle::MoveX:
    case Handle::RotX: return {0.9f, 0.2f, 0.2f, 1.0f};
    case Handle::MoveY:
    case Handle::RotY: return {0.2f, 0.75f, 0.2f, 1.0f};
    case Handle::MoveZ:
    case Handle::RotZ: return {0.2f, 0.4f, 0.95f, 1.0f};
    default: return {0.95f, 0.8f, 0.2f, 1.0f};
  }
}

Triad::Triad(const gp_Ax3& frame, double size) : frame_(frame), size_(size) {}

void Triad::planeAxes(Handle h, gp_Dir& a, gp_Dir& b) const {
  a = h == Handle::PlaneXY ? frame_.XDirection() : h == Handle::PlaneYZ ? frame_.YDirection() : frame_.Direction();
  b = h == Handle::PlaneXY ? frame_.YDirection() : h == Handle::PlaneYZ ? frame_.Direction() : frame_.XDirection();
}

gp_Dir Triad::axis(Handle h) const {
  switch (h) {
    case Handle::MoveX:
    case Handle::RotX:
    case Handle::PlaneYZ: return frame_.XDirection();
    case Handle::MoveY:
    case Handle::RotY:
    case Handle::PlaneZX: return frame_.YDirection();
    default: return frame_.Direction();
  }
}

std::vector<Vec3> Triad::handleSegments(Handle h) const {
  std::vector<Vec3> out;
  const gp_Pnt o = frame_.Location();
  auto push = [&](const gp_Pnt& a, const gp_Pnt& b) {
    out.push_back(Vec3::from(a));
    out.push_back(Vec3::from(b));
  };
  if (isMove(h)) {
    gp_Dir d = axis(h);
    gp_Pnt tip = o.Translated(gp_Vec(d) * size_);
    push(o, tip);
    // arrow head: 4 short lines back from the tip
    gp_Dir u = isMove(h) && h == Handle::MoveZ ? frame_.XDirection() : frame_.Direction();
    if (std::abs(u.Dot(d)) > 0.9) u = frame_.YDirection();
    gp_Vec side = gp_Vec(u) - gp_Vec(d) * u.Dot(d);
    side.Normalize();
    gp_Vec side2 = gp_Vec(d).Crossed(side);
    gp_Pnt base = tip.Translated(gp_Vec(d) * (-size_ * 0.18));
    double w = size_ * 0.06;
    push(tip, base.Translated(side * w));
    push(tip, base.Translated(side * -w));
    push(tip, base.Translated(side2 * w));
    push(tip, base.Translated(side2 * -w));
  } else if (isRotation(h)) {
    gp_Dir n = axis(h);
    gp_Dir u = h == Handle::RotX ? frame_.YDirection() : h == Handle::RotY ? frame_.Direction() : frame_.XDirection();
    gp_Vec v = gp_Vec(n).Crossed(gp_Vec(u));
    double r = size_ * 0.75;
    gp_Pnt prev;
    for (int i = 0; i <= kRingSegments; ++i) {
      double a = 2.0 * std::numbers::pi * i / kRingSegments;
      gp_Pnt p = o.Translated(gp_Vec(u) * (r * std::cos(a)) + v * (r * std::sin(a)));
      if (i > 0) push(prev, p);
      prev = p;
    }
  } else if (isPlane(h)) {
    gp_Dir a, b;
    planeAxes(h, a, b);
    double s0 = size_ * kPlaneInner, s1 = size_ * kPlaneOuter;
    gp_Pnt p00 = o.Translated(gp_Vec(a) * s0 + gp_Vec(b) * s0);
    gp_Pnt p10 = o.Translated(gp_Vec(a) * s1 + gp_Vec(b) * s0);
    gp_Pnt p11 = o.Translated(gp_Vec(a) * s1 + gp_Vec(b) * s1);
    gp_Pnt p01 = o.Translated(gp_Vec(a) * s0 + gp_Vec(b) * s1);
    push(p00, p10);
    push(p10, p11);
    push(p11, p01);
    push(p01, p00);
  }
  return out;
}

std::vector<std::pair<Triad::Handle, std::vector<Vec3>>> Triad::allSegments() const {
  std::vector<std::pair<Handle, std::vector<Vec3>>> out;
  for (Handle h : {Handle::MoveX, Handle::MoveY, Handle::MoveZ, Handle::RotX, Handle::RotY, Handle::RotZ, Handle::PlaneXY,
                   Handle::PlaneYZ, Handle::PlaneZX})
    if (isEnabled(h)) out.emplace_back(h, handleSegments(h));
  return out;
}

bool Triad::closestAxisParameter(const gp_Pnt& p, const gp_Dir& u, const gp_Pnt& q, const gp_Dir& v, double& t) {
  // minimise |p + t u - (q + s v)|
  gp_Vec w(q, p);  // p - q
  double b = u.Dot(v);
  double denom = 1.0 - b * b;
  if (denom < 1e-10) return false;  // parallel
  double d = gp_Vec(u).Dot(w), e = gp_Vec(v).Dot(w);
  t = (b * e - d) / denom;
  return true;
}

bool Triad::rayPlane(const gp_Pnt& ro, const gp_Dir& rd, const gp_Pnt& po, const gp_Dir& n, gp_Pnt& out) {
  double denom = rd.Dot(n);
  if (std::abs(denom) < 1e-9) return false;
  double t = gp_Vec(ro, po).Dot(gp_Vec(n)) / denom;
  out = ro.Translated(gp_Vec(rd) * t);
  return true;
}

double Triad::raySegmentDistance(const gp_Pnt& ro, const gp_Dir& rd, const gp_Pnt& a, const gp_Pnt& b) {
  gp_Vec ab(a, b);
  double len = ab.Magnitude();
  if (len < 1e-12) {
    gp_Vec w(ro, a);
    double t = std::max(0.0, w.Dot(gp_Vec(rd)));
    return a.Distance(ro.Translated(gp_Vec(rd) * t));
  }
  gp_Dir u(ab);
  double t;
  if (!closestAxisParameter(a, u, ro, rd, t)) {
    // parallel: distance from a to the ray line
    gp_Vec w(ro, a);
    return (w - gp_Vec(rd) * w.Dot(gp_Vec(rd))).Magnitude();
  }
  t = std::clamp(t, 0.0, len);
  gp_Pnt onSeg = a.Translated(gp_Vec(u) * t);
  gp_Vec w(ro, onSeg);
  double s = std::max(0.0, w.Dot(gp_Vec(rd)));
  return onSeg.Distance(ro.Translated(gp_Vec(rd) * s));
}

Triad::Handle Triad::hit(const gp_Pnt& ro, const gp_Dir& rd, double tol) const {
  Handle best = Handle::None;
  double bestD = tol;
  // moves and planes: segment distance
  for (const auto& [h, segs] : allSegments()) {
    if (isRotation(h)) {
      // ring: ray/plane intersection distance to the ring radius
      gp_Pnt p;
      if (!rayPlane(ro, rd, frame_.Location(), axis(h), p)) continue;
      double r = size_ * 0.75;
      double d = std::abs(p.Distance(frame_.Location()) - r);
      if (d < bestD) {
        bestD = d;
        best = h;
      }
      continue;
    }
    if (isPlane(h)) {
      // filled square: any point inside (plus the tolerance) hits; arrows win ties (they are tested first)
      gp_Pnt p;
      if (!rayPlane(ro, rd, frame_.Location(), axis(h), p)) continue;
      gp_Dir a, b;
      planeAxes(h, a, b);
      gp_Vec v(frame_.Location(), p);
      double da = v.Dot(gp_Vec(a)), db = v.Dot(gp_Vec(b));
      double s0 = size_ * kPlaneInner, s1 = size_ * kPlaneOuter;
      double d = std::max({s0 - da, da - s1, s0 - db, db - s1, 0.0});
      if (d < bestD && (d > 0.0 || tol * 0.5 < bestD)) {
        bestD = d > 0.0 ? d : tol * 0.5;
        best = h;
      }
      continue;
    }
    for (size_t i = 0; i + 1 < segs.size(); i += 2) {
      double d = raySegmentDistance(ro, rd, segs[i].toPnt(), segs[i + 1].toPnt());
      if (d < bestD) {
        bestD = d;
        best = h;
      }
    }
  }
  // arrows win over planes at equal distance (planes are drawn inside the arrows)
  return best;
}

bool Triad::beginDrag(Handle h, const gp_Pnt& ro, const gp_Dir& rd) {
  active_ = Handle::None;
  value_ = 0.0;
  if (h == Handle::None || !isEnabled(h)) return false;
  const gp_Pnt o = frame_.Location();
  if (isMove(h)) {
    if (!closestAxisParameter(o, axis(h), ro, rd, startParam_)) return false;
  } else if (isPlane(h)) {
    if (!rayPlane(ro, rd, o, axis(h), startPoint_)) return false;
  } else {
    gp_Pnt p;
    if (!rayPlane(ro, rd, o, axis(h), p)) return false;
    gp_Vec radial(o, p);
    if (radial.Magnitude() < 1e-9) return false;
    startRadial_ = gp_Dir(radial);
  }
  active_ = h;
  return true;
}

bool Triad::dragTo(const gp_Pnt& ro, const gp_Dir& rd, gp_Trsf& delta, double snap) const {
  delta = gp_Trsf();
  if (active_ == Handle::None) return false;
  const gp_Pnt o = frame_.Location();
  if (isMove(active_)) {
    double t;
    if (!closestAxisParameter(o, axis(active_), ro, rd, t)) return false;
    double d = t - startParam_;
    if (snap > 0) d = std::round(d / snap) * snap;
    value_ = d;
    delta.SetTranslation(gp_Vec(axis(active_)) * d);
    return true;
  }
  if (isPlane(active_)) {
    gp_Pnt p;
    if (!rayPlane(ro, rd, o, axis(active_), p)) return false;
    gp_Vec v(startPoint_, p);
    if (snap > 0) {
      gp_Dir a, b;
      planeAxes(active_, a, b);
      double da = std::round(v.Dot(gp_Vec(a)) / snap) * snap, db = std::round(v.Dot(gp_Vec(b)) / snap) * snap;
      v = gp_Vec(a) * da + gp_Vec(b) * db;
    }
    value_ = v.Magnitude();
    delta.SetTranslation(v);
    return true;
  }
  gp_Pnt p;
  if (!rayPlane(ro, rd, o, axis(active_), p)) return false;
  gp_Vec radial(o, p);
  if (radial.Magnitude() < 1e-9) return false;
  gp_Dir cur(radial);
  double c = std::clamp(startRadial_.Dot(cur), -1.0, 1.0);
  double s = gp_Vec(axis(active_)).Dot(gp_Vec(startRadial_).Crossed(gp_Vec(cur)));
  double angle = std::atan2(s, c);
  double deg = angle * 180.0 / std::numbers::pi;
  if (snap > 0) deg = std::round(deg / snap) * snap;
  value_ = deg;
  delta.SetRotation(gp_Ax1(o, axis(active_)), deg * std::numbers::pi / 180.0);
  return true;
}

}  // namespace fabricad::assembly
