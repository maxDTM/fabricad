// FabriCAD theme: colour math shared by the token checks (WCAG 2.x contrast, CIEDE2000, Machado
// 2009 colour-vision simulation). Mirrors tools/theme/colormath.py.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <array>

namespace fabricad::theme {

/// Linear-free sRGB colour with straight alpha, components 0..1.
struct Rgba {
  double r = 0, g = 0, b = 0, a = 1;
};

enum class Cvd { Normal, Protan, Deutan, Tritan };

Rgba fromU32(unsigned rgba);  ///< 0xRRGGBBAA
/// fg composited over an opaque bg (gamma space, as QPainter blends).
Rgba composite(const Rgba& fg, const Rgba& bg);
double relativeLuminance(const Rgba& c);
/// WCAG contrast ratio; a translucent fg is composited over bg first.
double contrastRatio(const Rgba& fg, const Rgba& bg);
/// Machado 2009 simulation, severity 1.0, applied in linear RGB.
Rgba simulate(const Rgba& c, Cvd kind);
/// CIEDE2000 colour difference (D65, 2 degree observer).
double deltaE2000(const Rgba& a, const Rgba& b);
/// ΔE00 in the normal, protan, deutan and tritan simulations.
std::array<double, 4> cvdDeltas(const Rgba& a, const Rgba& b);

}  // namespace fabricad::theme
