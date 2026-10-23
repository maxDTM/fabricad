// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/standards/Heads.h"

#include <cmath>
#include <optional>

namespace fabricad::part::standards {

namespace {

using H = HeadType;
// type, size, inch, dk, k, socket, csinkAngle, cbore
constexpr HeadRow kHeads[] = {
    // ISO 4762 socket head cap screws; counterbores DIN 974-1 series for ISO 4762
    {H::SocketCap, "M1.6", false, 3.0, 1.6, 1.5, 0, 3.5},  {H::SocketCap, "M2", false, 3.8, 2.0, 1.5, 0, 4.4},
    {H::SocketCap, "M2.5", false, 4.5, 2.5, 2.0, 0, 5.5},  {H::SocketCap, "M3", false, 5.5, 3.0, 2.5, 0, 6.5},
    {H::SocketCap, "M4", false, 7.0, 4.0, 3.0, 0, 8.0},    {H::SocketCap, "M5", false, 8.5, 5.0, 4.0, 0, 10.0},
    {H::SocketCap, "M6", false, 10.0, 6.0, 5.0, 0, 11.0},  {H::SocketCap, "M8", false, 13.0, 8.0, 6.0, 0, 15.0},
    {H::SocketCap, "M10", false, 16.0, 10.0, 8.0, 0, 18.0}, {H::SocketCap, "M12", false, 18.0, 12.0, 10.0, 0, 20.0},
    {H::SocketCap, "M14", false, 21.0, 14.0, 12.0, 0, 24.0}, {H::SocketCap, "M16", false, 24.0, 16.0, 14.0, 0, 26.0},
    {H::SocketCap, "M20", false, 30.0, 20.0, 17.0, 0, 33.0}, {H::SocketCap, "M24", false, 36.0, 24.0, 19.0, 0, 40.0},
    {H::SocketCap, "M30", false, 45.0, 30.0, 22.0, 0, 48.0}, {H::SocketCap, "M36", false, 54.0, 36.0, 27.0, 0, 57.0},
    // ASME B18.3 socket head cap screws (inch)
    {H::SocketCap, "#0", true, 0.096, 0.060, 0.050, 0, 0.125},   {H::SocketCap, "#2", true, 0.140, 0.086, 0.078, 0, 0.188},
    {H::SocketCap, "#4", true, 0.183, 0.112, 0.094, 0, 0.219},   {H::SocketCap, "#5", true, 0.205, 0.125, 0.094, 0, 0.250},
    {H::SocketCap, "#6", true, 0.226, 0.138, 0.109, 0, 0.281},   {H::SocketCap, "#8", true, 0.270, 0.164, 0.141, 0, 0.312},
    {H::SocketCap, "#10", true, 0.312, 0.190, 0.156, 0, 0.375},  {H::SocketCap, "1/4", true, 0.375, 0.250, 0.188, 0, 0.438},
    {H::SocketCap, "5/16", true, 0.469, 0.312, 0.250, 0, 0.531}, {H::SocketCap, "3/8", true, 0.562, 0.375, 0.312, 0, 0.625},
    {H::SocketCap, "7/16", true, 0.656, 0.438, 0.375, 0, 0.719}, {H::SocketCap, "1/2", true, 0.750, 0.500, 0.375, 0, 0.812},
    {H::SocketCap, "5/8", true, 0.938, 0.625, 0.500, 0, 1.000},  {H::SocketCap, "3/4", true, 1.125, 0.750, 0.625, 0, 1.188},
    {H::SocketCap, "1", true, 1.500, 1.000, 0.750, 0, 1.625},
    // ISO 4014 / 4017 hex head (dk = width across corners e), DIN 974-2 counterbores
    {H::HexBolt, "M3", false, 6.01, 2.0, 5.5, 0, 11},    {H::HexBolt, "M4", false, 7.66, 2.8, 7, 0, 13},
    {H::HexBolt, "M5", false, 8.79, 3.5, 8, 0, 15},      {H::HexBolt, "M6", false, 11.05, 4.0, 10, 0, 18},
    {H::HexBolt, "M8", false, 14.38, 5.3, 13, 0, 24},    {H::HexBolt, "M10", false, 17.77, 6.4, 16, 0, 28},
    {H::HexBolt, "M12", false, 20.03, 7.5, 18, 0, 33},   {H::HexBolt, "M14", false, 23.36, 8.8, 21, 0, 36},
    {H::HexBolt, "M16", false, 26.75, 10.0, 24, 0, 40},  {H::HexBolt, "M20", false, 33.53, 12.5, 30, 0, 46},
    {H::HexBolt, "M24", false, 39.98, 15.0, 36, 0, 58},  {H::HexBolt, "M30", false, 50.85, 18.7, 46, 0, 73},
    {H::HexBolt, "M36", false, 60.79, 22.5, 55, 0, 82},
    // ASME B18.2.1 hex bolts (inch)
    {H::HexBolt, "1/4", true, 0.505, 0.163, 0.4375, 0, 0.750}, {H::HexBolt, "5/16", true, 0.577, 0.211, 0.500, 0, 0.875},
    {H::HexBolt, "3/8", true, 0.650, 0.243, 0.5625, 0, 1.000}, {H::HexBolt, "7/16", true, 0.722, 0.291, 0.625, 0, 1.125},
    {H::HexBolt, "1/2", true, 0.866, 0.323, 0.750, 0, 1.250},  {H::HexBolt, "5/8", true, 1.083, 0.403, 0.9375, 0, 1.500},
    {H::HexBolt, "3/4", true, 1.299, 0.483, 1.125, 0, 1.750},  {H::HexBolt, "1", true, 1.732, 0.627, 1.500, 0, 2.250},
    // DIN 7984 low head socket cap screws
    {H::LowHead, "M3", false, 5.5, 2.0, 2.0, 0, 6.5},   {H::LowHead, "M4", false, 7.0, 2.8, 2.5, 0, 8.0},
    {H::LowHead, "M5", false, 8.5, 3.5, 3.0, 0, 10.0},  {H::LowHead, "M6", false, 10.0, 4.0, 4.0, 0, 11.0},
    {H::LowHead, "M8", false, 13.0, 5.0, 5.0, 0, 15.0}, {H::LowHead, "M10", false, 16.0, 6.0, 7.0, 0, 18.0},
    {H::LowHead, "M12", false, 18.0, 7.0, 8.0, 0, 20.0}, {H::LowHead, "M16", false, 24.0, 9.0, 12.0, 0, 26.0},
    {H::LowHead, "M20", false, 30.0, 11.0, 14.0, 0, 33.0},
    // ISO 7380 button head
    {H::Button, "M3", false, 5.7, 1.65, 2.0, 0, 6.5},   {H::Button, "M4", false, 7.6, 2.2, 2.5, 0, 8.5},
    {H::Button, "M5", false, 9.5, 2.75, 3.0, 0, 10.5},  {H::Button, "M6", false, 10.5, 3.3, 4.0, 0, 11.5},
    {H::Button, "M8", false, 14.0, 4.4, 5.0, 0, 15.0},  {H::Button, "M10", false, 17.5, 5.5, 6.0, 0, 18.5},
    {H::Button, "M12", false, 21.0, 6.6, 8.0, 0, 22.0}, {H::Button, "M16", false, 28.0, 8.8, 10.0, 0, 29.0},
    // ISO 10642 flat countersunk socket (90 deg), dk theoretical
    {H::FlatCountersunk, "M3", false, 6.72, 1.86, 2.0, 90, 6.72},   {H::FlatCountersunk, "M4", false, 8.96, 2.48, 2.5, 90, 8.96},
    {H::FlatCountersunk, "M5", false, 11.20, 3.10, 3.0, 90, 11.20}, {H::FlatCountersunk, "M6", false, 13.44, 3.72, 4.0, 90, 13.44},
    {H::FlatCountersunk, "M8", false, 17.92, 4.96, 5.0, 90, 17.92}, {H::FlatCountersunk, "M10", false, 22.40, 6.20, 6.0, 90, 22.40},
    {H::FlatCountersunk, "M12", false, 26.88, 7.44, 8.0, 90, 26.88}, {H::FlatCountersunk, "M16", false, 33.60, 8.80, 10.0, 90, 33.60},
    {H::FlatCountersunk, "M20", false, 40.32, 10.16, 12.0, 90, 40.32},
    // ASME B18.3 flat countersunk socket (82 deg), head diameter theoretical sharp
    {H::FlatCountersunk, "#4", true, 0.255, 0.083, 0.063, 82, 0.255}, {H::FlatCountersunk, "#6", true, 0.307, 0.097, 0.078, 82, 0.307},
    {H::FlatCountersunk, "#8", true, 0.359, 0.111, 0.094, 82, 0.359}, {H::FlatCountersunk, "#10", true, 0.411, 0.125, 0.125, 82, 0.411},
    {H::FlatCountersunk, "1/4", true, 0.531, 0.161, 0.156, 82, 0.531}, {H::FlatCountersunk, "5/16", true, 0.656, 0.198, 0.188, 82, 0.656},
    {H::FlatCountersunk, "3/8", true, 0.781, 0.234, 0.219, 82, 0.781}, {H::FlatCountersunk, "1/2", true, 1.031, 0.308, 0.312, 82, 1.031},
    {H::FlatCountersunk, "5/8", true, 1.282, 0.381, 0.375, 82, 1.282}, {H::FlatCountersunk, "3/4", true, 1.531, 0.453, 0.500, 82, 1.531},
    // ISO 1207 slotted cheese head / pan
    {H::Pan, "M2", false, 3.8, 1.3, 0, 0, 4.4},   {H::Pan, "M2.5", false, 4.5, 1.6, 0, 0, 5.5},
    {H::Pan, "M3", false, 5.5, 2.0, 0, 0, 6.5},   {H::Pan, "M4", false, 7.0, 2.6, 0, 0, 8.0},
    {H::Pan, "M5", false, 8.5, 3.3, 0, 0, 10.0},  {H::Pan, "M6", false, 10.0, 3.9, 0, 0, 11.0},
    {H::Pan, "M8", false, 13.0, 5.0, 0, 0, 15.0}, {H::Pan, "M10", false, 16.0, 6.0, 0, 0, 18.0},
};

}  // namespace

const std::vector<std::string>& headTypeNames() {
  static const std::vector<std::string> k{"Custom",
                                          "SHCS (ISO 4762 / ASME B18.3)",
                                          "Hex bolt (ISO 4014 / B18.2.1)",
                                          "Low head SHCS (DIN 7984)",
                                          "Button head (ISO 7380)",
                                          "CSK flat head (ISO 10642 90° / B18.3 82°)",
                                          "Pan / cheese head (ISO 1207)"};
  return k;
}

const std::vector<std::string>& headTypeKeys() {
  static const std::vector<std::string> k{"custom", "shcs", "hex", "lowhead", "button", "csk", "pan"};
  return k;
}

std::optional<HeadRow> headFor(HeadType type, const ThreadSpec& t) {
  const std::string size = sizeOf(t.designation);
  for (const HeadRow& r : kHeads) {
    if (r.type != type || r.inch != t.isInch() || size != r.size) continue;
    HeadRow out = r;
    if (r.inch) {
      out.dk *= kInch;
      out.k *= kInch;
      out.socket *= kInch;
      out.cboreDia *= kInch;
    }
    out.inch = false;
    return out;
  }
  return std::nullopt;
}

std::optional<HeadRecess> headRecess(HeadType type, const ThreadSpec& t, double headClearance, double headOffset) {
  auto row = headFor(type, t);
  if (!row) return std::nullopt;
  HeadRecess r;
  r.depth = row->k + headClearance + std::max(0.0, headOffset);
  if (row->csinkAngle > 0) {
    r.countersink = true;
    r.angle = row->csinkAngle;
    const double tanHalf = std::tan(r.angle * M_PI / 360.0);
    r.diameter = row->cboreDia + 2.0 * headClearance + 2.0 * std::max(0.0, headOffset) * tanHalf;
  } else {
    r.diameter = row->cboreDia;
  }
  return r;
}

const std::vector<PointAngle>& drillPointPresets() {
  static const std::vector<PointAngle> k{{"118° (HSS)", 118}, {"135° (split point)", 135}, {"140° (carbide)", 140}};
  return k;
}

}  // namespace fabricad::part::standards
