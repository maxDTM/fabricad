// FabriCAD drawing: dimension tolerances and ISO 286 limits and fits.
// Table values: ISO 286-1:2010 table 1 (standard tolerance grades IT1..IT16, nominal sizes up to
// 500 mm) and ISO 286-2 fundamental deviations; these are published engineering constants.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Tolerance.h"

#include "core/Json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace fabricad::drawing {

using core::qs;
using core::ss;

const char* toleranceModeName(ToleranceMode m) {
  switch (m) {
    case ToleranceMode::None: return "None";
    case ToleranceMode::Symmetric: return "Symmetric";
    case ToleranceMode::Deviation: return "Deviation";
    case ToleranceMode::Limits: return "Limits";
    case ToleranceMode::Fit: return "Fit";
    case ToleranceMode::Basic: return "Basic";
    case ToleranceMode::Reference: return "Reference";
  }
  return "None";
}

ToleranceMode toleranceModeFromName(const std::string& s) {
  std::string l;
  for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (l == "symmetric" || l == "sym") return ToleranceMode::Symmetric;
  if (l == "deviation" || l == "dev") return ToleranceMode::Deviation;
  if (l == "limits" || l == "limit") return ToleranceMode::Limits;
  if (l == "fit") return ToleranceMode::Fit;
  if (l == "basic") return ToleranceMode::Basic;
  if (l == "reference" || l == "ref") return ToleranceMode::Reference;
  return ToleranceMode::None;
}

const char* fitDisplayName(FitDisplay d) {
  switch (d) {
    case FitDisplay::Designation: return "Designation";
    case FitDisplay::DesignationDeviations: return "DesignationDeviations";
    case FitDisplay::Deviations: return "Deviations";
    case FitDisplay::Limits: return "Limits";
  }
  return "Designation";
}

FitDisplay fitDisplayFromName(const std::string& s) {
  for (FitDisplay d : {FitDisplay::Designation, FitDisplay::DesignationDeviations, FitDisplay::Deviations, FitDisplay::Limits})
    if (s == fitDisplayName(d)) return d;
  return FitDisplay::Designation;
}

QJsonObject DimTolerance::toJson() const {
  QJsonObject o;
  o["mode"] = toleranceModeName(mode);
  if (upper != 0) o["upper"] = upper;
  if (lower != 0) o["lower"] = lower;
  if (!fit.empty()) o["fit"] = qs(fit);
  if (fitDisplay != FitDisplay::Designation) o["fitDisplay"] = fitDisplayName(fitDisplay);
  if (precision >= 0) o["precision"] = precision;
  if (textScale != 0.7) o["textScale"] = textScale;
  return o;
}

DimTolerance DimTolerance::fromJson(const QJsonObject& o) {
  DimTolerance t;
  t.mode = toleranceModeFromName(ss(o["mode"].toString()));
  t.upper = o["upper"].toDouble(0);
  t.lower = o["lower"].toDouble(0);
  t.fit = ss(o["fit"].toString());
  t.fitDisplay = fitDisplayFromName(ss(o["fitDisplay"].toString()));
  t.precision = o["precision"].toInt(-1);
  t.textScale = o["textScale"].toDouble(0.7);
  return t;
}

// ---------------------------------------------------------------------------------------
// ISO 286 tables
// ---------------------------------------------------------------------------------------

namespace {

// Main nominal size ranges (over lo, up to and including hi).
constexpr std::array<double, 13> kRangeHi = {3, 6, 10, 18, 30, 50, 80, 120, 180, 250, 315, 400, 500};
// Intermediate ranges used by r and s above 50 mm (and the same below).
constexpr std::array<double, 22> kFineHi = {3,   6,   10,  18,  30,  50,  65,  80,  100, 120, 140,
                                            160, 180, 200, 225, 250, 280, 315, 355, 400, 450, 500};

using Row = std::array<double, 13>;
// IT1 .. IT16 in micrometres.
const std::array<Row, 16> kIT = {{
    {0.8, 1, 1, 1.2, 1.5, 1.5, 2, 2.5, 3.5, 4.5, 6, 7, 8},
    {1.2, 1.5, 1.5, 2, 2.5, 2.5, 3, 4, 5, 7, 8, 9, 10},
    {2, 2.5, 2.5, 3, 4, 4, 5, 6, 8, 10, 12, 13, 15},
    {3, 4, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20},
    {4, 5, 6, 8, 9, 11, 13, 15, 18, 20, 23, 25, 27},
    {6, 8, 9, 11, 13, 16, 19, 22, 25, 29, 32, 36, 40},
    {10, 12, 15, 18, 21, 25, 30, 35, 40, 46, 52, 57, 63},
    {14, 18, 22, 27, 33, 39, 46, 54, 63, 72, 81, 89, 97},
    {25, 30, 36, 43, 52, 62, 74, 87, 100, 115, 130, 140, 155},
    {40, 48, 58, 70, 84, 100, 120, 140, 160, 185, 210, 230, 250},
    {60, 75, 90, 110, 130, 160, 190, 220, 250, 290, 320, 360, 400},
    {100, 120, 150, 180, 210, 250, 300, 350, 400, 460, 520, 570, 630},
    {140, 180, 220, 270, 330, 390, 460, 540, 630, 720, 810, 890, 970},
    {250, 300, 360, 430, 520, 620, 740, 870, 1000, 1150, 1300, 1400, 1550},
    {400, 480, 580, 700, 840, 1000, 1200, 1400, 1600, 1850, 2100, 2300, 2500},
    {600, 750, 900, 1100, 1300, 1600, 1900, 2200, 2500, 2900, 3200, 3600, 4000},
}};

// Shaft upper deviations es (micrometres) for d..h.
const Row kShaftD = {-20, -30, -40, -50, -65, -80, -100, -120, -145, -170, -190, -210, -230};
const Row kShaftE = {-14, -20, -25, -32, -40, -50, -60, -72, -85, -100, -110, -125, -135};
const Row kShaftF = {-6, -10, -13, -16, -20, -25, -30, -36, -43, -50, -56, -62, -68};
const Row kShaftG = {-2, -4, -5, -6, -7, -9, -10, -12, -14, -15, -17, -18, -20};
// Shaft lower deviations ei (micrometres) for k (grades 4..7), m, n, p.
const Row kShaftK = {0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5};
const Row kShaftM = {2, 4, 6, 7, 8, 9, 11, 13, 15, 17, 20, 21, 23};
const Row kShaftN = {4, 8, 10, 12, 15, 17, 20, 23, 27, 31, 34, 37, 40};
const Row kShaftP = {6, 12, 15, 18, 22, 26, 32, 37, 43, 50, 56, 62, 68};
const std::array<double, 22> kShaftR = {10, 15, 19, 23, 28, 34, 41, 43, 51, 54, 63,
                                        65, 68, 77, 80, 84, 94, 98, 108, 114, 126, 132};
const std::array<double, 22> kShaftS = {14, 19, 23, 28, 35, 43, 53, 59, 71, 79, 92,
                                        100, 108, 122, 130, 140, 158, 170, 190, 208, 232, 252};

int rangeIndex(double nominal) {
  if (!(nominal > 0) || nominal > 500 + 1e-9) return -1;
  for (size_t i = 0; i < kRangeHi.size(); ++i)
    if (nominal <= kRangeHi[i] + 1e-9) return static_cast<int>(i);
  return -1;
}

int fineIndex(double nominal) {
  if (!(nominal > 0) || nominal > 500 + 1e-9) return -1;
  for (size_t i = 0; i < kFineHi.size(); ++i)
    if (nominal <= kFineHi[i] + 1e-9) return static_cast<int>(i);
  return -1;
}

bool parseZone(const std::string& zone, std::string& letters, int& grade) {
  letters.clear();
  size_t i = 0;
  while (i < zone.size() && std::isalpha(static_cast<unsigned char>(zone[i]))) letters += zone[i++];
  if (letters.empty() || i >= zone.size()) return false;
  std::string digits = zone.substr(i);
  for (char c : digits)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  grade = std::stoi(digits);
  return grade >= 1 && grade <= 16;
}

/// Shaft fundamental deviation in micrometres: returns true with `isUpper` telling whether it is
/// es (a..h) or ei (k..s). `grade` matters for k only.
bool shaftFundamental(const std::string& l, int ri, int fi, int grade, double& value, bool& isUpper) {
  isUpper = true;
  if (l == "d") value = kShaftD[ri];
  else if (l == "e") value = kShaftE[ri];
  else if (l == "f") value = kShaftF[ri];
  else if (l == "g") value = kShaftG[ri];
  else if (l == "h") value = 0;
  else {
    isUpper = false;
    if (l == "k") value = (grade >= 4 && grade <= 7) ? kShaftK[ri] : 0.0;
    else if (l == "m") value = kShaftM[ri];
    else if (l == "n") value = kShaftN[ri];
    else if (l == "p") value = kShaftP[ri];
    else if (l == "r") value = kShaftR[static_cast<size_t>(fi)];
    else if (l == "s") value = kShaftS[static_cast<size_t>(fi)];
    else return false;
  }
  return true;
}

}  // namespace

std::optional<double> isoStandardTolerance(double nominal, int grade) {
  int ri = rangeIndex(nominal);
  if (ri < 0 || grade < 1 || grade > 16) return std::nullopt;
  return kIT[static_cast<size_t>(grade - 1)][static_cast<size_t>(ri)];
}

std::optional<Deviations> isoZoneDeviations(double nominal, const std::string& zone) {
  std::string letters;
  int grade = 0;
  if (!parseZone(zone, letters, grade)) return std::nullopt;
  int ri = rangeIndex(nominal), fi = fineIndex(nominal);
  if (ri < 0 || fi < 0) return std::nullopt;
  const double it = kIT[static_cast<size_t>(grade - 1)][static_cast<size_t>(ri)];
  const bool hole = std::isupper(static_cast<unsigned char>(letters[0])) != 0;
  std::string l;
  for (char c : letters) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  double es = 0, ei = 0;  // micrometres
  if (l == "js") {
    es = it / 2.0;
    ei = -it / 2.0;
  } else if (!hole) {
    double fd = 0;
    bool upper = true;
    if (!shaftFundamental(l, ri, fi, grade, fd, upper)) return std::nullopt;
    if (upper) {
      es = fd;
      ei = fd - it;
    } else {
      ei = fd;
      es = fd + it;
    }
  } else {
    double fd = 0;
    bool shaftUpper = true;
    if (!shaftFundamental(l, ri, fi, grade, fd, shaftUpper)) return std::nullopt;
    if (shaftUpper) {
      // A..H: EI = -es
      ei = -fd;
      es = ei + it;
    } else {
      // K..ZC: ES = -ei (+ delta for the fine grades)
      double delta = 0;
      if (grade >= 3 && grade <= 8 && nominal > 3 + 1e-9)
        delta = kIT[static_cast<size_t>(grade - 1)][static_cast<size_t>(ri)] -
                kIT[static_cast<size_t>(grade - 2)][static_cast<size_t>(ri)];
      double ES = 0;
      if (l == "k") {
        // shaft k grades 4..7 deviations apply to holes K <= 8 via -ei + delta
        double kei = kShaftK[static_cast<size_t>(ri)];
        ES = grade <= 8 ? -kei + delta : 0.0;
        if (nominal <= 3 + 1e-9) ES = 0;
      } else if (l == "m") {
        ES = grade <= 8 ? -fd + delta : -fd;
      } else if (l == "n") {
        ES = grade <= 8 ? -fd + delta : 0.0;
      } else {
        ES = grade <= 7 ? -fd + delta : -fd;
      }
      es = ES;
      ei = ES - it;
    }
  }
  return Deviations{es / 1000.0, ei / 1000.0};
}

const char* fitKindName(FitKind k) {
  switch (k) {
    case FitKind::Clearance: return "Clearance";
    case FitKind::Transition: return "Transition";
    case FitKind::Interference: return "Interference";
  }
  return "Clearance";
}

bool splitFit(const std::string& fitIn, std::string& holeZone, std::string& shaftZone) {
  holeZone.clear();
  shaftZone.clear();
  std::string fit;
  for (char c : fitIn)
    if (!std::isspace(static_cast<unsigned char>(c))) fit += c;
  if (fit.empty()) return false;
  auto classify = [&](const std::string& z) {
    if (z.empty()) return false;
    if (std::isupper(static_cast<unsigned char>(z[0]))) holeZone = z;
    else shaftZone = z;
    return true;
  };
  size_t slash = fit.find('/');
  if (slash == std::string::npos) return classify(fit);
  return classify(fit.substr(0, slash)) && classify(fit.substr(slash + 1)) && !holeZone.empty() && !shaftZone.empty();
}

std::optional<FitAnalysis> analyzeFit(double nominal, const std::string& fit) {
  FitAnalysis a;
  if (!splitFit(fit, a.holeZone, a.shaftZone) || a.holeZone.empty() || a.shaftZone.empty()) return std::nullopt;
  auto h = isoZoneDeviations(nominal, a.holeZone);
  auto s = isoZoneDeviations(nominal, a.shaftZone);
  if (!h || !s) return std::nullopt;
  a.hole = *h;
  a.shaft = *s;
  a.maxClearance = a.hole.upper - a.shaft.lower;
  a.minClearance = a.hole.lower - a.shaft.upper;
  if (a.minClearance >= -1e-12) a.kind = FitKind::Clearance;
  else if (a.maxClearance <= 1e-12) a.kind = FitKind::Interference;
  else a.kind = FitKind::Transition;
  return a;
}

// ---------------------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------------------

namespace {
std::string fixed(double v, int decimals) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", std::max(0, decimals), v);
  return buf;
}
int neededDecimals(double v, int maxDecimals) {
  for (int d = 0; d < maxDecimals; ++d) {
    double s = std::pow(10.0, d);
    if (std::abs(std::round(v * s) - v * s) < 1e-6) return d;
  }
  return maxDecimals;
}
}  // namespace

std::string formatValue(double value, int precision) {
  std::string body = fixed(value, precision);
  if (body.find('.') != std::string::npos) {
    while (body.size() > 1 && body.back() == '0') body.pop_back();
    if (body.back() == '.') body.pop_back();
  }
  if (body == "-0") body = "0";
  return body;
}

std::string formatDeviation(double value, int precision) {
  int dec = precision >= 0 ? precision : neededDecimals(value, 4);
  if (std::abs(value) < 0.5 * std::pow(10.0, -dec)) return "0";
  std::string s = fixed(std::abs(value), dec);
  return (value > 0 ? "+" : "-") + s;
}

ToleranceText composeTolerance(const DimTolerance& tol, double valueMm, const std::string& valueText,
                               const std::string& symbolPrefix, const std::string& suffix, bool lengthDim,
                               int precision, const core::UnitSystem& units) {
  ToleranceText t;
  t.main = valueText;
  auto disp = [&](double mm) {
    return lengthDim ? core::toDisplayValue(core::Quantity::mm(mm), units) : mm;
  };
  // deviations share the number of decimals (ISO 129-1), zero is written without a sign
  auto pairPrecision = [&](double a, double b) {
    if (tol.precision >= 0) return tol.precision;
    return std::max(neededDecimals(disp(a), 4), neededDecimals(disp(b), 4));
  };
  auto setDeviations = [&](double up, double lo) {
    if (std::abs(up + lo) < 1e-12 && std::abs(up) > 1e-12) {
      t.after = " \xC2\xB1" + formatDeviation(disp(up), pairPrecision(up, lo)).substr(1);
      return;
    }
    int p = pairPrecision(up, lo);
    t.upper = formatDeviation(disp(up), p);
    t.lower = formatDeviation(disp(lo), p);
  };
  auto setLimits = [&](double up, double lo) {
    int p = std::max(precision, pairPrecision(up, lo));
    t.upper = symbolPrefix + fixed(disp(valueMm + up), p) + suffix;
    t.lower = symbolPrefix + fixed(disp(valueMm + lo), p) + suffix;
    t.stackedMain = true;
    t.main.clear();
  };
  switch (tol.mode) {
    case ToleranceMode::None: break;
    case ToleranceMode::Symmetric: {
      int p = tol.precision >= 0 ? tol.precision : neededDecimals(disp(tol.upper), 4);
      t.after = " \xC2\xB1" + fixed(std::abs(disp(tol.upper)), p);
      break;
    }
    case ToleranceMode::Deviation: setDeviations(tol.upper, tol.lower); break;
    case ToleranceMode::Limits: setLimits(tol.upper, tol.lower); break;
    case ToleranceMode::Basic: t.framed = true; break;
    case ToleranceMode::Reference: t.main = "(" + t.main + ")"; break;
    case ToleranceMode::Fit: {
      std::string hz, sz;
      splitFit(tol.fit, hz, sz);
      std::string designation = hz.empty() ? sz : (sz.empty() ? hz : hz + "/" + sz);
      // deviations of the single zone (hole if given, else shaft) for the deviation displays
      std::optional<Deviations> dev;
      if (lengthDim && (hz.empty() != sz.empty())) dev = isoZoneDeviations(std::abs(valueMm), hz.empty() ? sz : hz);
      switch (tol.fitDisplay) {
        case FitDisplay::Designation: t.after = " " + designation; break;
        case FitDisplay::DesignationDeviations:
          t.after = " " + designation;
          if (dev) {
            int p = pairPrecision(dev->upper, dev->lower);
            t.upper = "(" + formatDeviation(disp(dev->upper), p);
            t.lower = formatDeviation(disp(dev->lower), p) + ")";
          }
          break;
        case FitDisplay::Deviations:
          if (dev) setDeviations(dev->upper, dev->lower);
          else t.after = " " + designation;
          break;
        case FitDisplay::Limits:
          if (dev) setLimits(dev->upper, dev->lower);
          else t.after = " " + designation;
          break;
      }
      break;
    }
  }
  return t;
}

}  // namespace fabricad::drawing
