// FabriCAD core: on-canvas drag manipulators, headless math layer (see Manipulator.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Manipulator.h"

#include "core/Tool.h"

#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace fabricad::core {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr int kRingSegments = 64;

// Proportions relative to the effective size S.
constexpr double kShaftRadius = 0.030;
constexpr double kShaftLength = 0.70;
constexpr double kConeLength = 0.30;
constexpr double kConeRadius = 0.085;
constexpr double kRingMinor = 0.020;
constexpr double kAngularHandleRadius = 0.075;
constexpr double kPointRadius = 0.10;
constexpr double kTriadRing = 0.75;
constexpr double kPlaneInner = 0.30;
constexpr double kPlaneOuter = 0.50;

double deg(double rad) { return rad * 180.0 / kPi; }
double rad(double degrees) { return degrees * kPi / 180.0; }

/// Wrap an angle difference into (-180, 180].
double wrapDelta(double d) {
  while (d > 180.0) d -= 360.0;
  while (d <= -180.0) d += 360.0;
  return d;
}

/// A direction perpendicular to `d` (deterministic).
gp_Dir perpendicularTo(const gp_Dir& d) {
  gp_Dir u = std::abs(d.Z()) < 0.9 ? gp_Dir(0, 0, 1) : gp_Dir(1, 0, 0);
  gp_Vec side = gp_Vec(u) - gp_Vec(d) * u.Dot(d);
  return gp_Dir(side);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// enums
// ---------------------------------------------------------------------------------------------

const char* manipulatorKindName(ManipulatorKind k) {
  switch (k) {
    case ManipulatorKind::Linear: return "Linear";
    case ManipulatorKind::Angular: return "Angular";
    case ManipulatorKind::Triad: return "Triad";
    case ManipulatorKind::Point: return "Point";
  }
  return "?";
}

const char* manipulatorPhaseName(ManipulatorPhase p) {
  switch (p) {
    case ManipulatorPhase::Hover: return "Hover";
    case ManipulatorPhase::Leave: return "Leave";
    case ManipulatorPhase::DragBegin: return "DragBegin";
    case ManipulatorPhase::Drag: return "Drag";
    case ManipulatorPhase::DragEnd: return "DragEnd";
    case ManipulatorPhase::DragCancel: return "DragCancel";
  }
  return "?";
}

unsigned manipulatorHandleMask(ManipulatorHandle h) {
  switch (h) {
    case ManipulatorHandle::MoveX: return kHandleMoveX;
    case ManipulatorHandle::MoveY: return kHandleMoveY;
    case ManipulatorHandle::MoveZ: return kHandleMoveZ;
    case ManipulatorHandle::RotX: return kHandleRotX;
    case ManipulatorHandle::RotY: return kHandleRotY;
    case ManipulatorHandle::RotZ: return kHandleRotZ;
    case ManipulatorHandle::PlaneXY: return kHandlePlaneXY;
    case ManipulatorHandle::PlaneYZ: return kHandlePlaneYZ;
    case ManipulatorHandle::PlaneZX: return kHandlePlaneZX;
    case ManipulatorHandle::Point: return kHandlePoint;
    default: return 0;
  }
}

const char* manipulatorHandleName(ManipulatorHandle h) {
  switch (h) {
    case ManipulatorHandle::MoveX: return "Move X";
    case ManipulatorHandle::MoveY: return "Move Y";
    case ManipulatorHandle::MoveZ: return "Move Z";
    case ManipulatorHandle::RotX: return "Rotate X";
    case ManipulatorHandle::RotY: return "Rotate Y";
    case ManipulatorHandle::RotZ: return "Rotate Z";
    case ManipulatorHandle::PlaneXY: return "Move XY";
    case ManipulatorHandle::PlaneYZ: return "Move YZ";
    case ManipulatorHandle::PlaneZX: return "Move ZX";
    case ManipulatorHandle::Point: return "Point";
    default: return "None";
  }
}

bool isMoveHandle(ManipulatorHandle h) {
  return h == ManipulatorHandle::MoveX || h == ManipulatorHandle::MoveY || h == ManipulatorHandle::MoveZ;
}
bool isRotationHandle(ManipulatorHandle h) {
  return h == ManipulatorHandle::RotX || h == ManipulatorHandle::RotY || h == ManipulatorHandle::RotZ;
}
bool isPlaneHandle(ManipulatorHandle h) {
  return h == ManipulatorHandle::PlaneXY || h == ManipulatorHandle::PlaneYZ || h == ManipulatorHandle::PlaneZX;
}

std::array<float, 4> manipulatorColor(ManipulatorHandle h, ManipulatorKind kind, bool highlighted) {
  if (highlighted) return kManipulatorHighlight;
  if (kind != ManipulatorKind::Triad) return {0.25f, 0.55f, 0.95f, 1.0f};  // Fusion blue
  switch (h) {
    case ManipulatorHandle::MoveX:
    case ManipulatorHandle::RotX: return {0.90f, 0.22f, 0.22f, 1.0f};
    case ManipulatorHandle::MoveY:
    case ManipulatorHandle::RotY: return {0.20f, 0.75f, 0.25f, 1.0f};
    case ManipulatorHandle::MoveZ:
    case ManipulatorHandle::RotZ: return {0.22f, 0.42f, 0.95f, 1.0f};
    default: return {0.95f, 0.80f, 0.20f, 0.45f};  // plane squares, translucent
  }
}

// ---------------------------------------------------------------------------------------------
// ManipulatorModel: construction / spec
// ---------------------------------------------------------------------------------------------

ManipulatorModel::ManipulatorModel(ManipulatorSpec spec) : spec_(std::move(spec)) { point_ = spec_.frame.Location(); }

void ManipulatorModel::setSpec(const ManipulatorSpec& spec) {
  const double keep = spec_.value;
  spec_ = spec;
  if (isDragging()) spec_.value = keep;
  if (!isDragging()) point_ = spec_.frame.Location();
}

void ManipulatorModel::setValue(double value) {
  if (isDragging()) return;
  spec_.value = value;
}

double ManipulatorModel::effectiveSize(double pixelSize) const {
  if (spec_.size > 0) return spec_.size;
  return std::max(kAutoSizePixels * std::max(pixelSize, 1e-9), 1e-6);
}

gp_Ax3 ManipulatorModel::activeFrame() const {
  gp_Ax3 f = spec_.frame;
  if (spec_.kind == ManipulatorKind::Linear) f.Translate(gp_Vec(f.Direction()) * spec_.value);
  return f;
}

std::vector<ManipulatorHandle> ManipulatorModel::handles() const {
  switch (spec_.kind) {
    case ManipulatorKind::Linear: return {ManipulatorHandle::MoveZ};
    case ManipulatorKind::Angular: return {ManipulatorHandle::RotZ};
    case ManipulatorKind::Point: return {ManipulatorHandle::Point};
    case ManipulatorKind::Triad: {
      std::vector<ManipulatorHandle> out;
      for (ManipulatorHandle h : {ManipulatorHandle::MoveX, ManipulatorHandle::MoveY, ManipulatorHandle::MoveZ,
                                  ManipulatorHandle::RotX, ManipulatorHandle::RotY, ManipulatorHandle::RotZ,
                                  ManipulatorHandle::PlaneXY, ManipulatorHandle::PlaneYZ, ManipulatorHandle::PlaneZX})
        if (spec_.handles & manipulatorHandleMask(h)) out.push_back(h);
      return out;
    }
  }
  return {};
}

gp_Dir ManipulatorModel::axisOf(ManipulatorHandle h) const {
  const gp_Ax3& f = spec_.frame;
  switch (h) {
    case ManipulatorHandle::MoveX:
    case ManipulatorHandle::RotX:
    case ManipulatorHandle::PlaneYZ: return f.XDirection();
    case ManipulatorHandle::MoveY:
    case ManipulatorHandle::RotY:
    case ManipulatorHandle::PlaneZX: return f.YDirection();
    default: return f.Direction();
  }
}

void ManipulatorModel::planeAxes(ManipulatorHandle h, gp_Dir& a, gp_Dir& b) const {
  const gp_Ax3& f = spec_.frame;
  if (h == ManipulatorHandle::PlaneXY) {
    a = f.XDirection();
    b = f.YDirection();
  } else if (h == ManipulatorHandle::PlaneYZ) {
    a = f.YDirection();
    b = f.Direction();
  } else {
    a = f.Direction();
    b = f.XDirection();
  }
}

gp_Pnt ManipulatorModel::handlePoint(ManipulatorHandle h, double pixelSize) const {
  const double S = effectiveSize(pixelSize);
  const gp_Ax3& f = spec_.frame;
  const gp_Pnt o = f.Location();
  switch (spec_.kind) {
    case ManipulatorKind::Linear: return o.Translated(gp_Vec(f.Direction()) * spec_.value);
    case ManipulatorKind::Angular: {
      const double a = rad(spec_.value);
      return o.Translated(gp_Vec(f.XDirection()) * (S * std::cos(a)) + gp_Vec(f.YDirection()) * (S * std::sin(a)));
    }
    case ManipulatorKind::Point: return isDragging() ? point_ : o;
    case ManipulatorKind::Triad: {
      if (isMoveHandle(h)) return o.Translated(gp_Vec(axisOf(h)) * S);
      if (isRotationHandle(h)) {
        gp_Dir u = h == ManipulatorHandle::RotX ? f.YDirection() : h == ManipulatorHandle::RotY ? f.Direction() : f.XDirection();
        return o.Translated(gp_Vec(u) * (S * kTriadRing));
      }
      if (isPlaneHandle(h)) {
        gp_Dir a, b;
        planeAxes(h, a, b);
        const double m = S * 0.5 * (kPlaneInner + kPlaneOuter);
        return o.Translated(gp_Vec(a) * m + gp_Vec(b) * m);
      }
      return o;
    }
  }
  return o;
}

// ---------------------------------------------------------------------------------------------
// glyphs
// ---------------------------------------------------------------------------------------------

void ManipulatorModel::arrowGlyphs(std::vector<ManipulatorGlyph>& out, ManipulatorHandle h, const gp_Pnt& base,
                                   const gp_Dir& dir, double size, const std::array<float, 4>& color) const {
  const gp_Dir x = perpendicularTo(dir);
  ManipulatorGlyph shaft;
  shaft.handle = h;
  shaft.shape = ManipulatorGlyph::Shape::Cylinder;
  shaft.frame = gp_Ax3(base, dir, x);
  shaft.radius = size * kShaftRadius;
  shaft.length = size * kShaftLength;
  shaft.color = color;
  out.push_back(shaft);
  ManipulatorGlyph cone;
  cone.handle = h;
  cone.shape = ManipulatorGlyph::Shape::Cone;
  cone.frame = gp_Ax3(base.Translated(gp_Vec(dir) * (size * kShaftLength)), dir, x);
  cone.radius = size * kConeRadius;
  cone.length = size * kConeLength;
  cone.color = color;
  out.push_back(cone);
}

std::vector<ManipulatorGlyph> ManipulatorModel::glyphs(double pixelSize, ManipulatorHandle highlight) const {
  std::vector<ManipulatorGlyph> out;
  const double S = effectiveSize(pixelSize);
  const gp_Ax3& f = spec_.frame;
  const gp_Pnt o = f.Location();
  auto color = [&](ManipulatorHandle h) { return manipulatorColor(h, spec_.kind, h == highlight); };

  switch (spec_.kind) {
    case ManipulatorKind::Linear: {
      const gp_Pnt base = o.Translated(gp_Vec(f.Direction()) * spec_.value);
      arrowGlyphs(out, ManipulatorHandle::MoveZ, base, f.Direction(), S, color(ManipulatorHandle::MoveZ));
      if (std::abs(spec_.value) > 1e-9) {
        ManipulatorGlyph guide;
        guide.handle = ManipulatorHandle::MoveZ;
        guide.shape = ManipulatorGlyph::Shape::Segments;
        guide.points = {Vec3::from(o), Vec3::from(base)};
        guide.color = {0.45f, 0.45f, 0.45f, 1.0f};
        guide.dashed = true;
        out.push_back(guide);
      }
      break;
    }
    case ManipulatorKind::Angular: {
      ManipulatorGlyph ring;
      ring.handle = ManipulatorHandle::RotZ;
      ring.shape = ManipulatorGlyph::Shape::Torus;
      ring.frame = f;
      ring.radius = S;
      ring.radius2 = S * kRingMinor;
      ring.color = color(ManipulatorHandle::RotZ);
      out.push_back(ring);
      // zero reference and swept arc
      ManipulatorGlyph ref;
      ref.handle = ManipulatorHandle::RotZ;
      ref.shape = ManipulatorGlyph::Shape::Segments;
      ref.points = {Vec3::from(o), Vec3::from(o.Translated(gp_Vec(f.XDirection()) * S))};
      ref.color = {0.45f, 0.45f, 0.45f, 1.0f};
      ref.dashed = true;
      out.push_back(ref);
      if (std::abs(spec_.value) > 1e-9) {
        ManipulatorGlyph arc;
        arc.handle = ManipulatorHandle::RotZ;
        arc.shape = ManipulatorGlyph::Shape::Polyline;
        const int n = std::max(2, static_cast<int>(std::ceil(std::abs(spec_.value) / 5.0)));
        const double r = S * 0.9;
        for (int i = 0; i <= n; ++i) {
          const double a = rad(spec_.value * i / n);
          arc.points.push_back(Vec3::from(o.Translated(gp_Vec(f.XDirection()) * (r * std::cos(a)) + gp_Vec(f.YDirection()) * (r * std::sin(a)))));
        }
        arc.points.push_back(Vec3::from(o));
        arc.color = {0.45f, 0.45f, 0.45f, 1.0f};
        arc.dashed = true;
        out.push_back(arc);
      }
      ManipulatorGlyph knob;
      knob.handle = ManipulatorHandle::RotZ;
      knob.shape = ManipulatorGlyph::Shape::Sphere;
      knob.frame = gp_Ax3(handlePoint(ManipulatorHandle::RotZ, pixelSize), f.Direction(), f.XDirection());
      knob.radius = S * kAngularHandleRadius;
      knob.color = color(ManipulatorHandle::RotZ);
      out.push_back(knob);
      break;
    }
    case ManipulatorKind::Point: {
      ManipulatorGlyph sphere;
      sphere.handle = ManipulatorHandle::Point;
      sphere.shape = ManipulatorGlyph::Shape::Sphere;
      sphere.frame = gp_Ax3(handlePoint(ManipulatorHandle::Point, pixelSize), f.Direction(), f.XDirection());
      sphere.radius = S * kPointRadius;
      sphere.color = color(ManipulatorHandle::Point);
      out.push_back(sphere);
      break;
    }
    case ManipulatorKind::Triad: {
      for (ManipulatorHandle h : handles()) {
        if (isMoveHandle(h)) {
          arrowGlyphs(out, h, o, axisOf(h), S, color(h));
        } else if (isRotationHandle(h)) {
          ManipulatorGlyph ring;
          ring.handle = h;
          ring.shape = ManipulatorGlyph::Shape::Torus;
          ring.frame = gp_Ax3(o, axisOf(h), perpendicularTo(axisOf(h)));
          ring.radius = S * kTriadRing;
          ring.radius2 = S * kRingMinor * 0.75;
          ring.color = color(h);
          out.push_back(ring);
        } else if (isPlaneHandle(h)) {
          gp_Dir a, b;
          planeAxes(h, a, b);
          const double s0 = S * kPlaneInner, s1 = S * kPlaneOuter;
          ManipulatorGlyph quad;
          quad.handle = h;
          quad.shape = ManipulatorGlyph::Shape::Quad;
          quad.points = {Vec3::from(o.Translated(gp_Vec(a) * s0 + gp_Vec(b) * s0)), Vec3::from(o.Translated(gp_Vec(a) * s1 + gp_Vec(b) * s0)),
                         Vec3::from(o.Translated(gp_Vec(a) * s1 + gp_Vec(b) * s1)), Vec3::from(o.Translated(gp_Vec(a) * s0 + gp_Vec(b) * s1))};
          quad.color = color(h);
          out.push_back(quad);
        }
      }
      break;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
// ray helpers
// ---------------------------------------------------------------------------------------------

bool ManipulatorModel::closestAxisParameter(const gp_Pnt& p, const gp_Dir& u, const gp_Pnt& q, const gp_Dir& v, double& t) {
  // minimise |p + t u - (q + s v)|
  gp_Vec w(q, p);
  const double b = u.Dot(v);
  const double denom = 1.0 - b * b;
  if (denom < 1e-6) return false;  // (nearly) parallel: the axis cannot be driven from this view
  const double d = gp_Vec(u).Dot(w), e = gp_Vec(v).Dot(w);
  t = (b * e - d) / denom;
  return true;
}

bool ManipulatorModel::rayPlane(const gp_Pnt& ro, const gp_Dir& rd, const gp_Pnt& po, const gp_Dir& n, gp_Pnt& out) {
  const double denom = rd.Dot(n);
  if (std::abs(denom) < 1e-6) return false;
  const double t = gp_Vec(ro, po).Dot(gp_Vec(n)) / denom;
  out = ro.Translated(gp_Vec(rd) * t);
  return true;
}

double ManipulatorModel::rayPointDistance(const gp_Pnt& ro, const gp_Dir& rd, const gp_Pnt& p) {
  gp_Vec w(ro, p);
  const double t = std::max(0.0, w.Dot(gp_Vec(rd)));
  return p.Distance(ro.Translated(gp_Vec(rd) * t));
}

double ManipulatorModel::raySegmentDistance(const gp_Pnt& ro, const gp_Dir& rd, const gp_Pnt& a, const gp_Pnt& b) {
  gp_Vec ab(a, b);
  const double len = ab.Magnitude();
  if (len < 1e-12) return rayPointDistance(ro, rd, a);
  gp_Dir u(ab);
  double t;
  if (!closestAxisParameter(a, u, ro, rd, t)) {
    gp_Vec w(ro, a);
    return (w - gp_Vec(rd) * w.Dot(gp_Vec(rd))).Magnitude();
  }
  t = std::clamp(t, 0.0, len);
  const gp_Pnt onSeg = a.Translated(gp_Vec(u) * t);
  return rayPointDistance(ro, rd, onSeg);
}

// ---------------------------------------------------------------------------------------------
// hit testing
// ---------------------------------------------------------------------------------------------

ManipulatorModel::Hit ManipulatorModel::hit(const gp_Pnt& ro, const gp_Dir& rd, double pixelSize) const {
  const double S = effectiveSize(pixelSize);
  const double tol = kHitPixels * std::max(pixelSize, 1e-9);
  const gp_Ax3& f = spec_.frame;
  const gp_Pnt o = f.Location();
  Hit best;
  best.distance = std::numeric_limits<double>::infinity();
  auto consider = [&](ManipulatorHandle h, double d, double limit) {
    if (d <= limit && d < best.distance) {
      best.handle = h;
      best.distance = d;
    }
  };
  switch (spec_.kind) {
    case ManipulatorKind::Linear: {
      const gp_Pnt base = o.Translated(gp_Vec(f.Direction()) * spec_.value);
      const gp_Pnt tip = base.Translated(gp_Vec(f.Direction()) * S);
      consider(ManipulatorHandle::MoveZ, raySegmentDistance(ro, rd, base, tip), std::max(tol, S * kConeRadius));
      break;
    }
    case ManipulatorKind::Angular: {
      const gp_Pnt knob = handlePoint(ManipulatorHandle::RotZ, pixelSize);
      consider(ManipulatorHandle::RotZ, rayPointDistance(ro, rd, knob), std::max(tol, S * kAngularHandleRadius * 1.5));
      gp_Pnt p;
      if (rayPlane(ro, rd, o, f.Direction(), p)) consider(ManipulatorHandle::RotZ, std::abs(p.Distance(o) - S), tol);
      break;
    }
    case ManipulatorKind::Point: {
      consider(ManipulatorHandle::Point, rayPointDistance(ro, rd, handlePoint(ManipulatorHandle::Point, pixelSize)),
               std::max(tol, S * kPointRadius * 1.5));
      break;
    }
    case ManipulatorKind::Triad: {
      // arrows first (they win ties against the plane squares drawn inside them)
      for (ManipulatorHandle h : handles()) {
        if (!isMoveHandle(h)) continue;
        const gp_Pnt tip = o.Translated(gp_Vec(axisOf(h)) * S);
        consider(h, raySegmentDistance(ro, rd, o, tip), std::max(tol, S * kConeRadius));
      }
      for (ManipulatorHandle h : handles()) {
        if (isRotationHandle(h)) {
          gp_Pnt p;
          if (!rayPlane(ro, rd, o, axisOf(h), p)) continue;
          consider(h, std::abs(p.Distance(o) - S * kTriadRing), tol);
        } else if (isPlaneHandle(h)) {
          gp_Pnt p;
          if (!rayPlane(ro, rd, o, axisOf(h), p)) continue;
          gp_Dir a, b;
          planeAxes(h, a, b);
          gp_Vec v(o, p);
          const double da = v.Dot(gp_Vec(a)), db = v.Dot(gp_Vec(b));
          const double s0 = S * kPlaneInner, s1 = S * kPlaneOuter;
          const double outside = std::max({s0 - da, da - s1, s0 - db, db - s1, 0.0});
          // inside the square counts as "tol/2 away" so arrows still win when both are under the cursor
          consider(h, outside > 0 ? outside : tol * 0.5, tol);
        }
      }
      break;
    }
  }
  if (!best) best.distance = 0.0;
  return best;
}

// ---------------------------------------------------------------------------------------------
// snapping
// ---------------------------------------------------------------------------------------------

double ManipulatorModel::snapValue(double value, double increment) {
  if (!(increment > 0)) return value;
  return std::round(value / increment) * increment;
}

double ManipulatorModel::niceIncrement(double value) {
  if (!(value > 0) || !std::isfinite(value)) return 1.0;
  const double e = std::floor(std::log10(value));
  const double base = std::pow(10.0, e);
  for (double m : {1.0, 2.0, 5.0, 10.0})
    if (m * base >= value * (1.0 - 1e-9)) return m * base;
  return 10.0 * base;
}

double ManipulatorModel::niceAngleIncrement(double value) {
  for (double c : {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 45.0})
    if (c >= value * (1.0 - 1e-9)) return c;
  return 90.0;
}

double ManipulatorModel::autoIncrement(double pixelSize, bool angular, double ringRadius) {
  const double travel = kSnapPixels * std::max(pixelSize, 1e-9);
  if (!angular) return niceIncrement(travel);
  const double r = std::max(ringRadius, 1e-9);
  return niceAngleIncrement(deg(travel / r));
}

double ManipulatorModel::incrementFor(int modifiers, double pixelSize) const {
  if (modifiers & ModCtrl) return 0.0;
  const bool angular = spec_.kind == ManipulatorKind::Angular || (spec_.kind == ManipulatorKind::Triad && isRotationHandle(active_));
  double inc = spec_.increment > 0 ? spec_.increment : autoIncrement(pixelSize, angular, effectiveSize(pixelSize) * (spec_.kind == ManipulatorKind::Triad ? kTriadRing : 1.0));
  if (modifiers & ModShift) inc *= 10.0;
  return inc;
}

double ManipulatorModel::applyLimits(double v) const {
  if (spec_.minValue && v < *spec_.minValue) v = *spec_.minValue;
  if (spec_.maxValue && v > *spec_.maxValue) v = *spec_.maxValue;
  return v;
}

// ---------------------------------------------------------------------------------------------
// dragging
// ---------------------------------------------------------------------------------------------

bool ManipulatorModel::angleOnPlane(const gp_Pnt& ro, const gp_Dir& rd, const gp_Pnt& o, const gp_Dir& n, const gp_Dir& x,
                                    double& angleDeg) const {
  gp_Pnt p;
  if (!rayPlane(ro, rd, o, n, p)) return false;
  gp_Vec v(o, p);
  if (v.Magnitude() < 1e-9) return false;
  const gp_Vec y = gp_Vec(n).Crossed(gp_Vec(x));
  angleDeg = deg(std::atan2(v.Dot(y), v.Dot(gp_Vec(x))));
  return true;
}

bool ManipulatorModel::beginDrag(ManipulatorHandle h, const gp_Pnt& ro, const gp_Dir& rd, double pixelSize) {
  active_ = ManipulatorHandle::None;
  transform_ = gp_Trsf();
  amount_ = 0.0;
  accumAngle_ = 0.0;
  const gp_Ax3& f = spec_.frame;
  const gp_Pnt o = f.Location();
  bool ok = false;
  switch (spec_.kind) {
    case ManipulatorKind::Linear:
      if (h != ManipulatorHandle::MoveZ) return false;
      ok = closestAxisParameter(o, f.Direction(), ro, rd, startParam_);
      break;
    case ManipulatorKind::Angular:
      if (h != ManipulatorHandle::RotZ) return false;
      ok = angleOnPlane(ro, rd, o, f.Direction(), f.XDirection(), lastAngle_);
      break;
    case ManipulatorKind::Point:
      if (h != ManipulatorHandle::Point) return false;
      dragNormal_ = spec_.planar ? f.Direction() : rd.Reversed();
      ok = rayPlane(ro, rd, o, dragNormal_, startPoint_);
      break;
    case ManipulatorKind::Triad:
      if (!(spec_.handles & manipulatorHandleMask(h))) return false;
      if (isMoveHandle(h)) ok = closestAxisParameter(o, axisOf(h), ro, rd, startParam_);
      else if (isPlaneHandle(h)) ok = rayPlane(ro, rd, o, axisOf(h), startPoint_);
      else if (isRotationHandle(h)) ok = angleOnPlane(ro, rd, o, axisOf(h), perpendicularTo(axisOf(h)), lastAngle_);
      break;
  }
  if (!ok) return false;
  (void)pixelSize;
  active_ = h;
  startValue_ = spec_.value;
  point_ = handlePoint(h, pixelSize);
  return true;
}

bool ManipulatorModel::dragTo(const gp_Pnt& ro, const gp_Dir& rd, int modifiers, double pixelSize) {
  if (!isDragging()) return false;
  const gp_Ax3& f = spec_.frame;
  const gp_Pnt o = f.Location();
  const double inc = incrementFor(modifiers, pixelSize);
  switch (spec_.kind) {
    case ManipulatorKind::Linear: {
      double t;
      if (!closestAxisParameter(o, f.Direction(), ro, rd, t)) return false;
      double v = startValue_ + (t - startParam_);
      v = applyLimits(snapValue(v, inc));
      spec_.value = v;
      amount_ = v - startValue_;
      transform_ = gp_Trsf();
      transform_.SetTranslation(gp_Vec(f.Direction()) * amount_);
      point_ = handlePoint(active_, pixelSize);
      return true;
    }
    case ManipulatorKind::Angular: {
      double a;
      if (!angleOnPlane(ro, rd, o, f.Direction(), f.XDirection(), a)) return false;
      accumAngle_ += wrapDelta(a - lastAngle_);
      lastAngle_ = a;
      double v = applyLimits(snapValue(startValue_ + accumAngle_, inc));
      spec_.value = v;
      amount_ = v - startValue_;
      transform_ = gp_Trsf();
      transform_.SetRotation(gp_Ax1(o, f.Direction()), rad(amount_));
      point_ = handlePoint(active_, pixelSize);
      return true;
    }
    case ManipulatorKind::Point: {
      gp_Pnt p;
      if (!rayPlane(ro, rd, o, dragNormal_, p)) return false;
      gp_Vec d(startPoint_, p);
      if (inc > 0) {
        // snap the displacement along the frame axes
        const gp_Dir axes[3] = {f.XDirection(), f.YDirection(), f.Direction()};
        gp_Vec s(0, 0, 0);
        for (const gp_Dir& ax : axes) s += gp_Vec(ax) * snapValue(d.Dot(gp_Vec(ax)), inc);
        d = s;
      }
      point_ = o.Translated(d);
      amount_ = d.Magnitude();
      spec_.value = amount_;
      transform_ = gp_Trsf();
      transform_.SetTranslation(d);
      return true;
    }
    case ManipulatorKind::Triad: {
      if (isMoveHandle(active_)) {
        double t;
        if (!closestAxisParameter(o, axisOf(active_), ro, rd, t)) return false;
        amount_ = snapValue(t - startParam_, inc);
        transform_ = gp_Trsf();
        transform_.SetTranslation(gp_Vec(axisOf(active_)) * amount_);
      } else if (isPlaneHandle(active_)) {
        gp_Pnt p;
        if (!rayPlane(ro, rd, o, axisOf(active_), p)) return false;
        gp_Vec v(startPoint_, p);
        if (inc > 0) {
          gp_Dir a, b;
          planeAxes(active_, a, b);
          v = gp_Vec(a) * snapValue(v.Dot(gp_Vec(a)), inc) + gp_Vec(b) * snapValue(v.Dot(gp_Vec(b)), inc);
        }
        amount_ = v.Magnitude();
        transform_ = gp_Trsf();
        transform_.SetTranslation(v);
      } else {
        double a;
        if (!angleOnPlane(ro, rd, o, axisOf(active_), perpendicularTo(axisOf(active_)), a)) return false;
        accumAngle_ += wrapDelta(a - lastAngle_);
        lastAngle_ = a;
        amount_ = snapValue(accumAngle_, inc);
        transform_ = gp_Trsf();
        transform_.SetRotation(gp_Ax1(o, axisOf(active_)), rad(amount_));
      }
      spec_.value = amount_;
      point_ = handlePoint(active_, pixelSize).Transformed(transform_);
      return true;
    }
  }
  return false;
}

void ManipulatorModel::endDrag() {
  active_ = ManipulatorHandle::None;
  if (spec_.kind == ManipulatorKind::Triad || spec_.kind == ManipulatorKind::Point) spec_.value = 0.0;
  transform_ = gp_Trsf();
  amount_ = 0.0;
  point_ = spec_.frame.Location();
}

void ManipulatorModel::cancelDrag() {
  if (!isDragging()) return;
  spec_.value = startValue_;
  endDrag();
}

// ---------------------------------------------------------------------------------------------
// ManipulatorController
// ---------------------------------------------------------------------------------------------

ManipulatorId ManipulatorController::add(const ManipulatorSpec& spec, ManipulatorCallback callback) {
  const ManipulatorId id = next_++;
  models_.emplace(id, Entry{ManipulatorModel(spec), std::move(callback)});
  changed();
  return id;
}

void ManipulatorController::update(ManipulatorId id, const ManipulatorSpec& spec) {
  auto it = models_.find(id);
  if (it == models_.end()) return;
  it->second.model.setSpec(spec);
  changed();
}

void ManipulatorController::setValue(ManipulatorId id, double value) {
  auto it = models_.find(id);
  if (it == models_.end()) return;
  if (std::abs(it->second.model.value() - value) < 1e-12) return;
  it->second.model.setValue(value);
  changed();
}

void ManipulatorController::remove(ManipulatorId id) {
  if (dispatching_ > 0) {
    pendingRemove_.push_back(id);
    return;
  }
  auto it = models_.find(id);
  if (it == models_.end()) return;
  if (dragging_ == id) dragging_ = kInvalidManipulator;
  if (hovered_ == id) {
    hovered_ = kInvalidManipulator;
    hoveredHandle_ = ManipulatorHandle::None;
  }
  models_.erase(it);
  changed();
}

void ManipulatorController::clear() {
  if (dispatching_ > 0) {
    pendingClear_ = true;
    return;
  }
  const bool had = !models_.empty();
  models_.clear();
  dragging_ = kInvalidManipulator;
  hovered_ = kInvalidManipulator;
  hoveredHandle_ = ManipulatorHandle::None;
  if (had) changed();
}

void ManipulatorController::flushRemovals() {
  if (dispatching_ > 0) return;
  if (pendingClear_) {
    pendingClear_ = false;
    pendingRemove_.clear();
    clear();
    return;
  }
  std::vector<ManipulatorId> ids = std::move(pendingRemove_);
  pendingRemove_.clear();
  for (ManipulatorId id : ids) remove(id);
}

std::vector<ManipulatorId> ManipulatorController::ids() const {
  std::vector<ManipulatorId> out;
  for (const auto& [id, e] : models_) out.push_back(id);
  return out;
}

const ManipulatorModel* ManipulatorController::model(ManipulatorId id) const {
  auto it = models_.find(id);
  return it == models_.end() ? nullptr : &it->second.model;
}

ManipulatorModel* ManipulatorController::model(ManipulatorId id) {
  auto it = models_.find(id);
  return it == models_.end() ? nullptr : &it->second.model;
}

ManipulatorId ManipulatorController::findByTag(int tag) const {
  for (const auto& [id, e] : models_)
    if (e.model.spec().tag == tag) return id;
  return kInvalidManipulator;
}

ManipulatorHandle ManipulatorController::highlightFor(ManipulatorId id) const {
  if (dragging_ != kInvalidManipulator) {
    if (id != dragging_) return ManipulatorHandle::None;
    const ManipulatorModel* m = model(id);
    return m ? m->activeHandle() : ManipulatorHandle::None;
  }
  return id == hovered_ ? hoveredHandle_ : ManipulatorHandle::None;
}

void ManipulatorController::changed() {
  if (onChange_) onChange_();
}

ManipulatorController::Target ManipulatorController::hitTest(const gp_Pnt& ro, const gp_Dir& rd, const PixelSizeFn& pixelSize) const {
  Target best;
  bool have = false;
  for (const auto& [id, e] : models_) {
    const double ps = pixelSize ? pixelSize(e.model.spec().frame.Location()) : 1.0;
    ManipulatorModel::Hit h = e.model.hit(ro, rd, ps);
    if (!h) continue;
    if (!have || h.distance < best.hit.distance) {
      best.id = id;
      best.hit = h;
      have = true;
    }
  }
  return best;
}

void ManipulatorController::dispatch(ManipulatorId id, ManipulatorPhase phase, int modifiers, double pixelSize) {
  auto it = models_.find(id);
  if (it == models_.end()) return;
  const ManipulatorModel& m = it->second.model;
  ManipulatorEvent ev;
  ev.id = id;
  ev.tag = m.spec().tag;
  ev.phase = phase;
  ev.handle = (phase == ManipulatorPhase::Hover || phase == ManipulatorPhase::Leave) ? hoveredHandle_ : m.activeHandle();
  ev.value = m.value();
  ev.startValue = m.isDragging() ? m.startValue() : m.value();
  ev.transform = m.dragTransform();
  ev.point = m.point();
  ev.modifiers = modifiers;
  (void)pixelSize;
  ManipulatorCallback cb = it->second.callback;  // copy: the callback may remove us
  ++eventCount_;
  if (cb) {
    ++dispatching_;
    cb(ev);
    --dispatching_;
    flushRemovals();
  }
}

bool ManipulatorController::press(const gp_Pnt& ro, const gp_Dir& rd, int modifiers, const PixelSizeFn& pixelSize) {
  if (isDragging()) return true;
  Target t = hitTest(ro, rd, pixelSize);
  if (t.id == kInvalidManipulator) return false;
  Entry& e = models_.at(t.id);
  const double ps = pixelSize ? pixelSize(e.model.spec().frame.Location()) : 1.0;
  if (!e.model.beginDrag(t.hit.handle, ro, rd, ps)) return false;
  dragging_ = t.id;
  hovered_ = t.id;
  hoveredHandle_ = t.hit.handle;
  changed();
  dispatch(t.id, ManipulatorPhase::DragBegin, modifiers, ps);
  return true;
}

bool ManipulatorController::move(const gp_Pnt& ro, const gp_Dir& rd, int modifiers, const PixelSizeFn& pixelSize) {
  if (isDragging()) {
    auto it = models_.find(dragging_);
    if (it == models_.end()) {
      dragging_ = kInvalidManipulator;
      return false;
    }
    const double ps = pixelSize ? pixelSize(it->second.model.spec().frame.Location()) : 1.0;
    if (it->second.model.dragTo(ro, rd, modifiers, ps)) {
      changed();
      dispatch(dragging_, ManipulatorPhase::Drag, modifiers, ps);
    }
    return true;
  }
  Target t = hitTest(ro, rd, pixelSize);
  if (t.id != hovered_ || t.hit.handle != hoveredHandle_) {
    const ManipulatorId old = hovered_;
    hovered_ = t.id;
    hoveredHandle_ = t.hit.handle;
    changed();
    if (old != kInvalidManipulator && old != t.id) dispatch(old, ManipulatorPhase::Leave, modifiers, 1.0);
    if (t.id != kInvalidManipulator) dispatch(t.id, ManipulatorPhase::Hover, modifiers, 1.0);
  }
  return hovered_ != kInvalidManipulator;
}

bool ManipulatorController::release(const gp_Pnt& ro, const gp_Dir& rd, int modifiers, const PixelSizeFn& pixelSize) {
  if (!isDragging()) return false;
  auto it = models_.find(dragging_);
  if (it == models_.end()) {
    dragging_ = kInvalidManipulator;
    return false;
  }
  const double ps = pixelSize ? pixelSize(it->second.model.spec().frame.Location()) : 1.0;
  it->second.model.dragTo(ro, rd, modifiers, ps);
  const ManipulatorId id = dragging_;
  // DragEnd carries the final value and transform, then the model goes idle
  ManipulatorEvent ev;
  ev.id = id;
  ev.tag = it->second.model.spec().tag;
  ev.phase = ManipulatorPhase::DragEnd;
  ev.handle = it->second.model.activeHandle();
  ev.value = it->second.model.value();
  ev.startValue = it->second.model.startValue();
  ev.transform = it->second.model.dragTransform();
  ev.point = it->second.model.point();
  ev.modifiers = modifiers;
  it->second.model.endDrag();
  dragging_ = kInvalidManipulator;
  changed();
  ManipulatorCallback cb = it->second.callback;
  ++eventCount_;
  if (cb) {
    ++dispatching_;
    cb(ev);
    --dispatching_;
    flushRemovals();
  }
  // re-evaluate the hover under the cursor
  if (models_.count(id)) move(ro, rd, modifiers, pixelSize);
  return true;
}

void ManipulatorController::cancelDrag() {
  if (!isDragging()) return;
  auto it = models_.find(dragging_);
  const ManipulatorId id = dragging_;
  dragging_ = kInvalidManipulator;
  if (it == models_.end()) return;
  ManipulatorEvent ev;
  ev.id = id;
  ev.tag = it->second.model.spec().tag;
  ev.phase = ManipulatorPhase::DragCancel;
  ev.handle = it->second.model.activeHandle();
  ev.startValue = it->second.model.startValue();
  it->second.model.cancelDrag();
  ev.value = it->second.model.value();
  ev.point = it->second.model.point();
  changed();
  ManipulatorCallback cb = it->second.callback;
  ++eventCount_;
  if (cb) {
    ++dispatching_;
    cb(ev);
    --dispatching_;
    flushRemovals();
  }
}

void ManipulatorController::leave() {
  if (isDragging() || hovered_ == kInvalidManipulator) return;
  const ManipulatorId old = hovered_;
  hovered_ = kInvalidManipulator;
  hoveredHandle_ = ManipulatorHandle::None;
  changed();
  dispatch(old, ManipulatorPhase::Leave, 0, 1.0);
}

}  // namespace fabricad::core
