// FabriCAD drawing: name tables and small helpers for DrawingTypes.h.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingTypes.h"

#include <cmath>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fabricad::drawing {

namespace {
template <class E, size_t N>
const char* nameOf(const char* const (&names)[N], E v) {
  auto i = static_cast<size_t>(v);
  return i < N ? names[i] : names[0];
}
template <class E, size_t N>
E fromNameOf(const char* const (&names)[N], const std::string& s, E fallback) {
  for (size_t i = 0; i < N; ++i)
    if (s == names[i]) return static_cast<E>(i);
  return fallback;
}

const char* const kLineClassNames[] = {"Visible", "Hidden", "Tangent", "Section", "Hatch", "Center", "Dimension",
                                       "Annotation", "Border", "Detail"};
const char* const kOrientationNames[] = {"Front", "Back", "Top", "Bottom", "Left", "Right", "IsoTopRight", "IsoTopLeft",
                                         "IsoBottomRight", "IsoBottomLeft", "Custom"};
const char* const kStandardNames[] = {"FirstAngle", "ThirdAngle"};
const char* const kViewTypeNames[] = {"Base", "Projected", "Section", "Detail"};
const char* const kAlignmentNames[] = {"None", "Horizontal", "Vertical"};
const char* const kDirectionNames[] = {"Left", "Right", "Above", "Below", "AboveLeft", "AboveRight", "BelowLeft",
                                       "BelowRight"};
const char* const kTangentNames[] = {"Off", "Full"};
const char* const kDimTypeNames[] = {"Linear", "Horizontal", "Vertical", "Aligned", "Radius", "Diameter", "Angle"};
}  // namespace

const char* lineClassName(LineClass c) { return nameOf(kLineClassNames, c); }
LineClass lineClassFromName(const std::string& s) { return fromNameOf(kLineClassNames, s, LineClass::Visible); }

double lineClassWidth(LineClass c) {
  switch (c) {
    case LineClass::Visible: return 0.5;
    case LineClass::Section: return 0.5;
    case LineClass::Hidden: return 0.25;
    case LineClass::Annotation: return 0.25;
    case LineClass::Detail: return 0.25;
    case LineClass::Border: return 0.5;
    case LineClass::Tangent:
    case LineClass::Hatch:
    case LineClass::Center:
    case LineClass::Dimension: return 0.18;
  }
  return 0.25;
}

const char* viewOrientationName(ViewOrientation o) { return nameOf(kOrientationNames, o); }
ViewOrientation viewOrientationFromName(const std::string& s) {
  return fromNameOf(kOrientationNames, s, ViewOrientation::Front);
}
std::vector<ViewOrientation> standardOrientations() {
  return {ViewOrientation::Front, ViewOrientation::Top, ViewOrientation::Right, ViewOrientation::Left,
          ViewOrientation::Bottom, ViewOrientation::Back, ViewOrientation::IsoTopRight, ViewOrientation::IsoTopLeft,
          ViewOrientation::IsoBottomRight, ViewOrientation::IsoBottomLeft};
}

const char* projectionStandardName(ProjectionStandard s) { return nameOf(kStandardNames, s); }
ProjectionStandard projectionStandardFromName(const std::string& s) {
  return fromNameOf(kStandardNames, s, ProjectionStandard::FirstAngle);
}

const char* viewTypeName(ViewType t) { return nameOf(kViewTypeNames, t); }
ViewType viewTypeFromName(const std::string& s) { return fromNameOf(kViewTypeNames, s, ViewType::Base); }

const char* viewAlignmentName(ViewAlignment a) { return nameOf(kAlignmentNames, a); }
ViewAlignment viewAlignmentFromName(const std::string& s) { return fromNameOf(kAlignmentNames, s, ViewAlignment::None); }

const char* projectionDirectionName(ProjectionDirection d) { return nameOf(kDirectionNames, d); }
ProjectionDirection projectionDirectionFromName(const std::string& s) {
  return fromNameOf(kDirectionNames, s, ProjectionDirection::Right);
}

ProjectionDirection classifyDirection(const Point2& off) {
  // Sheet Y points down: negative y = above.
  const double ax = std::abs(off.x), ay = std::abs(off.y);
  const bool diagonal = ax > 1e-9 && ay > 1e-9 && std::min(ax, ay) / std::max(ax, ay) > 0.4;
  if (diagonal) {
    if (off.y < 0) return off.x < 0 ? ProjectionDirection::AboveLeft : ProjectionDirection::AboveRight;
    return off.x < 0 ? ProjectionDirection::BelowLeft : ProjectionDirection::BelowRight;
  }
  if (ax >= ay) return off.x < 0 ? ProjectionDirection::Left : ProjectionDirection::Right;
  return off.y < 0 ? ProjectionDirection::Above : ProjectionDirection::Below;
}

bool isDiagonal(ProjectionDirection d) {
  return d == ProjectionDirection::AboveLeft || d == ProjectionDirection::AboveRight ||
         d == ProjectionDirection::BelowLeft || d == ProjectionDirection::BelowRight;
}

Point2 projectionDirectionVector(ProjectionDirection d) {
  const double k = std::sqrt(0.5);
  switch (d) {
    case ProjectionDirection::Left: return {-1, 0};
    case ProjectionDirection::Right: return {1, 0};
    case ProjectionDirection::Above: return {0, -1};
    case ProjectionDirection::Below: return {0, 1};
    case ProjectionDirection::AboveLeft: return {-k, -k};
    case ProjectionDirection::AboveRight: return {k, -k};
    case ProjectionDirection::BelowLeft: return {-k, k};
    case ProjectionDirection::BelowRight: return {k, k};
  }
  return {1, 0};
}

const char* tangentEdgeModeName(TangentEdgeMode m) { return nameOf(kTangentNames, m); }
TangentEdgeMode tangentEdgeModeFromName(const std::string& s) {
  return fromNameOf(kTangentNames, s, TangentEdgeMode::Off);
}

const char* dimensionTypeName(DimensionType t) { return nameOf(kDimTypeNames, t); }
DimensionType dimensionTypeFromName(const std::string& s) { return fromNameOf(kDimTypeNames, s, DimensionType::Linear); }

const std::vector<ScalePreset>& scalePresets() {
  static const std::vector<ScalePreset> presets = {
      {"1:100", 0.01}, {"1:50", 0.02}, {"1:20", 0.05}, {"1:10", 0.1}, {"1:5", 0.2}, {"1:2", 0.5},
      {"1:1", 1.0},    {"2:1", 2.0},   {"5:1", 5.0},   {"10:1", 10.0}, {"20:1", 20.0}, {"50:1", 50.0},
  };
  return presets;
}

std::string scaleLabel(double scale) {
  for (const auto& p : scalePresets())
    if (std::abs(p.value - scale) < 1e-9 * std::max(1.0, scale)) return p.label;
  char buf[64];
  if (scale >= 1.0) std::snprintf(buf, sizeof buf, "%g:1", scale);
  else std::snprintf(buf, sizeof buf, "1:%g", 1.0 / scale);
  return buf;
}

bool parseScale(const std::string& text, double& value) {
  std::string t;
  for (char c : text)
    if (c != ' ') t.push_back(c);
  if (t.empty()) return false;
  auto colon = t.find(':');
  char* end = nullptr;
  if (colon == std::string::npos) {
    double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || v <= 0) return false;
    value = v;
    return true;
  }
  std::string a = t.substr(0, colon), b = t.substr(colon + 1);
  double na = std::strtod(a.c_str(), &end);
  if (end == a.c_str() || *end != '\0' || na <= 0) return false;
  double nb = std::strtod(b.c_str(), &end);
  if (end == b.c_str() || *end != '\0' || nb <= 0) return false;
  value = na / nb;
  return true;
}

}  // namespace fabricad::drawing
