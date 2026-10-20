// FabriCAD sketch: curve math for the non-circular sketch curves (ellipse, elliptical arc,
// control-point and fit-point B-splines) shared by the geometry model, the solver wrapper and
// the OCCT bridge. Pure math, no Qt, no OCCT.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/SketchGeometry.h"

#include <vector>

namespace fabricad::sketch {

/// Ellipse frame decoded from the PlaneGCS parameter layout (centre, focus1, minor radius).
struct EllipseFrame {
  Pt2 center;
  Pt2 major{1, 0};  ///< unit vector along the major axis (towards focus1)
  Pt2 minor{0, 1};  ///< major rotated +90 degrees
  double a = 0;     ///< major radius
  double b = 0;     ///< minor radius
  Pt2 value(double u) const { return center + major * (a * std::cos(u)) + minor * (b * std::sin(u)); }
  Pt2 derivative(double u) const { return major * (-a * std::sin(u)) + minor * (b * std::cos(u)); }
  /// Parameter (eccentric anomaly) of the point of the ellipse closest to q, in [0, 2pi).
  double closestParam(Pt2 q) const;
};
/// Frame from centre, focus and minor radius (a = sqrt(|f - c|^2 + b^2)).
EllipseFrame ellipseFrame(Pt2 center, Pt2 focus, double minorRadius);
/// Focus position for centre, major direction (unit) and radii (a >= b enforced by the caller).
Pt2 ellipseFocus(Pt2 center, Pt2 majorDir, double a, double b);

/// Non-rational B-spline in sketch coordinates with a full (flattened, clamped) knot vector.
struct BSpline2 {
  int degree = 3;
  std::vector<Pt2> poles;
  std::vector<double> knots;  ///< size poles + degree + 1, clamped (first/last degree+1 equal)
  bool isValid() const {
    return degree >= 1 && poles.size() >= static_cast<size_t>(degree) + 1 &&
           knots.size() == poles.size() + static_cast<size_t>(degree) + 1;
  }
  double first() const { return knots.empty() ? 0.0 : knots[static_cast<size_t>(degree)]; }
  double last() const { return knots.empty() ? 1.0 : knots[knots.size() - 1 - static_cast<size_t>(degree)]; }
  Pt2 value(double u) const;
  Pt2 derivative(double u) const;
  /// Polyline with `perSpan` points per knot span.
  std::vector<Pt2> sample(int perSpan = 16) const;
  /// Parameter of the closest point to q.
  double closestParam(Pt2 q) const;
  /// Distinct knots + multiplicities (PlaneGCS representation).
  void distinctKnots(std::vector<double>& values, std::vector<int>& mults) const;
};

/// Basis function values N_{i,p}(u) for all poles (size n). Knot vector clamped.
std::vector<double> bsplineBasis(const std::vector<double>& knots, int degree, size_t poleCount, double u);
/// Clamped uniform knot vector on [0, 1] for n poles and the given degree.
std::vector<double> clampedUniformKnots(size_t poleCount, int degree);
/// Degree actually used for a spline with n control/fit points and a requested degree.
int effectiveDegree(size_t pointCount, int requested);

/// Control-point spline: the points are the poles.
BSpline2 controlPointSpline(const std::vector<Pt2>& poles, int degree);
/// Normalised chord-length parameters (0 .. 1) of a point sequence.
std::vector<double> chordLengthParams(const std::vector<Pt2>& pts);
/// Averaged knot vector (Piegl & Tiller eq. 9.8) for interpolation at `params`.
std::vector<double> averagedKnots(const std::vector<double>& params, int degree);
/// Global interpolation: a spline of degree min(3, n-1) through `fit` at `params`.
/// Empty spline (no poles) for fewer than two points or a singular system.
BSpline2 interpolateSpline(const std::vector<Pt2>& fit, const std::vector<double>& params);

/// Numeric intersection of two parametric curves given by sampling functions (used for curve
/// types without closed-form intersections). Returns points within `tol` of both curves.
std::vector<Pt2> intersectPolylines(const std::vector<Pt2>& a, const std::vector<Pt2>& b);

}  // namespace fabricad::sketch
