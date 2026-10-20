// FabriCAD drawing: dimension tolerances (symmetric, deviation, limits, basic, reference) and
// ISO 286 limits and fits (tolerance zones such as H7 or g6, hole/shaft fits such as H7/g6).
// Pure value code, no Qt widgets; used by Dimension (persisted with it) and SheetRenderer.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Units.h"

#include <QJsonObject>

#include <optional>
#include <string>

namespace fabricad::drawing {

/// How the tolerance of a dimension is shown (Fusion 360 "Tolerance" options + ISO 286 fits).
enum class ToleranceMode {
  None,
  Symmetric,  ///< 20 ±0.1
  Deviation,  ///< 20 with stacked +0.1 / -0.05
  Limits,     ///< stacked 20.1 / 19.95
  Fit,        ///< ISO 286 designation (20 H7, 20 H7/g6), optionally with deviations
  Basic,      ///< theoretically exact dimension (framed) for GD&T
  Reference,  ///< reference dimension in parentheses: (20)
};
const char* toleranceModeName(ToleranceMode m);
ToleranceMode toleranceModeFromName(const std::string& s);

/// What a Fit tolerance prints.
enum class FitDisplay {
  Designation,             ///< 20 H7
  DesignationDeviations,   ///< 20 H7 with stacked (+0.021 / 0)
  Deviations,              ///< 20 with the stacked ISO deviations
  Limits,                  ///< stacked limits computed from the ISO deviations
};
const char* fitDisplayName(FitDisplay d);
FitDisplay fitDisplayFromName(const std::string& s);

struct DimTolerance {
  ToleranceMode mode = ToleranceMode::None;
  double upper = 0;           ///< upper deviation (mm or deg, signed); Symmetric: the ± value
  double lower = 0;           ///< lower deviation (signed, usually <= 0)
  std::string fit;            ///< "H7", "g6", "H7/g6" (Fit mode)
  FitDisplay fitDisplay = FitDisplay::Designation;
  int precision = -1;         ///< decimals of the deviations; -1 = automatic (trimmed, <= 4)
  double textScale = 0.7;     ///< stacked deviation text height relative to the dimension text

  bool isSet() const { return mode != ToleranceMode::None; }
  QJsonObject toJson() const;
  static DimTolerance fromJson(const QJsonObject& o);
  bool operator==(const DimTolerance&) const = default;
};

/// Upper / lower deviation of a tolerance zone in mm.
struct Deviations {
  double upper = 0;
  double lower = 0;
};

/// ISO 286-1 standard tolerance IT<grade> in micrometres for a nominal size (0 < size <= 500
/// mm, grades 1..16). nullopt outside the table.
std::optional<double> isoStandardTolerance(double nominal, int grade);

/// ISO 286-2 deviations (mm) of a tolerance zone: hole letters A..ZC upper case, shaft letters
/// a..zc lower case, followed by the grade ("H7", "g6", "js6", "K7", "p6"). Supported
/// fundamental deviations: A-H, JS, K, M, N, P, R, S (holes) and a-h, js, k, m, n, p, r, s
/// (shafts), grades 1..16 where the standard defines them; nullopt otherwise.
std::optional<Deviations> isoZoneDeviations(double nominal, const std::string& zone);

enum class FitKind { Clearance, Transition, Interference };
const char* fitKindName(FitKind k);

/// A hole/shaft fit ("H7/g6") analysed at a nominal size.
struct FitAnalysis {
  std::string holeZone, shaftZone;
  Deviations hole, shaft;   ///< mm
  double maxClearance = 0;  ///< hole max - shaft min (mm, negative = interference)
  double minClearance = 0;  ///< hole min - shaft max
  FitKind kind = FitKind::Clearance;
};
std::optional<FitAnalysis> analyzeFit(double nominal, const std::string& fit);

/// Split "H7/g6" into zones; a single zone fills only one side. False for malformed text.
bool splitFit(const std::string& fit, std::string& holeZone, std::string& shaftZone);

/// Rendered text parts of a toleranced dimension value. `main` is the (prefixed) value text;
/// `after` follows it on the same line (" ±0.1", " H7/g6"); `upper` / `lower` are stacked to
/// the right (deviations) or replace `main` (limits: stackedMain). `framed` boxes `main` (basic).
struct ToleranceText {
  std::string main;
  std::string after;
  std::string upper, lower;
  bool stackedMain = false;   ///< limits: upper/lower are full-size and replace main
  bool framed = false;
  bool stacked() const { return !upper.empty() || !lower.empty(); }
};

/// Format a deviation value: "+0.1", "-0.05", "0" (zero never signed); decimals = precision or
/// automatic (trailing zeros trimmed, at most 4 decimals). `value` already in display units.
std::string formatDeviation(double value, int precision);
/// Format a plain value with the dimension precision rules (trailing zeros trimmed).
std::string formatValue(double value, int precision);

/// Compose the texts of a toleranced value. `valueMm` is the measured value (mm or degrees),
/// `valueText` its formatted text including prefix/symbol ("Ø20"), `lengthDim` selects unit
/// conversion of deviations (false for angles), `precision` is the dimension precision.
ToleranceText composeTolerance(const DimTolerance& tol, double valueMm, const std::string& valueText,
                               const std::string& symbolPrefix, const std::string& suffix, bool lengthDim,
                               int precision, const core::UnitSystem& units);

}  // namespace fabricad::drawing
