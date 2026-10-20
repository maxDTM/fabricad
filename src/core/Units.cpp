// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Units.h"

#include <cmath>
#include <format>
#include <numbers>

namespace fabricad::core {

std::string Dimension::toString() const {
  if (isScalar()) return "scalar";
  std::string s;
  if (length == 1) s += "length";
  else if (length == 2) s += "area";
  else if (length == 3) s += "volume";
  else if (length != 0) s += std::format("length^{}", length);
  if (angle != 0) {
    if (!s.empty()) s += "*";
    s += angle == 1 ? "angle" : std::format("angle^{}", angle);
  }
  return s;
}

double Quantity::radians() const { return value * std::numbers::pi / 180.0; }

double lengthUnitToMm(LengthUnit u) {
  switch (u) {
    case LengthUnit::Millimeter: return 1.0;
    case LengthUnit::Centimeter: return 10.0;
    case LengthUnit::Meter: return 1000.0;
    case LengthUnit::Inch: return 25.4;
    case LengthUnit::Foot: return 304.8;
  }
  return 1.0;
}

double angleUnitToDeg(AngleUnit u) {
  switch (u) {
    case AngleUnit::Degree: return 1.0;
    case AngleUnit::Radian: return 180.0 / std::numbers::pi;
  }
  return 1.0;
}

const char* lengthUnitSymbol(LengthUnit u) {
  switch (u) {
    case LengthUnit::Millimeter: return "mm";
    case LengthUnit::Centimeter: return "cm";
    case LengthUnit::Meter: return "m";
    case LengthUnit::Inch: return "in";
    case LengthUnit::Foot: return "ft";
  }
  return "mm";
}

const char* angleUnitSymbol(AngleUnit u) { return u == AngleUnit::Radian ? "rad" : "deg"; }

std::optional<LengthUnit> lengthUnitFromSymbol(std::string_view s) {
  if (s == "mm") return LengthUnit::Millimeter;
  if (s == "cm") return LengthUnit::Centimeter;
  if (s == "m") return LengthUnit::Meter;
  if (s == "in" || s == "inch" || s == "\"") return LengthUnit::Inch;
  if (s == "ft" || s == "'") return LengthUnit::Foot;
  return std::nullopt;
}

std::optional<AngleUnit> angleUnitFromSymbol(std::string_view s) {
  if (s == "deg" || s == "°") return AngleUnit::Degree;
  if (s == "rad") return AngleUnit::Radian;
  return std::nullopt;
}

std::optional<UnitInfo> parseUnitSymbol(std::string_view token) {
  if (auto lu = lengthUnitFromSymbol(token)) return UnitInfo{lengthUnitToMm(*lu), Dimension::Length()};
  if (auto au = angleUnitFromSymbol(token)) return UnitInfo{angleUnitToDeg(*au), Dimension::Angle()};
  return std::nullopt;
}

double toDisplayValue(const Quantity& q, const UnitSystem& units) {
  if (q.dim == Dimension::Length()) return q.value / lengthUnitToMm(units.length);
  if (q.dim == Dimension::Area()) return q.value / std::pow(lengthUnitToMm(units.length), 2);
  if (q.dim == Dimension::Volume()) return q.value / std::pow(lengthUnitToMm(units.length), 3);
  if (q.dim == Dimension::Angle()) return q.value / angleUnitToDeg(units.angle);
  return q.value;
}

Quantity fromDisplayValue(double displayValue, Dimension dim, const UnitSystem& units) {
  Quantity q{displayValue, dim};
  if (dim == Dimension::Length()) q.value *= lengthUnitToMm(units.length);
  else if (dim == Dimension::Area()) q.value *= std::pow(lengthUnitToMm(units.length), 2);
  else if (dim == Dimension::Volume()) q.value *= std::pow(lengthUnitToMm(units.length), 3);
  else if (dim == Dimension::Angle()) q.value *= angleUnitToDeg(units.angle);
  return q;
}

std::string formatQuantity(const Quantity& q, const UnitSystem& units, bool withUnit) {
  double v = toDisplayValue(q, units);
  // Trim trailing zeros but keep up to `precision` decimals.
  std::string num = std::format("{:.{}f}", v, std::max(0, units.precision));
  if (num.find('.') != std::string::npos) {
    while (!num.empty() && num.back() == '0') num.pop_back();
    if (!num.empty() && num.back() == '.') num.pop_back();
  }
  if (num == "-0") num = "0";
  if (!withUnit) return num;
  if (q.dim == Dimension::Length()) return num + " " + lengthUnitSymbol(units.length);
  if (q.dim == Dimension::Area()) return num + " " + lengthUnitSymbol(units.length) + "^2";
  if (q.dim == Dimension::Volume()) return num + " " + lengthUnitSymbol(units.length) + "^3";
  if (q.dim == Dimension::Angle()) return num + " " + angleUnitSymbol(units.angle);
  return num;
}

}  // namespace fabricad::core
