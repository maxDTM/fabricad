// FabriCAD core: physical quantities and units.
// Internal base units: length in millimetres, angle in degrees. Every stored
// Length/Angle property value is in these base units; conversion happens only when
// parsing expressions or formatting for display.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fabricad::core {

/// Dimension as integer powers of the two base dimensions we care about.
struct Dimension {
  int length = 0;
  int angle = 0;
  constexpr bool operator==(const Dimension&) const = default;
  constexpr bool isScalar() const { return length == 0 && angle == 0; }
  static constexpr Dimension Scalar() { return {0, 0}; }
  static constexpr Dimension Length() { return {1, 0}; }
  static constexpr Dimension Area() { return {2, 0}; }
  static constexpr Dimension Volume() { return {3, 0}; }
  static constexpr Dimension Angle() { return {0, 1}; }
  std::string toString() const;
};

/// A value with a dimension. Length values are mm, angles are degrees.
struct Quantity {
  double value = 0.0;
  Dimension dim{};
  constexpr bool operator==(const Quantity&) const = default;
  static constexpr Quantity scalar(double v) { return {v, Dimension::Scalar()}; }
  static constexpr Quantity mm(double v) { return {v, Dimension::Length()}; }
  static constexpr Quantity deg(double v) { return {v, Dimension::Angle()}; }
  double radians() const;  ///< angle value in radians (valid for Angle)
};

enum class LengthUnit { Millimeter, Centimeter, Meter, Inch, Foot };
enum class AngleUnit { Degree, Radian };

/// Document unit preferences: how bare numbers are read and how values display.
struct UnitSystem {
  LengthUnit length = LengthUnit::Millimeter;
  AngleUnit angle = AngleUnit::Degree;
  int precision = 3;  ///< decimals shown in the UI
  bool operator==(const UnitSystem&) const = default;
};

double lengthUnitToMm(LengthUnit u);           ///< factor: value_in_unit * factor = mm
double angleUnitToDeg(AngleUnit u);
const char* lengthUnitSymbol(LengthUnit u);    ///< "mm", "cm", "m", "in", "ft"
const char* angleUnitSymbol(AngleUnit u);      ///< "deg", "rad"
std::optional<LengthUnit> lengthUnitFromSymbol(std::string_view s);
std::optional<AngleUnit> angleUnitFromSymbol(std::string_view s);

/// Recognise a unit token ("mm", "in", "deg", "°", "\"", "'") -> factor to base unit + dimension.
struct UnitInfo {
  double toBase = 1.0;
  Dimension dim{};
};
std::optional<UnitInfo> parseUnitSymbol(std::string_view token);

/// Format a quantity for display: "12.5 mm", "90 deg", "3.2". Scalars have no unit.
std::string formatQuantity(const Quantity& q, const UnitSystem& units, bool withUnit = true);
/// Convert a base-unit quantity to the display unit (mm -> in etc.).
double toDisplayValue(const Quantity& q, const UnitSystem& units);
/// Inverse of toDisplayValue for a known dimension.
Quantity fromDisplayValue(double displayValue, Dimension dim, const UnitSystem& units);

}  // namespace fabricad::core
