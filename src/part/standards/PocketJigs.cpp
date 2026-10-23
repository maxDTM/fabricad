// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/standards/PocketJigs.h"

#include <cctype>
#include <cmath>

namespace fabricad::part::standards {

const std::vector<PocketJig>& pocketJigs() {
  static const std::vector<PocketJig> k{
      {"kreg15", "Kreg 15°", 15.0, 3.6, 9.5},
      {"castle6", "Castle 6°", 6.0, 3.2, 7.9},
      {"custom", "Custom angle", 15.0, 3.6, 9.5},
  };
  return k;
}

const PocketJig* findPocketJig(const std::string& keyOrName) {
  std::string l;
  for (char c : keyOrName) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const PocketJig& j : pocketJigs())
    if (l == j.key || keyOrName == j.name) return &j;
  if (l == "kreg") return &pocketJigs()[0];
  if (l == "castle") return &pocketJigs()[1];
  return nullptr;
}

double pocketScrewLength(double t) {
  if (t < 17) return 25.4;        // 1/2" .. 5/8" stock: 1"
  if (t < 22) return 31.75;       // 3/4": 1 1/4"
  if (t < 32) return 38.1;        // 1": 1 1/2"
  return 63.5;                    // 1 1/2": 2 1/2"
}

const std::vector<double>& pocketScrewLengths() {
  static const std::vector<double> k{25.4, 31.75, 38.1, 63.5};
  return k;
}

const std::vector<double>& pocketThicknessPresets() {
  static const std::vector<double> k{12, 15, 18, 19.05, 25.4, 38.1};
  return k;
}

PocketGeometry pocketGeometry(double thickness, double angleDeg, double screwLength, double exitOffset) {
  PocketGeometry g;
  const double a = angleDeg * M_PI / 180.0;
  const double depthAtExit = thickness / 2.0 - exitOffset;  // below the entry face
  g.axisToExit = depthAtExit / std::sin(a);
  g.entryInset = g.axisToExit * std::cos(a);
  g.stepDepth = std::max(0.0, g.axisToExit - screwLength / 2.0);
  return g;
}

}  // namespace fabricad::part::standards
