// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/standards/Clearance.h"

#include <cmath>
#include <cstring>

namespace fabricad::part::standards {

namespace {

struct MetricRow {
  double d, fine, medium, coarse;
};
// ISO 273
constexpr MetricRow kIso273[] = {
    {1.6, 1.7, 1.8, 2.0},  {2, 2.2, 2.4, 2.6},   {2.5, 2.7, 2.9, 3.1},  {3, 3.2, 3.4, 3.6},  {3.5, 3.7, 3.9, 4.2},
    {4, 4.3, 4.5, 4.8},    {5, 5.3, 5.5, 5.8},   {6, 6.4, 6.6, 7},      {8, 8.4, 9, 10},     {10, 10.5, 11, 12},
    {12, 13, 13.5, 14.5},  {14, 15, 15.5, 16.5}, {16, 17, 17.5, 18.5},  {18, 19, 20, 21},    {20, 21, 22, 24},
    {22, 23, 24, 26},      {24, 25, 26, 28},     {27, 28, 30, 32},      {30, 31, 33, 35},    {33, 34, 36, 38},
    {36, 37, 39, 42},      {39, 40, 42, 45},     {42, 43, 45, 48},      {45, 46, 48, 52},    {48, 50, 52, 56},
    {52, 54, 56, 62},      {56, 58, 62, 66},     {60, 62, 66, 70},      {64, 66, 70, 74},
};

// ASME B18.2.8 (inches), close / normal / loose
constexpr ClearanceRow kB1828[] = {
    {"#0", 0.0670, 0.0730, 0.0860},  {"#1", 0.0810, 0.0890, 0.0995},  {"#2", 0.0960, 0.1040, 0.1160},
    {"#3", 0.1100, 0.1160, 0.1285},  {"#4", 0.1200, 0.1285, 0.1440},  {"#5", 0.1360, 0.1440, 0.1590},
    {"#6", 0.1495, 0.1610, 0.1770},  {"#8", 0.1770, 0.1910, 0.2010},  {"#10", 0.2010, 0.2130, 0.2280},
    {"#12", 0.2280, 0.2400, 0.2570}, {"1/4", 0.2660, 0.2810, 0.3125}, {"5/16", 0.3320, 0.3440, 0.3750},
    {"3/8", 0.3970, 0.4060, 0.4375}, {"7/16", 0.4640, 0.4690, 0.5000}, {"1/2", 0.5312, 0.5469, 0.5625},
    {"9/16", 0.5938, 0.6094, 0.6406}, {"5/8", 0.6562, 0.6719, 0.7031}, {"3/4", 0.7812, 0.7969, 0.8281},
    {"7/8", 0.9062, 0.9219, 0.9531}, {"1", 1.0312, 1.0469, 1.0938},   {"1 1/4", 1.2812, 1.3125, 1.3750},
    {"1 1/2", 1.5312, 1.5625, 1.6250},
};

double pick(Fit fit, double c, double n, double l) { return fit == Fit::Close ? c : fit == Fit::Loose ? l : n; }

}  // namespace

double clearanceDiameter(const ThreadSpec& t, Fit fit) {
  if (t.isInch()) {
    const std::string size = sizeOf(t.designation);
    for (const ClearanceRow& r : kB1828)
      if (size == r.size) return pick(fit, r.close, r.normal, r.loose) * kInch;
  } else {
    for (const MetricRow& r : kIso273)
      if (std::abs(r.d - t.nominal) < 1e-9) return pick(fit, r.fine, r.medium, r.coarse);
  }
  return t.nominal * pick(fit, 1.07, 1.1, 1.2);
}

}  // namespace fabricad::part::standards
