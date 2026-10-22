// FabriCAD sketch: ellipse and B-spline math (evaluation, interpolation, closest points).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchCurves.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>

namespace fabricad::sketch {

// ---------------------------------------------------------------------------
// ellipse
// ---------------------------------------------------------------------------

EllipseFrame ellipseFrame(Pt2 center, Pt2 focus, double minorRadius) {
  EllipseFrame f;
  f.center = center;
  Pt2 d = focus - center;
  double c = d.length();
  f.major = c > 1e-12 ? d / c : Pt2{1, 0};
  f.minor = f.major.perp();
  f.b = std::abs(minorRadius);
  f.a = std::sqrt(c * c + f.b * f.b);
  return f;
}

Pt2 ellipseFocus(Pt2 center, Pt2 majorDir, double a, double b) {
  double c2 = a * a - b * b;
  // keep the focus off the centre so the major direction stays defined (near-circles)
  double c = std::sqrt(std::max(c2, (1e-6 * std::max(a, 1e-9)) * (1e-6 * std::max(a, 1e-9))));
  return center + majorDir.normalized() * c;
}

double EllipseFrame::closestParam(Pt2 q) const {
  // coarse sampling, then Newton on f(u) = (P(u) - q) . P'(u)
  const int n = 72;
  double best = 0, bestD = 1e300;
  for (int i = 0; i < n; ++i) {
    double u = 2 * kPi * i / n;
    double d = value(u).distance(q);
    if (d < bestD) {
      bestD = d;
      best = u;
    }
  }
  double u = best;
  for (int it = 0; it < 30; ++it) {
    Pt2 p = value(u), d1 = derivative(u);
    Pt2 d2 = major * (-a * std::cos(u)) + minor * (-b * std::sin(u));
    double f = (p - q).dot(d1);
    double fp = d1.dot(d1) + (p - q).dot(d2);
    if (std::abs(fp) < 1e-15) break;
    double step = f / fp;
    step = std::clamp(step, -0.3, 0.3);
    u -= step;
    if (std::abs(step) < 1e-13) break;
  }
  return normalizeAngle(u);
}

// ---------------------------------------------------------------------------
// B-splines
// ---------------------------------------------------------------------------

int effectiveDegree(size_t pointCount, int requested) {
  if (pointCount < 2) return 1;
  return std::clamp(requested, 1, static_cast<int>(pointCount) - 1);
}

std::vector<double> clampedUniformKnots(size_t n, int p) {
  std::vector<double> k;
  if (n < static_cast<size_t>(p) + 1) return k;
  size_t spans = n - static_cast<size_t>(p);
  for (int i = 0; i <= p; ++i) k.push_back(0.0);
  for (size_t i = 1; i < spans; ++i) k.push_back(static_cast<double>(i) / static_cast<double>(spans));
  for (int i = 0; i <= p; ++i) k.push_back(1.0);
  return k;
}

namespace {

/// Knot span index s with knots[s] <= u < knots[s+1] (last non-empty span for u == end).
size_t findSpan(const std::vector<double>& knots, int p, size_t n, double u) {
  size_t lo = static_cast<size_t>(p), hi = n;  // valid spans p .. n-1
  if (u >= knots[n]) return n - 1;
  if (u <= knots[lo]) return lo;
  while (hi - lo > 1) {
    size_t mid = (lo + hi) / 2;
    if (u < knots[mid]) hi = mid;
    else lo = mid;
  }
  return lo;
}

/// Non-zero basis functions N_{s-p..s,p}(u) and their first derivatives (Piegl & Tiller A2.3).
void basisFuns(const std::vector<double>& U, int p, size_t s, double u, std::vector<double>& N, std::vector<double>* dN) {
  const size_t P = static_cast<size_t>(p);
  std::vector<std::vector<double>> ndu(P + 1, std::vector<double>(P + 1, 0.0));
  std::vector<double> left(P + 1), right(P + 1);
  ndu[0][0] = 1.0;
  for (size_t j = 1; j <= P; ++j) {
    left[j] = u - U[s + 1 - j];
    right[j] = U[s + j] - u;
    double saved = 0.0;
    for (size_t r = 0; r < j; ++r) {
      ndu[j][r] = right[r + 1] + left[j - r];
      double tmp = std::abs(ndu[j][r]) > 1e-300 ? ndu[r][j - 1] / ndu[j][r] : 0.0;
      ndu[r][j] = saved + right[r + 1] * tmp;
      saved = left[j - r] * tmp;
    }
    ndu[j][j] = saved;
  }
  N.assign(P + 1, 0.0);
  for (size_t j = 0; j <= P; ++j) N[j] = ndu[j][P];
  if (dN) {
    dN->assign(P + 1, 0.0);
    if (P == 0) return;
    // first derivative: N'_{i,p} = p/(u_{i+p}-u_i) N_{i,p-1} - p/(u_{i+p+1}-u_{i+1}) N_{i+1,p-1}
    for (size_t r = 0; r <= P; ++r) {
      double d = 0.0;
      size_t i = s - P + r;
      if (r >= 1) {
        double den = U[i + P] - U[i];
        if (den > 1e-300) d += static_cast<double>(p) * ndu[r - 1][P - 1] / den;
      }
      if (r <= P - 1) {
        double den = U[i + P + 1] - U[i + 1];
        if (den > 1e-300) d -= static_cast<double>(p) * ndu[r][P - 1] / den;
      }
      (*dN)[r] = d;
    }
  }
}

}  // namespace

std::vector<double> bsplineBasis(const std::vector<double>& knots, int p, size_t n, double u) {
  std::vector<double> out(n, 0.0);
  if (n == 0 || knots.size() != n + static_cast<size_t>(p) + 1) return out;
  size_t s = findSpan(knots, p, n, u);
  std::vector<double> N;
  basisFuns(knots, p, s, u, N, nullptr);
  for (size_t j = 0; j <= static_cast<size_t>(p); ++j) out[s - static_cast<size_t>(p) + j] = N[j];
  return out;
}

Pt2 BSpline2::value(double u) const {
  if (!isValid()) return poles.empty() ? Pt2{} : poles.front();
  u = std::clamp(u, first(), last());
  size_t s = findSpan(knots, degree, poles.size(), u);
  std::vector<double> N;
  basisFuns(knots, degree, s, u, N, nullptr);
  Pt2 r;
  for (size_t j = 0; j <= static_cast<size_t>(degree); ++j) r += poles[s - static_cast<size_t>(degree) + j] * N[j];
  return r;
}

Pt2 BSpline2::derivative(double u) const {
  if (!isValid()) return {1, 0};
  u = std::clamp(u, first(), last());
  size_t s = findSpan(knots, degree, poles.size(), u);
  std::vector<double> N, dN;
  basisFuns(knots, degree, s, u, N, &dN);
  Pt2 r;
  for (size_t j = 0; j <= static_cast<size_t>(degree); ++j) r += poles[s - static_cast<size_t>(degree) + j] * dN[j];
  return r;
}

std::vector<Pt2> BSpline2::sample(int perSpan) const {
  std::vector<Pt2> out;
  if (!isValid()) return out;
  std::vector<double> vals;
  std::vector<int> mults;
  distinctKnots(vals, mults);
  out.push_back(value(vals.front()));
  for (size_t k = 0; k + 1 < vals.size(); ++k) {
    for (int i = 1; i <= perSpan; ++i) out.push_back(value(vals[k] + (vals[k + 1] - vals[k]) * i / perSpan));
  }
  return out;
}

double BSpline2::closestParam(Pt2 q) const {
  if (!isValid()) return 0.0;
  const double u0 = first(), u1 = last();
  const int n = std::max(32, static_cast<int>(poles.size()) * 16);
  double best = u0, bestD = 1e300;
  for (int i = 0; i <= n; ++i) {
    double u = u0 + (u1 - u0) * i / n;
    double d = value(u).distance(q);
    if (d < bestD) {
      bestD = d;
      best = u;
    }
  }
  // Newton with numeric second derivative
  double u = best;
  const double h = (u1 - u0) * 1e-6;
  for (int it = 0; it < 30; ++it) {
    Pt2 p = value(u), d1 = derivative(u);
    Pt2 d2 = (derivative(std::min(u1, u + h)) - derivative(std::max(u0, u - h))) / (2 * h);
    double f = (p - q).dot(d1);
    double fp = d1.dot(d1) + (p - q).dot(d2);
    if (std::abs(fp) < 1e-15) break;
    double step = std::clamp(f / fp, -(u1 - u0) * 0.1, (u1 - u0) * 0.1);
    double nu = std::clamp(u - step, u0, u1);
    if (std::abs(nu - u) < 1e-14) break;
    u = nu;
  }
  return u;
}

void BSpline2::distinctKnots(std::vector<double>& values, std::vector<int>& mults) const {
  values.clear();
  mults.clear();
  for (double k : knots) {
    if (!values.empty() && std::abs(k - values.back()) < 1e-14) {
      ++mults.back();
    } else {
      values.push_back(k);
      mults.push_back(1);
    }
  }
}

namespace {
/// Insert knot u once (Boehm).
void insertKnot(BSpline2& s, double u) {
  const int p = s.degree;
  const size_t n = s.poles.size();
  size_t k = findSpan(s.knots, p, n, u);
  std::vector<Pt2> q(n + 1);
  for (size_t i = 0; i <= n; ++i) {
    if (i + static_cast<size_t>(p) <= k) {
      q[i] = s.poles[i];
    } else if (i > k) {
      q[i] = s.poles[i - 1];
    } else {
      double den = s.knots[i + static_cast<size_t>(p)] - s.knots[i];
      double a = den > 1e-15 ? (u - s.knots[i]) / den : 0.0;
      q[i] = s.poles[i - 1] * (1 - a) + s.poles[i] * a;
    }
  }
  s.poles = std::move(q);
  s.knots.insert(s.knots.begin() + static_cast<std::ptrdiff_t>(k) + 1, u);
}
}  // namespace

bool splitBSpline(const BSpline2& s, double u, BSpline2& left, BSpline2& right) {
  if (!s.isValid()) return false;
  const double eps = 1e-9 * std::max(1.0, s.last() - s.first());
  if (u <= s.first() + eps || u >= s.last() - eps) return false;
  BSpline2 w = s;
  const int p = w.degree;
  int mult = 0;
  for (double k : w.knots)
    if (std::abs(k - u) <= eps) ++mult;
  for (double k : w.knots)  // snap to an existing knot
    if (std::abs(k - u) <= eps) u = k;
  for (int i = mult; i < p; ++i) insertKnot(w, u);
  size_t r = 0;
  while (r < w.knots.size() && w.knots[r] < u - eps) ++r;  // first occurrence of u
  // r .. r+p-1 are the p copies of u
  left.degree = right.degree = p;
  left.knots.assign(w.knots.begin(), w.knots.begin() + static_cast<std::ptrdiff_t>(r + static_cast<size_t>(p)));
  left.knots.push_back(u);
  left.poles.assign(w.poles.begin(), w.poles.begin() + static_cast<std::ptrdiff_t>(r));
  right.knots.assign(1, u);
  right.knots.insert(right.knots.end(), w.knots.begin() + static_cast<std::ptrdiff_t>(r), w.knots.end());
  right.poles.assign(w.poles.begin() + static_cast<std::ptrdiff_t>(r) - 1, w.poles.end());
  return left.isValid() && right.isValid();
}

BSpline2 subBSpline(const BSpline2& s, double u0, double u1) {
  BSpline2 cur = s, l, r;
  if (splitBSpline(cur, u0, l, r)) cur = r;
  if (splitBSpline(cur, u1, l, r)) cur = l;
  return cur;
}

BSpline2 joinBSplines(const BSpline2& a, const BSpline2& b) {
  BSpline2 out;
  if (!a.isValid() || !b.isValid() || a.degree != b.degree) return out;
  if (a.poles.back().distance(b.poles.front()) > 1e-6) return out;
  const size_t p = static_cast<size_t>(a.degree);
  const double shift = a.last() - b.first();
  out.degree = a.degree;
  out.knots.assign(a.knots.begin(), a.knots.end() - 1);
  for (size_t i = p + 1; i < b.knots.size(); ++i) out.knots.push_back(b.knots[i] + shift);
  out.poles = a.poles;
  out.poles.insert(out.poles.end(), b.poles.begin() + 1, b.poles.end());
  if (!out.isValid()) return BSpline2{};
  return out;
}

BSpline2 controlPointSpline(const std::vector<Pt2>& poles, int degree) {
  BSpline2 s;
  if (poles.size() < 2) return s;
  s.degree = effectiveDegree(poles.size(), degree);
  s.poles = poles;
  s.knots = clampedUniformKnots(poles.size(), s.degree);
  return s;
}

std::vector<double> chordLengthParams(const std::vector<Pt2>& pts) {
  std::vector<double> t(pts.size(), 0.0);
  if (pts.size() < 2) return t;
  double total = 0;
  for (size_t i = 1; i < pts.size(); ++i) total += pts[i].distance(pts[i - 1]);
  if (total < 1e-12) {
    for (size_t i = 0; i < pts.size(); ++i) t[i] = static_cast<double>(i) / static_cast<double>(pts.size() - 1);
    return t;
  }
  double acc = 0;
  for (size_t i = 1; i < pts.size(); ++i) {
    acc += pts[i].distance(pts[i - 1]);
    t[i] = acc / total;
  }
  t.back() = 1.0;
  // strictly increasing (coincident consecutive points would make the system singular)
  for (size_t i = 1; i < t.size(); ++i)
    if (t[i] <= t[i - 1] + 1e-9) t[i] = std::min(1.0, t[i - 1] + 1e-6);
  return t;
}

std::vector<double> averagedKnots(const std::vector<double>& t, int p) {
  const size_t n = t.size();
  std::vector<double> U;
  if (n < static_cast<size_t>(p) + 1) return U;
  for (int i = 0; i <= p; ++i) U.push_back(t.front());
  for (size_t j = 1; j + static_cast<size_t>(p) < n; ++j) {
    double s = 0;
    for (size_t i = j; i < j + static_cast<size_t>(p); ++i) s += t[i];
    U.push_back(s / p);
  }
  for (int i = 0; i <= p; ++i) U.push_back(t.back());
  return U;
}

BSpline2 interpolateSpline(const std::vector<Pt2>& fit, const std::vector<double>& params) {
  BSpline2 s;
  const size_t n = fit.size();
  if (n < 2 || params.size() != n) return s;
  s.degree = effectiveDegree(n, 3);
  s.knots = averagedKnots(params, s.degree);
  Eigen::MatrixXd A = Eigen::MatrixXd::Zero(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
  Eigen::MatrixXd B(static_cast<Eigen::Index>(n), 2);
  for (size_t i = 0; i < n; ++i) {
    std::vector<double> N = bsplineBasis(s.knots, s.degree, n, params[i]);
    for (size_t j = 0; j < n; ++j) A(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) = N[j];
    B(static_cast<Eigen::Index>(i), 0) = fit[i].x;
    B(static_cast<Eigen::Index>(i), 1) = fit[i].y;
  }
  Eigen::PartialPivLU<Eigen::MatrixXd> lu(A);
  if (std::abs(lu.determinant()) < 1e-14) return BSpline2{};
  Eigen::MatrixXd X = lu.solve(B);
  s.poles.resize(n);
  for (size_t i = 0; i < n; ++i) s.poles[i] = {X(static_cast<Eigen::Index>(i), 0), X(static_cast<Eigen::Index>(i), 1)};
  return s;
}

std::vector<Pt2> intersectPolylines(const std::vector<Pt2>& a, const std::vector<Pt2>& b) {
  std::vector<Pt2> out;
  for (size_t i = 0; i + 1 < a.size(); ++i) {
    Pt2 p = a[i], r = a[i + 1] - a[i];
    Pt2 amn{std::min(a[i].x, a[i + 1].x), std::min(a[i].y, a[i + 1].y)};
    Pt2 amx{std::max(a[i].x, a[i + 1].x), std::max(a[i].y, a[i + 1].y)};
    for (size_t j = 0; j + 1 < b.size(); ++j) {
      if (std::max(b[j].x, b[j + 1].x) < amn.x - 1e-9 || std::min(b[j].x, b[j + 1].x) > amx.x + 1e-9 ||
          std::max(b[j].y, b[j + 1].y) < amn.y - 1e-9 || std::min(b[j].y, b[j + 1].y) > amx.y + 1e-9)
        continue;
      Pt2 q = b[j], s = b[j + 1] - b[j];
      double den = r.cross(s);
      if (std::abs(den) < 1e-15) continue;
      double t = (q - p).cross(s) / den;
      double u = (q - p).cross(r) / den;
      if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) continue;
      Pt2 x = p + r * t;
      bool dup = false;
      for (const Pt2& o : out)
        if (o.near(x, 1e-7)) dup = true;
      if (!dup) out.push_back(x);
    }
  }
  return out;
}

}  // namespace fabricad::sketch
