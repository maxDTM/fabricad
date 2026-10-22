// FabriCAD theme: colour math.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme/ColorMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fabricad::theme {

namespace {

double toLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double toSrgb(double c) {
  c = std::clamp(c, 0.0, 1.0);
  return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}
double rad(double d) { return d * std::numbers::pi / 180.0; }
double deg(double r) { return r * 180.0 / std::numbers::pi; }

constexpr double kMachado[3][3][3] = {
    {{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}},
    {{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}},
    {{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}},
};

std::array<double, 3> toLab(const Rgba& c) {
  const double r = toLinear(c.r), g = toLinear(c.g), b = toLinear(c.b);
  const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
  const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
  const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
  auto f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
  const double fx = f(x), fy = f(y), fz = f(z);
  return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

}  // namespace

Rgba fromU32(unsigned v) {
  return {((v >> 24) & 0xFF) / 255.0, ((v >> 16) & 0xFF) / 255.0, ((v >> 8) & 0xFF) / 255.0, (v & 0xFF) / 255.0};
}

Rgba composite(const Rgba& fg, const Rgba& bg) {
  const double a = fg.a;
  return {fg.r * a + bg.r * (1 - a), fg.g * a + bg.g * (1 - a), fg.b * a + bg.b * (1 - a), 1.0};
}

double relativeLuminance(const Rgba& c) {
  return 0.2126 * toLinear(c.r) + 0.7152 * toLinear(c.g) + 0.0722 * toLinear(c.b);
}

double contrastRatio(const Rgba& fgIn, const Rgba& bg) {
  const Rgba fg = fgIn.a < 1.0 ? composite(fgIn, bg) : fgIn;
  double l1 = relativeLuminance(fg), l2 = relativeLuminance(bg);
  if (l1 < l2) std::swap(l1, l2);
  return (l1 + 0.05) / (l2 + 0.05);
}

Rgba simulate(const Rgba& c, Cvd kind) {
  if (kind == Cvd::Normal) return c;
  const auto& m = kMachado[static_cast<int>(kind) - 1];
  const double lin[3] = {toLinear(c.r), toLinear(c.g), toLinear(c.b)};
  double out[3];
  for (int r = 0; r < 3; ++r) out[r] = m[r][0] * lin[0] + m[r][1] * lin[1] + m[r][2] * lin[2];
  return {toSrgb(out[0]), toSrgb(out[1]), toSrgb(out[2]), c.a};
}

double deltaE2000(const Rgba& c1, const Rgba& c2) {
  const auto [L1, a1, b1] = toLab(c1);
  const auto [L2, a2, b2] = toLab(c2);
  const double C1 = std::hypot(a1, b1), C2 = std::hypot(a2, b2);
  const double Cb = (C1 + C2) / 2;
  const double G = 0.5 * (1 - std::sqrt(std::pow(Cb, 7) / (std::pow(Cb, 7) + std::pow(25.0, 7))));
  const double a1p = (1 + G) * a1, a2p = (1 + G) * a2;
  const double C1p = std::hypot(a1p, b1), C2p = std::hypot(a2p, b2);
  auto hue = [](double b, double a) {
    double h = deg(std::atan2(b, a));
    return h < 0 ? h + 360 : h;
  };
  const double h1p = C1p != 0 ? hue(b1, a1p) : 0.0, h2p = C2p != 0 ? hue(b2, a2p) : 0.0;
  const double dLp = L2 - L1, dCp = C2p - C1p;
  double dhp = 0;
  if (C1p * C2p != 0) {
    dhp = h2p - h1p;
    if (dhp > 180) dhp -= 360;
    else if (dhp < -180) dhp += 360;
  }
  const double dHp = 2 * std::sqrt(C1p * C2p) * std::sin(rad(dhp) / 2);
  const double Lbp = (L1 + L2) / 2, Cbp = (C1p + C2p) / 2;
  double hbp;
  if (C1p * C2p == 0) hbp = h1p + h2p;
  else if (std::abs(h1p - h2p) <= 180) hbp = (h1p + h2p) / 2;
  else hbp = h1p + h2p < 360 ? (h1p + h2p + 360) / 2 : (h1p + h2p - 360) / 2;
  const double T = 1 - 0.17 * std::cos(rad(hbp - 30)) + 0.24 * std::cos(rad(2 * hbp)) + 0.32 * std::cos(rad(3 * hbp + 6)) -
                   0.20 * std::cos(rad(4 * hbp - 63));
  const double dtheta = 30 * std::exp(-std::pow((hbp - 275) / 25, 2));
  const double Rc = 2 * std::sqrt(std::pow(Cbp, 7) / (std::pow(Cbp, 7) + std::pow(25.0, 7)));
  const double Sl = 1 + 0.015 * std::pow(Lbp - 50, 2) / std::sqrt(20 + std::pow(Lbp - 50, 2));
  const double Sc = 1 + 0.045 * Cbp;
  const double Sh = 1 + 0.015 * Cbp * T;
  const double Rt = -std::sin(rad(2 * dtheta)) * Rc;
  return std::sqrt(std::pow(dLp / Sl, 2) + std::pow(dCp / Sc, 2) + std::pow(dHp / Sh, 2) + Rt * (dCp / Sc) * (dHp / Sh));
}

std::array<double, 4> cvdDeltas(const Rgba& a, const Rgba& b) {
  std::array<double, 4> d{};
  for (int i = 0; i < 4; ++i) d[i] = deltaE2000(simulate(a, Cvd(i)), simulate(b, Cvd(i)));
  return d;
}

}  // namespace fabricad::theme
